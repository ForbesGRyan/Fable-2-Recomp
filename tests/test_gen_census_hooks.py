import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen", ROOT / "tools" / "xdk_sigmatch" / "gen_census_hooks.py")
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)


class GenTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.map = d / "map.json"
        self.map.write_text(json.dumps([
            {"name": "D3DDevice_DrawIndexedVertices", "ref": 1, "target": 0x82BA0000, "confidence": "loose", "candidates": []},
            {"name": "D3DDevice_Swap", "ref": 2, "target": 0x82B9CD68, "confidence": "loose", "candidates": []},
            {"name": "Gone", "ref": 3, "target": None, "confidence": "none", "candidates": []},
            {"name": "D3DDevice_DrawVertices", "ref": 4, "target": None, "confidence": "none", "candidates": [0x82BA1000], "score": 0.969},
            {"name": "D3DDevice_Swap2", "ref": 5, "target": None, "confidence": "none", "candidates": [0x82BA2000], "score": 0.311},
        ]))
        self.init = d / "init.cpp"
        self.init.write_text("\t{ 0x82BA0000, sub_82BA0000 },\n\t{ 0x82B9CD68, MainRenderLoop_82B9CD68 },\n\t{ 0x82BA1000, sub_82BA1000 },\n\t{ 0x82BA2000, sub_82BA2000 },\n")
        self.src = d / "src"
        (self.src / "diagnostics").mkdir(parents=True)
        (self.src / "diagnostics" / "fps_meter.h").write_text("__imp__MainRenderLoop_82B9CD68(ctx, base);\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_emits_hook_for_mapped_function(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertIn('FABLE2_D3D_CENSUS_HOOK(0, "D3DDevice_DrawIndexedVertices", sub_82BA0000)', out)

    def test_skips_existing_override(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertNotIn("FABLE2_D3D_CENSUS_HOOK(1", out)
        self.assertIn("// skipped D3DDevice_Swap (MainRenderLoop_82B9CD68): already overridden in", out)

    def test_unmapped_omitted(self):
        self.assertNotIn("Gone", gen.generate(self.map, self.init, self.src))

    def test_near_miss_high_score_emitted(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertIn('FABLE2_D3D_CENSUS_HOOK(3, "D3DDevice_DrawVertices?", sub_82BA1000)', out)

    def test_near_miss_low_score_not_emitted(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertNotIn("Swap2", out)
        self.assertNotIn("sub_82BA2000", out)


if __name__ == "__main__":
    unittest.main()
