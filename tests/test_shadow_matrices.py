import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import shadow_matrices as sm  # noqa: E402

VS_WORLD = "0xF160B4DA459A6D40"   # oPos = c0..c3 . p with world rows c4..c6
VS_DIRECT = "0xA8A5000000000000"  # stands for an instancing shader whose c0..c3 is the light matrix
SHADOW_SURFACE = "0x10000410"     # pitch 1040
SCENE_SURFACE = "0x11810460"      # pitch 1120
HALF = 0.5 / 1024


def f32(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


def light(tx=-6.732423, ty=-10.841799, scale=1.0):
    """An orthographic light matrix of the shape the bridge capture shows (rows x, y, z, w)."""
    return [[0.052114 * scale, 0.0, 0.041577 * scale, tx],
            [0.000898 * scale, 0.066651 * scale, -0.001126 * scale, ty],
            [0.005422, -0.000188, -0.006796, 0.737847],
            [0.0, 0.0, 0.0, 1.0]]


def world(angle, tx, ty, tz, s=1.0):
    c, n = math.cos(angle) * s, math.sin(angle) * s
    return [[c, 0.0, n, tx], [0.0, s, 0.0, ty], [-n, 0.0, c, tz]]


def mul(a, w):
    """a (4x4) times the affine matrix whose first three rows are w."""
    w4 = w + [[0.0, 0.0, 0.0, 1.0]]
    return [[sum(a[i][k] * w4[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def clear_row(frame, surface=SHADOW_SURFACE):
    return {"kind": "raw", "frame": frame, "func": "DrawIndx2:821EF988",
            "args": ["0x4410B500", "0x00000001", "0x7049C8C0", "0x7049CC70", "0x00000400", surface,
                     "0x00000000", "0x00008777"]}


def shadow_row(frame, lvp, w, vs=VS_WORLD, direct=False, surface=SHADOW_SURFACE, mask=0):
    m = lvp if direct else mul(lvp, w)
    bank = [f32(v) for r in m for v in r] + [f32(v) for r in w for v in r] + [0.0] * 4
    return {"kind": "draw", "frame": frame, "func": "D3DDevice_DrawIndexedVertices?", "in_scene": False,
            "rs": {"2104": f"0x{mask:08X}", "2200": "0x00708766"}, "surface_info": surface, "vs_hash": vs,
            "bank": bank}


def map_rows(lvp, su=1, sv=1):
    """c36..c39 (or c40..c43) of a light matrix: texture space with the half texel of sign su, sv."""
    x, y, z, w = lvp
    return [[0.5 * a + (0.5 + su * HALF) * b for a, b in zip(x, w)],
            [-0.5 * a + (0.5 + sv * HALF) * b for a, b in zip(y, w)], list(z), list(w)]


def scene_row(frame, near, far, c12=(20.0, 35.0, 2.0, 0.486), unread=()):
    regs = {12: c12, 13: (1 / 1024, 1 / 1024, 0.0, 0.0), 14: (1 / 1024, 1 / 1024, 0.0, 0.0)}
    for i in range(4):
        regs[36 + i] = near[i]
        regs[40 + i] = far[i]
    full = "".join("-" * 32 if r in unread else struct.pack(">4f", *regs.get(r, (0.0,) * 4)).hex().upper()
                   for r in range(256))
    return {"kind": "draw", "frame": frame, "func": "D3DDevice_DrawIndexedVertices?", "in_scene": True,
            "rs": {"2104": "0x0000000F"}, "surface_info": SCENE_SURFACE, "pfull": full}


def frame_rows(frame, near, far, su=1, sv=1, near_rows=4, far_rows=3):
    """One frame as the capture writes it: clear, near pass, clear, far pass, scene."""
    rows = [clear_row(frame)]
    rows += [shadow_row(frame, near, world(0.3 * i, 80.0 + i, 150.0, 50.0 - i, 1.0 + 0.1 * i)) for i in range(near_rows)]
    rows += [clear_row(frame)]
    rows += [shadow_row(frame, far, world(-0.2 * i, 70.0 - i, 160.0, 40.0 + i)) for i in range(far_rows)]
    rows += [scene_row(frame, map_rows(near, su, sv), map_rows(far, su, sv)) for _ in range(5)]
    return rows


def run(rows, *args):
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "capture.jsonl"
        with open(path, "w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r, separators=(",", ":")) + "\n")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = sm.main([str(path), *args])
    return code, out.getvalue()


NEAR, FAR = light(), light(-3.123047, -4.865234, 30.0 / 70.0)


class ShadowMatricesTest(unittest.TestCase):
    def test_exact_pass_agrees(self):
        code, out = run(frame_rows(1, NEAR, FAR))
        self.assertEqual(code, 0, out)
        self.assertIn("RESULT: OK", out)
        self.assertIn("frame 1 pass 0 (near): 4 rows, 4 used", out)
        self.assertIn("frame 1 pass 1 (far): 3 rows, 3 used", out)
        self.assertIn("passes compared: 2, agreeing: 2", out)
        # The median is the light matrix itself, not a multiple or a transpose of it.
        res = sm.analyse_rows(sm.read_rows_from(iter_lines(frame_rows(1, NEAR, FAR))))
        m = res.frames[1].passes[0].median
        for i in range(4):
            for j in range(4):
                self.assertAlmostEqual(m[i][j], NEAR[i][j], places=5)
        self.assertLess(res.frames[1].passes[0].max_dev, 1e-4)
        self.assertLess(res.frames[1].passes[1].residual, 1e-5)

    def test_wrong_row_is_named(self):
        rows = frame_rows(3, NEAR, FAR)
        bad = shadow_row(3, light(tx=-6.5), world(0.1, 81.0, 150.0, 49.0), vs="0xFBD39C64463E180B")
        rows.insert(3, bad)  # the third shadow row of the near pass, line 4 of the file
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn("RESULT: FAIL", out)
        self.assertRegex(out, r"frame 3 pass 0 \(near\) row 2 \(line 4\) vs 0xFBD39C64463E180B: deviation 0\.23")
        self.assertIn("passes compared: 2, agreeing: 1", out)
        # The pass's matrix is the median, not its first row: a wrong first row is the one named.
        rows = frame_rows(3, NEAR, FAR)
        rows.insert(1, bad)
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertEqual(out.count("deviation 0.23"), 1, out)
        self.assertIn("frame 3 pass 0 (near) row 0 (line 2) vs 0xFBD39C64463E180B", out)

    def test_singular_world_block_is_skipped_and_counted(self):
        rows = frame_rows(1, NEAR, FAR)
        dead = shadow_row(1, NEAR, [[0.0] * 4, [0.0] * 4, [0.0] * 4])
        nan = shadow_row(1, NEAR, world(0.0, 1.0, 2.0, 3.0))
        nan["bank"][16] = float("nan")
        rows[1:1] = [dead, nan]
        code, out = run(rows)
        self.assertEqual(code, 0, out)
        self.assertIn("frame 1 pass 0 (near): 6 rows, 4 used, 2 skipped (singular or non-finite world rows)", out)
        # A pass with nothing but singular rows proves nothing and fails.
        only = [clear_row(1), dead, clear_row(1)] + frame_rows(1, NEAR, FAR)[6:]
        code, out = run(only)
        self.assertEqual(code, 1, out)
        self.assertIn("no usable row", out)

    def test_half_texel_of_either_sign_is_accepted_and_reported(self):
        for su, sv, text in ((1, 1, "half texel u +, v +"), (-1, -1, "half texel u -, v -"),
                             (1, -1, "half texel u +, v -"), (0, 0, "half texel u none, v none")):
            code, out = run(frame_rows(1, NEAR, FAR, su, sv))
            self.assertEqual(code, 0, out)
            self.assertEqual(out.count(text), 2, out)

    def test_scene_registers_of_another_matrix_fail(self):
        rows = frame_rows(1, NEAR, FAR)
        rows = rows[:9] + [scene_row(1, map_rows(light(tx=-6.6)), map_rows(FAR)) for _ in range(5)]
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertRegex(out, r"frame 1 pass 0 \(near\).*DISAGREES")
        self.assertIn("passes compared: 2, agreeing: 1", out)
        # Each of the four rows is compared: another depth row, or another w row, alone fails.
        for row, at in ((2, 3), (3, 3)):
            near = map_rows(NEAR)
            near[row][at] += 0.01
            rows = frame_rows(1, NEAR, FAR)[:9] + [scene_row(1, near, map_rows(FAR))]
            code, out = run(rows)
            self.assertEqual(code, 1, (row, out))
            self.assertRegex(out, r"frame 1 pass 0 \(near\).*DISAGREES")
        # The near pass against the far registers is not accepted either (the passes are not swapped).
        rows = frame_rows(1, NEAR, FAR)
        rows = rows[:9] + [scene_row(1, map_rows(FAR), map_rows(NEAR)) for _ in range(5)]
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn("passes compared: 2, agreeing: 0", out)

    def test_scene_rows_must_hold_one_value(self):
        rows = frame_rows(1, NEAR, FAR) + [scene_row(1, map_rows(light(tx=-6.0)), map_rows(FAR))]
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn("2 distinct values of c36..c43 in 6 scene rows", out)
        # Two values inside the tolerance of each other are still two values.
        rows = frame_rows(1, NEAR, FAR) + [scene_row(1, map_rows(light(tx=NEAR[0][3] + 1e-5)), map_rows(FAR))]
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn("the scene rows hold more than one value", out)
        self.assertNotIn("DISAGREES", out)
        # A row whose registers were not read is left out, not taken as zeros.
        rows = frame_rows(1, NEAR, FAR) + [scene_row(1, map_rows(light(tx=-6.0)), map_rows(FAR), unread=(37,))]
        code, out = run(rows)
        self.assertEqual(code, 0, out)
        self.assertIn("1 distinct value of c36..c43 in 5 scene rows (1 unread)", out)

    def test_pass_without_scene_registers_or_beyond_the_second_fails(self):
        code, out = run(frame_rows(1, NEAR, FAR)[:9])
        self.assertEqual(code, 1, out)
        self.assertIn("no scene row with c36..c43", out)
        rows = frame_rows(1, NEAR, FAR)
        rows[9:9] = [clear_row(1), shadow_row(1, NEAR, world(0.0, 1.0, 2.0, 3.0))]
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn("frame 1 pass 2 (extra): 1 rows", out)
        # No pass at all is not a proof.
        code, out = run([scene_row(1, map_rows(NEAR), map_rows(FAR))])
        self.assertEqual(code, 1, out)
        self.assertIn("passes compared: 0", out)

    def test_direct_form_and_rows_that_are_not_shadow_rows(self):
        rows = frame_rows(1, NEAR, FAR)
        junk = world(0.0, 5.0, 6.0, 7.0)
        rows.insert(2, shadow_row(1, NEAR, junk, vs=VS_DIRECT, direct=True))
        # Pitch 1040 with a colour mask, and another pitch with colour mask 0: neither is a shadow row.
        rows.insert(3, shadow_row(1, light(tx=5.0), junk, mask=0xF))
        rows.insert(4, shadow_row(1, light(tx=5.0), junk, surface="0x04020118"))
        code, out = run(rows, "--direct", VS_DIRECT)
        self.assertEqual(code, 0, out)
        self.assertIn("frame 1 pass 0 (near): 5 rows, 5 used", out)
        self.assertIn("1 direct", out)
        # Without the option the same row is read as world rows and named.
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        self.assertIn(f"vs {VS_DIRECT}: deviation", out)

    def test_origin_difference_between_frames_in_texels(self):
        # 3.25 texels in u and -2 in v: clip x moves by texels * 2 / 1024.
        moved = light(tx=NEAR[0][3] + 3.25 * 2 / 1024, ty=NEAR[1][3] - 2.0 * 2 / 1024)
        code, out = run(frame_rows(1, NEAR, FAR) + frame_rows(2, moved, FAR))
        self.assertEqual(code, 0, out)
        self.assertRegex(out, r"near, frame 1 -> 2: origin moves \(3\.250, -2\.000\) texels")
        self.assertRegex(out, r"far, frame 1 -> 2: origin moves \(0\.000, 0\.000\) texels")
        # The synthetic near map sits on whole texels in frame 1 and a quarter texel off in frame 2.
        self.assertRegex(out, r"near, frame 1: \(-3447\.00\d+, -5551\.00\d+\) texels, off a whole texel by "
                              r"\(-0\.00\d+, -0\.00\d+\)")
        self.assertRegex(out, r"near: largest distance of the origin from a whole texel \(0\.2\d+, 0\.00\d+\) over "
                              r"2 frames")
        # Frames that are not consecutive are not differenced.
        code, out = run(frame_rows(1, NEAR, FAR) + frame_rows(3, moved, FAR))
        self.assertNotIn("origin moves", out)

    def test_tolerance_is_absolute_and_tight(self):
        # A row off by 8e-3 in one element fails at the default 1e-3 and passes at 1e-2.
        rows = frame_rows(1, NEAR, FAR)
        rows[1]["bank"][3] += 8e-3  # the x translation of one of four rows; the median stays
        code, out = run(rows)
        self.assertEqual(code, 1, out)
        code, out = run(rows, "--tolerance", "1e-2")
        self.assertEqual(code, 0, out)


def iter_lines(rows):
    return (json.dumps(r, separators=(",", ":")) for r in rows)


if __name__ == "__main__":
    unittest.main()
