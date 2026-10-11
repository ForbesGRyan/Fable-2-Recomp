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

The post chain's eight pixel shaders follow (post_composite with post_table_texel, post_downsample,
post_bright, post_blur13, post_radial, post_reduce, post_ramp, post_unlit_scale, POST_SHADERS; the post
sub-project). They work on scene values in the game's scale, after c20.w, and are checked by
post_check.py.

The sun layer's and the sky furniture's shaders are the last part (post_moon, post_cloud, post_glare,
post_beam_source, post_flare, post_moon_glow, post_sun_disc, post_cloud_occluder, post_layer_depth,
post_tree_occluder, post_previous_frame, post_quad_position, post_cube_position; POST_FURNITURE_SHADERS and
POST_QUAD_SHADERS, the quads the capture records; phase 10c). post_check.py checks them too.
"""
import collections
import math
import struct


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


def pwl_gamma_to_linear(g):
    """What the console's texture fetch gives for a component whose sign field in the fetch constant is 3
    (gamma): the stored value g in 0..1 through the piecewise linear curve, as the SDK's translators apply it
    to the filtered sample (thirdparty/rexglue-sdk/src/graphics/xenos.cpp, PWLGammaToLinear; dxbc_translator_fetch.cpp,
    the case TextureSign::kGamma). With v = 255 g: 1023 * linear is v below 64, 2 v - 64 below 96, 4 v - 256
    below 192 and 8 v - 1024 from there, each plus the whole part of itself times its slope / 1024. The terrain's
    base map tf3 (the baked term of terrain_layer and terrain_far) is fetched so in every captured terrain row
    (signs 3, 3, 3, 0: docs/native-renderer/frame-map.md, section 17, "The base map's gamma sign"). A value
    that is no number gives 0."""
    g = sat(g)
    if g >= 96.0 / 255.0:
        scale, offset = (8.0 / 1024.0, -1024.0) if g >= 192.0 / 255.0 else (4.0 / 1024.0, -256.0)
    else:
        scale, offset = (2.0 / 1024.0, -64.0) if g >= 64.0 / 255.0 else (1.0 / 1024.0, 0.0)
    linear = g * (255.0 * 1024.0 * scale) + offset
    return (linear + math.trunc(linear * scale)) / 1023.0


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


# --- the post chain (sub-project 10) -------------------------------------------------------------------------
#
# The eight pixel shaders of the game's post chain as closed formulas (POST_SHADERS), each checked against
# its microcode by post_check.py; docs/native-renderer/lighting-formulas.md, "The post chain", has the
# instruction numbers and what stands on inference. Scene values are in the game's scale (colour * c20.w);
# colours are three floats, red first.
#
# Textures are callables. `tex(px, py)` takes UNNORMALISED coordinates, in texels, texel i's centre at
# i + 0.5 (post_texture makes one from rows of texels, point or linear, clamped to the edge); the radial
# blur's `tex(u, v)` takes the normalised coordinate its taps are computed in. The half texel the fetches
# add to an interpolated coordinate (OffsetX=0.5, OffsetY=0.5 in the dumps) is in the functions, which
# take the pixel's integer position: that the rectangles' interpolator is i / N at pixel i is inference
# (it is what makes every point tap a texel centre and 0A96 the mean of a 2 x 2 block).
#
# Guards (review focus 2, spec Design 5 and 10): nothing here raises on numbers, and with `clamp` (the
# default) every function returns numbers in its target's range.
#   - a scene texel's channel is read as post_scene: 0 unless above 0 (a NaN, a negative), at most 31.875
#     (the game's k_2_10_10_10_FLOAT target's largest value; an infinity counts as that);
#   - a texel of a 10-bit target, a bloom value or a table texel that is not finite counts as 0 (in the
#     composite's `sat(bloom * c31.x)` a positive infinity counts as 1);
#   - a register that is not finite gives black (the product refuses such a block: MakePostBlock);
#   - the bright pass at L = 0 has no scene term (the microcode multiplies 0 by 1 / 0); a divisor c47.x
#     that is not above 0 gives black;
#   - no table or ramp index leaves its array, a NaN's included (index 0).
# `clamp=True` is the target's own clamp, (v > 0) ? min(v, 1) : 0: the composite's k_2_10_10_10 target and
# the bright, blur and layer targets; the downsample's is the scene's range. The microcode does not clamp
# (the render target does); post_check.py compares with `clamp=False` on inputs inside the ranges.

POST_LUM = (0.2125, 0.7154, 0.0721)     # literal c255 of 5821, EE92 and 37B1, read as dot(c.zxy, c255.xyz)
POST_SCENE_MAX = 31.875                 # the largest value of the game's scene target (7e3)
POST_TABLE_TEXELS = 512

PostShader = collections.namedtuple("PostShader", "hash role")
POST_SHADERS = (
    PostShader("0A96B3FD5121E331", "reduce"),       # C1, C2, C3: 560 x 360 to 280 x 180
    PostShader("87E85199E1E9B260", "downsample"),   # D: the scene to 280 x 180
    PostShader("5821366EE8FDE893", "luminance"),    # L: what the CPU measures
    PostShader("EE922CFB4426EA83", "bright"),       # BP
    PostShader("EA83C7E7E24EC4C8", "blur-x"),       # H
    PostShader("28DBE18F313350BF", "blur-y"),       # V
    PostShader("4BE4344C27016E04", "radial"),       # B1, B2
    PostShader("37B186F4BD3288F7", "composite"),    # X
)


def post_lum(c):
    """0.2125 r + 0.7154 g + 0.0721 b. No guard: the callers clamp what they read."""
    return c[0] * POST_LUM[0] + c[1] * POST_LUM[1] + c[2] * POST_LUM[2]


def post_scene(v):
    """A channel of a scene texel as the post steps read it: (v > 0) ? min(v, 31.875) : 0."""
    return min(v, POST_SCENE_MAX) if v > 0.0 else 0.0


def _post_finite(*values):
    return all(math.isfinite(v) for v in values)


def post_table_texel(u, n=POST_TABLE_TEXELS):
    """The texel a point-filtered, clamped tfetch1D with OffsetX=0.5 reads of an n-texel table at the
    normalised coordinate u: min(n - 1, max(0, floor(u * n + 0.5))).

    That is the interpreter's rule for a point fetch (ucode_eval.point_texel: floor(coordinate * size +
    offset)) with the composite's own fetch (37B1 instruction 7: `tfetch1D r0.x___, r1.w, tf1,
    MagFilter=point, MinFilter=point, OffsetX=0.5`), and the SDK's translator's: it adds the offset in texels
    to the coordinate and samples with the host's point filter (dxbc_translator_fetch.cpp, "Get offsets
    applied to the coordinates before sampling"; its own 1.5 / 1024 of a texel on top is not modelled). So
    texel k is read for u in [(k - 0.5) / n, (k + 0.5) / n): the texel NEAREST u * n, not floor(u * n). With
    the captured c46.x = 0.25 texel k serves scene luminance (k -+ 0.5) / 128, and texel 0, which is 0 in every
    captured table, the luminances below 1 / 256. A coordinate that is not a number, or not above 0, reads
    texel 0; one beyond the end reads the last texel."""
    if n < 1:
        raise ValueError("a table needs at least one texel")
    if not u > 0.0:
        return 0
    x = u * n + 0.5
    if not x < n:
        return n - 1
    return int(math.floor(x))


def post_unlit_scale(c77y, table):
    """The frame's scale of an unlit draw (spec ruling 9): 1 / max(c77.y * T[128], 1 / 64), the reciprocal of
    the composite's gain at scene luminance 1 (texel 128 with the captured c46.x = 0.25), at most 64. A gain
    that is not a number, or not above 1 / 64, gives 64."""
    gain = c77y * table[128]
    return 1.0 / gain if gain > 1.0 / 64.0 and math.isfinite(gain) else 64.0


def post_ramp_index(level):
    """The scan-out ramp's entry for a front-target value in 0..1: min(255, floor(v * 255 + 0.5)); 0 for a
    value that is not a number or not above 0."""
    if not level > 0.0:
        return 0
    x = level * 255.0 + 0.5
    return 255 if not x < 255.0 else int(math.floor(x))


def post_ramp(level, ramp):
    """One channel through the guest's 256-entry gamma ramp, as the swap blit applies it to an 8-bit front
    buffer: ramp[post_ramp_index(level)]. level: the front target's value in 0..1 (its 8-bit level over
    255); ramp: 256 floats in 0..1 (one channel of the table)."""
    if len(ramp) != 256:
        raise ValueError("the ramp has 256 entries, got %d" % len(ramp))
    return ramp[post_ramp_index(level)]


def post_texture(rows, linear=False):
    """A picture as a callable tex(px, py) of unnormalised coordinates, clamped to the edge. rows[y][x] is a
    texel (a sequence of floats). Point: the texel floor(px), floor(py). Linear: the blend of the four
    texels around the coordinate, centres at i + 0.5, as a host sampler filters. A coordinate that is not a
    number reads texel 0 of its axis."""
    data = [[list(t) for t in row] for row in rows]
    if not data or not data[0]:
        raise ValueError("a picture needs at least one texel")
    h, w = len(data), len(data[0])

    def axis(p, n):
        if p != p:
            return 0, 0, 0.0
        if linear:
            x = min(max(p - 0.5, 0.0), n - 1.0)
            i = min(int(x), n - 1)
            return i, min(i + 1, n - 1), x - i
        i = int(math.floor(min(max(p, 0.0), n - 1.0)))
        return i, i, 0.0

    def fetch(px, py):
        x0, x1, fx = axis(px, w)
        y0, y1, fy = axis(py, h)
        if fx == 0.0 and fy == 0.0:
            return list(data[y0][x0])
        a, b, c, d = data[y0][x0], data[y0][x1], data[y1][x0], data[y1][x1]
        out = []
        for i in range(len(a)):
            top = a[i] + (b[i] - a[i]) * fx if fx else a[i]
            bottom = c[i] + (d[i] - c[i]) * fx if fx else c[i]
            out.append(top + (bottom - top) * fy if fy else top)
        return out

    return fetch


def post_reduce(tex, x, y, clamp=True):
    """0A96B3FD5121E331: one linear fetch at the interpolated coordinate plus (1, 1) texels of the source.
    With a target half the source's size, pixel (x, y) reads tex(2 x + 1, 2 y + 1): the corner shared by four
    texels, their mean. tex: the source, linear."""
    out = list(tex(2.0 * x + 1.0, 2.0 * y + 1.0))
    return [sat(v) for v in out] if clamp else out


def post_downsample(tex, x, y, bias):
    """87E85199E1E9B260: the scene at a quarter of its size.
        P = 4 * (|pixel position| + bias)                     param_gen, unnormalised coordinates
        out = 0.25 * (t(P + (2.5, 2.5)) + t(P + (2.5, 0.5)) + t(P + (0.5, 0.5)) + t(P + (0.5, 2.5)))
    tex: the scene, linear. (x, y): the pixel; bias 0: the shader is handed the pixel's integral position,
    so the four fetches are the centres of texels (0, 0), (2, 0), (0, 2), (2, 2) of its 4 x 4 block; bias
    0.5: its centre, the four fetches two texels further on in each axis (discovery question 6 decides).
    Every sample's channel is read as post_scene; as many channels as tex returns."""
    px, py = 4.0 * (abs(x) + bias), 4.0 * (abs(y) + bias)
    taps = [tex(px + ox, py + oy) for ox, oy in ((2.5, 2.5), (2.5, 0.5), (0.5, 0.5), (0.5, 2.5))]
    return [0.25 * sum(post_scene(t[i]) for t in taps) for i in range(len(taps[0]))]


def post_bright(d, tf1, tf2, c31x, c46x, c47x, c77, clamp=True):
    """EE922CFB4426EA83, the bright pass (alpha is 1).
        L   = lum(d)
        out = (d * sat(L * c77.y - c77.x) / L * c31.x  +  tf1 * 6 * c46.x  +  tf2) / c47.x
    d: the downsample's texel; tf1, tf2: the two layers of the sun (the radial-blurred beams and the flare
    layer; which is which is discovery question 3), zero until 10c. With c77.y = 1 / c20.w the scene term is
    the part of the pre-scale luminance above c77.x, in the pixel's own colour. L = 0: the scene term is 0."""
    if not _post_finite(c31x, c46x, c47x, c77[0], c77[1]) or not c47x > 0.0:
        return [0.0, 0.0, 0.0]
    s = [post_scene(v) for v in d[:3]]
    L = post_lum(s)
    k = sat(L * c77[1] - c77[0]) / L * c31x if L > 0.0 else 0.0
    out = []
    for i in range(3):
        v = (s[i] * k + _finite(tf1[i]) * 6.0 * c46x + _finite(tf2[i])) / c47x
        out.append(sat(v) if clamp else v)
    return out


def post_blur13(tex, x, y, weights7, vertical, clamp=True):
    """EA83C7E7E24EC4C8 (along x) and 28DBE18F313350BF (along y): 13 point taps at the texel centres
    (x + 0.5 + k, y + 0.5) or (x + 0.5, y + 0.5 + k), k = -6..6, with the weights weights7[|k|]: c46.x
    (centre), c46.y, c46.z, c46.w, c47.x, c47.y, c47.z. tex: the source, point; as many channels as it
    returns. A tap or a weight that is not finite counts as 0."""
    if len(weights7) != 7:
        raise ValueError("the blur has 7 weights, got %d" % len(weights7))
    w = [_finite(v) for v in weights7]
    out = None
    for k in range(-6, 7):
        t = tex(x + 0.5, y + 0.5 + k) if vertical else tex(x + 0.5 + k, y + 0.5)
        if out is None:
            out = [0.0] * len(t)
        for i in range(len(out)):
            out[i] += w[abs(k)] * _finite(t[i])
    return [sat(v) for v in out] if clamp else out


def post_radial_taps(u, v, c31, c46x, c47x):
    """The 14 taps of 4BE4344C27016E04 as ((u, v), weight):
        s = 1 + (4/7) * (c47.x - 1);  g = 1 + (4/7) * (c46.x - 1)
        p_k = c31.xy + (uv - c31.xy) * s^k;  w_k = 0.5 * g^k,  k = 0..13
    c31.xy: the blur's centre (the sun's point on the screen), in the normalised coordinate of the
    interpolator; it may lie outside the picture."""
    s = (c47x - 1.0) * (4.0 / 7.0) + 1.0
    g = (c46x - 1.0) * (4.0 / 7.0) + 1.0
    return [((c31[0] + (u - c31[0]) * s ** k, c31[1] + (v - c31[1]) * s ** k), 0.5 * g ** k) for k in range(14)]


def post_radial(tex, u, v, c31, c46x, c47x, clamp=True):
    """4BE4344C27016E04, one pass of the radial blur: (4/7) * sum of w_k * tex(p_k) over post_radial_taps.
    tex(u, v): the source at a normalised coordinate, point filtered, with the fetch's half texel
    (OffsetX = OffsetY = 0.5) applied by the caller's tex. The weights sum to 2.54 with the captured c46.x =
    0.87, so the target's clamp is part of the picture."""
    if not _post_finite(u, v, c31[0], c31[1], c46x, c47x):
        return [0.0, 0.0, 0.0]
    try:
        taps = post_radial_taps(u, v, c31, c46x, c47x)
    except OverflowError:       # a register far outside the block's bounds
        return [0.0, 0.0, 0.0]
    out = None
    for (pu, pv), w in taps:
        t = tex(pu, pv)
        if out is None:
            out = [0.0] * len(t)
        for i in range(len(out)):
            out[i] += w * _finite(t[i])
    out = [_finite(x * (4.0 / 7.0)) for x in out]
    return [sat(x) for x in out] if clamp else out


def post_composite(scene, bloom, table, c31x, c46x, c47x, c77, c78x, clamp=True):
    """37B186F4BD3288F7, the composite: the front buffer's colour before the scan-out ramp.
        L = lum(scene)
        e = c77.y * T(c46.x * L)                              the exposure: a gain by scene luminance
        c = sat(e * (L + sat(c47.x * L) * (scene - L)))       the colour fades to grey below L = 1 / c47.x
        b = sat(bloom * c31.x)
        s = c + b * (1 - c)                                   screen blend
        t = s + c77.x * (s - lum(s))                          saturation step
        out = |t| ^ c78.x                                     (the shader's alpha is the blue value)
    scene: the scene texel (the game's scale; read as post_scene); bloom: the blurred bright pass at the
    pixel (0, 0, 0 for none). table: the frame's 512 floats, read at post_table_texel(c46.x * L); or a
    callable of the coordinate (post_check.py's, to compare the coordinate itself)."""
    if not _post_finite(c31x, c46x, c47x, c77[0], c77[1], c78x):
        return [0.0, 0.0, 0.0]
    s = [post_scene(v) for v in scene[:3]]
    L = post_lum(s)
    u = c46x * L
    T = _finite(table(u) if callable(table) else table[post_table_texel(u, len(table))])
    e = c77[1] * T
    k = sat(c47x * L)
    c = [sat(e * (L + k * (x - L))) for x in s]
    b = [sat(bloom[i] * c31x) for i in range(3)]
    scr = [c[i] + b[i] * (1.0 - c[i]) for i in range(3)]
    L2 = post_lum(scr)
    out = powc([x + c77[0] * (x - L2) for x in scr], c78x)
    return [sat(x) for x in out] if clamp else out


# --- the sun layer and the sky furniture (sub-project 10, phase 10c) -------------------------------------------
#
# The eleven pixel shaders and three vertex shaders of the game's sky furniture and of its half-size "sun
# layer" (POST_FURNITURE_SHADERS), each checked against its microcode by post_check.py; the document's part
# "The sun layer and the sky furniture" has the instruction numbers, the states they are drawn with and what
# stands on inference. Colours are four floats, red first, alpha last.
#
# What a function is handed. The quad shaders that only scale a texel take that texel (`t`: red, green,
# blue, alpha as the fetch delivers it; the fetch is at the interpolated coordinate of the quad's corner
# values, slot 13, filtered as the fetch constant says: linear with mips, wrap, in every captured row). The
# clouds and their occluder compute their own coordinate and take the texture: `tex(u, v, dx, dy)` at the
# normalised coordinate (u, v), moved by (dx, dy) texels (the fetches' OffsetX / OffsetY), returning the four
# channels. TEXELS OF WHICH LEVEL is open, and the caller's tex decides it: the SDK's translator applies a
# fetch's offsets in texels of the BASE level whatever level is sampled (dxbc_translator_fetch.cpp, "Get
# offsets applied to the coordinates before sampling": "FIXME: Offsets need to be applied at the LOD being
# fetched, not at LOD 0"), so every emulated picture and capture has a step of one base-level texel; the
# console, by that note, steps one texel of the level fetched (not measured here or anywhere in this
# project). The slope 6197 takes over two such steps is about 2^k times larger at level k under the
# console's reading. The microcode check cannot tell (its field has one level).
#
# The literal registers (c254, c255) are the shaders' own; their values are the functions' defaults, read
# from the captures' "ps_lit" / "vs_lit" rows (tests/test_post_sun_layer.py holds them to a capture). They are
# parameters because the microcode reads them as registers: post_check.py also draws them at random, which
# holds what a literal of 0 or 1 would hide (an absolute value, an added term).
#
# Guards (review focus 2): a texel's channel that is not finite counts as 0 (a texel of three channels has
# alpha 1); a register, a position or a coordinate that is not finite gives zeros; a normal or a direction
# without length is (0, 0, 0); and with `clamp` (the default) every result is a number in its target's
# range: (v > 0) ? min(v, top) : 0 with top = 31.875 for what is drawn into the scene (moon, clouds, glare)
# and 1 for what is drawn into the layer's k_2_10_10_10 targets and for every alpha. The microcode does not
# clamp; post_check.py compares with `clamp=False`.
#
# Not in any function, because it is the draw's state and not the shader's: the blend, the depth test and
# the ALPHA TEST. Five of the eight quad kinds are drawn with the alpha test on (register 0x2202 bit 3,
# function "greater", reference 0x210E): the moon (reference 1 / 255), the clouds (5 / 255), the cloud
# occluders (0.149), the moon glow (10 / 255) and the sun disc (0.502: only the disc's core is drawn).
# POST_QUAD_SHADERS says which; the reference is the draw's.

POST_QUAD_INDICES = (0, 1, 2, 1, 3, 2)      # the builder's six indices of every captured quad: corners 0 1 / 2 3
POST_PREVIOUS_BLEND = 0x3DD0D0D2            # the blend constant of pass F's draw (registers 0x2105 to 0x2108)
POST_PREVIOUS_WEIGHT = 0.10196079313755035  # ... as a float (26 / 255): the share of the previous final picture
POST_CLOUD_C254 = (0.0, 0.001, 1000.0, 1.0)             # 6197: 0, the alpha threshold and the fade's rate, its start, 1
POST_CLOUD_C255X = 32.0                                 # 6197: the exponent of the light's falloff
POST_CLOUD_OCCLUDER_C255 = (1000.0, 0.0, 0.001, 1.0)    # 3FF7: the fade's start, 0, its rate, 1
POST_BEAM_C255X = 1.0 / 6.0                             # 8E4D
POST_GLOW_C255X = 0.2                                   # 4B58 (0.200000003 as a float)
POST_CUBE_C254 = (2.0, 0.0, 2850.0, -1.0)               # D1CC: vertex c254; its c255.x is 1

PostFurnitureShader = collections.namedtuple("PostFurnitureShader", "hash stage role")
POST_FURNITURE_SHADERS = (
    PostFurnitureShader("18D39DCEBEE4C2FC", "frag", "moon"),
    PostFurnitureShader("61976295517D7015", "frag", "cloud"),
    PostFurnitureShader("04985A0F7296E130", "frag", "glare"),
    PostFurnitureShader("8E4DBB9E9A63EF67", "frag", "beam-source"),
    PostFurnitureShader("D72FD0903CCE7DD6", "frag", "flare"),
    PostFurnitureShader("4B58837D42F08C89", "frag", "moon-glow"),
    PostFurnitureShader("3E77B1AD4BC5B900", "frag", "sun-disc"),
    PostFurnitureShader("3FF79524FCDB71C5", "frag", "cloud-occluder"),
    PostFurnitureShader("5D6768847B0602B8", "frag", "layer-depth"),
    PostFurnitureShader("F1EB2497108F159D", "frag", "tree-occluder"),
    PostFurnitureShader("686DE15FC84D8B73", "frag", "previous-frame"),
    PostFurnitureShader("81BADE63A169E3DC", "vert", "quad"),
    PostFurnitureShader("C304C6A1AF18B46F", "vert", "cloud-quad"),
    PostFurnitureShader("D1CC6DC28244C55A", "vert", "cube"),
)

# The quads the capture records (plan Task 12) and the pass draws (Tasks 13, 14, 16), in the order of the
# record's kinds. Every one is in POST_FURNITURE_SHADERS and passes post_check.py (spec ruling 18: a shader
# that did not would not be here).
#   hash, vs    the pixel shader and the vertex shader it is drawn with in every captured row
#   registers   the pixel registers the microcode reads, ascending, without the literals: the record's
#               `ps` rows in this order (the test holds the list to the dump)
#   slots       its texture slots, in the record's `fetch` order
#   where       "scene" (the main scene's target), or the sun layer: "layer-depth" (its depth, colour mask
#               0), "layer1" (the beam source's picture), "layer2" (flares, moon glow, sun disc)
#   blend       "alpha" (source alpha, inverse source alpha: 0x07060706), "add" (one, one: 0x01010101),
#               "none" (0x00010001), "depth" (nothing but depth is written)
#   test        the alpha test's enable bit (register 0x2202 bit 3) as every captured row has it; the
#               function is "greater" wherever it is on. A record carries the draw's own words (ruling 23:
#               on / off, function, reference). The enable bit is what tells: with the test off, register
#               0x210E still holds a stale 0.502, the sun disc's reference.
# A record is read only where its kind is drawn (ruling 24), kind by kind: moon, clouds and glare at the
# main scene's target, inside the scene; the cloud occluders, the beam source, flares, moon glow and sun
# disc at the sun layer's target, half the scene's size (pitch 560 in the captures: half of 1120, not a
# number to carry). D72F is also drawn elsewhere: 4 to 16 calls a frame into a target of pitch 280 before
# the main scene, each a quad of no area (its first two corners at one position). Those are not the layer's
# flares. (5D67, no recorded kind, is likewise drawn twice a frame at a target of pitch 320.)
PostQuadShader = collections.namedtuple("PostQuadShader", "hash kind vs registers slots where blend test")
_QUAD_VS, _CLOUD_VS = "81BADE63A169E3DC", "C304C6A1AF18B46F"
POST_QUAD_SHADERS = (
    PostQuadShader("18D39DCEBEE4C2FC", "moon", _QUAD_VS, (20, 31), (13,), "scene", "alpha", True),
    PostQuadShader("61976295517D7015", "cloud", _CLOUD_VS, (20, 31, 46, 47, 51, 69, 70, 71, 77, 78, 79), (13,),
                   "scene", "alpha", True),
    PostQuadShader("04985A0F7296E130", "glare", _QUAD_VS, (31,), (13,), "scene", "add", False),
    PostQuadShader("8E4DBB9E9A63EF67", "beam-source", _QUAD_VS, (31,), (13,), "layer1", "none", False),
    PostQuadShader("D72FD0903CCE7DD6", "flare", _QUAD_VS, (31,), (13,), "layer2", "add", False),
    PostQuadShader("4B58837D42F08C89", "moon-glow", _QUAD_VS, (), (13,), "layer2", "alpha", True),
    PostQuadShader("3E77B1AD4BC5B900", "sun-disc", _QUAD_VS, (20, 31), (13,), "layer2", "add", True),
    PostQuadShader("3FF79524FCDB71C5", "cloud-occluder", _CLOUD_VS, (31, 46, 47, 77), (13,), "layer-depth", "depth", True),
)


def _post_texel(t):
    """A texel's four channels, one that is not finite read as 0; a texel of three channels has alpha 1."""
    v = [_finite(x) for x in t[:4]]
    return v + [1.0] * (4 - len(v))


def _post_out(rgb, alpha, clamp, top):
    if not clamp:
        return list(rgb) + [alpha]
    return [(min(v, top) if v > 0.0 else 0.0) for v in rgb] + [sat(alpha)]


def post_moon(t, c31, c20w, clamp=True):
    """18D39DCEBEE4C2FC, the moon (in the scene, right after the sky; alpha blend, alpha test, depth test
    without write):
        rgb = tex.rgb * c31.rgb * tex.a * c20.w        alpha = c31.w * tex.a
    c20.w: the scene's output scale. c31.w is 0 in every captured row (the moon below the horizon by day):
    its alpha is then 0 and the alpha test drops every pixel."""
    if not _post_finite(c20w, *c31[:4]):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[i] * c31[i] * t[3] * c20w for i in range(3)], c31[3] * t[3], clamp, POST_SCENE_MAX)


def post_glare(t, c31, clamp=True):
    """04985A0F7296E130, the glare of the sun and of the moon (the scene's last two draws; additive, no depth
    test):  rgb = tex.rgb * c31.rgb,  alpha 1. No output scale: c31 carries the brightness."""
    if not _post_finite(*c31[:3]):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[i] * c31[i] for i in range(3)], 1.0, clamp, POST_SCENE_MAX)


def post_beam_source(t, c31, c255x=POST_BEAM_C255X, clamp=True):
    """8E4DBB9E9A63EF67, the sun-beam source (layer 1; no blend, depth-tested): the glare's texture again,
        rgb = tex.rgb * c255.x * c31.rgb,  alpha 1       c255.x = 1 / 6 (literal), c31 = the sun's colour."""
    if not _post_finite(c255x, *c31[:3]):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[i] * c255x * c31[i] for i in range(3)], 1.0, clamp, 1.0)


def post_flare(t, c31, clamp=True):
    """D72FD0903CCE7DD6, a flare (layer 2; additive, depth-tested):  rgb = tex.a * c31.rgb,  alpha 1."""
    if not _post_finite(*c31[:3]):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[3] * c31[i] for i in range(3)], 1.0, clamp, 1.0)


def post_moon_glow(t, c255x=POST_GLOW_C255X, clamp=True):
    """4B58837D42F08C89, the moon's glow (layer 2; alpha blend, alpha test, depth-tested):
        rgb = tex.rgb * c255.x * tex.a,  alpha = tex.a       c255.x = 0.2 (literal). It reads no register."""
    if not _post_finite(c255x):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[i] * c255x * t[3] for i in range(3)], t[3], clamp, 1.0)


def post_sun_disc(t, c31, c20w, clamp=True):
    """3E77B1AD4BC5B900, the sun's disc (layer 2 only; additive, alpha test above 0.502, depth-tested):
        rgb = tex.rgb * c31.rgb * tex.a * c20.w,  alpha = tex.a
    c20.w is the register at the draw's call: 1 in every captured row (not the scene's scale)."""
    if not _post_finite(c20w, *c31[:3]):
        return [0.0, 0.0, 0.0, 0.0]
    t = _post_texel(t)
    return _post_out([t[i] * c31[i] * t[3] * c20w for i in range(3)], t[3], clamp, 1.0)


def post_layer_depth(t):
    """5D6768847B0602B8, the layer's depth from the scene's: (colour, depth) = ((0, 0, 0, 0), sat(tex.r)).
    Drawn with colour mask 0, depth test and write: the depth output is what it is for. t: the
    scene depth's texel at the pixel (a point fetch at the interpolated coordinate)."""
    return [0.0, 0.0, 0.0, 0.0], sat(t[0])


def post_previous_frame(t, clamp=True):
    """686DE15FC84D8B73, pass F: the previous frame's final picture at the pixel (a point fetch with half a
    texel in each axis), unchanged. Its weight is not the shader's: the draw blends with the constant factor
    POST_PREVIOUS_WEIGHT (blend 0x0F0E0F0E)."""
    t = _post_texel(t)
    return [sat(v) for v in t] if clamp else t


def post_previous_blend(new, previous, weight=POST_PREVIOUS_WEIGHT, levels=255):
    """Pass F's blend as the front buffer ends up: new * (1 - weight) + previous * weight a channel (the draw's
    constant blend factor, 0x0F0E0F0E: source the previous picture times the constant, destination the new
    one times its complement), stored as a level of an 8-bit target (the nearest; 0 for a value that is not
    a number). new: the composite's value as the front buffer holds it (a level); previous: the previous
    frame's final picture at the pixel. A weight that is not in (0, 1], or not a number, leaves the new
    value as it is (no blend)."""
    out = []
    for n, p in zip(new[:3], previous[:3]):
        n, p = sat(_finite(n)), sat(_finite(p))
        if not (weight == weight and 0.0 < weight <= 1.0):
            out.append(n)
            continue
        v = sat(n + (p - n) * weight)
        out.append(math.floor(v * levels + 0.5) / levels)
    return out


def post_cloud_uv(uv, c31, c46):
    """The clouds' and their occluders' texture coordinate: uv * c31.xy + c46.xy (tiling, scroll)."""
    return uv[0] * c31[0] + c46[0], uv[1] * c31[1] + c46[1]


def post_cloud_fade(P, eye, start=POST_CLOUD_C254[2], rate=POST_CLOUD_C254[1], zero=POST_CLOUD_C254[0], one=POST_CLOUD_C254[3]):
    """one - sat(rate * (sqrt(|(P.xy - eye.xy)^2 + zero|) - start)): 1 up to `start` from the eye on the
    ground plane, 0 from start + 1 / rate on (1000 and 2000 with the literals). The height does not count."""
    d2 = (P[0] - eye[0]) ** 2 + (P[1] - eye[1]) ** 2 + zero
    return one - sat(rate * (math.sqrt(abs(d2)) - start))


def post_cloud(tex, uv, P, c20w, c31, c46, c47, c51x, c69x, c70x, c71x, c77, c78, c79, c254=POST_CLOUD_C254,
               c255x=POST_CLOUD_C255X, clamp=True):
    """61976295517D7015, a cloud layer (in the scene after the moon; alpha blend, alpha test, depth test and
    write). P: the pixel's world position (the quad's corner positions, interpolated); uv: its coordinate.
        t      = tex(uv * c31.xy + c46.xy)                         the layer's texture: colour and density
        alpha  = t.a * c69.x * fade(P, eye = c79)                  post_cloud_fade
        alpha <= c254.y (0.001):  rgb = 0
        else:  g  = (t.a at +1 texel in x - t.a at -1, the same in y)       the density's slope (which
                                                                   level's texel: open, see the part's head)
               N  = (g.x, g.y, c70.x) / |(g.x, g.y, c70.x)|
               Dn = (P - c77.xyz) / |P - c77.xyz|                  c77: a far point (the sun's side)
               a1 = sat(-N . c78.xyz);  b1 = sat(-Dn . N);  b2 = sat(Dn . N)       c78: the view direction (the
                                                                   quads' matrix row c3.xyz in every capture)
               l  = b1^2 * (1 - t.a) + b2 * (1 + (1 - a1)^c255.x)
               rgb = (l * c47.rgb + c71.x) * c51.x * t.rgb
        out = (rgb * c20.w, alpha)
    The literals: c254 = (0, 0.001, 1000, 1), c255.x = 32. Written with them as registers: the two zeros of
    c254.x that are added (to the squared distance and to the slope's squared length), the 1 of c254.w in
    1 - t.a and 1 - a1, and the thin branch's rgb = (-|P.x| > c254.x) ? 1 : 0, which is 0 with the literal."""
    regs = (c20w, c51x, c69x, c70x, c71x, c255x, *uv[:2], *P[:3], *c31[:2], *c46[:2], *c47[:3], *c77[:3], *c78[:3],
            *c79[:2], *c254[:4])
    if not _post_finite(*regs):
        return [0.0, 0.0, 0.0, 0.0]
    u, v = post_cloud_uv(uv, c31, c46)
    t = _post_texel(tex(u, v, 0, 0))
    a = t[3]
    alpha = a * c69x * post_cloud_fade(P, c79, c254[2], c254[1], c254[0], c254[3])
    if not alpha > c254[1]:
        dark = c20w if -abs(P[0]) > c254[0] else 0.0
        return _post_out([dark, dark, dark], alpha, clamp, POST_SCENE_MAX)
    gx = _finite(tex(u, v, 1, 0)[3]) - _finite(tex(u, v, -1, 0)[3])
    gy = _finite(tex(u, v, 0, 1)[3]) - _finite(tex(u, v, 0, -1)[3])
    n2 = abs(c70x * c70x + (gx * gx + gy * gy + c254[0]))
    inv = 1.0 / math.sqrt(n2) if n2 > 0.0 else 0.0
    N = (gx * inv, gy * inv, c70x * inv)
    Dn = normalize([P[i] - c77[i] for i in range(3)])
    a1 = sat(-dot(N, c78[:3]))
    towards = dot(Dn, N)
    b1, b2 = sat(-towards), sat(towards)
    l = b1 * b1 * (c254[3] - a) + (b2 + b2 * powc([c254[3] - a1], c255x)[0])
    rgb = [(l * c47[i] + c71x) * c51x * t[i] * c20w for i in range(3)]
    return _post_out([_finite(x) for x in rgb], alpha, clamp, POST_SCENE_MAX)


def post_cloud_occluder(tex, uv, P, c31, c46, c47, c77x, c255=POST_CLOUD_OCCLUDER_C255, clamp=True):
    """3FF79524FCDB71C5, a cloud layer as an occluder of the sun layer (colour mask 0, alpha test, depth test
    and write): the alpha the test sees; its colour output is 0.
        alpha = tex(uv * c31.xy + c46.xy).a * c77.x * (c255.w - sat(c255.z * (|P.xy - c47.xy| - c255.x)))
    c47 is the eye here (the clouds' c79), c77.x 0.5 in every captured row; the literals (1000, 0, 0.001, 1)
    are the clouds' fade. The pixel is kept where alpha is above the draw's reference (0.149 captured)."""
    if not _post_finite(c77x, *uv[:2], *P[:2], *c31[:2], *c46[:2], *c47[:2], *c255[:4]):
        return 0.0
    u, v = post_cloud_uv(uv, c31, c46)
    alpha = _finite(tex(u, v, 0, 0)[3]) * c77x * post_cloud_fade(P, c47, c255[0], c255[2], c255[1], c255[3])
    return sat(alpha) if clamp else alpha


def post_tree_occluder_uv(uv, c8):
    """F1EB's texture coordinate: uv * c8.xy + c8.zw."""
    return uv[0] * c8[0] + c8[2], uv[1] * c8[1] + c8[3]


def post_tree_occluder(t, c15w, clamp=True):
    """F1EB2497108F159D, the tree occluders of the sun layer (colour mask 0, alpha test and alpha to mask,
    depth test and write; slot 0): rgb = tex.rgb, alpha = tex.a * c15.w. Not a quad: its draws are indexed
    tree geometry (vertex shader 4325...), which no record holds."""
    t = _post_texel(t)
    k = _finite(c15w)
    return _post_out(t[:3], t[3] * k, clamp, 1.0)


def post_quad_position(p, c0, c1, c2, c3):
    """81BADE63A169E3DC and C304C6A1AF18B46F: the clip position of a quad's corner p = (x, y, z):
    (c0 . p1, c1 . p1, c2 . p1, c3 . p1) with p1 = (x, y, z, 1) and vertex registers c0..c3. Both pass the
    corner's coordinate on (81BA in interpolator 0, C304 in interpolator 1) and C304 also the position p
    itself (interpolator 0: the world position the cloud shaders read). No guard: a record's corners and
    matrix are checked where they are read (MakeQuadRecord)."""
    q = (p[0], p[1], p[2], 1.0)
    return [sum(c[i] * q[i] for i in range(4)) for c in (c0, c1, c2, c3)]


def post_cube_point(v, c9, c119x, c254=POST_CUBE_C254, c255x=1.0):
    """The world point D1CC6DC28244C55A makes of a unit cube's vertex v = (x, y, z):
        xy = (v.xy * c254.x + c254.w) * c254.z                 (2 v - 1) * 2850 with the literals
        v.z == c255.x (1):  (xy + c9.xy, c9.z)                 the face about the eye (vertex c9), at its height
        else:               (xy, c119.x)                       the other face: about the origin, at the height c119.x
    A slab 5,700 wide between the height c119.x (18.47 and 20.00 in the captured rows) and the eye's."""
    x, y = ((v[i] * c254[0] + c254[3]) * c254[2] for i in range(2))
    if v[2] == c255x:
        return x + c9[0], y + c9[1], c9[2]
    return x, y, c119x


def post_cube_bias(v, c254=POST_CUBE_C254, c255x=1.0):
    """What D1CC adds to the transformed point besides: c254.y (0 with the literal) to x and y at the eye's
    face, to y and w at the other."""
    k = c254[1]
    return [k, k, 0.0, 0.0] if v[2] == c255x else [0.0, k, 0.0, k]


def post_cube_position(v, c0, c1, c2, c3, c9, c119x, c254=POST_CUBE_C254, c255x=1.0):
    """D1CC6DC28244C55A, the three depth-only cubes of the sun layer: post_quad_position of post_cube_point
    (plus post_cube_bias, which is 0)."""
    q = post_quad_position(post_cube_point(v, c9, c119x, c254, c255x), c0, c1, c2, c3)
    return [a + b for a, b in zip(q, post_cube_bias(v, c254, c255x))]


# --- the wind of the foliage's vertex shaders (sub-project 11, phase 11a) ---------------------------------------
#
# Twelve vertex shaders move their vertices in the wind (frame-map section 18; lighting-formulas.md, "Wind"):
# three families, each one closed formula that wind_check.py holds against the shaders' microcode. A formula
# takes the position AT REST (what the native pass draws today: the fetched position read as yxw1, for a plant
# through its copy's rows and the draw's offset c7) and gives the position the shader puts through c0..c3.
#
# A shader's literal registers are data (spec ruling 9): the shaders of one family hold the same numbers in
# other registers and components. WIND_LITERALS has a family's numbers by name (single precision, as the
# shader objects' tables hold them), WIND_LITERAL_ORDER the names in the order the generated table and the
# native constants keep them, and each row of WIND_SHADERS says where its shader holds each ("lit_at": name ->
# (register, component)). The names of WIND_STRUCTURE are not read by a formula: in the microcode they are a
# select's condition, an added zero, or the constant 1 that is also the position's w, and the formula is the
# microcode's with them at the values given here; they are in the rows so that a shader object whose table
# differs there is not drawn with the formula either.

def _wind_f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


WIND_STRUCTURE = {"zero": 0.0, "sel_zero": 0.0, "sel_one": 1.0, "unit": 1.0}
WIND_LITERAL_ORDER = {
    "plant": ("lo", "two_pi", "inv_two_pi", "gain", "height", "half", "unit"),
}
WIND_LITERALS = {
    # lo, two_pi, inv_two_pi and half wrap the phase to [-pi, pi); gain is the share of a copy's seed in its
    # own rate; height is the mesh height (in the mesh's own units, 1 / height) at which the sway is whole;
    # unit is the 1 of the phase, which is also the w the shader gives its position (structure).
    "plant": dict(zip(WIND_LITERAL_ORDER["plant"],
                      (_wind_f32(v) for v in (-math.pi, 2.0 * math.pi, 1.0 / (2.0 * math.pi), 0.1, 5.0, 0.5, 1.0)))),
}
WIND_REGISTERS = {"plant": (10, 21, 22, 25, 26)}      # vertex registers, in WindInputs' order
_WIND_EVIDENCE = ("wind_check.py formulas, rows and identities (out/native-evidence/foliage/t7_wind_check.txt); "
                  "lighting-formulas.md, \"Wind\"")


def _wind_row(family, role, lit_at, elements=(), note=""):
    """A row of WIND_SHADERS: `lit_at` is "name=register.component ..." for every name of the family."""
    places = {}
    for item in lit_at.split():
        name, place = item.split("=")
        reg, comp = place.split(".")
        places[name] = (int(reg), "xyzw".index(comp))
    order = WIND_LITERAL_ORDER[family]
    if set(places) != set(order):
        raise ValueError("wind row: literals %s" % sorted(set(places) ^ set(order)))
    return {"family": family, "role": role, "reg": WIND_REGISTERS[family],
            "lit_reg": tuple(sorted({reg for reg, _ in places.values()})),
            "lit": dict(WIND_LITERALS[family]), "lit_at": {name: places[name] for name in order},
            "elements": tuple(elements), "evidence": (note + " " if note else "") + _WIND_EVIDENCE}


_PLANT_NEAR = "Checked with the push loop's count at 0: the push away from characters is not drawn (spec ruling 10)."
WIND_SHADERS = {            # the twelve; a shader not machine-checked is not in it
    # The instanced plants: the sway alone. "elements" is empty: what the formula reads per vertex (the copy's
    # rows and the mesh position) is what the instance entry of vs-transforms.json already names.
    "8123C16DBF583F92": _wind_row("plant", "scene", "lo=253.x two_pi=253.y inv_two_pi=253.z gain=254.w height=255.w "
                                                    "half=254.z unit=254.y"),
    "B636821F95DC9D8E": _wind_row("plant", "scene", "lo=253.x two_pi=254.z inv_two_pi=253.z gain=255.w height=253.y "
                                                    "half=254.w unit=254.y"),
    "48D30ECCF684F488": _wind_row("plant", "scene", "lo=253.x two_pi=253.z inv_two_pi=253.w gain=254.w height=255.w "
                                                    "half=254.z unit=254.y", note=_PLANT_NEAR),
    "FC4F2EF6930768BE": _wind_row("plant", "scene", "lo=253.x two_pi=253.z inv_two_pi=253.w gain=254.w height=255.w "
                                                    "half=254.z unit=254.y", note=_PLANT_NEAR),
    "33C00226C154C1F2": _wind_row("plant", "scene", "lo=253.x two_pi=253.w inv_two_pi=253.z gain=254.w height=255.w "
                                                    "half=254.z unit=254.y", note=_PLANT_NEAR),
    "A8A5B2A89F70336A": _wind_row("plant", "caster", "lo=253.y two_pi=253.x inv_two_pi=253.z gain=254.w height=255.w "
                                                     "half=254.z unit=254.y"),
}


def _wind_frac(x):
    return x - math.floor(x)


def wind_plant(p, z, a3, b3, c7xy, c10x, c21, c22, c25x, c26xy, lit):
    """p + sat(lit.height * z) * cos(theta) * (c26x * c21 + c26y * c22); R A1.

    The instanced plants (8123, instr 38 to 58; B636, 48D3, FC4F, 33C0 and the caster A8A5 hold the same block):
        f     = frac(c7.x + c7.y + B3 + A3)                      the copy's seed: a3, b3 are the fourth stored
                                                                 components of its first two rows (its x and y)
        theta = 1 + f - c10.x * c25.x * (1 + gain * f)           the clock c10.x, the rate c25.x
        theta = frac(theta * inv_two_pi + half) * two_pi + lo    wrapped to [-pi, pi)
        h     = sat(height * z)                                  z: the mesh vertex's third coordinate
        p'    = p + h * cos(theta) * (c26.x * c21 + c26.y * c22)
    `p` is the copy-transformed position with the draw's offset c7 added (the microcode's r5 at instr 31), which
    the distance cut has tested before (the cut is not this function's). A vertex at or below the root (z <= 0)
    stands; with c26 = 0 the plant stands (ruling 26: c25.x = 0 with c26 = 0 in the bridge's caster rows)."""
    f = _wind_frac(c7xy[0] + c7xy[1] + b3 + a3)
    theta = 1.0 - c10x * c25x * (f * lit["gain"] + 1.0) + f
    theta = _wind_frac(theta * lit["inv_two_pi"] + lit["half"]) * lit["two_pi"] + lit["lo"]
    s = sat(lit["height"] * z) * math.cos(theta)
    return tuple(p[k] + s * (c21[k] * c26xy[0] + c22[k] * c26xy[1]) for k in range(3))


def wind_reach_plant(c21, c22, c26xy):      # an upper bound of |offset|: |c26x c21 + c26y c22|
    """The largest distance wind_plant moves a vertex: h and the cosine are at most 1."""
    return math.sqrt(sum((c21[k] * c26xy[0] + c22[k] * c26xy[1]) ** 2 for k in range(3)))


# --- the trees: trunks and branches (plan Task 6) ---------------------------------------------------------------

WIND_LITERAL_ORDER["trunk"] = ("half", "base", "rate1", "rate2", "two_pi", "quarter", "lean", "gust", "phase_x",
                               "phase_y", "lo", "amp", "along", "across", "rate0", "zero", "sel_zero", "sel_one")
WIND_LITERALS["trunk"] = dict(zip(WIND_LITERAL_ORDER["trunk"], (_wind_f32(v) for v in (
    0.5, 0.7, 0.035, 0.472, 2.0 * math.pi, 0.25, 0.04, 0.025, 0.23, 0.11, -math.pi, 0.3849, 0.3, 0.019245, 0.1,
    0.0, 0.0, 1.0))))
WIND_REGISTERS["trunk"] = (15, 16, 17)
# The one element a trunk's wind reads besides the position: the 8_8_8_8 of dword 7, its four values in the
# fetch's SOURCE order (x to w, before the destination swizzle, which differs between the two shaders' registers
# and not between what they compute). (name, fetch ordinal, Xenos format, offset in dwords, source, count)
_TRUNK_WEIGHTS = ("weights", 6, 7, "xyzw", 4)
WIND_SHADERS.update({
    "475EC9F795E5EDBB": _wind_row(
        "trunk", "scene", "half=251.x base=251.y rate1=252.x rate2=252.y two_pi=252.z quarter=252.w lean=253.x "
        "gust=253.y phase_x=254.x phase_y=254.y lo=254.z amp=255.x along=255.y across=255.z rate0=255.w zero=254.w "
        "sel_zero=253.z sel_one=253.w", elements=(_TRUNK_WEIGHTS[:1] + (4,) + _TRUNK_WEIGHTS[1:],)),
    "FBD39C64463E180B": _wind_row(
        "trunk", "caster", "half=251.x base=254.w rate1=252.x rate2=252.y two_pi=252.z quarter=252.w lean=253.x "
        "gust=253.y phase_x=254.x phase_y=254.y lo=254.z amp=255.x along=255.y across=255.z rate0=255.w zero=253.z "
        "sel_zero=253.z sel_one=253.w", elements=(_TRUNK_WEIGHTS[:1] + (1,) + _TRUNK_WEIGHTS[1:],)),
})


def _wind_phase(q, lit):
    """A phase as the tree shaders wrap it, in [-pi, pi): its sine is sin(2 pi q)."""
    return _wind_frac(_wind_frac(q) + lit["half"]) * lit["two_pi"] + lit["lo"]


def wind_tree_terms(w4, c15, c16, c17, lit):
    """(along, across, u, v): the four amplitudes wind_tree_offset is made of. `w4` = (b, a, d, c): the four
    values of the 8_8_8_8 element in the fetch's source order.

    With t = c15.x (the tree's clock: seconds plus the tree's own phase), g = c17.w * gust (c17.w: twice the
    clock) and s(q) = sin of the wrapped phase 2 pi q (instruction numbers of 475E):
        s0 = s(rate0 * t)                                                        17, 18, 22 to 27
        s1 = s(rate0 * b + d * c16.x + (c16.y + rate1) * t + g)                  17 to 26
        s2 = s(d * c16.x + (c16.y + rate2) * t + g)                              17 to 28
        along  = d^2 * c17.z * along * (amp * s1 * (|amp * s0| * quarter + base) + c17.z * lean)     26 to 31
        across = d^2 * c17.z * across * s2                                                           26 to 31
        s3 = s(phase_y * t + b)                                                  35 to 41, 46
        s4 = s((c16.z * t + g + phase_x) * c + b)                                40 to 48
        s5 = s((c16.z * t + g + phase_y) * c + b)                                40 to 49
        S  = (amp * s3 + half) * a * (half * c17.z) * c15.z                      50 to 54
        u  = amp * s4 * S,  v = amp * s5 * S                                     50, 55
    along and across are the sway of the whole tree along the wind's direction c17.xy and across it, growing
    with the square of d; u and v are a branch's own wobble, scaled by a."""
    b, a, d, c = w4
    t = c15[0]
    g = c17[3] * lit["gust"]
    s0 = math.sin(_wind_phase(t * lit["rate0"], lit))
    s1 = math.sin(_wind_phase(b * lit["rate0"] + (d * c16[0] + ((c16[1] + lit["rate1"]) * t + g)), lit))
    s2 = math.sin(_wind_phase(d * c16[0] + ((c16[1] + lit["rate2"]) * t + g), lit))
    dd = d * c17[2] * d
    along = dd * lit["along"] * (lit["amp"] * s1 * (abs(lit["amp"] * s0) * lit["quarter"] + lit["base"]) + c17[2] * lit["lean"])
    across = dd * lit["across"] * s2
    s3 = math.sin(_wind_phase(t * lit["phase_y"] + b, lit))
    turn = c16[2] * t + g
    s4 = math.sin(_wind_phase((turn + lit["phase_x"]) * c + b, lit))
    s5 = math.sin(_wind_phase((turn + lit["phase_y"]) * c + b, lit))
    scale = (lit["amp"] * s3 + lit["half"]) * a * (lit["half"] * c17[2]) * c15[2]
    return along, across, lit["amp"] * s4 * scale, lit["amp"] * s5 * scale


def wind_tree_offset(w4, c15, c16, c17, lit):
    """What the wind adds to a trunk vertex, and to a leaf's pivot (the two families hold one block):
        x = along * c17.x - across * c17.y + u                                   32 to 37, 58
        y = along * c17.y + across * c17.x + v
        z = -quarter * along^2 - half * (u^2 + v^2)                              38, 40, 49, 56, 57
    The drop of z is the second-order term that keeps a swaying branch near its length."""
    along, across, u, v = wind_tree_terms(w4, c15, c16, c17, lit)
    return (along * c17[0] - across * c17[1] + u, along * c17[1] + across * c17[0] + v,
            -(along * along) * lit["quarter"] - (u * u + v * v) * lit["half"])


def wind_trunk(p, w4, c15, c16, c17, lit):
    """The position 475E's instr 65 makes (FBD3: instr 51): p + wind_tree_offset. `p` the fetched position read
    as yxw1, `w4` the four values of fetch 4 in the fetch's source order, `lit` the named literals of the
    shader's c251..c255. A vertex whose four values are zero stands. c16.w and c15.y, c15.w are not read."""
    off = wind_tree_offset(w4, c15, c16, c17, lit)
    return tuple(p[k] + off[k] for k in range(3))


def wind_reach_tree(w4_max, c15, c16, c17, lit):
    """An upper bound of |wind_tree_offset| for a mesh whose four values are at most `w4_max` each (1 when
    they are not known), each sine taken as 1."""
    w = abs(w4_max)
    dd = w * w * abs(c17[2])
    along = dd * abs(lit["along"]) * (abs(lit["amp"]) * (abs(lit["amp"]) * abs(lit["quarter"]) + abs(lit["base"]))
                                      + abs(c17[2] * lit["lean"]))
    across = dd * abs(lit["across"])
    wobble = abs(lit["amp"]) * (abs(lit["amp"]) + abs(lit["half"])) * w * abs(lit["half"] * c17[2]) * abs(c15[2])
    flat = math.hypot(c17[0], c17[1]) * math.hypot(along, across) + math.sqrt(2.0) * wobble
    drop = abs(lit["quarter"]) * along * along + abs(lit["half"]) * 2.0 * wobble * wobble
    return math.hypot(flat, drop)


# --- the trees: leaf clumps (plan Task 7) -----------------------------------------------------------------------

WIND_LITERAL_ORDER["leaf"] = WIND_LITERAL_ORDER["trunk"] + (
    "hundred", "slack", "phase_f", "flutter_x", "flutter_y", "one", "back", "push_gust", "lift_gust", "rate_f")
WIND_LITERALS["leaf"] = dict(WIND_LITERALS["trunk"], **dict(zip(WIND_LITERAL_ORDER["leaf"][18:], (_wind_f32(v) for v in (
    100.0, -0.3, 0.05, 0.25, 1.0, 1.0, 0.28867501, 0.55, 0.9, 0.15)))))
WIND_REGISTERS["leaf"] = (15, 16, 17)
# A leaf's elements: the pivot (three halves of dword 5, read as the position is: yxw), the 8_8_8_8 of dword 7
# as a trunk's, and the 8_8_8_8 of dword 8, of which the flutter reads three values (source x, y, z).


def _leaf_elements(pivot, weights, flutter):
    return (("pivot", pivot, 32, 5, "yxw", 3), ("weights", weights, 6, 7, "xyzw", 4), ("flutter", flutter, 6, 8, "xyz", 3))


_LEAF_D7DD = ("half=252.z base=248.y rate1=252.x rate2=252.y two_pi=250.x quarter=250.y lean=251.x gust=251.y "
              "phase_x=250.w phase_y=250.z lo=248.x amp=253.x along=253.y across=253.z rate0=254.w zero=251.z "
              "sel_zero=251.z sel_one=251.w hundred=248.z slack=249.x phase_f=249.y flutter_x=249.z flutter_y=249.w "
              "one=252.w back=253.w push_gust=254.x lift_gust=254.y rate_f=254.z")
WIND_SHADERS.update({
    "A5846836C90E1192": _wind_row(
        "leaf", "scene", "half=253.z base=249.y rate1=253.x rate2=253.y two_pi=251.x quarter=251.y lean=252.x "
        "gust=252.y phase_x=251.w phase_y=251.z lo=249.x amp=254.x along=254.y across=254.z rate0=255.w zero=252.z "
        "sel_zero=252.z sel_one=252.w hundred=249.z slack=250.x phase_f=250.y flutter_x=250.z flutter_y=250.w "
        "one=253.w back=254.w push_gust=255.x lift_gust=255.y rate_f=255.z", elements=_leaf_elements(3, 4, 5)),
    # A584 with two lights: its literals one register lower.
    "D7DD31B9BF194DE8": _wind_row("leaf", "scene", _LEAF_D7DD, elements=_leaf_elements(3, 4, 5)),
    # A584 with four lights: D7DD's places but for the three of its c248.
    "2D40B53C926109BE": _wind_row(
        "leaf", "scene", _LEAF_D7DD.replace("base=248.y", "base=248.z").replace("hundred=248.z", "hundred=248.w"),
        elements=_leaf_elements(3, 4, 5),
        note="In one capture only (native_discovery_20261010_182130, frame 8: three rows); otherwise held by made inputs."),
    # The leaves' shadow caster, and with the pixel shader F1EB the sun layer's tree occluder.
    "432563420047C96C": _wind_row(
        "leaf", "caster", "half=249.y base=250.w rate1=253.x rate2=253.y two_pi=252.z quarter=252.w lean=254.x "
        "gust=254.y phase_x=251.y phase_y=251.x lo=249.x amp=255.x along=255.y across=255.z rate0=250.z zero=254.z "
        "sel_zero=254.z sel_one=254.w hundred=252.y slack=251.z phase_f=251.w flutter_x=250.x flutter_y=250.y "
        "one=254.w back=255.w push_gust=253.z lift_gust=253.w rate_f=252.x", elements=_leaf_elements(2, 3, 4)),
})


def wind_leaf_lift(p, q, w4, v3, c15, c16, c17, lit):
    """What the flutter adds to the third coordinate of a leaf vertex's direction P - Q before it is brought
    back to its length. `v3`: the three values (source x, y, z) of the 8_8_8_8 of dword 8.

    With t, g, b, d as wind_tree_terms has them, L = |P - Q| and T = t + c17.w * rate_f (instruction numbers
    of A584):
        s6 = s(rate_f * b + (c16.y + rate1) * t + g + c16.x * d + phase_f)       29, 73, 76 to 82
        s7 = s(T * flutter_x + v.z + hundred * v.x)                              72 to 79, 83, 86, 88
        s8 = s(T * flutter_y + v.y)                                              75, 77 to 87
        k  = amp * s6 where s6 >= 0, back * s6 where it is below                  84, 85
        push = amp * s7 * max(L + slack, 0) * c16.w                              73, 89 to 92
        lift = c15.w * (|k| * amp * s8) * k                                      88 to 91
        push * (push_gust * c17.z + one) + lift * (lift_gust * c17.z + one)      72, 79, 93
    push grows with the vertex's distance from its pivot beyond -slack; lift is the same for a whole clump."""
    b, d = w4[0], w4[2]
    t = c15[0]
    g = c17[3] * lit["gust"]
    length = math.sqrt(sum((p[k] - q[k]) ** 2 for k in range(3)))
    clock = t + c17[3] * lit["rate_f"]
    s6 = math.sin(_wind_phase(b * lit["rate_f"] + (((c16[1] + lit["rate1"]) * t + g) + c16[0] * d + lit["phase_f"]), lit))
    s7 = math.sin(_wind_phase((clock * lit["flutter_x"] + v3[2]) + lit["hundred"] * v3[0], lit))
    turn = _wind_frac(clock * lit["flutter_y"] + v3[1])        # the shader doubles it and takes half of that (78, 80)
    s8 = math.sin(_wind_frac((turn + turn) * lit["half"] + lit["half"]) * lit["two_pi"] + lit["lo"])
    k = lit["back"] * s6 if -(lit["amp"] * s6) > 0.0 else lit["amp"] * s6
    push = lit["amp"] * s7 * max(length + lit["slack"], 0.0) * c16[3]
    lift = c15[3] * (abs(k) * lit["amp"] * s8) * k
    return push * (c17[2] * lit["push_gust"] + lit["one"]) + lift * (c17[2] * lit["lift_gust"] + lit["one"])


def wind_leaf(p, q, w4, v3, c15, c16, c17, lit):
    """The position A584's instr 98 makes (D7DD 100, 2D40 102, the caster 4325 87):
        Q' = Q + wind_tree_offset(w4, ...)                       the pivot sways as a trunk vertex does (22 to 74)
        D  = P - Q,  D' = (D.x, D.y, D.z + wind_leaf_lift(...))  (26, 75 to 93)
        position = Q' + |D| * D' / |D'|                           (35, 40, 94 to 98)
    `p` the fetched position read as yxw1, `q` the pivot (fetch 3, read the same way), `w4` the four values of
    fetch 4 and `v3` the first three of fetch 5 in the fetches' source order. The vertex keeps its distance from
    its pivot. Where D' has no length (a vertex at its pivot with no lift) the position is Q': the microcode
    multiplies 0 by 1 / 0 there, which is +0 by the console's rule for a product with a zero factor."""
    off = wind_tree_offset(w4, c15, c16, c17, lit)
    pivot = tuple(q[k] + off[k] for k in range(3))
    d = [p[k] - q[k] for k in range(3)]
    length = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    d[2] += wind_leaf_lift(p, q, w4, v3, c15, c16, c17, lit)
    turned = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    if turned == 0.0:
        return pivot
    return tuple(pivot[k] + d[k] / turned * length for k in range(3))


def wind_reach_leaf(w4_max, length_max, c15, c16, c17, lit):
    """An upper bound of |wind_leaf - p| for a mesh whose four values are at most `w4_max` and whose vertices
    are at most `length_max` from their pivots: the pivot's reach, and the turn of the direction, which moves
    the vertex by at most twice the lift (and never by more than twice its distance from the pivot)."""
    length = abs(length_max)
    k = max(abs(lit["amp"]), abs(lit["back"]))
    push = abs(lit["amp"]) * max(length + lit["slack"], 0.0) * abs(c16[3])
    lift = abs(c15[3]) * k * abs(lit["amp"]) * k
    turn = push * (abs(c17[2] * lit["push_gust"]) + abs(lit["one"])) + lift * (abs(c17[2] * lit["lift_gust"]) + abs(lit["one"]))
    return wind_reach_tree(w4_max, c15, c16, c17, lit) + min(2.0 * turn, 2.0 * length)


def wind_table_member(hash_):
    """The "wind" member of a shader's entry of docs/native-renderer/vs-transforms.json, from its row of
    WIND_SHADERS (tests/test_wind_formulas.py holds the committed table to it; gen_transform_table.py writes
    src/native/capture/wind_table.inc from the table)."""
    row = WIND_SHADERS[hash_]
    return {"family": row["family"], "role": row["role"], "reg": list(row["reg"]), "lit_reg": list(row["lit_reg"]),
            "lit": dict(row["lit"]),
            "lit_at": {name: "c%d.%s" % (reg, "xyzw"[comp]) for name, (reg, comp) in row["lit_at"].items()},
            "elements": [{"name": name, "fetch": fetch, "format": fmt, "offset": offset, "source": source}
                         for name, fetch, fmt, offset, source, _count in row["elements"]],
            "evidence": row["evidence"]}
