# Native Renderer Shadows (sub-project 8) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The native view shadows its sun light where the game does, from the game's own casters, light matrices and mask arithmetic.

**Architecture:** The game's two shadow passes are captured as record lists of their own with a per-frame shadow block (two world-to-map matrices, split distances, bias). The clay pass draws them depth-only into two 1024 x 1024 maps, runs a depth pre-pass of the scene, computes a screen mask in one full-screen step with the mask shader's checked arithmetic, and the lit shaders multiply their sun terms by the mask sampled at their pixel.

**Tech Stack:** C++20 (clang, pure headers under `src/native` with native tests), HLSL through the SDK's D3D12 RHI (`thirdparty/rexglue-sdk/include/rex/graphics/native_rhi.h`: one colour target, no pipeline depth bias, `Barrier`, `kD32_FLOAT` viewed as `R32_FLOAT`, `kR8_UNORM`, texture pairs), Python 3.12 tools under `tools/xdk_sigmatch` with `unittest`, PowerShell driver `tools/drive_game.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-08-native-renderer-shadows-design.md`. Facts about the game's shadows come from `.superpowers/sdd/shadows-research.md` (untracked) and `docs/native-renderer/frame-map.md` section 14. Statements of the research note marked NEW were read once by scripts: Tasks 1 and 2 check them before anything is built on them.

## Global Constraints

- Branch `native-renderer-shadows`. Never push, stash, reset or switch branches. Never stage `fable_2_manifest.toml`; never `git add -A`. Commit trailer: the two lines `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`, exactly.
- Environment: PATH begins with `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin`, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin`. Game build: `& .\build.cmd -release fable_2` (no `fable_2.exe` running; never kill one you did not start). Python tests: `python -m unittest discover -s tests -p "test_*.py"`. Native tests: see the workspace's implementer rules, section "Antivirus on this machine" (test executables only in `out\tests\runtime-unit-tests\`; the shader test under another output name through a scratch copy of the runner while the locked file exists; any binary removed, locked or refused: stop and report).
- SDK changes (`thirdparty/rexglue-sdk`): only the temporary diagnostic of Task 1; no `;` comment lines in `rexruntime.def`; always pass `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` to `build_runtime_sdk.cmd`; the diagnostic is removed and the SDK rebuilt before Task 1's last commit.
- The game is started only through `tools/drive_game.ps1`. Agents never pass `-UserPresent`: when the idle gate refuses, report NEEDS_RUN with the exact command lines; the controller makes the run on the user's permission and such evidence is labelled `RUN UNCHECKED`. Start with `-BootSave Hero000` (world from about 23 s, views from 32 s, captures from 33 s). Every run is checked window by window for the freeze signature (23 to 26 fps, work about 40 ms, identical draw counts, 0 uploads) before any number or picture is used.
- A build that changes bindings, targets or shader text is not run on the real GPU before one clean 240 s run on the software adapter with the debug layer (`--d3d12_adapter=-2 --d3d12_debug=true --fable2_native_d3d_messages=true`, `-Total 240 -Shots "225"`, below-normal priority; the controller makes it and warns the user first). Timing figures only from runs on a quiet machine.
- Evidence rule: every number names its log, capture or command output; inference is marked; nothing is "verified" without evidence; screenshots relied on are viewed and described honestly. Evidence goes to `out/native-evidence/shadows/`.
- Reads of guest memory and of captured data are bounded and fail closed (a shadow draw that cannot be read is skipped and counted, never drawn from guessed data). No value read from the guest reaches a shader without a finiteness check.
- `DrawRecord` does not grow (616 bytes). View-off capture median stays under 0.5 ms; nothing is added outside the records gate; shadow records are taken only with the native view, the light and the shadows on.
- Nothing may assume the game's resolution: maps are a constant 1024, screen addressing is normalised to the native target (spec ruling 4).
- Never put Windows backslash paths inside inline Python or heredoc strings; scan edited docs for control characters; keep each file's line endings. `*.ps1` is git-ignored: a new one needs `git add -f <file>`.
- Reason strings added by this plan: `shadow-block` (the frame's shadow block was not read or failed its proof), `shadow-view` (no single inverse view-projection for the frame), `shadow-pass-extra` (a third or later shadow pass in a frame).

## Review Focus

1. A frame without a valid shadow block (menus, loading, the first frames, night without a sun, a frame the view was switched on in after the shadow passes): mask 1 for that frame, `shadow-block` counted; never a stale map or another frame's matrix.
2. A non-finite or singular matrix (inverse of the world rows, inverse view-projection), a zero `split`, a division by `c39 . P` near zero: every inverse is guarded, the mask arithmetic is finite for any depth including the clear value, and a test feeds zeros and non-finite values.
3. The pre-pass and the colour pass disagreeing in depth (alpha-tested, skinned, instanced, terrain): no draw disappears or z-fights with shadows on; shadows off is pixel-equal to the lighting picture.
4. The maps, the mask target and the white fallback across view switches (`VIEW=` tokens), window resize and device loss: recreated with the pass's other targets; the mask is never sampled while bound as a target, the depth never while bound for writing (debug layer clean).
5. Shadow passes of unusual shape: more than two passes in a frame, a pass with zero readable draws, a pass whose draws use a vertex shader without a table entry, more shadow records than the list reserves: counted by reason and shader, the frame still drawn.

---

### Task 1: Discovery of the mask draws and the bias

**Files:**
- Modify: `src/native/capture/capture.cpp`, `src/native/capture/xdk_layout.h`, `src/native/capture/discovery_format.h`, `src/native/capture/render_state.h`, `tests/native/test_discovery_format.cpp`, `tests/native/test_render_state.cpp`, `tools/xdk_sigmatch/render_state_check.py`, `tests/test_render_state_check.py`, `thirdparty/rexglue-sdk` (temporary diagnostic, removed at the end), `docs/native-renderer/frame-map.md` (new section 15 "Shadows", subsections "Passes", "Mask draws", "Bias")
- Create: `tools/xdk_sigmatch/shadow_matrices.py`, `tests/test_shadow_matrices.py`

**Interfaces:**
- Produces:
  - Discovery rows for `BeginVertices` draws (hook `kHook_D3DDevice_BeginVertices`, emitter `0x822060A0`): `"type":"verts"` with the shader pair, `"tf"`, `"pfull"`, `"rs"` (discovery only; nothing recorded).
  - `"rs"` gains `"2380".."2383"` (polygon offset front/back scale and offset) for every decoded row, and the stencil reference word; `render_state_check.py` compares them with the GPU log.
  - `shadow_matrices.py <capture.jsonl>`: per frame and pass `M = [c0..c3] * inverse([c4..c6; 0 0 0 1])` (median over the pass's rows, largest deviation), the texture-space mapping (`u = 0.5 x + 0.5`, `v = -0.5 y + 0.5`, the half texel) against `c36..c38` / `c40..c42` of the frame's scene rows, and the difference of the map origin between consecutive frames in texels; exit 0 only when every pass agrees within 1e-3. (The scratch scripts `lvp.py`, `more.py` in `out/tests/shadows-research/` are the starting point.)
  - Frame-map section 15 with: the pass list as rows show it; tf6, tf7, tf13, tf14 of the mask draws (address, format, size, filter, whether the pixels are in guest memory); `c12`, `c13`, `c46`, `c47`, `c77` at both rectangles over all frames; the rectangles' state (depth word, stencil, alpha test) and which pixels each covers; the literals `c248..c255` of `0x0F99810F072BF0FC` and `0x1B4B5AE12EF270CA`; the bias values per shadow shader; whether the map origin snaps.

- [ ] **Step 1: Offline first (no run).** Decode the literal tables of the two mask shaders from `out/shader_dump/shader_<hash>.ucode.bin.frag` (the layout `ucode_eval.py` / the SDK's dump writer use; state it) and mine `out/native-evidence/lighting/task1_*_gpu_pconst.log.gz` for `c12`, `c13`, `c46`, `c47`, `c77`, `c36..c43`, `c116..c119` at the two mask draws of every frame they hold. Write both into `out/native-evidence/shadows/task1_offline.txt`.
- [ ] **Step 2: `shadow_matrices.py` with tests first** (synthetic rows: an exact pass passes; a row with a wrong matrix is named; a singular world block is skipped and counted; the half texel in either sign is accepted and reported). Run it on the three existing captures named in the research note.
- [ ] **Step 3: Rows.** Tests first in `test_discovery_format.cpp` / `test_render_state.cpp`: the new row type, the four offset registers and the stencil reference in `"rs"`. Find the registers' place in the device's shadow state by the method of materials Task 2 (frame-map section 13 "Render state"); a temporary SDK diagnostic logs the GPU's `0x2380..0x2383`, the stencil reference and the fetch constants at `DRAW_INDX` packets of pitch 1040 and at the two rectangles (the removed patch `task1_sdk_gpu_pconst_log.patch` in `out/native-evidence/lighting/` is the pattern).
- [ ] **Step 4: Captures** (gated driver; NEEDS_RUN with the exact commands if the gate refuses twice): bridge (`-Total 75 -Shots "36,70"`, discovery 8 frames every frame, delay 40), trees (the trees plan, delay 60, and `DISC=8/1` again while walking), third spot. The diagnostic armed for the same frames.
- [ ] **Step 5: Compare and write.** `render_state_check.py` for the new words (count compared, equal, different); `shadow_matrices.py` on every capture; section 15. If the bias registers cannot be read from the guest, state what was tried and give the GPU's values as constants per shader with their evidence (the controller rules on the fallback).
- [ ] **Step 6: Remove the diagnostic**, rebuild SDK and game, suites green.
- [ ] **Step 7: Commit** — "Shadows discovery: mask draws decoded, polygon offset in the render state, light matrices checked".

---

### Task 2: Mask formulas and caster tables (offline; can run beside Task 1)

**Files:**
- Modify: `tools/xdk_sigmatch/ucode_eval.py`, `tools/xdk_sigmatch/light_formulas.py`, `tools/xdk_sigmatch/light_check.py`, `tests/test_ucode_eval.py`, `tests/test_light_check.py`, `docs/native-renderer/vs-transforms.json`, `src/native/capture/vs_transform_table.inc` (generated), `docs/native-renderer/lighting-formulas.md` (new part "Shadows")
- Create: `docs/native-renderer/shadow-casters.json`, `tools/xdk_sigmatch/gen_caster_table.py`, `tests/test_gen_caster_table.py`, `src/native/capture/shadow_caster_table.inc` (generated)

**Interfaces:**
- Produces (pure Python, inputs as plain lists and callables):

```python
def mask_near(P, eye_depth_cmp, near_rows, sample_near, rot, offsets, radius):
    """12 taps: uv = (rows[0] . P, rows[1] . P) / (rows[3] . P) + rotate(offsets[i], rot) * radius;
    lit when (1 - rows[2] . P / w) > sample_near(uv); returns lit count / 12."""
def mask_far(P, far_rows, sample_far_texel, texel):
    """4 point taps at texel offsets (-1,-1), (0,-1), (-1,0), (0,0) with the bilinear weights of
    getWeights2D (offset -0.5); compare >=; returns the weighted sum."""
def mask_combine(d, near, far, split, slope):
    """m = lerp(near, far, sat((d - split[0]) / split[2])); lerp(m, split[3], sat(slope * (d - split[1])))"""
def mask_0f99(...): ...   # the whole shader, branches as the microcode takes them
def mask_1b4b(...): ...
```

  The exact argument lists, compare directions and literal names are fixed by this task from the dumps and stated in `lighting-formulas.md`; the functions above are the minimum Task 5 mirrors in HLSL and C++.
  - `light_check.py --family mask-near|mask-far|mask` and every lit family with `mask` drawn at random in [0, 1] (it was fixed to 1): `terrain-layer`, `object`, the probe-colour forms, `plant`, `plant-lit`, `tree-leaf` (`m1 = 0.6 mask + 0.4`, `m2 = 0.6 m1 + 0.4`), the multi-light forms.
  - `shadow-casters.json`: one entry per shadow-pass shader pair `{vs, ps|null, kind: "opaque"|"alpha-test", alpha: {texture_slot, scale_register|null}, evidence}`; generator in the strict style of `gen_albedo_table.py` (unknown key, missing evidence, an alpha entry without a pixel shader: refused with the hash); macro `FABLE2_SHADOW_CASTER(vs, ps, kind, slot, scale_reg)`.
  - Vertex-table entries for `0xA8A5...`, `0x5781...`, `0x87E0...` (full hashes from the captures), each checked by `position_check.py` / `vs_check.py` on the existing captures; a shader that cannot be closed is listed with what is open and left out.

- [ ] **Step 1: Interpreter.** Tests first: `getWeights2D` on hand-computed fractions; a fetch with texel offsets; a predicated branch pair as `0x0F99...` uses (near branch taken / not taken). Implement in `ucode_eval.py`.
- [ ] **Step 2: Mask formulas.** Tests first (`tests/test_light_check.py`): a formula with a compare direction flipped, a tap offset wrong, the fade to 1 instead of `split[3]` each fail on a synthetic dump (always run) and on the real dumps (skip without `out/shader_dump`). Synthetic depth maps: a constant map (all lit, all shadowed), a half-plane edge (the penumbra's width equals the kernel). Implement; `light_check.py --family mask` passes for both shaders.
- [ ] **Step 3: Families with the mask.** Extend each family's check; no family's formula may change at mask 1 (the pinned numbers of lighting stay).
- [ ] **Step 4: Casters.** The table, the generator with tests, the three vertex entries; per caster pixel shader the alpha expression read from the dump with instruction numbers (`0xF1EB...`, `0x3347...`, `0x85A9...`: state exactly what the dissolve of `0x85A9...` compares; if it is not one comparison of constants it is left out and named as a limitation).
- [ ] **Step 5: `lighting-formulas.md` part "Shadows"**: the formulas with instruction numbers, inputs and their sources, the checker's output file names (in `out/native-evidence/shadows/task2_*`), what was switched off in each check and why.
- [ ] **Step 6: Commit** — "Shadows, offline: mask formulas checked against the microcode; caster tables".

---

### Task 3: Shadow passes in the capture

**Files:**
- Create: `src/native/capture/shadow_block.h`, `tests/native/test_shadow_block.cpp`
- Modify: `src/native/capture/capture.cpp`, `src/native/capture/main_scene.h`, `src/native/capture/draw_record.h`, `src/native/render/frame_scene.h`, `src/native/capture/discovery_format.h`, `tests/native/test_frame_scene.cpp`, `tests/native/test_main_scene.cpp`, `tests/native/test_draw_record.cpp`, `tests/run_native_tests.cmd`, `README.md`

**Interfaces:**
- Consumes: Task 1's findings (the bias words in `render_state.h`), Task 2's `shadow_caster_table.inc`.
- Produces:
  - `struct ShadowBlock { float world_to_map[2][4][4]; float split[4]; float texel[2]; bool valid; };` and `ShadowBlock MakeShadowBlock(const float (*reg)[4], const bool* have)` (pure): rows from pixel `c36..c39` and `c40..c43`, `split` = `c12`, `texel` = (`c13.x`, `c14.x`); valid only when every register was read and is finite, `split[0] > 0`, `split[1] > split[0]`, `split[2] > 0`, `split[3]` in [0, 1], `texel` in (0, 1].
  - `bool PassMatrix(const float c0_3[4][4], const float world[3][4], float out[4][4])` (pure; false when the world block is singular or non-finite) and `bool PassAgrees(const float pass[4][4], const float map_rows[4][4], float texel, float tol)` (pure: the texture-space mapping of Task 1, either sign of the half texel).
  - `enum class ShadowPass : uint8_t { kNone, kNear, kFar, kExtra }` and a pure classifier in `main_scene.h`: `ShadowPassTracker::Observe(surface_pitch, colour_mask, is_clear, in_scene)`; a frame's first run of pitch-1040, colour-mask-0 draws after a clear is `kNear`, the second `kFar`, later ones `kExtra`; reset at the frame boundary.
  - `FrameScene::shadow_draws[2]` (`std::vector<capture::DrawRecord>`, reserved, reused), with their side entries in the scene's existing side arrays or twins of them (state which and why), `FrameScene::shadow` (`ShadowBlock`), `shadow_skipped[2]` by reason, `shadow_skipped_by_vs`, `shadow_extra`. A shadow record carries the caster kind, the alpha reference and texture identity (alpha-test kinds), cull, and the bias words, in fields `DrawRecord` already has or in a side array (the record stays 616 bytes; `static_assert`).
  - Capture log line `shadow: block <valid|invalid(reason)>, near N (skipped {...}), far M (skipped {...}), extra K`.

- [ ] **Step 1: Failing tests**: `MakeShadowBlock` validity rules one by one; `PassMatrix` on a known matrix pair, a singular world block, a non-finite input; `PassAgrees` with the half texel in both signs and a wrong matrix; the tracker over a synthetic frame (two passes, three passes, a pass with no draws, a scene draw between, the frame boundary); scene reuse holds nothing of the previous frame; the reserve limit (a pass with more records than the limit: the rest counted, no growth past it).
- [ ] **Step 2: Run**: fail. **Step 3: Implement**; the per-frame proof (`PassAgrees` for each pass's first record with readable world rows; failure makes the block invalid, reason `shadow-block`).
- [ ] **Step 4: Verify** on runs at the three spots (NEEDS_RUN if refused twice): the `shadow:` line's counts against Task 1's pass list; skipped by shader; block valid in every window; view-off capture median unchanged; records-on median against the lighting figure on a quiet machine.
- [ ] **Step 5: Commit** — "Native capture: the two shadow passes as record lists, a proven shadow block per frame".

---

### Task 4: Native shadow maps

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/render/clay_logic.h`, `src/native/render/geometry_cache.cpp`, `src/native/render/composite.cpp`, `src/native/fable2_native_render.cpp` (cvar, view token), `tests/native/test_native_shaders.cpp`, `tests/native/test_clay_logic.cpp`, `README.md`

**Interfaces:**
- Consumes: `FrameScene::shadow_draws`, `FrameScene::shadow`, the caster kinds.
- Produces: two `kD32_FLOAT` targets `kShadowMapSize = 1024` (constant in `clay_logic.h`); depth-only pipelines for the static, skinned and instanced vertex paths with `world_to_map` in the place of the view-projection, the game's compare (state how the reversed range is kept or mapped, and test it), cull from the record, bias applied in the shaders (`ShadowBias(depth, slope, scale, offset)` pure mirror in `clay_logic.h`); alpha-test forms binding the caster's texture; the maps left in a shader-resource state; cvar `--fable2_native_shadows=on|off` (default `off` in this task); composite token `VIEW=shadow` showing both maps (and later the mask) for evidence shots; `Shadow:` stats line (spec Design 5).

- [ ] **Step 1: Failing tests**: `ShadowBias` and the depth mapping at the range ends; pipeline keys for the depth-only forms (count stated); shader text and reflection tests for the new shaders; the stats text.
- [ ] **Step 2: Run**: fail. **Step 3: Implement**; targets created and destroyed with the pass's others.
- [ ] **Step 4: Verify**: suites; build; the software-adapter run first (controller); then bridge, trees, third spot with `VIEW=shadow` shots: the maps described honestly (rail, posts, trunks, canopy, characters visible as casters or not), native share of shadow draws per cascade (criterion 3), fps, clay CPU, resident geometry against the lighting figures.
- [ ] **Step 5: Commit** — "Clay pass: the game's two sun shadow maps drawn natively".

---

### Task 5: Depth pre-pass, mask step, shadowed sun

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/render/clay_logic.h`, `src/native/render/texture_cache.cpp` (the rotation texture, if Task 1 found it readable), `tests/native/test_native_shaders.cpp`, `tests/native/test_clay_logic.cpp`, `README.md`, `docs/native-renderer/frame-map.md` (section 15 "Native shadows")

**Interfaces:**
- Consumes: the maps (Task 4), `ShadowBlock`, `light_formulas.py`'s mask functions (the HLSL matches them term for term; a comment names the `lighting-formulas.md` part).
- Produces:
  - Step A, no picture change: the binding table with `kParamAlbedo` as a pair (albedo, mask; registers after it up by one, `kClayBindings` and every `static_assert` updated, still twelve parameters), a 1 x 1 white mask bound, every lit pixel shader sampling it (`mask`), families multiplying their sun terms as the checked formulas say. Verified pixel-equal to the lighting picture.
  - Step B: the depth pre-pass over the scene's depth-writing records (depth-only forms of the scene's vertex paths; alpha-tested and alpha-to-mask records with their discard), then the mask step into a `kR8_UNORM` target of the clay target's size: `bool FrameInverseViewProj(...)` (pure, `clay_logic.h`: from the records' matrices, false when they disagree beyond a tolerance or the inverse is not finite; reason `shadow-view`), HLSL `MaskNear`, `MaskFar`, `MaskCombine` and their pure C++ mirrors pinned to numbers from `light_formulas.py`; clear depth gives mask 1; then the colour pass with the mask bound. `--fable2_native_shadows` default `on`.

- [ ] **Step 1: Failing tests**: the C++ mirrors against numbers pinned from Python (shared literals in `tests/test_light_check.py`); zero, non-finite and clear-depth inputs give a finite mask in [0, 1]; `FrameInverseViewProj` (agreeing matrices, one outlier, a singular matrix); shader text, reflection and binding-table tests for the pair; pipeline counts.
- [ ] **Step 2: Run**: fail. **Step 3: Implement Step A**, verify with shadows off and on (white mask): pixel-equal to the lighting shots at the bridge (software-adapter run first).
- [ ] **Step 4: Implement Step B.**
- [ ] **Step 5: Verify**: suites; build; software-adapter run; bridge, trees, third spot with bracketing emulated shots and a `VIEW=shadow` shot: where the shadows are against the emulated frame (rail and posts on the planks, canopy on the ground, the hero and dog), acne or detached shadows, the far fade, the mask's rotation pattern; shadows off against the lighting picture (criterion 6); fps, clay CPU (pre-pass cost stated), guest work.
- [ ] **Step 6: Commit** — "Native shadows: depth pre-pass, the game's mask arithmetic, shadowed sun terms".

---

### Task 6: Validation, documents

**Files:**
- Modify: `docs/native-renderer/lighting-regions.json` (or a new `shadow-regions.json` beside it), `tools/xdk_sigmatch/region_compare.py` and `tests/test_region_compare.py` (an edge-position measure), `docs/native-renderer/frame-map.md` (section 15 "Validation"), `docs/native-renderer/user-checks.md`, the spec (status, "Implementation notes (deviations)"), `README.md`

**Interfaces:**
- Produces: `region_compare.py --edge <name>`: along a named line segment (relative coordinates) the position of the steepest luminance step in each picture and their difference in pixels; refuses a segment without a step above a stated contrast.

- [ ] **Step 1: Tool with tests first** (synthetic step images: a shifted edge gives the shift; no edge is refused; a segment outside the picture is refused).
- [ ] **Step 2: Measure** (spec criteria 3 to 7) at the three spots: shadowed regions and the six sunlit regions, three edges, one walking run for shimmer (described), shadows off, fps per window, capture medians on a quiet machine, clay CPU, guest work, target memory.
- [ ] **Step 3: Documents**: section 15 "Validation" (runs with the binary's commit and verdict lines, criteria table, limitations: no sway, no per-light map, blended characters and water, the dissolve, daylight only, anything Tasks 1 and 2 left open), follow-ups; user checks (town, night, interior, a walk under the trees: what to look at, commands, right and wrong pictures; nothing renumbered); the spec's status and deviations with every ruling of the ledger; README (`Shadow:` line, the cvar, `VIEW=shadow`, reasons).
- [ ] **Step 4: Commit** — "Shadows validation: measured regions and edges, user checks".

---

## Self-Review Notes

- Spec coverage: scope items 1 to 6 map to Tasks 1; 2; 3; 4; 5; 6. Criteria 1 (Tasks 1, 3), 2 (Task 2), 3 (Task 4), 4 to 7 (Task 6, first seen in Task 5), 8 (Tasks 4, 5), 9 (all).
- Review Focus tests: 1 in Task 3 (`MakeShadowBlock`, the proof) and Task 5 (white mask); 2 in Tasks 3 and 5 (guards); 3 in Task 5 (Step A pixel-equal, Step B shadows off); 4 in Tasks 4 and 5 (the software-adapter runs); 5 in Task 3 (tracker, reserve limit).
- Tasks 1 and 2 run at the same time (Task 2 is Python, JSON and `lighting-formulas.md`; Task 1 is capture rows and the frame map). Task 3 can start on the tracker, the block and the lists before Task 1's run; its bias fields wait for Task 1.
- Open by design, decided inside the tasks with the reason recorded: the bias source or fallback and the rotation texture (Task 1), the dissolve of `0x85A9...` (Task 2), where shadow records keep their side data (Task 3), how the reversed depth range is kept (Task 4).
