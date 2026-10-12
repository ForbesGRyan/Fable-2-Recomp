"""The water step's HLSL text held to the Python reference by value: the translator's rules for it and the test's
frame (the water sub-project, plan Task 7). The text itself is plan Task 12's.

As tests/test_wind_hlsl.py and tests/test_impostor_hlsl.py do for their texts: the functions between the markers
`// WATER VERTEX TEXT BEGIN` / `END` and `// WATER PIXEL TEXT BEGIN` / `END` of src/native/fable2_native_shaders.h
are translated line by line to Python (tools/xdk_sigmatch/wind_hlsl.py) and evaluated in double against
light_formulas (water_vertex, water_alpha, water_normal, water_lookup, water_base, water_mix, water_sun) at made
inputs in which every register component differs and EVERY COMPONENT NO FORMULA READS IS NO NUMBER
(water_check.UNREAD), and at the inputs of the pins (water_pins.py).

SINCE PLAN TASK 12 THE HEADER HAS THE WATER TEXT and class `TheText` RUNS (the suite's skip count is the one of
before the frame). Every rule and helper is also held on FIXTURE, a text inside this file that is the reference
written as HLSL (class `Rules`), which is how the frame could fail before the text existed; `TheText` skipped then,
with a message that names plan Task 12. How a skip is kept from staying unnoticed, should the regions ever go:
  1. `Tripwire.test_a_water_program_without_its_text_fails` does not skip: the moment the header names the water
     step's programs (kClayWaterVs or kClayWaterPs) without both marked regions, it FAILS.
  2. `Tripwire.test_what_is_skipped_is_known` holds the list of TheText's tests and the reason's wording, so that the
     suite's "skipped" count rises by exactly len(THE_TEXT_TESTS) (9) while they wait, and by 0 from Task 12 on:
     plan Task 12's step asks the count back.
  3. From Task 12 on MUTANTS must hold at least 12 wrong texts (test_each_wrong_text_fails fails on fewer).

What the text must be (the frame's contract with plan Task 12; where it differs from the plan's Produces of Task 7
it is said in the Tasks 4-6-7 report): the functions of wind_hlsl.WATER_VERTEX and WATER_PIXEL with the
signatures of FIXTURE (a register is passed whole, as a float4 with its garbage: a text that reads a component
no formula reads gives no number here); the operations in the reference's order (TOLERANCE is 1e-9: both sides
are double); and, by ledger ruling 34:
  - the console's zero product written out at the three reciprocal-square-root sites (the normal, the view vector,
    the sun's second normal): `(x == 0.0) ? 0.0 : rsqrt(x)`, so that a flat normal with c128.x = 0, a pixel at
    the eye and c136.x = 0 on a flat pair give the reference's value and not an accident of a saturate;
  - the power as `(cosine > 0.0) ? exp2(c138.x * log2(cosine)) : 0.0`: 0 on a cosine of 0 whatever the power. That
    is the reference for every power above 0; at a power of exactly 0 the console gives 1 (the sun's whole colour)
    and this text 0: the record's rule keeps c138.x above 0 (water_table.inc, kind 3), so no drawn frame has it;
  - WaterFinite(colour, alpha): 1 when all four are finite, else 0; the pixel main DISCARDS at 0 (the draw is
    blended: a no-number clamped to 0 would be a black pixel at alpha 0.997). The main is not reached from here:
    TheText holds its line by text, tests/native/test_native_shaders.cpp (case 55) the compiled program.

Not reached from here: the compiler, `precise` and the GPU's arithmetic, the mains (the fetches, the fog, the
exposure, the discard, the bools), the position's four dot products."""
import math
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import water_check  # noqa: E402
import water_pins  # noqa: E402
import wind_hlsl  # noqa: E402

Vec = wind_hlsl.Vec
NAN = math.nan
MADE = 200              # made inputs
TOLERANCE = 1e-9        # of max(1, |value|): double against double, the same order of operations
PIN_BOUND = 1e-4        # the double text against the single-precision pins
MUTANT_FLOOR = 1e-4     # a changed text must leave the reference by at least this (of max(1, |value|)) somewhere
NAMES = ("coord", "alpha", "fate", "sum", "normal", "lookup", "view", "fresnel", "base", "mix", "sun")
CALLBACKS = ()          # the texts' functions take texels, not samplers; a main's sample is a callback (Rules holds the rule)

# The reference written as HLSL: what plan Task 12's text may start from. Every function of WATER_VERTEX and
# WATER_PIXEL, in the reference's order of operations.
FIXTURE = '''// WATER VERTEX TEXT BEGIN
float4 WaterCoords(float3 p, float4 c115, float4 c116, float4 c117, float4 c118) {
  return float4(p.x * c117.x + c115.x, p.y * c117.y + c115.y, p.x * c118.x + c116.x, p.y * c118.y + c116.y);
}
// WATER VERTEX TEXT END
// WATER PIXEL TEXT BEGIN
float WaterAlpha(float m, float4 c141) {
  precise float ramp = saturate(-(1.0 - m) * c141.y + c141.x);
  return 1.0 - ramp;
}
float2 WaterFate(float alpha, float c141w, float alpha_ref) {
  precise float alive = (alpha == 1.0) ? 0.0 : 1.0;
  precise float shaded = (alpha > c141w) ? alive : 0.0;
  precise float drawn = (alpha > alpha_ref) ? alive : 0.0;
  return float2(shaded, drawn);
}
float2 WaterNormalSum(float2 a, float2 b) {
  return a * 2.0 + (b * 2.0 + -2.0);
}
float3 WaterNormal(float2 s, float2 a, float2 b, float c128x) {
  precise float2 da = a * 2.0 + -1.0;
  precise float2 db = b * 2.0 + -1.0;
  precise float za = sqrt(abs(-(da.x * da.x + da.y * da.y) + 1.0));
  precise float zb = sqrt(abs(-(db.x * db.x + db.y * db.y) + 1.0));
  precise float3 n = float3(s.x * 0.5, s.y * 0.5, (0.5 * c128x) * (za + zb));
  precise float nn = abs((n.z * n.z + n.x * n.x) + n.y * n.y);
  precise float inv = (nn == 0.0) ? 0.0 : rsqrt(nn);
  return n * inv;
}
float4 WaterLookup(float2 q, float2 s, float4 c0, float4 c31, float4 c129, float4 c130) {
  precise float sx = abs(q.x) * c0.z;
  precise float sy = abs(q.y) * c0.w;
  precise float u = ((-0.5 + c0.x) + sx) * c31.x + 0.5;
  precise float v = (c0.y + (-0.5 + sy)) * c31.y + 0.5;
  return float4(u + (c129.x * 0.5) * s.x, v + (c129.y * 0.5) * s.y, u + (s.x * 0.5) * c130.x, v + (s.y * 0.5) * c130.y);
}
float4 WaterView(float3 P, float3 eye) {
  precise float3 e = -P + eye;
  precise float dd = abs((e.z * e.z + e.x * e.x) + e.y * e.y);
  precise float inv = (dd == 0.0) ? 0.0 : rsqrt(dd);
  return float4(e * inv, sqrt(dd));
}
float WaterFresnel(float3 V, float3 N, float c116x) {
  precise float cosine = (V.z * N.z + V.x * N.x) + V.y * N.y;
  return saturate((-cosine + 1.0) + c116x);
}
float3 WaterBase(float F, float4 c126, float4 c127) {
  return (-c127.xyz + c126.xyz) * F + c127.xyz;
}
float3 WaterMix(float3 base, float3 r, float3 near14, float F, float d, float c117x, float c131x, float c140x) {
  precise float share = saturate(c131x);
  precise float t = saturate(F + c117x);
  precise float w = saturate(c140x * d);
  precise float3 beyond = (r - base) * share + base;
  precise float3 within = ((r - near14) * t + (-base + near14)) * share + base;
  return w * (beyond - within) + within;
}
float3 WaterSun(float3 N, float3 V, float c136x, float c137x, float c138x, float4 c142, float4 c143) {
  precise float z2 = c136x * N.z;
  precise float nn = abs(z2 * z2 + ((N.x * N.x + N.y * N.y) + 0.0));
  precise float inv = (nn == 0.0) ? 0.0 : rsqrt(nn);
  precise float3 n2 = float3(N.x * inv, N.y * inv, z2 * inv);
  precise float twice = (n2.z * -c142.z + n2.x * -c142.x) + n2.y * -c142.y;
  precise float both = twice + twice;
  precise float3 w = -n2 * both + -c142.xyz;
  precise float cosine = saturate((-V.z * w.z + -V.x * w.x) + -V.y * w.y);
  precise float power = (cosine > 0.0) ? exp2(c138x * log2(cosine)) : 0.0;
  precise float scaled = c137x * power;
  return float3((c143.x * c137x) * power, scaled * c143.y, scaled * c143.z);
}
float WaterFinite(float3 colour, float alpha) {
  precise float sum = (abs(colour.x) + abs(colour.y)) + (abs(colour.z) + abs(alpha));
  return (sum < 3.0e38) ? 1.0 : 0.0;
}
// WATER PIXEL TEXT END
'''

# (name, the text as it stands, the changed text, the comparisons it must break): wrong texts of FIXTURE, so that the
# frame is seen to fail today. Plan Task 12 fills MUTANTS with at least 12 of the header's own text.
FIXTURE_MUTANTS = (
    ("c116 for c115 in the first coordinate", "p.x * c117.x + c115.x,", "p.x * c117.x + c116.x,", ("coord",)),
    ("the coordinates from p.xz", "p.y * c117.y + c115.y,", "p.z * c117.y + c115.y,", ("coord",)),
    ("the ramp's two registers exchanged", "saturate(-(1.0 - m) * c141.y + c141.x)", "saturate(-(1.0 - m) * c141.x + c141.y)", ("alpha",)),
    ("the mask for one minus the mask", "saturate(-(1.0 - m) * c141.y", "saturate(-m * c141.y", ("alpha",)),
    ("c128.x left out", "(0.5 * c128x) * (za + zb)", "0.5 * (za + zb)", ("normal",)),
    ("the scale about 0, not the centre", "precise float u = ((-0.5 + c0.x) + sx) * c31.x + 0.5;", "precise float u = (c0.x + sx) * c31.x;", ("lookup",)),
    ("c130 for c129 at slot 13", "u + (c129.x * 0.5) * s.x,", "u + (c130.x * 0.5) * s.x,", ("lookup",)),
    ("the lookup's axes exchanged", "precise float sx = abs(q.x) * c0.z;", "precise float sx = abs(q.y) * c0.z;", ("lookup",)),
    ("the fresnel bias with the other sign", "saturate((-cosine + 1.0) + c116x)", "saturate((-cosine + 1.0) - c116x)", ("fresnel",)),
    ("c126 and c127 exchanged", "(-c127.xyz + c126.xyz) * F + c127.xyz", "(-c126.xyz + c127.xyz) * F + c126.xyz", ("base",)),
    ("the mix's c131.x left out", "precise float share = saturate(c131x);", "precise float share = 1.0;", ("mix",)),
    ("near and far exchanged", "return w * (beyond - within) + within;", "return w * (within - beyond) + beyond;", ("mix",)),
    ("c140.x by 1%", "precise float w = saturate(c140x * d);", "precise float w = saturate(1.01 * c140x * d);", ("mix",)),
    ("slot 14 left out of the near branch", "((r - near14) * t + (-base + near14))", "(r - base)", ("mix",)),
    ("c136.x left out", "precise float z2 = c136x * N.z;", "precise float z2 = N.z;", ("sun",)),
    ("the power by 1%", "exp2(c138x * log2(cosine))", "exp2(1.01 * c138x * log2(cosine))", ("sun",)),
    ("c142 with the other sign", "precise float3 w = -n2 * both + -c142.xyz;", "precise float3 w = n2 * both + c142.xyz;", ("sun",)),
    ("c137.x left out", "precise float scaled = c137x * power;", "precise float scaled = power;", ("sun",)),
    ("the mix factor not saturated", "precise float share = saturate(c131x);", "precise float share = c131x;", ("mix",)),
    # (ledger ruling 43) the two thresholds folded into one, either way; the kill left out; the kill by the opaque twin's rule
    ("drawn by c141.w, not by the state's reference", "precise float drawn = (alpha > alpha_ref) ? alive : 0.0;",
     "precise float drawn = (alpha > c141w) ? alive : 0.0;", ("fate",)),
    ("shaded by the state's reference, not by c141.w", "precise float shaded = (alpha > c141w) ? alive : 0.0;",
     "precise float shaded = (alpha > alpha_ref) ? alive : 0.0;", ("fate",)),
    ("the kill at an alpha of 1 left out", "precise float alive = (alpha == 1.0) ? 0.0 : 1.0;", "precise float alive = 1.0;", ("fate",)),
)
# The header's wrong texts (plan Task 12): each site stands exactly once in the WHOLE header and breaks the comparison
# it names. The fixture's own, since the header's text is the fixture's but for WaterFinite (review of Tasks 4 to
# 15, Minor 4: every wrong fixture is applied to the header too).
MUTANTS = FIXTURE_MUTANTS

HEADER = (ROOT / "src" / "native" / "fable2_native_shaders.h").read_text(encoding="utf-8")
HAS_TEXT = wind_hlsl.WATER_VERTEX[0] in HEADER and wind_hlsl.WATER_PIXEL[0] in HEADER
NO_TEXT = ("the header has no water text yet: plan Task 12 writes it and these tests must then RUN (a skip after Task 12 is a "
           "failure of that task)")
THE_TEXT_TESTS = ("test_a_colour_that_is_no_number_is_discarded", "test_an_unread_component_reaches_no_result", "test_each_wrong_text_fails",
                  "test_the_console_s_rule_stands_at_the_three_sites_and_the_power", "test_the_text_is_the_reference_at_made_inputs",
                  "test_the_text_is_the_reference_at_the_pins", "test_the_text_writes_the_guards_out",
                  "test_the_translation_reads_both_texts_whole", "test_the_two_thresholds_are_the_main_s")


class Case:
    """One made or pinned input of the texts: the vertex's position, the pixel's position register, place, two
    normal samples, mask sample, two picture texels, the registers ({register: [4]}; a component no formula reads
    is no number), and what it is ("" or the name of a degenerate kind)."""

    def __init__(self, name, p, q, P, a, b, m, r13, r14, vertex, pixel, label="", alpha_ref=None):
        (self.name, self.p, self.q, self.P, self.a, self.b, self.m, self.r13, self.r14, self.vertex, self.pixel, self.label) = (
            name, p, q, P, a, b, m, r13, r14, vertex, pixel, label)
        # the state's alpha reference: a number of its own, on either side of c141.w (a made case: three quarters or
        # five quarters of it by the case's parity; the pins: the pin's)
        self.alpha_ref = alpha_ref


def cases(n=MADE, seed=1):
    """`n` made inputs (water_check.made_pixel's, without its dumps): every register a formula reads about its
    captured value and every component no formula reads NO NUMBER; four of forty at the points of the console's
    zero product (water_check.PIXEL_KINDS)."""
    out = []
    for k in range(n):
        made = water_check.made_pixel(seed, k, text="")
        pixel = {reg: [NAN if c in water_check.UNREAD[reg] else v for c, v in enumerate(made.consts[reg])] for reg in lf.WATER_SHADER["ps_reg"]}
        vertex = {reg: [made.consts[reg][0] * 0.37 + 0.11 * (reg - 114), made.consts[reg][1] * 0.21 - 0.07 * (reg - 114), NAN, NAN]
                  for reg in lf.WATER_SHADER["reg"]}
        coords = made.regs[1]
        a, b = made.normal(coords[0], coords[1]), made.normal(coords[2], coords[3])
        out.append(Case("made %d%s" % (k, " (%s)" % made.label if made.label else ""), made.regs[0][:3], made.regs[4][:2], made.regs[0][:3],
                        a, b, made.m, made.mirror(0.3 + 0.01 * k, 0.6)[:3], made.near14(0.7, 0.2 + 0.01 * k)[:3], vertex, pixel, made.label,
                        alpha_ref=pixel[141][3] * (0.75 if k % 2 else 1.25)))
    return out


def pin_cases():
    """[(Case, the pin's results)] of water_pins.py's pixel lines (the plan's `pin_case(line)`: taken from the pins'
    own tuples, not from the printed text, so that no parser stands between)."""
    out = []
    names = ["captured %d" % k for k in range(len(water_pins.CAPTURED_PIXELS))] + ["made: " + kind for kind in water_pins.MADE_KINDS]
    for name, item in zip(names, water_pins.CAPTURED_PIXELS + water_pins.MADE_PIXELS):
        (q, P, coord, m, c, word, a, b, r13, r14, shadow, scatter, transmit), result = water_pins.pixel_pin(item)
        vertex, _, _ = water_pins.row_registers(item[0])
        label = name[6:] if name[6:] in water_check.PIXEL_KINDS.values() else ""
        out.append((Case(name, P, q, P, a, b, m, r13, r14, {reg: vertex[reg] for reg in lf.WATER_SHADER["reg"]}, c, label), result))
    return out


def programs(text=FIXTURE):
    """(the vertex text's functions, the pixel text's) of a header text."""
    env = wind_hlsl.water_env()
    vertex = wind_hlsl.Region(text, *wind_hlsl.WATER_VERTEX, known=wind_hlsl.WATER_INTRINSICS + CALLBACKS).functions(env)
    pixel = wind_hlsl.Region(text, *wind_hlsl.WATER_PIXEL, known=wind_hlsl.WATER_INTRINSICS + CALLBACKS).functions(env)
    return vertex, pixel


def _flat(x):
    if isinstance(x, Vec):
        return list(x.values)
    if isinstance(x, (list, tuple)):
        return [float(v) for v in x]
    return [float(x)]


def evaluate(vertex, pixel, case):
    """{name: floats} of the texts' results for a case (NAMES, and "finite": WaterFinite of the mix plus the sun)."""
    v, c = case.vertex, case.pixel
    reg = {r: Vec(*c[r]) for r in c}
    s = pixel["WaterNormalSum"](Vec(*case.a), Vec(*case.b))
    normal = pixel["WaterNormal"](s, Vec(*case.a), Vec(*case.b), c[128][0])
    view = pixel["WaterView"](Vec(*case.P), reg[19].xyz)
    fresnel = pixel["WaterFresnel"](view.xyz, normal, c[116][0])
    base = pixel["WaterBase"](fresnel, reg[126], reg[127])
    mix = pixel["WaterMix"](base, Vec(*case.r13), Vec(*case.r14), fresnel, view.w, c[117][0], c[131][0], c[140][0])
    sun = pixel["WaterSun"](normal, view.xyz, c[136][0], c[137][0], c[138][0], reg[142], reg[143])
    alpha = pixel["WaterAlpha"](case.m, reg[141])
    out = {"coord": vertex["WaterCoords"](Vec(*case.p), *[Vec(*v[r]) for r in lf.WATER_SHADER["reg"]]),
           "alpha": alpha, "fate": pixel["WaterFate"](alpha, c[141][3], _reference_alpha(case)), "sum": s, "normal": normal,
           "lookup": pixel["WaterLookup"](Vec(*case.q), s, reg[0], reg[31], reg[129], reg[130]),
           "view": view, "fresnel": fresnel, "base": base, "mix": mix, "sun": sun,
           "finite": pixel["WaterFinite"](mix + sun, alpha)}
    return {name: _flat(x) for name, x in out.items()}


def _reference_alpha(case):
    return case.pixel[141][3] if case.alpha_ref is None else case.alpha_ref


def fate_reference(alpha, c141, alpha_ref):
    """(shaded, drawn) as 0 or 1 from the reference: shaded by light_formulas.water_kept (not killed, alpha above
    c141.w: the shader's own threshold; else the colour is 0); drawn when not killed and alpha above the state's alpha
    reference (the alpha test, function "greater": water_pins.alpha_pin's rule)."""
    fate = lf.water_kept(alpha, c141, "surface")
    return [1.0 if fate == "shaded" else 0.0, 1.0 if fate != "killed" and alpha > alpha_ref else 0.0]


def reference(case):
    """The same dict from light_formulas (the double reference)."""
    c = case.pixel
    s, normal = lf.water_normal(case.a, case.b, c[128][0])
    at13, at14 = lf.water_lookup(case.q, s, c[0], c[31], c[129], c[130])
    base, fresnel, view, d = lf.water_base(case.P, c[19], normal, c[116][0], c[126], c[127])
    mix = lf.water_mix(base, case.r13, case.r14, fresnel, d, c[117][0], c[131][0], c[140][0])
    sun = lf.water_sun(normal, view, c[136][0], c[137][0], c[138][0], c[142], c[143])
    rows = {reg: [0.0] * 4 for reg in (0, 1, 2, 3)}             # the position's rows are the main's, not a function's
    rows.update(case.vertex)
    out = {"coord": lf.water_vertex(case.p, (0.0, 0.0), rows)["coord"],
           "alpha": lf.water_alpha(case.m, c[141]), "fate": fate_reference(lf.water_alpha(case.m, c[141]), c[141], _reference_alpha(case)),
           "sum": s, "normal": normal, "lookup": list(at13) + list(at14),
           "view": list(view) + [d], "fresnel": fresnel, "base": base, "mix": mix, "sun": sun, "finite": 1.0}
    return {name: _flat(x) for name, x in out.items()}


def _gap(got, want):
    worst = 0.0
    for g, w in zip(got, want):
        if g != g or w != w or math.isinf(g) or math.isinf(w):
            if not (g == w or (g != g and w != w)):
                return math.inf
            continue
        worst = max(worst, abs(g - w) / max(1.0, abs(w)))
    return worst


def skipped(case):
    """The comparisons a case is not held to: the sun at a power of exactly 0 (module text: the text gives 0 there,
    the console the sun's whole colour; the record keeps c138.x above 0)."""
    return ("sun",) if case.label == "power_zero" else ()


def compare(vertex, pixel, case, bound):
    """The names whose values differ from the reference by more than `bound` of max(1, |value|)."""
    got, want = evaluate(vertex, pixel, case), reference(case)
    return [name for name in NAMES + ("finite",) if name not in skipped(case) and _gap(got[name], want[name]) > bound]


def leave(vertex, pixel, case, names):
    """The largest difference from the reference among `names`."""
    got, want = evaluate(vertex, pixel, case), reference(case)
    return max(_gap(got[name], want[name]) for name in names if name not in skipped(case)) if set(names) - set(skipped(case)) else 0.0


def check_reference(test, vertex, pixel):
    for case in cases(MADE):
        test.assertEqual(compare(vertex, pixel, case, TOLERANCE), [], case.name)


def check_pins(test, vertex, pixel):
    pins = pin_cases()
    test.assertGreaterEqual(len(pins), 16)
    test.assertGreaterEqual(len([line for line in water_pins.lines() if not line.startswith("//")]), 24)
    for case, result in pins:
        fate, alpha, colour, at13, at14, normal, fresnel, distance, sun, fog_at = result
        got = evaluate(vertex, pixel, case)
        test.assertLessEqual(abs(got["alpha"][0] - alpha), PIN_BOUND, case.name)
        if fate != 2:
            continue
        want = {"lookup": list(at13) + list(at14), "normal": normal, "fresnel": [fresnel], "sun": sun}
        for name, values in want.items():
            if name in skipped(case):
                continue
            test.assertLessEqual(_gap(got[name], values), PIN_BOUND, (case.name, name))
        test.assertLessEqual(_gap([got["view"][3]], [distance]), PIN_BOUND, case.name)


def check_unread(test, vertex, pixel):
    for case in cases(MADE):                                   # every UNREAD component of a made case is no number
        got = evaluate(vertex, pixel, case)
        test.assertTrue(all(math.isfinite(x) for values in got.values() for x in values), case.name)
        test.assertEqual(got["finite"], [1.0], case.name)


def check_console(test, vertex, pixel):
    """Ledger ruling 34 by value: at the points of the console's zero product the text gives the reference's value
    (and, at a power of 0, a 0 and no no-number)."""
    seen = set()
    for case in cases(MADE):
        if not case.label:
            continue
        seen.add(case.label)
        got, want = evaluate(vertex, pixel, case), reference(case)
        test.assertTrue(all(math.isfinite(x) for values in got.values() for x in values), case.name)
        if case.label == "flat_zero":
            test.assertEqual(got["normal"], [0.0, 0.0, 0.0], case.name)
        elif case.label == "at_eye":
            test.assertEqual(got["view"], [0.0, 0.0, 0.0, 0.0], case.name)
        elif case.label == "second_flat":                            # N2 = 0: what is mirrored is -c142 itself, cosine sat(V . c142)
            c142, view = case.pixel[142], got["view"]
            cosine = min(max(sum(view[k] * c142[k] for k in range(3)), 0.0), 1.0)
            test.assertEqual(max(got["sun"]) > 0.0, cosine > 0.0, case.name)
        elif case.label == "power_zero":
            test.assertEqual(got["sun"], [0.0, 0.0, 0.0], case.name)             # the text's choice (module text)
            test.assertGreater(max(want["sun"]), 0.0, case.name)                 # the console's: the sun's whole colour
        test.assertEqual(compare(vertex, pixel, case, TOLERANCE), [], case.name)
    test.assertEqual(seen, set(water_check.PIXEL_KINDS.values()))


def check_finite(test, pixel):
    finite = pixel["WaterFinite"]
    test.assertEqual(finite(Vec(0.5, 1.5, 2.5), 0.997), 1.0)
    test.assertEqual(finite(Vec(0.0, 0.0, 0.0), 0.0), 1.0)
    for bad in (NAN, math.inf, -math.inf):
        for k in range(3):
            colour = [0.5, 1.5, 2.5]
            colour[k] = bad
            test.assertEqual(finite(Vec(*colour), 0.997), 0.0, (bad, k))
        test.assertEqual(finite(Vec(0.5, 1.5, 2.5), bad), 0.0, bad)


def check_mutants(test, text, mutants, least):
    test.assertGreaterEqual(len(mutants), least)
    made = cases(MADE)
    for name, old, new, breaks in mutants:
        test.assertEqual(text.count(old), 1, name)                # each site exactly once in the text
        vertex, pixel = programs(text.replace(old, new))
        test.assertGreater(max(leave(vertex, pixel, case, breaks) for case in made), MUTANT_FLOOR, name)


class Rules(unittest.TestCase):
    """The translator's new rules and the frame's helpers, on FIXTURE: they can fail today."""

    @classmethod
    def setUpClass(cls):
        cls.vertex, cls.pixel = programs()

    def region(self, text, known=None):
        return wind_hlsl.Region(text, *wind_hlsl.WATER_PIXEL, known=wind_hlsl.WATER_INTRINSICS + CALLBACKS if known is None else known)

    def test_the_markers_and_the_functions(self):
        self.assertEqual(wind_hlsl.WATER_VERTEX, ("// WATER VERTEX TEXT BEGIN\n", "// WATER VERTEX TEXT END\n", ("WaterCoords",)))
        self.assertEqual(wind_hlsl.WATER_PIXEL[:2], ("// WATER PIXEL TEXT BEGIN\n", "// WATER PIXEL TEXT END\n"))
        self.assertEqual(wind_hlsl.WATER_PIXEL[2], ("WaterAlpha", "WaterFate", "WaterNormalSum", "WaterNormal", "WaterLookup", "WaterView", "WaterFresnel",
                                                    "WaterBase", "WaterMix", "WaterSun", "WaterFinite"))

    def test_the_fixture_is_the_reference_at_made_inputs(self):
        check_reference(self, self.vertex, self.pixel)
        p = self.pixel
        self.assertAlmostEqual(p["WaterAlpha"](0.0, Vec(2.003, 2.0, NAN, 0.0588)), lf.water_alpha(0.0, (2.003, 2.0, 0.0, 0.0)), places=12)
        s, n = lf.water_normal((0.3, 0.9), (0.6, 0.2), 0.3)
        got = p["WaterNormal"](Vec(*s), Vec(0.3, 0.9), Vec(0.6, 0.2), 0.3)
        for a, b in zip(got.values, n):
            self.assertAlmostEqual(a, b, places=12)

    def test_the_fixture_is_the_reference_at_the_pins(self):
        check_pins(self, self.vertex, self.pixel)

    def test_an_unread_component_reaches_no_result(self):
        check_unread(self, self.vertex, self.pixel)
        # and the frame sees a text that reads one: c126.w in the base colour
        vertex, pixel = programs(FIXTURE.replace("(-c127.xyz + c126.xyz) * F + c127.xyz", "(-c127.xyz + c126.xyz) * F + c127.xyz * (1.0 + 0.0 * c126.w)"))
        self.assertFalse(all(math.isfinite(x) for x in evaluate(vertex, pixel, cases(1)[0])["base"]))

    def test_the_console_s_rule_stands_at_the_three_sites_and_the_power(self):
        check_console(self, self.vertex, self.pixel)
        # each guard is needed: without it the text is not the reference at its point
        bare = (("flat_zero", "precise float inv = (nn == 0.0) ? 0.0 : rsqrt(nn);\n  return n * inv;", "precise float inv = rsqrt(nn);\n  return n * inv;"),
                ("at_eye", "precise float inv = (dd == 0.0) ? 0.0 : rsqrt(dd);", "precise float inv = rsqrt(dd);"),
                ("second_flat", "precise float inv = (nn == 0.0) ? 0.0 : rsqrt(nn);\n  precise float3 n2", "precise float inv = rsqrt(nn);\n  precise float3 n2"),
                ("power_zero", "(cosine > 0.0) ? exp2(c138x * log2(cosine)) : 0.0;", "exp2(c138x * log2(cosine));"))
        made = [case for case in cases(MADE) if case.label]
        for label, old, new in bare:
            self.assertEqual(FIXTURE.count(old), 1, label)
            vertex, pixel = programs(FIXTURE.replace(old, new))
            wrong = 0
            for case in made:
                if case.label != label:
                    continue
                got = evaluate(vertex, pixel, case)
                # no number, or (where a saturate takes the no-number back to 0) another value than the reference's
                wrong += (not all(math.isfinite(x) for values in got.values() for x in values) or got["finite"] != [1.0]
                          or compare(vertex, pixel, case, TOLERANCE) != [])
            self.assertGreater(wrong, 0, label)

    def test_a_colour_that_is_no_number_is_not_finite(self):
        check_finite(self, self.pixel)

    def test_each_wrong_fixture_fails(self):
        check_mutants(self, FIXTURE, FIXTURE_MUTANTS, 12)

    def test_a_line_no_rule_knows_stops_the_translation(self):
        for bad in ("  return normalize(n);", "  float3 x = mul(n, inv);\n  return x;", "  clip(inv - 0.5);\n  return n;", "  return pow(n, inv);",
                    "  return n * inv; }}", "  for (int i = 0; i < 2; i++) n = n * inv;\n  return n;"):
            with self.assertRaises(wind_hlsl.Untranslated, msg=bad):
                self.region(FIXTURE.replace("  return n * inv;", bad))

    def test_normalize_is_refused_by_name(self):
        """The text must write the reference's own x * rsqrt(dot(x, x)) with its guard: HLSL's normalize has neither
        the console's zero product nor the reference's order."""
        with self.assertRaises(wind_hlsl.Untranslated) as e:
            self.region(FIXTURE.replace("  return n * inv;", "  return normalize(n);"))
        self.assertIn("normalize", str(e.exception))
        self.assertNotIn("normalize", wind_hlsl.WATER_INTRINSICS)

    def test_a_missing_function_stops_it(self):
        with self.assertRaises(wind_hlsl.Untranslated):
            wind_hlsl.Region(FIXTURE, wind_hlsl.WATER_PIXEL[0], wind_hlsl.WATER_PIXEL[1], wind_hlsl.WATER_PIXEL[2] + ("WaterFog",),
                             known=wind_hlsl.WATER_INTRINSICS)

    def test_the_intrinsics_of_a_scalar_or_a_vector(self):
        env = wind_hlsl.water_env()
        self.assertEqual(env["sqrt"](9.0), 3.0)
        self.assertEqual(env["sqrt"](Vec(4.0, 9.0)).values, [2.0, 3.0])
        self.assertTrue(math.isnan(env["sqrt"](-1.0)))
        self.assertEqual(env["abs"](-2.0), 2.0)
        self.assertEqual(env["abs"](Vec(-1.0, 2.0, -3.0)).values, [1.0, 2.0, 3.0])
        self.assertEqual(env["rsqrt"](4.0), 0.5)
        self.assertEqual(env["rsqrt"](Vec(4.0, 16.0)).values, [0.5, 0.25])
        self.assertEqual(env["rsqrt"](0.0), math.inf)                 # an IEEE machine: the guard is the text's to write
        self.assertTrue(math.isnan(env["rsqrt"](-1.0)))
        self.assertEqual(env["dot"](Vec(1.0, 2.0, 3.0), Vec(4.0, 5.0, 6.0)), 32.0)
        with self.assertRaises(ValueError):
            env["dot"](Vec(1.0, 2.0), Vec(1.0, 2.0, 3.0))
        self.assertEqual(env["lerp"](2.0, 4.0, 0.25), 2.5)
        self.assertEqual(env["lerp"](Vec(0.0, 2.0), Vec(4.0, 2.0), 0.5).values, [2.0, 2.0])
        self.assertEqual(env["lerp"](Vec(0.0, 2.0), Vec(4.0, 6.0), Vec(0.5, 0.25)).values, [2.0, 3.0])
        self.assertEqual(env["saturate"](NAN), 0.0)                   # Direct3D's saturate of no number
        self.assertEqual(env["saturate"](Vec(-1.0, 0.5, 2.0)).values, [0.0, 0.5, 1.0])
        self.assertEqual(env["log2"](0.0), -math.inf)
        self.assertEqual(env["exp2"](-math.inf), 0.0)
        self.assertTrue(math.isnan(0.0 * env["log2"](0.0)))           # what the power's guard keeps out

    def test_a_conditional_return_and_a_callback(self):
        text = ("// WATER PIXEL TEXT BEGIN\n"
                "float3 Pick(float2 uv, float flip) {\n"
                "  precise float2 at = float2(uv.x, lerp(uv.y, 1.0 - uv.y, flip));\n"
                "  return (flip > 0.5) ? WaterSample13(at).zyx : WaterSample13(at).xyz;\n"
                "}\n"
                "// WATER PIXEL TEXT END\n")
        seen = []

        def sample(uv):
            seen.append(tuple(uv.values))
            return Vec(uv.x, uv.y, 7.0, 1.0)

        with self.assertRaises(wind_hlsl.Untranslated):               # a callback the test did not name is no rule
            wind_hlsl.Region(text, wind_hlsl.WATER_PIXEL[0], wind_hlsl.WATER_PIXEL[1], ("Pick",), known=wind_hlsl.WATER_INTRINSICS)
        region = wind_hlsl.Region(text, wind_hlsl.WATER_PIXEL[0], wind_hlsl.WATER_PIXEL[1], ("Pick",), known=wind_hlsl.WATER_INTRINSICS + ("WaterSample13",))
        pick = region.functions(dict(wind_hlsl.water_env(), WaterSample13=sample))["Pick"]
        self.assertEqual(pick(Vec(0.25, 0.75), 0.0).values, [0.25, 0.75, 7.0])
        self.assertEqual(pick(Vec(0.25, 0.75), 1.0).values, [7.0, 0.25, 0.25])
        self.assertEqual(seen, [(0.25, 0.75), (0.25, 0.25)])          # only the branch taken is evaluated

    def test_the_older_texts_translate_as_before(self):
        """The call check is asked for by a caller (`known`); the wind's and the impostors' texts are read without it."""
        wind_hlsl.translate(HEADER)
        for begin, end, functions in (wind_hlsl.IMPOSTOR_VERTEX, wind_hlsl.IMPOSTOR_PIXEL):
            wind_hlsl.Region(HEADER, begin, end, functions)


@unittest.skipUnless(HAS_TEXT, NO_TEXT)
class TheText(unittest.TestCase):
    """The header's own water text (plan Task 12). The tests here are THE_TEXT_TESTS; Tripwire holds the list."""

    @classmethod
    def setUpClass(cls):
        cls.vertex, cls.pixel = programs(HEADER)

    def test_the_translation_reads_both_texts_whole(self):
        for (begin, end, names) in (wind_hlsl.WATER_VERTEX, wind_hlsl.WATER_PIXEL):
            region = wind_hlsl.Region(HEADER, begin, end, names, known=wind_hlsl.WATER_INTRINSICS + CALLBACKS)
            self.assertEqual(sorted(re.findall(r"^def (\w+)\(", region.source, re.M)), sorted(names))

    def test_the_text_is_the_reference_at_made_inputs(self):
        check_reference(self, self.vertex, self.pixel)

    def test_the_text_is_the_reference_at_the_pins(self):
        check_pins(self, self.vertex, self.pixel)

    def test_an_unread_component_reaches_no_result(self):
        check_unread(self, self.vertex, self.pixel)

    def test_each_wrong_text_fails(self):
        check_mutants(self, HEADER, MUTANTS, 12)

    def test_the_console_s_rule_stands_at_the_three_sites_and_the_power(self):
        check_console(self, self.vertex, self.pixel)

    def test_the_text_writes_the_guards_out(self):
        """Ledger ruling 34 by text: three guarded reciprocal square roots and no bare one, the guarded power and
        no bare one, in the pixel text."""
        text = wind_hlsl.region(HEADER, wind_hlsl.WATER_PIXEL[0], wind_hlsl.WATER_PIXEL[1])
        code = "\n".join(line.split("//")[0] for line in text.split("\n"))
        self.assertEqual(len(re.findall(r"== 0\.0\) \? 0\.0 : rsqrt\(", code)), 3)
        self.assertEqual(code.count("rsqrt("), 3)
        self.assertEqual(len(re.findall(r"> 0\.0\) \? exp2\(", code)), 1)
        self.assertEqual(code.count("exp2("), 1)
        self.assertEqual(code.count("log2("), 1)
        self.assertNotIn("pow(", code)
        self.assertNotIn("normalize(", code)

    def test_a_colour_that_is_no_number_is_discarded(self):
        check_finite(self, self.pixel)
        # the pixel main discards on it and clamps nothing (the draw is blended): held by text, as the main is not translated
        start = HEADER.index("kClayWaterPs")
        main = HEADER[start:HEADER.index(')hlsl"', start)] if ')hlsl"' in HEADER[start:] else HEADER[start:start + 20000]
        self.assertRegex(main, r"if \(WaterFinite\([^;]*\) == 0\.0\) discard;")

    def test_the_two_thresholds_are_the_main_s(self):
        """c141.w decides shaded against colour 0; the state's alpha reference decides drawn against not. Held BY VALUE
        (ledger ruling 43, after the review of Tasks 4 to 15, I4: the test of before asked only that both names occur
        somewhere after the program's name, which a declaration or a comment satisfies): the text's WaterFate at
        every surface line of the pins' alpha section gives the pin's two answers, among them the line that is
        skipped and drawn and the line that is shaded and not drawn; the main takes its two decisions from that
        function's two components and from nothing else; and a text that folds the two thresholds, either way,
        fails at those lines."""
        def answers(pixel):
            out, seen = [], set()
            for item in water_pins.ALPHA:
                (m, c141, reference, kind), (alpha, fate, drawn) = water_pins.alpha_pin(item)
                if kind != 0:
                    continue                                          # (the opaque twin has no native program)
                got = pixel["WaterFate"](alpha, c141[3], reference).values
                out.append((got, [1.0 if fate == 2 else 0.0, float(drawn)]))
                seen.add((fate, drawn))
            return out, seen
        own, seen = answers(self.pixel)
        self.assertGreaterEqual(len(own), 8)
        for got, want in own:
            self.assertEqual(got, want)
        self.assertIn((1, 1), seen)                                   # skipped by c141.w (colour 0) and DRAWN by the state's test
        self.assertIn((2, 0), seen)                                   # shaded by c141.w and NOT drawn
        self.assertIn((0, 0), seen)                                   # killed at an alpha of exactly 1
        for name, old, new, breaks in MUTANTS:
            if breaks != ("fate",):
                continue
            self.assertEqual(HEADER.count(old), 1, name)
            _, pixel = programs(HEADER.replace(old, new))
            self.assertTrue(any(got != want for got, want in answers(pixel)[0]), name)
        self.assertEqual(len([m for m in MUTANTS if m[3] == ("fate",)]), 3)
        # The main: one call of the function with c141.w and the state's reference as its two thresholds; the discard
        # by its second answer, the colour's block by its first; neither threshold is compared anywhere else.
        start = HEADER.index("float4 main(WaterVsOut i) : SV_Target {")
        main = "\n".join(line.split("//")[0] for line in HEADER[start:HEADER.index(')hlsl"', start)].split("\n"))
        self.assertEqual(main.count("  float2 fate = WaterFate(alpha, W_C141.w, w_alpha_ref);\n"), 1)
        self.assertEqual(main.count("  if (fate.y == 0.0) discard;\n"), 1)
        self.assertEqual(main.count("  [branch] if (fate.x != 0.0) {\n"), 1)
        self.assertEqual(main.count("w_alpha_ref"), 1)
        self.assertEqual(main.count("W_C141.w"), 1)
        self.assertEqual(main.count("fate."), 2)
        self.assertEqual(main.count("discard"), 2)                    # the state's test and the no-number rule
        self.assertLess(main.index("if (fate.y == 0.0) discard;"), main.index("[branch] if (fate.x != 0.0) {"))
        self.assertEqual(HEADER.count("w_alpha_ref"), 4)              # the table, the block, the text above the programs, and the main's one use


class Tripwire(unittest.TestCase):
    """What keeps TheText's skip from staying unnoticed (module text). These never skip."""

    def test_a_water_program_without_its_text_fails(self):
        named = [name for name in ("kClayWaterVs", "kClayWaterPs") if name in HEADER]
        if named:
            self.assertTrue(HAS_TEXT, "the header names %s and has no `// WATER VERTEX TEXT BEGIN` / `// WATER PIXEL TEXT BEGIN` regions: "
                                      "tests/test_water_hlsl.py's TheText would skip a text that exists" % ", ".join(named))
            self.assertGreaterEqual(len(MUTANTS), 12, "plan Task 12 fills MUTANTS")
        else:
            self.assertFalse(HAS_TEXT and not MUTANTS, "a water text without its wrong texts (MUTANTS)")

    def test_what_is_skipped_is_known(self):
        names = tuple(sorted(name for name in dir(TheText) if name.startswith("test_")))
        self.assertEqual(names, THE_TEXT_TESTS)
        self.assertEqual(len(THE_TEXT_TESTS), 9)
        self.assertIn("plan Task 12", NO_TEXT)
        self.assertIn("RUN", NO_TEXT)
        skip = getattr(TheText, "__unittest_skip__", False)
        self.assertEqual(bool(skip), not HAS_TEXT)                    # skipped exactly while the header has no text
        if skip:
            self.assertEqual(TheText.__unittest_skip_why__, NO_TEXT)


class ReflectionBlurText(unittest.TestCase):
    def evaluate(self, text, samples, weight):
        functions = wind_hlsl.Region(text, "// WATER BLUR TEXT BEGIN", "// WATER BLUR TEXT END",
                                    ("WaterBlurFinite", "WaterBlurSum"), known=("abs",)).functions(wind_hlsl.water_env())
        result = functions["WaterBlurSum"](*(Vec(*v) for v in samples), weight)
        return [result.x, result.y, result.z, result.w]

    def test_reference_and_three_mutants(self):
        samples = [[0.03 * k, 0.1 + 0.02 * k, 0.9 - 0.04 * k] for k in range(7)]
        samples[3][1] = math.nan
        offsets = [(k, 0) for k in range(7)]
        expected = lf.water_blur7(lambda u, v: samples[int(u)], 0, 0, offsets, 1.0 / 7)
        actual = self.evaluate(HEADER, samples, 1.0 / 7)
        for a, b in zip(actual, expected): self.assertAlmostEqual(a, b, places=12)
        mutants = (("sum = sum + WaterBlurFinite(c);", "sum = sum + WaterBlurFinite(b);"),
                   ("return float4(sum * weight, 0.0);", "return float4(sum * (weight * 0.9), 0.0);"),
                   ("sum = sum + WaterBlurFinite(g);", "sum = sum + WaterBlurFinite(g) * 2.0;"))
        for before, after in mutants:
            self.assertIn(before, HEADER)
            other = self.evaluate(HEADER.replace(before, after), samples, 1.0 / 7)
            self.assertGreater(max(abs(a-b) for a,b in zip(other, expected)), 1e-4)

    def test_coordinate_and_own_tap_order(self):
        start = HEADER.index('inline constexpr const char* kClayWaterBlurPs')
        text = HEADER[start:HEADER.index(')hlsl";', start)]
        self.assertIn('floor(pos.xy) * b_inv_size', text)
        taps = re.findall(r'water_sharp.SampleLevel\(s_linear_clamp, uv \+ b_offset\[(\d)\].xy, 0\).rgb', text)
        self.assertEqual(taps, ['0','1','2','3','4','5','6'])
        self.assertIn('b_weight', text)


if __name__ == "__main__":
    unittest.main()
