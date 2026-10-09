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
part after the families (mask_near, mask_far, mask_combine, mask_0f99, mask_1b4b; the shadows
sub-project).

The fog block every family's shader runs on its colour, the trees' vertex form of it, the sky's pixel
shader and the list of fogged pixel shaders are the last part (table_1d, fog, vertex_fog, fog_apply, sky,
FOG_SHADERS; the fog sub-project).
"""
import collections
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


# --- fog, in-scatter and sky (fog sub-project) ------------------------------------------------------
#
# After its lit colour and before the output scale c20.w, every fogged pixel shader runs one block: distance
# fog and in-scatter from two 1D tables the game rewrites every frame (fog, below). The trees compute the
# same two quantities per vertex from two other tables and their pixel shaders apply them (vertex_fog,
# fog_apply). The sky is one pixel shader of its own whose clear term is the same in-scatter table (sky).
# All of it is read from the dumps and checked against them by fog_check.py
# (docs/native-renderer/lighting-formulas.md, "Fog and sky", has the instruction numbers and what is not
# checked).
#
# A table is a callable: tf4(u), tf5(u), tf16(u) return the texel at a 1D coordinate (three components or
# more, red first, as the shader's register receives them: the swizzle of the texture's fetch constant
# is the table decode's business, not this file's); tf17(a, t), tf13(u, v), tf14(u, v) take two
# coordinates. table_1d makes such a callable from a list of texels with the addressing of the game's
# fetch.
#
# Guards (review focus 2). Nothing here raises on numbers.
#   - fog, vertex_fog: a fog input that is not finite (the position, the eye, a register the block reads,
#     the alpha of the blended form, a texel the two fetches return) or a result that is not finite leaves
#     the pixel UNFOGGED: the colour comes back as it went in (factor 1 and add 0 for the vertex form),
#     which is how the product draws a frame without a valid fog block. A colour channel that is not
#     finite counts as 0, as in the families above.
#   - sky: there is no unfogged colour to fall back to, so a register, a direction or a texel that is not
#     finite counts as 0 and the result is finite.
#   - a zero axis c66 reads the in-scatter table at 0; a pixel at the eye (d = 0) is inside the start for
#     any c67.x >= 0 and has no view direction otherwise (the angle then reads 0); a coordinate beyond a
#     table's end reads its last texel.
# The microcode has no such guards; fog_check.py feeds it finite inputs.

def table_1d(texels):
    """A 1D table as tfetch1D reads it with a linear filter and clamped addressing: a callable u -> texel.

    Texel i of N has its centre at u = (i + 0.5) / N. Between two centres the result is their linear
    blend; u <= 0.5 / N reads texel 0 alone and u >= 1 - 0.5 / N the last texel alone. That is the
    emulated game's addressing: the SDK's translator hands the normalised coordinate of a 1D fetch to
    the host sampler unchanged but for its own rounding offset of 1.5 / 1024 of a texel, with the texture
    bound as a 2D array one texel high (thirdparty/rexglue-sdk, dxbc_translator_fetch.cpp: "1D and 2D
    textures ... are treated as 2D arrays", the offsets block with `rounding_offset`, "Pad to 2D array
    coordinates"); the filter and the clamp are the fetch constant's (linear and clamp to edge for tf4 and
    tf5 in every captured row). The rounding offset is the emulator's own and is not modelled here.
    A coordinate that is not a number reads texel 0. An empty table is the caller's error (ValueError)."""
    rows = [list(t) for t in texels]
    if not rows:
        raise ValueError("a table needs at least one texel")
    n = len(rows)

    def fetch(u):
        if u != u:
            return list(rows[0])
        x = min(max(min(max(u, 0.0), 1.0) * n - 0.5, 0.0), n - 1.0)
        i = min(int(math.floor(x)), n - 1)
        f = x - i
        if f == 0.0:
            return list(rows[i])
        return [p + (q - p) * f for p, q in zip(rows[i], rows[min(i + 1, n - 1)])]

    return fetch


def _all_finite(*values):
    return all(math.isfinite(x) for x in values)


def fog_coords(P, eye, c66, c67):
    """The fog block's three coordinates (a, s, t), or None where the block is skipped.
        V = eye - P;  d = |V|;  dD = d - c67.x;  skipped unless dD > 0
        a = sat((-V / d) . c66.xyz)        the in-scatter table's coordinate: the angle to the axis
        s = c67.w * dD                     the transmittance table's coordinate (not clamped here)
        t = sat(c67.z * dD)                the blend from c68 to the in-scatter table
    P: the pixel's world position; eye: pixel c19.xyz. c67.y is not read. Also None when an input or
    the distance is not finite (the guards above)."""
    if not _all_finite(*P[:3], *eye[:3], *c66[:3], c67[0], c67[2], c67[3]):
        return None
    V = [eye[i] - P[i] for i in range(3)]
    d2 = dot(V, V)
    if not math.isfinite(d2):
        return None
    dD = math.sqrt(d2) - c67[0]
    if not dD > 0.0:
        return None
    a = sat(dot(normalize([-x for x in V]), c66[:3]))
    s, t = c67[3] * dD, sat(c67[2] * dD)
    return (a, s, t) if _all_finite(a, s, t) else None


def fog(C, P, eye, c28, c66, c67, c68, tf4, tf5, scatter_scale=1.0):
    """The fog block of the pixel shaders of FOG_SHADERS: the colour C (linear, before the output scale)
    fogged.
        (a, s, t) = fog_coords(P, eye, c66, c67);  inside the start: C unchanged, no table read
        S = tf4(a).rgb                     in-scatter colour by angle
        T = tf5(s).rgb                     transmittance by distance, per channel
        F = c68.rgb + t * (S - c68.rgb)
        G = C * T + (1 - T) * c28.y * scatter_scale * F
        C' = C + c28.x * (G - C)
    scatter_scale: 1 in the plain form; in the blended form ("alpha" in FOG_SHADERS) the pixel's alpha,
    the shader's output alpha divided by c26.w. Alpha itself is not touched by the block. The shader then
    multiplies C' by c20.w (and by c26.xyz where it reads c26; 2F8A... also by its cross-fade)."""
    base = [_finite(x) for x in C[:3]]
    coords = fog_coords(P, eye, c66, c67)
    if coords is None or not _all_finite(c28[0], c28[1], scatter_scale, *c68[:3]):
        return base
    a, s, t = coords
    S, T = list(tf4(a)[:3]), list(tf5(s)[:3])
    if not _all_finite(*S, *T):
        return base
    scale = c28[1] * scatter_scale
    out = []
    for i in range(3):
        F = c68[i] + t * (S[i] - c68[i])
        G = base[i] * T[i] + (1.0 - T[i]) * scale * F
        out.append(base[i] + c28[0] * (G - base[i]))
    return out if _all_finite(*out) else base


def vertex_fog(P, eye, c8x, c110, c111, tf16, tf17):
    """The trees' fog, computed per vertex: (factor, add), each three floats.
        (a, s, t) = fog_coords(P, eye, c110, c111)        vertex c110, c111: equal to pixel c66, c67 in the rows
        inside the start: ((1, 1, 1), (0, 0, 0)), no table read
        T = tf16(s).rgb;  S2 = tf17(a, t).rgb             a 2D fetch: the angle and the near blend
        factor = T;  add = (1 - T) * S2 * c8x
    P: the vertex's world position after the wind; eye: vertex c9.xyz; c8x: vertex c8.x (the place of pixel
    c28.y; there is no amount c28.x in this form). The pixel shader applies the pair with fog_apply.
    What the two tables are is settled by the captures (frame-map section 16, "The trees' tables": 28 of
    28 frames): tf16 is tf5 itself and tf17 is tf4 itself, a 1D texture fetched with a 2D fetch, so the
    second coordinate addresses nothing and tf17(a, t) = tf4(a) for every t. The trees therefore have no
    blend from c68: this is not fog() at the vertex but T = tf5(s), add = (1 - T) * tf4(a) * c8x (ruling B);
    it would be fog() with c28.x = 1 only if tf17(a, t) were c68 + t * (tf4(a) - c68), which a test holds
    as arithmetic (tests/test_fog_formulas.py) and the game's table is not."""
    one, zero = [1.0, 1.0, 1.0], [0.0, 0.0, 0.0]
    coords = fog_coords(P, eye, c110, c111)
    if coords is None or not math.isfinite(c8x):
        return one, zero
    a, s, t = coords
    T, S2 = list(tf16(s)[:3]), list(tf17(a, t)[:3])
    add = [(1.0 - T[i]) * S2[i] * c8x for i in range(3)]
    if not _all_finite(*T, *S2, *add):
        return one, zero
    return T, add


def fog_apply(C, factor, add):
    """What the tree pixel shaders do with the vertex shader's pair under bool b132: C * factor + add (trunks:
    interpolators o6 and o7; leaves: o4 and o5). C is the family's result (lit_object, tree_leaf: c49
    included); the output scale c20.w and c26 follow."""
    return [_finite(_finite(C[i]) * _finite(factor[i]) + _finite(add[i])) for i in range(3)]


# The sky's pixel shader and its literal registers c250..c255: the emulated GPU's registers at every
# execution of the shader in frame 1116 of the GPU log of the fog research (gpu_shadow_bridge2.log: six
# draws, two in the main scene and four in the small reflection passes; single precision, shown exactly;
# the hex is in the document). They are the coefficients of an arctangent and an arccosine polynomial,
# 1 / (2 pi), 2 / pi, pi, pi / 2 (each as the game rounded it: 0.63663852 is not the single nearest to
# 2 / pi) and 1 / ln 2.
SKY_SHADER = "F7601FBE485936FD"
SKY_LITERALS = {
    250: (-0.018729299306869507, 0.18014100193977356, 0.5, 1.0499999523162842),
    251: (-3.1415927410125732, -0.08513300120830536, 3.1415927410125732, -2.0),
    252: (1.5707963705062866, 1.5707287788391113, -0.2121143937110901, 0.9998660087585449),
    253: (1.0, 0.0, 0.07426100224256516, -0.3302994966506958),
    254: (0.20000000298023224, 0.02083509974181652, 0.6366385221481323, 0.15915963053703308),
    255: (1.4426950216293335, 0.0, 0.0, 0.0),
}


def sky_latlong(n):
    """The sky textures' coordinate of a direction: (u, v) = (0.5 + atan2(n.y, n.x) / (2 pi),
    asin(n.z) * 2 / pi), computed as the shader computes it, with its polynomials and its literals
    (within 5e-5 of the exact functions).
        q  = min(|x|, |y|) / max(|x|, |y|);  at = q * (0.99987 - 0.33030 q^2 + 0.18014 q^4 - 0.08513 q^6 + 0.02084 q^8)
        |y| > |x|: at = pi / 2 - at;  x < 0: at = at - pi;  x and y of different sign: at = -at
        ac = sqrt(1 - |z|) * (1.57073 - 0.21211 |z| + 0.07426 |z|^2 - 0.01873 |z|^3);  z < 0: ac = pi - ac
        u  = 0.5 + at * 0.15916;  v = (pi / 2 - ac) * 0.63664
    n need not be a unit vector. The seam: u is 1 (1.0000148 with these literals) on the half plane y = 0,
    x < 0 and just north of it (y > 0), and 0 (-0.0000148) just south: "different sign" is
    min(x, y) < 0 <= max(x, y), so y = 0 counts as positive. v runs from -1.00003 at the nadir to 1.00003
    at the zenith; the upper hemisphere is v in 0..1. The poles: x = y = 0 makes q the product 0 * (1 / 0),
    which is 0 by the console's product rule (ucode_eval.py, zero_product), so u = 0.5 there; a direction of
    no length or not finite counts as (0, 0, 0)."""
    L = SKY_LITERALS
    x, y, z = normalize([n[0], n[1], n[2]])
    ax, ay, az = abs(x), abs(y), abs(z)
    hi, lo = max(ax, ay), min(ax, ay)
    q = lo * (1.0 / hi) if hi > 0.0 else 0.0
    q2 = q * q
    at = q * (q2 * (q2 * (q2 * (q2 * L[254][1] + L[251][1]) + L[250][1]) + L[253][3]) + L[252][3])
    if ay > ax:
        at = at + (L[251][3] * at + L[252][0])
    if x < 0.0:
        at = at + L[251][0]
    if min(x, y) < 0.0 and max(x, y) >= 0.0:
        at = at - (at + at)
    root = math.sqrt(abs(1.0 - az))
    poly = az * (az * (az * L[250][0] + L[253][2]) + L[252][2]) + L[252][1]
    ac = poly * root
    if z < 0.0:
        ac = ac + (L[251][3] * root * poly + L[251][2])
    return L[250][2] + at * L[254][3], (L[252][0] - ac) * L[254][2]


def sky_paths(n, c67y, c31x, c46x):
    """The two path lengths of the sky's extinction, from the direction's elevation:
        k = |n.z|^0.2;  h = c67y * (1.05 - k);  l1 = h + (c31x - h) * k;  l2 = h + (c46x - h) * k
    At the zenith (c31x, c46x); at the horizon both 1.05 * c67y; mirrored below the horizon. c67y: pixel
    c67.y (1900 in every capture); c31x, c46x: pixel c31.x, c46.x at the sky draw (150 and 100 in the one
    frame read)."""
    L = SKY_LITERALS
    z = abs(normalize([n[0], n[1], n[2]])[2])
    k = z ** L[254][0] if z > 0.0 else 0.0
    h = _finite(c67y) * (L[250][3] - k)
    return _finite(h + (_finite(c31x) - h) * k), _finite(h + (_finite(c46x) - h) * k)


def sky_extinction(l1, l2, c64, c65):
    """E = exp(-(l1 * c64.rgb + l2 * c65.rgb)) per channel (the shader: exp2 of the sum times its literal
    1.4426950, 1 / ln 2). c64, c65: pixel registers, one value a frame."""
    out = []
    for i in range(3):
        x = -(_finite(l1) * _finite(c64[i]) + _finite(l2) * _finite(c65[i])) * SKY_LITERALS[255][0]
        try:
            out.append(_finite(2.0 ** x))
        except OverflowError:
            out.append(0.0)
    return out


def sky(n, block, tf4, tf13, tf14):
    """Pixel shader F7601FBE485936FD, the sky, as linear RGB before the output scale c20.w (alpha is 1).
        a      = sat(normalize(n) . c66.xyz);  S = tf4(a).rgb          the fog's in-scatter table
        (l1, l2) = sky_paths(n, c67y, c31x, c46x);  E = sky_extinction(l1, l2, c64, c65)
        (u, v) = sky_latlong(n);  s13 = tf13(u, v);  s14 = tf14(u, v)
        col    = s13.rgb^2 + c47x * (s14.rgb^2 - s13.rgb^2);  alpha = s13.a + c47x * (s14.a - s13.a)
        result = (1 - E) * S + alpha * (col - (1 - E) * S)
    n: the view direction (the cube's interpolator: the vertex position before the eye is added; any
    length). block: a mapping with c31x, c46x, c47x (pixel c31.x, c46.x, c47.x at the sky draw), c64, c65,
    c66 (three floats each) and c67y. tf13 None: the clear-sky term (1 - E) * S alone; tf14 None: the first
    texture alone, as with c47x = 0. Without the ground-fog volume of bool b129 (clear in every draw of the
    GPU log)."""
    a = sat(dot(normalize([n[0], n[1], n[2]]), [_finite(x) for x in block["c66"][:3]]))
    S = [_finite(x) for x in tf4(a)[:3]]
    E = sky_extinction(*sky_paths(n, block["c67y"], block["c31x"], block["c46x"]), block["c64"], block["c65"])
    base = [(1.0 - E[i]) * S[i] for i in range(3)]
    if tf13 is None:
        return [_finite(x) for x in base]
    u, v = sky_latlong(n)
    s13 = [_finite(x) for x in tf13(u, v)[:4]]
    col, alpha = [x * x for x in s13[:3]], s13[3]
    if tf14 is not None:
        s14 = [_finite(x) for x in tf14(u, v)[:4]]
        k = _finite(block["c47x"])
        col = [col[i] + k * (s14[i] * s14[i] - col[i]) for i in range(3)]
        alpha = alpha + k * (s14[3] - alpha)
    return [_finite(base[i] + alpha * (col[i] - base[i])) for i in range(3)]


# The pixel shaders with the fog block, for the capture's fog flag: (hash, light family, the interpolator
# that is the world position, form). 40 of the 41 dumps that fetch tf4 and tf5 with tfetch1D and read c67
# (fog_check.scan); every one is checked by fog_check.py. family: the pixel shader's family in
# light_check.FAMILIES, None where it has none (the native pass does not light those: the blended
# characters of the alpha form, the fur 014F..., the water A17D... and F6D9..., and eleven more).
# world: interpolator N is register rN of the pixel shader. form: "plain", or "alpha" where the in-scatter
# term is multiplied by the pixel's alpha (fog's scatter_scale).
FogShader = collections.namedtuple("FogShader", "hash family world form")
FOG_SHADERS = (
    FogShader("014F8A02DB7B19CA", None, 1, "plain"),
    FogShader("04F23292B2F44077", "terrain-layer-multi", 4, "plain"),
    FogShader("0981301D645E2517", None, 2, "plain"),
    FogShader("09CB69BD265B58D1", None, 2, "plain"),
    FogShader("14217F5C1118D00F", None, 2, "plain"),
    FogShader("165EDCD2CB963868", "plant-lit", 2, "plain"),
    FogShader("20055F971BB9E749", None, 2, "alpha"),
    FogShader("2F8A0341E8C09698", "terrain-far", 1, "plain"),
    FogShader("2FD24989AB04A475", "object", 2, "plain"),
    FogShader("31AE85A9275B4A29", None, 2, "plain"),
    FogShader("3275A966F62EA3B1", None, 2, "alpha"),
    FogShader("401A01FD4E5F8757", None, 2, "alpha"),
    FogShader("44B567635EB10620", None, 2, "alpha"),
    FogShader("45243B758F9272AB", "terrain-layer-multi", 4, "plain"),
    FogShader("4755A2E4A05BA678", "terrain-layer", 2, "plain"),
    FogShader("578342593A37DD35", "terrain-layer-multi", 2, "plain"),
    FogShader("648B965C200A5113", "object-multi", 2, "plain"),
    FogShader("789266C3E42D7E59", "object-multi", 2, "plain"),
    FogShader("7DDAD5C29B3F56EE", "terrain-far", 1, "plain"),
    FogShader("827B878F3D404635", None, 2, "alpha"),
    FogShader("8C512607225EC5C7", None, 2, "plain"),
    FogShader("8D900846800943C8", "object", 2, "plain"),
    FogShader("90B310AE5D5890F3", None, 2, "alpha"),
    FogShader("95ED70BD54B49D5E", None, 2, "alpha"),
    FogShader("9C2144E42E95A76F", "terrain-layer", 4, "plain"),
    FogShader("A17D8AEC3A817D45", None, 0, "plain"),
    FogShader("A22982E86122DA41", None, 2, "plain"),
    FogShader("A7A88924C272F438", None, 2, "alpha"),
    FogShader("A7E45D07F5CF6627", "plant", 2, "plain"),
    FogShader("AF1E16EE57E4BB06", "terrain-layer-multi", 2, "plain"),
    FogShader("C0108405DECA669F", None, 2, "alpha"),
    FogShader("C0715ED52CE7ECF2", None, 2, "plain"),
    FogShader("E0996BF4D9539D38", None, 4, "plain"),
    FogShader("E99F4ACC7A3C78B4", "plant-multi", 2, "plain"),
    FogShader("F2F103F16764043B", None, 2, "alpha"),
    FogShader("F525E023E08AC9BB", "plant-lit-multi", 2, "plain"),
    FogShader("F6D98C7B4D98438B", None, 0, "plain"),
    FogShader("F96A4494958BFA12", None, 2, "plain"),
    FogShader("F98433EC8C4A4740", None, 2, "plain"),
    FogShader("FC0922A3A8D8DA7A", None, 2, "plain"),
)
# The 41st: the lake's surface, which fetches both tables but with the sky's arithmetic inside it, also
# with the fog predicate off. Not of the form and not checked; a draw of it must not count as fogged by
# fog().
FOG_NOT_OF_THE_FORM = ("2B091BD0C45A7454",)

# The vertex shaders with the trees' form: (hash, the interpolator that is the world position, the
# interpolators of factor and add). The first four are checked by fog_check.py (trunks 475E...; leaves
# with one, two and four lights A584..., D7DD..., 2D40...). The last two are read only: D57E... (the same
# block as the leaves': dump instructions 209 to 217) and the distant-tree billboards 129F... (75 to 85),
# whose block has no c8.x and takes the eye from vertex c47, not c9.
FogVertexShader = collections.namedtuple("FogVertexShader", "hash world factor add")
FOG_VERTEX_SHADERS = (
    FogVertexShader("475EC9F795E5EDBB", 2, 6, 7),
    FogVertexShader("A5846836C90E1192", 0, 4, 5),
    FogVertexShader("D7DD31B9BF194DE8", 0, 4, 5),
    FogVertexShader("2D40B53C926109BE", 0, 4, 5),
    FogVertexShader("D57E7E65F94CBB2C", 0, 4, 5),
    FogVertexShader("129F7F2793DA9084", 0, 2, 3),
)
