import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/xdk_sigmatch'))
from presentation_post_formulas import composite_156, composite_577
from ucode_eval import Machine, load


class OpeningPostProof(unittest.TestCase):
    @unittest.skipUnless((ROOT / 'out/shader_dump/shader_15675EB73FB39170.ucode.frag').exists(),
                         'local shader evidence unavailable')
    def test_opening_composites_and_sample_coordinates(self):
        rng = random.Random(14577)
        for family, formula in [('577CB5267A8C92C3', composite_577),
                                ('15675EB73FB39170', composite_156)]:
            shader = load(ROOT / 'out/shader_dump', family, 'frag')
            for trial in range(512):
                uv = [rng.random(), rng.random(), rng.uniform(-1, 1), rng.uniform(-1, 1)]
                # Both captured scene inputs are HDR 7e3, within 0..31.875.
                scene_max = [.1, 2.0, 31.875][trial % 3]
                textures = {slot: [rng.uniform(0, scene_max) for _ in range(3)] + [1]
                            for slot in range(6)}
                for slot in (2, 3, 5):
                    textures[slot] = [rng.random() * [.01, .2, 1][trial % 3] for _ in range(4)]
                c = {31: [rng.uniform(0, 300), 0, 0, 0],
                     46: [1 / 1120, 1 / 720, 0, 0],
                     47: [rng.uniform(-1, 1), rng.uniform(-1, 1),
                          rng.uniform(0, 5), rng.uniform(0, 5)],
                     51: [.45454545, 0, 0, 0],
                     69: [rng.uniform(-.2, .2), rng.uniform(.5, 2), 0, 0],
                     70: [rng.uniform(.1, 5), 2, 6, rng.uniform(.01, 2)],
                     71: [rng.random(), rng.random(), 0, 0],
                     77: [rng.uniform(0, 4), 0, 0, 0],
                     78: [rng.uniform(.01, 2), 0, 0, 0],
                     79: [rng.uniform(0, 50), 0, 0, 0]}
                if family.startswith('156'):
                    # Cover near, in-focus, far, and both distance-fade endpoints.
                    distance = [1, 2, 4, 6, 8, 2550, 2550.5, 2551][trial % 8]
                    c[18] = [1.00002, -.100002, 0, 0]
                    textures[1] = [1 - c[18][0] - c[18][1] / distance] * 4
                    c[254], c[255] = [1, 2550, 0, 0], [.0721, .2125, .7154, 0]
                    table_slot = 2
                else:
                    c[70] = [rng.uniform(.1, 1), 0, 0, 0]
                    c[254], c[255] = [.0721, .2125, .7154, 2], [1, 0, 0, 0]
                    table_slot = 1
                table = rng.uniform(.1, 4)
                calls = []
                def sample(slot, coords, attrs=None):
                    calls.append((slot, tuple(coords)))
                    return [table] * 4 if slot == table_slot else textures[slot]
                expected = Machine(shader, c, sample, regs={0: uv}).run()['oC0'][:3]
                reference_calls = list(calls)
                calls.clear()
                actual = formula(uv, c, sample)
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b, places=10, msg=f'{family} trial {trial}: {actual} != {expected}')
                # Order differs, but each producer and its lookup must agree.
                for slot, coords in calls:
                    matches = [at for s, at in reference_calls if s == slot]
                    self.assertEqual(len(matches), 1)
                    for a, b in zip(coords, matches[0]):
                        self.assertAlmostEqual(a, b, places=10,
                                               msg=f'{family} trial {trial} slot {slot}: {coords} != {matches[0]}')


if __name__ == '__main__':
    unittest.main()
