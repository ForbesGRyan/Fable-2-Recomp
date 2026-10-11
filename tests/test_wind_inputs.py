"""The wind inputs of the capture (foliage plan Task 8; spec criterion 3): the native header's bounds, reasons and
literal counts against the Python reference, and `wind_check.py inputs`, which holds a capture's "wind" members
against the same rows' bank, fetches and streams."""
import copy
import json
import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import wind_check as wc  # noqa: E402

HEADER = ROOT / "src" / "native" / "capture" / "wind_inputs.h"
TABLE = ROOT / "src" / "native" / "capture" / "wind_table.inc"
TRUNK, LEAF, GRASS = "475EC9F795E5EDBB", "A5846836C90E1192", "8123C16DBF583F92"
LEAF_CASTER = "432563420047C96C"
CAPTURE = wc.cr.LOGS / "native_discovery_20261010_182313.jsonl"


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


class Header(unittest.TestCase):
    """wind_inputs.h is written by hand; these hold its numbers to the reference the formulas were checked with."""

    def text(self):
        return HEADER.read_text(encoding="utf-8")

    def test_the_register_bounds_are_the_checkers(self):
        found = {}
        for tree, slot, reg, comp, lo, hi in re.findall(
                r"^\s*\{([01]), (\d), (\d+), (\d), (-?[0-9.e]+)f, (-?[0-9.e]+)f\},$", self.text(), re.M):
            found[(int(tree), int(slot), int(reg), int(comp))] = (float(lo), float(hi))
        want = {}
        for family in ("trunk", "plant"):
            regs = lf.WIND_REGISTERS[family]
            for (reg, comp), bounds in wc.REGISTER_BOUNDS[family].items():
                want[(1 if family == "trunk" else 0, regs.index(reg), reg, comp)] = bounds
        self.assertEqual(found, want)
        self.assertEqual(wc.REGISTER_BOUNDS["leaf"], wc.REGISTER_BOUNDS["trunk"])
        self.assertEqual(lf.WIND_REGISTERS["leaf"], lf.WIND_REGISTERS["trunk"])
        for lo, hi in want.values():                    # each bound is a single-precision number: no edge moves
            self.assertEqual((f32(lo), f32(hi)), (lo, hi))

    def test_the_literal_counts_and_the_registers(self):
        text = self.text()
        m = re.search(r"return f == WindFamily::kTrunk \? (\d+)u : f == WindFamily::kLeaf \? (\d+)u : "
                      r"f == WindFamily::kPlant \? (\d+)u : 0u;", text)
        self.assertEqual(tuple(int(x) for x in m.groups()),
                         tuple(len(lf.WIND_LITERAL_ORDER[f]) for f in ("trunk", "leaf", "plant")))
        m = re.search(r"kWindRegisters = (\d+), kWindLiterals = (\d+), kWindElements = (\d+);", text)
        self.assertEqual(tuple(int(x) for x in m.groups()),
                         (max(len(r) for r in lf.WIND_REGISTERS.values()),
                          max(len(o) for o in lf.WIND_LITERAL_ORDER.values()), 3))
        m = re.search(r"kWindLiteralFirst = (\d+), kWindLiteralRegisters = (\d+);", text)
        first, count = int(m.group(1)), int(m.group(2))
        for row in lf.WIND_SHADERS.values():
            for reg, comp in row["lit_at"].values():
                self.assertTrue(first <= reg < first + count and 0 <= comp < 4)
            for reg in row["reg"]:
                self.assertLess(reg, first)

    def test_the_reasons_names(self):
        names = re.findall(r'case WindReason::k\w+: return "([a-z-]+)";', self.text())
        self.assertEqual(tuple(names), wc.WIND_REASONS)
        self.assertEqual(wc.WIND_REASONS, ("none", "off", "no-entry", "register-unread", "register-not-finite",
                                           "register-range", "literal", "element", "stream", "instance", "over-limit"))

    def test_a_slot_is_its_name_in_the_familys_order(self):
        # The table's lines carry no names (review of Tasks 5 to 7, item 1): slot k of a shader is the k-th name of
        # light_formulas.WIND_LITERAL_ORDER[family], at the place and with the bits its row of WIND_SHADERS has.
        # Whoever indexes the native literals by number (the vertex program's o_wind_lit) holds to this order.
        lines = re.findall(r"^FABLE2_WIND_LITERAL\(0x([0-9A-F]{16})ull, (\d+), (\d+), (\d), 0x([0-9A-F]{8})u\)$",
                           TABLE.read_text(encoding="utf-8"), re.M)
        self.assertEqual(len(lines), 6 * 7 + 2 * 18 + 4 * 28)
        seen = set()
        for hash_, slot, reg, comp, bits in lines:
            row = lf.WIND_SHADERS[hash_]
            name = lf.WIND_LITERAL_ORDER[row["family"]][int(slot)]
            self.assertEqual(row["lit_at"][name], (int(reg), int(comp)), (hash_, name))
            self.assertEqual(struct.pack("<I", int(bits, 16)), struct.pack("<f", row["lit"][name]), (hash_, name))
            seen.add((hash_, int(slot)))
        self.assertEqual(len(seen), len(lines))
        # a leaf's first eighteen names are a trunk's, in the same order (one block of the tree's sway)
        self.assertEqual(lf.WIND_LITERAL_ORDER["leaf"][:18], lf.WIND_LITERAL_ORDER["trunk"])

    def test_the_tables_order_is_the_specs_index(self):
        order = wc.table_order(TABLE)
        self.assertEqual(sorted(order), sorted(lf.WIND_SHADERS))
        self.assertEqual(len(order), 12)


def made_row(hash_, line=7, frame=1):
    """A draw row as the capture writes it, for a shader of WIND_SHADERS: registers inside their bounds, the
    shader's own literal table, its fetches delivering every component, one stream of 100 vertices."""
    row = lf.WIND_SHADERS[hash_]
    bank = [0.0] * 1024
    for reg in row["reg"]:
        bank[4 * reg:4 * reg + 4] = [f32(0.5 + 0.001 * reg + 0.0001 * c) for c in range(4)]
    lit = [0.0] * 32
    for name, (reg, comp) in row["lit_at"].items():
        lit[(reg - 248) * 4 + comp] = row["lit"][name]
    fetches = [{"i": i, "slot": 95, "fmt": 16, "off": 2, "stride": 9, "signed": True, "norm": True, "swz": 1672}
               for i in range(8)]
    for _name, fetch, fmt, offset, _source, _count in row["elements"]:
        fetches[fetch] = {"i": fetch, "slot": 95, "fmt": fmt, "off": offset, "stride": 9, "signed": fmt == 32,
                          "norm": fmt == 6, "swz": 1672}
    r = {"kind": "draw", "frame": frame, "line": line, "in_scene": True, "vs_hash": "0x" + hash_, "bank": bank,
         "vs_lit": [{"reg": 248, "v": lit}], "fetches": fetches,
         "streams": [{"slot": 95, "phys": 0x1000000, "size": 3600, "endian": 2}],
         "ib": {"max_index": 99, "base_vertex": 0}, "base_vertex": 0}
    return r


class Predict(unittest.TestCase):
    """wind_check.expected_inputs: what MakeWindInputs must give for a row, from the row's own evidence."""

    def reason(self, row):
        return wc.expected_inputs(row)["reason"]

    def test_a_good_row_of_each_family(self):
        for hash_ in lf.WIND_SHADERS:
            with self.subTest(hash_):
                want = wc.expected_inputs(made_row(hash_))
                self.assertEqual(want["reason"], "none")
                self.assertEqual(want["family"], lf.WIND_SHADERS[hash_]["family"])
                self.assertEqual(len(want["reg"]), 160)
        want = wc.expected_inputs(made_row(LEAF))
        self.assertEqual(want["pivot"], {"phys": 0x1000000, "size": 3600, "off": 20, "stride": 36, "fmt": 32,
                                         "src": "yxw", "flags": 9})
        self.assertEqual(want["weights"], {"phys": 0x1000000, "size": 3600, "off": 28, "stride": 36, "fmt": 6,
                                           "src": "xyzw", "flags": 10})
        self.assertEqual(want["flutter"]["src"], "xyz")
        self.assertNotIn("pivot", wc.expected_inputs(made_row(TRUNK)))
        self.assertNotIn("weights", wc.expected_inputs(made_row(GRASS)))

    def test_the_registers_bits(self):
        r = made_row(TRUNK)
        r["bank"][4 * 15] = f32(123.456)
        want = wc.expected_inputs(r)
        self.assertEqual(want["reg"][:8], "%08X" % struct.unpack("<I", struct.pack("<f", 123.456))[0])
        self.assertEqual(want["reg"][96:], "0" * 64)            # a tree has no fourth and fifth register
        # every component of every register, in the row's order (c15, c16, c17), written out here
        words = ["%08X" % struct.unpack("<I", struct.pack("<f", r["bank"][4 * reg + c]))[0] for reg in (15, 16, 17) for c in range(4)]
        self.assertEqual(len(set(words)), 12)
        self.assertEqual(want["reg"][:96], "".join(words))
        self.assertEqual(wc.expected_inputs(made_row(GRASS))["reg"][96:104],
                         "%08X" % struct.unpack("<I", struct.pack("<f", made_row(GRASS)["bank"][100]))[0])

    def test_each_refusal(self):
        for hash_ in (TRUNK, LEAF, GRASS, LEAF_CASTER):
            reg = lf.WIND_SHADERS[hash_]["reg"]
            with self.subTest(hash_):
                r = made_row(hash_)
                r["vs_hash"] = "0x36B543CECAE5C781"
                self.assertEqual(self.reason(r), "no-entry")
                r = made_row(hash_)
                r["vs_lit"][0]["v"][-1] = 7.0                   # one literal (each family names c255.w)
                self.assertEqual(self.reason(r), "literal")
                r = made_row(hash_)
                r["vs_lit"] = [{"reg": 252, "v": r["vs_lit"][0]["v"][16:]}, {"reg": reg[0], "v": [0.0] * 4}]
                self.assertEqual(self.reason(r), "literal")     # the shader loads a wind register itself
                r = made_row(hash_)
                del r["bank"]
                self.assertEqual(self.reason(r), "register-unread")
                r = made_row(hash_)
                r["bank"][4 * reg[1] + 3] = float("nan")
                self.assertEqual(self.reason(r), "register-not-finite")
                r = made_row(hash_)
                r["bank"][4 * reg[0]] = 2e7
                self.assertEqual(self.reason(r), "register-range")
        r = made_row(GRASS)
        del r["ib"]
        self.assertEqual(self.reason(r), "instance")            # a plant that is not an indexed draw has no set
        r = made_row(GRASS)
        r["bank"][4 * 25] = 0.0
        r["bank"][4 * 26:4 * 26 + 2] = [0.0, 0.0]
        self.assertEqual(self.reason(r), "none")                # no sway is not out of range (ledger ruling 26)
        for name, key, value, want in (("offset", "off", 6, "element"), ("format", "fmt", 7, "element"),
                                       ("not delivered", "swz", 0o7770, "element"), ("stride", "stride", 7, "element")):
            with self.subTest(name):
                r = made_row(TRUNK)
                r["fetches"][4][key] = value
                self.assertEqual(self.reason(r), want)
        r = made_row(TRUNK)
        r["streams"] = []
        self.assertEqual(self.reason(r), "stream")
        r = made_row(TRUNK)
        r["ib"]["max_index"] = 100
        self.assertEqual(self.reason(r), "stream")
        r = made_row(TRUNK)
        r["ib"]["max_index"] = -1
        self.assertEqual(self.reason(r), "stream")
        r = made_row(LEAF)
        r["fetches"][5]["off"] = 6
        self.assertEqual(self.reason(r), "element")


def write_capture(folder, rows, name="native_discovery_20990101_000000.jsonl"):
    path = Path(folder) / name
    with open(path, "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps({k: v for k, v in r.items() if k != "line"}, separators=(",", ":")) + "\n")   # (as the capture writes)
    return path


def frame_row(frame, wind):
    return {"kind": "frame", "frame": frame, "captured": 1, "wind": wind}


def with_member(row):
    r = copy.deepcopy(row)
    r["wind"] = wc.expected_inputs(r)
    return r


class Inputs(unittest.TestCase):
    """wind_check.check_inputs on made captures: what passes, and every way it must fail."""

    def run_on(self, rows):
        with tempfile.TemporaryDirectory() as tmp:
            return wc.check_inputs(write_capture(tmp, rows))

    def good(self, n=3, frame=1):
        rows = [with_member(made_row(h, line=i, frame=frame)) for i, h in enumerate((TRUNK, LEAF, GRASS, LEAF_CASTER) * n)]
        records = sum(1 for r in rows if r["vs_hash"][2:] != LEAF_CASTER)
        wind = {"records": records, "casters": n, "family": {"trunk": n, "leaf": n, "plant": n},
                "reason": {"none": records, "no-entry": 50}, "caster_reason": {"none": n, "no-entry": 4}}
        return rows + [frame_row(frame, wind)]

    def test_a_capture_that_agrees(self):
        r = self.run_on(self.good())
        self.assertEqual((r.rows, r.compared, r.problems), (12, 12, []))
        self.assertEqual(r.frames_on, 1)
        self.assertEqual(r.reasons, {"none": 12})
        self.assertTrue(r.ok)

    def test_one_bit_of_a_register(self):
        rows = self.good()
        reg = rows[0]["wind"]["reg"]
        rows[0]["wind"]["reg"] = reg[:7] + ("0" if reg[7] != "0" else "1") + reg[8:]
        r = self.run_on(rows)
        self.assertFalse(r.ok)
        self.assertIn("reg", r.problems[0])

    def test_an_element_at_another_place(self):
        rows = self.good()
        rows[1]["wind"]["flutter"]["off"] = 28
        r = self.run_on(rows)
        self.assertFalse(r.ok)
        self.assertIn("flutter", r.problems[0])

    def test_another_reason_than_the_rows_evidence_gives(self):
        rows = self.good()
        rows[2]["wind"] = {"reason": "register-range", "family": "plant"}
        r = self.run_on(rows)
        self.assertFalse(r.ok)
        self.assertIn("register-range", r.problems[0])

    def test_a_wind_row_without_the_member(self):
        rows = self.good()
        del rows[3]["wind"]
        r = self.run_on(rows)
        self.assertFalse(r.ok)
        self.assertIn("no \"wind\" member", r.problems[0])

    def test_a_reason_in_more_than_one_of_a_hundred(self):
        # 200 wind rows, 3 of them out of range and saying so truthfully: each row agrees, the share does not.
        rows = []
        for i in range(200):
            row = made_row(TRUNK, line=i)
            if i < 3:
                row["bank"][4 * 17] = 20.0
            rows.append(with_member(row))
        rows.append(frame_row(1, {"records": 197, "casters": 0, "family": {"trunk": 197, "leaf": 0, "plant": 0},
                                  "reason": {"none": 197, "register-range": 3}, "caster_reason": {}}))
        r = self.run_on(rows)
        self.assertEqual(r.reasons, {"none": 197, "register-range": 3})
        self.assertFalse(r.ok)
        self.assertTrue(any("register-range" in p and "1%" in p for p in r.problems))
        # ... also when the frame row's own counts hide it (the rows are what is judged)
        hidden = rows[:-1] + [frame_row(1, {"records": 197, "casters": 0, "family": {"trunk": 197, "leaf": 0, "plant": 0},
                                            "reason": {"none": 197}, "caster_reason": {}})]
        r = self.run_on(hidden)
        self.assertFalse(r.ok)
        self.assertEqual(["reason \"register-range\" in 3 of 200 wind draws: more than 1%"], r.problems)
        rows = rows[2:]                                         # 1 of 198: within the bound
        rows[-1] = frame_row(1, {"records": 197, "casters": 0, "family": {"trunk": 197, "leaf": 0, "plant": 0},
                                 "reason": {"none": 197, "register-range": 1}, "caster_reason": {}})
        self.assertTrue(self.run_on(rows).ok)

    def test_the_frame_rows_counts_must_add_up(self):
        rows = self.good()
        rows[-1]["wind"]["family"]["leaf"] = 7
        r = self.run_on(rows)
        self.assertFalse(r.ok)
        self.assertTrue(any("family" in p for p in r.problems))
        rows = self.good()
        rows[-1]["wind"]["reason"]["none"] = 2
        self.assertFalse(self.run_on(rows).ok)
        rows = self.good()
        rows[-1]["wind"]["reason"]["stream"] = 40               # of the scene's records: more than 1% at rest
        self.assertFalse(self.run_on(rows).ok)

    def test_an_off_capture(self):
        rows = [made_row(TRUNK, line=1), made_row(GRASS, line=2), frame_row(1, "off")]
        r = self.run_on(rows)
        self.assertEqual((r.frames_on, r.frames_off, r.compared), (0, 1, 0))
        self.assertTrue(r.off_clean)
        self.assertFalse(r.ok)                                  # nothing was compared: not a result for the inputs
        rows[0] = with_member(rows[0])                          # a member in a frame that says off
        r = self.run_on(rows)
        self.assertFalse(r.off_clean)
        self.assertTrue(any("off" in p for p in r.problems))

    def test_a_capture_from_before_the_task(self):
        rows = [made_row(TRUNK, line=1), {"kind": "frame", "frame": 1, "captured": 1}]
        r = self.run_on(rows)
        self.assertEqual((r.frames_on, r.frames_off, r.compared, r.rows), (0, 0, 0, 1))
        self.assertFalse(r.ok)
        self.assertEqual(r.predicted, {"none": 1})

    def test_the_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = []
            good = write_capture(tmp, self.good())
            self.assertEqual(wc.main(["inputs", str(good)], say=out.append), 0)
            self.assertTrue(any("12 of 12" in line for line in out))
            rows = self.good()
            del rows[0]["wind"]
            bad = write_capture(tmp, rows, "native_discovery_20990101_000001.jsonl")
            self.assertEqual(wc.main(["inputs", str(bad)], say=out.append), 1)
            off = write_capture(tmp, [made_row(TRUNK), frame_row(1, "off")], "native_discovery_20990101_000002.jsonl")
            out.clear()
            self.assertEqual(wc.main(["inputs", str(off)], say=out.append), 1)
            self.assertTrue(any("NOT A RESULT" in line for line in out))
            self.assertEqual(wc.main(["inputs", "--off", str(off)], say=out.append), 0)
            self.assertEqual(wc.main(["inputs", "--off", str(good)], say=out.append), 1)


@unittest.skipUnless(CAPTURE.is_file(), f"no capture {CAPTURE.name}")
class RealRows(unittest.TestCase):
    def test_the_games_rows_would_all_have_inputs(self):
        # A capture from before the task: no member to compare, but the rows' own evidence says what the capture
        # must give. Every wind draw row of its first frame is inside every rule.
        rows = [r for r in wc.cr.rows(CAPTURE, kinds=("draw",), frame=1) if wc.cr.vs_hash(r) in lf.WIND_SHADERS]
        self.assertGreater(len(rows), 200)
        reasons = {}
        for r in rows:
            reason = wc.expected_inputs(r)["reason"]
            reasons[reason] = reasons.get(reason, 0) + 1
        self.assertEqual(reasons, {"none": len(rows)})


if __name__ == "__main__":
    unittest.main()
