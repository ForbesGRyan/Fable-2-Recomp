import importlib.util
import json
import random
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("mf", ROOT / "tools" / "xdk_sigmatch" / "matrix_finder.py")
mf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mf)
SPEC2 = importlib.util.spec_from_file_location("gtt", ROOT / "tools" / "xdk_sigmatch" / "gen_transform_table.py")
gtt = importlib.util.module_from_spec(SPEC2)
SPEC2.loader.exec_module(gtt)

# A perspective-ish matrix mapping the unit cube in front of the camera into clip space.
M = [[1.2, 0.0, 0.0, 0.0],
     [0.0, 1.6, 0.0, 0.0],
     [0.0, 0.0, 1.0, -0.1],
     [0.0, 0.0, 1.0, 0.0]]


def bank_with(rows, base, layout, seed=1):
    rnd = random.Random(seed)
    bank = [rnd.uniform(-50, 50) for _ in range(1024)]
    for r in range(4):
        for c in range(4):
            v = rows[r][c] if layout == "dot" else rows[c][r]
            bank[4 * (base + r) + c] = v
    return bank


def verts(seed=2, n=40):
    rnd = random.Random(seed)
    return [[rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(2, 10), 1.0] for _ in range(n)]


class FinderTests(unittest.TestCase):
    def test_finds_dot_layout(self):
        samples = [(bank_with(M, 12, "dot", s), verts(s)) for s in range(3)]
        r = mf.find_transform(samples)
        self.assertEqual((r["base"], r["layout"]), (12, "dot"))
        self.assertGreaterEqual(r["score"], 0.9)

    def test_finds_combine_layout(self):
        samples = [(bank_with(M, 40, "combine", s), verts(s)) for s in range(3)]
        r = mf.find_transform(samples)
        self.assertEqual((r["base"], r["layout"]), (40, "combine"))

    def test_rejects_bank_without_matrix(self):
        rnd = random.Random(9)
        samples = [([rnd.uniform(-50, 50) for _ in range(1024)], verts(s)) for s in range(5)]
        self.assertIsNone(mf.find_transform(samples))

    def test_product_of_two_windows(self):
        # Vertices behind the camera; "world" flips z so only view-projection x world fits.
        # Sparse bank keeps the O(windows^2) product search fast.
        world = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, -1, 0], [0, 0, 0, 1]]
        bank = [0.0] * 1024
        for r in range(4):
            for c in range(4):
                bank[4 * (8 + r) + c] = M[r][c]
                bank[4 * (20 + r) + c] = world[r][c]
        rnd = random.Random(4)
        behind = [[rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-10, -2), 1.0] for _ in range(40)]
        samples = [(bank, behind)]
        self.assertIsNone(mf.find_transform(samples, min_score=0.99))  # neither window alone fits
        r = mf.find_transform(samples, products=True, min_score=0.99)
        self.assertEqual((r.get("base"), r.get("base2")), (8, 20))

    def test_cli_merges_and_keeps_manual(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            log = d / "disc.jsonl"
            rows = [{"kind": "draw", "vs_hash": "0x1", "bank": bank_with(M, 12, "dot", s), "positions": verts(s),
                     "viewport": [1120, 720]} for s in range(2)]
            # Decoded from a stream the draw indexes past: not a position sample.
            rows.append({"kind": "draw", "vs_hash": "0x1", "bank": bank_with(M, 12, "dot", 7),
                         "positions": verts(7), "pos_suspect": True, "viewport": [1120, 720]})
            rows.append({"kind": "meta"})
            log.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            out = d / "vs.json"
            out.write_text(json.dumps({"0x2": {"base": 4, "layout": "combine", "manual": True, "pos_fetch": 1}}))
            mf.main([str(log), "--out", str(out)])
            data = json.loads(out.read_text())
            self.assertEqual(data["0x1"]["base"], 12)
            self.assertEqual(data["0x1"]["samples"], 2)
            self.assertTrue(data["0x2"]["manual"])
            inc = d / "t.inc"
            gtt.main(["--json", str(out), "--out", str(inc)])
            text = inc.read_text()
            self.assertIn("FABLE2_VS_TRANSFORM(0x1ull, 12, 0, -1)", text)
            self.assertIn("FABLE2_VS_TRANSFORM(0x2ull, 4, 1, 1)", text)
            self.assertLess(text.index("0x1ull"), text.index("0x2ull"))


if __name__ == "__main__":
    unittest.main()
