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
            "vb": {"phys_addr": 0x10002000 if ok else 0x10003000, "size": 2048, "fc_match": True}}


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
        row["vb"] = {"phys_addr": 0x10001F00, "offset": 0x100, "size": 2304, "fc_match": True}
        self.assertEqual(cd.check_rows([row], b), (1, 1))
        row["vb"]["offset"] = 0x80
        self.assertEqual(cd.check_rows([row], b), (1, 0))

    def test_object_fetch_constant_mismatch_fails(self):
        # vb.offset comes from the device shadow, so phys_addr + offset always
        # equals the logged fc; fc_match is the check of the object's dwords.
        b = cd.parse_vbind(LOG.splitlines())
        row = draw()
        row["vb"]["fc_match"] = True
        self.assertEqual(cd.check_rows([row], b), (1, 1))
        row["vb"]["fc_match"] = False
        self.assertEqual(cd.check_rows([row], b), (1, 0))

    def test_missing_fc_match_is_not_a_match(self):
        # Rows without the field (older captures, a decode that stopped early)
        # are not evidence of a correct object layout.
        b = cd.parse_vbind(LOG.splitlines())
        row = draw()
        del row["vb"]["fc_match"]
        self.assertEqual(cd.check_rows([row], b), (1, 0))

    def test_binding_without_fc_line_is_not_a_match(self):
        # Without its fc line a logged binding has no address to compare.
        b = cd.parse_vbind(LOG.splitlines()[:2])
        self.assertIsNone(b["0x00000000000000AB"][0]["fc"])
        self.assertEqual(cd.check_rows([draw()], b), (1, 0))

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

    def test_index_files_exist_with_their_bytes(self):
        # Foliage discovery (frame-map section 18): a row's "ib" names a file of "file_bytes" bytes.
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "native_geo_x").mkdir()
            (d / "native_geo_x" / "a.bin").write_bytes(bytes(12))
            row = draw()
            row["ib"] = {"size": 12, "index32": False, "start": 0, "count": 6,
                         "file": "native_geo_x/a.bin", "file_offset": 0, "file_bytes": 12}
            old = draw()
            old["ib"] = {"size": 12, "index32": False, "start": 0, "count": 6}   # no file: not counted
            self.assertEqual(cd.check_index_files([row, old, draw()], d), (1, []))
            short = json.loads(json.dumps(row))
            short["ib"]["file_bytes"] = 16                                         # the file is shorter
            missing = json.loads(json.dumps(row))
            missing["ib"]["file"] = "native_geo_x/b.bin"
            outside = json.loads(json.dumps(row))                                  # the draw's range is not in the file
            (d / "native_geo_x" / "c.bin").write_bytes(bytes(8))
            outside["ib"].update(file="native_geo_x/c.bin", file_offset=4, file_bytes=8, start=0, count=6)
            past = json.loads(json.dumps(row))                                     # the file ends past the buffer
            past["ib"].update(size=8)
            named, problems = cd.check_index_files([row, short, missing, outside, past], d)
            self.assertEqual(named, 5)
            self.assertEqual(len(problems), 4)
            self.assertIn("16 bytes", problems[0])
            self.assertIn("missing", problems[1])
            self.assertIn("range", problems[2])
            self.assertIn("buffer", problems[3])

    def test_index_range_inside_a_part_file(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "g").mkdir()
            (d / "g" / "p.bin").write_bytes(bytes(2400))
            row = draw()
            row["ib"] = {"size": 4 << 20, "index32": True, "start": 100, "count": 600,
                         "file": "g/p.bin", "file_offset": 400, "file_bytes": 2400}
            self.assertEqual(cd.check_index_files([row], d), (1, []))

    def test_scene_frame_rises_by_frame(self):
        frames = [{"kind": "frame", "frame": i + 1, "scene_frame": 900 + i} for i in range(3)]
        self.assertEqual(cd.check_scene_frames(frames), (3, []))
        self.assertEqual(cd.check_scene_frames([{"kind": "frame", "frame": 1}]), (0, []))   # an older capture
        frames[2]["scene_frame"] = 901                                                         # the same scene twice
        seen, problems = cd.check_scene_frames(frames)
        self.assertEqual(seen, 3)
        self.assertEqual(len(problems), 1)
        self.assertIn("frame 3", problems[0])
        frames[2]["scene_frame"] = 0                                                           # no scene number
        self.assertEqual(len(cd.check_scene_frames(frames)[1]), 1)
        # A frame that published no scene (no records: the view was not a native one) has no number
        # and is no problem; the numbers of the others must still rise.
        del frames[1]["scene_frame"]
        self.assertEqual(len(cd.check_scene_frames(frames)[1]), 1)
        frames[2]["scene_frame"] = 902
        self.assertEqual(cd.check_scene_frames(frames), (2, []))

    def test_verts_rows_have_their_data(self):
        verts = {"kind": "verts", "frame": 1, "n": 7, "in_scene": True, "count": 8, "storage_dwords": 8,
                 "storage": "0xFF762700",
                 "bank": [0.0] * 1024, "vfetch": {"shadow": ["0x00000000"] * 6, "packet": ["0x00000001", "0x00000000"] * 2 + ["0x1F763703", "0x10000022"]}}
        data = {"kind": "vertsdata", "frame": 1, "n": 7, "want": 32, "read": True, "bytes": "00" * 32}
        self.assertEqual(cd.check_verts_data([verts, data]), (1, []))
        short = dict(data, bytes="00" * 31)
        self.assertEqual(len(cd.check_verts_data([verts, short])[1]), 1)       # fewer bytes than "want"
        self.assertEqual(len(cd.check_verts_data([verts])[1]), 1)              # no data row
        self.assertEqual(len(cd.check_verts_data([verts, data, data])[1]), 1)  # two data rows
        self.assertEqual(len(cd.check_verts_data([verts, dict(data, want=16, bytes="00" * 16)])[1]), 1)   # not storage_dwords * 4
        self.assertEqual(len(cd.check_verts_data([dict(verts, bank=[0.0] * 4), data])[1]), 1)             # the bank is not whole
        unread = {"kind": "vertsdata", "frame": 1, "n": 7, "want": 32, "read": False}
        self.assertEqual(cd.check_verts_data([verts, unread]), (1, ["frame 1 verts 7: the storage was not read"]))
        outside = dict(verts, in_scene=False)
        outside.pop("n")
        self.assertEqual(cd.check_verts_data([outside]), (0, []))             # a row outside the scene has neither

    def test_verts_data_more(self):
        verts = {"kind": "verts", "frame": 1, "n": 7, "in_scene": True, "count": 8, "storage_dwords": 8,
                 "storage": "0xFF762700",
                 "bank": [0.0] * 1024, "vfetch": {"shadow": ["0x00000000"] * 6, "packet": ["0x00000001", "0x00000000"] * 2 + ["0x1F763703", "0x10000022"]}}
        data = {"kind": "vertsdata", "frame": 1, "n": 7, "want": 32, "read": True, "bytes": "00" * 32}
        # A capture from before the storage was kept: no in-scene row has a number and no data row exists.
        old = dict(verts)
        old.pop("n")
        self.assertEqual(cd.check_verts_data([old, old]), (0, []))
        # But a numbered row beside an unnumbered one, or a data row no "verts" row owns, is a problem.
        self.assertEqual(len(cd.check_verts_data([old, verts, data])[1]), 1)
        self.assertEqual(len(cd.check_verts_data([verts, data, dict(data, n=8)])[1]), 1)
        # The same number in another frame is another row.
        self.assertEqual(len(cd.check_verts_data([verts, dict(data, frame=2)])[1]), 2)
        # The builder's packet must name the storage (review of phase 12a, Minor 1c): the shadow is not asked for.
        self.assertEqual(cd.check_verts_data([dict(verts, vfetch={"shadow": None, "packet": verts["vfetch"]["packet"]}), data]), (1, []))
        for packet in (["0x00000001", "0x00000000"] * 2 + ["0x1F764703", "0x10000022"],        # another address
                       ["0x00000001", "0x00000000"] * 2 + ["0x1F763702", "0x10000022"],        # not a vertex buffer
                       ["0x00000001", "0x00000000"] * 2 + ["0x1F763703", "0x10000032"],        # another size
                       ["0x1F763703", "0x10000022"]):                                          # not six dwords
            self.assertEqual(cd.check_verts_data([dict(verts, vfetch={"shadow": ["0x00000000"] * 6, "packet": packet}), data])[1],
                             ["frame 1 verts 7: the builder's packet does not name the storage"])
        self.assertTrue(cd.packet_names_storage(dict(verts, storage="0x4A120000", vfetch={"packet": ["0x0"] * 4 + ["0x0A120003", "0x00000020"]})))
        # A data row says in which frame it was read: after its own frame's end is a problem; an older row has no "at".
        self.assertEqual(cd.check_verts_data([verts, dict(data, at=1)]), (1, []))
        self.assertEqual(cd.check_verts_data([verts, dict(data, at=2)])[1], ["frame 1 verts 7: the storage was read in frame 2: after its frame's end"])
        # No fetch constant; a packet that is none; bytes that are no hex.
        self.assertEqual(len(cd.check_verts_data([{k: v for k, v in verts.items() if k != "vfetch"}, data])[1]), 1)
        self.assertEqual(len(cd.check_verts_data([dict(verts, vfetch={"shadow": None, "packet": None}), data])[1]), 1)
        self.assertEqual(len(cd.check_verts_data([verts, dict(data, bytes="0G" * 32)])[1]), 1)
        # A storage past the cap has no data row and is said so; an empty one has none and is no problem.
        big = dict(verts, storage_dwords=16385, vfetch={"packet": ["0x0"] * 4 + ["0x1F763703", "0x10010006"]})
        self.assertEqual(cd.check_verts_data([big]), (1, ["frame 1 verts 7: the storage is 65540 bytes, over the cap: not kept"]))
        self.assertEqual(cd.check_verts_data([dict(verts, storage_dwords=0)]), (1, []))
        # A row without the device's storage fields has no data row and is said so.
        self.assertEqual(len(cd.check_verts_data([{k: v for k, v in verts.items() if k != "storage_dwords"}])[1]), 1)

    def test_cli_fails_on_a_missing_verts_data_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "g.log").write_text(LOG)
            rows = [draw() for _ in range(20)]
            rows.append({"kind": "verts", "frame": 1, "n": 1, "in_scene": True, "count": 8, "storage_dwords": 8,
                         "storage": "0xFF762700", "bank": [0.0] * 1024,
                         "vfetch": {"shadow": ["0x00000000"] * 6, "packet": ["0x00000001", "0x00000000"] * 2 + ["0x1F763703", "0x10000022"]}})
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 1)
            rows.append({"kind": "vertsdata", "frame": 1, "n": 1, "want": 32, "read": True, "bytes": "00" * 32})
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 0)
            # Review of phase 12a, Minor 1: a run without [vbind] lines. The default still fails (no draw was
            # checked: the bound of 20 is not relaxed); --rows-only runs the capture's own checks and says so.
            (d / "empty.log").write_text("nothing logged\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "empty.log")]), 1)
            self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl")]), 0)
            self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl"), str(d / "empty.log")]), 0)
            rows.pop()                                                   # the storage's row gone: a structural problem
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl")]), 1)

    def test_cli_fails_on_a_missing_index_file(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "g.log").write_text(LOG)
            rows = [draw() for _ in range(20)]
            rows[0]["ib"] = {"size": 12, "index32": False, "start": 0, "count": 6,
                             "file": "native_geo_x/a.bin", "file_offset": 0, "file_bytes": 12}
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 1)
            (d / "native_geo_x").mkdir()
            (d / "native_geo_x" / "a.bin").write_bytes(bytes(12))
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 0)

    # Water discovery (frame-map section 20): the rows of the lake's draws name the reflection pictures they
    # sampled by content, and the rows of the reflection views (outside the scene, surface pitch 280) carry "vp".
    def _water(self):
        tf = {"1": ["0x00000002"] * 6, "2": ["0x00000002"] * 6,
              "13": ["0x82000002", "0x13401086", "0x001FE0FF", "0x00000000", "0x00000000", "0x00000200"],
              "14": ["0x82000002", "0x13441086", "0x001FE0FF", "0x00000000", "0x00000000", "0x00000200"]}
        draw = {"kind": "draw", "frame": 1, "in_scene": True, "vs_hash": "0xBEAD84BD72072E0E", "ps_hash": "0xA17D8AEC3A817D45",
                "tf": tf, "tfc": {"13": "0x00000000000000AA", "14": "0x00000000000000BB"}}
        tex13 = {"kind": "texture", "frame": 1, "content": "0x00000000000000AA", "bytes": 262144, "fc": tf["13"]}
        tex14 = {"kind": "texture", "frame": 1, "content": "0x00000000000000BB", "bytes": 262144, "fc": tf["14"]}
        view = {"kind": "draw", "frame": 1, "in_scene": False, "surface_info": "0x04020118", "vp": {"210F": "0x3F800000"}}
        return draw, tex13, tex14, view

    def test_water_rows_name_their_pictures(self):
        draw, tex13, tex14, view = self._water()
        self.assertEqual(cd.check_water_rows([tex13, tex14, view, draw]), (2, []))        # one water row, one view row
        self.assertEqual(len(cd.check_water_rows([tex14, view, draw])[1]), 1)              # slot 13's content was never written
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, view, dict(draw, tfc={})])[1]), 2)   # the row names no content
        other = dict(tex13, fc=tex14["fc"])
        self.assertEqual(len(cd.check_water_rows([other, tex14, view, draw])[1]), 1)       # the content belongs to another base
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, {k: v for k, v in view.items() if k != "vp"}, draw])[1]), 1)
        scene = dict(view, in_scene=True)
        scene.pop("vp")
        self.assertEqual(cd.check_water_rows([tex13, tex14, scene, draw]), (1, []))        # a row of the scene has no "vp"
        frame2 = dict(draw, frame=2)
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, view, draw, frame2])[1]), 0)   # the same content in frame 2: written once
        self.assertEqual(cd.check_water_rows([]), (0, []))

    def test_water_rows_more(self):
        draw, tex13, tex14, view = self._water()
        # a content's row must stand BEFORE the row that names it (the capture writes it so)
        self.assertEqual(len(cd.check_water_rows([tex14, view, draw, tex13])[1]), 1)
        # the views' rows of the other two kinds; a row at another pitch is none of the check's
        for kind in ("indx", "tess"):
            self.assertEqual(cd.check_water_rows([dict(view, kind=kind)]), (1, []))
            self.assertEqual(len(cd.check_water_rows([{k: v for k, v in dict(view, kind=kind).items() if k != "vp"}])[1]), 1)
        self.assertEqual(cd.check_water_rows([dict(view, surface_info="0x00000140")]), (0, []))
        self.assertEqual(cd.check_water_rows([{"kind": "verts", "in_scene": False, "surface_info": "0x04020118"}]), (0, []))
        # the shader named as the other rows name it ("vs": {"hash"}); another shader's row is not checked
        nested = {k: v for k, v in draw.items() if k != "vs_hash"}
        nested["vs"] = {"hash": "0xBEAD84BD72072E0E"}
        self.assertEqual(cd.check_water_rows([tex13, tex14, dict(nested, tfc={})])[0], 1)
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, dict(nested, tfc={})])[1]), 2)
        self.assertEqual(cd.check_water_rows([dict(draw, vs_hash="0x00000000000000AB", tfc={})]), (0, []))
        # a slot that holds no reflection picture (the 128 x 128 pass at the same base) asks for nothing
        small = dict(draw, tf=dict(draw["tf"], **{"13": ["0x81000002", "0x13401086", "0x000FE07F", "0x0", "0x0", "0x00000200"]}),
                     tfc={"14": "0x00000000000000BB"})
        self.assertEqual(cd.check_water_rows([tex14, small]), (1, []))
        # the normal map (slot 2, a 2D k_DXN texture with a mip address): its texture row names its stored levels
        nm = ["0x82000002", "0x14D40071", "0x001FE0FF", "0x00A80D10", "0x00000203", "0x14D50A18"]
        with_nm = dict(draw, tf=dict(draw["tf"], **{"2": nm}))
        tex_nm = {"kind": "texture", "bytes": 65536, "fc": nm, "mips": {"file": "x_mips.bin", "bytes": 4, "levels": [{"level": 1}]}}
        self.assertEqual(cd.check_water_rows([tex13, tex14, tex_nm, with_nm]), (1, []))
        bare = {k: v for k, v in tex_nm.items() if k != "mips"}
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, bare, with_nm])[1]), 1)   # dumped without its levels
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, with_nm])[1]), 1)         # not dumped at all
        self.assertEqual(len(cd.check_water_rows([tex13, tex14, bare, with_nm, dict(with_nm, frame=2)])[1]), 1)   # said once a base

    def test_cli_prints_and_fails_on_water_problems(self):
        import contextlib
        import io
        draw, tex13, tex14, view = self._water()
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in [tex13, tex14, view, draw]) + "\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl")]), 0)
            self.assertIn("water rows: 2 checked, 0 problems", out.getvalue())
            (d / "x.jsonl").write_text("\n".join(json.dumps(r) for r in [tex14, view, draw]) + "\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl")]), 1)
            self.assertIn("water rows: 2 checked, 1 problems", out.getvalue())
            # a capture without a water row or a view's row prints no water line
            (d / "x.jsonl").write_text(json.dumps({"kind": "frame", "frame": 1}) + "\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(cd.main(["--rows-only", str(d / "x.jsonl")]), 0)
            self.assertNotIn("water rows", out.getvalue())


if __name__ == "__main__":
    unittest.main()
