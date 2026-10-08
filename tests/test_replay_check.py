import contextlib
import io
import json
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import replay_check as rc  # noqa: E402

# Two small vertex shaders in the dump's syntax with the probe arithmetic of the game's and nothing else.
# PLAIN has the form of 79EA... (weights fetched xyzw, `dp4 r.yxzw, c.yxzw`; the set index from r2.x of a
# zxwy fetch); SKINNED the form of D4D5... / A1F7... (weights fetched wzxy, `dp4 r.wzyx, c.yxzw`; the set
# index from r7.w of a yxwz fetch).
PLAIN = """/*    0.0 */       exec
/*    1   */          vfetch_full r2.zxwy, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=2, Signed=true, NumFormat=integer
/*    2   */          vfetch_full r3, r0.x, vf1, DataFormat=FMT_32_32_32_32_FLOAT, Stride=4, Signed=true, NumFormat=integer
/*    0.1 */       alloc interpolators
/*    1.0 */       exece
/*    3   */          truncs r2.x___, r2.x
/*    4   */          maxas r0._, r2.xx
/*    5   */          dp4 r2.x___, r3.yxzw, c[30+a0].yxzw
/*    6   */          dp4 r2._y__, r3.yxzw, c[31+a0].yxzw
/*    7   */          dp4 r2.__z_, r3.yxzw, c[29+a0].yxzw
/*    8   */          add o6.xyz1, c[28+a0].xyzz, r2.zxyy
"""
SKINNED = """/*    0.0 */       exec
/*    1   */          vfetch_full r7.yxwz, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=2, Signed=true, NumFormat=integer
/*    2   */          vfetch_full r3.wzxy, r0.x, vf1, DataFormat=FMT_32_32_32_32_FLOAT, Stride=4, Signed=true, NumFormat=integer
/*    0.1 */       alloc interpolators
/*    1.0 */       exece
/*    3   */          truncs r7.___w, r7.w
/*    4   */          maxas r0._, r7.ww
/*    5   */          dp4 r4.x___, r3.wzyx, c[30+a0].yxzw
/*    6   */          dp4 r4._y__, r3.wzyx, c[31+a0].yxzw
/*    7   */          dp4 r4.__z_, r3.wzyx, c[29+a0].yxzw
/*    8   */          add o6.xyz1, c[28+a0].xyzz, r4.zxyy
"""
# A game that dots the weights in another order (register xyzw against constant yxzw): what the check
# must report when the renderer's order is not the game's.
OTHER_ORDER = PLAIN.replace("r3.yxzw", "r3.xyzw")
# A game that takes the set index from another component of the position element (source x).
OTHER_SET = PLAIN.replace("truncs r2.x___, r2.x", "truncs r2.x___, r2.y")
HASHES = {"PLAIN": "5959000000000001", "SKINNED": "5959000000000002", "OTHER_ORDER": "5959000000000003",
          "OTHER_SET": "5959000000000004"}
TEXTS = {"PLAIN": PLAIN, "SKINNED": SKINNED, "OTHER_ORDER": OTHER_ORDER, "OTHER_SET": OTHER_SET}


def half(x):
    return struct.unpack(">H", struct.pack(">e", x))[0]


def make_draw(name, vertices=24, seed=1, weights=None, sets=None, frame=1):
    """A draw row of the capture's form and its two streams' bytes, {fetch slot: bytes}."""
    rng = random.Random(seed)
    weights = weights or [[rng.uniform(-1, 1) for _ in range(4)] for _ in range(vertices)]
    sets = sets or [rng.choice([0, 4, 8]) for _ in range(len(weights))]
    # The position element: four halves in memory; the GPU's source components are (m1, m0, m3, m2), so
    # source z (the set index) is the fourth half and source x the second.
    pos = b"".join(struct.pack(">4H", half(rng.uniform(-2, 2)), half(float(rng.choice([0, 4, 8]))), half(rng.uniform(-2, 2)), half(float(s)))
                   for s in sets)
    wts = b"".join(struct.pack(">4f", *w) for w in weights)
    vc = [[rc.f32(rng.uniform(-0.5, 0.5)) for _ in range(4)] for _ in range(12)]
    bank = [0.0] * 1024
    for k in range(12):
        bank[4 * (28 + k):4 * (28 + k) + 4] = vc[k]
    row = {"kind": "draw", "frame": frame, "in_scene": True, "cmd": "0x00000001", "vbool": "0x00000000",
           "vs_hash": "0x" + HASHES[name], "bank": bank, "vs_lit": [],
           "lin": {"reason": "none", "family": "object", "multi": 0, "kind": "probe", "a0": 2,
                   "second": {"phys": 2000, "size": len(wts), "off": 0, "stride": 16, "fmt": 38, "src": "xyzw", "flags": 9},
                   "vc": vc},
           "pos": {"fetch_slot": 95, "offset_bytes": 0, "stride_bytes": 8, "format": 32, "swap16": True},
           "fetches": [{"slot": 95, "off": 0, "stride": 2, "fmt": 32, "signed": True, "norm": False, "exp": 0},
                       {"slot": 94, "off": 0, "stride": 4, "fmt": 38, "signed": True, "norm": False, "exp": 0}],
           "streams": [{"slot": 95, "phys": 1000, "size": len(pos), "file": "geo/pos.bin"},
                       {"slot": 94, "phys": 2000, "size": len(wts), "file": "geo/weights.bin"}]}
    return row, {95: pos, 94: wts}


def run_draw(name, **kw):
    row, data = make_draw(name, **kw)
    res = rc.ShaderResult()
    done = rc.check_row(row, TEXTS[name], data, res, out_reg=6)
    return res, done


class FetchSource(unittest.TestCase):
    def test_four_floats_in_memory_order(self):
        data = struct.pack(">4f", 0.25, -0.5, 0.75, 1.5)
        self.assertEqual(rc.fetch_source(38, data, 0), [0.25, -0.5, 0.75, 1.5])

    def test_halves_are_pair_swapped(self):
        data = struct.pack(">4H", half(1.0), half(2.0), half(3.0), half(4.0))
        self.assertEqual(rc.fetch_source(32, data, 0), [2.0, 1.0, 4.0, 3.0])
        self.assertEqual(rc.fetch_source(31, data, 4), [4.0, 3.0, 0.0, 0.0])

    def test_packed_fields_from_bit_zero(self):
        # 10_11_11: x in bits 0-10, y in 11-21, z in 22-31; signed fractions clamp at -1.
        word = (1023 & 0x7FF) | ((-1023 & 0x7FF) << 11) | ((-512 & 0x3FF) << 22)
        v = rc.fetch_source(16, struct.pack(">I", word), 0, signed=True, normalized=True)
        self.assertEqual(v, [1.0, -1.0, -1.0, 0.0])
        # 2_10_10_10: the two-bit field is not scaled.
        word = 511 | (0 << 10) | ((-511 & 0x3FF) << 20) | (1 << 30)
        self.assertEqual(rc.fetch_source(7, struct.pack(">I", word), 0, signed=True, normalized=True), [1.0, 0.0, -1.0, 1.0])
        # 8_8_8_8: x is the dword's low byte, the last in memory.
        self.assertEqual(rc.fetch_source(6, bytes([4, 3, 2, 255]), 0, signed=False, normalized=False), [255.0, 2.0, 3.0, 4.0])
        self.assertEqual(rc.fetch_source(6, bytes([0, 0, 0, 255]), 0, signed=False, normalized=True)[0], 1.0)

    def test_outside_the_stream(self):
        with self.assertRaises(IndexError):
            rc.fetch_source(38, bytes(15), 0)
        with self.assertRaises(NotImplementedError):
            rc.fetch_source(57, bytes(16), 0)


class NativeSide(unittest.TestCase):
    def test_weights_and_set_as_the_reader_takes_them(self):
        row, data = make_draw("PLAIN", weights=[[0.1, 0.2, 0.3, 0.4]], sets=[8])
        w = rc.native_weights(data[94], row["lin"], 0)
        self.assertEqual(w, [rc.f32(x) for x in (0.1, 0.2, 0.3, 0.4)])
        self.assertEqual(rc.native_probe_set(data[95], row["pos"], 2, 0), 8)
        vc = row["lin"]["vc"]
        want = [vc[8][i] + sum(a * b for a, b in zip(vc[9 + i], w)) for i in range(3)]
        self.assertEqual(rc.native_probe_colour(vc, 8, w), want)
        self.assertEqual(rc.native_probe_colour(vc, 12, w), want)      # a0 = min(set, 8)

    def test_refusals(self):
        row, data = make_draw("PLAIN", weights=[[0.1, 0.2, 0.3, 0.4]], sets=[4])
        for change, why in (({"src": "yxzw"}, "element"), ({"fmt": 32}, "element"), ({"flags": 5}, "element")):
            lin = dict(row["lin"], second=dict(row["lin"]["second"], **change))
            with self.assertRaises(rc.Refused) as ctx:
                rc.native_weights(data[94], lin, 0)
            self.assertEqual(str(ctx.exception), why)
        with self.assertRaises(rc.Refused):
            rc.native_weights(data[94], row["lin"], 1)                      # past the stream
        with self.assertRaises(rc.Refused):
            rc.native_weights(struct.pack(">4f", 0.0, 2000.0, 0.0, 0.0), row["lin"], 0)
        with self.assertRaises(rc.Refused):
            rc.native_weights(struct.pack(">4f", 0.0, float("nan"), 0.0, 0.0), row["lin"], 0)
        with self.assertRaises(rc.Refused):
            rc.native_probe_set(data[95], row["pos"], 0, 0)                 # not the component the reader knows
        bad = struct.pack(">4H", half(0.0), half(0.0), half(0.0), half(3.0))
        with self.assertRaises(rc.Refused) as ctx:
            rc.native_probe_set(bad, row["pos"], 2, 0)
        self.assertEqual(str(ctx.exception), "probe-set")

    def test_count_is_what_both_streams_hold(self):
        row, data = make_draw("PLAIN", vertices=5)
        self.assertEqual(rc.native_count(row["lin"], row["pos"], len(data[94]), len(data[95])), 5)
        self.assertEqual(rc.native_count(row["lin"], row["pos"], len(data[94]) - 1, len(data[95])), 4)
        self.assertEqual(rc.native_count(row["lin"], row["pos"], len(data[94]), 3 * 8), 3)


class ProbeComparison(unittest.TestCase):
    def test_both_forms_of_the_game_match_the_native_decode(self):
        for name in ("PLAIN", "SKINNED"):
            res, done = run_draw(name)
            self.assertEqual((done, res.vertices, res.draws), (24, 24, 1), name)
            self.assertLessEqual(res.max_abs, 1e-12, name)
            self.assertEqual((res.set_mismatch, res.const_mismatch, res.failed), (0, 0, 0), name)
            self.assertIsNone(res.first_bad, name)
            self.assertEqual(rc.verdict({name: res}), 0, name)

    def test_the_permuted_weights_control_fails(self):
        # Every other order of the weights is far from the game's value somewhere: the check can fail.
        for name in ("PLAIN", "SKINNED"):
            res, _ = run_draw(name)
            self.assertEqual(len(res.control), 23)
            for order, largest in res.control.items():
                self.assertGreater(largest, 0.01, (name, order))

    def test_a_reader_with_another_order_is_a_mismatch(self):
        real = rc.native_weights

        def swapped(data, lin, vertex):
            w = real(data, lin, vertex)
            return [w[3], w[2], w[1], w[0]]
        with mock.patch.object(rc, "native_weights", swapped):
            res, _ = run_draw("SKINNED")
        self.assertGreater(res.max_abs, 0.01)
        self.assertIsNotNone(res.first_bad)
        self.assertEqual(rc.verdict({"SKINNED": res}), 1)
        # ... and the control names the order that would have matched: wzyx undoes wzyx.
        self.assertLessEqual(res.control[(3, 2, 1, 0)], 1e-12)

    def test_a_game_with_another_order_is_a_mismatch(self):
        res, _ = run_draw("OTHER_ORDER")
        self.assertGreater(res.max_abs, 0.01)
        self.assertEqual(rc.verdict({"OTHER_ORDER": res}), 1)
        self.assertEqual(len(bytes.fromhex(res.first_bad["weight_bytes"])), 16)
        self.assertEqual(len(bytes.fromhex(res.first_bad["position_bytes"])), 8)

    def test_a_game_with_another_set_component_is_a_mismatch(self):
        res, _ = run_draw("OTHER_SET")
        self.assertGreater(res.set_mismatch, 0)
        self.assertEqual(rc.verdict({"OTHER_SET": res}), 1)

    def test_another_set_is_a_mismatch_even_where_the_colours_agree(self):
        # The three sets hold the same constants: the colour cannot tell which set the game took.
        row, data = make_draw("OTHER_SET")
        for k in range(4, 12):
            row["lin"]["vc"][k] = list(row["lin"]["vc"][k - 4])
            row["bank"][4 * (28 + k):4 * (28 + k) + 4] = row["lin"]["vc"][k]
        res = rc.ShaderResult()
        rc.check_row(row, OTHER_SET, data, res, out_reg=6)
        self.assertLessEqual(res.max_abs, 1e-12)
        self.assertGreater(res.set_mismatch, 0)
        self.assertIsNotNone(res.first_bad)
        self.assertEqual(rc.verdict({"OTHER_SET": res}), 1)

    def test_constants_that_are_not_the_registers_are_a_mismatch(self):
        row, data = make_draw("PLAIN")
        row["bank"][4 * 30] += 0.25
        res = rc.ShaderResult()
        rc.check_row(row, PLAIN, data, res, out_reg=6)
        self.assertEqual(res.const_mismatch, 1)
        self.assertEqual(rc.verdict({"PLAIN": res}), 1)
        # ... also in a component no colour reads (the base register's w).
        row, data = make_draw("PLAIN")
        row["bank"][4 * 28 + 3] += 0.25
        res = rc.ShaderResult()
        rc.check_row(row, PLAIN, data, res, out_reg=6)
        self.assertLessEqual(res.max_abs, 1e-12)
        self.assertEqual(res.const_mismatch, 1)
        self.assertEqual(rc.verdict({"PLAIN": res}), 1)

    def test_the_literals_lie_over_the_bank(self):
        row, _ = make_draw("PLAIN")
        row["bank"][4 * 254:4 * 256] = [9.0] * 8
        row["vs_lit"] = [{"reg": 254, "v": [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 0.1]}]
        c = rc.game_constants(row)
        self.assertEqual(c[254], [1.0, 2.0, 3.0, 4.0])
        self.assertEqual(c[255], [5.0, 6.0, 7.0, rc.f32(0.1)])
        self.assertEqual(c[253], [0.0] * 4)
        self.assertEqual(len(c), 256)

    def test_equal_weights_are_blind(self):
        res, _ = run_draw("PLAIN", weights=[[0.25] * 4] * 6)
        self.assertEqual(res.max_abs, 0.0)
        self.assertEqual(rc.verdict({"PLAIN": res}), 3)

    def test_a_refused_draw_is_not_compared(self):
        row, data = make_draw("PLAIN", sets=[0, 4, 3, 8])
        res = rc.ShaderResult()
        self.assertEqual(rc.check_row(row, PLAIN, data, res, out_reg=6), 0)
        self.assertEqual((res.draws, res.vertices, dict(res.refused)), (0, 0, {"probe-set": 1}))
        self.assertEqual(rc.verdict({"PLAIN": res}), 2)
        row, data = make_draw("PLAIN")
        del data[94]
        res = rc.ShaderResult()
        self.assertEqual(rc.check_row(row, PLAIN, data, res, out_reg=6), 0)
        self.assertEqual(dict(res.refused), {"stream": 1})

    def test_a_vertex_the_microcode_cannot_run_is_not_a_match(self):
        row, data = make_draw("PLAIN", vertices=4)
        row["fetches"] = [f for f in row["fetches"] if f["slot"] != 94] + [
            {"slot": 94, "off": 0, "stride": 4, "fmt": 32, "signed": True, "norm": False, "exp": 0}]   # not the microcode's format
        res = rc.ShaderResult()
        self.assertEqual(rc.check_row(row, PLAIN, data, res, out_reg=6), 0)
        self.assertEqual((res.failed, res.vertices, res.draws), (4, 0, 1))
        self.assertEqual(rc.verdict({"PLAIN": res}), 2)
        good, _ = run_draw("SKINNED")
        self.assertEqual(rc.verdict({"PLAIN": res, "SKINNED": good}), 2)      # also beside a shader that matched

    def test_a_third_stream_past_its_bytes_is_zero_and_counted(self):
        text = PLAIN.replace("/*    0.1 */", "/*    2   */          vfetch_full r9, r3.x, vf3, DataFormat=FMT_32_32_32_32_FLOAT, "
                                              "Stride=4, Signed=true, NumFormat=integer\n/*    0.1 */")
        row, data = make_draw("PLAIN", weights=[[0.5, 0.25, 0.125, 2.0]] * 3)
        row["fetches"].append({"slot": 92, "off": 0, "stride": 4, "fmt": 38, "signed": True, "norm": False, "exp": 0})
        row["streams"].append({"slot": 92, "phys": 3000, "size": 16, "file": "geo/bones.bin"})
        data[92] = bytes(16)                      # one element; the index 0.5 rounds to 1: past it
        res = rc.ShaderResult()
        self.assertEqual(rc.check_row(row, text, data, res, out_reg=6), 3)
        self.assertEqual((res.outside, res.failed), (3, 0))
        self.assertLessEqual(res.max_abs, 1e-12)


class Command(unittest.TestCase):
    def _capture(self, folder, names, spoil=False):
        folder = Path(folder)
        (folder / "dumps").mkdir()
        lines = [json.dumps({"kind": "frame", "frame": 1}, separators=(",", ":"))]
        for n, name in enumerate(names):
            row, data = make_draw(name, seed=n + 1)
            sub = folder / f"geo{n}"
            sub.mkdir()
            for s in row["streams"]:
                s["file"] = f"geo{n}/{Path(s['file']).name}"
                (folder / s["file"]).write_bytes(data[s["slot"]])
            (folder / "dumps" / f"shader_{HASHES[name]}.ucode.vert").write_text(TEXTS[name])
            lines.append(json.dumps(row, separators=(",", ":")))
            if spoil:      # the same draw outside the main scene and one whose light inputs were refused: not probe rows
                outside = dict(row, in_scene=False, bank=[0.5] + row["bank"][1:])
                lines.append(json.dumps(outside, separators=(",", ":")))
                lines.append(json.dumps(dict(row, lin={"reason": "light-unknown", "kind": "probe"}), separators=(",", ":")))
        path = folder / "native_discovery_test.jsonl"
        path.write_text("\n".join(lines) + "\n")
        return path

    def _main(self, path, *more):
        out = io.StringIO()
        outputs = {h: 6 for h in HASHES.values()}
        with mock.patch.dict(rc.PROBE_OUTPUT, outputs), contextlib.redirect_stdout(out):
            code = rc.main([str(path), "--dumps", str(path.parent / "dumps"), *more])
        return code, out.getvalue()

    def test_match_prints_the_table_and_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, text = self._main(self._capture(tmp, ["PLAIN", "SKINNED"], spoil=True))
        self.assertEqual(code, 0, text)
        self.assertIn("MATCH", text)
        for name in ("PLAIN", "SKINNED"):
            line = next(ln for ln in text.splitlines() if ln.startswith(HASHES[name] + " "))
            self.assertEqual(line.split()[1:3], ["1", "24"], line)

    def test_draws_outside_the_main_scene_only_when_asked(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, text = self._main(self._capture(tmp, ["PLAIN"], spoil=True), "--out-of-scene")
        self.assertEqual(code, 0, text)
        self.assertEqual(next(ln for ln in text.splitlines() if ln.startswith(HASHES["PLAIN"] + " ")).split()[1:3], ["2", "48"])

    def test_mismatch_exits_one_with_the_example(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, text = self._main(self._capture(tmp, ["PLAIN", "OTHER_ORDER"]))
        self.assertEqual(code, 1, text)
        self.assertIn("MISMATCH", text)
        self.assertIn("FIRST MISMATCH", text)
        self.assertIn("weight bytes", text)

    def test_no_probe_draw_exits_two(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self._capture(tmp, ["PLAIN"])
            code, text = self._main(path, "--frame", "7")
        self.assertEqual(code, 2, text)
        self.assertIn("INCOMPLETE", text)

    def test_equal_draws_are_counted_once_unless_asked(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self._capture(tmp, ["PLAIN"])
            path.write_text(path.read_text() + path.read_text().splitlines()[1] + "\n")
            code, text = self._main(path)
            code_every, text_every = self._main(path, "--every")
        self.assertEqual((code, code_every), (0, 0))
        self.assertIn("1 draw rows equal to an earlier one", text)
        self.assertEqual(next(ln for ln in text_every.splitlines() if ln.startswith(HASHES["PLAIN"] + " ")).split()[1:3], ["2", "48"])

    def test_the_parse_cache_is_taken_off_again(self):
        real = rc.ue.st.parse
        with tempfile.TemporaryDirectory() as tmp:
            self._main(self._capture(tmp, ["PLAIN"]))
        self.assertIs(rc.ue.st.parse, real)


if __name__ == "__main__":
    unittest.main()
