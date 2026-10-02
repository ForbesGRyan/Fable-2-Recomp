import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cd", ROOT / "tools" / "xdk_sigmatch" / "check_discovery.py")
cd = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cd)

LOG = """[2026-10-01 10:00:00.000] [info] [gpu] [vbind] vs=0x00000000000000AB dwords=120
[2026-10-01 10:00:00.000] [info] [gpu] [vbind]   fetch=1 stride_dw=8 attrs=0:57:1:1,3:6:0:1
[2026-10-01 10:00:00.000] [info] [gpu] [vbind]   fc=0x10002003 0x00000802
"""


def draw(ok=True):
    return {"kind": "draw", "vs_hash": "0x00000000000000AB",
            "pos": {"fetch_slot": 1, "offset_bytes": 0, "stride_bytes": 32, "format": 57},
            "vb": {"phys_addr": 0x10002000 if ok else 0x10003000, "size": 2048}}


class CheckDiscoveryTests(unittest.TestCase):
    def test_parse_vbind(self):
        b = cd.parse_vbind(LOG.splitlines())
        self.assertEqual(b["0x00000000000000AB"][0]["stride_dw"], 8)
        self.assertEqual(b["0x00000000000000AB"][0]["attrs"][0], (0, 57, 1, 1))
        self.assertEqual(b["0x00000000000000AB"][0]["fc"], (0x10002003, 0x00000802))

    def test_matches(self):
        b = cd.parse_vbind(LOG.splitlines())
        self.assertEqual(cd.check_rows([draw()] * 20, b), (20, 20))
        self.assertEqual(cd.check_rows([draw(), draw(ok=False)], b), (2, 1))

    def test_stream_offset_is_part_of_the_fetch_address(self):
        # The XDK writes base + SetStreamSource offset into the fetch constant
        # (frame-map section 8), so a row's base plus its offset must match.
        b = cd.parse_vbind(LOG.splitlines())
        row = draw()
        row["vb"] = {"phys_addr": 0x10001F00, "offset": 0x100, "size": 2304}
        self.assertEqual(cd.check_rows([row], b), (1, 1))
        row["vb"]["offset"] = 0x80
        self.assertEqual(cd.check_rows([row], b), (1, 0))

    def test_repeated_shader_blocks_accumulate_bindings(self):
        # The SDK logs one block per distinct (shader, fetch constants) pair.
        log = LOG + LOG.replace("0x10002003", "0x10004003")
        b = cd.parse_vbind(log.splitlines())
        self.assertEqual(len(b["0x00000000000000AB"]), 2)
        row = draw()
        row["vb"]["phys_addr"] = 0x10004000
        self.assertEqual(cd.check_rows([row], b), (1, 1))

    def test_cli_exit_code(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "g.log").write_text(LOG)
            (d / "x.jsonl").write_text("\n".join(json.dumps(draw()) for _ in range(20)) + "\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 0)
            (d / "y.jsonl").write_text("\n".join(json.dumps(draw()) for _ in range(5)) + "\n")
            self.assertEqual(cd.main([str(d / "y.jsonl"), str(d / "g.log")]), 1)

    def test_cli_skips_a_truncated_last_line(self):
        # A capture cut off by closing the game ends in a partial row.
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "g.log").write_text(LOG)
            good = "\n".join(json.dumps(draw()) for _ in range(20)) + "\n"
            (d / "x.jsonl").write_text(good + json.dumps(draw())[:40])
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 0)
            (d / "y.jsonl").write_text(json.dumps(draw())[:40] + "\n" + good)
            with self.assertRaises(ValueError):
                cd.main([str(d / "y.jsonl"), str(d / "g.log")])


if __name__ == "__main__":
    unittest.main()
