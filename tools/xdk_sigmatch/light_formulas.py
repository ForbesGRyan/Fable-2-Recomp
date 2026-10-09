"""The game's light as closed formulas, one pure function per family of pixel shaders.

These are the reference the native renderer's C++ and HLSL are pinned to. Each was read from the
shader microcode and is checked against it by light_check.py on random inputs
(docs/native-renderer/lighting-formulas.md has the instruction numbers, what the check switches off
and the coverage). A family function returns linear RGB before fog and before the output scale
`c20.w`; colours are lists of three floats.

Where an input comes from (pixel constants are the frame's light block unless marked "per draw";
"vs" marks a vertex shader constant):

    A, F        the material's base colour, squared: tf0^2 (layers: with the steep-ground mix of
                frame-map section 13; objects: divided by tf0.a^2 under bool b128), far terrain tf13^2
    baked       terrain: the texel of the region's baked light map tf3 (0..1 per channel)
    vcolour     objects: interpolator o6 of the vertex shader (see vertex_colour, probe_colour)
    tint        c21.xyz         tint_pow    c21.w (1.3 in every captured row)
    add         c20.xyz         (output scale c20.w = 16 is not applied here)
    floor       c22.xyz, channel for channel: (c22.x, c22.y, c22.z) for red, green, blue
    t           ambient_t(axis, N): the clamped cosine between the normal and the family's axis
    n_dot_sun   sat(N . sun direction); which register and whether it is normalised differs per family
    sun_col     c87.xyz
    sun_gate    every other factor on the sun term: the screen mask tf8 (1 without shadows) and what
                sun_gate_single / sun_gate_multi compute
    c11x        c11.x, per draw: 1 at layer draws, the sun's scale at far draws
    c27w        c27.w, per draw: scale of the ambient part of objects
    c49         c49.xyz, per draw: factor on an object's whole colour before fog

Not in any function: after fog, objects, trunks and leaves multiply the colour by pixel c26.xyz (per
draw; their alpha by c26.w) together with the output scale c20.w. c26 reads (1, 1, 1, 1) in every
captured row of those shaders; a caller that draws them must apply it with c20.w.

Defaults. c11x and c27w default to 1: c11.x reads 1 at every captured layer row and c27.w at every
captured object, trunk and leaf row. The objects' and plants' sun_gate defaults to 1, a mask of 1 (no
shadows). An input whose value is open or in no
capture row has NO default and must be passed by keyword: the terrain layers' sun_gate (it carries
the facing factor of pixel c109), c49, and the leaves' m1 and m2. Leaving one out is a TypeError that
names it.

Plants and tree leaves compute their ambient (and leaves their sun) in the vertex shader from vertex
constants; instance_ambient, plant_normal, leaf_light and tree_leaf name those registers. In the
captured rows vs c18 = (pixel c21.xyz, 2.2 * pixel c21.w), vs c54 = pixel c20 and vs c19.xyz = pixel
c23.xyz, so the frame's light block covers them (an observation of two scenes, not a rule read from
the game).

Guards (review focus 2): a zero-length or non-finite vector normalises to (0, 0, 0), the power of a
zero or non-finite base is 0, a non-finite scalar counts as 0, and every function returns finite
numbers for any input. The microcode has no such guards (rsq(0) is infinity there); light_check.py
never feeds it those inputs and counts a non-finite microcode result as a failed trial.

The screen mask the families read is written by two pixel shaders of its own; their arithmetic is the
last part of this file (mask_near, mask_far, mask_combine, mask_0f99, mask_1b4b; the shadows
sub-project).
"""
import math


def _finite(x):
    return x if math.isfinite(x) else 0.0


def sat(x):
    """Clamp to [0, 1]; a non-finite value gives 0 (NaN) or the bound it exceeds."""
    if x != x:
        return 0.0
    return min(max(x, 0.0), 1.0)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normalize(v):
    """v / |v|; (0, 0, 0) for a zero-length or non-finite vector."""
    if not all(math.isfinite(x) for x in v):
        return [0.0] * len(v)
    n2 = dot(v, v)
    if n2 <= 0.0 or not math.isfinite(n2):
        return [0.0] * len(v)
    k = 1.0 / math.sqrt(n2)
    return [x * k for x in v]


def powc(base, exponent):
    """|base|^exponent per channel, as the microcode computes it: exp2(exponent * log2(|base|)). The
    absolute value is the shaders' `r_abs` operand; a zero or non-finite base, or a non-finite exponent,
    gives 0."""
    if not math.isfinite(exponent):
        return [0.0 for _ in base]
    out = []
    for b in base:
        b = abs(b)
        try:
            out.append(_finite(b ** exponent) if b > 0.0 and math.isfinite(b) else 0.0)
        except OverflowError:
            out.append(0.0)
    return out


def ambient_factor(floor, t):
    """L = floor + (1 - floor) * t^2 per channel, t clamped to [0, 1]."""
    t = sat(t)
    return [_finite(f) + (1.0 - _finite(f)) * t * t for f in floor]


def ambient_axis(normal, axis):
    """The vertex shaders' vector for t: lerp(normal, axis, (1 + normal . axis) / 2), not normalised.

    normal: the vertex normal as the vertex shader has it (terrain: the tf17 normal; objects: the
    world normal times vs c7.w); axis: vs c19.xyz (terrain layers and objects; equal to pixel c23.xyz in
    the captured rows). The 1/2 and the 1 are vertex shader literals (terrain c253.x, c254.y; objects
    c255.y, c255.z). The pixel shader normalises the interpolated vector (ambient_t)."""
    k = 0.5 + 0.5 * dot(normal, axis)
    return [_finite(n * (1.0 - k) + a * k) for n, a in zip(normal, axis)]


def ambient_t(axis_vector, normal):
    """t = sat(normalize(axis_vector) . normalize(normal)); 0 when either has no direction."""
    return sat(dot(normalize(axis_vector), normalize(normal)))


def vertex_colour(stream_colour, power):
    """Objects with a colour stream (vertex shader ECD66A10092E6562): o6 = pow(|vf3|, c255.x) per
    component, the vertex shader's literal c255.x = 2.2. stream_colour is the 8_8_8_8 element of stream
    vf3 as fractions 0..1, in the order the fetch delivers it to r3 (swizzle `zyxw`)."""
    return powc(stream_colour, power)


def probe_colour(base, rows, weights):
    """Objects without a colour stream (vertex shaders 79EAC49585797037, D4D558DA6A82BDC8,
    A1F7E9885EC466DF): o6 = base + (rows[0] . weights, rows[1] . weights, rows[2] . weights), four-component
    dot products. weights is the 32_32_32_32_FLOAT element of stream vf1; base is vs c[28 + a0].xyz and
    rows are vs c[29 + a0], c[30 + a0], c[31 + a0], with a0 the fourth component of the position element
    (0, 4 or 8 in the captured vertices). No power in the vertex shader."""
    return [_finite(_finite(base[i]) + dot(rows[i], weights)) for i in range(3)]


def facing_gate(n_dot_c109, k=8.0):
    """Terrain layer shaders only: sat(k * (N . c109.xyz)), a ramp that takes the sun (and with it the
    mask) off ground that faces away from c109. c109 is a pixel constant (a direction; it reads zero in
    the capture rows, as c80 does), k the pixel shader's literal 8. Objects, plants and far terrain have
    no such factor (1)."""
    return sat(_finite(k) * _finite(n_dot_c109))


def sun_gate_single(mask=1.0, facing=1.0):
    """One-light shaders: facing * mask, mask the texel of the screen mask tf8 (1 = lit; 1 while the
    native pass draws no shadows)."""
    return sat(facing) * sat(mask)


def sun_gate_multi(shadow_strength, mask=1.0, facing=1.0, spot_blend=0.0, distance=0.0, range_scale=0.0,
                   cone_dot=0.0, cone_cos=0.0, cone_scale=0.0):
    """Two- and four-light shaders, light 0 with bool b135 clear:
        (1 - shadow) * (1 + spot_blend * (cone * range - 1)),
        shadow = (1 - facing * mask) * shadow_strength,
        range = sat(1 - distance * range_scale),  cone = sat((cone_dot - cone_cos) * cone_scale).
    shadow_strength c112.x, spot_blend c94.x, range_scale c96.x, cone_cos c105.x, cone_scale c107.x;
    distance = |c80.xyz - P * c80.w| and cone_dot = (that vector, normalised) . c98.xyz, P the world
    position. With shadow_strength 1 and spot_blend 0 this is sun_gate_single."""
    shadow = (1.0 - sat(facing) * sat(mask)) * _finite(shadow_strength)
    rng = sat(1.0 - _finite(distance) * _finite(range_scale))
    cone = sat((_finite(cone_dot) - _finite(cone_cos)) * _finite(cone_scale))
    return _finite((1.0 - shadow) * (1.0 + _finite(spot_blend) * (cone * rng - 1.0)))


def _ambient(base, tint, tint_pow, add, floor, t):
    """pow(base, tint_pow) * tint * L + add, per channel."""
    lit = ambient_factor(floor, t)
    p = powc(base, tint_pow)
    return [p[i] * _finite(tint[i]) * lit[i] + _finite(add[i]) for i in range(3)]


def _sun(sun_col, n_dot_sun, sun_gate):
    k = sat(n_dot_sun) * _finite(sun_gate)
    return [_finite(c) * k for c in sun_col]


def terrain_layer(A, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x=1.0, *, sun_gate):
    """Terrain layer shaders: A * c11x * (pow(baked, tint_pow) * tint * L + add) + A * sun_col * n_dot_sun
    * sun_gate. n_dot_sun: one-light shaders sat(N . c80.xyz) with c80 as it is; multi-light shaders
    sat(N . normalize(c80.xyz - P * c80.w)). N is the unit normal the pixel shader lights with (with a
    flat normal map: the vertex shader's tf17 normal, normalised). sun_gate has no default: every layer
    shader multiplies the sun by the facing factor (sun_gate_single / sun_gate_multi with facing_gate),
    and 1 would be arithmetic none of them computes. c11x defaults to the 1 of every captured layer row."""
    amb = _ambient(baked, tint, tint_pow, add, floor, t)
    sun = _sun(sun_col, n_dot_sun, sun_gate)
    return [_finite(_finite(A[i]) * (_finite(c11x) * amb[i] + sun[i])) for i in range(3)]


def terrain_far(F, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x):
    """Far (adaptive) terrain: F * (pow(baked, tint_pow) * tint * L + add) + F * c11x * sun_col *
    n_dot_sun. t = sat(N . c23.xyz) and n_dot_sun = sat(N . c80.xyz), N the unit tf17 normal, both
    constants as they are. No mask and no c109 term. Shader 2F8A... multiplies the result by its fade
    sat(d2 * c31.x + c31.y) (frame-map section 13), which is not part of this function."""
    amb = _ambient(baked, tint, tint_pow, add, floor, t)
    sun = _sun(sun_col, n_dot_sun, 1.0)
    return [_finite(_finite(F[i]) * (amb[i] + _finite(c11x) * sun[i])) for i in range(3)]


def instance_ambient(instance_colour, tint, tint_pow, add):
    """Instanced plants whose pixel shader has no ambient factor (interpolator o4 of vertex shader
    8123C16DBF583F92): pow(instance_colour, tint_pow) * tint + add. instance_colour is the 8_8_8_8 element
    of the instance stream (vf1, dword 6) as fractions, channels (z, y, x) of the fetch; tint vs c18.xyz,
    tint_pow vs c18.w, add vs c54.xyz. In the captured rows vs c18 is (pixel c21.xyz, 2.2 * pixel c21.w):
    the objects' vertex colour power and ambient exponent in one."""
    p = powc(instance_colour, tint_pow)
    return [_finite(p[i] * _finite(tint[i]) + _finite(add[i])) for i in range(3)]


def plant_normal(normal, up, height, strength):
    """The normal the plant pixel shaders light with: the instance-rotated vertex normal bent towards the
    instance's up axis with height.
        bent = normalize(lerp(normal, up, height))                 (vertex shader)
        N    = normalize(lerp(normal, strength * bent, sat(strength * height)))      (pixel shader)
    normal: normalize(M n), M the instance's 3x3 (the rows that transform the position); up: M's third
    column, normalised; height = sat(z * c45.y + c45.z), z the vertex position's third component in the
    instance's space; strength vs c45.x. The interpolators carry (strength * bent, strength * height)."""
    h = sat(height)
    bent = normalize([n + (u - n) * h for n, u in zip(normal, up)])
    k = sat(_finite(strength) * h)
    return normalize([n + (_finite(strength) * b - n) * k for n, b in zip(normal, bent)])


def plant(A, vs_ambient, n_dot_sun, sun_col, sun_gate=1.0):
    """Instanced plants without an ambient factor (A7E4..., 2605..., E99F...): A * vs_ambient + A * sun_col
    * n_dot_sun * sun_gate. A = tf0.rgb^2 / tf0.a^2 (always, no bool); vs_ambient the vertex shader's
    interpolator (instance_ambient); n_dot_sun = sat(plant_normal . c80.xyz)."""
    sun = _sun(sun_col, n_dot_sun, sun_gate)
    return [_finite(_finite(A[i]) * (_finite(vs_ambient[i]) + sun[i])) for i in range(3)]


def leaf_light(normal, view, light_dir, wrap, spot=1.0):
    """The leaf vertex shaders' two scalars of light 0, (front, back):
        Nf    = the normal, negated when it faces away from the viewer (view . normal < 0)
        front = sat((light_dir . Nf + wrap) / (1 + wrap)) * spot
        back  = sat(-(light_dir . Nf)) * (1 - (1 - sat(view . Nf))^2) * spot
    normal: the world normal (c4..c6 rows times the 10_11_11 element, times c7.w); view: unit vector from
    the vertex to the eye vs c9; light_dir: normalize(vs c76.xyz - P * vs c76.w); wrap vs c50.w; spot =
    1 + vs c90.x * (cone * range - 1) with range = sat(1 - |light vector| * vs c92.x) and cone =
    sat((light_dir . vs c94.xyz - vs c101.x) * vs c103.x)."""
    n = [_finite(x) for x in normal]
    if dot(view, n) < 0.0:
        n = [-x for x in n]
    nl = dot(light_dir, n)
    w = _finite(wrap)
    front = sat((nl + w) / (1.0 + w)) if (1.0 + w) != 0.0 else 0.0
    back = sat(-nl) * (1.0 - (1.0 - sat(dot(view, n))) ** 2)
    return _finite(front * _finite(spot)), _finite(back * _finite(spot))


def leaf_mask(mask, gain, bias):
    """The leaf pixel shader's two factors from the screen mask: m1 = mask * gain + bias on the front
    light, m2 = m1 * gain + bias on the back light. gain and bias are the pixel shader's literals c255.w
    and c255.z: 0.6 and 0.4 in every execution of 0x7CD5... in the GPU logs of plan Task 1 (frame-map
    section 14, "The shaders' literals"; in no capture row when this was written)."""
    m1 = _finite(sat(mask) * _finite(gain) + _finite(bias))
    return m1, _finite(m1 * _finite(gain) + _finite(bias))


def tree_leaf(A, vs_ambient, front, back, sun_col, c27w=1.0, *, m1, m2, c49):
    """Tree leaves (pixel shader 7CD5... with vertex shader A584...): everything but the texture and the
    mask is computed per vertex.
        (A * (vs_ambient * c27w + m1 * sun_col * front) + green * A * m2 * sun_col * back) * c49
    green = 1 where the texel's A has green >= red, else 0: the back light passes through leaf texels
    only. A = tf0.rgb^2 / tf0.a^2; vs_ambient = instance_ambient(probe_colour, vs c18.xyz, vs c18.w *
    0.4545 (literal c249.w), vs c54.xyz); front, back = leaf_light(...); sun_col vs c83.xyz; m1, m2 =
    leaf_mask(...). The interpolators are o1 = vs_ambient, o2 = sun_col * front, o3 = sun_col * back.
    m1, m2 and c49 have no default: the caller makes m1 and m2 (leaf_mask with the literals 0.6 and
    0.4) and c49 is in no capture row of the time. c27w defaults to the 1 of every captured leaf row."""
    green = 1.0 if _finite(A[1]) >= _finite(A[0]) else 0.0
    out = []
    for i in range(3):
        a, s = _finite(A[i]), _finite(sun_col[i])
        lit = a * (_finite(vs_ambient[i]) * _finite(c27w) + _finite(m1) * s * _finite(front))
        out.append(_finite((lit + green * a * _finite(m2) * s * _finite(back)) * _finite(c49[i])))
    return out


def lit_object(A, vcolour, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c27w=1.0, sun_gate=1.0, *, c49):
    """Lit objects: (A * c27w * (pow(vcolour, tint_pow) * tint * L + add) + A * sun_col * n_dot_sun *
    sun_gate) * c49. The terrain's ambient with the vertex shader's colour o6 in the place of the baked
    map; c27w scales the ambient part only. Without the specular, emissive (b133) and cube (b134) terms.
    c49 has no default: it is in no capture row (the lit plants, whose shaders have no c49, pass ones).
    c27w defaults to the 1 of every captured object and trunk row, sun_gate to a mask of 1."""
    amb = _ambient(vcolour, tint, tint_pow, add, floor, t)
    sun = _sun(sun_col, n_dot_sun, sun_gate)
    return [_finite(_finite(A[i]) * (_finite(c27w) * amb[i] + sun[i]) * _finite(c49[i])) for i in range(3)]


# --- the screen mask (shadows sub-project) --------------------------------------------------------
#
# The game writes one screen-size mask (1 = lit) with two pixel shaders before the colour draws; the lit
# families above read it as `mask`. The functions below are those two shaders, read from the dumps and
# checked against them by light_check.py --family mask / mask-near / mask-far
# (docs/native-renderer/lighting-formulas.md, "Shadows", has the instruction numbers and the inputs).
#
# A cascade's rows are four four-component rows applied to a homogeneous point P: (u, v, depth) =
# (rows[0] . P, rows[1] . P, rows[2] . P) / (rows[3] . P). The game hands the mask shaders rows for the
# SCREEN point (mask_screen_point): its pixel registers c36..c39 (near) and c40..c43 (far) at the mask
# draws. With the world-to-map rows and the world position (x, y, z, 1) the functions give the same value
# (the screen rows are the world rows times the screen-to-world rows; tests/test_light_check.py).
#
# The maps hold 1 - depth of the surface nearest the light (reversed range): a point is lit where its own
# 1 - depth is beyond the map's value. The near cascade compares with `>`, the far one with `>=`.
#
# Guards (review focus 2): a projection whose w is zero or whose u, v or depth is not finite gives 1 for
# that cascade (no shadow from a map that cannot be addressed) without a sample being taken; a tap whose
# coordinates are not finite counts as lit; every function returns a finite number for any input. The
# microcode has no such guards; light_check.py never feeds it those inputs.

# The twelve near taps of shader 0F99810F072BF0FC, in units of the kernel radius: its literal registers
# c250..c255 as the emulated GPU held them at every execution of the shader in four logs (single precision,
# shown exactly; out/native-evidence/shadows/task2_gpu_log_mask_registers.txt), paired as the microcode
# pairs them (light_check.mask_taps). They are the 12-tap Poisson disc of the published samples.
MASK_NEAR_TAPS = (
    (-0.20334500074386597, 0.6207159757614136),
    (0.18546099960803986, -0.893123984336853),
    (0.9623399972915649, -0.19498300552368164),
    (0.5194560289382935, 0.7670220136642456),
    (-0.6959139704704285, 0.45713698863983154),
    (0.5074309706687927, 0.06442499905824661),
    (-0.8401439785957336, -0.07357999682426453),
    (0.8964200019836426, 0.41245800256729126),
    (0.4734340012073517, -0.4800260066986084),
    (-0.7915589809417725, -0.5977100133895874),
    (-0.32621198892593384, -0.40580999851226807),
    (-0.32194000482559204, -0.9326149821281433),
)


def mask_rotate(offset, rot):
    """A tap offset (x, y) turned by the pixel's rotation terms (a, b): (a x + b y, a y - b x). (a, b) =
    2 * texel.xy - 1 of the rotation texture (mask_0f99); nothing normalises it."""
    return (rot[0] * offset[0] + rot[1] * offset[1], rot[0] * offset[1] - rot[1] * offset[0])


def _mask_project(rows, P):
    """(u, v, depth) of P in a cascade, or None when w is zero or a result is not finite."""
    w = dot(rows[3], P)
    if not math.isfinite(w) or w == 0.0:
        return None
    out = [dot(rows[i], P) / w for i in range(3)]
    return out if all(math.isfinite(x) for x in out) else None


def mask_near(P, near_rows, sample_near, rot, offsets, radius, tap_weight=None):
    """The near cascade's term: the share of taps that are lit.
        (u, v, depth) = near_rows . P / w
        tap i at (u, v) + radius * mask_rotate(offsets[i], rot); lit where 1 - depth > sample_near(tap)
        result = lit taps * tap_weight
    sample_near(u, v): the near map's value at normalised coordinates; offsets: MASK_NEAR_TAPS; radius:
    pixel c77.x at the draw, in normalised map units (2.5 texels of 1024); tap_weight: the shader's literal
    1 / 12 (c248.y), by default 1 / len(offsets). Not clamped."""
    weight = (1.0 / len(offsets) if offsets else 0.0) if tap_weight is None else _finite(tap_weight)
    p = _mask_project(near_rows, P)
    if p is None or not offsets:
        return 1.0
    u, v, depth = p
    lit = 0
    for offset in offsets:
        ox, oy = mask_rotate(offset, rot)
        tu, tv = u + radius * ox, v + radius * oy
        if not (math.isfinite(tu) and math.isfinite(tv)) or 1.0 - depth > sample_near(tu, tv):
            lit += 1
    return _finite(lit * weight)


def mask_far(P, far_rows, sample_far_texel, size):
    """The far cascade's term: the linear filter of four lit / shadowed texels.
        (u, v, depth) = far_rows . P / w;  x = u * width, y = v * height
        texels (floor(x) - 1 .. floor(x)) x (floor(y) - 1 .. floor(y)), each lit where
        1 - depth >= sample_far_texel(ix, iy); weights frac(x) and frac(y) towards the higher texel.
    sample_far_texel(ix, iy): the far map's texel (integers; the caller clamps); size: (width, height) of
    the map in texels, 1024 x 1024 in the game. In the microcode these are four point fetches at texel
    offsets (-1, -1), (0, -1), (-1, 0), (0, 0) and getWeights2D with offsets of -0.5."""
    try:
        width, height = float(size[0]), float(size[1])
    except (TypeError, ValueError, IndexError):
        return 1.0
    if not (math.isfinite(width) and math.isfinite(height) and width > 0.0 and height > 0.0):
        return 1.0
    p = _mask_project(far_rows, P)
    if p is None:
        return 1.0
    x, y, depth = p[0] * width, p[1] * height, p[2]
    if not (abs(x) < 2.0 ** 31 and abs(y) < 2.0 ** 31):
        return 1.0
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy

    def lit(dx, dy):
        return 1.0 if 1.0 - depth >= sample_far_texel(ix + dx, iy + dy) else 0.0

    return _finite(fx * fy * lit(0, 0) + (1.0 - fx) * fy * lit(-1, 0) + (1.0 - fx) * (1.0 - fy) * lit(-1, -1)
                   + fx * (1.0 - fy) * lit(0, -1))


def mask_combine(d, near, far, split, slope):
    """The two cascades and the fade:
        m = lerp(near, far, sat((d - split[0]) / split[2]));  result = lerp(m, split[3], sat(slope * (d - split[1])))
    d: the distance of the surface from the eye; split: pixel c12 (20, 35, 2, 0.46 to 0.49 in the logs): the
    near cascade's reach, the far cascade's, the width of the blend, and the value beyond the shadow
    distance, which is NOT 1; slope: the shader's literal 0.1 (c249.w). A blend width that is not positive is
    a step at split[0]."""
    s = [_finite(x) for x in split]
    slope, near, far = _finite(slope), _finite(near), _finite(far)
    if d != d:
        d = 0.0
    if s[2] > 0.0:
        w = sat((d - s[0]) / s[2])
    else:
        w = 1.0 if d > s[0] else 0.0
    m = near + w * (far - near)
    fade = sat(slope * (d - s[1]))
    return _finite(m + fade * (s[3] - m))


def mask_depth_uv(pixel, scale, offset):
    """Where the mask shaders read the scene depth: ((|px| + 0.5 + offset.x) * scale.x, the same in y).
    pixel: the position register's xy (the pixel's corner); scale: 1 / the target's size; offset: (0, 0)
    in the logs."""
    return ((abs(pixel[0]) + (0.5 + offset[0])) * scale[0], (abs(pixel[1]) + (0.5 + offset[1])) * scale[1])


def mask_screen_point(pixel, depth, scale, offset):
    """The homogeneous screen point the mask shaders apply their rows to: (X, -Y, 1 - depth, 1) with
    X = (2 |px| + 1 + 2 offset.x) * scale.x - 1 and Y likewise: the pixel's centre in -1..1, y up, and the
    depth turned to 0 at the near plane (the depth target holds the reversed value)."""
    x = (2.0 * abs(pixel[0]) + (1.0 + 2.0 * offset[0])) * scale[0] - 1.0
    y = (2.0 * abs(pixel[1]) + (1.0 + 2.0 * offset[1])) * scale[1] - 1.0
    return [x, -y, 1.0 - depth, 1.0]


def mask_0f99(*, pixel, sample_depth, depth_scale, depth_offset, screen_to_world, eye, split, near_rows, far_rows,
              sample_rot, rot_offset, rot_scale, offsets, radius, sample_near, sample_far_texel, far_size,
              tap_weight=None, far_reach=10.0, slope=0.1):
    """Pixel shader 0F99810F072BF0FC, the whole mask of a pixel, branches as the microcode takes them:
        depth = sample_depth(mask_depth_uv(pixel));  S = mask_screen_point(pixel, depth)
        P = screen_to_world . S / w;  d = |P - eye|
        near = mask_near(S, ...) where d < split[0] + split[2], else 0
        far  = mask_far(S, ...)  where split[0] < d < split[1] + far_reach, else 0
        result = mask_combine(d, near, far, split, slope)
    Registers at the draw: depth_scale, depth_offset c46.xy, c46.zw; screen_to_world c116..c119; eye
    c31.xyz; split c12; near_rows c36..c39 and far_rows c40..c43 (for the screen point); the rotation texel
    is sample_rot((|px| + rot_offset.x) * rot_scale.x, ...) with c47.xy, c47.zw, and (a, b) = 2 * texel.xy
    - 1; radius c77.x; tap_weight, far_reach and slope the literals c248.y, c248.x, c249.w (1 / 12, 10,
    0.1). A pixel without a finite world position gives split[3], as one beyond the shadow distance."""
    s = [_finite(x) for x in split]
    u, v = mask_depth_uv(pixel, depth_scale, depth_offset)
    S = mask_screen_point(pixel, sample_depth(u, v), depth_scale, depth_offset)
    world = _mask_project(screen_to_world, S)
    if world is None:
        return s[3]
    d = math.sqrt(sum((world[i] - eye[i]) * (world[i] - eye[i]) for i in range(3)))
    if not math.isfinite(d):
        return s[3]
    near = far = 0.0
    if s[0] + s[2] > d:
        texel = sample_rot((abs(pixel[0]) + rot_offset[0]) * rot_scale[0], (abs(pixel[1]) + rot_offset[1]) * rot_scale[1])
        rot = (2.0 * texel[0] - 1.0, 2.0 * texel[1] - 1.0)
        near = mask_near(S, near_rows, sample_near, rot, offsets, radius, tap_weight)
    if d > s[0] and s[1] + _finite(far_reach) > d:
        far = mask_far(S, far_rows, sample_far_texel, far_size)
    return mask_combine(d, near, far, s, slope)


def mask_1b4b(*, pixel, sample_depth, depth_scale, depth_offset, far_rows, sample_far_texel, far_size, start,
              fade_start, fade_width, fade_to):
    """Pixel shader 1B4B5AE12EF270CA, the far cascade alone for distant pixels: (value, alpha).
        depth = sample_depth(mask_depth_uv(pixel));  z = 1 - depth
        z <= start:  (fade_to, 0)
        else:        (lerp(mask_far(S, ...), fade_to, sat((z - fade_start) / fade_width)), 1)
    It measures by the depth value where 0F99... measures by distance. Registers at the draw: depth_scale,
    depth_offset c31.xy, c31.zw; far_rows c40..c43; start c46.x; fade_start c47.x; fade_width c77.x; fade_to
    c12.w. A fade width of zero is a step at fade_start."""
    fade_to = _finite(fade_to)
    u, v = mask_depth_uv(pixel, depth_scale, depth_offset)
    depth = sample_depth(u, v)
    if not ((1.0 - depth) - start > 0.0):
        return fade_to, 0.0
    far = mask_far(mask_screen_point(pixel, depth, depth_scale, depth_offset), far_rows, sample_far_texel, far_size)
    t = (1.0 - fade_start) - depth
    if math.isfinite(fade_width) and fade_width != 0.0:
        fade = sat(t * (1.0 / fade_width))
    else:
        fade = 1.0 if t > 0.0 else 0.0
    return _finite(far + fade * (fade_to - far)), 1.0
