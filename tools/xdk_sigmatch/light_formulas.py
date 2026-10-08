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
