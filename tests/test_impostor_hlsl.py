"""The impostor step's HLSL text is held to the Python reference by value (distant trees, plan Task 10).

The pins hold the C++ mirrors to the reference (tests/native/test_clay_logic.cpp, cases 820 to 829); nothing but the
game runs the text that is compiled: the functions of shaders::kClayImpostorVs and kClayImpostorPs in
src/native/fable2_native_shaders.h. Sub-project 11's review found that pins on a mirror hold nothing of the HLSL
(seven wrong texts passed every test), so here, as tests/test_wind_hlsl.py does for the wind, the two texts are
translated line by line to Python (tools/xdk_sigmatch/wind_hlsl.py) and evaluated in double against
light_formulas: impostor_position, impostor_fog, impostor_distance, impostor_pixel, impostor_out,
impostor_alpha_test and covered_samples; at the inputs of every pin line and at made inputs in which every
register component differs and every component NO formula reads is no number (a text that read one would give no
number). The wrong texts listed below must each fail the comparison.

The vertex text's guard (ledger ruling 27) is held here too: a quad of no size, and a quad whose two weights are
no numbers, has the refused position at all four corners (a point beyond the far plane: no pixel, and never a
position that is no number), and the test of it reads nothing of the corner.

Not reached from here: the compiler, `precise` and the GPU's arithmetic (single precision; its log and exp), the
vertex main (the load of the quad's word, the corner table) and the pixel main (the sample, the discard, the
clamp of what is written): tests/native/test_native_shaders.cpp, case 54, holds those on the compiled programs."""
import math
import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import gen_impostor_table as gen  # noqa: E402
import impostor_pins  # noqa: E402
import light_formulas as lf  # noqa: E402
import wind_hlsl  # noqa: E402

REG = lf.IMPOSTOR_SHADER["reg"]
PS_REG = lf.IMPOSTOR_SHADER["ps_reg"]
TOLERANCE = 1e-9        # of max(1, |value|): double against double, the same order of operations
PIN_BOUND = 1e-4        # the pins' own bound (criterion 1; ledger ruling 26): the double text against the single-precision pins
MADE = 200              # made inputs a text
MUTANT_FLOOR = 1e-4     # a changed text must leave the reference by at least this (of max(1, |value|)) somewhere
REFUSED = (0.0, 0.0, 2.0, 1.0)
NAN = math.nan

# (name, the text as committed, the changed text, the comparisons it must break): each site exactly once in the header.
MUTANTS = (
    ("the raise reads c8.x", "precise float lift = S * I_C8.z;", "precise float lift = S * I_C8.x;", ("position",)),
    ("the place is not raised", "precise float raised = word.z + lift;", "precise float raised = word.z;", ("place",)),
    ("c116 for c115 in the first row's sum", "precise float xy115 = (X * I_C115.x + Y * I_C115.y) + 0.0;",
     "precise float xy115 = (X * I_C116.x + Y * I_C116.y) + 0.0;", ("position",)),
    ("the lying axis's first two components exchanged", "precise float fore_x = S * I_C46.x + X;", "precise float fore_x = S * I_C46.y + X;", ("position",)),
    ("the lying axis without its third component", "precise float fore_z = (S * I_C46.z + lift) + Z;", "precise float fore_z = (0.0 + lift) + Z;", ("position",)),
    ("the upright extent without its scale", "exp2(I_C114.y * log2(abs(I_C114.x * upright)))", "exp2(I_C114.y * log2(abs(upright)))", ("position",)),
    ("the lying extent's exponent reads c114.x", "precise float eh = exp2(I_C114.y * log2(lying));", "precise float eh = exp2(I_C114.x * log2(lying));", ("position",)),
    ("the height comparison turned", "(raised > I_C47.z) ? 1.0 : 0.0;", "(raised < I_C47.z) ? 1.0 : 0.0;", ("position",)),
    ("the lying axis's ends exchanged", "precise float3 side = (upper != above) ? fore : back;", "precise float3 side = (upper == above) ? fore : back;", ("position",)),
    ("the picture's edges exchanged", "precise float3 end = (upper != 0.0) ? top : bottom;", "precise float3 end = (upper == 0.0) ? top : bottom;", ("position",)),
    ("the sideways term's sign", "precise float moved = ((u * 2.0 + -1.0) * S) + q_x;", "precise float moved = ((u * 2.0 + -1.0) * -S) + q_x;", ("position",)),
    ("the sideways term on the second component", "precise float q_y = wh * side.y + wv * end.y;", "precise float q_y = (wh * side.y + wv * end.y) + S;", ("position",)),
    ("two rows of the last matrix exchanged", "+ I_C72.x * moved) + I_C72.w * w, (((0.0 + I_C73.z * q_z)", "+ I_C72.x * moved) + I_C72.w * w, (((0.0 + I_C74.z * q_z)", ("position",)),
    ("two corners exchanged", "precise float u = (k >= 2.0) ? 1.0 : 0.0;\n  precise float upper = (k == 1.0 || k == 2.0) ? 1.0 : 0.0;\n  precise float xy115",
     "precise float u = (k >= 2.0) ? 1.0 : 0.0;\n  precise float upper = (k == 0.0 || k == 2.0) ? 1.0 : 0.0;\n  precise float xy115", ("position",)),
    ("the picture coordinate's scale and offset exchanged", "v * I_C113.w + I_C113.z", "v * I_C113.z + I_C113.w", ("coord",)),
    ("the fog's angle turned", "dot(V, I_C110.xyz) * -inv", "dot(V, I_C110.xyz) * inv", ("fog",)),
    ("the fog's start reads c111.y", "float dD = q * inv - I_C111.x;", "float dD = q * inv - I_C111.y;", ("fog",)),
    ("the transmittance's scale reads c111.z", "float s = I_C111.w * dD;", "float s = I_C111.z * dD;", ("fog",)),
    ("the fog's (1 - o2) left out", "add = (1.0 - T) * fog_scatter(at.x);", "add = fog_scatter(at.x);", ("fog",)),
    ("the fog's eye reads the place alone", "float3 V = I_C47.xyz - place;", "float3 V = I_C47.xyz - place * 0.5;", ("fog",)),
    ("c12.y for c29.x in the dissolve", "saturate((d - I_P29.x) * I_P29.y)", "saturate((d - I_P12.y) * I_P29.y)", ("pixel",)),
    ("the light's tenth changed by 1%", "saturate(0.1 * (d + -I_P12.y))", "saturate(0.101 * (d + -I_P12.y))", ("pixel",)),
    ("c31.x left out", "precise float3 colour = (texel.xyz * I_P31.x) * light;", "precise float3 colour = texel.xyz * light;", ("pixel",)),
    ("the light factor's sign", "precise float span = I_P12.w + -1.0;", "precise float span = -I_P12.w + 1.0;", ("pixel",)),
    ("the alpha without the picture's", "precise float alpha = texel.w * dissolve;", "precise float alpha = dissolve;", ("pixel",)),
    ("the distance from two components", "((0.0 + v.z * v.z) + v.x * v.x) + v.y * v.y", "(0.0 + v.z * v.z) + v.x * v.x", ("pixel",)),
    ("the first sample's threshold", "float first = (alpha >= 0.25) ? 1.0 : 0.0;", "float first = (alpha >= 0.5) ? 1.0 : 0.0;", ("keep",)),
    ("the alpha test left out", "keep = (alpha > i_alpha_ref) ? keep : 0.0;", "keep = (alpha > i_alpha_ref) ? keep : keep;", ("keep",)),
    ("the half set for the first", "float keep = (i_kept == 1) ? first : 0.0;", "float keep = (i_kept == 1) ? first - second : 0.0;", ("keep",)),
    ("the fog pair's order", "float3 v = (colour * o2 + o3) * i_out_scale;", "float3 v = (colour * o3 + o2) * i_out_scale;", ("out",)),
    ("the output scale left out", "float3 v = (colour * o2 + o3) * i_out_scale;", "float3 v = colour * o2 + o3;", ("out",)),
)


def _header():
    return (ROOT / "src" / "native" / "fable2_native_shaders.h").read_bytes().decode("utf-8").replace("\r\n", "\n")


def _dot(a, b):
    return sum(x * y for x, y in zip(a.values, b.values))


def _rsqrt(x):
    return 1.0 / math.sqrt(x)


class Texts:
    """The two texts of a header, translated."""

    def __init__(self, header):
        self.vertex = wind_hlsl.Region(header, *wind_hlsl.IMPOSTOR_VERTEX)
        self.pixel = wind_hlsl.Region(header, *wind_hlsl.IMPOSTOR_PIXEL)

    def vertex_functions(self, regs, fog_on=1.0, frame_fog=1, transmit=None, scatter=None):
        """`regs`: {register: four numbers} for the fourteen; `transmit`, `scatter`: u -> three numbers (the
        frame's two tables as fog_transmit and fog_scatter read them)."""
        env = {"i_vs": [wind_hlsl.Vec(*regs[r]) for r in REG], "i_fog_on": fog_on, "f_fog_on": frame_fog, "abs": abs,
               "exp2": lf._imp_exp2, "log2": lf._imp_log2, "rsqrt": _rsqrt, "dot": _dot,
               "fog_transmit": lambda u: wind_hlsl.Vec(*transmit(u)[:3]), "fog_scatter": lambda u: wind_hlsl.Vec(*scatter(u)[:3])}
        return self.vertex.functions(env)

    def pixel_functions(self, ps, kept=1, alpha_ref=128.0 / 255.0, out_scale=1.0):
        """`ps`: {register: four numbers} for c12, c19, c20, c29, c31."""
        env = {"i_ps": [wind_hlsl.Vec(*ps[r]) for r in PS_REG], "i_kept": kept, "i_alpha_ref": alpha_ref, "i_out_scale": out_scale,
               "abs": abs, "sqrt": math.sqrt}
        return self.pixel.functions(env)


def _unread(kind, reg, values, rng):
    """`values` with every component the formulas do not read made no number (or, one time in four, a large
    number): ledger ruling 34 carries such components as they are, and the text must not read them."""
    read = gen.READ[(kind, reg)]
    return [v if i in read else (NAN if rng.random() < 0.75 else 3.0e37) for i, v in enumerate(values)]


def _made_registers(rng):
    """Fourteen vertex registers of a general view: every read component its own number."""
    def row(scale, shift):
        return [rng.uniform(-scale, scale) for _ in range(3)] + [rng.uniform(-shift, shift)]
    axis = lf.normalize([rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 0.6)])
    fog_axis = lf.normalize([rng.uniform(-1, 1) for _ in range(3)])
    regs = {
        8: [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.0, 1.0), rng.uniform(-1, 1)],
        46: list(axis) + [rng.uniform(-1, 1)],
        47: [rng.uniform(-300, 300), rng.uniform(-300, 300), rng.uniform(20, 80), rng.uniform(-1, 1)],
        72: row(2.0, 200.0), 73: row(2.0, 200.0), 74: row(2.0, 200.0), 75: row(2.0, 200.0),
        110: list(fog_axis) + [rng.uniform(-1, 1)],
        111: [rng.uniform(5.0, 60.0), rng.uniform(100, 2000), rng.uniform(0.0, 1.0), rng.uniform(0.0005, 0.01)],
        113: [rng.uniform(0.5, 1.0), rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1), rng.uniform(0.5, 1.0)],
        114: [rng.uniform(0.5, 2.0), rng.uniform(0.5, 3.0), rng.uniform(-1, 1), rng.uniform(-1, 1)],
        115: row(1.0, 300.0), 116: row(1.0, 300.0), 117: row(1.0, 300.0),
    }
    return {reg: _unread("vs", reg, values, rng) for reg, values in regs.items()}


def _made_vertex(rng):
    """[(corner, word, registers, the two tables)]: MADE quads; the raised place on both sides of the eye's height."""
    out = []
    for n in range(MADE):
        regs = _made_registers(rng)
        size = rng.uniform(0.5, 60.0)
        # (the raised place within a few units of the eye's height, either side)
        z = regs[47][2] - size * regs[8][2] + rng.uniform(-6.0, 6.0)
        word = (rng.uniform(-500, 500), rng.uniform(-500, 500), z, size)
        transmit = lf.table_1d([[rng.uniform(0.0, 1.0) for _ in range(3)] for _ in range(256)])
        scatter = lf.table_1d([[rng.uniform(0.0, 2.0) for _ in range(3)] for _ in range(64)])
        out.append((n % 4, word, regs, transmit, scatter))
    return out


def _made_pixel(rng):
    """[(texel, place, registers, o2, o3, the alpha test's reference)]: MADE pixels, the distances across the
    dissolve (48 to 56 units with the captured c29) and the light's fall (35 to 45)."""
    out = []
    for n in range(MADE):
        ps = {12: [rng.uniform(0, 30), rng.uniform(30.0, 40.0), rng.uniform(0, 4), rng.uniform(0.3, 0.7)],
              19: [rng.uniform(-300, 300), rng.uniform(-300, 300), rng.uniform(20, 80), rng.uniform(-1, 1)],
              20: [rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(1.0, 12.0)],
              29: [rng.uniform(44.0, 52.0), rng.uniform(0.08, 0.2), rng.uniform(0, 1000), rng.uniform(0, 30)],
              31: [rng.uniform(0.2, 0.6), rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)]}
        ps = {reg: _unread("ps", reg, values, rng) for reg, values in ps.items()}
        distance = (rng.uniform(25.0, 47.0), rng.uniform(47.0, 62.0), rng.uniform(47.0, 62.0), rng.uniform(62.0, 600.0))[n % 4]
        ray = lf.normalize([rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.3)])
        place = tuple(ps[19][i] + distance * ray[i] for i in range(3))
        texel = (rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0, 1), (1.0, 1.0, rng.uniform(0.0, 1.0))[n % 3])
        o2, o3 = [rng.uniform(0.2, 1.0) for _ in range(3)], [rng.uniform(0.0, 0.5) for _ in range(3)]
        out.append((texel, place, ps, o2, o3, (128.0 / 255.0, rng.uniform(0.0, 0.9))[n % 2]))
    return out


def _gap(got, want):
    """The largest |got - want| / max(1, |want|) over two sequences; a value that is no number is infinitely far."""
    worst = 0.0
    for a, b in zip(got, want):
        worst = max(worst, abs(a - b) / max(1.0, abs(b)) if math.isfinite(a) and math.isfinite(b) else math.inf)
    return worst


def _worst(texts, vertex, pixel):
    """{comparison: the largest gap of the texts from the reference} over vertex cases (corner, word, registers,
    transmit, scatter) and pixel cases (texel, place, registers, o2, o3, reference)."""
    worst = {"position": 0.0, "place": 0.0, "coord": 0.0, "fog": 0.0, "pixel": 0.0, "keep": 0.0, "out": 0.0}
    V = wind_hlsl.Vec
    for corner, word, regs, transmit, scatter in vertex:
        f = texts.vertex_functions(regs, transmit=transmit, scatter=scatter)
        want = lf.impostor_position(corner, word, regs)
        worst["position"] = max(worst["position"], _gap(f["ImpostorPosition"](float(corner), V(*word)).values, want["pos"]))
        place = f["ImpostorPlace"](V(*word)).values
        worst["place"] = max(worst["place"], _gap(place, want["place"]))
        worst["coord"] = max(worst["coord"], _gap(f["ImpostorCoord"](float(corner)).values, want["uv"]))
        # (the fog at the reference's place, so that a wrong place is the place's failure alone)
        o2, o3 = lf.impostor_fog(want["place"], regs[47][:3], regs[110], regs[111], transmit, lambda a, t: scatter(a))
        at = f["ImpostorFogCoords"](V(*want["place"]))
        factor = f["ImpostorFogFactor"](at)
        worst["fog"] = max(worst["fog"], _gap(factor.values, o2), _gap(f["ImpostorFogAdd"](at, factor).values, o3))
    for texel, place, ps, o2, o3, ref in pixel:
        f = texts.pixel_functions(ps, kept=1, alpha_ref=ref, out_scale=ps[20][3])
        d = lf.impostor_distance(place, ps[19][:3])
        colour, alpha = lf.impostor_pixel(texel, d, ps[12], ps[29], ps[31][0])
        got = f["ImpostorPixel"](V(*texel), f["ImpostorDistance"](V(*place))).values
        worst["pixel"] = max(worst["pixel"], _gap(got, list(colour) + [alpha]))
        worst["out"] = max(worst["out"], _gap(f["ImpostorOut"](V(*colour), V(*o2), V(*o3)).values, lf.impostor_out(colour, o2, o3, ps[20][3])))
        passed, covered = lf.impostor_alpha_test(alpha, ref), lf.covered_samples(0xAA, 0, 0, alpha)
        for kept, want_keep in ((1, covered >= 1), (2, covered == 2), (3, covered == 1)):
            keep = texts.pixel_functions(ps, kept=kept, alpha_ref=ref)["ImpostorKeep"](alpha)
            worst["keep"] = max(worst["keep"], abs(keep - float(passed and want_keep)))
    return worst


class TheText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = _header()
        cls.texts = Texts(cls.header)
        rng = random.Random(12)
        cls.vertex = _made_vertex(rng)
        cls.pixel = _made_pixel(rng)

    def test_the_translation_reads_both_texts_whole(self):
        for region, (_, _, names) in ((self.texts.vertex, wind_hlsl.IMPOSTOR_VERTEX), (self.texts.pixel, wind_hlsl.IMPOSTOR_PIXEL)):
            for name in names:
                self.assertIn("def %s(" % name, region.source)
        # the register names are the table's order (the constant block holds the rows in it)
        self.assertEqual(self.texts.vertex.defines, {"I_C%d" % reg: "i_vs[%d]" % i for i, reg in enumerate(REG)})
        self.assertEqual(self.texts.pixel.defines, {"I_P%d" % reg: "i_ps[%d]" % PS_REG.index(reg) for reg in (12, 19, 29, 31)})

    def test_a_line_no_rule_knows_stops_the_translation(self):
        site = "float3 ImpostorPlace(float4 word) {"
        self.assertEqual(self.header.count(site), 1)
        for text in (site + "\n  for (int i = 0; i < 2; ++i) { word.z += 1.0; }", site + "\n#if 1", site + "\n  word.z = word.z & 1;"):
            with self.assertRaises(wind_hlsl.Untranslated):
                wind_hlsl.translate(self.header.replace(site, text), *wind_hlsl.IMPOSTOR_VERTEX)

    def test_the_made_inputs_reach_every_branch(self):
        # a comparison on inputs that take one side alone holds nothing of the other
        self.assertEqual([sum(1 for c in self.vertex if c[0] == k) for k in range(4)], [MADE // 4] * 4)
        above = sum(1 for _, word, regs, _, _ in self.vertex if lf.impostor_position(0, word, regs)["place"][2] > regs[47][2])
        self.assertGreater(above, MADE // 4)
        self.assertGreater(MADE - above, MADE // 4)
        fogged = sum(1 for _, word, regs, _, _ in self.vertex
                     if lf.fog_coords(lf.impostor_position(0, word, regs)["place"], regs[47][:3], regs[110], regs[111]) is not None)
        self.assertGreater(fogged, MADE // 2)
        self.assertGreater(MADE - fogged, 0)
        absent = dissolving = whole = falling = one_sample = below_ref = 0
        for texel, place, ps, _, _, ref in self.pixel:
            d = lf.impostor_distance(place, ps[19][:3])
            _, alpha = lf.impostor_pixel((1.0, 1.0, 1.0, 1.0), d, ps[12], ps[29], ps[31][0])
            absent += alpha == 0.0
            whole += alpha == 1.0
            dissolving += 0.0 < alpha < 1.0
            falling += ps[12][1] < d < ps[12][1] + 10.0
            _, alpha = lf.impostor_pixel(texel, d, ps[12], ps[29], ps[31][0])
            one_sample += lf.covered_samples(0xAA, 0, 0, alpha) == 1
            below_ref += alpha >= 0.25 and not lf.impostor_alpha_test(alpha, ref)
        for count in (absent, dissolving, whole, falling, one_sample, below_ref):
            self.assertGreater(count, MADE // 20, (absent, dissolving, whole, falling, one_sample, below_ref))
        # and the unread components are no numbers in most made registers
        self.assertGreater(sum(1 for _, _, regs, _, _ in self.vertex if regs[8][0] != regs[8][0]), MADE // 2)
        self.assertGreater(sum(1 for _, _, ps, _, _, _ in self.pixel if ps[31][1] != ps[31][1]), MADE // 2)

    def test_the_text_is_the_reference_at_made_inputs(self):
        worst = _worst(self.texts, self.vertex, self.pixel)
        for name, value in worst.items():
            self.assertLessEqual(value, TOLERANCE, "%s at made inputs: %r" % (name, worst))

    def test_the_text_is_the_reference_at_the_position_pins(self):
        V = wind_hlsl.Vec
        rows = impostor_pins.CAPTURED + impostor_pins.MADE
        self.assertEqual(len(rows), 16)
        for row in rows:
            (corner, word, regs), (pos, place, uv) = impostor_pins.position_pin(row)
            c = {reg: list(regs[i]) for i, reg in enumerate(REG)}
            f = self.texts.vertex_functions(c, transmit=lambda u: (1.0, 1.0, 1.0), scatter=lambda u: (0.0, 0.0, 0.0))
            got = f["ImpostorPosition"](float(corner), V(*word)).values
            double = lf.impostor_position(corner, word, c)
            self.assertLessEqual(_gap(got, double["pos"]), TOLERANCE)
            # (and within the pins' bound of the single-precision numbers the mirror is pinned to)
            self.assertLessEqual(_gap(got, pos), PIN_BOUND)
            self.assertLessEqual(_gap(f["ImpostorPlace"](V(*word)).values, place), PIN_BOUND)
            self.assertLessEqual(_gap(f["ImpostorCoord"](float(corner)).values, uv), PIN_BOUND)

    def test_the_text_is_the_reference_at_the_pixel_pins(self):
        V = wind_hlsl.Vec
        self.assertEqual(len(impostor_pins.PIXEL), 9)
        kept = 0
        for row in impostor_pins.PIXEL:
            (texel, place, eye, c12, c29, c31x, o2, o3, c20w), (d, rgb, alpha) = impostor_pins.pixel_pin(row)
            ps = {12: c12, 19: list(eye) + [NAN], 20: [NAN, NAN, NAN, c20w], 29: c29, 31: [c31x, NAN, NAN, NAN]}
            f = self.texts.pixel_functions(ps, kept=1, alpha_ref=128.0 / 255.0, out_scale=c20w)
            got_d = f["ImpostorDistance"](V(*place))
            self.assertLessEqual(_gap([got_d], [d]), TOLERANCE)
            got = f["ImpostorPixel"](V(*texel), got_d)
            self.assertLessEqual(_gap([got.w], [alpha]), TOLERANCE)
            self.assertLessEqual(_gap(f["ImpostorOut"](got.xyz, V(*o2), V(*o3)).values, rgb), TOLERANCE)
            keep = f["ImpostorKeep"](got.w)
            self.assertEqual(keep, float(alpha > 128.0 / 255.0 and alpha >= 0.25))
            kept += keep
        self.assertGreaterEqual(kept, 3)

    def test_the_text_is_the_reference_at_the_fog_pins(self):
        V = wind_hlsl.Vec
        self.assertEqual(len(impostor_pins.FOG), 6)
        ran = 0
        for row in impostor_pins.FOG:
            (place, eye, c110, c111, transmit, scatter), (runs, coords, o2, o3) = impostor_pins.fog_pin(row)
            regs = {reg: [NAN] * 4 for reg in REG}
            regs[47], regs[110], regs[111] = list(eye) + [NAN], list(c110[:3]) + [NAN], [c111[0], NAN, NAN, c111[3]]
            f = self.texts.vertex_functions(regs, transmit=lambda u: transmit, scatter=lambda u: scatter)
            at = f["ImpostorFogCoords"](V(*place))
            self.assertEqual(at.z, 1.0 if runs else 0.0)
            if runs:
                self.assertLessEqual(_gap([at.x, at.y], coords[:2]), TOLERANCE)
            factor = f["ImpostorFogFactor"](at)
            self.assertLessEqual(_gap(factor.values, o2), TOLERANCE)
            self.assertLessEqual(_gap(f["ImpostorFogAdd"](at, factor).values, o3), TOLERANCE)
            ran += runs
            # with the fog off in the record's constants, or in the frame: (1, 0), and no table read
            for fog_on, frame_fog in ((0.0, 1), (1.0, 0)):
                def never(u):
                    raise AssertionError("a table was read with the fog off")
                g = self.texts.vertex_functions(regs, fog_on=fog_on, frame_fog=frame_fog, transmit=never, scatter=never)
                off = g["ImpostorFogCoords"](V(*place))
                self.assertEqual(off.z, 0.0)
                self.assertEqual(g["ImpostorFogFactor"](off).values, [1.0, 1.0, 1.0])
                self.assertEqual(g["ImpostorFogAdd"](off, V(1.0, 1.0, 1.0)).values, [0.0, 0.0, 0.0])
        self.assertGreaterEqual(ran, 3)
        self.assertGreaterEqual(len(impostor_pins.FOG) - ran, 2)

    def test_a_place_at_the_eye_or_no_number_is_not_fogged(self):
        V = wind_hlsl.Vec
        (place, eye, c110, c111, transmit, scatter), _ = impostor_pins.fog_pin(impostor_pins.FOG[3])
        regs = {reg: [NAN] * 4 for reg in REG}
        regs[47], regs[110], regs[111] = list(eye) + [NAN], list(c110[:3]) + [NAN], [c111[0], NAN, NAN, c111[3]]
        f = self.texts.vertex_functions(regs, transmit=lambda u: transmit, scatter=lambda u: scatter)
        for at in (eye, (NAN, 0.0, 0.0), (math.inf, 0.0, 0.0), (1.0e20, 0.0, 0.0)):
            self.assertEqual(f["ImpostorFogCoords"](V(*at)).z, 0.0, at)

    def test_a_quad_that_cannot_be_placed_is_refused_whole(self):
        """Ledger ruling 27: no size, a size that is no number, weights that are no numbers: the refused position
        (beyond the far plane, a number in every component) at all four corners, never a position that is no number."""
        V = wind_hlsl.Vec
        self.assertGreater(REFUSED[2], REFUSED[3])      # z / w = 2: outside the depth range 0 to 1, clipped
        corner, word, regs, transmit, scatter = self.vertex[0]
        flat = dict(regs)
        flat[116] = [0.0, 0.0, 0.0, regs[116][3]]       # a view whose second row has no part of either axis: both extents 0
        steep = dict(regs)
        steep[114] = [regs[114][0], 4000.0, NAN, NAN]   # a power beyond single precision's range would be an infinite weight
        level = dict(regs)
        level[114] = [regs[114][0], 0.0, NAN, NAN]      # (drawn: both powers 1; here as the control)
        huge = dict(regs)
        huge[72] = [3.0e37, 3.0e37, 3.0e37, 3.0e37]     # (ruling 37) a corner beyond single precision's range: the last test
        lost = dict(regs)
        lost[75] = [NAN, 0.0, 0.0, 1.0]                 # a component that is no number, in the last row alone
        cases = (("a row beyond the range", word, huge, True), ("a row that is no number", word, lost, True),
                 ("no size", (word[0], word[1], word[2], 0.0), regs, True), ("a negative size", (word[0], word[1], word[2], -3.0), regs, True),
                 ("a size that is no number", (word[0], word[1], word[2], NAN), regs, True), ("no extent", word, flat, True),
                 ("an infinite weight", (word[0], word[1], word[2], 3.0e30), steep, True), ("the control", word, level, False),
                 ("the made quad", word, regs, False))
        for name, w, c, refused in cases:
            f = self.texts.vertex_functions(c, transmit=transmit, scatter=scatter)
            got = [tuple(f["ImpostorPosition"](float(k), V(*w)).values) for k in range(4)]
            for k in range(4):
                self.assertTrue(all(math.isfinite(v) for v in got[k]), (name, k, got[k]))
                self.assertEqual(got[k] == REFUSED, refused, (name, k, got[k]))

    def test_an_unread_component_reaches_no_result(self):
        """Ledger ruling 38 (review of Tasks 8 and 9, I2): every made input and every pinned line twice, with numbers
        and with no number in ALL the components no formula reads (9 of the vertex registers, 11 of the pixel's): the
        results are the same numbers, bit for bit. IEEE arithmetic has no zero-product rule: a text that touched such
        a component, even times zero, would give no number."""
        V = wind_hlsl.Vec
        unread_vs = [(reg, i) for reg in REG for i in range(4) if i not in gen.READ[("vs", reg)]]
        unread_ps = [(reg, i) for reg in PS_REG for i in range(4) if i not in gen.READ[("ps", reg)]]
        self.assertEqual((len(unread_vs), len(unread_ps)), (9, 11))

        def both(regs, unread):
            a, b = {r: list(v) for r, v in regs.items()}, {r: list(v) for r, v in regs.items()}
            for n, (reg, i) in enumerate(unread):
                a[reg][i], b[reg][i] = 0.25 + n, NAN
            return a, b

        vertex = [(c, w, r, t, sc) for c, w, r, t, sc in self.vertex]
        for row in impostor_pins.CAPTURED + impostor_pins.MADE:
            (corner, word, regs), _ = impostor_pins.position_pin(row)
            vertex.append((corner, word, {reg: list(regs[i]) for i, reg in enumerate(REG)}, lambda u: (0.5, 0.6, 0.7), lambda u: (0.1, 0.2, 0.3)))
        for corner, word, regs, transmit, scatter in vertex:
            got = []
            for c in both(regs, unread_vs):
                f = self.texts.vertex_functions(c, transmit=transmit, scatter=scatter)
                place = f["ImpostorPlace"](V(*word))
                at = f["ImpostorFogCoords"](place)
                factor = f["ImpostorFogFactor"](at)
                got.append(f["ImpostorPosition"](float(corner), V(*word)).values + place.values + f["ImpostorCoord"](float(corner)).values +
                           at.values + factor.values + f["ImpostorFogAdd"](at, factor).values)
            self.assertTrue(all(math.isfinite(v) for v in got[0]), got[0])
            self.assertEqual(got[0], got[1])
        pixel = list(self.pixel)
        for row in impostor_pins.PIXEL:
            (texel, place, eye, c12, c29, c31x, o2, o3, c20w), _ = impostor_pins.pixel_pin(row)
            pixel.append((texel, place, {12: list(c12), 19: list(eye) + [0.0], 20: [0.0, 0.0, 0.0, c20w], 29: list(c29), 31: [c31x, 0.0, 0.0, 0.0]},
                          o2, o3, 128.0 / 255.0))
        for texel, place, ps, o2, o3, ref in pixel:
            got = []
            for c in both(ps, unread_ps):
                f = self.texts.pixel_functions(c, kept=1, alpha_ref=ref, out_scale=ps[20][3])
                c4 = f["ImpostorPixel"](V(*texel), f["ImpostorDistance"](V(*place)))
                got.append(c4.values + f["ImpostorOut"](c4.xyz, V(*o2), V(*o3)).values + [f["ImpostorKeep"](c4.w)])
            self.assertTrue(all(math.isfinite(v) for v in got[0]), got[0])
            self.assertEqual(got[0], got[1])

    def test_the_refusal_reads_nothing_of_the_corner(self):
        # every line of ImpostorPosition up to the test names the quad's word and the registers alone: the corner's
        # two selectors (u, upper) are read only inside the block, so a quad is drawn whole or refused whole
        body = self.header[self.header.index("float4 ImpostorPosition(float k, float4 word) {"):]
        test = "  [branch] if (S > 0.0 && total > 0.0 && total < 3.0e38) {\n"
        self.assertEqual(body.count(test), 1)
        before = [line for line in body[:body.index(test)].split("\n")[1:] if "float u = " not in line and "float upper = " not in line]
        self.assertEqual(len(before) + 2, len(body[:body.index(test)].split("\n")[1:]))
        import re
        for line in before:
            self.assertIsNone(re.search(r"\b(k|u|upper)\b", line.split("//")[0]), line)

    def test_each_wrong_text_fails(self):
        for name, old, new, broken in MUTANTS:
            with self.subTest(mutant=name):
                self.assertEqual(self.header.count(old), 1, "the site of the mutant is not in the header exactly once")
                worst = _worst(Texts(self.header.replace(old, new)), self.vertex, self.pixel)
                for comparison in broken:
                    self.assertGreater(worst[comparison], MUTANT_FLOOR, "%s passes with %s: %r" % (comparison, name, worst))
                for comparison in set(worst) - set(broken):
                    self.assertLessEqual(worst[comparison], TOLERANCE, "%s is touched by %s: %r" % (comparison, name, worst))


if __name__ == "__main__":
    unittest.main()
