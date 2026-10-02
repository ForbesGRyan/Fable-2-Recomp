import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import albedo_finder as af  # noqa: E402
from test_shader_trace import PS, VS  # noqa: E402

PS_HASH = "0x00000000000000A1"
VS_HASH = "0x00000000000000B1"
FC = [2 | (1 << 22), 18 | (1 << 6) | 0x1A000000, 3 | (3 << 13), 0x688 << 1, 0, 1 << 9]


def hx(v):
    return f"0x{v:08X}"


def draw(in_scene=True, ps=PS_HASH):
    return {"kind": "draw", "frame": 1, "in_scene": in_scene, "ps": {"tex_slots": [0, 8, 2]},
            "ps_hash": ps, "vs_hash": VS_HASH, "tf": {"0": [hx(v) for v in FC]}}


class FinderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        dumps = self.dir / "dump"
        dumps.mkdir()
        (dumps / "shader_00000000000000A1.ucode.frag").write_text(PS)
        (dumps / "shader_00000000000000B1.ucode.vert").write_text(VS)
        tex = self.dir / "native_tex_x"
        tex.mkdir()
        block = struct.pack(">HHI", 0xF800, 0xF800, 0) + bytes(248)
        (tex / "1A000000_0123456789ABCDEF.bin").write_bytes(block)
        rows = [draw(), draw(), draw(in_scene=False),
                {"kind": "tess", "frame": 1, "in_scene": True, "ps_hash": "0x00000000000000C1"},
                {"kind": "texture", "file": "native_tex_x/1A000000_0123456789ABCDEF.bin",
                 "fc": [hx(v) for v in FC]}]
        self.cap = self.dir / "native_discovery_test.jsonl"
        self.cap.write_text("".join(json.dumps(r) + "\n" for r in rows))
        self.out = self.dir / "proposals.json"
        self.thumbs = self.dir / "thumbs"

    def run_finder(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            af.main([str(self.cap), "--dumps", str(self.dir / "dump"), "--out", str(self.out),
                     "--thumbs", str(self.thumbs)])
        return buf.getvalue(), json.loads(self.out.read_text())

    def test_proposals_and_coverage(self):
        text, data = self.run_finder()
        e = data["ps_albedo"][PS_HASH]
        self.assertEqual(e["slot"], 0)
        self.assertEqual(e["u"]["input"], "r0.x")
        self.assertEqual(e["v"]["input"], "r0.y")
        self.assertTrue(e["proposed"])
        self.assertIn("2 draws", e["evidence"])
        self.assertIn("native_discovery_test.jsonl", e["evidence"])
        vs = data["vs_transforms"][VS_HASH]
        self.assertTrue(vs["proposed"])
        self.assertEqual(sorted(vs["uv"]), ["o0.x", "o0.y"])
        self.assertEqual(vs["uv"]["o0.x"]["src"], "y")
        self.assertIn("covered 2 of 2 draws", text)

    def test_thumbnail_written(self):
        self.run_finder()
        png = self.thumbs / "00000000000000A1_tf0.png"
        self.assertEqual(png.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")

    def test_missing_dump_is_not_proposed(self):
        (self.dir / "dump" / "shader_00000000000000A1.ucode.frag").unlink()
        text, data = self.run_finder()
        self.assertEqual(data["ps_albedo"], {})
        self.assertIn("covered 0 of 2 draws", text)


if __name__ == "__main__":
    unittest.main()
