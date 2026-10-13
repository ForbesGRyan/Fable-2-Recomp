# Native Local Lights (lanterns and torches) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The native view lights the ground, props, trunks, plants and leaves near a lantern or torch with the game's own local lights, proven offline per light slot and in the game at the Bower Lake bridge.

**Architecture:** The capture reads the game's light set (pixel `c80..c115`) once per upload at draws of the multi-light families into a side array of the frame (`FrameScene::light_sets`); each such draw's `LightInputs` names its set. The pass copies the frame's lanterns into a tail of the frame constant buffer (cbuffer `Frame`, after the fog tables: no binding parameter is free, the root signature is at 12 of 12 parameters and 56 of 64 words) and packs each draw's lantern range into free bits of the draw's `light_mode` word. The lit pixel paths add `LocalLights(P, N, ...)`, the game's per-light form checked against the microcode for every slot by `light_check.py --multi-lanterns`; the leaf vertex path sums the lights as the game's leaf shaders do.

**Tech Stack:** C++23 (clang), HLSL SM5 through `d3dcompiler`, Python 3 (`unittest`), the project's own microcode evaluator (`tools/xdk_sigmatch/ucode_eval.py`), PowerShell driver `tools/drive_game.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-13-native-renderer-local-lights-design.md`

## Global Constraints

- Work in the worktree `C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\lanterns` (branch `native-renderer-lanterns`); never touch the main checkout, its `out\build`, or the shared `fable_2.exe`. Plain `git` (no `rtk git`) from the worktree, one git call at a time.
- Native tests: `& "$PWD\tests\run_native_tests.cmd" C:\Users\Ryan\code\Fable-2-Recomp\thirdparty\rexglue-sdk` from PowerShell with `C:\Program Files\LLVM\bin` on PATH. Python tests: `python -m unittest tests.test_light_check tests.test_ucode_eval -v` from the worktree root (the checker needs `out\shader_dump`: Task 0 links it).
- Every number that reaches a shader is checked (`LightValueUsable`: magnitude at most 1,024; `LightPositionUsable` for positions) and a block that fails is refused as a whole, counted by reason; nothing is lit from a guessed value.
- The HLSL functions carry `light_formulas.py`'s names and argument lists statement for statement; the C++ mirrors are pinned to numbers the Python functions compute (`tests/test_light_check.py` `PinnedForTheRenderer`, `tests/native/test_clay_logic.cpp`).
- `tests/native/test_native_shaders.cpp` pins the clay HLSL text verbatim (`kPinLight`, `kPinVsMain`, `kPinTexturedMain`, `kPinLayerMain`): every shader edit updates the matching pin on purpose, and the light-mode loops (`kLightModeCount == 128`) stay as they are (the new bits lie above bit 7 and are tested by their own loop).
- Lanterns are the slots before the sun's slot (`c112` / `c113` one-hot); a set whose sun is not the frame's (direction bit-equal to `ClayFrameConstants::sun_dir`) is drawn without lanterns, counted. No lanterns in reflection draws. The lantern vector uses the vertex at rest. No specular, no per-light shadow map, no `b135`.
- Switch `fable2_native_local_lights`, words `on | off` (`ParseClayLight`), default `on`; `off` reads, makes and draws nothing of this plan.
- Commit after every task with a one-line subject in the repository's style and the trailer `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

## Review Focus

1. A light set whose `c112` names a slot but `c94` is 1 there or w is 1 (a lantern where the sun should be): `MakeLightSet` refuses (`kNoSun`), the draw is lit by the sun alone. Test in Task 3.
2. A frame with more distinct sets than the table holds (16) or more lanterns than the buffer (64): the extra sets are refused and counted (`kTooMany`), never truncated silently into a wrong index. Tests in Tasks 4 and 6.
3. A lantern slot read with a NaN colour, a position beyond 1,024 units or a non-finite range: the whole set is refused (`kUnusable`), no lantern of it is drawn. Test in Task 3.
4. A set read in frame N under upload U must not be used by a draw of frame N + 1 whose thread cache still holds U: the per-thread cache is keyed by the frame number and `AddLightSet` runs under the scene mutex per record. Test in Task 4 (pure cache) and the capture's existing frame recheck.
5. A draw whose `light_mode` has the lantern bit but `lantern_count` 0, or a `lantern_first + count` beyond 64: the shader loop runs zero times / the pass never emits it (`LightModeLanterns` clamps and returns 0 for an empty range). Tests in Task 6 and the shader loop test in Task 7.

---

### Task 0: Worktree setup

**Files:**
- Create (junction, untracked): `out/shader_dump` -> `C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump`
- Create: `out/native-evidence/lighting/lanterns/` (evidence folder; `out/` is git-ignored)

- [ ] **Step 1: Link the shader dumps and make the evidence folder**

PowerShell, from the worktree:
```powershell
New-Item -ItemType Directory -Force out | Out-Null
cmd /c mklink /J out\shader_dump C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump
New-Item -ItemType Directory -Force out\native-evidence\lighting\lanterns | Out-Null
(Get-ChildItem out\shader_dump | Measure-Object).Count   # expect 609
```

- [ ] **Step 2: Run the two suites once to know the baseline**

```powershell
$env:PATH = "C:\Program Files\LLVM\bin;" + $env:PATH
python -m unittest tests.test_light_check -v 2>&1 | Select-Object -Last 5
& "$PWD\tests\run_native_tests.cmd" C:\Users\Ryan\code\Fable-2-Recomp\thirdparty\rexglue-sdk 2>&1 | Select-Object -Last 5
```
Expected: both pass (`OK` / exit 0). Record the Python run's count of tests in the task notes. No commit (nothing tracked changed).

---

### Task 1: The local light formulas (Python)

**Files:**
- Modify: `tools/xdk_sigmatch/light_formulas.py` (after `sun_gate_multi`, line 181; `terrain_layer` 212, `plant` 258, `tree_leaf` 294, `lit_object` 313)
- Test: `tests/test_light_check.py` (a new class after `PinnedForTheRenderer`)

**Interfaces:**
- Produces: `local_light(P, N, pos, w, col, blend, range_scale, axis, cone_cos, cone_scale, shadow_strength, mask=1.0, facing=1.0) -> [r, g, b]`; `local_lights(P, N, lanterns, mask=1.0, facing=1.0) -> [r, g, b]` where `lanterns` is a list of dicts with keys `pos, w, col, blend, range_scale, axis, cone_cos, cone_scale, shadow`; `terrain_layer(..., sun_gate, lights=None)`, `lit_object(..., c49, lights=None)`, `plant(..., sun_gate=1.0, lights=None)` add `lights` (three channels) to their sun term; `tree_leaf_sums(A, vs_ambient, front3, back3, c27w=1.0, *, m1, m2, c49)` with `tree_leaf` calling it.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_light_check.py`:
```python
class LocalLights(unittest.TestCase):
    """The per-light form of the two-, four- and seven-light shaders (lighting-formulas.md, 'The sun',
    the multi form), as one function of one light, and the families' sum."""
    L = dict(pos=[3.0, 4.0, 0.0], w=1.0, col=[1.5, 0.75, 0.1], blend=1.0, range_scale=0.1,
             axis=[0.0, 0.0, 1.0], cone_cos=-1.0, cone_scale=1.0, shadow=0.0)

    def test_a_lantern_without_cone_is_colour_times_n_dot_times_range(self):
        P, N = [0.0, 0.0, 0.0], [0.6, 0.8, 0.0]
        got = lf.local_light(P, N, **self.L)
        # V = (3, 4, 0), |V| = 5, l = (0.6, 0.8, 0): n_dot 1, range 0.5, cone sat(0 + 1) = 1, gate 0.5
        self.assertEqual(got, [0.75, 0.375, 0.05])

    def test_a_direction_light_with_w_zero_ignores_the_position(self):
        L = dict(self.L, pos=[0.0, 0.0, 1.0], w=0.0, blend=0.0)
        got = lf.local_light([100.0, 100.0, 100.0], [0.0, 0.0, 1.0], **L)
        self.assertEqual(got, [1.5, 0.75, 0.1])

    def test_the_shadow_strength_takes_the_mask(self):
        L = dict(self.L, shadow=1.0, blend=0.0)
        got = lf.local_light([0.0, 0.0, 0.0], [0.6, 0.8, 0.0], mask=0.25, facing=1.0, **L)
        self.assertEqual(got, [1.5 * 0.25, 0.75 * 0.25, 0.1 * 0.25])

    def test_the_sum_and_the_families_add_it_to_the_sun_term(self):
        P, N = [0.0, 0.0, 0.0], [0.6, 0.8, 0.0]
        one = lf.local_light(P, N, **self.L)
        two = lf.local_lights(P, N, [self.L, self.L])
        self.close(two, [2 * x for x in one])
        A, baked = [0.5, 0.5, 0.5], [0.2, 0.3, 0.4]
        base = lf.terrain_layer(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, sun_gate=1.0)
        lit = lf.terrain_layer(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, sun_gate=1.0, lights=two)
        self.close(lit, [b + A[i] * two[i] for i, b in enumerate(base)])
        base = lf.lit_object(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, c49=[2.0] * 3)
        lit = lf.lit_object(A, baked, [1.0] * 3, 1.0, [0.0] * 3, [0.0] * 3, 1.0, 0.0, [1.0] * 3, c49=[2.0] * 3, lights=two)
        self.close(lit, [b + A[i] * two[i] * 2.0 for i, b in enumerate(base)])
        base = lf.plant(A, baked, 0.0, [1.0] * 3)
        lit = lf.plant(A, baked, 0.0, [1.0] * 3, lights=two)
        self.close(lit, [b + A[i] * two[i] for i, b in enumerate(base)])

    def test_tree_leaf_is_its_colour_sum_form(self):
        A, amb, sun = [0.25, 0.5, 0.25], [0.5, 0.5, 0.5], [2.0, 2.0, 2.0]
        want = lf.tree_leaf(A, amb, 0.5, 0.25, sun, 0.5, m1=0.75, m2=0.875, c49=[1.0, 0.5, 2.0])
        got = lf.tree_leaf_sums(A, amb, [s * 0.5 for s in sun], [s * 0.25 for s in sun], 0.5, m1=0.75, m2=0.875, c49=[1.0, 0.5, 2.0])
        self.close(got, want)

    def test_finite_for_any_input(self):
        nan, inf = float("nan"), float("inf")
        L = dict(self.L, pos=[nan, inf, 0.0], col=[inf, nan, 1.0], range_scale=nan, cone_scale=inf, shadow=nan)
        for x in lf.local_light([inf, 0.0, 0.0], [nan, 0.0, 0.0], **L):
            self.assertTrue(math.isfinite(x))

    def close(self, got, want):
        self.assertEqual(len(got), len(want))
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, places=12)
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest tests.test_light_check.LocalLights -v`
Expected: FAIL / ERROR with `module 'light_formulas' has no attribute 'local_light'`.

- [ ] **Step 3: Implement the formulas**

In `light_formulas.py` after `sun_gate_multi`:
```python
def local_light(P, N, pos, w, col, blend, range_scale, axis, cone_cos, cone_scale, shadow, mask=1.0, facing=1.0):
    """One light of the two-, four- and seven-light shaders, slot j, as the sun's slot is read (sun_gate_multi):
        V = c(80+j).xyz - P * c(80+j).w,  n_dot = sat(N . normalize(V)),
        shadow_j = (1 - facing * mask) * c112[j],  range = sat(1 - |V| * c96[j]),
        cone = sat((normalize(V) . c(98+j).xyz - c105[j]) * c107[j]),
        gate = (1 - shadow_j) * (1 + c94[j] * (cone * range - 1)),
        term = c(87+j).xyz * n_dot * gate.
    Slots 4 to 6 read c95, c97, c106, c108, c113 in place of c94, c96, c105, c107, c112. P the world
    position, N the unit normal. Finite for any input: every term is passed through _finite."""
    v = [_finite(pos[i]) - _finite(P[i]) * _finite(w) for i in range(3)]
    d = math.sqrt(dot(v, v)) if all(math.isfinite(x) for x in v) else 0.0
    l = normalize(v)
    n_dot = sat(dot(N, l))
    gate = sun_gate_multi(shadow, mask=mask, facing=facing, spot_blend=blend, distance=d, range_scale=range_scale,
                          cone_dot=dot(l, axis), cone_cos=cone_cos, cone_scale=cone_scale)
    return [_finite(_finite(c) * n_dot * gate) for c in col]


def local_lights(P, N, lanterns, mask=1.0, facing=1.0):
    """The sum of local_light over `lanterns` (dicts with local_light's keyword arguments but P, N, mask, facing)."""
    out = [0.0, 0.0, 0.0]
    for L in lanterns:
        term = local_light(P, N, mask=mask, facing=facing, **L)
        out = [_finite(out[i] + term[i]) for i in range(3)]
    return out
```
`normalize` and `dot` must give finite results for non-finite input: check `normalize` (line 88) returns zeros for a vector with a NaN or infinite length; if it does not, guard it there (`if not all(math.isfinite(x) for x in v): return [0.0, 0.0, 0.0]`).

Add `lights=None` to `terrain_layer`, `lit_object` and `plant`: in each, after the sun term is formed, `extra = [_finite(x) for x in lights] if lights is not None else [0.0, 0.0, 0.0]` and add `A[i] * extra[i]` where the sun term is added (`lit_object`: inside the `* c49` product beside `sun[i]`; `terrain_layer`: beside the sun part; `plant`: beside the sun part). Update each docstring with one line: "lights: the sum of the local lights (local_lights), added to the sun term".

Add `tree_leaf_sums` and make `tree_leaf` call it:
```python
def tree_leaf_sums(A, vs_ambient, front3, back3, c27w=1.0, *, m1, m2, c49):
    """tree_leaf with the interpolators as the leaf vertex shaders export them: o2 = front3 = sum over the
    lights of colour * front, o3 = back3 = the same of back (one light: sun_col * front, sun_col * back).
        (A * (vs_ambient * c27w + m1 * front3) + green * A * m2 * back3) * c49"""
    green = 1.0 if _finite(A[1]) >= _finite(A[0]) else 0.0
    out = []
    for i in range(3):
        a = _finite(A[i])
        lit = a * (_finite(vs_ambient[i]) * _finite(c27w) + _finite(m1) * _finite(front3[i]))
        out.append(_finite((lit + green * a * _finite(m2) * _finite(back3[i])) * _finite(c49[i])))
    return out


def tree_leaf(A, vs_ambient, front, back, sun_col, c27w=1.0, *, m1, m2, c49):
    """(docstring as before)"""
    return tree_leaf_sums(A, vs_ambient, [_finite(s) * _finite(front) for s in sun_col],
                          [_finite(s) * _finite(back) for s in sun_col], c27w, m1=m1, m2=m2, c49=c49)
```

- [ ] **Step 4: Run the tests**

Run: `python -m unittest tests.test_light_check -v 2>&1 | Select-Object -Last 4`
Expected: `OK` (the new class and every existing test, `PinnedForTheRenderer` included: no pinned number moves).

- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/light_formulas.py tests/test_light_check.py
git commit -m "Local lights: the per-light form and the families' sum in light_formulas"
```

---

### Task 2: The checker's `--multi-lanterns` mode

**Files:**
- Modify: `tools/xdk_sigmatch/light_check.py` (`_sun_in_slot` 250, the four drivers `_terrain_layer_driver` 322, `_object_driver` 540, `_leaf_driver` 604, `_plant_driver` 820, the `--multi-sun` block 984-1050, `main` 1346-1380)
- Test: `tests/test_light_check.py` (class `MultiLanterns` after `MultiSun`)
- Evidence: `out/native-evidence/lighting/lanterns/light_check_multi_lanterns.txt`

**Interfaces:**
- Consumes: `lf.local_lights`, the families' `lights=` (Task 1).
- Produces: `check_multi_lanterns(family, ps, vs, trials=200, seed=1, dumps=DUMPS, tol=TOLERANCE) -> Result`; `print_multi_lanterns(trials, seed, dumps) -> bad`; `_lanterns_in_slots(rng, c, sun_slot, world, lights=4) -> list of lantern dicts` (pixel registers); `_leaf_lanterns(rng, vc, sun_slot) -> list` (vertex registers 76.., 83.., 90, 92, 94.., 101, 103); CLI `--multi-lanterns`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_light_check.py` after `MultiSun`:
```python
class MultiLanterns(unittest.TestCase):
    """Local lights (plan Task 2): with the game's light set (the sun in its slot) and random lanterns
    in every slot before it, each multi-light pair is its one-light function of the sun plus the sum of
    local_light over the lanterns. Only pairs that agree here are drawn with lanterns by the native pass."""
    TRIALS = 24

    def test_every_pair_agrees_with_lanterns_before_the_sun(self):
        checked = 0
        for family in lc.MULTI_SUN_FAMILIES:
            for ps, vs in lc.FAMILIES[family]:
                with self.subTest(family=family, ps=ps, vs=vs):
                    res = lc.check_multi_lanterns(family, ps, vs, trials=self.TRIALS)
                    self.assertTrue(res.ok, f"{res.agree}/{res.trials} worst {res.worst:.3g} {res.first_bad}")
                    self.assertEqual(res.trials, self.TRIALS)
                    checked += 1
        self.assertEqual(checked, 24)   # 21 two- and four-light pairs, 3 seven-light (campfire: 2f1001b)

    def test_a_lantern_term_dropped_or_moved_fails(self):
        family, (ps, vs) = "object-multi", lc.FAMILIES["object-multi"][0]
        with mock.patch.object(lc.lf, "local_lights", lambda *a, **k: [0.0, 0.0, 0.0]):
            self.assertFalse(lc.check_multi_lanterns(family, ps, vs, trials=8).ok)
        real = lc.lf.local_light
        with mock.patch.object(lc.lf, "local_light", lambda P, N, **k: real([x + 1.0 for x in P], N, **k)):
            self.assertFalse(lc.check_multi_lanterns(family, ps, vs, trials=8).ok)

    def test_the_lanterns_stand_before_the_sun_and_nowhere_else(self):
        rng = random.Random(3)
        c = {}
        lc._sun_in_slot(rng, c, 2)
        lanterns = lc._lanterns_in_slots(rng, c, 2, [1.0, 2.0, 3.0])
        self.assertEqual(len(lanterns), 2)
        for j, L in enumerate(lanterns):
            self.assertEqual(c[80 + j][3], 1.0)
            self.assertEqual(c[87 + j][:3], L["col"])
            self.assertEqual(c[112][j], L["shadow"])
        self.assertEqual(c[112][2], 1.0)
        self.assertEqual(c[87 + 3], [0.0] * 4)
```
Keep `random` and `mock` imported (they are, at the file's head).

- [ ] **Step 2: Run them to see them fail**

Run: `python -m unittest tests.test_light_check.MultiLanterns -v`
Expected: ERROR `module 'light_check' has no attribute 'check_multi_lanterns'`.

- [ ] **Step 3: Implement the lantern set and the mode**

In `light_check.py` after `_sun_in_slot`:
```python
def _lanterns_in_slots(rng, c, sun_slot, world, lights=4):
    """After _sun_in_slot: every slot before the sun's a lit lantern (the game's sets list lanterns first).
    A position near the vertex (|V| from 0 to about 2 / c96 so that the range term is not always 0 or 1),
    w = 1, a colour in [0.2, 2], c94 = 1 (one trial in three a blend in (0, 1): the general form), c96 in
    [0, 0.5], a random cone axis, c105 in [-1, 0.5], c107 in [0.5, 3], c112 = 0 (one trial in three a random
    strength: the shadow form). Returns the lanterns as local_light's keyword dicts, in slot order."""
    out = []
    for j in range(sun_slot):
        bank = j >= 4
        comp = j - 4 if bank else j
        blend_reg, range_reg, cos_reg, scale_reg, shadow_reg = (95, 97, 106, 108, 113) if bank else (94, 96, 105, 107, 112)
        for reg in (blend_reg, range_reg, cos_reg, scale_reg, shadow_reg):
            c.setdefault(reg, [0.0] * 4)
        L = dict(pos=[world[i] + rng.uniform(-8.0, 8.0) for i in range(3)], w=1.0,
                 col=[rng.uniform(0.2, 2.0) for _ in range(3)],
                 blend=rng.choice([1.0, 1.0, rng.random()]), range_scale=rng.uniform(0.0, 0.5),
                 axis=unit(rng), cone_cos=rng.uniform(-1.0, 0.5), cone_scale=rng.uniform(0.5, 3.0),
                 shadow=rng.choice([0.0, 0.0, rng.random()]))
        c[80 + j] = list(L["pos"]) + [1.0]
        c[87 + j] = list(L["col"]) + [1.0]
        c[98 + j] = list(L["axis"]) + [0.0]
        c[blend_reg][comp] = L["blend"]
        c[range_reg][comp] = L["range_scale"]
        c[cos_reg][comp] = L["cone_cos"]
        c[scale_reg][comp] = L["cone_scale"]
        c[shadow_reg][comp] = L["shadow"]
        out.append(L)
    return out
```
`unit(rng)` exists in the file (used by `_extra_lights`).

Give each pixel-side driver a `lanterns=False` parameter: `_terrain_layer_driver(ps, vs, dumps, multi, sun_slot=None, lanterns=False)`, `_object_driver(...)`, `_plant_driver(ps, vs, dumps, multi, lit, sun_slot=None, lanterns=False)`. In each `make_case`, right after the `_sun_in_slot(...)` call:
```python
                lantern_list = _lanterns_in_slots(rng, c, sun_slot, world, lights=light_count(ps, vs)) if lanterns else []
```
(`world` is `regs[slots["world"]][:3]` in the layer driver and `regs[2][:3]` in the object and plant drivers: the same vector `_light0` is fed). After `mask = rng.random()` and the gate (the `else` branch: `n_dot_sun`, `sun_gate_single`), compute:
```python
        lights = lf.local_lights(world, normal, lantern_list, mask=mask, facing=facing) if lanterns else None
```
(`facing` exists in the layer driver; the object and plant drivers pass `facing=1.0`; the plant driver's normal is `n_lit`.) Add `lights=lights` to `args` in each driver (both `args` dicts of the plant driver).

The leaf driver: add `lanterns=False`; after the `sun_slot` block that copies `game[80 + j]` into `vc[76 + j]`, when `lanterns`:
```python
            if lanterns:
                lantern_list = _lanterns_in_slots(rng, game, sun_slot, [0.0, 0.0, 0.0])
                for j in range(sun_slot):
                    vc[76 + j], vc[83 + j], vc[94 + j] = game[80 + j], game[87 + j], game[98 + j]
                vc[90], vc[92], vc[101], vc[103] = game[94], game[96], game[105], game[107]
```
(the lantern positions in [-8, 8] about the origin: the leaf's world position is random in a similar range through `vc[4..6]`). Then, where `front, back` are made for the sun (`light_dir, spot = lf.normalize(vc[76 + sun][:3]), 1.0`), build the sums:
```python
        front3 = [s * front for s in vc[83 + sun][:3]]
        back3 = [s * back for s in vc[83 + sun][:3]]
        if sun_slot is not None and lanterns:
            for j in range(sun_slot):
                to_light = [vc[76 + j][i] - world[i] * vc[76 + j][3] for i in range(3)]
                ld = lf.normalize(to_light)
                rng_ = lf.sat(1.0 - math.sqrt(lf.dot(to_light, to_light)) * vc[92][j])
                cone = lf.sat((lf.dot(ld, vc[94 + j][:3]) - vc[101][j]) * vc[103][j])
                spot = 1.0 + vc[90][j] * (cone * rng_ - 1.0)
                f, b = lf.leaf_light(normal, view, ld, vc[50][3], spot)
                front3 = [front3[i] + vc[83 + j][i] * f for i in range(3)]
                back3 = [back3[i] + vc[83 + j][i] * b for i in range(3)]
```
and the leaf's `args` become `dict(A=..., vs_ambient=ambient, front3=front3, back3=back3, c27w=c[27][3], m1=m1, m2=m2, c49=c[49][:3])` with `FORMULAS["tree-leaf"] = FORMULAS["tree-leaf-multi"] = lf.tree_leaf_sums`. (The one-light and `--multi-sun` leaf checks then run through `tree_leaf_sums` with the sun's products: the same numbers.)

Add after `print_multi_sun`:
```python
_LANTERN_DRIVERS = {
    "terrain-layer-multi": lambda ps, vs, dumps, slot: _terrain_layer_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "object-multi": lambda ps, vs, dumps, slot: _object_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "tree-trunk-multi": lambda ps, vs, dumps, slot: _object_driver(ps, vs, dumps, multi=True, sun_slot=slot, lanterns=True),
    "plant-multi": lambda ps, vs, dumps, slot: _plant_driver(ps, vs, dumps, True, lit=False, sun_slot=slot, lanterns=True),
    "plant-lit-multi": lambda ps, vs, dumps, slot: _plant_driver(ps, vs, dumps, True, lit=True, sun_slot=slot, lanterns=True),
    "tree-leaf-multi": lambda ps, vs, dumps, slot: _leaf_driver(ps, vs, dumps, sun_slot=slot, lanterns=True),
}


def check_multi_lanterns(family, ps, vs, trials=200, seed=1, dumps=DUMPS, tol=TOLERANCE):
    """Whether a multi-light pair, drawn with the game's set (the sun in GAME_SUN_SLOT) and lit lanterns in
    every slot before it, is its family's one-light function of the sun plus local_lights over the
    lanterns (lighting-formulas.md, the multi form per slot). A pair that agrees may be drawn with its
    lanterns by the native pass (local lights design, ruling 2); one that does not stays sun-only."""
    if family not in _LANTERN_DRIVERS:
        raise KeyError(f"{family}: not a family --multi-lanterns checks")
    slot = GAME_SUN_SLOT[light_count(ps, vs)]
    ps_text, make_case = _LANTERN_DRIVERS[family](ps, vs, dumps, slot)
    return check(ps_text, make_case, FORMULAS[family], trials, seed, tol)


def print_multi_lanterns(trials, seed, dumps):
    """Every pair of MULTI_SUN_FAMILIES with lanterns before the game's sun slot. Returns the pairs that disagree."""
    bad = 0
    for family in MULTI_SUN_FAMILIES:
        for ps, vs in FAMILIES[family]:
            lights = light_count(ps, vs)
            res = check_multi_lanterns(family, ps, vs, trials, seed, dumps)
            print(f"{family}: ps {ps} vs {vs} ({lights} lights, sun in slot {GAME_SUN_SLOT[lights]}, "
                  f"{GAME_SUN_SLOT[lights]} lanterns): {res.line()}")
            if not res.ok:
                bad += 1
                if res.first_bad:
                    k, got, want_ = res.first_bad
                    print(f"    first disagreement, trial {k}: microcode {got} formula {want_}")
    return bad
```
In `main`: `ap.add_argument("--multi-lanterns", action="store_true", help="every multi-light pair with lit lanterns before the game's sun slot, against the one-light function plus local_lights")`, include `args.multi_lanterns` in the "nothing asked" check, and `if args.multi_lanterns: bad += print_multi_lanterns(args.trials, args.seed, args.dumps)` beside the `--multi-sun` block; the usage text at the file's head gains the line.

- [ ] **Step 4: Run the tests and the mode**

Run: `python -m unittest tests.test_light_check -v 2>&1 | Select-Object -Last 4`
Expected: `OK`. If a pair disagrees, read `first_bad`: the slot form for that pair differs from the sun slot's; do not change the formula to fit; record the pair under "Open" in the task notes (it stays sun-only, spec ruling 2) and make the test's expected count the agreeing pairs with that pair listed in a `SUN_ONLY` set in `light_check.py` that `check_multi_lanterns` refuses with a `KeyError`.

Run: `python tools/xdk_sigmatch/light_check.py --multi-lanterns --trials 400 > out/native-evidence/lighting/lanterns/light_check_multi_lanterns.txt; Get-Content out/native-evidence/lighting/lanterns/light_check_multi_lanterns.txt | Select-Object -Last 25`
Expected: 24 lines, each `400/400` (the three seven-light campfire pairs among them: their sun in slot 4, four lanterns, the second bank).

- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/light_check.py tests/test_light_check.py
git commit -m "Local lights: light_check --multi-lanterns, every multi pair against the per-slot form"
```

---

### Task 3: The light set reader (pure)

**Files:**
- Modify: `src/native/capture/light_block.h` (after `LightBlock` / `MakeLightBlock`, before `LightBlockTries` at line 483)
- Test: `tests/native/test_light_block.cpp`

**Interfaces:**
- Produces:
```cpp
struct LocalLight { float pos[3]; float w; float col[3]; float blend; float range; float cone_cos, cone_scale; float shadow; float axis[3]; float pad; };  // 64 bytes
inline constexpr uint32_t kMaxLightSetLanterns = 6;
enum class LightSetReason : uint8_t { kNone, kNotThisScene, kNoSun, kUnread, kUnusable, kTooMany, kCount };
struct LightSet { LocalLight lights[kMaxLightSetLanterns]; uint32_t count; uint32_t sun_slot; float sun_dir[3]; LightSetReason reason; bool valid; };
inline constexpr uint32_t kLightSetSlotRegisters[7][2];   // {position reg, colour reg} per slot: {80+j, 87+j}
inline uint32_t LightSetSunSlot(const float (*reg)[4], const bool* have);   // 0..6, or 7 when c112/c113 is not one-hot or unread
inline LightSet MakeLightSet(const float (*reg)[4], const bool* have, bool light_set_this_scene);
inline bool LightSetEqual(const LightSet& a, const LightSet& b);  // every float bit-equal (memcmp of the lanterns and the sun)
inline const char* LightSetReasonName(LightSetReason r);  // "none", "not-this-scene", "no-sun", "unread", "unusable", "too-many"
```

- [ ] **Step 1: Write the failing tests**

In `tests/native/test_light_block.cpp`, after `Day()`, add a bank of the bridge's four-light set and the tests (in `main`, with the file's `CHECK` macro):
```cpp
// The registers of the bridge's "both lanterns, sun" set (frame-map section 14, "The light set"): two lanterns
// in slots 0 and 1, the sun in slot 2, c112 one-hot at 2.
Bank FourLight() {
  Bank b = Day();
  b.Set(80, 93.7503f, 161.322f, 42.3773f, 1.0f);
  b.Set(81, 115.876f, 158.233f, 41.4589f, 1.0f);
  b.Set(82, -0.671334f, 0.0940108f, 0.735169f, 0.0f);
  b.Set(83, 0.0f, 0.0f, 0.0f, 1.0f);
  b.Set(87, 1.55806f, 0.727093f, 0.0916504f, 0.998754f);
  b.Set(88, 1.64307f, 0.766766f, 0.0966512f, 1.05325f);
  b.Set(89, 1.00523f, 0.84597f, 0.408496f, 1.0f);
  b.Set(90, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(94, 1.0f, 1.0f, 0.0f, 0.0f);
  b.Set(96, 0.2505f, 0.2302f, 0.0f, 0.0f);
  b.Set(98, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(99, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(100, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(105, -1.0f, -1.0f, -1.0f, -1.0f);
  b.Set(107, 1.0f, 1.0f, 1.0f, 1.0f);
  b.Set(109, -0.671334f, 0.0940108f, 0.735169f, 0.0f);
  b.Set(112, 0.0f, 0.0f, 1.0f, 0.0f);
  b.Set(113, 0.0f, 0.0f, 0.0f, 0.0f);
  return b;
}
```
and in `main` (before the final `return 0`):
```cpp
  // The light set: the bridge's four-light set gives two lanterns in slot order, the sun at slot 2.
  {
    static_assert(sizeof(LocalLight) == 64 && sizeof(LightSet) == 6 * 64 + 4 + 4 + 12 + 1 + 1 + 2);
    Bank b = FourLight();
    CHECK(LightSetSunSlot(b.reg, b.have) == 2);
    LightSet s = MakeLightSet(b.reg, b.have, true);
    CHECK(s.valid && s.reason == LightSetReason::kNone && s.count == 2 && s.sun_slot == 2);
    CHECK(s.lights[0].pos[0] == 93.7503f && s.lights[0].w == 1.0f && s.lights[0].col[1] == 0.727093f && s.lights[0].blend == 1.0f &&
          s.lights[0].range == 0.2505f && s.lights[0].cone_cos == -1.0f && s.lights[0].cone_scale == 1.0f && s.lights[0].shadow == 0.0f &&
          s.lights[0].axis[2] == 0.0f);
    CHECK(s.lights[1].pos[0] == 115.876f && s.lights[1].range == 0.2302f && s.lights[1].col[0] == 1.64307f);
    CHECK(s.sun_dir[0] == -0.671334f && s.sun_dir[2] == 0.735169f);
    CHECK(LightSetEqual(s, MakeLightSet(b.reg, b.have, true)));
    // Not loaded this scene: refused.
    CHECK(!MakeLightSet(b.reg, b.have, false).valid && MakeLightSet(b.reg, b.have, false).reason == LightSetReason::kNotThisScene);
    // A set whose c112 is not one-hot, or whose named slot is a position (w = 1), or has c94 = 1 there: no sun.
    Bank two = b; two.Set(112, 1.0f, 0.0f, 1.0f, 0.0f);
    CHECK(MakeLightSet(two.reg, two.have, true).reason == LightSetReason::kNoSun);
    Bank pos = b; pos.Set(82, 1.0f, 2.0f, 3.0f, 1.0f);
    CHECK(MakeLightSet(pos.reg, pos.have, true).reason == LightSetReason::kNoSun);
    Bank blend = b; blend.Set(94, 1.0f, 1.0f, 1.0f, 0.0f);
    CHECK(MakeLightSet(blend.reg, blend.have, true).reason == LightSetReason::kNoSun);
    // A lantern register not read: unread; a NaN colour, a position past 1,024, an infinite range: unusable.
    Bank unread = b; unread.have[96] = false;
    CHECK(MakeLightSet(unread.reg, unread.have, true).reason == LightSetReason::kUnread);
    Bank nan = b; nan.Set(88, std::numeric_limits<float>::quiet_NaN(), 0.7f, 0.1f, 1.0f);
    CHECK(MakeLightSet(nan.reg, nan.have, true).reason == LightSetReason::kUnusable);
    Bank far = b; far.Set(81, 100000.0f, 158.0f, 41.0f, 1.0f);
    CHECK(MakeLightSet(far.reg, far.have, true).reason == LightSetReason::kUnusable);
    Bank inf = b; inf.Set(96, 0.25f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    CHECK(MakeLightSet(inf.reg, inf.have, true).reason == LightSetReason::kUnusable);
    // The sun in slot 0 (a one-light set): valid, no lanterns. A dark lantern (colour zero) is dropped.
    Bank day = Day();
    LightSet none = MakeLightSet(day.reg, day.have, true);
    CHECK(none.valid && none.count == 0 && none.sun_slot == 0);
    Bank dark = b; dark.Set(87, 0.0f, 0.0f, 0.0f, 1.0f);
    LightSet one = MakeLightSet(dark.reg, dark.have, true);
    CHECK(one.valid && one.count == 1 && one.lights[0].pos[0] == 115.876f);
    // Slots 4 to 6 read the second bank: the sun at slot 4 with lanterns in 0..3 uses c95, c97, c106, c108, c113 for none of
    // them; a sun at slot 5 reads the lantern of slot 4 from the second bank.
    Bank seven = b;
    seven.Set(82, 50.0f, 60.0f, 40.0f, 1.0f); seven.Set(89, 1.0f, 0.5f, 0.1f, 1.0f);
    seven.Set(83, 51.0f, 61.0f, 41.0f, 1.0f); seven.Set(90, 1.1f, 0.5f, 0.1f, 1.0f);
    seven.Set(84, 52.0f, 62.0f, 42.0f, 1.0f); seven.Set(91, 1.2f, 0.5f, 0.1f, 1.0f);
    seven.Set(85, -0.671334f, 0.0940108f, 0.735169f, 0.0f); seven.Set(92, 1.0f, 0.8f, 0.4f, 1.0f);
    seven.Set(94, 1.0f, 1.0f, 1.0f, 1.0f); seven.Set(95, 1.0f, 0.0f, 0.0f, 0.0f);
    seven.Set(96, 0.25f, 0.25f, 0.25f, 0.25f); seven.Set(97, 0.125f, 0.0f, 0.0f, 0.0f);
    seven.Set(101, 0.0f, 0.0f, 0.0f, 0.0f); seven.Set(102, 0.0f, 0.0f, 1.0f, 0.0f);
    seven.Set(106, -1.0f, -1.0f, -1.0f, 0.0f); seven.Set(108, 1.0f, 1.0f, 1.0f, 0.0f);
    seven.Set(112, 0.0f, 0.0f, 0.0f, 0.0f); seven.Set(113, 0.0f, 1.0f, 0.0f, 0.0f);
    LightSet s7 = MakeLightSet(seven.reg, seven.have, true);
    CHECK(s7.valid && s7.sun_slot == 5 && s7.count == 5 && s7.lights[4].pos[0] == 52.0f && s7.lights[4].range == 0.125f &&
          s7.lights[4].axis[2] == 1.0f && s7.lights[4].blend == 1.0f && s7.lights[4].shadow == 0.0f);
    CHECK(std::strcmp(LightSetReasonName(LightSetReason::kTooMany), "too-many") == 0);
  }
```

- [ ] **Step 2: Compile and run to see it fail**

```powershell
clang++ -std=c++23 tests\native\test_light_block.cpp -o out\tests\light_block.exe
```
Expected: compile errors `unknown type name 'LightSet'`.

- [ ] **Step 3: Implement**

In `light_block.h` before `class LightBlockTries`:
```cpp
// --- the light set: the lanterns of a multi-light draw (local lights, 2026-10-13) --------------
// One local light as the two-, four- and seven-light shaders read slot j (lighting-formulas.md, the
// multi form; light_formulas.local_light): the position or direction c(80 + j) with its w, the colour
// c(87 + j), and component j of c94 (blend of cone and range), c96 (range term), c105 and c107 (the
// cone's cosine and slope), c112 (shadow strength), with the cone axis c(98 + j); slots 4 to 6 read
// c95, c97, c106, c108, c113. 64 bytes, the layout the frame constant buffer carries (four float4).
struct LocalLight {
  float pos[3]; float w;
  float col[3]; float blend;
  float range; float cone_cos; float cone_scale; float shadow;
  float axis[3]; float pad;
};
static_assert(sizeof(LocalLight) == 64);
inline constexpr uint32_t kLightSetSlots = 7, kMaxLightSetLanterns = 6;
enum class LightSetReason : uint8_t { kNone, kNotThisScene, kNoSun, kUnread, kUnusable, kTooMany, kCount };
inline const char* LightSetReasonName(LightSetReason r) {
  switch (r) {
    case LightSetReason::kNone: return "none";
    case LightSetReason::kNotThisScene: return "not-this-scene";
    case LightSetReason::kNoSun: return "no-sun";
    case LightSetReason::kUnread: return "unread";
    case LightSetReason::kUnusable: return "unusable";
    case LightSetReason::kTooMany: return "too-many";
    default: return "?";
  }
}
// A draw's light set as the pass needs it: the lanterns (the slots before the sun's, lit ones, in
// slot order), the sun's slot and its direction (the pass compares it with the frame's block:
// MakeLanternBuffer refuses a set whose sun is not the frame's). `valid` false: `reason` says why
// and nothing else is read.
struct LightSet {
  LocalLight lights[kMaxLightSetLanterns];
  uint32_t count = 0;
  uint32_t sun_slot = 0;
  float sun_dir[3] = {};
  LightSetReason reason = LightSetReason::kNone;
  bool valid = false;
  uint8_t pad[2] = {};
};
static_assert(sizeof(LightSet) == 6 * 64 + 4 + 4 + 12 + 1 + 1 + 2);
// The usable magnitudes, as the pass's rules (clay_logic.h LightValueUsable, LightPositionUsable):
// a value at most 1,024, a position at most 65,536.
inline constexpr float kLightSetValueMax = 1024.0f, kLightSetPositionMax = 65536.0f;
inline bool LightSetValueUsable(float v) { return std::fabs(v) <= kLightSetValueMax; }
inline bool LightSetPositionUsable(float v) { return std::fabs(v) <= kLightSetPositionMax; }
// The per-slot registers: {blend, range, cone cos, cone slope, shadow} and the component, by slot.
struct LightSetSlotRegs { uint32_t blend, range, cone_cos, cone_scale, shadow, comp; };
constexpr LightSetSlotRegs LightSetRegsOf(uint32_t slot) {
  return slot < 4 ? LightSetSlotRegs{94, 96, 105, 107, 112, slot} : LightSetSlotRegs{95, 97, 106, 108, 113, slot - 4};
}
// The sun's slot: the one component of c112.xyzw, c113.xyz that is 1 with every other 0 (one-hot).
// kLightSetSlots when the two registers were not read or are not one-hot.
inline uint32_t LightSetSunSlot(const float (*reg)[4], const bool* have) {
  if (!reg || !have || !have[112] || !have[113]) return kLightSetSlots;
  uint32_t hot = kLightSetSlots, hots = 0;
  for (uint32_t slot = 0; slot < kLightSetSlots; ++slot) {
    const LightSetSlotRegs r = LightSetRegsOf(slot);
    const float v = reg[r.shadow][r.comp];
    if (v == 1.0f) { hot = slot; ++hots; } else if (v != 0.0f) return kLightSetSlots;
  }
  return hots == 1 ? hot : kLightSetSlots;
}
inline LightSet MakeLightSet(const float (*reg)[4], const bool* have, bool light_set_this_scene) {
  LightSet s;
  auto refuse = [&](LightSetReason why) { LightSet r; r.reason = why; return r; };
  if (!reg || !have) return refuse(LightSetReason::kUnread);
  if (!light_set_this_scene) return refuse(LightSetReason::kNotThisScene);
  const uint32_t sun = LightSetSunSlot(reg, have);
  if (sun >= kLightSetSlots) return refuse(LightSetReason::kNoSun);
  // The sun: a direction (w = 0) without range or cone (c94 zero there), read.
  const LightSetSlotRegs sr = LightSetRegsOf(sun);
  if (!have[80 + sun] || !have[sr.blend]) return refuse(LightSetReason::kUnread);
  if (reg[80 + sun][3] != 0.0f || reg[sr.blend][sr.comp] != 0.0f) return refuse(LightSetReason::kNoSun);
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(reg[80 + sun][i])) return refuse(LightSetReason::kUnusable);
    s.sun_dir[i] = reg[80 + sun][i];
  }
  s.sun_slot = sun;
  for (uint32_t slot = 0; slot < sun; ++slot) {
    const LightSetSlotRegs r = LightSetRegsOf(slot);
    if (!have[80 + slot] || !have[87 + slot] || !have[98 + slot] || !have[r.blend] || !have[r.range] || !have[r.cone_cos] ||
        !have[r.cone_scale] || !have[r.shadow]) {
      return refuse(LightSetReason::kUnread);
    }
    LocalLight l{};
    for (int i = 0; i < 3; ++i) {
      l.pos[i] = reg[80 + slot][i];
      l.col[i] = reg[87 + slot][i];
      l.axis[i] = reg[98 + slot][i];
      if (!LightSetPositionUsable(l.pos[i]) || !LightSetValueUsable(l.col[i]) || !LightSetValueUsable(l.axis[i])) {
        return refuse(LightSetReason::kUnusable);
      }
    }
    l.w = reg[80 + slot][3];
    l.blend = reg[r.blend][r.comp];
    l.range = reg[r.range][r.comp];
    l.cone_cos = reg[r.cone_cos][r.comp];
    l.cone_scale = reg[r.cone_scale][r.comp];
    l.shadow = reg[r.shadow][r.comp];
    if (!LightSetValueUsable(l.w) || !LightSetValueUsable(l.blend) || !LightSetValueUsable(l.range) ||
        !LightSetValueUsable(l.cone_cos) || !LightSetValueUsable(l.cone_scale) || !LightSetValueUsable(l.shadow)) {
      return refuse(LightSetReason::kUnusable);
    }
    if (l.col[0] == 0.0f && l.col[1] == 0.0f && l.col[2] == 0.0f) continue;  // an unused slot
    s.lights[s.count++] = l;
  }
  s.valid = true;
  return s;
}
inline bool LightSetEqual(const LightSet& a, const LightSet& b) {
  if (a.valid != b.valid || a.count != b.count || a.sun_slot != b.sun_slot || a.reason != b.reason) return false;
  return std::memcmp(a.lights, b.lights, sizeof(LocalLight) * a.count) == 0 && std::memcmp(a.sun_dir, b.sun_dir, sizeof(a.sun_dir)) == 0;
}
```
(`<cstring>` and `<cmath>` are included by the header already; add them if not.) The `-1.0f` value of `c105` is at most 1,024: usable.

- [ ] **Step 4: Compile and run**

```powershell
clang++ -std=c++23 tests\native\test_light_block.cpp -o out\tests\light_block.exe; .\out\tests\light_block.exe
```
Expected: exit 0, no `FAIL` line. Then the whole native suite (Global Constraints): exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/light_block.h tests/native/test_light_block.cpp
git commit -m "Local lights: MakeLightSet, the lanterns before the sun of a light set"
```

---

### Task 4: The light set in the capture and the frame

**Files:**
- Modify: `src/native/capture/material.h:538-577` (`LightInputs::light_set`)
- Modify: `src/native/capture/light_block.h` (after `LightBlockTries`: `LightSetCache`)
- Modify: `src/native/capture/capture.cpp:1670-1705` (`ReadLightSet`), `:3880-3940` (discovery `"lset"`), `:5625-5700` (RecordDraw), the `Light inputs:` log line (grep `light_multi` in `capture.cpp` / `frame_scene.h` `FormatLightInputsText` or its equivalent)
- Modify: `src/native/capture/capture.h` (`SetLocalLightsWanted(bool)`)
- Modify: `src/native/render/frame_scene.h:47-61` (`light_sets`, tallies), `:690-760` (builder `AddLightSet`, `Add`), spares and `Finish`
- Test: `tests/native/test_light_block.cpp` (the cache), `tests/native/test_frame_scene.cpp` (the builder)

**Interfaces:**
- Consumes: `LightSet`, `MakeLightSet`, `LightSetEqual`, `LightSetReason` (Task 3); `LightSetUpload()`, `LightSetThisScene()`, `ReadPixelConstant` (capture.cpp).
- Produces: `LightInputs::light_set` (`uint16_t`, `kNoSideIndex` none); `FrameScene::light_sets` (`std::vector<capture::LightSet>`), `FrameScene::light_set_draws` (`uint32_t`: draws with a set), `FrameScene::light_set_reason[size_t(LightSetReason::kCount)]`; `FrameBuilder::AddLightSet(const capture::LightSet&) -> uint16_t` (dedupes by `LightSetEqual`; `kNoSideIndex` and `kTooMany` counted past `kMaxLightSets = 16`); `inline const capture::LightSet* LightSetOf(const FrameScene&, const capture::LightInputs&)`; `class LightSetCache` (per thread: `const LightSet* Find(uint32_t frame, uint64_t upload)`, `void Put(uint32_t frame, uint64_t upload, const LightSet&)`, 16 entries, a new frame empties it); `capture::SetLocalLightsWanted(bool)`.

- [ ] **Step 1: Write the failing tests**

`tests/native/test_light_block.cpp`, in `main`:
```cpp
  // The per-thread cache of sets by upload: a hit within the frame, none across frames, 16 entries.
  {
    LightSetCache cache;
    Bank b = FourLight();
    const LightSet s = MakeLightSet(b.reg, b.have, true);
    CHECK(cache.Find(7, 100) == nullptr);
    cache.Put(7, 100, s);
    CHECK(cache.Find(7, 100) != nullptr && LightSetEqual(*cache.Find(7, 100), s));
    CHECK(cache.Find(8, 100) == nullptr);   // another frame: empty (and the entry of frame 7 is gone)
    CHECK(cache.Find(7, 100) == nullptr);
    for (uint64_t u = 1; u <= 20; ++u) cache.Put(9, u, s);
    CHECK(cache.Find(9, 20) != nullptr && cache.Find(9, 1) == nullptr);  // the oldest entries make room
  }
```
`tests/native/test_frame_scene.cpp`: find the block that tests `Add` with light inputs (line 192) and add after it, using that file's builder `lb` and record `plain` / inputs `obj` as that block does (read the block first; use its names):
```cpp
    // Light sets: AddLightSet dedupes by value, numbers in order, refuses past kMaxLightSets.
    capture::LightSet set{};
    set.valid = true; set.count = 1; set.sun_slot = 1; set.lights[0].pos[0] = 1.0f; set.lights[0].col[0] = 1.0f;
    const uint16_t i0 = lb.AddLightSet(set);
    capture::LightSet other = set; other.lights[0].pos[0] = 2.0f;
    const uint16_t i1 = lb.AddLightSet(other);
    CHECK(i0 == 0 && i1 == 1 && lb.AddLightSet(set) == 0);
    for (int k = 0; k < 20; ++k) { other.lights[0].pos[1] = float(k + 10); lb.AddLightSet(other); }
    capture::LightInputs with_set = obj; with_set.light_set = i1;
    lb.Add(plain, nullptr, &with_set);
    FrameScene* fs = lb.Finish(...);   // as the block's own Finish call
    CHECK(fs->light_sets.size() == 16 && fs->light_set_reason[size_t(capture::LightSetReason::kTooMany)] == 6);
    CHECK(fs->light_set_draws == 1 && LightSetOf(*fs, *LightOf(*fs, fs->draws.back())) == &fs->light_sets[1]);
    capture::LightInputs no_set = obj;
    CHECK(LightSetOf(*fs, no_set) == nullptr);
```

- [ ] **Step 2: Run to see them fail**

Compile both tests as the suite does (`run_native_tests.cmd` lines for `light_block` and `frame_scene`); expected: unknown identifiers `LightSetCache`, `AddLightSet`, `light_sets`.

- [ ] **Step 3: Implement**

`material.h`: add to `LightInputs` after `tree_fog`: `uint16_t light_set = kNoSideIndex;  // the draw's light set in FrameScene::light_sets (local lights): kNoSideIndex none` and two bytes of padding so the struct's size is a multiple of 4; change the `static_assert(sizeof(LightInputs) == 332 ...)` to 336 and its comment. `kNoSideIndex` is in `draw_record.h`; include it or forward the constant (`material.h` may already include it: check).

`light_block.h` after `LightBlockTries`:
```cpp
// The sets a thread read this frame, by the upload that loaded them (LightSetUpload): at most
// kEntries, the oldest replaced; another frame empties it. A set read in one frame is never another
// frame's (the frame number keys it); the builder dedupes by value anyway (FrameBuilder::AddLightSet).
class LightSetCache {
 public:
  static constexpr uint32_t kEntries = 16;
  const LightSet* Find(uint32_t frame, uint64_t upload) {
    Open(frame);
    for (uint32_t i = 0; i < count_; ++i) {
      if (uploads_[i] == upload) return &sets_[i];
    }
    return nullptr;
  }
  void Put(uint32_t frame, uint64_t upload, const LightSet& set) {
    Open(frame);
    const uint32_t at = count_ < kEntries ? count_++ : next_++ % kEntries;
    uploads_[at] = upload;
    sets_[at] = set;
  }
 private:
  void Open(uint32_t frame) {
    if (frame != frame_) { frame_ = frame; count_ = 0; next_ = 0; }
  }
  uint64_t uploads_[kEntries] = {};
  LightSet sets_[kEntries] = {};
  uint32_t frame_ = 0, count_ = 0, next_ = 0;
};
```

`frame_scene.h`: in `FrameScene` after `light_tries`: `std::vector<capture::LightSet> light_sets;` with a comment (side array of the frame's light sets, named by `LightInputs::light_set`; built and handed over like `lights`), `uint32_t light_set_draws = 0;`, `uint32_t light_set_reason[size_t(capture::LightSetReason::kCount)] = {};`. After `LightOf`:
```cpp
inline const capture::LightSet* LightSetOf(const FrameScene& scene, const capture::LightInputs& l) {
  if (l.light_set == capture::kNoSideIndex || l.light_set >= scene.light_sets.size()) return nullptr;
  return &scene.light_sets[l.light_set];
}
```
In `FrameBuilder`: a member `std::vector<capture::LightSet> light_sets_;`, counters `light_set_draws_`, `light_set_reason_[]`; `static constexpr size_t kMaxLightSets = 16;`
```cpp
  // A draw's light set (capture::MakeLightSet, valid): its index in the frame's sets, an existing
  // entry when one is bit-equal (LightSetEqual), else a new one; kNoSideIndex, counted too-many, past
  // kMaxLightSets. An invalid set is counted by its reason and gives kNoSideIndex.
  uint16_t AddLightSet(const capture::LightSet& s) {
    if (!open_) return capture::kNoSideIndex;
    if (!s.valid) { ++light_set_reason_[size_t(s.reason)]; return capture::kNoSideIndex; }
    for (size_t i = 0; i < light_sets_.size(); ++i) {
      if (capture::LightSetEqual(light_sets_[i], s)) return uint16_t(i);
    }
    if (light_sets_.size() >= kMaxLightSets) { ++light_set_reason_[size_t(capture::LightSetReason::kTooMany)]; return capture::kNoSideIndex; }
    light_sets_.push_back(s);
    return uint16_t(light_sets_.size() - 1);
  }
```
In `Add`, where `lit` is handled: `if (lit && light->light_set != capture::kNoSideIndex && light->light_set < light_sets_.size()) ++light_set_draws_;` (an index past the sets is left as it is: `LightSetOf` refuses it). In `Finish` and the spares' swap/clear, treat `light_sets_` as `lights_` is treated (hand over, clear, and the spares' `Buffers` struct gains the vector; follow every place `lights` / `lights_` appears: grep them all and mirror each). Copy the counters into the scene in `Finish` as `light_multi_` is copied.

`capture.cpp`: after `ReadLightBlock()`:
```cpp
// The light set at the draw this thread is capturing (light_block.h, MakeLightSet): the sun's slot
// from c112 and c113, then the registers of the slots before it. Refused as a whole (the reason in the
// set) unless the set was loaded this scene (LightSetThisScene). Costs 2 + 8 per lantern slot
// ReadPixelConstant calls; made once per upload of the set (LightSetCache), at draws of the
// multi-light families and the leaves only, with the switch on (g_local_lights_frame).
LightSet ReadLightSet() {
  if (!LightSetThisScene()) { LightSet s; s.reason = LightSetReason::kNotThisScene; return s; }
  float reg[ConstantLoads::kRegisters][4] = {};
  bool have[ConstantLoads::kRegisters] = {};
  have[112] = ReadPixelConstant(112, reg[112]);
  have[113] = ReadPixelConstant(113, reg[113]);
  const uint32_t sun = LightSetSunSlot(reg, have);
  if (sun >= kLightSetSlots) return MakeLightSet(reg, have, true);  // (no-sun, with the two read)
  const LightSetSlotRegs sr = LightSetRegsOf(sun);
  have[80 + sun] = ReadPixelConstant(80 + sun, reg[80 + sun]);
  have[sr.blend] = ReadPixelConstant(sr.blend, reg[sr.blend]);
  for (uint32_t slot = 0; slot < sun; ++slot) {
    const LightSetSlotRegs r = LightSetRegsOf(slot);
    for (uint32_t k : {80 + slot, 87 + slot, 98 + slot, r.blend, r.range, r.cone_cos, r.cone_scale}) {
      if (!have[k]) have[k] = ReadPixelConstant(k, reg[k]);
    }
  }
  return MakeLightSet(reg, have, true);
}
constinit thread_local LightSetCache t_light_sets;
std::atomic<bool> g_local_lights_frame{false};   // SetLocalLightsWanted; read at the draws
```
`capture.h`: `void SetLocalLightsWanted(bool wanted);` beside `SetWindWanted`, implemented as the others are (store the atomic; the render side calls it where it calls `SetWindWanted`, Task 8).

In `RecordDraw` (line 5625 on): after `light_reason = FillLightInputs(...)`, when `light_reason == LightReason::kNone && g_local_lights_frame.load(std::memory_order_relaxed)` and the family is multi or the leaf family (`shaders.ps->light->multi || shaders.ps->light->family == LightFamily::kTreeLeaf`):
```cpp
      const uint32_t frame = g_light_tries_frame.load(std::memory_order_relaxed);
      const uint64_t upload = LightSetUpload();
      if (upload == 0) {
        light_set = LightSet{}; light_set.reason = LightSetReason::kNotThisScene; have_light_set = true;
      } else if (const LightSet* hit = t_light_sets.Find(frame, upload)) {
        light_set = *hit; have_light_set = true;
      } else {
        light_set = ReadLightSet(); have_light_set = true;
        t_light_sets.Put(frame, upload, light_set);
      }
```
(declare `LightSet light_set; bool have_light_set = false;` beside `LightInputs light;`). Under the scene mutex, just before `g_builder.Add(...)` for the record: `if (have_light_set) light.light_set = g_builder.AddLightSet(light_set);`. Note the builder is given `&light` already; the field is set before the call.

Discovery rows (`AppendLightInputs`, line 3896 on; the row is made per draw with the same reads): after the `"c8x"` float, when the draw's family is multi or the leaf family, read the set as `RecordDraw` does (through the cache with the row's frame) and append `,"lset":{"upload":%llu,"reason":"%s","sun_slot":%u,"count":%u}` and, when valid, `"lanterns":[[pos x,y,z,w, col r,g,b, blend, range, cone_cos, cone_scale, shadow, axis x,y,z], ...]` through the file's `AppendFloat`.

The `Light inputs:` line (grep `light_multi` where the line is formatted): append `; sets N (draws M; refused: not-this-scene a, no-sun b, unread c, unusable d, too-many e)` with zero counts left out, through `LightSetReasonName`.

- [ ] **Step 4: Run the suite**

The native suite: exit 0 (`light_block`, `frame_scene`, and `discovery_format` if it checks row keys: add `"lset"` where that test lists optional keys, read the test first).

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/material.h src/native/capture/light_block.h src/native/capture/capture.cpp src/native/capture/capture.h src/native/render/frame_scene.h tests/native/test_light_block.cpp tests/native/test_frame_scene.cpp
git commit -m "Local lights: the light set read once per upload into the frame's side array"
```

---

### Task 5: World positions for the lit objects and trunks

**Files:**
- Modify: `src/native/capture/material.h:709-716` (`MakeLightInputs`: `world_pos` for every object kind)
- Modify: `src/native/render/clay_logic.h:3101-3163` (`MakeClayObjectConstants`: translation and inverse scale for objects and trunks)
- Modify: `src/native/fable2_native_shaders.h:392-396` (the `Object` comment), `:585` (`o.world`), `:602-618` (the object block hands on the world position)
- Test: `tests/native/test_clay_logic.cpp` (the `MakeClayObjectConstants` cases: grep `MakeClayObjectConstants(` there), `tests/native/test_native_shaders.cpp` (`kPinVsMain`)

**Interfaces:**
- Produces: `ClayObjectConstants::rot[r][3]` = `world_pos[r]` and `leaf[1]` = `1 / world_scale` for `kObject` and `kTreeTrunk` as for a leaf (an instanced plant unchanged); the vertex shader's `world` interpolator is the world position for every lit object, trunk and leaf (`(M p) * o_leaf.y + translation`).

- [ ] **Step 1: Write the failing tests**

`test_clay_logic.cpp`: find the case that builds `ClayObjectConstants` from a `kObject` `LightInputs` (grep `MakeClayObjectConstants(` and read the nearest cases); add, with the next free case numbers (the file's highest `CHECK` is 862, so use 863 on):
```cpp
  // 863-866: an object's and a trunk's translation and inverse scale reach the constants as a leaf's do.
  {
    capture::LightInputs l = ObjectInputs();   // the helper the neighbouring cases use (kObject, kStream)
    l.world_pos[0] = 93.75f; l.world_pos[1] = 161.3f; l.world_pos[2] = 42.4f;
    l.world_scale = 2.0f;
    ClayObjectConstants c;
    CHECK(863, MakeClayObjectConstants(l, &c) && c.rot[0][3] == 93.75f && c.rot[2][3] == 42.4f && c.leaf[1] == 0.5f);
    l.world_pos[1] = 1.0e6f;   // past kLightPositionMax: refused as a whole
    CHECK(864, !MakeClayObjectConstants(l, &c));
    capture::LightInputs t = TrunkInputs();   // kTreeTrunk, kProbe
    t.world_pos[2] = -3.0f; t.world_scale = 0.8f;
    CHECK(865, MakeClayObjectConstants(t, &c) && c.rot[2][3] == -3.0f && std::fabs(c.leaf[1] - 1.25f) < 1e-6f);
    capture::LightInputs p = PlantInputs();   // kInstance: the bend constants keep the fourth components
    CHECK(866, MakeClayObjectConstants(p, &c) && c.rot[0][3] == p.vs_const[0][0] && c.leaf[1] == 0.0f);
  }
```
If helpers with those names do not exist, build the inputs inline as the neighbouring cases do (copy their construction).

- [ ] **Step 2: Run to see them fail**

Compile `test_clay_logic.cpp` as the suite does; expected: case 863 fails (`rot[0][3]` is 0 today).

- [ ] **Step 3: Implement**

`material.h` line 714: `take(d.vs_regs, kLightVsRotationReg + row, 3, &l.world_pos[row]);` for every row (drop the `if (leaf)`); keep the comment: "the row's fourth component too, the world position's translation (a leaf's light, and since the local lights every object's lantern vector)".

`clay_logic.h` `MakeClayObjectConstants`: move the translation and inverse-scale block out of `if (leaf)` so it runs for every non-instance draw:
```cpp
  const float inv_scale = 1.0f / l.world_scale;
  if (!LightValueUsable(inv_scale)) return false;
  for (int r = 0; r < 3; ++r) {
    if (!LightPositionUsable(l.world_pos[r])) return false;
    c.rot[r][3] = l.world_pos[r];
  }
  c.leaf[1] = inv_scale;
  if (leaf) {
    if (!(l.wrap >= 0.0f) || !LightValueUsable(l.wrap)) return false;
    c.leaf[0] = l.wrap;
    c.colour_power = kLeafColourPower;
  }
```
and update the struct's comments (`rot[r][3]`: the translation for an object, trunk or leaf; `leaf[1]`: the inverse scale for all three).

`fable2_native_shaders.h`: in the object block (`(light_mode & 82) == 2`), after the normal: 
```hlsl
    o.world = float3(dot(o_rot0.xyz, p.xyz), dot(o_rot1.xyz, p.xyz), dot(o_rot2.xyz, p.xyz)) * o_leaf.y + float3(o_rot0.w, o_rot1.w, o_rot2.w);
```
and in the leaf block replace the local `at` by `o.world = ...` with the same expression and use `o.world` in `LeafLight`'s view vector. Update the `Object` cbuffer comment lines (393-395) and the vertex-shader comment ("hands on the position it read (`world`...)": now the world position for a lit object, trunk or leaf; the position as read for the terrain and the plants). Update `kPinVsMain` in `test_native_shaders.cpp` to the new text (copy the main verbatim).

- [ ] **Step 4: Run the suite**

Native suite: exit 0 (`clay_logic`, `native_shaders`).

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/material.h src/native/render/clay_logic.h src/native/fable2_native_shaders.h tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp
git commit -m "Local lights: the world position of every lit object and trunk in the vertex shader"
```

---

### Task 6: The lanterns in the frame constant buffer and the draw word

**Files:**
- Modify: `src/native/render/clay_logic.h:2896-2920` (`ClayFrameConstants` tail), `:2217-2245` (the lantern bits), `:5822-5826` (`kClayFrameSlots`), after `MakeClayFrameConstants` (`MakeLanternBuffer`)
- Modify: `src/native/fable2_native_shaders.h:136-172` (field tables), `:221-236` (cbuffer `Frame`), the vertex shader's cbuffer (the `CLAY_LANTERNS_VS` pad)
- Test: `tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp:2071-2150` (the frame layout test)

**Interfaces:**
- Produces:
```cpp
inline constexpr uint32_t kMaxFrameLanterns = 64;
struct ClayFrameConstants { ...; float transmit[256][4]; float lanterns[kMaxFrameLanterns][16]; };   // 9440 bytes, 37 slots
inline constexpr uint32_t kClayFrameLanternBytes = offsetof(ClayFrameConstants, lanterns);  // 5344
inline constexpr uint32_t kLightModeLanterns = 2048u, kLightModeLanternFirstShift = 16, kLightModeLanternCountShift = 22;
constexpr uint32_t LightModeLanterns(uint32_t first, uint32_t count);  // 0 for count 0 or first + count > kMaxFrameLanterns; else the three fields
struct LanternPlan { uint16_t first[16]; uint8_t count[16]; uint32_t lanterns; uint32_t sets_drawn, sets_refused_sun, sets_refused_room; };
inline LanternPlan MakeLanternBuffer(ClayFrameConstants& c, const std::vector<capture::LightSet>& sets);  // fills c.lanterns, requires c.light_on
```
HLSL: `float4 f_lanterns[256]` after `f_transmit` in the lit programs (under `CLAY_FOG`, which all three lit pixel programs define) and in the vertex shader under `CLAY_LANTERNS_VS` after a pad `float4 f_fog_pad[327]`.

- [ ] **Step 1: Write the failing tests**

`test_clay_logic.cpp` (cases 867 on):
```cpp
  // 867-872: the lantern word and the frame's lantern buffer.
  {
    CHECK(867, LightModeLanterns(0, 0) == 0 && LightModeLanterns(63, 2) == 0 && LightModeLanterns(5, 7) == 0);
    const uint32_t w = LightModeLanterns(5, 3);
    CHECK(868, (w & kLightModeLanterns) && ((w >> kLightModeLanternFirstShift) & 63) == 5 && ((w >> kLightModeLanternCountShift) & 7) == 3 &&
               (w & (kLightModeCount - 1 + kLightModeUnlitFog + kLightModeUnlitFogAlpha + kLightModeLitNoFog + kLightModeUnlitFogTree + kLightModeWindMask)) == 0);
    ClayFrameConstants c{};
    c.light_on = 1; c.sun_dir[0] = -0.671334f; c.sun_dir[1] = 0.0940108f; c.sun_dir[2] = 0.735169f;
    std::vector<capture::LightSet> sets(3);
    for (auto& s : sets) { s.valid = true; s.count = 2; s.sun_slot = 2; std::memcpy(s.sun_dir, c.sun_dir, sizeof(c.sun_dir));
                           s.lights[0].pos[0] = 93.75f; s.lights[0].col[0] = 1.5f; s.lights[0].range = 0.25f; s.lights[0].w = 1.0f;
                           s.lights[1].pos[0] = 115.9f; s.lights[1].col[0] = 1.6f; s.lights[1].range = 0.23f; s.lights[1].w = 1.0f; }
    sets[1].sun_dir[0] = -0.5f;         // another sun: refused
    sets[2].valid = false;              // invalid: refused (not counted as a sun refusal)
    const LanternPlan plan = MakeLanternBuffer(c, sets);
    CHECK(869, plan.sets_drawn == 1 && plan.sets_refused_sun == 1 && plan.lanterns == 2 && plan.first[0] == 0 && plan.count[0] == 2 &&
               plan.count[1] == 0 && plan.count[2] == 0);
    CHECK(870, c.lanterns[0][0] == 93.75f && c.lanterns[0][3] == 1.0f && c.lanterns[0][4] == 1.5f && c.lanterns[0][8] == 0.25f &&
               c.lanterns[1][0] == 115.9f && c.lanterns[1][8] == 0.23f);
    // No room: 33 sets of two lanterns fill 64; the 33rd is refused for room.
    std::vector<capture::LightSet> many(33, sets[0]);
    for (size_t i = 0; i < many.size(); ++i) many[i].lights[0].pos[1] = float(i);
    ClayFrameConstants d{}; d.light_on = 1; std::memcpy(d.sun_dir, c.sun_dir, sizeof(c.sun_dir));
    const LanternPlan p2 = MakeLanternBuffer(d, many);
    CHECK(871, p2.sets_drawn == 32 && p2.sets_refused_room == 1 && p2.lanterns == 64 && p2.first[15] == 30);
    // A frame without light: nothing is written, every set refused for the sun.
    ClayFrameConstants e{};
    CHECK(872, MakeLanternBuffer(e, sets).sets_drawn == 0 && e.lanterns[0][0] == 0.0f);
  }
```
`test_native_shaders.cpp` frame layout test (line 2071 on): the static_assert becomes `kClayFrameSize == 9440`; the fog fields' end check becomes `kClayFrameFogFields[13].offset + kClayFrameFogFields[13].bytes == kClayFrameLanternSize` with a new `kClayFrameLanternFields[] = {{"f_lanterns", 5344, 4096}}` appended to the `frame_with_fog` vector and checked against the reflected cbuffer of the lit pixel programs; the vertex shader's reflected cbuffer `Frame` has `f_fog_pad` (5232 bytes at 112) and `f_lanterns` (4096 at 5344): add a check that `kClayVs`'s reflection of `Frame` holds `f_lanterns` at 5344.

- [ ] **Step 2: Run to see them fail**

Compile; expected: unknown `LightModeLanterns`, `MakeLanternBuffer`, `lanterns` member.

- [ ] **Step 3: Implement**

`clay_logic.h` near the mode bits (after `LightModeWind`):
```cpp
// The local lights (local lights design, 2026-10-13). kLightModeLanterns (with kLightModeLit; read by
// the lit pixel paths and, for a leaf, the vertex path): the draw has lanterns in the frame's buffer
// (ClayFrameConstants::lanterns), the range in the word's upper bits: the first lantern's index in
// bits 16 to 21 (0 to 63) and the count in bits 22 to 24 (1 to 6). A range that is empty or past the
// buffer gives 0 (no bit: the draw is lit by the sun alone), so no shader loop can read past it.
inline constexpr uint32_t kLightModeLanterns = 2048u, kLightModeLanternFirstShift = 16, kLightModeLanternCountShift = 22;
inline constexpr uint32_t kMaxFrameLanterns = 64;
static_assert((kLightModeLanterns & (kLightModeCount - 1 + kLightModeUnlitFog + kLightModeUnlitFogAlpha + kLightModeLitNoFog +
                                     kLightModeUnlitFogTree + kLightModeWindMask)) == 0 &&
              (63u << kLightModeLanternFirstShift) < (1u << kLightModeLanternCountShift) && (7u << kLightModeLanternCountShift) < (1u << 25));
constexpr uint32_t LightModeLanterns(uint32_t first, uint32_t count) {
  if (count == 0 || count > capture::kMaxLightSetLanterns || first >= kMaxFrameLanterns || first + count > kMaxFrameLanterns) return 0u;
  return kLightModeLanterns | (first << kLightModeLanternFirstShift) | (count << kLightModeLanternCountShift);
}
```
`ClayFrameConstants`: after `transmit`: `float lanterns[kMaxFrameLanterns][16];  // the frame's lanterns (capture::LocalLight, four float4 each): MakeLanternBuffer`; `kClayFrameLanternBytes = offsetof(ClayFrameConstants, lanterns)`; the static_asserts: `sizeof == 9440`, `kClayFrameLanternBytes == 5344`; `kClayFrameSlots == 37`. After `MakeClayFrameConstants`:
```cpp
// The frame's lanterns from its light sets (local lights): a set is drawn when it is valid and its sun
// is the frame's (sun_dir bit-equal to the block's: the set and the block are the same frame's light),
// its lanterns copied in order while the buffer has room (kMaxFrameLanterns); plan.first/count[i] is set
// i's range (count 0: not drawn). Every lantern is copied as it is: MakeLightSet checked its values.
struct LanternPlan {
  uint16_t first[FrameBuilderMaxLightSets] = {};
  uint8_t count[FrameBuilderMaxLightSets] = {};
  uint32_t lanterns = 0, sets_drawn = 0, sets_refused_sun = 0, sets_refused_room = 0;
};
inline LanternPlan MakeLanternBuffer(ClayFrameConstants& c, const std::vector<capture::LightSet>& sets) {
  LanternPlan plan;
  std::memset(c.lanterns, 0, sizeof(c.lanterns));
  for (size_t i = 0; i < sets.size() && i < FrameBuilderMaxLightSets; ++i) {
    const capture::LightSet& s = sets[i];
    if (!s.valid || s.count == 0) continue;
    if (!c.light_on || std::memcmp(s.sun_dir, c.sun_dir, sizeof(c.sun_dir)) != 0) { ++plan.sets_refused_sun; continue; }
    if (plan.lanterns + s.count > kMaxFrameLanterns) { ++plan.sets_refused_room; continue; }
    plan.first[i] = uint16_t(plan.lanterns);
    plan.count[i] = uint8_t(s.count);
    for (uint32_t j = 0; j < s.count; ++j) std::memcpy(c.lanterns[plan.lanterns++], &s.lights[j], sizeof(capture::LocalLight));
    ++plan.sets_drawn;
  }
  return plan;
}
```
(`FrameBuilderMaxLightSets` = 16: define it in `frame_scene.h` as `inline constexpr size_t kMaxFrameLightSets = 16` used by both the builder and here; `clay_logic.h` includes `frame_scene.h` or the constant goes to `light_block.h` as `kMaxFrameLightSets`. Put it in `light_block.h` beside `kMaxLightSetLanterns` and use it in both.)

`fable2_native_shaders.h`: `kClayFrameLanternFields[] = {{"f_lanterns", 5344, 4096}}`, `kClayFrameSize = 9440`, `kClayFrameLanternSize = 5344`. cbuffer `Frame`:
```hlsl
#ifdef CLAY_FOG
  ... (as it is)
  float4 f_lanterns[256];                // the frame's lanterns, four float4 each: (pos, w), (col, blend), (range, cone_cos, cone_scale, shadow), (axis, 0)
#elif defined(CLAY_LANTERNS_VS)
  float4 f_fog_pad[327];                 // the fog's tail, which the vertex shader does not read
  float4 f_lanterns[256];
#endif
```
The vertex shader text (`FABLE2_CLAY_VS_HLSL`) starts with `#define CLAY_LANTERNS_VS 1\n` so that its cbuffer reaches the lanterns (the leaf path, Task 7). Note the wind program (`kClayWindVs`) is built from the same text and inherits it. Update `kPinLight`'s cbuffer text in the test if the pin includes the cbuffer (it does: "cbuffer Frame and every light function"); the field tables are checked by reflection in the frame layout test (Step 1's edits).

- [ ] **Step 4: Run the suite**

Native suite: exit 0. The frame-layout test must see `f_lanterns` at 5344 in a lit pixel program and in the vertex program.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/clay_logic.h src/native/capture/light_block.h src/native/fable2_native_shaders.h tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp
git commit -m "Local lights: the frame's lantern buffer and the draw word's lantern range"
```

---

### Task 7: The lit paths: LocalLights in the pixel shaders and the leaf vertex path

**Files:**
- Modify: `src/native/fable2_native_shaders.h:262-322` (`LocalLight`, `LocalLights`; `LightTerrainLayer`, `LightObject`, `LightPlant`, `LightPlantLit` gain `float3 lights`; `LightLeaf` takes `float3 front, float3 back`), `:538-545` (`VsOut`: `float3 back : TEXCOORD7`), `:640-662` (the leaf block sums), `:803-880` (the textured pixel shader's lit paths), `:1128-1136` (the layer shader's lit path)
- Modify: `src/native/render/clay_logic.h:2639-2740` (the C++ mirrors gain `lights`; `LocalLight`, `LocalLights`), `:2825` (`LightLeaf` with sums)
- Test: `tests/native/test_clay_logic.cpp` (pinned numbers), `tests/test_light_check.py` (`PinnedForTheRenderer`: the lantern pins), `tests/native/test_native_shaders.cpp` (pins; a loop over the lantern bits)

**Interfaces:**
- Consumes: `f_lanterns`, `kLightModeLanterns` and the shifts (Task 6); `world` (Task 5).
- Produces (HLSL, statement for statement `light_formulas.local_light`):
```hlsl
float3 LocalLight(float3 P, float3 N, float4 pos_w, float4 col_blend, float4 terms, float3 axis, float mask, float facing);
float3 LocalLights(float3 P, float3 N, float mask, float facing, uint light_mode);   // the loop over the draw's range; (0,0,0) without the bit
```
C++: `Float3 LocalLight(const Float3& P, const Float3& N, const capture::LocalLight& l, float mask, float facing)`, `Float3 LocalLights(const Float3& P, const Float3& N, const capture::LocalLight* lights, uint32_t count, float mask, float facing)`; `LightTerrainLayer(..., sun_gate, const Float3& lights = {})`, `LightObject(..., c49, const Float3& lights = {})`, `LightPlant(..., sun_gate, const Float3& lights = {})`, `LightPlantLit(..., sun_gate, const Float3& lights = {})`, `LightLeaf(A, vs_ambient, const Float3& front3, const Float3& back3, c27w, m1, m2, c49)`.

- [ ] **Step 1: Write the failing tests**

`tests/test_light_check.py`, in `PinnedForTheRenderer`, add a method that prints the numbers the C++ test pins (run it once, paste its output into the C++ case) and asserts the composition:
```python
    LANTERN = dict(pos=[95.0, 160.0, 44.0], w=1.0, col=[1.5, 0.75, 0.1], blend=1.0, range_scale=0.25,
                   axis=[0.0, 0.0, 0.0], cone_cos=-1.0, cone_scale=1.0, shadow=0.0)

    def test_lantern_pins(self):
        P, N = [93.0, 161.0, 42.0], lf.normalize([0.3, 0.1, 1.0])
        one = lf.local_light(P, N, **self.LANTERN)
        two = lf.local_lights(P, N, [self.LANTERN, dict(self.LANTERN, pos=[90.0, 158.0, 43.0], shadow=0.5)], mask=0.5, facing=0.75)
        obj = lf.lit_object(self.A, [0.5, 0.5, 0.5], self.TINT, self.TINT_POW, self.ADD, self.FLOOR, 0.5, 0.25, self.SUN_COL,
                            c49=[1.0, 1.0, 1.0], lights=two)
        print("LANTERN PINS", repr(one), repr(two), repr(obj))        # the C++ case 873 holds these
        self.assertTrue(all(x > 0.0 for x in one))
```
`test_clay_logic.cpp` case 873-875: `LocalLight` / `LocalLights` / `LightObject(..., lights)` on the same inputs equal the printed numbers to 1e-6 relative (use the file's `close` helper or `std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b))`), plus `finite3(LocalLights(...))` on NaN and infinite inputs (case 876) and `LightLeaf` with sums equal to the old leaf case's number when `front3 = sun_col * front`, `back3 = sun_col * back` (case 877: rewrite the existing leaf cases at 8349 and 8422 to the new signature with the same expected numbers).

`test_native_shaders.cpp`: after the loop at line 2730, a loop `for (unsigned first = 0; first < 64; first += 21) for (unsigned count = 1; count <= 6; ++count)` compiling the textured pixel shader and the layer shader with `WithLightMode(src, 4 | kLightModeLanterns | first << 16 | count << 22)` (lit + lanterns; and with the object bits 2, 16, 64 for the three mesh forms): each compiles, reads `cb2` beyond the post word, and its listing contains `f_lanterns` reads inside a loop or unrolled `count` times (count `dp3` instructions grows with `count`); and with `light_mode = 4` alone (no bit) the listing has no read of cb2 registers at or past `5344 / 16 = 334`. Update the four pins (`kPinLight`, `kPinVsMain`, `kPinTexturedMain`, `kPinLayerMain`) to the new text verbatim.

- [ ] **Step 2: Run to see them fail**

Python: the pins method errors (`lights` unknown? no: Task 1 added it; the method passes but prints). C++: unknown `LocalLight`; the pins fail.

- [ ] **Step 3: Implement**

HLSL, in the light block after `light_sun`:
```hlsl
// One local light (light_formulas.local_light): pos_w = (c(80+j).xyz, w), col_blend = (c(87+j).xyz, c94[j]),
// terms = (c96[j], c105[j], c107[j], c112[j]), axis = c(98+j).xyz.
float3 LocalLight(float3 P, float3 N, float4 pos_w, float4 col_blend, float4 terms, float3 axis, float mask, float facing) {
  float3 v = pos_w.xyz - P * pos_w.w;
  float d2 = dot(v, v);
  float d = (d2 > 0.0 && d2 < 3.0e38) ? sqrt(d2) : 0.0;
  float3 l = light_normalize(v);
  float n_dot = saturate(dot(N, l));
  float shadow = (1.0 - saturate(facing) * saturate(mask)) * terms.w;
  float rng = saturate(1.0 - d * terms.x);
  float cone = saturate((dot(l, axis) - terms.y) * terms.z);
  float gate = (1.0 - shadow) * (1.0 + col_blend.w * (cone * rng - 1.0));
  return col_blend.xyz * (n_dot * gate);
}
// The sum over the draw's lanterns (light_formulas.local_lights): the range in the draw word's upper
// bits (kLightModeLanterns, LightModeLanterns in clay_logic.h); nothing without the bit.
float3 LocalLights(float3 P, float3 N, float mask, float facing, uint light_mode) {
  float3 sum = float3(0.0, 0.0, 0.0);
  [branch] if (light_mode & 2048) {
    uint first = (light_mode >> 16) & 63;
    uint count = (light_mode >> 22) & 7;
    [loop] for (uint j = 0; j < count; ++j) {
      uint at = (first + j) * 4;
      sum += LocalLight(P, N, f_lanterns[at], f_lanterns[at + 1], f_lanterns[at + 2], f_lanterns[at + 3].xyz, mask, facing);
    }
  }
  return sum;
}
```
`f_lanterns` must be declared before the light block in every program that includes it: the light block is included by the vertex shader and the lit pixel programs (both declare it: Task 6); the depth-only and shadow programs include the light block too? Check which programs include `FABLE2_CLAY_LIGHT_HLSL`; if one declares no `f_lanterns`, wrap `LocalLights`' body in `#if defined(CLAY_FOG) || defined(CLAY_LANTERNS_VS)` with `return sum;` otherwise.

Add the `lights` argument: `LightTerrainLayer(..., float sun_gate, float3 lights) { return A * (c11x * light_ambient(...) + light_sun(...) + lights); }`; `LightObject(..., float3 c49, float3 lights) { return A * (c27w * light_ambient(...) + light_sun(...) + lights) * c49; }`; `LightPlant(..., float sun_gate, float3 lights) { return A * (vs_ambient + light_sun(...) + lights); }`; `LightPlantLit(..., float sun_gate, float3 lights)` likewise. `LightLeaf(float3 A, float3 vs_ambient, float3 front3, float3 back3, float c27w, float m1, float m2, float3 c49)`: `(A * (vs_ambient * c27w + m1 * front3) + green * A * m2 * back3) * c49` with `green` as it is.

The callers: the layer shader: `lin = LightTerrainLayer(..., SunGateSingle(mask, facing), LocalLights(world, N, mask, facing, light_mode));`; the object path: `LightObject(..., o_c49, LocalLights(world, N, mask, 1.0, light_mode))`; the plant paths: `LocalLights(world, Np, mask, 1.0, light_mode)`; the far terrain: no change (no multi form). The leaf pixel path: `LightLeaf(clay_linear(t.rgb), vcolour, bend.xyz * m.x ... ` — no: pass `bend.xyz` and `back` and let `LightLeaf` apply `m1`, `m2`: `LightLeaf(clay_linear(t.rgb), vcolour, bend.xyz, back, o_c27w, m.x, m.y, o_c49)`.

`VsOut`: add `float3 back : TEXCOORD7;` after `bend`; every pixel `main` of the textured shaders adds the parameter `float3 back : TEXCOORD7` (the three mains at 803, 898 and the depth variant; `clay_textured` gains it). The vertex shader sets `o.back = float3(0, 0, 0)` with the other defaults. The leaf block:
```hlsl
    float3 view = light_normalize(f_eye - o.world);
    float2 sun_fb = LeafLight(N, view, light_normalize(f_sun_dir), o_leaf.x, 1.0);
    float3 front3 = f_sun_col * sun_fb.x, back3 = f_sun_col * sun_fb.y;
    [branch] if (light_mode & 2048) {
      uint first = (light_mode >> 16) & 63;
      uint count = (light_mode >> 22) & 7;
      [loop] for (uint j = 0; j < count; ++j) {
        uint at = (first + j) * 4;
        float4 pos_w = f_lanterns[at], col_blend = f_lanterns[at + 1], terms = f_lanterns[at + 2];
        float3 v = pos_w.xyz - o.world * pos_w.w;
        float d2 = dot(v, v);
        float d = (d2 > 0.0 && d2 < 3.0e38) ? sqrt(d2) : 0.0;
        float3 ld = light_normalize(v);
        float rng = saturate(1.0 - d * terms.x);
        float cone = saturate((dot(ld, f_lanterns[at + 3].xyz) - terms.y) * terms.z);
        float spot = 1.0 + col_blend.w * (cone * rng - 1.0);
        float2 fb = LeafLight(N, view, ld, o_leaf.x, spot);
        front3 += col_blend.xyz * fb.x;
        back3 += col_blend.xyz * fb.y;
      }
    }
    o.bend = float4(front3, 0.0);
    o.back = back3;
```
(the leaf's vertex form: `leaf_light` with `spot = 1 + c90[j] * (cone * range - 1)`, the vertex set being the pixel set four registers lower: spec ruling 5).

C++ mirrors in `clay_logic.h`, beside `LightObject`:
```cpp
inline Float3 LocalLight(const Float3& P, const Float3& N, const capture::LocalLight& l, float mask, float facing) {
  const Float3 v{LightFinite(l.pos[0] - P.x * l.w), LightFinite(l.pos[1] - P.y * l.w), LightFinite(l.pos[2] - P.z * l.w)};
  const float d2 = LightFinite(v.x * v.x + v.y * v.y + v.z * v.z);
  const float d = (d2 > 0.0f && d2 < 3.0e38f) ? std::sqrt(d2) : 0.0f;
  const Float3 u = LightNormalize(v);
  const float n_dot = LightSat(u.x * N.x + u.y * N.y + u.z * N.z);
  const float shadow = (1.0f - LightSat(facing) * LightSat(mask)) * LightFinite(l.shadow);
  const float range = LightSat(1.0f - d * LightFinite(l.range));
  const float cone = LightSat((u.x * l.axis[0] + u.y * l.axis[1] + u.z * l.axis[2] - LightFinite(l.cone_cos)) * LightFinite(l.cone_scale));
  const float gate = LightFinite((1.0f - shadow) * (1.0f + LightFinite(l.blend) * (cone * range - 1.0f)));
  return {LightFinite(l.col[0] * n_dot * gate), LightFinite(l.col[1] * n_dot * gate), LightFinite(l.col[2] * n_dot * gate)};
}
inline Float3 LocalLights(const Float3& P, const Float3& N, const capture::LocalLight* lights, uint32_t count, float mask, float facing) {
  Float3 sum{0.0f, 0.0f, 0.0f};
  for (uint32_t j = 0; j < count && lights; ++j) {
    const Float3 t = LocalLight(P, N, lights[j], mask, facing);
    sum = {LightFinite(sum.x + t.x), LightFinite(sum.y + t.y), LightFinite(sum.z + t.z)};
  }
  return sum;
}
```
(use the file's existing helpers: grep `LightFinite`, `LightSat`, `LightNormalize` in `clay_logic.h` and use their actual names.) The family mirrors gain the trailing `lights` argument with a default of `{0, 0, 0}` added beside the sun term exactly as the HLSL does; `LightLeaf` takes the two sums (update its two existing callers in the tests).

- [ ] **Step 4: Run both suites**

Python: `python -m unittest tests.test_light_check -v` prints `LANTERN PINS ...` once: paste into case 873. Native suite: exit 0, including the new lantern-bit loop and the updated pins.

- [ ] **Step 5: Commit**

```bash
git add src/native/fable2_native_shaders.h src/native/render/clay_logic.h tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp tests/test_light_check.py
git commit -m "Local lights: LocalLights in the lit pixel paths and the leaf vertex sums"
```

---

### Task 8: The pass wiring, the switch and the statistics

**Files:**
- Modify: `src/native/render/clay_pass.cpp:3853-3860` (the frame's lanterns after `MakeClayFrameConstants`), `:1418-1430` (the draw's lantern bits), the reflection draw path (grep `reflection_lights` at 2712: no bits there), the frame slots' upload (the code that copies `frame_` into the buffer's head: grep `kClayFrameSlots` at 884 and 5111)
- Modify: `src/native/render/clay_pass.h:147` (`Render(..., bool local_lights, ...)`), the `Prepared` struct (`lanterns` word per draw)
- Modify: `src/native/render/clay_logic.h:510-670` (`ClayStats`: `lanterns_sets`, `lanterns_refused_sun`, `lanterns_refused_room`, `lanterns_draws`), `:8940` (`FormatLightDrawnText` adds `; lanterns: sets N, draws M (refused: sun a, room b)`)
- Modify: `src/native/fable2_native_render.cpp:115` (`REXCVAR_DEFINE_STRING(fable2_native_local_lights, "on", ...)`), `:464` (`CurrentLocalLights()`), `:896` (`Render` call), `:1270-1280` (the start-up log line), `:1379` (`capture::SetLocalLightsWanted(wanted && CurrentLocalLights())`)
- Test: `tests/native/test_clay_logic.cpp` (`FormatLightDrawnText` with lantern counts: grep its existing case and extend)

**Interfaces:**
- Consumes: `MakeLanternBuffer`, `LightModeLanterns`, `LightSetOf` (Tasks 4, 6); `SetLocalLightsWanted` (Task 4).
- Produces: `--fable2_native_local_lights=on|off`; the F3/log line `Light inputs: ... ; lanterns: sets N, draws M (refused: sun a, room b)`; `ClayPass::Render(..., bool local_lights, ...)`.

- [ ] **Step 1: Write the failing test**

`test_clay_logic.cpp`: the existing `FormatLightDrawnText` case (grep it): set `st.lanterns_sets = 2; st.lanterns_draws = 37; st.lanterns_refused_sun = 1; st.lanterns_refused_room = 0;` and check the text ends with `; lanterns: sets 2, draws 37 (refused: sun 1)` (zero counts left out; with all four zero and the switch on, `; lanterns: none`; the stats carry `lanterns_on` so that with the switch off nothing is appended).

- [ ] **Step 2: Run to see it fail** (unknown members).

- [ ] **Step 3: Implement**

`clay_logic.h` `ClayStats`: `bool lanterns_on = false; uint32_t lanterns_sets = 0, lanterns_draws = 0, lanterns_refused_sun = 0, lanterns_refused_room = 0;` and the formatting in `FormatLightDrawnText`.

`clay_pass.cpp`: in `Render`'s frame preparation after `frame_ = MakeClayFrameConstants(scene.light, light); SetClayFrameMask(...)`:
```cpp
  lantern_plan_ = LanternPlan{};
  if (local_lights && frame_.light_on) {
    lantern_plan_ = MakeLanternBuffer(frame_, scene.light_sets);
    st.lanterns_sets = lantern_plan_.sets_drawn; st.lanterns_refused_sun = lantern_plan_.sets_refused_sun;
    st.lanterns_refused_room = lantern_plan_.sets_refused_room;
  } else {
    std::memset(frame_.lanterns, 0, sizeof(frame_.lanterns));
  }
  st.lanterns_on = local_lights;
```
(`lantern_plan_` a member `LanternPlan` of the pass, `local_lights` the new `Render` parameter.) The frame's constants are uploaded whole (`sizeof(ClayFrameConstants)`, 37 slots now): check the upload code at 884/5111 uses `sizeof` / `kClayFrameSlots` and not a literal.

Per draw (1418 on), inside `if (p.lit)` after the mode is chosen and only for the main scene's draws (the reflection path passes a flag or the `reflection_height` pointer: no bits when `reflection_height != nullptr`):
```cpp
      if (lantern_plan_.lanterns && p.light && !reflection_height) {
        const uint16_t set = p.light->light_set;
        if (set != capture::kNoSideIndex && set < capture::kMaxFrameLightSets && lantern_plan_.count[set]) {
          const uint32_t bits = LightModeLanterns(lantern_plan_.first[set], lantern_plan_.count[set]);
          light_mode |= bits;
          if (bits) ++st.lanterns_draws;
        }
      }
```
(`p.light` is the record's `LightInputs*`; the terrain's prepared draw holds its light inputs under another name: grep `fog_light` / `light` in `PrepareTerrain` and use the terrain record's inputs through `LightOf(scene, r)`.) The depth pre-pass draws use the same word: harmless (the depth-only programs do not read the bit), but keep the pre-pass's word without the bits to leave its programs byte-identical (set the bits after the pre-pass word is taken, or mask them off there).

`clay_pass.h`: `Render(..., bool local_lights, ...)` beside `wind`; `fable2_native_render.cpp`: the cvar (text in the style of `fable2_native_wind`: "Native local lights (lanterns and torches of the two-, four- and seven-light shaders; local lights design 2026-10-13): on (the default) = the capture reads each multi-light draw's light set once per upload and the native view adds the lanterns before the set's sun to its lit draws; off = none of it is read or drawn, the picture of before"), `CurrentLocalLights()` as `CurrentWind()` with the default on, the `Render` argument, the start-up `[native] local lights: on|off (...)` line beside the wind's, and `capture::SetLocalLightsWanted(wanted && CurrentLocalLights())` beside `SetWindWanted`.

- [ ] **Step 4: Build the game and run the suite**

Native suite: exit 0. Then the worktree build (first time: configure as `docs/superpowers/plans/2026-10-07-boot-save.md` Task 2 says, with the SDK roots of the main checkout; stage content with hard links; copy `data\dir.manifest` from the release dir). Build: `cmake --build out\build\win-amd64-release --target fable_2 2>&1 | Select-Object -Last 3`. Expected: 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/clay_pass.cpp src/native/render/clay_pass.h src/native/render/clay_logic.h src/native/fable2_native_render.cpp tests/native/test_clay_logic.cpp
git commit -m "Local lights: the pass draws each multi-light record with its set's lanterns; the switch"
```

---

### Task 9: Validation in the game

**Files:**
- Create: `out/native-evidence/lighting/lanterns/` (runs, shots, captures, `lantern_regions.py`, `lantern_regions.txt`, `timing.txt`) — untracked evidence
- Create: `tools/xdk_sigmatch/lantern_regions.py` (tracked: the measure)
- Test: `tests/test_lantern_regions.py`

**Interfaces:**
- Produces: `lantern_regions.py --native <f2ns> --game <game scene> --view <capture row json> --lanterns "x,y,z;x,y,z" --radius 2.0` printing, per lantern, the disc's mean linear colour of both scenes and native/game per channel; a `--self-test` that runs the measure on a synthetic pair.

- [ ] **Step 1: The measure and its test**

Write `lantern_regions.py` on the stored-scene readers the water and distant-trees closings used (`frame-map.md` section 19 "Validation (12)": the `.f2ns` reader and the game's stored linear scene reader live in `tools/xdk_sigmatch` as `scene_dump.py` / the region scripts named there: grep `f2ns` in `tools/xdk_sigmatch/*.py` and reuse the reader). Projection: the capture row's view-projection (the frame's `c0..c3` of the terrain rows, as `region_compare.py` takes them) maps the lantern's ground point (its position with z lowered by 0.5) to a pixel; the disc of `radius` units is `radius * (height_px / 2) / (w * tan(fov/2))` pixels, with w the clip w of the point; mean over the disc in linear space, both scenes, ratio per channel. `tests/test_lantern_regions.py`: a synthetic native and game array (numpy) with a brighter disc at a known pixel: the ratio is 1 where both have the disc, 0.5 when the native one is half. Commit:
```bash
git add tools/xdk_sigmatch/lantern_regions.py tests/test_lantern_regions.py
git commit -m "Local lights: the lantern disc measure on paired scenes"
```

- [ ] **Step 2: Software adapter, debug layer**

Announce the run to session `fable-2-recomp-c8` (SendMessage), check `tools/idle_check.ps1 -MinIdleSeconds 300` or the user's standing leave for this sub-project's runs (ask once if neither holds). Run the worktree's exe through `tools/drive_game.ps1 -BootSave Hero000 -Total 60` with the software adapter and the debug layer switches the water closing used (`frame-map.md` section 20, "Validation (13)", the exact `-GameArgs`), once with `--fable2_native_local_lights=on` and once `off`. Expected: `0 errors` in both logs; the log's `[native] local lights:` line; the `Light inputs:` line shows `lanterns: sets N, draws M` with N >= 1 at the bridge (the two lanterns) in the `on` run.

- [ ] **Step 3: The bridge captures**

Real GPU, `-BootSave Hero000`, `-Total 60 -Shots "36,50"`, `-Env @{FABLE2_NATIVE_DISCOVERY="2"; FABLE2_NATIVE_DISCOVERY_EVERY="1"; FABLE2_NATIVE_DISCOVERY_DELAY="40"}` with `--fable2_native_scene_dump=on` and the game's stored scene read-back as the water closing did (same `-GameArgs`), once `on` and once `off`. From each: the `.f2ns` native scene, the game's scene of the same frame, the capture row of a terrain draw (its `c0..c3`), and the frame's lanterns from the discovery rows' `"lset"` (the two positions and colours). Run `lantern_regions.py` on both pairs with those positions, radius 2. Expected (criterion 2): native/game at each disc in [0.8, 1.25] with `on`, lower with `off`; the frame-wide measures of `region_compare.py` outside the discs within their spread. Write `lantern_regions.txt` with both runs' numbers and the shots' names.

- [ ] **Step 4: Cost**

`tools/perf/ab_runs.ps1` as the water closing used it (section 20, the cost paragraph): `on` against `off`, the usual window; expected within +0.5 ms a frame, or the number stated. `timing.txt`.

- [ ] **Step 5: Record**

Append the run numbers, the measures and any `DISTURBED` runs to `out/native-evidence/lighting/lanterns/README.txt`. No commit (evidence is untracked); the documents of Task 10 cite the files.

---

### Task 10: Documents, user checks, closing

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (section 14, "Native light": a new subsection "Local lights" after "The two- and four-light forms"; the "Light inputs" `lset` row field; the `Light inputs:` line's lanterns part)
- Modify: `docs/native-renderer/lighting-formulas.md` (the multi form: the per-slot statement, `local_light`, the leaves' sums; the families' `lights`)
- Modify: `docs/native-renderer/user-checks.md` (checks 32a, 38, 59 re-read; new pending checks: a street lamp at night, a torch indoors, the campfire after the merge)
- Modify: `docs/native-renderer/roadmap.md` ("Post-closure local lights (2026-10-13)" after the water ripples entry; the seven-light pairs as a follow-up waiting on the campfire commit)
- Modify: `docs/superpowers/specs/2026-10-07-native-renderer-lighting-design.md` (ruling 5: "superseded 2026-10-13 by the local lights follow-up")
- Modify: `docs/superpowers/specs/2026-10-13-native-renderer-local-lights-design.md` ("Implementation notes": what was built differently, the rulings made during the work, the criteria's verdicts)
- Modify: `README.md` (the switch in the native renderer's switch list, if the list is there: grep `fable2_native_wind` in README.md)

- [ ] **Step 1: Write the documents** with the numbers of Task 9 (every figure cited by file), the limits (reflections, the swayed position, specular, the per-light shadow map, the seven-light pairs pending, slots after the sun not read), and the user checks with exact commands.

- [ ] **Step 2: Run both suites once more** (exit 0, `OK`) and `python tools/xdk_sigmatch/light_check.py --multi-lanterns --trials 400` into the evidence file again if the checker changed after Task 2.

- [ ] **Step 3: Commit**

```bash
git add docs README.md
git commit -m "Local lights closing: the documents, the user checks, ruling 5 superseded"
```

- [ ] **Step 4: Whole-branch review and merge**

Use `superpowers:requesting-code-review` on the branch's diff against `00d0d29`; fix what it finds (each fix its own commit); then `superpowers:finishing-a-development-branch`: merge locally into `native-renderer-water` only if that branch's owner session has committed its work (ask through SendMessage); otherwise leave the branch and say so in the final summary. Never push.
