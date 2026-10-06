# Native Renderer Materials (sub-project 6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every draw of the native view shows its correct unlit base colour and shape: textured ground with the game's layer blend and holes, cut-outs through alpha test, blended layers, and textures for the trees and the shaders seen away from the bridge.

**Architecture:** Capture reads six GPU register words of render state from the game's D3D device shadow per draw (proven against the emulated GPU first). The offline material table gains an alpha recipe and a kind per pixel shader. The clay pass keeps a small pipeline cache keyed by state, discards for alpha test, blends with the captured factors, and draws terrain with a dedicated hand-ported pixel shader. Autoplay gains stick entries so captures and validation can leave the bridge.

**Tech Stack:** C++20 (clang-cl, Ninja), D3D12 through the SDK's `nrhi` RHI, HLSL embedded in `src/native/fable2_native_shaders.h`, Python 3 tools under `tools/xdk_sigmatch`, PowerShell driver `tools/drive_game.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-06-native-renderer-materials-design.md`

## Global Constraints

- Environment: put `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin` FIRST on PATH, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin` (`tests\run_native_tests.cmd` calls bare `clang++`).
- Game build: `& .\build.cmd -release fable_2` from PowerShell; no `fable_2.exe` may be running (check `tasklist`; never kill one you did not start).
- SDK changes (Task 4 only): work in `thirdparty\rexglue-sdk` on branch `renderer`; rebuild with `build_runtime_sdk.cmd` passing `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` as the install directory; `rexruntime.def` must not gain `;` comment lines; commit in the submodule, then commit the new submodule pointer in the game repo. Never push either repository.
- Python tests: `python -m unittest discover -s tests -p "test_*.py"`. Native tests: PowerShell `$env:PATH = "C:\Program Files\LLVM\bin;" + $env:PATH; & .\tests\run_native_tests.cmd` (add new tests before its final `exit /b 0`; the "broken_ps failed ... X3004" output is an expected negative test).
- Preserve each edited file's line endings (`core.autocrlf=true`). Text written by scripts must not contain control characters other than CR, LF, TAB (a Windows path inside an interpreted string once produced a form feed: write paths with forward slashes or raw strings).
- Git: branch `native-renderer-materials`; stage files explicitly; never stage `fable_2_manifest.toml`; never `git add -A`; never push, stash, reset or switch branches. Every commit message ends with a blank line and these two lines exactly:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`
- Gameplay runs ONLY via `tools\drive_game.ps1` (in-process autoplay; from Task 1 on it can also move the sticks). Never `-KeyboardInput`, never any other input method. The save loads the outdoor bridge scene; the world is fully up at about 50 s.
- **The freeze.** The world sometimes freezes when the machine is in use or the game window is not in front (`docs/freeze-investigation.md`). Before using any number or screenshot from a run, check its `[frame] guest` lines: live is about 30.0 fps with changing draw counts; frozen is 23 to 26 fps, work about 40 ms, identical draw counts, 0 uploads. Discard frozen runs and rerun (at most three attempts per configuration, then report).
- Checks only the user can do are added to `docs/native-renderer/user-checks.md` under "Pending" (highest existing number is 16) with the exact command and the expected result; the plan never waits on them.
- Unit-tested headers marked "pure" must not include SDK, Windows or GPU headers.
- New reason strings (exact): `alpha-unknown`, `blend-unknown`.
- Nothing is drawn from guessed data: a table entry that does not match what the decoder finds leaves the draw with its existing reason; a blended draw whose colour is unknown is not drawn (`blend-unknown`); an alpha-tested draw without an alpha recipe is drawn without the test and counted (`alpha-unknown`).
- Evidence rule for a table entry: the dump reading with instruction numbers; a decoded thumbnail of the texture; for an alpha recipe, the share of sampled draws of that shader whose captured state has alpha test or blending on; a native-view screenshot against the emulated frame for at least one draw of the shader. Entries are `"manual": true`.
- The emulated frame is never modified; with `fable2_native_view=off` records are not built and capture overhead stays under 0.5 ms per frame.
- Evidence files live outside the build directory too: copy every capture, dump folder, log and screenshot an entry or a result rests on to `out\native-evidence\materials\` (git-ignored) and name it there.
- Shader dumps: `out\shader_dump\shader_<HASH>.ucode.vert` / `.frag` (add `--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump` to a run to dump shaders it meets). Existing captures: `out\native-evidence\native_discovery_20261005_103213.jsonl` with `native_geo_20261005_103213\` (bridge scene).

## Review Focus

1. Garbage render-state words (unknown blend factor, compare function 8+, non-finite alpha reference, unreadable device memory): the draw uses the default state (opaque, depth Less with write, no cull), is counted, and nothing crashes or is skipped for that alone. Pinned in Task 3.
2. More distinct state combinations than the pipeline cache holds: further combinations draw with the default pipeline and are counted; no unbounded pipeline creation, no eviction of a pipeline in use this frame. Pinned in Task 6.
3. A blended draw whose colour is unknown is not drawn and counted `blend-unknown`; an alpha-tested draw with no recipe is drawn without the test and counted `alpha-unknown`; a blended draw with colour but no recipe is drawn with alpha 1 and counted `alpha-unknown`. Pinned in Task 5 (rule function) and Task 6.
4. An autoplay plan with a malformed stick token (missing field, value out of range, extra `/`, zero duration) is rejected as a whole, as unknown button names are today; sticks are centred outside every entry and after `EndTime`. Pinned in Task 1.
5. A terrain texture that is pending, bad or over budget, or a tf18 region that is truncated: the terrain draw falls back to flat clay for that frame under the existing texture reasons, no hole is cut from missing data, and the DXT3A decoder reads nothing out of range. Pinned in Tasks 8 and 10.

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `src/input/autoplay.h` (pure) | plan grammar, `ButtonsAt`, `SticksAt`, `EndTime` | 1 |
| `src/core/fable_2_app.h` | publishes buttons and sticks from the plan | 1 |
| `tools/drive_game.ps1` | passes `LS=`/`RS=` tokens through | 1 |
| `src/native/capture/xdk_layout.h` (pure) | device shadow offsets of the render-state registers | 2 |
| `tools/xdk_sigmatch/render_state_check.py` | offline equality proof and state census | 2 |
| `src/native/capture/render_state.h` (pure, new) | raw words to `RenderState` | 3 |
| `src/native/capture/draw_record.h` (pure) | `DrawRecord::state_words` | 3 |
| `src/native/capture/capture.cpp` | reads the words; terrain inputs; discovery rows | 2, 3, 9 |
| SDK `include/rex/graphics/native_rhi.h`, `src/graphics/d3d12/native_rhi_d3d12.cpp` | blend factors, operations, compare functions | 4 |
| `docs/native-renderer/ps-albedo.json`, `tools/xdk_sigmatch/gen_albedo_table.py`, `src/native/capture/ps_albedo_table.inc` | material table with alpha recipe and kind | 5, 7, 9 |
| `src/native/capture/material.h` (pure) | `AlphaSpec`, `MaterialKind`, `AlphaRule` | 5 |
| `tools/xdk_sigmatch/shader_trace.py` | `trace_ps_alpha`, kill report | 5 |
| `src/native/render/clay_logic.h` (pure) | `PipelineKey`, constants, stats text | 6, 10 |
| `src/native/render/clay_pass.cpp`, `clay_pass.h` | pipeline cache, bindings, draw | 6, 10 |
| `src/native/fable2_native_shaders.h` | alpha test, terrain pixel shaders | 6, 10 |
| `src/native/render/texture_decode.h` (pure), `texture_cache.cpp` | `k_DXT3A` to single channel | 8 |
| `tools/xdk_sigmatch/terrain_replay.py` | offline replay of the terrain colour formula | 9 |
| `src/native/capture/terrain_patch.h` (pure) | terrain material inputs in the patch | 9, 10 |
| `docs/native-renderer/frame-map.md` section 13 | evidence, readings, validation | 2, 7, 9, 11 |

---

### Task 1: Autoplay stick movement

**Files:**
- Modify: `src/input/autoplay.h`, `src/core/fable_2_app.h` (around the autoplay thread, about lines 410 to 445), `tools/drive_game.ps1` (plan translation, about lines 61 to 81), `README.md` (autoplay rows near lines 99 to 103)
- Test: `tests/native/test_autoplay.cpp`

**Interfaces:**
- Consumes: existing `fable2::autoplay::Step {double t_seconds; uint16_t buttons; std::string name}`, `Parse`, `ButtonsAt(steps, t, hold)`, `EndTime(steps, hold)`; `fable2::remote::InputSnapshot {buttons, left_trigger, right_trigger, stk_lx, stk_ly, stk_rx, stk_ry}` (`src/input/remote_input_state.h`).
- Produces:
  - Grammar: token `seconds:LS=x/y/ms` or `seconds:RS=x/y/ms`; x, y integers in [-100, 100] (percent of full deflection; positive y forward, positive x right); ms integer in [1, 60000]. Case-insensitive `LS`/`RS`. The token contains no `,` and no second `:`.
  - `Step` gains `uint8_t stick = 0` (0 none, 1 left, 2 right), `int8_t x = 0, y = 0`, `uint32_t stick_ms = 0`.
  - `struct Sticks { int32_t lx = 0, ly = 0, rx = 0, ry = 0; };` and `Sticks SticksAt(const std::vector<Step>& steps, double t_seconds);` in axis units: `value = percent * 32767 / 100`. Per stick, the entry with the latest start among those with `t_step <= t < t_step + stick_ms / 1000.0` wins; none means centred.
  - `EndTime(steps, hold)` = the latest of every button step's `t + hold` and every stick step's `t + stick_ms / 1000.0`.

- [ ] **Step 1: Failing tests.** Append to `tests/native/test_autoplay.cpp` (keep its return-code style; use the next free codes):

```cpp
  {  // stick entries
    auto s = fable2::autoplay::Parse("50:LS=0/100/4000, 52:ls=-50/0/500,53:RS=100/-100/250,54:A");
    if (!s || s->size() != 4) return 60;
    if ((*s)[0].stick != 1 || (*s)[0].x != 0 || (*s)[0].y != 100 || (*s)[0].stick_ms != 4000) return 61;
    if ((*s)[2].stick != 2 || (*s)[2].x != 100 || (*s)[2].y != -100) return 62;
    if ((*s)[3].stick != 0 || (*s)[3].buttons != 0x1000) return 63;
    using fable2::autoplay::SticksAt;
    if (SticksAt(*s, 49.999).ly != 0) return 64;                       // before
    if (SticksAt(*s, 50.0).ly != 32767 || SticksAt(*s, 50.0).lx != 0) return 65;   // start inclusive
    if (SticksAt(*s, 52.2).lx != -16383 || SticksAt(*s, 52.2).ly != 0) return 66;  // later entry wins
    if (SticksAt(*s, 52.6).ly != 32767) return 67;                     // back to the longer entry
    if (SticksAt(*s, 54.0).ly != 0) return 68;                         // end exclusive
    if (SticksAt(*s, 53.1).rx != 32767 || SticksAt(*s, 53.1).ry != -32767) return 69;
    if (fable2::autoplay::ButtonsAt(*s, 52.0, 0.1) != 0) return 70;    // stick entries press nothing
    if (fable2::autoplay::EndTime(*s, 0.1) != 54.1) return 71;         // 54 + hold > 50 + 4
    auto only = fable2::autoplay::Parse("10:LS=10/10/60000");
    if (!only || fable2::autoplay::EndTime(*only, 0.1) != 70.0) return 72;
  }
  for (const char* bad : {"5:LS=0/100", "5:LS=0/100/0", "5:LS=101/0/100", "5:LS=0/-101/100",
                          "5:LS=0/100/60001", "5:LS=0/100/10/1", "5:LS=a/0/100", "5:XS=0/0/100",
                          "5:LS=", "5:LS=0//100"}) {
    if (fable2::autoplay::Parse(bad)) return 73;   // the whole plan is rejected
  }
```

- [ ] **Step 2: Run** the native tests; expected: `test_autoplay.cpp` fails to compile (`no member named 'stick'`, `SticksAt` undeclared).
- [ ] **Step 3: Implement** in `autoplay.h`: in `Parse`, before the button-name lookup, recognise a name whose first three characters are `LS=` or `RS=` (case-insensitive); split the rest on `/` into exactly three fields; parse each as a decimal integer with an optional leading `-` for x and y (no other characters); range-check; fill the stick fields with `buttons = 0` and `name` the canonical upper-case token. `ButtonsAt` must ignore stick steps. Implement `SticksAt` and extend `EndTime` as specified. Keep the header pure (std only).
- [ ] **Step 4: App and script.** In `fable_2_app.h`, where the autoplay thread publishes `{.buttons = ...}`, publish `{.buttons = ButtonsAt(...), .stk_lx = s.lx, .stk_ly = s.ly, .stk_rx = s.rx, .stk_ry = s.ry}` with `s = SticksAt(steps, t)`; the final all-zero publish at `EndTime` stays. In `drive_game.ps1`, a plan token whose key part matches `^(LS|RS)=-?\d+/-?\d+/\d+$` (case-insensitive) is passed through unchanged; everything else keeps the key translation and the unknown-key error. Update the script's header comment and the README autoplay rows with the new token and an example.
- [ ] **Step 5: Verify.** Native tests exit 0. Release build. One run: `.\tools\drive_game.ps1 -Total 80 -Plan "23:E,27:S,30:E,35:E,55:LS=0/100/3000,60:RS=60/0/1500" -Shots "52,66" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`. Expected: the two screenshots differ in camera position and heading (the hero has walked and the camera has turned); the log's capture counts change between the windows around 52 s and 66 s. View both screenshots and describe them. Check the freeze signature first.
- [ ] **Step 6: Commit**

```bash
git add src/input/autoplay.h src/core/fable_2_app.h tools/drive_game.ps1 README.md tests/native/test_autoplay.cpp
git commit -m "Autoplay: timed stick entries (LS=x/y/ms, RS=x/y/ms)"
```

---

### Task 2: Render-state discovery and proof

**Files:**
- Modify: `src/native/capture/xdk_layout.h`, `src/native/capture/capture.cpp` (discovery row writer `WriteDrawRow` and the tess row writer), `tests/native/test_xdk_layout.cpp`, `docs/native-renderer/frame-map.md` (new section 13 "Materials", subsection "Render state")
- Create: `tools/xdk_sigmatch/render_state_check.py`, `tests/test_render_state_check.py`
- Temporary, not committed unless stated: an SDK log line in `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp` `IssueDraw`

**Interfaces:**
- Produces:
  - `xdk_layout.h`: `inline constexpr uint32_t kDeviceRegShadowBase2100 = 0x28CC;` (register 0x2100) and `kDeviceRegShadowBase2200 = 0x2934;` (register 0x2200), `constexpr uint32_t DeviceRegShadowOffset(uint32_t reg)` returning `base + 4 * (reg - first)` for 0x2100..0x2114 and 0x2200..0x220B and 0 otherwise, and named registers `kRegColorMask = 0x2104`, `kRegBlendRed = 0x2105`, `kRegAlphaRef = 0x210E`, `kRegDepthControl = 0x2200`, `kRegBlendControl0 = 0x2201`, `kRegColorControl = 0x2202`, `kRegSuScModeCntl = 0x2205`.
  - Discovery draw and tess rows gain `"rs": {"2104": "0x...", "210E": "0x...", "2200": "0x...", "2201": "0x...", "2202": "0x...", "2205": "0x...", "2105": "0x...", "2106": "0x...", "2107": "0x...", "2108": "0x..."}` read from the device shadow at the draw (bounded reads; only when discovery is armed).
  - `render_state_check.py <capture.jsonl> --gpu <gpu_log>`: prints, per register, sampled draws, equal, unequal (first five mismatches with frame and draw ordinal); and a census of the decoded states (alpha test on/off with function, blend factor triples, depth functions, cull modes, colour masks) with counts by vertex and pixel shader. Exit code 1 if any register is unequal.
- Background (from the fact sheet; verify each): the draw builders flush eight register ranges from the device with the run writer `0x8221C908` (r5 base register, r6 device + offset): +0x2880 for 0x2000 (16 words), +0x28CC for 0x2100 (21), +0x2920 for 0x2180 (5), +0x2934 for 0x2200 (12), +0x2964 for 0x2280 (21), +0x29B8 for 0x2300 (38), +0x2A50 for 0x2380 (8), +0x2780 for 0x4900 (40). `RB_BLENDCONTROL0` is register 0x2201 (0x2208 is `RB_MODECONTROL`; `thirdparty\rexglue-sdk\include\rex\graphics\register_table.inc`).

- [ ] **Step 1: Layout test first.** In `tests/native/test_xdk_layout.cpp` add checks: `DeviceRegShadowOffset(0x2104) == 0x28DC`, `(0x210E) == 0x2904`, `(0x2200) == 0x2934`, `(0x2201) == 0x2938`, `(0x2202) == 0x293C`, `(0x2205) == 0x2948`, `(0x2208) == 0x2954` (agrees with the existing `RB_MODECONTROL` comment), `(0x2285) == 0`, `(0x20FF) == 0`. Run: fails to compile. Implement in `xdk_layout.h`. Run: passes.
- [ ] **Step 2: Confirm the flush offsets in the generated code.** For one draw builder (`DrawIndexedVertices`, `generated/default/fable_2_recomp.201.cpp` about lines 1215 to 1411) read the eight calls and record, in the frame map, the `addi r6,r31,<offset>` and `li r5,<base>` pairs you see. If any differs from the table above, stop and report.
- [ ] **Step 3: Discovery rows.** Add the `"rs"` object to draw and tess rows (same pattern as `"vbool"`: bounded `ReadVirtual` of the device, behind the armed check). No cost when discovery is off.
- [ ] **Step 4: GPU side.** Add a temporary log line in the D3D12 command processor's `IssueDraw` that prints, for each executed draw of the main scene pass while a discovery environment variable is set, a frame ordinal, a draw ordinal within the frame and the same ten registers from the register file. Match guest draws to GPU draws the way frame-map section 9 did for constants (draw ordinal within the frame for the same builder, checked with the index count). Prefer a discovery-gated permanent line if it is small and costs nothing when off (then it is an SDK change: follow the SDK constraint and commit it); otherwise keep it out of the commit and save the patch under `out\native-evidence\materials\`.
- [ ] **Step 5: Captures.** Two discovery captures through `drive_game.ps1` with `-Env @{FABLE2_NATIVE_DISCOVERY="120"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="4"}` (see how sub-project 5 named them): the bridge scene standing still, and one after walking with a Task 1 plan off the bridge to the left (towards the birches; iterate the plan with screenshots until trees fill part of the view; record the plan string). Add `--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump` to the second. Copy both captures and the GPU logs to `out\native-evidence\materials\`.
- [ ] **Step 6: Checker (TDD).** `tests/test_render_state_check.py` first: a synthetic capture and GPU log with all equal (exit 0, counts), one with a mismatch in `2201` (exit 1, the mismatch listed), rows without `"rs"` (reported as not sampled, not as equal), and the census (a row with `2202 = 0x0000000C` counted as alpha test on with function 4). Then implement. Bit layout for the census (confirm against `thirdparty\rexglue-sdk\include\rex\graphics\registers.h` and `xenos.h`; the SDK headers win if they differ, and say so): `RB_COLORCONTROL` bits 0-2 alpha function, bit 3 alpha test enable, bit 4 alpha-to-mask enable; `RB_BLENDCONTROL0` bits 0-4 colour source factor, 5-7 colour operation, 8-12 colour destination factor, 16-20 alpha source, 21-23 alpha operation, 24-28 alpha destination; `RB_DEPTHCONTROL` bit 1 depth test, bit 2 depth write, bits 4-6 depth function; `PA_SU_SC_MODE_CNTL` bit 0 cull front, bit 1 cull back, bit 2 front face; `RB_COLOR_MASK` bits 0-3 target 0 (R, G, B, A); `RB_ALPHA_REF` a float.
- [ ] **Step 7: Run the proof** on both captures. Required: every one of the six state registers equal in every sampled draw. For a register that fails: find its writers (the fact sheet lists direct stores: depth control 15 sites such as `sub_82B90B18`, blend control 8 such as `sub_82B90780`, colour control 4, cull 12, colour mask 5, alpha reference 1 at `82225F28`), decide whether the failures are draws whose state the engine writes straight to the command buffer, and report the finding with counts: the fallback (a capture-side shadow) is then specified by the controller before Task 3.
- [ ] **Step 8: Write up** frame-map section 13 "Materials", "Render state": the offsets with their evidence, the proof numbers per capture, the census tables (which blend factor triples, depth functions, cull modes and alpha functions occur, by shader, with counts), whether the blend constant is used by any draw, the walking plan string, and what the census means for later tasks: the list of RHI blend factors, operations and compare functions that must exist (input to Task 4) and the depth convention (which depth function opaque draws use; whether cull modes are consistent with the emulated image).
- [ ] **Step 9: Commit**

```bash
git add src/native/capture/xdk_layout.h src/native/capture/capture.cpp tests/native/test_xdk_layout.cpp tools/xdk_sigmatch/render_state_check.py tests/test_render_state_check.py docs/native-renderer/frame-map.md
git commit -m "Native materials: render-state shadow offsets, discovery rows and the equality proof"
```

---

### Task 3: Render state in the record

**Files:**
- Create: `src/native/capture/render_state.h`, `tests/native/test_render_state.cpp`
- Modify: `src/native/capture/draw_record.h`, `src/native/capture/capture.cpp` (`FillDrawInputs`, the terrain inputs path), `tests/native/test_draw_record.cpp`, `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: Task 2's offsets and bit layout (as confirmed there).
- Produces (pure, namespace `fable2::native::capture`):

```cpp
// Raw GPU register words of one draw's render state, as read from the device shadow.
struct StateWords {
  uint32_t color_control = 0;   // RB_COLORCONTROL
  uint32_t alpha_ref = 0;       // RB_ALPHA_REF, a float's bits
  uint32_t blend_control = 0x00010001;  // RB_BLENDCONTROL0: One, Zero, Add for colour and alpha
  uint32_t color_mask = 0xF;    // RB_COLOR_MASK
  uint32_t depth_control = 0;   // RB_DEPTHCONTROL
  uint32_t su_sc_mode = 0;      // PA_SU_SC_MODE_CNTL
  uint32_t vport_z_scale = 0x3F800000;  // PA_CL_VPORT_ZSCALE (register 0x2113), a float's bits; negative: depth is reversed
  bool read = false;            // false: the words could not be read
};
enum class Compare : uint8_t { kNever, kLess, kEqual, kLessEqual, kGreater, kNotEqual, kGreaterEqual, kAlways };
enum class Factor : uint8_t { kZero = 0, kOne = 1, kSrcColor = 4, kInvSrcColor = 5, kSrcAlpha = 6, kInvSrcAlpha = 7,
                              kDstColor = 8, kInvDstColor = 9, kDstAlpha = 10, kInvDstAlpha = 11, kConstColor = 12,
                              kInvConstColor = 13, kConstAlpha = 14, kInvConstAlpha = 15, kSrcAlphaSat = 16 };
enum class BlendOp : uint8_t { kAdd = 0, kSubtract = 1, kMin = 2, kMax = 3, kRevSubtract = 4 };
enum class Cull : uint8_t { kNone, kFront, kBack };
struct RenderState {
  bool valid = false;
  bool alpha_test = false; Compare alpha_func = Compare::kAlways; float alpha_ref = 0.0f; bool alpha_to_mask = false;
  bool blend = false; Factor src = Factor::kOne, dst = Factor::kZero; BlendOp op = BlendOp::kAdd;
  Factor src_alpha = Factor::kOne, dst_alpha = Factor::kZero; BlendOp op_alpha = BlendOp::kAdd;
  uint8_t color_mask = 0xF;
  bool depth_test = true, depth_write = true; Compare depth_func = Compare::kLess;
  Cull cull = Cull::kNone;
  bool front_cw = false;         // PA_SU_SC_MODE_CNTL front-face bit, as Task 2 established its meaning
  bool depth_reversed = false;   // viewport Z scale negative: Task 6 mirrors depth_func
};
RenderState DefaultRenderState();          // valid, opaque, depth Less with write, no cull, mask 0xF
RenderState DecodeRenderState(const StateWords& w);
```

  Rules of `DecodeRenderState`: `!w.read` gives `DefaultRenderState()` with `valid = false`. A factor value outside the enum (2, 3, 17 and up), an operation above 4, or a non-finite alpha reference gives the default state with `valid = false`. `blend` is true unless colour and alpha are both (One, Zero, Add). Alpha-to-mask sets `alpha_to_mask`; it does not set `alpha_test`. `cull`: both cull bits give `kNone` and `valid = false` (nothing would be drawn; treat as unknown); otherwise front or back as the bits say; `front_cw` is the front-face bit as Task 2's write-up defines it; `depth_reversed` is true when the viewport Z scale is a negative finite float (a non-finite scale gives the default state with `valid = false`).
  - `DrawRecord::state_words` (`StateWords`), `DrawInputs::state_words`; `AssembleRecord` copies it for every drawn record (terrain, instanced, skinned, plain).

- [ ] **Step 1: Failing tests** in `tests/native/test_render_state.cpp` (new; add to `run_native_tests.cmd` beside the other pure tests): the default state; real words from Task 2's census (take at least four distinct real combinations from the frame map: an opaque draw, an alpha-tested draw, a blended draw, a terrain layer) with the expected decoded fields; unread words; factor 2, factor 17, operation 5, alpha reference NaN and infinity (default, invalid); both cull bits; colour mask 0; alpha-to-mask without alpha test. In `test_draw_record.cpp`: `AssembleRecord` carries `state_words` for a plain, an instanced and a terrain input.
- [ ] **Step 2: Run**: compile failures (`render_state.h` not found).
- [ ] **Step 3: Implement** `render_state.h` (std headers only) and the record fields.
- [ ] **Step 4: Capture.** In `FillDrawInputs` and the terrain path read the seven words from the device (the six state registers and `PA_CL_VPORT_ZSCALE`, register 0x2113, through `DeviceRegShadowOffset`) at `DeviceRegShadowOffset(reg)` with the existing bounded device read; a failed read leaves `read = false`. Only when records are built.
- [ ] **Step 5: Verify.** Native tests and Python tests green; release build; one split run of the bridge scene: counts unchanged from the last run before this task (captured, drawable, instanced, skinned, textured), view-off capture median still under 0.5 ms in a view-off run.
- [ ] **Step 6: Commit**

```bash
git add src/native/capture/render_state.h src/native/capture/draw_record.h src/native/capture/capture.cpp tests/native/test_render_state.cpp tests/native/test_draw_record.cpp tests/run_native_tests.cmd
git commit -m "Native materials: render state words in the draw record and their decoder"
```

---

### Task 4: RHI blend factors, operations and compare functions (SDK) — DROPPED

**Dropped by ruling, 2026-10-06, after Task 2's census:** the game's draws in both captures use only blend factors (zero, one, source alpha, inverse source alpha), the blend operation (add) and, after mirroring for the reversed depth, compare functions (Less, LessEqual) that the RHI already has; alpha test is a shader `discard`. The one exception, constant-colour blend factors on about one draw per frame of the walking scene, is handled in Task 6 by leaving such draws out (`blend-unknown`). The text below is kept for the record and is not executed.

**Files (in `thirdparty/rexglue-sdk`, branch `renderer`):**
- Modify: `include/rex/graphics/native_rhi.h` (enums `BlendFactor` about lines 151 to 156, `BlendOp` 158 to 161, `CompareFunc` 139 to 143), `src/graphics/d3d12/native_rhi_d3d12.cpp` (`ToBlend`, `ToBlendOp`, `ToCompare` about lines 239 to 267)
- Game repo: the submodule pointer

**Interfaces:**
- Produces: `nrhi::BlendFactor` gains `kSrcColor, kInvSrcColor, kDstColor, kInvDstColor, kDstAlpha, kInvDstAlpha, kSrcAlphaSat` (and `kBlendFactor`, `kInvBlendFactor` only if Task 2's census found a constant factor in use, together with a blend-constant setter on the command list); `nrhi::BlendOp` gains `kSubtract, kRevSubtract, kMax`; `nrhi::CompareFunc` gains `kNever, kEqual, kGreater, kNotEqual, kGreaterEqual`. Existing enumerators keep their values (append only).

- [ ] **Step 1:** Append the enumerators; extend the three mappers with the D3D12 equivalents (`D3D12_BLEND_SRC_COLOR`, `D3D12_BLEND_INV_SRC_COLOR`, `D3D12_BLEND_DEST_COLOR`, `D3D12_BLEND_INV_DEST_COLOR`, `D3D12_BLEND_DEST_ALPHA`, `D3D12_BLEND_INV_DEST_ALPHA`, `D3D12_BLEND_SRC_ALPHA_SAT`; `D3D12_BLEND_OP_SUBTRACT`, `D3D12_BLEND_OP_REV_SUBTRACT`, `D3D12_BLEND_OP_MAX`; `D3D12_COMPARISON_FUNC_NEVER`, `_EQUAL`, `_GREATER`, `_NOT_EQUAL`, `_GREATER_EQUAL`). A mapper's `default:` must stay a defined value, never fall off the end. If another backend file has the same mappers, extend it too (only D3D12 is expected).
- [ ] **Step 2:** Rebuild the SDK with `build_runtime_sdk.cmd` and the install directory from Global Constraints; rebuild the game. No `rexruntime.def` change is expected (enums only); if the linker disagrees, report before adding exports.
- [ ] **Step 3: Verify:** native tests green; one split run of the bridge scene with counts and screenshot unchanged (the new enumerators are not used yet).
- [ ] **Step 4: Commit** in the submodule (`Native RHI: full blend factor, blend operation and compare function sets`, with the two trailer lines), then in the game repo:

```bash
git add thirdparty/rexglue-sdk
git commit -m "SDK: native RHI blend factors, operations and compare functions"
```

---

### Task 5: Material table with alpha recipe and kind; alpha trace tool

**Files:**
- Modify: `tools/xdk_sigmatch/gen_albedo_table.py`, `tests/test_gen_albedo_table.py`, `tools/xdk_sigmatch/shader_trace.py`, `tests/test_shader_trace.py`, `src/native/capture/material.h`, `tests/native/test_material.cpp`, `src/native/capture/capture.cpp` (the table block near lines 374 to 400 and `FillMaterial`), `src/native/capture/ps_albedo_table.inc` (generated)

**Interfaces:**
- Produces:
  - JSON: optional `"alpha"` in an albedo entry: `{"source": "albedo", "component": "x|y|z|w", "scale": "cN.c"}` (`scale` optional), `{"source": "one"}`, or `{"source": "fetch", "slot": n, "component": "...", "scale": "cN.c"}` (same UV as the albedo). Optional `"kind"`: `"plain"` (default), `"terrain-layer"`, `"terrain-far"`. The generator raises `ValueError` naming the shader for an unknown source, component or kind, a `scale` that is not a pixel constant reference, `"alpha"` on a `no_albedo` entry, or `"source": "fetch"` without `slot`.
  - Macro: `FABLE2_PS_ALPHA(hash, source, slot, component, scale_ref)` (source 0 one, 1 albedo, 2 fetch; `scale_ref` = register * 4 + component, or -1) and `FABLE2_PS_KIND(hash, kind)` (0 plain, 1 terrain-layer, 2 terrain-far), emitted after the existing lines; the table block in `capture.cpp` defines both.
  - `material.h` (pure): `enum class AlphaSource : uint8_t { kNone, kOne, kAlbedo, kFetch };` `struct AlphaSpec { AlphaSource source = AlphaSource::kNone; uint8_t slot = 0, component = 3; int32_t scale_ref = -1; };` `enum class MaterialKind : uint8_t { kPlain, kTerrainLayer, kTerrainFar };` `Material` gains `AlphaSpec alpha; float alpha_scale = 1.0f; MaterialKind kind = MaterialKind::kPlain;` (`alpha_scale` is the pixel constant's value read at the draw; 1 without a scale).
  - `enum class AlphaRule : uint8_t { kOpaque, kApply, kDrawAlphaUnknown, kSkipBlendUnknown };` and `AlphaRule DecideAlpha(bool alpha_test_or_mask, bool blend, bool colour_known, bool has_recipe);` with: neither test nor blend gives `kOpaque`; blend and not colour known gives `kSkipBlendUnknown`; (test or blend) and recipe gives `kApply`; otherwise `kDrawAlphaUnknown`.
  - `MaterialStatus` gains nothing; the two new reasons are counted by the pass (Task 6) from `AlphaRule`.
  - `shader_trace.py`: `trace_ps_alpha(instrs)` returns `{"source": ..., "slot": ..., "component": ..., "scale": ...}` or raises `Unsupported(reason)`; `find_kills(instrs)` returns the list of kill instructions with index and predicate. `trace_ps_alpha` accepts `oC0.w` written exactly once from: a literal 1 (source one), one component of a 2D `tfetch` result, or that component multiplied by one pixel constant component; it refuses predicated writes, more than one writer, and anything else.

- [ ] **Step 1: Failing tests.** Python: generator cases (each valid form emits the expected macro line; each invalid form raises); `trace_ps_alpha` on three real dumps (`out/shader_dump/shader_7CD57B81550F19E3.ucode.frag` reads `oC0.w = c15.w x r0.x`: expect the trace of `r0.x` to a fetch component or `Unsupported` with the reason; `0x7DDAD5C29B3F56EE` writes `oC0.xyz1`: source one; one grass pixel shader already in the table, e.g. `0x00E09D1BC5295D52`: record what it yields) plus synthetic microcode for the refused shapes; `find_kills` on `0xA17D8AEC3A817D45` (a predicated `kill_gt` at instruction 16). If a dump file is missing the test must skip with a message, not pass silently. Native: `DecideAlpha` truth table (all 16 inputs), `Material` defaults.
- [ ] **Step 2: Run** both suites: the new cases fail.
- [ ] **Step 3: Implement** generator, tool, `material.h`, the table block and `FillMaterial` (look up alpha and kind; read the scale constant from the pixel constant bank at the draw with the existing bounded read; an unreadable constant leaves the recipe absent). Regenerate the `.inc`: with no `"alpha"`/`"kind"` in the JSON yet it must be byte-identical to the committed one apart from nothing (confirm).
- [ ] **Step 4: Verify.** Both suites green; release build; bridge scene counts unchanged.
- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/gen_albedo_table.py tools/xdk_sigmatch/shader_trace.py tests/test_gen_albedo_table.py tests/test_shader_trace.py src/native/capture/material.h src/native/capture/capture.cpp src/native/capture/ps_albedo_table.inc tests/native/test_material.cpp
git commit -m "Native materials: alpha recipe and kind in the material table, alpha trace tool"
```

---

### Task 6: State-driven pipelines, alpha test and blending in the clay pass

**Files:**
- Modify: `src/native/render/clay_logic.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/fable2_native_shaders.h`, `tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`, `README.md` (F3 lines)

**Interfaces:**
- Consumes: `RenderState`, `DecodeRenderState`, `StateWords` (Task 3); `Material::alpha`, `alpha_scale`, `kind`, `DecideAlpha` (Task 5); the RHI enums as they are today (`nrhi::BlendFactor` Zero, One, SrcAlpha, InvSrcAlpha; `BlendOp` Add, Min; `CompareFunc` Always, Less, LessEqual): Task 4 was dropped.
- Produces (pure, `clay_logic.h`):

```cpp
struct PipelineKey {
  uint8_t textured = 0;      // 0 flat, 1 textured, 2 terrain layer, 3 terrain far (2 and 3 from Task 10)
  uint8_t blend = 0, src = 1, dst = 0, op = 0, src_alpha = 1, dst_alpha = 0, op_alpha = 0;
  uint8_t color_mask = 0xF;
  uint8_t depth_test = 1, depth_write = 1, depth_func = 1;   // Compare values
  uint8_t cull = 0;          // Cull values
  bool operator==(const PipelineKey&) const = default;
};
PipelineKey MakePipelineKey(const capture::RenderState& s, uint8_t textured);  // invalid state: the default key
inline constexpr size_t kMaxClayPipelines = 64;
inline constexpr size_t kNoPipelineSlot = ~size_t(0);
// Index of `k` in `keys`, appended if absent and there is room; kNoPipelineSlot when the
// cache is full (the caller then draws with the default pipeline and counts the overflow).
size_t FindOrAddPipelineSlot(std::vector<PipelineKey>& keys, const PipelineKey& k, size_t max = kMaxClayPipelines);
```

  - `ClayConstants`: the two pad words become `uint32_t alpha_mode` (bits 0-2 compare function, bit 3 test on, bit 4 alpha source is the albedo's component, bits 5-6 the component index, bit 7 blend alpha wanted) and `float alpha_ref`; one more float `alpha_scale` (grow the struct by 4 words to keep 16-byte packing: `float alpha_scale; uint32_t pad2[3];` and update `static_assert`, `kConstantCount`, the root constant count and every HLSL `cbuffer Draw` together, as Task 7b of sub-project 5 did).
  - `ClayStats` gains `alpha_tested`, `blended`, `alpha_unknown`, `blend_unknown`, `state_invalid`, `pipelines`, `pipeline_overflow`. F3 `Textures:` line gains the reasons `alpha-unknown` and `blend-unknown` in its top-untextured list; a new suffix on the `Geometry:` line or a new line `State: P pipelines, alpha test A, blended B, invalid I` (check the overlay width; README updated to the exact text).
- Behaviour:
  - The pass keeps `std::vector<std::pair<PipelineKey, Pipeline>>` (linear search, at most `kMaxClayPipelines`); a key beyond the bound uses the default pipeline of its `textured` value and counts `pipeline_overflow`. Pipelines are never evicted within a run.
  - Per record: `rs = DecodeRenderState(r.state_words)`; `rule = DecideAlpha(rs.alpha_test || rs.alpha_to_mask, rs.blend, colour_known, material.alpha.source != kNone)`; `kSkipBlendUnknown`: the record is not drawn and counts `blend_unknown` (and as skipped with that reason on the `Native:` line); `kDrawAlphaUnknown`: drawn with the test off and alpha 1, counted; `kApply`: constants carry the mode, reference and scale. `colour_known` is: the record will be drawn textured this frame, or its material is a terrain kind with usable textures (Task 10). A colour mask of 0 with depth write on draws depth only (keep the pipeline, it writes no colour).
  - HLSL (`kClayTexturedPs`, and the terrain shaders in Task 10): after sampling, `float a = alpha_from(t) * alpha_scale;` then, if the test bit is set, `if (!cmp(a, alpha_ref, func)) discard;` with all eight compare functions; alpha-to-mask is passed as a test `a >= 0.5`. Output alpha is `a` when blending, else 1. The flat-colour shader (`kClayPs`) has no alpha source: an alpha-tested or blended flat draw can only come through `kDrawAlphaUnknown`.
  - Mapping to the RHI: when `depth_reversed`, mirror the depth function (Greater to Less, GreaterEqual to LessEqual, Less to Greater, LessEqual to GreaterEqual; Equal, NotEqual, Always, Never unchanged) because the clay pass keeps its own non-reversed depth; a blend factor, blend operation or (mirrored) depth function the RHI cannot express makes the state unsupported: a blended draw with an unsupported blend state is left out and counted `blend-unknown`, an unsupported depth function falls back to the default depth state and counts `state_invalid`. Task 2's census: opaque draws use GreaterEqual with write on under a reversed viewport; terrain layers blend with (SrcAlpha, One) or (SrcAlpha, Zero).
  - Depth and cull from the key. Culling: use the captured mode with the front-face convention established in Task 2's write-up; if Task 2 left it open, start with culling as captured, compare a native-view screenshot of the bridge scene with the emulated frame, and if faces the emulated image shows are missing, flip the convention once, globally, and record the evidence.

- [ ] **Step 1: Failing tests.** `test_clay_logic.cpp`: `MakePipelineKey` for the default state, for an invalid state (equals the default key), for two states that differ only in alpha reference (same key: the reference is a constant, not pipeline state), for states that differ in each key field (different keys); `FindOrAddPipelineSlot` returns the same index for an equal key, appends until `max`, then returns `kNoPipelineSlot` for a new key while still finding the existing ones (Review Focus 2); `MakeClayConstants` carries mode, reference and scale and leaves them zero/one for an opaque draw; the stats text with the new counters. `test_native_shaders.cpp`: the textured pixel shader compiles and contains a `discard`; the cbuffer declarations of all clay shaders declare the new fields (as the cut test does).
- [ ] **Step 2: Run**: compile failures.
- [ ] **Step 3: Implement** the pure parts, then the pass and the HLSL.
- [ ] **Step 4: Verify.** Suites green; release build; bridge scene, native and split runs with screenshots: nothing that was drawn before is missing except draws now skipped as `blend-unknown` (list them by pixel shader from the log; the dog's fur shells `0x014F8A02DB7B19CA` are expected there if their state blends, and then the dog shows its textured body); alpha-tested draws are still rectangles (no recipes yet) and counted `alpha-unknown`; frame rate 30.0 in live windows; clay cost (`hash`, `decode`, `max total`) as before within noise; number of pipelines reported. View-off run: capture median under 0.5 ms.
- [ ] **Step 5: Commit**

```bash
git add src/native/render/clay_logic.h src/native/render/clay_pass.cpp src/native/render/clay_pass.h src/native/fable2_native_shaders.h tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp README.md
git commit -m "Native materials: pipelines by render state, alpha test and blending in the clay pass"
```

---

### Task 7: Alpha recipes and the missing pixel shaders

**Files:**
- Modify: `docs/native-renderer/ps-albedo.json`, `src/native/capture/ps_albedo_table.inc` (generated), `docs/native-renderer/vs-transforms.json` and `src/native/capture/vs_transform_table.inc` (the `"uv"` entry of `0xA5846836C90E1192` and of any vertex shader the new pixel shaders pair with), `docs/native-renderer/frame-map.md` (section 13, "Alpha and missing shaders"), `docs/native-renderer/user-checks.md`

**Interfaces:**
- Consumes: Tasks 1 to 6; `tools/xdk_sigmatch/albedo_finder.py`, `shader_trace.py`, `texture_thumb.py`, `position_check.py`; the captures of Task 2.

- [ ] **Step 1: Which shaders need a recipe.** From Task 2's census on both captures list every pixel shader with draws whose state has alpha test, alpha-to-mask or blending on, with draw counts and whether it has an albedo entry. This list, sorted by draws, is the work list; record it in the frame map.
- [ ] **Step 2: Recipes for shaders already textured** (grass, ferns, plants, fence and prop shaders on the list): run `trace_ps_alpha`; for each accepted trace add `"alpha"` with the evidence; for each refusal read `oC0.w` by hand with instruction numbers and either add the recipe or record why not. Check each against a thumbnail whose alpha channel shows the cut-out shape (extend `texture_thumb.py` with an alpha-only output if it has none; test first).
- [ ] **Step 3: Leaves.** `0x7CD57B81550F19E3`: hand trace of the tf0 coordinate back to the vertex shader's outputs (the tool refuses with "scalar co-issue mulsc") and of `oC0.w = c15.w x r0.x`; add the albedo entry with `"alpha"` and the `"uv"` entry for `0xA5846836C90E1192` (hand trace of the export, as sub-project 5 did for the instancing shaders). Thumbnail and UV range check on dumped vertices.
- [ ] **Step 4: The shaders near the trees.** In the walking capture find the rows of `0x58D3FAAAA84A48AA`, `0x44B567635EB10620`, `0x2FBE9B4EE822609A`, `0x14217F5C1118D00F`, `0x09CB69BD265B58D1`, `0xF96A4494958BFA12` and any other `ps-unknown` shader with more than about 1% of the scene's draws; for each: its vertex shader(s) (does it have a transform and a `"uv"` entry?), `albedo_finder` and traces, thumbnail, entry with `"alpha"` where its state needs one. A shader absent from the capture stays unentered with the reason recorded and a user check (16 already asks for a capture by the trees; refer to it). The one without a dump must have been dumped by Task 2's second capture; if it never appeared, say so.
- [ ] **Step 5: The two mask shaders and the fur shells.** `0xA17D8AEC3A817D45`, `0xF6D98C7B4D98438B`: read the dumps and their captured state; either an entry (albedo and recipe) with evidence, or the recorded reason they stay `no_albedo` and which rule they fall under. `0x014F8A02DB7B19CA`: confirm from the census whether its state blends; it stays `no_albedo`; record that it is now left out as `blend-unknown` (or, if its state does not blend, that it is still drawn opaque and why).
- [ ] **Step 6: Regenerate, build, run.** Bridge scene and the walking plan, native and split views, screenshots at the end points. Acceptance: grass, ferns and leaves show cut-out silhouettes against the emulated half in split view and in native view; trunks and leaves by the trees are textured; the dog shows its textured body; `alpha-unknown` and `blend-unknown` counts by shader are reported and each remaining shader has a recorded reason. Compare textured counts before and after.
- [ ] **Step 7: User checks.** Add check 17 (cut-outs and tree textures in town and an interior: exact command, what right and wrong look like) and note in check 16 what was achieved by walking.
- [ ] **Step 8: Commit**

```bash
git add docs/native-renderer/ps-albedo.json src/native/capture/ps_albedo_table.inc docs/native-renderer/vs-transforms.json src/native/capture/vs_transform_table.inc docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md tools/xdk_sigmatch/texture_thumb.py tests/test_texture_thumb.py
git commit -m "Native materials: alpha recipes, leaf and tree shader entries"
```

---

### Task 8: Single-channel texture format (`k_DXT3A`)

**Files:**
- Modify: `src/native/render/texture_decode.h` (pure), `src/native/render/texture_cache.cpp`, `tests/native/test_texture_decode.cpp`, `tools/xdk_sigmatch/texture_thumb.py`, `tests/test_texture_thumb.py`

**Interfaces:**
- Produces:
  - `texture_decode.h`: `bool DecodeDxt3aLevel(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height, uint32_t pitch_blocks, bool tiled, Endian endian, std::vector<uint8_t>* out_r8);` writing `width * height` bytes (each 4-bit value v becomes `v * 17`); returns false, leaving `out_r8` empty, when the source is smaller than the layout needs. Block layout: `k_DXT3A` (Xenos format 58) is the explicit-alpha half of a DXT3 block, 8 bytes per 4 x 4 block, 4 bits per texel, rows low nibble first; confirm against the SDK's texture conversion code (`thirdparty\rexglue-sdk\src\graphics\pipeline\texture\`, search `DXT3A`) and cite the lines; the SDK wins if it differs.
  - The format table accepts format 58 and maps it to an R8 upload (`nrhi::Format::kR8_UNORM`); the fetch swizzle is applied through the view as for the other formats.
- [ ] **Step 1: Failing tests.** A hand-built 8 x 4 image of two blocks with known nibbles, linear and tiled, both endians the game uses, expected bytes written out; a source one byte short (false, empty output); a real region dumped from tf18 by Task 9's capture if available at this point, otherwise from a discovery texture dump made in this task with `DumpTexture` for the terrain's tf18 (extend the tess row writer to dump it), compared with the Python decoder's output for the same bytes (`texture_thumb.py` gains format 58, test first).
- [ ] **Step 2: Run**: failures. **Step 3: Implement.** **Step 4: Verify:** suites green; build; bridge scene unchanged (no entry uses the format yet).
- [ ] **Step 5: Commit**

```bash
git add src/native/render/texture_decode.h src/native/render/texture_cache.cpp tests/native/test_texture_decode.cpp tools/xdk_sigmatch/texture_thumb.py tests/test_texture_thumb.py src/native/capture/capture.cpp
git commit -m "Native materials: k_DXT3A as a single-channel texture"
```

---

### Task 9: Terrain reading, inputs and offline replay

**Files:**
- Modify: `src/native/capture/capture.cpp` (tess rows and terrain inputs), `src/native/capture/terrain_patch.h` (pure), `tests/native/test_terrain_patch.cpp` (or the existing terrain test file), `docs/native-renderer/frame-map.md` (section 13, "Terrain"), `docs/native-renderer/ps-albedo.json` (terrain kinds), generated `.inc`
- Create: `tools/xdk_sigmatch/terrain_replay.py`, `tests/test_terrain_replay.py`

**Interfaces:**
- Background (fact sheet, to be verified by reading): terrain vertex shaders `0xC30A97D946FA2BE4`, `0xFB68A7F2301210E1` (builder `8221C9C8`) and `0x5003700B7C9B1C16` (builder `82207C30`); pixel shaders `0x9C2144E42E95A76F`, `0x4755A2E4A05BA678`, `0x45243B758F9272AB`, `0xAF1E16EE57E4BB06`, `0x04F23292B2F44077`, `0x578342593A37DD35` (layers) and `0x7DDAD5C29B3F56EE`, `0x2F8A0341E8C09698` (far). Layer colour `tf0^2 x (pow(tf3, c21.w) x c21.xyz x lighting + c20.xyz) + tf0^2 x sun term x c87`, then fog, then `x c20.w`; alpha `(1 - sat(dist^2 x c31.x + c31.y)) x weight`, weight `c253.w x (tf18.w - c253.y)` from the vertex shader; hole test `tf18.w < c114.x`; tf3 UV `(world.xy - c47.zw) x c14.xy`; detail UV `world x c8.x / c8.y`; tf18 addressed through `c115`. Far colour `tf13^2 x (pow(tf3) x light + c20)`, opaque.
- Produces:
  - Frame-map "Terrain": the reading of all eight pixel shaders and three vertex shaders with instruction numbers: for each term whether it is base colour, lighting, sun, fog or fade; sampled values of every constant used (min, max, typical over the capture) and what it is; the detail and base UV formulas; the tf18 addressing; the weight, fade and hole formulas; the differences between the shader variants; what the unlit rule drops.
  - `TerrainPatch` gains `struct TerrainMaterialInputs { uint32_t detail_fetch[6], base_fetch[6], weight_fetch[6]; float detail_uv[4]; float base_uv[4]; float weight_uv[4]; float base_pow; float base_tint[3]; float weight_scale, weight_bias; float fade[2]; float hole_ref; float eye[3]; bool valid; };` (names of the fields may follow the reading; the meaning of each must be stated in a comment with the constant it comes from) and a pure `MakeTerrainMaterialInputs(...)` from the captured constants and fetch constants, unit-tested with real values from the capture.
  - Discovery: tess rows record the constants and fetch constants the reading needs and dump tf0, tf3, tf13 and tf18 (`DumpTexture`), so the replay and Task 8 have real bytes.
  - `terrain_replay.py <capture> --frame N --patch K --out image.png`: decodes the dumped textures and evaluates the unlit layer formula for one patch (all its layers in draw order, blended with the captured blend state) on a pixel grid; `--weights image.png` writes the weight and hole map of a region.
  - `ps-albedo.json`: the eight terrain pixel shaders get `"kind"` entries (`terrain-layer` or `terrain-far`) with the slots of base, detail and far textures and the reading as evidence. Extend the generator and its tests for the slot fields of terrain kinds (`FABLE2_PS_TERRAIN(hash, kind, base_slot, detail_slot)`), test first.
- [ ] **Step 1: Read** the shaders; write the frame-map subsection. Stop and report if the reading contradicts the background in a way that changes the design (for example more than one detail texture per draw, or a weight that is not from tf18).
- [ ] **Step 2: Discovery additions and a capture** of the bridge scene and the walking scene with terrain textures and constants; copy to `out\native-evidence\materials\`.
- [ ] **Step 3: Replay (TDD).** Tests with a synthetic two-layer patch (known textures and constants, expected pixels) first; then the tool; then run it on a bridge-scene patch that has a path or a grass-to-dirt transition and on the weight map of the bridge area. View the images with the Read tool. Acceptance: the weight and hole map is recognisable against the emulated ground (the path, the lake shore, the bridge ends) and the replayed patch shows the emulated ground's pattern (lighting aside). If the image is noise, the tf18 decode or its addressing is wrong: fix it here, with Task 8's decoder.
- [ ] **Step 4: Inputs (TDD).** `MakeTerrainMaterialInputs` with tests from real captured values (compare with the replay's intermediate numbers for the same patch); capture fills it for terrain records (bounded reads; invalid when a constant or fetch constant cannot be read).
- [ ] **Step 5: Verify.** Suites green; build; bridge scene counts unchanged (terrain is still drawn as clay).
- [ ] **Step 6: Commit**

```bash
git add src/native/capture/capture.cpp src/native/capture/terrain_patch.h tests/native tools/xdk_sigmatch/terrain_replay.py tools/xdk_sigmatch/gen_albedo_table.py tests/test_terrain_replay.py tests/test_gen_albedo_table.py docs/native-renderer/ps-albedo.json src/native/capture/ps_albedo_table.inc docs/native-renderer/frame-map.md
git commit -m "Native materials: terrain shader reading, captured inputs and offline replay"
```

---

### Task 10: Terrain material in the clay pass

**Files:**
- Modify: `src/native/fable2_native_shaders.h`, `src/native/render/clay_logic.h`, `src/native/render/clay_pass.cpp`, `src/native/render/clay_pass.h`, `src/native/render/texture_cache.cpp` (if a second lookup path is needed), `tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`, `README.md`

**Interfaces:**
- Consumes: `TerrainMaterialInputs` (Task 9), the R8 format (Task 8), `PipelineKey::textured` values 2 and 3 and the state path (Task 6), `MaterialKind` (Task 5).
- Produces:
  - HLSL `kClayTerrainLayerPs` and `kClayTerrainFarPs`: inputs world xy from the vertex shader (the clay vertex shader passes `p.xy` in a new interpolator for every draw; other pixel shaders ignore it), textures in a second table (`t4` base, `t5` detail or far, `t6` weight), samplers from the static four by each fetch constant's filter and wrap (base clamps, detail wraps: take it from the fetch constants, not from this sentence). Layer: `colour = detail^2 x base'`, `alpha = saturate(weight) x fade`, hole discard, then the common alpha test and output code of Task 6. Far: `colour = far^2 x base'`, alpha 1, hole discard. The exact expressions are the ones the reading and the replay of Task 9 established; the HLSL must match the replay term for term, and a comment names the frame-map subsection.
  - Terrain constants: a second root constant block or a constant buffer for the terrain parameters (choose by size; the D3D12 root signature limit is 64 words in total and the plain signature uses about 43 after Task 6).
  - `ClayStats`: terrain draws drawn with the material count as textured; a terrain draw whose textures are not usable this frame is drawn as flat clay and counted under the texture cache's reason; `TerrainMaterialInputs::valid == false` counts `terrain-inputs` under the existing `render-other` style counter or a new reason (state which in the README).
  - The `Textures:` share is now of all drawn draws with terrain included in the numerator when textured.
- Behaviour notes:
  - Layer draws rely on the captured depth function and blending (Task 6). If layers z-fight or vanish on the fixed 16 x 16 grid (the game tessellates the same patch more finely), apply in this order and record what was needed: (1) make sure every layer of a patch is built from the same cached grid (identical vertex positions give identical depth); (2) if the base and the layers use different builders or grids, a small constant depth bias on blended terrain draws through the vertex shader's z; do not disable depth testing.
  - Texture table tuples: bind the white view for an absent slot explicitly (the D3D12 backend skips null entries); each distinct tuple takes slots in a 32768-slot heap until a view is destroyed: report the number of distinct terrain tuples per frame.
- [ ] **Step 1: Failing tests.** Shaders compile; both terrain shaders contain a `discard`; the constants struct for terrain packs the inputs at the offsets the HLSL declares (a test that writes distinct values and checks byte offsets against a table kept beside the HLSL); `MakePipelineKey` for textured values 2 and 3; stats text with terrain textured.
- [ ] **Step 2: Run**: failures. **Step 3: Implement.**
- [ ] **Step 4: Verify.** Suites green; build; bridge scene and the walking plan, overlay, split and native views with screenshots (overlay at 50% shows whether the native ground pattern lies on the emulated one). Acceptance (spec success criteria 1 and 4): paths, grass, dirt and rock lie on the emulated ground at the bridge and by the trees and at one more spot (a second walking plan, recorded); holes are cut where the emulated ground has them and nowhere else; the textured share of all drawn draws is at least 90% at each spot (report the figure and the remaining reasons by shader); 30.0 fps in live windows; `hash`, `decode`, `max total`, texture and geometry resident MB against Task 7's run; view-off capture median under 0.5 ms. Describe every screenshot honestly, including seams between patches, layers that look too strong or too weak, and anything worse than before.
- [ ] **Step 5: Commit**

```bash
git add src/native/fable2_native_shaders.h src/native/render/clay_logic.h src/native/render/clay_pass.cpp src/native/render/clay_pass.h src/native/render/texture_cache.cpp tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp README.md
git commit -m "Native materials: terrain layer and far materials with holes"
```

---

### Task 11: Validation and documentation

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (section 13 "Validation"), `docs/native-renderer/user-checks.md`, `docs/superpowers/specs/2026-10-06-native-renderer-materials-design.md` (status line, "Implementation notes (deviations)"), `README.md`

- [ ] **Step 1: Autoplay measurements** (one after another, 120 s each unless a walking plan needs more; freeze signature checked on each): renderer off with `-Env @{FABLE2_GUEST_WORK_LOG="1"}`; view off; split and native at the bridge with `-Shots`; overlay, split and native with the two walking plans. Record: fps per window, guest work medians, view-off capture median, captured / drawable / textured with the share of all drawn draws, the reasons that remain by shader (`alpha-unknown`, `blend-unknown`, `no-albedo`, `ps-unknown`, texture reasons), pipelines, alpha-tested and blended draw counts, clay `hash` / `decode` / `max total`, geometry and texture resident MB.
- [ ] **Step 2: Screenshots** judged against success criteria 1 to 3, each described; side-by-side statements of what the emulated frame shows and what the native frame shows for ground, grass, leaves, trunks and the dog.
- [ ] **Step 3: Frame-map section 13 "Validation"**: the criteria table with logs named, known limitations (at least: no lighting so colours differ in brightness from the emulated frame; draws left out as `blend-unknown` with their shaders; entries that rest on hand traces; the fixed terrain grid; anything the reading dropped; sorting not done), follow-up candidates.
- [ ] **Step 4: User checks**: town, field and interior for ground, cut-outs, trees and the dog (exact commands, right and wrong pictures); the F3 lines to report; nothing renumbered.
- [ ] **Step 5: Spec** status line and "Implementation notes (deviations)" listing every difference from the spec and every ruling in the ledger.
- [ ] **Step 6: README**: F3 lines, reasons, autoplay stick tokens, anything user-visible that changed.
- [ ] **Step 7: Commit**

```bash
git add docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md docs/superpowers/specs/2026-10-06-native-renderer-materials-design.md README.md
git commit -m "Materials validation results (autoplay; user checks pending)"
```
