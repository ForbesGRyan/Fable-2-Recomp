# Native Renderer Lighting (sub-project 7) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The native view's brightness and colour follow the game's ambient, baked and sun light (no shadows yet), computed from the game's own constants, vertex data and textures.

**Architecture:** A per-frame light block is read from the guest (its source is found and proven first). A table entry per pixel shader names a light family whose closed formula is checked against the shader microcode. Normals and vertex colours join the geometry cache; the clay pass renders linear colour into a float target, lights each draw by its family, and a fixed output mapping makes display values.

**Tech Stack:** C++20 (clang, pure headers under `src/native` with native tests), HLSL through the SDK's D3D12 RHI, Python 3.12 tools under `tools/xdk_sigmatch` with `unittest`, PowerShell driver `tools/drive_game.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-07-native-renderer-lighting-design.md`. Facts about the game's light come from the research brief `.superpowers/sdd/2026-10-07-native-renderer-lighting/context-brief.md` (untracked; sections A to H) and `docs/native-renderer/frame-map.md` (section 13 "Terrain" for the terrain formulas). Statements of the brief marked NEW were read by hand once: Tasks 1 and 2 check them before anything is built on them.

## Global Constraints

- Branch `native-renderer-lighting`. Never push, stash, reset or switch branches. Never stage `fable_2_manifest.toml`; never `git add -A`. Commit trailer: the two lines `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`, exactly.
- Environment: PATH begins with `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin`, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin`. Game build: `& .\build.cmd -release fable_2` (no `fable_2.exe` running; never kill one you did not start). Native tests: `& .\tests\run_native_tests.cmd` (new tests before its final `exit /b 0`). Python tests: `python -m unittest discover -s tests -p "test_*.py"`.
- SDK changes (`thirdparty/rexglue-sdk`): only the temporary diagnostic of Task 1; no `;` comment lines in `rexruntime.def`; always pass `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` to `build_runtime_sdk.cmd`; the diagnostic is removed and the SDK rebuilt before Task 1's last commit.
- The game is started only through `tools/drive_game.ps1`, by agents without `-UserPresent` (the idle gate decides); start with `-BootSave Hero000` (world from about 23 s, views from 32 s, captures from 33 s). Evidence only from runs whose verdict line is `RUN CLEAN` (or `RUN UNCHECKED` when the controller made the run with the user present: say which). Every run is checked window by window for the freeze signature (23 to 26 fps, work about 40 ms, identical draw counts, 0 uploads) before any number or picture is used.
- Evidence rule: every number names its log, capture or command output; inference is marked; nothing is "verified" without evidence; screenshots relied on are viewed and described honestly. Evidence goes to `out/native-evidence/lighting/`.
- Reads of guest memory and of captured data are bounded and fail closed (a draw whose light inputs cannot be read keeps its material and is counted, never drawn from guessed data). No value read from the guest reaches a shader without a finiteness check.
- `DrawRecord` does not grow (spec ruling 7). View-off capture median stays under 0.5 ms; nothing is added outside the records gate.
- Never put Windows backslash paths inside inline Python or heredoc strings; scan edited docs for control characters; keep each file's line endings. `*.ps1` is git-ignored: tracked scripts stay tracked; a new one needs `git add -f <file>`.
- Reason strings added by this plan: `light-unknown` (no family or inputs unreadable), `normal-unsupported` (a normal format the decoder does not know), `light-block` (the frame's light block was not read).

## Review Focus

1. A frame without a readable light block (menus, loading, the first frames, a region that sets other registers): the pass draws with light off for that frame and counts `light-block`; it never draws black or with stale values from another frame.
2. NaN or infinity from a zero-length normal, a zero colour stream, a constant read as non-finite, or `pow` of a negative: one such pixel in a float target spreads through additive blending. Every light function guards its inputs, and a test feeds zeros and non-finite values.
3. A mesh whose normal fetch is missing or in another format, or whose colour stream is absent: `normal-unsupported` / `light-unknown` with its material kept; not a wrong normal read from another element.
4. Mirrored or non-uniformly scaled instances and scaling bones: the normal is transformed so that it stays perpendicular (or the case is detected and counted), and is renormalised.
5. The float target across view switches (F6 and `VIEW=` tokens), window resize and device loss: recreated with the pass's other targets; the output mapping never samples a destroyed or uninitialised texture; light off reproduces the materials picture.

---

### Task 1: The light constants' source (gate)

**Files:**
- Create: `tools/xdk_sigmatch/pconst_check.py`, `tests/test_pconst_check.py`, `src/native/capture/light_block.h`, `tests/native/test_light_block.cpp`
- Modify: `thirdparty/rexglue-sdk` (temporary diagnostic, removed at the end), `src/native/capture/capture.cpp`, `src/native/capture/xdk_layout.h`, `src/native/capture/discovery_format.h`, `tests/run_native_tests.cmd`, `docs/native-renderer/frame-map.md` (new section 14 "Lighting", subsection "Pixel constants")

**Interfaces:**
- Produces:
  - `struct LightBlock { float eye[3]; float tint[3]; float tint_pow; float add[3]; float out_scale; float floor[2]; float axis[3]; float sun_dir[3]; float sun_col[3]; float mask_xform[4]; bool valid; };` in `light_block.h` (pure): `tint` = `c21.xyz`, `tint_pow` = `c21.w`, `add` = `c20.xyz`, `out_scale` = `c20.w`, `floor` = `c22.xy`, `axis` = `c23.xyz`, `sun_dir` = `c80.xyz`, `sun_col` = `c87.xyz`, `mask_xform` = `c0`, `eye` = `c19.xyz`.
  - `LightBlock MakeLightBlock(const float (*reg)[4], const bool* have)` (pure): `reg[r]` are the registers by number, `have[r]` whether each was read; `valid` only when every register above was read and is finite, `tint_pow` is in (0, 8], `out_scale` is in (0, 1024], and `sun_dir` has length within 0.05 of 1 or is exactly zero (night).
  - In `capture.cpp`: `bool ReadPixelConstant(uint32_t reg, float out[4])`: the proven read path for any pixel register 0..255 at a draw (bank or the source this task finds).
  - Discovery rows: draw and tess rows gain `"pfull"`: all 256 pixel registers as the proven path reads them (discovery only).
  - `pconst_check.py <capture.jsonl> <gpu_log> [--frame N]`: pairs capture rows with GPU-log draws (by packet address, as `render_state_check.py` does) and prints, per register, compared / equal / different counts; exit 0 only when the registers of `LightBlock` are equal in every compared in-scene draw.

- [ ] **Step 1: Checker first.** Tests in `tests/test_pconst_check.py` with synthetic rows and a synthetic log: all equal passes; one register different in one draw fails and is named; a draw present in only one input is reported and not counted as equal; a register the row lacks is "not compared". Run: fails (no tool). Implement `pconst_check.py` reusing `render_state_check.py`'s pairing.
- [ ] **Step 2: GPU-side diagnostic (temporary).** In the SDK's command processor, at each `DRAW_INDX` / `DRAW_INDX_2` of at most 16 GPU frames after an arming condition (an environment variable, as the Task 2 diagnostic of the materials sub-project did: frame-map section 13 "Render state" names the file and pattern), log the packet address and the 256 pixel ALU constant registers (the register file's second constant bank: confirm the indices in `thirdparty/rexglue-sdk` `register_table.inc` and state them). Compact output (hex floats, one line per draw). Rebuild the SDK and the game.
- [ ] **Step 3: Discovery row.** Add `"pfull"` (the device bank at `device + 0x1780`, 256 registers, bounded read) to draw and tess rows; test the formatter in `tests/native/test_discovery_format.cpp`.
- [ ] **Step 4: One capture at the bridge with both** (gated driver; `-BootSave Hero000`, `DISC=8/1` at 40 s, the diagnostic armed for the same frames). If the gate refuses twice, report NEEDS_RUN with the exact command; the controller makes the run.
- [ ] **Step 5: Compare.** `pconst_check.py`: the list of registers where the device bank differs from the GPU, with values (expected from the brief: `c0..c7`, `c80`, `c81`, `c87`, `c88`, `c109`, `c112`; report what is found, not what was expected). Record the GPU's values of every `LightBlock` register and of `c12`, `c13`, `c32..c49`, `c81..c83`, `c88..c90`, `c94..c119` for the frame map (later sub-projects need them).
- [ ] **Step 6: Find the guest-side source.** Method, in this order: (a) which D3D entry points the game calls between two draws that precede a change of a missed register (the discovery layer's hook log, `"raw"` rows and `"cmd"` words); (b) static reading of the recompiled callers of those entry points and of the functions that write `LOAD_ALU_CONSTANT` packets (the literal upload in `GpuLoadShaders`, frame-map line near "LOAD_ALU_CONSTANT", is the known example); (c) where the values sit in guest memory at the draw (search the device structure and the structures it points to for the 16 bytes of the GPU's `c87`, with a bounded scan in the diagnostic build only). State the path with addresses and evidence.
- [ ] **Step 7: Proven reader.** `ReadPixelConstant` reads through the found path; `"pfull"` now shows it; a second capture: `pconst_check.py` exits 0 for the `LightBlock` registers at the bridge and by the trees (count of compared draws stated). `light_block.h` with tests first: every validity rule above, a night block (sun zero) valid, a non-finite register invalid, a missing register invalid.
- [ ] **Step 8: The bool word.** From the same log, compare the device's pixel bool word with the GPU's (`b128`, `b133..b136`): proven or refuted, stated.
- [ ] **Step 9: Remove the diagnostic**, rebuild SDK and game, suites green, frame-map section 14 "Pixel constants" (what the bank misses and why, the path, the proof with file names, the recorded values, what is still unread).
- [ ] **Step 10: Commit** `git add tools/xdk_sigmatch/pconst_check.py tests/test_pconst_check.py src/native/capture/light_block.h tests/native/test_light_block.cpp src/native/capture/capture.cpp src/native/capture/xdk_layout.h src/native/capture/discovery_format.h tests/native/test_discovery_format.cpp tests/run_native_tests.cmd docs/native-renderer/frame-map.md` — "Native lighting: the pixel constants the device bank misses, their source, a proven light block".

**Gate:** if Step 6 finds no guest-side source, stop after Step 5, commit the checker, the row and the findings, and report BLOCKED with what was tried. Do not derive the sun from other registers on your own.

---

### Task 2: Formula tools and the light families (offline)

**Files:**
- Create: `tools/xdk_sigmatch/ucode_eval.py`, `tools/xdk_sigmatch/ps_check.py`, `tools/xdk_sigmatch/vs_check.py`, `tools/xdk_sigmatch/light_check.py`, `tools/xdk_sigmatch/light_formulas.py`, `tests/test_ucode_eval.py`, `tests/test_light_check.py`
- Modify: `docs/native-renderer/frame-map.md` (section 14, subsection "Formulas")

**Interfaces:**
- Consumes: the three scripts in `out/native-evidence/materials/task9/` (copy, do not rewrite), shader dumps in `out/shader_dump/`.
- Produces: `light_formulas.py` with one pure function per family, taking a dict of inputs and returning linear RGB before fog and before `* c20.w`:

```python
def ambient_factor(floor, t):            # L = floor + (1 - floor) * t^2, per channel; t clamped to [0, 1]
    t = min(max(t, 0.0), 1.0)
    return [f + (1.0 - f) * t * t for f in floor]

def terrain_layer(A, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x=1.0): ...
def terrain_far(F, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x): ...
def lit_object(A, vcolour, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c27w=1.0): ...
def plant(A, vs_ambient, n_dot_sun, sun_col): ...
def tree(...): ...   # signature fixed by this task's reading
```

  `floor` is `(c22.x, c22.y, c22.x)`-style as the microcode uses it (state the exact channel mapping from the dump). `light_check.py --family <name> [--shader HASH] [--trials N]` runs the microcode through `ucode_eval.Machine` and the function on random inputs and exits 0 only when every trial agrees within 1e-4 relative; `--all` runs every shader listed in its table `FAMILIES = {family: [(ps_hash, vs_hash), ...]}`.

- [ ] **Step 1: Promote the interpreter.** Copy `ucode_eval.py`, `ps_check.py`, `vs_check.py`; tests in `tests/test_ucode_eval.py`: a hand-written five-instruction dump for each instruction class the interpreter knows (vector and scalar ALU, a predicate, a `cexec` on and off, a fetch with a swizzle) with hand-computed results; `ps_check` and `vs_check` run on the real terrain dumps when `out/shader_dump` exists (skip with a message otherwise) and pass as they did (`ps_check.txt`, `vs_check.txt` of Task 9 are the reference outputs).
- [ ] **Step 2: Terrain families.** `terrain_layer` and `terrain_far` from frame-map section 13 "Terrain" plus the sun term; the vector of `t` for the layer shaders read from vertex shader `C30A97D946FA2BE4` (`o8`: instructions 50, 56 to 60 per the brief) and written as a closed formula; `light_check.py` for the eight terrain shaders with the normal map input flat, lights 1 to 3 zero, mask 1, fog off, `b135` clear. Tests first (`tests/test_light_check.py`): a formula with a wrong exponent, a missing `c11.x`, and a swapped floor channel each fail on the real dump (skip without dumps) and on a small synthetic dump (always run).
- [ ] **Step 3: Object family.** Read `0x8D900846800943C8` with vertex shader `0xECD66A10092E6562` in full (the brief's C3 is the starting point: verify each instruction number): ambient with `pow(vcolour, c21.w)`, where `vcolour = pow(vf3, c255.x)` in the vertex shader (the literal's value from the dump's literal table), the vector `o5` of `t`, the sun diffuse; specular, `b133`, `b134` blocks off in the check. Then `2FD2`, `648B`, `7892` (extra lights zero) against the same function; a shader that needs another form gets its own family name, not a special case.
- [ ] **Step 4: Plants.** Read vertex shader `0x8123C16DBF583F92` (and the vertex shaders paired with `2605`, `165E`, `E99F`, `F525`: list the pairs from a capture): the ambient interpolator as a closed formula of the vertex inputs and vertex constants (which registers: the brief lists vertex `c21`, `c22`, `c25`, `c26`, `c45`, `c54`), checked with `vs_check`-style trials. Then the pixel side.
- [ ] **Step 5: Trees.** Read `00E0`, `7CD5` (with `A584` and the trunk vertex shaders), `58D3`, `2FBE`: what `c72..c76`, tf11, tf12 are; the ambient and sun form. If a family cannot be closed in this task, say exactly which term is open; that family is then drawn `light-unknown` and Task 8 is cut to what is closed.
- [ ] **Step 6: Frame map "Formulas"**: per family the formula, instruction numbers, inputs with their sources (frame block, vertex element, vertex constant, texture), the checker's output file names, what was switched off in the check and why, and coverage (share of drawn draws per family at the bridge and by the trees, recomputed from `task11_bridge_up2_capture` and `task11_trees_up1_capture`).
- [ ] **Step 7: Commit** the seven tool and test files and the frame map — "Lighting tools: microcode interpreter and checkers tracked; light families checked against the shaders".

---

### Task 3: Record side arrays (no behaviour change)

**Files:**
- Modify: `src/native/capture/draw_record.h`, `src/native/capture/frame_scene.h` (or where `FrameScene` lives), `src/native/capture/capture.cpp`, `src/native/capture/terrain_patch.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_logic.h`, `src/native/render/geometry_cache.cpp`, `tests/native/test_draw_record.cpp`, `tests/native/test_frame_scene.cpp`, `tests/native/test_clay_logic.cpp`

**Interfaces:**
- Produces: `FrameScene::terrain` (`std::vector<TerrainPatch>` or the existing container type of the scene, reserved, reused between frames) and `DrawRecord::terrain_index` (`uint16_t`, `kNoSideIndex = 0xFFFF`); `const TerrainPatch* TerrainOf(const FrameScene&, const DrawRecord&)` returning null for a non-terrain record or an index out of range. `sizeof(DrawRecord) <= 640`, pinned by a `static_assert` with the measured value.

- [ ] **Step 1: Failing tests**: `sizeof(DrawRecord)` bound; `TerrainOf` for no index, a valid index, an index past the end; a scene reused for a second frame holds no entry of the first; 65,535 terrain records in one frame make the next one a non-terrain skip counted under the existing overflow reason (no wrap).
- [ ] **Step 2: Run**: fail. **Step 3: Implement**: every reader of `r.terrain` goes through `TerrainOf`; the capture thread appends, the render thread reads the published scene exactly as it reads records today (state the ownership rule in a comment where the scene is published).
- [ ] **Step 4: Verify**: suites green; build; one bridge run and one trees run (`-BootSave Hero000`, the plans of the materials validation in frame-map section 13 "Validation"): `Terrain:` and `State:` lines and draw counts equal to runs 277 / 278, native shots equal to `final_bridge_shot_42s.png` / `final_trees_shot_64s.png` (pixel difference stated), records-on capture median against the same session's view of run 277 (expect lower). If the gate refuses twice: NEEDS_RUN.
- [ ] **Step 5: Commit** — "Native capture: terrain inputs in a side array (DrawRecord 1,064 to N bytes)".

---

### Task 4: Light inputs in the table and the capture

**Files:**
- Modify: `docs/native-renderer/ps-albedo.json`, `tools/xdk_sigmatch/gen_albedo_table.py`, `tests/test_gen_albedo_table.py`, `src/native/capture/ps_albedo_table.inc` (generated), `src/native/capture/material.h`, `src/native/capture/capture.cpp`, `src/native/capture/draw_record.h`, `src/native/capture/frame_scene.h`, `src/native/capture/discovery_format.h`, `tests/native/test_material.cpp`, `tests/native/test_draw_record.cpp`, `README.md`

**Interfaces:**
- Consumes: `LightBlock`, `MakeLightBlock`, `ReadPixelConstant` (Task 1); the families and their inputs (Task 2); side arrays (Task 3).
- Produces:
  - JSON: an entry may carry `"light": {"family": "terrain-layer" | "terrain-far" | "object" | "plant" | "tree", "evidence": "..."}`; the generator refuses a family the entry's kind cannot have (a terrain family on a non-terrain entry and the reverse), an unknown key, a missing evidence string, with the hash in the message. Macro `FABLE2_PS_LIGHT(hash, family_code)`; `enum class LightFamily : uint8_t { kNone, kTerrainLayer, kTerrainFar, kObject, kPlant, kTree }` pinned to the generator's codes by `static_assert`.
  - `FrameScene::light` (`LightBlock`), filled once per frame at the first in-scene draw whose block is valid; `light.valid == false` when none was.
  - `struct LightInputs { LightFamily family; uint8_t normal_format; uint16_t normal_offset; uint16_t normal_stride; uint32_t colour_stream_addr; uint32_t colour_stream_size; uint16_t colour_stride; float world_rot[3][3]; float vs_const[4][4]; };` in a side array `FrameScene::lights` with `DrawRecord::light_index`; `const LightInputs* LightOf(const FrameScene&, const DrawRecord&)`. `normal_format` is the Xenos vertex format code; `vs_const` holds what the family's vertex-side formula needs (object: vertex `c19` and the literal power; plant: the registers Task 2 names), zero otherwise.
  - Reasons `light-unknown`, `normal-unsupported`, `light-block` counted per frame and shown on a capture log line `light: lit N by family {...}, unknown {...}`.

- [ ] **Step 1: Failing tests**: generator (each refusal; the committed JSON generates; codes); `LightOf` like `TerrainOf`; a pure `MakeLightInputs(...)` in `material.h` from fetch descriptions and register values: a draw without a normal element, with a normal in a format other than `FMT_10_11_11`, with a non-finite rotation, with a colour stream of zero size for a family that needs one: each returns the stated reason.
- [ ] **Step 2: Run**: fail. **Step 3: Implement**; enter `"light"` for every pixel shader whose family Task 2 closed, with the checker's output named as evidence.
- [ ] **Step 4: Verify** on a capture (bridge, trees): the `light:` line's counts by family against Task 2's coverage table; discovery rows show the block and the inputs; view-off capture median unchanged; records-on median reported. NEEDS_RUN if the gate refuses twice.
- [ ] **Step 5: Commit** — "Native capture: light block per frame, light inputs per draw, light families in the table".

---

### Task 5: Normals and vertex colours in the geometry cache

**Files:**
- Create: `src/native/render/normal_decode.h`, `tests/native/test_normal_decode.cpp`
- Modify: `src/native/render/geometry_cache.cpp`, `src/native/render/geometry_cache.h`, `src/native/render/geometry_cache_index.h`, `src/native/capture/terrain_patch.h`, `tests/native/test_terrain_patch.cpp`, `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `LightInputs` (Task 4), the existing position decode and skinning (`position_decode.h`, `bone_skin.h`), `BuildTerrainNormals` (materials Task 10b).
- Produces:
  - `bool DecodeNormal(uint32_t format, const uint8_t* p, size_t avail, float out[3])` (pure; big-endian guest words; `FMT_10_11_11`: x = bits 0 to 10, y = bits 11 to 21, z = bits 22 to 31, each signed and divided by its positive maximum, then normalised; confirm the layout against `thirdparty/rexglue-sdk` and against `out/native-evidence/materials/task7d/leaf_evidence.py`'s `stored_normals`, and state the source). Returns false for an unknown format, short data, or a zero vector.
  - `void RotateNormal(const float rot[3][3], const float n[3], float out[3])`: multiplies by the inverse transpose of `rot` (cofactor matrix; falls back to `rot` when the determinant is within 1e-12 of zero) and normalises; a zero result gives (0, 0, 1).
  - Geometry entries gain a normal buffer (3 floats per vertex, world space for static draws, skinned through the same weights as positions, instanced through the instance transform) and, for families that need it, a colour buffer (4 bytes per vertex). The cache key includes the colour stream's content hash when a colour buffer is built. Terrain: `(n.x, n.y)` for every terrain patch, not only steep-ground layers.

- [ ] **Step 1: Failing tests**: `DecodeNormal` on hand-packed words (+x, -y, +z, a diagonal, the most negative code, zero, short data, format 0); `RotateNormal` under a rotation, a non-uniform scale (the result stays perpendicular to a transformed tangent), a mirror, a singular matrix; an env-gated real-stream test (variable `FABLE2_NORMALS_BIN` with a dumped stream and its layout): decoded normals agree with the triangles' face normals above 0.9 on average (the figure the materials Task 7d measured was 0.99).
- [ ] **Step 2: Run**: fail. **Step 3: Implement**, bounded like the position decode (vertex counts, stream sizes), with the same eviction accounting (resident bytes include the new buffers).
- [ ] **Step 4: Verify**: suites; build; bridge and trees run: draw counts and pictures unchanged (nothing reads the buffers yet), geometry resident MB and clay `decode` / `max total` against Task 3's run, the count of `normal-unsupported` by vertex shader. NEEDS_RUN if refused twice.
- [ ] **Step 5: Commit** — "Geometry cache: normals (static, skinned, instanced), vertex colours, terrain normals everywhere".

---

### Task 6: Linear target and output mapping (light off)

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/render/clay_logic.h`, `src/native/render/composite.cpp` (where the clay target is sampled), `src/native/fable2_native_render.cpp` (cvar), `tests/native/test_native_shaders.cpp`, `tests/native/test_clay_logic.cpp`, `README.md`

**Interfaces:**
- Produces: clay colour target `R16G16B16A16_FLOAT` (or `R11G11B10_FLOAT`: measure both, state the choice and why); every clay pixel shader writes linear colour (`albedo * albedo` where it wrote `albedo`; flat clay squared likewise; alpha, alpha test, alpha-to-mask and discard rules unchanged; terrain layers add linear values with the captured blend state); a full-screen step `kOutputMapPs`: `display.rgb = sqrt(saturate(linear.rgb * E))`, alpha 1, into the 8-bit target the composite views already use; `E` from the `Draw`-independent frame constant `exposure` (1.0 with light off); cvar `--fable2_native_light=off|on` (default `off` in this task); pure `float OutputMap(float linear, float exposure)` in `clay_logic.h` mirroring the HLSL.

- [ ] **Step 1: Failing tests**: `OutputMap` at 0, 0.25, 1, 4, negative, NaN (NaN maps to 0); shader text and reflection tests for the new step and for each shader's squared output; the pipeline key distinguishes nothing new (same count as before).
- [ ] **Step 2: Run**: fail. **Step 3: Implement**; target recreated wherever the 8-bit one was (resize, device loss, view switch).
- [ ] **Step 4: Verify (spec criterion 2)**: bridge, trees, third spot with light off against `final_bridge_shot_42s.png`, `final_trees_shot_64s.png` and the third spot's last native shot: share of pixels differing by more than 8 levels under 1% outside terrain blend bands (describe where the bands differ and that they are now the game's linear sums); fps, clay times and target memory against Task 5's run; views cycle with F6 tokens (`VIEW=off,native,split,overlay`) without a black or stale frame.
- [ ] **Step 5: Commit** — "Clay pass: linear float target with a square-root output mapping; light switch (off)".

---

### Task 7: Lit terrain and objects

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/render/clay_logic.h`, `src/native/render/texture_cache.cpp` (the base map tf3 binding), `tests/native/test_native_shaders.cpp`, `tests/native/test_clay_logic.cpp`, `tools/xdk_sigmatch/terrain_replay.py`, `tests/test_terrain_replay.py`, `README.md`, `docs/native-renderer/frame-map.md` (section 14 "Native light")

**Interfaces:**
- Consumes: `FrameScene::light`, `LightOf`, normal and colour buffers, the linear target, `light_formulas.py` (the HLSL must match it term for term; a comment names the frame-map subsection).
- Produces: cbuffer `Frame` (the light block, `exposure`, a `light_on` flag) bound once per frame; HLSL `float3 AmbientFactor(float2 floor, float t)`, `float3 LightTerrainLayer(...)`, `LightTerrainFar(...)`, `LightObject(...)` with the argument lists of `light_formulas.py`; guards: `t` and `n_dot_sun` saturated, `pow` on `max(x, 0)`, a normal of length under 1e-4 replaced by (0, 0, 1); the result divided by nothing. Terrain binds the base map tf3 (`base_valid` inputs kept since materials) as a fourth terrain texture; a square whose base map is unusable falls back to light off for that square and is counted. Objects read the colour buffer; an object without one is `light-unknown`. Draws with `light-unknown`, and every draw of a frame with `light-block`, use `ambient = tint + add` with baked term 1 and L = 1 (spec table, last row). Log and F3 line `Light: on|off, block <valid|missing>, lit N (terrain-layer a, terrain-far b, object c, plant d, tree e), unknown {shader: n, ...}, sun (x, y, z) colour (r, g, b)`. Default of `--fable2_native_light` becomes `on`; `E` is left at 1 until Task 9 and the README says so.

- [ ] **Step 1: Failing tests**: pure mirrors of each light function in `clay_logic.h` against numbers pinned from `light_formulas.py` (the same literals in `tests/test_light_check.py` and the C++ test); zero and non-finite inputs give finite output (Review Focus 2); shader text and reflection tests (`Frame` block offsets against the C++ struct); the stats text; `terrain_replay.py --lit` on a synthetic capture with hand-computed pixels.
- [ ] **Step 2: Run**: fail. **Step 3: Implement.**
- [ ] **Step 4: Verify**: suites; build; bridge and third spot runs, each with emulated shots bracketing the native shot (`VIEW=off` shot, `VIEW=native` shot, `VIEW=off` shot, 4 s apart), the `Light:` line, `light-unknown` by shader, fps and clay times against Task 6. Describe: ground and objects brighter and warmer or not, the sun side of the rock and planks against the averted side, where the picture is too bright (shadowed ground) or wrong. Offline: `terrain_replay.py --lit` for the dirt-patch square with the captured block against the same square's crop in the emulated shot (mean colour, stated as a ratio; no pass threshold in this task).
- [ ] **Step 5: Commit** — "Native light: ambient, baked and sun light for terrain and objects".

---

### Task 8: Lit plants and trees

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_logic.h`, `tests/native/test_native_shaders.cpp`, `tests/native/test_clay_logic.cpp`, `docs/native-renderer/frame-map.md`

**Interfaces:**
- Consumes: Task 2's closed formulas for the `plant` and `tree` families (only those it closed), Task 4's `vs_const`, Task 7's `Frame` block and guards.
- Produces: `LightPlant(...)`, `LightTree(...)` in HLSL and their pure mirrors; the plant's vertex-side ambient evaluated in the clay vertex shader from `vs_const` and the vertex inputs it needs (state which; an input the cache does not hold makes the draw `light-unknown`).

- [ ] **Step 1: Failing tests** as in Task 7 (pinned numbers shared with Python; guards; text and reflection). **Step 2: Run**: fail. **Step 3: Implement.**
- [ ] **Step 4: Verify**: trees and bridge runs with bracketing emulated shots; lit share of drawn draws at the three spots (spec criterion 3: at least 90%; report per family and the remaining shaders); the canopy, ferns and grass against the emulated frame described honestly (they will lack shadow and translucency).
- [ ] **Step 5: Commit** — "Native light: plants and trees".

---

### Task 9: Calibration, validation, documents

**Files:**
- Modify: `src/native/render/clay_logic.h` (the constant `kLitExposure` with its evidence comment), `docs/native-renderer/frame-map.md` (section 14 "Validation"), `docs/native-renderer/user-checks.md`, `docs/superpowers/specs/2026-10-07-native-renderer-lighting-design.md` (status, "Implementation notes (deviations)"), `README.md`
- Create: `tools/xdk_sigmatch/region_compare.py`, `tests/test_region_compare.py`

**Interfaces:**
- Produces: `region_compare.py <emulated_before.png> <native.png> <emulated_after.png> --regions <file>`: for each named rectangle (relative coordinates) the mean linear luminance and hue of each picture, the native / emulated ratio against the mean of the two emulated shots, and the drift between them; `--calibrate <region>` prints the `E` that makes that region's ratio 1 given the current `E`.

- [ ] **Step 1: Tool with tests first** (synthetic images: a region twice as bright gives ratio 2 in linear terms; hue of a known colour; drift; a rectangle outside the picture refused).
- [ ] **Step 2: Calibrate** `E` on the dirt patch at the bridge from one bracketed run; write `kLitExposure` with the run's log and shot names in the comment; rebuild.
- [ ] **Step 3: Measure** (spec criteria 3 to 7) at the three spots with bracketed shots: the six regions and five pairs named in the spec (regions file committed under `docs/native-renderer/`), the shadowed regions listed apart, fps per window, capture medians (view off, records on), clay CPU, target memory, lit share by family.
- [ ] **Step 4: Documents**: frame-map section 14 "Validation" (runs with the binary's commit and verdict lines, criteria table, limitations: no shadows, no further lights, no normal maps or specular, no fog or haze, fixed exposure, daylight only, anything Task 2 left open), follow-ups; user checks (town, night, interior: what to look at, commands with `-BootSave` where a save reaches the place, right and wrong pictures; nothing renumbered); the spec's status and deviations with every ruling of the ledger; README (the `Light:` line, the cvar, reasons).
- [ ] **Step 5: Commit** — "Lighting validation: calibrated exposure, measured regions, user checks".

---

## Self-Review Notes

- Spec coverage: scope items 1 to 6 map to Tasks 1; 2; 4 and 5; 6; 7 and 8; 9. Ruling 7 (record size) is Task 3. Criteria 1 (Task 1), 2 (Task 6), 3 (Task 8), 4 to 7 (Task 9), 8 (Tasks 2, 4, 7).
- Review Focus tests: 1 in Tasks 4 and 7 (`light-block`), 2 in Tasks 6 and 7 (non-finite inputs), 3 in Tasks 4 and 5, 4 in Task 5 (`RotateNormal`), 5 in Task 6 (view cycling, recreation).
- Tasks 1 and 2 can run at the same time (Task 2 is Python and documents only; both write frame-map section 14 in separate subsections: Task 2 commits its subsection after Task 1's commit, or rebases its text onto it).
- Open by design, decided inside the tasks with the reason recorded: the float format (Task 6), the tree family's form (Task 2), the source of the constants (Task 1, gated).
