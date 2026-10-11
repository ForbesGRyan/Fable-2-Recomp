# Native renderer foliage (wind, part coverage, the tree occluder): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. One fresh
> implementer and one fresh reviewer per task; steps use checkboxes.

**Goal:** the native picture's trees, bushes and grass sway as the game's do, in colour, depth and
shadow (11a); foliage the game shows part-covered is drawn part-covered with the game's first
sample's depth (11b); the sun layer's tree occluder is drawn, the darkness about the sun is traced
on a stored native scene, and sub-project 10's criterion 19 is judged again (11c).

**Architecture:** discovery captures keep whole index buffers and a rasterising replay tool turns a
capture frame into the oracle of all three phases. Per-draw wind inputs (the game's registers and
the vertex elements' places) travel in a side array of the frame's scene, indexed from the record's
tail padding; the geometry cache holds the per-vertex wind words; one wind vertex program (the clay
vertex shader compiled with `CLAY_WIND`) moves the position for the colour pass, the pre-pass, the
shadow maps and the tree occluder. A coverage record is drawn as two draws (fully covered pixels in
place, half-covered pixels blended at one half in a tail), the sky step before the first blend. A
frame, a record or a switch that fails any rule is today's picture, unchanged.

**Tech stack:** C++ (capture in `src/native/capture`, pass in `src/native/render`, the switches in
`src/native/fable2_native_render.cpp`), HLSL in `src/native/fable2_native_shaders.h`, Python tools
in `tools/xdk_sigmatch` (numpy), tests in `tests/` and `tests/native/`.

**Spec:** `docs/superpowers/specs/2026-10-10-native-renderer-foliage-design.md` (its Design,
ruling and criterion numbers are cited). Research: `.superpowers/sdd/foliage-research.md` (read it
before any task; "R" with a section cites it; its scripts are in
`out/native-evidence/foliage/research/`).

## Global Constraints

- Branch `native-renderer-foliage`. **No stash, reset, push, amend or rebase by an agent**; no
  branch switch. **Never stage `fable_2_manifest.toml`; never `git add -A`**; `*.ps1` is
  git-ignored and needs `git add -f`. Every commit is a pathspec commit:
  `rtk git add <paths>` then `rtk git commit --only -F <message file> -- <paths>`. The commit
  trailer is exactly these two lines after a blank line:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`. New files have LF
  line endings.
- **Every shell command is prefixed with `rtk`** (`rtk git ...`, `rtk python ...`,
  `rtk clang++ ...`, `rtk .\build.cmd ...`).
- **`DrawRecord` stays 616 bytes** (`static_assert` in `src/native/capture/draw_record.h`).
- **No SDK change** (`thirdparty/rexglue-sdk`) without need: none is foreseen. A seeming need:
  stop and report. If the controller rules one in: **no `;` comment lines in
  `src/system/rexruntime.def`**, and the SDK is rebuilt with
  `tools\build_runtime_sdk.cmd C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64`.
- **The game starts only through `tools/drive_game.ps1`**, with `-BootSave Hero000`. **Agents
  never pass `-UserPresent`**: an agent that needs a run reports `NEEDS_RUN` with the exact lines
  below, what each run must produce and where it wants it copied; the controller makes the run
  (ledger ruling 2) and its evidence is labelled `RUN UNCHECKED`. Every run is checked for the
  freeze signature before a number or picture is used.
- **Before any real-GPU launch of a build that changes bindings, targets or shader text: one
  clean 240 s software-adapter run with the debug layer** (`--d3d12_adapter=-2 --d3d12_debug=true
  --fable2_native_d3d_messages=true`: the lines WARP and WARP-SUN). Tasks 2, 10, 13, 14, 16 are
  such builds (17 too if it mends shader text).
- **A stick entry of a plan holds at most 60000 ms**; a longer hold is a chain of entries.
  **No shot within 2 s of a view switch.**
- **Timing runs count only at shot width 1942, with NO agent running beside them** (no analysis,
  no build, no test). Every other run is "no timing evidence".
- **Antivirus rule**: test executables are built only under `out\tests\runtime-unit-tests\`. A
  binary that is removed, locked or refused: stop and report BLOCKED with file, time and message;
  no retry loop, no rename, no change of options. While the locked
  `out\tests\runtime-unit-tests\native_shaders.exe` exists, the suite runs through a scratch copy
  of the runner in which only the shader test's output name differs
  (`out\tests\run_native_tests_t<N>.cmd`, generated from the present script each time).
- **One builder at a time** (one process building the game or the test executables in this
  tree). **Reviewers are read-only.** **A mutation check is run alone**: on a scratch copy, never
  on a tracked file, with no other test, build or game running.
- **New targets are resolution-independent**: a size is `ClayPass::kWidth` / `kHeight` divided by
  1, 2 or 4 with a static assert that the division is exact; texel sizes reach a shader as
  constants. **A write into an 8-bit or 10-bit target writes the rounded level itself**
  (`floor(v * 255 + 0.5) / 255`, `/ 1023`).
- Guest reads are bounded and fail closed; no value from the guest reaches a shader without a
  finiteness check and a bound. Nothing is read with the native view off or with the phase's
  switch off; with a switch off every compiled program equals today's.
- Tests first where a pure function can hold the rule; a Python reference, machine-checked
  against the shader dumps, and a C++ mirror pinned to it come before any shader text.
- Evidence goes to `out/native-evidence/foliage/`; every number names its file; the research's
  labels ([measured], [replay], [microcode], [code], [model], [inferred], [unknown]) are kept
  where its facts are repeated.
- New reason strings: `wind` (with `off`, `no-entry`, `register-unread`, `register-not-finite`,
  `register-range`, `literal`, `element`, `stream`, `instance`, `over-limit`, and in the pass
  `buffer`, `pipeline`, `slot`), `coverage` (`pipeline`). New switches: `--fable2_native_wind`,
  `--fable2_native_coverage` (the light switch's parser: on, true, 1, off, false, 0; default
  off), the diagnostic `--fable2_native_scene_dump` (default off).

**The workspace's rule files** (`.superpowers/sdd/2026-10-10-native-renderer-foliage/`:
`global-constraints.md`, `implementer-rules.md`, `reviewer-rules.md`) were copied from sub-project
10 and bind every task. Where they name the previous sub-project, read: branch
`native-renderer-foliage`; plan and spec the two files above; research
`.superpowers/sdd/foliage-research.md` with its scripts in `out/native-evidence/foliage/research/`;
the rules folder `.superpowers/sdd/2026-10-10-native-renderer-foliage/`; the evidence folder
`out/native-evidence/foliage/` (previous: `out/native-evidence/post/`; frame-map section 17 names
its files); the plan's "Additions" are this plan's run lines. Their "only while the user is away"
binds agents as written; the controller's runs follow ledger ruling 2.

**Commands.** Python suite: `rtk python -m unittest discover -s tests -p "test_*.py"`; one file:
`rtk python -m unittest tests.test_wind_formulas -v`. Native suite: `rtk cmd /c
tests\run_native_tests.cmd` (or the scratch copy, above); one test: `rtk clang++ -std=c++23
tests\native\<file>.cpp -o out\tests\runtime-unit-tests\<name>.exe` then `rtk
out\tests\runtime-unit-tests\<name>.exe`. Game build: `rtk .\build.cmd -release fable_2` (no
`fable_2.exe` running; never kill one you did not start).

**Run lines** (after `tools\drive_game.ps1 -BootSave Hero000`; the controller adds `-UserPresent`;
`<args>` is appended to `-GameArgs` after a comma):

| Name | Arguments |
|---|---|
| BRIDGE | `-Total 96 -Shots "35,39,43,47,51,55,59,63,92" -Plan "32:VIEW=off,36:VIEW=native,40:VIEW=off,44:VIEW=native,48:VIEW=off,52:VIEW=native,56:VIEW=off,60:VIEW=native,66:DISC=8/1" -GameArgs --fable2_native_render=true` |
| THIRD | `-Total 100 -Shots "43,47,51,55,59,63,96" -Plan "33:LS=0/-100/3500,40:VIEW=off,44:VIEW=native,48:VIEW=off,52:VIEW=native,56:VIEW=off,60:VIEW=native,66:DISC=8/1" -GameArgs --fable2_native_render=true` |
| TREES | `-Total 108 -Shots "58,64,70,76,89,93,98" -Plan "33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/2500,47:RS=-60/0/600,50:RS=60/0/600,54:VIEW=off,60:VIEW=native,66:VIEW=split,72:VIEW=overlay,78:VIEW=native,82:DISC=8/1,86:VIEW=off,90:VIEW=native" -GameArgs --fable2_native_render=true` |
| SUNWARD | `-Total 86 -Shots "44,48,52,56,82" -Plan "32:VIEW=off,33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/14000,45:VIEW=native,49:VIEW=off,53:VIEW=native,58:DISC=8/1" -GameArgs --fable2_native_render=true` |
| STAND | `-Total 96 -Shots "36,39,42,51,54,57,66,69,72,81,84,87,93" -Plan "32:VIEW=off,47:VIEW=native,62:VIEW=off,77:VIEW=native,88:DISC=4/1" -GameArgs --fable2_native_render=true` |
| CLOCK | `-Total 90 -Shots "86" -Plan "32:VIEW=native,36:DISC=2/1,56:DISC=2/1,76:DISC=2/1" -GameArgs --fable2_native_render=true` (if the third `DISC=` is refused: drop `76:DISC=2/1`) |
| WARP | `-Total 240 -Shots "225" -Plan "32:VIEW=native" -GameArgs --fable2_native_render=true,--d3d12_adapter=-2,--d3d12_debug=true,--fable2_native_d3d_messages=true` |
| WARP-SUN | `-Total 240 -Shots "225" -Plan "33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/14000,45:VIEW=native" -GameArgs --fable2_native_render=true,--d3d12_adapter=-2,--d3d12_debug=true,--fable2_native_d3d_messages=true` |

Suffixes: READBACK `--readback_resolve=full,--native_render_collect_stats=true`; POST
`--fable2_native_post=on,--fable2_native_bloom=on`; SUN `--fable2_native_sun=on`; WIND
`--fable2_native_wind=on`; COV `--fable2_native_coverage=on`; DUMP
`--fable2_native_scene_dump=on`. PAIR-SUN is SUNWARD with POST, SUN, DUMP and READBACK (and WIND,
COV as the task says). BRIDGE, THIRD, TREES and SUNWARD are sub-project 10's lines with other
shot seconds: **every shot is at least 3 s after a view switch** (its shots 2 s after a switch
caught the first native frames: FM 6760), and SUNWARD's three brackets are native 48 s over
emulated 44 s, 48 over 52 and 56 over 52; its capture begins at 58 s in the native view, so that a
scene dump pairs with it. In STAND every shot is at least 4 s after a view switch and the pairs
are 3 s and 6 s apart (the sway's periods are 5.7 to 7.1 s: a pair 3 s apart is near half a
period).

## Review Focus

The five failure modes most likely to pass unnoticed, each pinned by a test in its owning task:

1. **A wind record moved in one pass and at rest in another** (holes over every tree; shadows
   beside their casters). Owner: Task 10. Pin: `DecideClayWind`'s all-or-none test (case 720),
   `tests/test_wind_literals.py::test_one_vertex_program_for_every_pass`, and criterion 7's holes
   count on a stored scene.
2. **An unbounded or retyped read at `t9`** (a buffer bound by address has no bounds check; the
   wind program reads floats where the rest program reads float2 terrain normals). Owner: Tasks 9
   and 10. Pin: `WindWordsNeeded`'s test, the shader test "the wind program loads `t9` only inside
   the family's branch and only for a vertex below `vertex_count`", and `DecideClayWind` giving
   none for a terrain record (case 721).
3. **A formula check that cannot fail** (a tolerance that passes a wrong term; inputs on which
   nothing moves; a mirror checked against itself). Owner: Tasks 5 to 7. Pin:
   `test_mutations_fail` and the `moved` floor of `check_formula` in
   `tests/test_wind_formulas.py`.
4. **Coverage's order and sets** (a blend with nothing behind it because the sky comes later; the
   early tail after the layer's depth; a pixel in both draws or in neither, so that the depth is
   not today's). Owner: Task 12. Pin: the exhaustive tests of `PlanCoverageOrder` (case 741) and
   `CoveredSamples` (cases 700 to 702).
5. **A switch that is off and still costs or changes something** (the `#ifdef` text reaching
   today's programs; a wind register read with the switch off; a side index left from another
   record). Owner: Tasks 8, 10, 13. Pin: the shader test "programs without the define equal the
   baseline" (Task 10, Task 13), `FrameBuilder`'s index tests (Task 8, returns 150 to 156), and
   the frame row's `"wind":"off"` on an off-run (criterion 8).

Also for every reviewer: guest reads whole or not at all; the frame rechecked under the scene's
mutex; sizes as fractions of the logical size; evidence that names its file; what a run did not
show is said.

---

## Phase 11a: wind

Tasks 1, 3 and 5 run in parallel (3 and 5 need no game and none of Task 1's files; one builder at
a time still holds for the test executables). Task 2 follows Task 1. Task 4 needs 1 to 3. Tasks 6
and 7 follow 5. Tasks 8 to 11 follow in order.

### Task 1: Discovery additions (capture only)

**Files:** `src/native/capture/discovery_format.h`, `src/native/capture/capture.cpp`,
`tests/native/test_discovery_format.cpp`, `tools/xdk_sigmatch/check_discovery.py`,
`tests/test_check_discovery.py`, `docs/native-renderer/frame-map.md` (new section 18 "Foliage:
wind and coverage", before "Pending"; subsection "Discovery").

**Consumes:** `DumpStream(uint32_t phys, uint32_t bytes)` (`capture.cpp` line 1804),
`AppendGeometry` (1860), `WriteDrawRow` (2162: the `"ib"` member at 2202 to 2223), `IbView`,
`kMaxGeoStreamBytes` (1 MiB), the frame row (4861).

**Produces:**

```cpp
// discovery_format.h. The bytes of a draw's index buffer a discovery row keeps as a file: the
// buffer whole when it is at most `cap` bytes; else the draw's own range when that is; else none.
struct IndexDump { uint32_t offset_bytes = 0, bytes = 0; bool whole = false; };
inline IndexDump IndexDumpRange(uint32_t ib_size, uint32_t start, uint32_t count, bool index32, uint32_t cap);
```

- The `"ib"` member of every `"draw"` row gains `"file":"native_geo_<stamp>/<name>"`,
  `"file_offset":N`, `"file_bytes":M` (left out when `IndexDumpRange` gives none or `DumpStream`
  gives no name).
- A `"draw"` row outside the main scene whose vertex shader's table entry is `deformed`
  (`vs->transform && vs->transform->deformed`) gets `AppendGeometry`'s members (`"fetches"`,
  `"streams"`, `"idx"`), as an in-scene row has them.
- The `"frame"` row gains `"scene_frame":<the frame number of the scene published at this swap>`.
- The stored mip levels of an albedo sampled under an alpha test: only if `DumpTexture` can write
  them with the fetch constant's level count and no new guest address arithmetic; otherwise the
  frame map says so in one sentence and open question 7 stays with Task 16.
- `check_discovery.py` checks the new members (a named file exists and has `file_bytes` bytes;
  `scene_frame` rises by frame).

- [ ] Write the test first and see it fail (append to `tests/native/test_discovery_format.cpp`,
      numbers after the file's last):

```cpp
  {
    const uint32_t cap = 1u << 20;
    IndexDump d = IndexDumpRange(37444, 0, 18722, false, cap);              // the late leaf mesh: whole
    if (!(d.whole && d.offset_bytes == 0 && d.bytes == 37444)) return 60;
    d = IndexDumpRange(4u << 20, 100, 600, false, cap);                      // a shared buffer: the draw's range
    if (!(!d.whole && d.offset_bytes == 200 && d.bytes == 1200)) return 61;
    d = IndexDumpRange(4u << 20, 100, 600, true, cap);
    if (!(!d.whole && d.offset_bytes == 400 && d.bytes == 2400)) return 62;
    if (IndexDumpRange(4u << 20, 0, 600000, false, cap).bytes != 0) return 63;       // the range over the cap
    if (IndexDumpRange(4u << 20, 0x7FFFFFFFu, 2, false, cap).bytes != 0) return 64;  // a range past the buffer
    if (IndexDumpRange(4u << 20, 0xFFFFFFFFu, 0xFFFFFFFFu, true, cap).bytes != 0) return 65;  // no 32-bit wrap
    if (IndexDumpRange(0, 0, 0, false, cap).bytes != 0 || IndexDumpRange(4u << 20, 5, 0, false, cap).bytes != 0) return 66;
    d = IndexDumpRange(cap, 7, 3, false, cap);                               // exactly the cap: whole
    if (!(d.whole && d.bytes == cap)) return 67;
  }
```

- [ ] Implement `IndexDumpRange` (64-bit arithmetic) and the three row changes inside the
      discovery gate; nothing a record or the pass reads changes. Suites green; build.
- [ ] Frame map section 18, "Discovery": the members, the caps, what is not kept. Commit
      "Foliage discovery: whole index buffers, geometry of the casters' rows, the scene's frame".

### Task 2: The stored native scene (diagnostic)

**Files:** `src/native/render/scene_dump.h` (new, pure), `src/native/render/clay_pass.cpp` / `.h`,
`src/native/render/clay_logic.h`, `src/native/fable2_native_shaders.h`,
`src/native/fable2_native_render.cpp` (the switch), `src/native/capture/capture.cpp` / `.h`,
`tests/native/test_scene_dump.cpp` (new; two lines in `tests/run_native_tests.cmd`),
`tests/native/test_native_shaders.cpp`, README (the switch's row), frame map "Stored native scene".

**Consumes:** the oracle feed's read-back (`CopyTextureToBuffer`, `TakePostOracleDump`,
`clay_pass.cpp` lines 2167 to 2248); `fable2::native::ArmDiscovery` (`fable2_native_render.cpp`
line 918); Task 1's `"scene_frame"`.

**Produces:**

```cpp
// scene_dump.h, namespace fable2::native::render. File "F2NS", little-endian:
// u32 magic 'F2NS', u32 version 1, u32 width, u32 height, u64 scene_frame, u64 asked_frame,
// u32 flags, f32 exposure, u32 bad_values, u32 reserved; then width * height * 3 f32 (the linear
// target, rows from the top); width * height u32 (the scene depth's 24-bit level); and with
// kSceneDumpLayer two planes of (width / 2) * (height / 2) u32 (the sun layer's depth before and
// after the tree occluders).
inline constexpr uint32_t kSceneDumpMagic = 0x534E3246u, kSceneDumpVersion = 1;
inline constexpr uint32_t kSceneDumpPost = 1u, kSceneDumpLayer = 2u, kSceneDumpPrepass = 4u;
struct SceneDumpHeader { uint32_t magic, version, width, height; uint64_t scene_frame, asked_frame;
                         uint32_t flags; float exposure; uint32_t bad_values, reserved; };
static_assert(sizeof(SceneDumpHeader) == 48);
void DecodeR11G11B10(uint32_t word, float out[3]);       // the float target's texel
uint32_t DepthLevel24(float depth);                       // floor(sat(d) * 16777215 + 0.5); a NaN: 0
float DepthFromLevel24(uint32_t level);                   // level / 16777215
bool ParseSceneDump(const uint8_t* file, size_t bytes, SceneDumpHeader* head);   // sizes whole, magic, version
```

`shaders::kClayDepthPackPs` (the post layout; one depth texture at `t0`, `Load`; writes
`float4(level & 255, (level >> 8) & 255, level >> 16, 255) / 255.0` with
`uint level = uint(saturate(d) * 16777215.0 + 0.5)`, 0 for a value that is no number: whole byte
levels, so the 8-bit write is the rounded level itself). Targets `dump_depth_` (`kWidth` x
`kHeight`) and `dump_layer_[2]` (`kWidth / 2` x `kHeight / 2`), `R8G8B8A8_UNORM` with copy source,
**made on the first request only**. `capture::DiscoveryFirstSceneFrame()` (0 until the first frame
of the running capture is published). `ClayPass::RequestSceneDump(uint64_t frame)`,
`TakeSceneDump(nrhi::Device*, std::vector<uint8_t>* file)`. Switch
`fable2_native_scene_dump` (off); the file `logs/native_scene_<stamp>.f2ns`; one log line
`[native] scene dump: frame N (asked M), flags F, bad values B -> <file>`. A Render whose scene is
later than the frame asked for dumps that later scene and says so (`asked_frame` differs).

- [ ] Tests first (`tests/native/test_scene_dump.cpp`), see them fail:

```cpp
#include "../../src/native/render/scene_dump.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
using namespace fable2::native::render;
int main() {
  float c[3];
  DecodeR11G11B10(0, c);
  if (c[0] != 0.0f || c[1] != 0.0f || c[2] != 0.0f) return 1;
  DecodeR11G11B10((15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22), c);   // exponent 15: 1.0 in all three
  if (c[0] != 1.0f || c[1] != 1.0f || c[2] != 1.0f) return 2;
  DecodeR11G11B10((16u << 6) | 32u, c);                                        // 2 * (1 + 32 / 64) = 3.0
  if (c[0] != 3.0f) return 3;
  DecodeR11G11B10(((16u << 5) | 16u) << 22, c);                                // blue: 2 * (1 + 16 / 32) = 3.0
  if (c[2] != 3.0f) return 4;
  DecodeR11G11B10(1u, c);                                                      // a denormal: 2^-14 / 64
  if (c[0] != std::ldexp(1.0f, -20)) return 5;
  if (DepthLevel24(0.0f) != 0 || DepthLevel24(1.0f) != 16777215u || DepthLevel24(2.0f) != 16777215u) return 6;
  if (DepthLevel24(-1.0f) != 0 || DepthLevel24(std::numeric_limits<float>::quiet_NaN()) != 0) return 7;
  for (uint32_t level : {0u, 1u, 255u, 256u, 65535u, 8388608u, 16777215u}) {
    if (DepthLevel24(DepthFromLevel24(level)) != level) return 8;
  }
  SceneDumpHeader h{kSceneDumpMagic, kSceneDumpVersion, 4, 2, 77, 77, kSceneDumpPost, 8.4f, 0, 0};
  std::vector<uint8_t> file(sizeof(h) + 4 * 2 * 12 + 4 * 2 * 4);
  std::memcpy(file.data(), &h, sizeof(h));
  SceneDumpHeader got{};
  if (!ParseSceneDump(file.data(), file.size(), &got) || got.scene_frame != 77) return 9;
  if (ParseSceneDump(file.data(), file.size() - 1, &got)) return 10;            // a short file
  h.flags |= kSceneDumpLayer;                                                    // the layer's planes are missing
  std::memcpy(file.data(), &h, sizeof(h));
  if (ParseSceneDump(file.data(), file.size(), &got)) return 11;
  h.flags = 0; h.width = 3;                                                      // an odd width with no layer: sizes still whole
  std::memcpy(file.data(), &h, sizeof(h));
  if (ParseSceneDump(file.data(), file.size(), &got)) return 12;                 // (the size no longer fits)
  return 0;
}
```

- [ ] Shader test: `kClayDepthPackPs` compiles; its compiled text holds the constant 16777215 and
      no `Sample` (a `Load` alone); with the switch off no program of the pass names it (the
      pattern of cases 246, 252, 254).
- [ ] Implement. The copies are taken after the scene's last draw and before the first post
      step; the layer's two planes at the two points Task 16 will draw between (until then both
      hold the same depth). Suites; build.
- [ ] NEEDS_RUN: WARP with POST, SUN, DUMP and one added plan token `60:DISC=2/1` (the dump
      path's first execution under the debug layer); then SUNWARD with POST, SUN, DUMP, READBACK.
      From them: the log line; `ParseSceneDump` through `foliage_trace.py head` (Task 17 owns the
      tool; here a ten-line script in the report suffices); the dump's frame equal to the
      capture's first `"scene_frame"`.
- [ ] Frame map; commit "Stored native scene: a diagnostic dump of the scene before post".

### Task 3: The rasterising replay tool (no game; parallel with Tasks 1 and 2)

**Files:** `tools/xdk_sigmatch/capture_replay.py` (new: the research's `cap.py` and `replay.py`
as one tracked module), `tools/xdk_sigmatch/foliage_replay.py` (new),
`tests/test_foliage_replay.py` (new), frame map "The rasterising replay".

**Consumes:** `ucode_eval.Machine`, `ucode_eval.load(dump_dir, hash_, kind)`;
`position_check.positions_for(row, entry, capture_dir)`, `triangles(prim, idx)`;
`post_replay.py`'s loader of a frame's depth copy and oracle scene; Task 1's `"ib"` file members
(a row without them is replayed from its 512 indices and counted `partial`).

**Produces** (`foliage_replay.py`; arrays are numpy):
- `window(clip, size=(1120, 720))` -> x, y in pixels from the corner, `z / w`, `w`.
- `raster(draws, size=(1120, 720), origin=0.0)` -> `(depth, ident)`: `draws` a list of
  `(clip[n, 4], tris[m, 3], ident)`; a pixel `(i, j)` is sampled at `(i + origin, j + origin)`
  (0.0: the game's integral position; 0.5: D3D's centre), top-left rule, the nearest `z / w`
  kept, `ident` 0 where nothing is drawn; triangles with a vertex of `w <= 0` are clipped against
  `w = 1e-6` first.
- `row_clip(row, capture_dir, wind=True)` -> the row's `oPos` from the game's microcode on its
  streams and bank (`wind=True`), or from its table entry at rest (`wind=False`).
- `alpha_cover(row, uv, texture, parity, samples)` -> the first sample's and the second's cover
  for a family A row (base level, bilinear; `0x2202`'s offsets; pixel `c15.w` x `c26.w`).
- Subcommands: `selftest`; `depth-relation <capture> <frame>` (criterion 5's first part: the
  tolerance it prints is the 99th percentile of `|stored - replayed|` over the unmoving draws'
  nearest pixels, in the stored depth's own units, with the relation tried in both forms
  `1 - z / w` and `z / w`); `wind <capture> <frame>` (criterion 5's second part); `pixels
  <capture> <frame>` (per fading and late draw: pixels with 1 and with 2 samples covered, and per
  region of `post-regions.json`); `mask <capture> <frame> --out <npy>` (the wind draws' cover);
  `motion <mask> <shot a> <shot b> ...` (criterion 6's `D` inside and outside the mask, HUD and
  text box cut off as `shot_motion.py` does); `occluder <capture> <frame>` (Task 16).

- [ ] Tests first (`tests/test_foliage_replay.py`), see them fail:

```python
import sys
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import foliage_replay as fr  # noqa: E402

TRI = np.array([[0, 1, 2]])


def half(z=0.5):
    """The upper left half of the picture: window corners (0, 0), (W, 0), (0, H)."""
    return np.array([[-1.0, 1.0, z, 1.0], [1.0, 1.0, z, 1.0], [-1.0, -1.0, z, 1.0]])


class Raster(unittest.TestCase):
    def test_cover_at_the_integral_position_and_at_the_centre(self):
        depth, ident = fr.raster([(half(), TRI, 7)], size=(8, 8), origin=0.0)
        self.assertEqual(int((ident == 7).sum()), 36)        # i + j < 8: the slanted edge is not top or left
        self.assertTrue(ident[0, 0] == 7 and ident[7, 0] == 7 and ident[0, 7] == 7 and ident[4, 4] == 0)
        depth, ident = fr.raster([(half(), TRI, 7)], size=(8, 8), origin=0.5)
        self.assertEqual(int((ident == 7).sum()), 28)        # i + j < 7

    def test_two_halves_share_no_pixel_and_miss_none(self):
        other = np.array([[1.0, 1.0, 0.5, 1.0], [1.0, -1.0, 0.5, 1.0], [-1.0, -1.0, 0.5, 1.0]])
        for origin in (0.0, 0.5):
            a = fr.raster([(half(), TRI, 1)], size=(8, 8), origin=origin)[1]
            b = fr.raster([(other, TRI, 2)], size=(8, 8), origin=origin)[1]
            self.assertEqual(int(((a == 1) & (b == 2)).sum()), 0)
            self.assertEqual(int(((a == 1) | (b == 2)).sum()), 64)   # the shared edge belongs to exactly one

    def test_the_nearer_draw_wins_and_depth_is_z_over_w(self):
        depth, ident = fr.raster([(half(0.5), TRI, 1), (half(0.25) * 2.0, TRI, 2)], size=(8, 8))
        self.assertTrue((ident[ident != 0] == 2).all())      # 0.5 / 2 = 0.25 is nearer than 0.5
        self.assertTrue(np.allclose(depth[ident == 2], 0.25))

    def test_winding_does_not_matter_and_a_vertex_behind_the_eye_is_clipped(self):
        back = half()[::-1].copy()
        self.assertEqual(int((fr.raster([(back, TRI, 3)], size=(8, 8))[1] == 3).sum()), 36)
        behind = half()
        behind[2, 3] = -1.0
        depth, ident = fr.raster([(behind, TRI, 4)], size=(8, 8))
        self.assertTrue((ident == 4).any() and np.isfinite(depth[ident == 4]).all())

    def test_no_number_draws_nothing(self):
        bad = half()
        bad[1, 0] = np.nan
        self.assertEqual(int((fr.raster([(bad, TRI, 5)], size=(8, 8))[1] != 0).sum()), 0)


class Motion(unittest.TestCase):
    def test_share_inside_and_outside_a_mask(self):
        a = np.zeros((4, 4, 3), dtype=np.uint8)
        b = a.copy()
        b[0, :2] = 9      # two pixels change by 9 levels, inside
        b[3, 0] = 8       # one by exactly 8: not "more than 8"
        mask = np.zeros((4, 4), dtype=bool)
        mask[0] = True
        inside, outside = fr.motion_share(a, b, mask, levels=8)
        self.assertEqual((inside, outside), (0.5, 0.0))
```

      (The counts: the first triangle's left and top edges lie on the picture's border and are
      its own; the slanted edge is the second triangle's left edge. At `origin` 0.0 a pixel
      `(i, j)` is the first triangle's iff `i + j < 8`, 36 pixels; at 0.5 iff `i + j < 7`, 28.)
- [ ] Implement `capture_replay.py` (`rows(path, kinds)`, `class Draw`, `element`, `pixels`: the
      research's functions, with the capture's path an argument) and `foliage_replay.py`. The
      subcommands that need a capture run on `120435` frame 1 with its 512 indices a draw and
      print `partial` for every such draw: that output is the task's evidence that the tool reads
      real rows, not a result.
- [ ] Frame map; commit "Foliage replay: a rasterising replay of capture rows".

### Task 4: Discovery runs and their analysis

**Files:** `docs/native-renderer/foliage-regions.json` (new: boxes on the bridge's trees and
grass, on the sunward crowns of R B7's table, and one "still" box of rock or wall at each spot,
drawn on this task's emulated shots), `tools/xdk_sigmatch/foliage_replay.py` and its tests only
for what a run shows to be wrong, frame map "Discovery runs".

- [ ] NEEDS_RUN (Task 1's and Task 2's build; all `RUN UNCHECKED`): SUNWARD with POST, READBACK;
      BRIDGE with POST, READBACK; THIRD with POST, READBACK; STAND with POST; CLOCK with POST.
      Copies wanted: every capture's `.jsonl` with its `native_geo_` and `native_tex_` folders'
      names, the logs, the shots, into `out/native-evidence/foliage/t4_*`.
- [ ] Open question 1: from CLOCK's captures, `c10.x` of the first frame of each against the
      log's times of the two `[native-discovery] armed` lines: units a second, and the same for a
      tree's `c15.x` and `c17.w / 2`. Open question 2: `c17.xy`, `c21`, `c26`, `c16`, `c22`,
      `c25` over all captures of this task: their ranges against the spec's Design 5 bounds. **A
      value outside a bound is reported to the controller before Task 8 starts.**
- [ ] Open question 4, criterion 5's first part: `foliage_replay.py depth-relation` on SUNWARD's
      and BRIDGE's frame 1: which relation holds, the tolerance, the share. Below 95%: say which
      draws disagree (by vertex shader) and stop the criterion at "not decidable".
- [ ] Open question 3: `foliage_replay.py pixels` on the three spots' frame 1: the table R B6
      could not give (pixels at one and at two samples for each fading and late draw; by region
      towards the sun).
- [ ] Open question 5: `foliage_replay.py mask` from STAND's capture, then `motion` on its
      emulated pairs: `D` inside and outside the mask and in the "still" box at 3 s and 6 s. This
      is the baseline of criterion 6; if the precondition fails (outside above 3%), say so now.
- [ ] The casters' own rows (Task 1 gave them streams): the fetch lists of `FBD3`, `4325`,
      `A8A5` against their scene shaders' (R A1's table): the same elements at the same offsets,
      or the differences listed for Task 5.
- [ ] Frame map "Discovery runs" with every figure's file; commit "Foliage discovery runs: the
      clock, the gusts, pixel counts, the standing camera, the replay against the depth copy".

### Task 5: Wind formulas, the plants and the harness (no game; parallel with Tasks 1 to 4)

**Files:** `tools/xdk_sigmatch/light_formulas.py`, `tools/xdk_sigmatch/wind_check.py` (new),
`tools/xdk_sigmatch/vs_check.py` (`CASTER_UV_ONLY`), `tests/test_wind_formulas.py` (new),
`docs/native-renderer/lighting-formulas.md` (part "Wind").

**Consumes:** `ucode_eval.Machine(text, consts, sampler, regs=, bools=, vfetch=, loops=)`;
`light_check.run_vs`; `capture_replay.Draw` (Task 3; until it lands, the research's `replay.py`
by path); the research's `plant_formula_check.py`, `wind_used.py`, `wind_elements.py`.

**Produces** (in `light_formulas.py`; positions and vectors 3-tuples of floats):

```python
WIND_SHADERS = {            # the twelve; a shader not machine-checked is not in it
    "8123C16DBF583F92": {"family": "plant", "role": "scene",
                         "reg": (10, 21, 22, 25, 26),            # vertex registers, in WindInputs' order
                         "lit_reg": (253, 254, 255),             # the shader's literal registers read
                         "lit": {"lo": -math.pi, "two_pi": 2 * math.pi, "inv_two_pi": 1 / (2 * math.pi),
                                 "gain": 0.1, "height": 5.0},     # the family's named literals, from the row's vs_lit
                         "elements": ()},                         # (name, fetch, format, offset_dwords, source, count)
    # ... B636, 48D3, FC4F, 33C0 (scene), A8A5 (caster); Tasks 6 and 7 add the trees
}
def wind_plant(p, z, a3, b3, c7xy, c10x, c21, c22, c25x, c26xy, lit):
    """p + sat(lit.height * z) * cos(theta) * (c26x * c21 + c26y * c22); R A1."""
def wind_reach_plant(c21, c22, c26xy):      # an upper bound of |offset|: |c26x c21 + c26y c22|
```

`wind_check.py`: `check_formula(hash_, trials=2000, seed=1, mutation=None, dumps=DUMPS)` ->
`Result(trials, failed, moved, worst, first_failure)` (made inputs: registers inside the spec's
Design 5 bounds, elements over their whole range; `moved` counts the trials whose microcode `oPos`
differs from the rest position's by more than 1e-3; the bound is criterion 1's);
`check_rows(capture_path, hash_=None, per_draw=64)` (real rows); `check_identities(capture_path)`
(criterion 2); `MUTATIONS = ("register", "bytes", "sign", "literal")`; subcommands `formulas`,
`rows`, `identities`, `inputs` (Task 8), each exiting 1 on any failure.

- [ ] Tests first (`tests/test_wind_formulas.py`), see them fail:

```python
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import wind_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
GRASS = "8123C16DBF583F92"
C21, C22, C26 = (0.9024, 0.4099, 0.1329), (-0.35, 1.53, 0.0), (0.025, 0.0637)   # bridge, R A2


class Plant(unittest.TestCase):
    def lit(self):
        return lf.WIND_SHADERS[GRASS]["lit"]

    def test_a_vertex_at_the_root_stands(self):
        p = (10.0, 20.0, 3.0)
        self.assertEqual(lf.wind_plant(p, 0.0, 0.25, 0.5, (0.1, 0.2), 58.0, C21, C22, 1.0, C26, self.lit()), p)
        self.assertEqual(lf.wind_plant(p, -2.0, 0.25, 0.5, (0.1, 0.2), 58.0, C21, C22, 1.0, C26, self.lit()), p)

    def test_the_closed_form_written_out(self):
        p, z, a3, b3, c7, t = (10.0, 20.0, 3.0), 0.1, 0.25, 0.5, (0.1, 0.2), 58.0
        f = (c7[0] + c7[1] + a3 + b3) % 1.0
        theta = 1.0 + f - t * 1.0 * (1.0 + 0.1 * f)
        theta = (theta + math.pi) % (2.0 * math.pi) - math.pi
        s = min(max(5.0 * z, 0.0), 1.0) * math.cos(theta)
        want = tuple(p[k] + s * (C26[0] * C21[k] + C26[1] * C22[k]) for k in range(3))
        got = lf.wind_plant(p, z, a3, b3, c7, t, C21, C22, 1.0, C26, self.lit())
        for g, w in zip(got, want):
            self.assertAlmostEqual(g, w, delta=2e-6)

    def test_the_offset_never_leaves_its_reach(self):
        reach = lf.wind_reach_plant(C21, C22, C26)
        self.assertAlmostEqual(reach, 0.108, delta=0.001)                      # R A2
        for i in range(400):
            got = lf.wind_plant((0.0, 0.0, 0.0), 0.01 * i, 0.37 * i, 0.11 * i, (0.3, 0.9), 57.0 + 0.07 * i,
                                C21, C22, 1.0, C26, self.lit())
            self.assertLessEqual(math.sqrt(sum(v * v for v in got)), reach * (1.0 + 1e-6))

    def test_the_tables_rows(self):
        for hash_, row in lf.WIND_SHADERS.items():
            self.assertEqual(len(hash_), 16)
            self.assertIn(row["family"], ("trunk", "leaf", "plant"))
            self.assertIn(row["role"], ("scene", "caster"))
        self.assertNotIn("36B543CECAE5C781", lf.WIND_SHADERS)                   # push only (ruling 10)
        self.assertNotIn("6AD4108C3FF07966", lf.WIND_SHADERS)                   # moves nothing


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class AgainstMicrocode(unittest.TestCase):
    def test_every_entered_shader_on_made_inputs(self):
        for hash_ in lf.WIND_SHADERS:
            with self.subTest(hash_):
                r = wind_check.check_formula(hash_, trials=200, seed=11, dumps=DUMPS)
                self.assertEqual(r.failed, 0, r.first_failure)
                self.assertGreaterEqual(r.moved, r.trials // 2)    # a check on vertices that stand proves nothing

    def test_mutations_fail(self):
        families = {row["family"]: hash_ for hash_, row in lf.WIND_SHADERS.items() if row["role"] == "scene"}
        for hash_ in families.values():
            for name in wind_check.MUTATIONS:
                with self.subTest(hash_=hash_, mutation=name):
                    r = wind_check.check_formula(hash_, trials=200, seed=11, mutation=name, dumps=DUMPS)
                    self.assertGreater(r.failed, 0, f"{name} was not caught")
```

- [ ] The plants: read the block of each of the five scene shaders and of `A8A5`
      (`out/shader_dump/shader_<HASH>.ucode.vert`), write `wind_plant` once and each shader's
      `"lit"` from its row's `"vs_lit"` (their registers differ by shader: FM 1050 to 1057); the
      four near shaders are checked with their push loop's count at 0 (ruling 10: the check's
      text says so). `h` and `f` term by term (R A1 left them unchecked).
- [ ] `wind_check.py formulas` 2,000 of 2,000 for the six plant shaders; `rows` on `120435`,
      `120714`, `121036`; `identities` for `A8A5` on `8123` and on `B636`. `vs_check.py`: the
      casters that have a wind entry leave `CASTER_UV_ONLY` as their family is entered.
- [ ] Document; commit "Wind formulas: the instanced plants, the checker, its mutations".

### Task 6: Wind formulas, the trunks

**Files:** `light_formulas.py`, `wind_check.py`, `tests/test_wind_formulas.py`,
`lighting-formulas.md`.

**Produces:** `wind_trunk(p, w4, c15, c16, c17, lit)` (`p` the fetched position read as `yxw1`,
`w4` the four values of fetch 4 in the fetch's source order, `lit` the named literals of
`475E`'s `c251..c255`) -> the position `475E`'s instr 65 makes; `wind_reach_tree(w4_max, c15, c16,
c17, lit)` (an upper bound of the offset's length, each sine taken as 1); the rows `475E` (scene)
and `FBD3` (caster) of `WIND_SHADERS`.

- [ ] Tests first, appended to `tests/test_wind_formulas.py`: with `w4 = (0, 0, 0, 0)` the
      position is `p` exactly (`W.x` scales the whole: R A1); for 400 made inputs
      `|wind_trunk(...) - p| <= wind_reach_tree(...) * (1 + 1e-6)`; the horizontal offset and the
      drop of `z` have the relation instr 56, 57 give (written out in the test from the reading,
      as `test_the_closed_form_written_out` does for the plants).
- [ ] Instr 17 to 58 of `475E` transcribed register by register into Python first (the
      interpreter is the oracle at each step: `wind_check.py` gains `trace <hash> --instr N`,
      which prints the interpreter's register after instruction N for a made input), then folded
      into the closed form. 2,000 of 2,000 on made inputs; every `475E` and `FBD3` row of the
      three captures (and `FBD3` on its own streams from Task 4's captures); the identity `FBD3`
      on `475E` exact.
- [ ] If the form does not close: `475E` and `FBD3` get no rows, the frame map says which
      instructions disagree, and **the leaves are not entered either** (spec risk 4: a swaying
      crown on a still trunk is worse than a still tree); Task 7 then documents and stops.
- [ ] Commit "Wind formulas: the trunks".

### Task 7: Wind formulas, the leaves; pins and the position check

**Files:** `light_formulas.py`, `wind_check.py`, `tools/xdk_sigmatch/wind_pins.py` (new, as
`post_pins.py`), `tools/xdk_sigmatch/position_check.py`, `tests/test_wind_formulas.py`,
`tests/test_position_check.py`, `docs/native-renderer/vs-transforms.json` (the `"wind"` member),
`tools/xdk_sigmatch/gen_transform_table.py`, `tests/test_gen_transform_table.py`,
`lighting-formulas.md`.

**Produces:** `wind_leaf(p, q, w4, v2, c15, c16, c17, lit)` (`q` the pivot, `v2` the two values of
fetch 5) -> the position `A584`'s instr 98 makes; the rows `A584`, `D7DD`, `2D40` (scene) and
`4325` (caster); `wind_pins.lines()` (sections `// wind plant pins`, `// wind trunk pins`,
`// wind leaf pins`, `// wind reach pins`: real rows' inputs and the formula's outputs as hex
float bits); in `vs-transforms.json` each entered shader's `"wind": {"family", "reg", "lit_reg",
"lit", "elements"}`; `gen_transform_table.py` writes
`src/native/capture/wind_table.inc` from it (Task 8 defines the row's struct; this task writes the
generator and its test against a made JSON); `position_check.py` replays a wind entry's position
with its formula and prints `wind: p50 / p99 / max pixel` against the microcode's `oPos`.

**As built (ledger rulings 30 to 33; Tasks 8 to 10 consume this, not the paragraph above):**
`wind_leaf(p, q, w4, v3, c15, c16, c17, lit)` with `v3` the THREE values of fetch 5 (source x, y,
z); `wind_reach_leaf(w4_max, length_max, c15, c16, c17, lit)`; the `"wind"` member also has
`"role"`, `"lit_at"` (name to `"c253.x"`) and `"evidence"`; `wind_table.inc` is committed (X-macro
lines `FABLE2_WIND`, `FABLE2_WIND_LITERAL`); the leaf pins' lines carry three flutter values, and
the last made leaf line is the vertex at its pivot with the lift exactly 0 (ledger ruling 36);
`position_check.py --wind` judges the vertices in view (spec criterion 4, "As judged").

- [ ] Tests first: with the pivot at the vertex (`q == p`) and with `w4` and `v2` zero the
      position is the pivot's own (the direction has no length to turn); `|position - q'|` equals
      `|p - q|` within 1e-5 for 400 made inputs (instr 98: the length is kept); the reach bound;
      `2D40` and `D7DD` on made inputs; the generator on a made entry (every field of the row,
      and a `"wind"` member with an unknown family is refused).
- [ ] Transcribe instr 22 to 98 of `A584` as Task 6 does; 2,000 of 2,000; every `A584`, `D7DD`
      and `4325` row of the three captures; the identities `4325` on `A584` and the occluder on
      its twin, exact. `2D40`: made inputs alone, said in its row's evidence string.
- [ ] Criterion 4: `position_check.py` on the three captures; criterion 1 and 2 as one run of
      `wind_check.py formulas`, `rows`, `identities` with its output in
      `out/native-evidence/foliage/t7_wind_check.txt`.
- [ ] Commit "Wind formulas: the leaves; pins, the table's generator, positions through the
      table".

### Task 8: Wind inputs (capture)

**Files:** `src/native/capture/wind_inputs.h` (new, pure), `src/native/capture/wind_table.inc`
(generated), `src/native/capture/draw_record.h`, `src/native/capture/capture.cpp` / `capture.h`,
`src/native/render/frame_scene.h`, `src/native/fable2_native_render.cpp` (the switch
`fable2_native_wind`, off; `SetWindWanted` in `PollFrame`'s order of stores),
`tests/native/test_wind_inputs.cpp` (new; two lines in the runner),
`tests/native/test_draw_record.cpp`, `tests/native/test_frame_scene.cpp`,
`tools/xdk_sigmatch/wind_check.py` (`inputs`), README (the switch, the counters), frame map "Wind
inputs".

**Consumes:** `RegisterView`, `LightStream`, `LightElement`, `LightFetchSpec`, `LightFetch`,
`MakeLightElement`, `LightElementBytes` (`material.h` lines 440 to 640); `ReadConstantRegisters`
(`capture.cpp`; as built, into a bank of the wind's own; as first written `ReadBankRegisters`); `ReadLiteralRegisters` and `LiteralsJson` (the literal table's reader);
`ResolveLightStream`; `FillLightInputs` (3282) as the pattern; `FrameBuilder::Add` (line 494),
`AddShadow` (580); Task 7's `wind_table.inc`.

**Interfaces (produce exactly; amended after Tasks 5 to 7 to ledger rulings 26, 28, 30, 31 and
33, each amended line marked; the names of the amended lines are those of `wind_inputs.h` as
Task 8 committed it, commit 4f2f4de):**

```cpp
// wind_inputs.h, namespace fable2::native::capture
enum class WindFamily : uint8_t { kNone = 0, kTrunk = 1, kLeaf = 2, kPlant = 3, kCount };
enum class WindReason : uint8_t { kNone, kOff, kNoEntry, kRegisterUnread, kRegisterNotFinite, kRegisterRange,
                                  kLiteral, kElement, kStream, kInstance, kOverLimit, kCount };
const char* WindReasonName(WindReason r);   // "none", "off", "no-entry", "register-unread", ... "over-limit"
inline constexpr uint32_t kWindRegisters = 5, kWindLiterals = 28, kWindElements = 3;   // ruling 31: literal SLOTS; was 5 registers
constexpr uint32_t WindLiteralCount(WindFamily f);   // ruling 31: trunk 18, leaf 28, plant 7 (light_formulas.WIND_LITERAL_ORDER), else 0
inline constexpr uint32_t kWindLiteralFirst = 248, kWindLiteralRegisters = 8;   // ruling 31: the registers a table may name a literal at
inline constexpr uint8_t kWindNoRegister = 0xFF;
enum : uint32_t { kWindPivot = 0, kWindWeights = 1, kWindFlutter = 2 };   // WindSpec::element, WindInputs::element
struct WindSpec {                       // one row of wind_table.inc
  uint64_t vs_hash;
  WindFamily family;
  bool caster;
  uint8_t reg[kWindRegisters];          // trees: 15, 16, 17, none, none; plants: 10, 21, 22, 25, 26
  // Ruling 31: one entry a literal SLOT (its place in the family's order), not a register: the register and
  // the component this shader's own table holds it at, and its bits. A slot past lit_count: kWindNoRegister, 0, 0.
  // (Was: lit_reg[5], float lit[5][4].)
  uint8_t lit_count;                    // WindLiteralCount(family)
  uint8_t lit_reg[kWindLiterals];
  uint8_t lit_comp[kWindLiterals];
  uint32_t lit_bits[kWindLiterals];
  LightFetchSpec element[kWindElements];   // fetch -1: the family has none there
  uint8_t source_count[kWindElements];  // 3 (the pivot), 4 (the weights), 3 (the flutter: ruling 30; was 2); 0: none
};
// Ruling 33: the table is read through the X-macro lines of wind_table.inc (FABLE2_WIND, FABLE2_WIND_LITERAL),
// whose forms change only through gen_transform_table.py; a static assert holds the table well formed (every
// slot of the family named exactly once, by a register of the window and a component).
// Ruling 31: the shader object's own table against the row, each literal alone: `bits[r][c]` is component c of
// register kWindLiteralFirst + r, bit r * 4 + c of `have` says the object's table holds that dword.
bool WindLiteralsMatch(const WindSpec& spec, const uint32_t (*bits)[4], uint32_t have);
const WindSpec* FindWind(uint64_t vs_hash);
struct WindInputs {
  WindFamily family = WindFamily::kNone;
  uint8_t spec = 0xFF;                  // the row's index in the table
  uint16_t pad = 0;
  float reg[kWindRegisters][4] = {};
  LightElement element[kWindElements];
};
static_assert(sizeof(WindInputs) == 132, "WindInputs grew or shrank: one entry a wind draw is copied on the guest thread");
struct WindDraw {
  const WindSpec* spec = nullptr;
  bool literals_match = false;          // WindLiteralsMatch on the shader object's own literal table (ruling 31)
  RegisterView vs_regs;
  const std::vector<VertexFetch>* fetches = nullptr;
  LightStream stream[kWindElements];
  int64_t max_vertex = -1;              // DrawShaders::max_vertex
  bool instanced = false;               // DrawInputs::instances.active
};
// The spec's Design 5 bounds as amended: c25.x in [0, 64] (ruling 26: zero inside), |c17.xy| <= 16 (ruling 28).
// They are a table of lines that a Python test holds against wind_check.REGISTER_BOUNDS (review of Tasks 5 to 7).
bool WindRegisterInBounds(WindFamily family, uint32_t slot, const float v[4]);
WindReason MakeWindInputs(const WindDraw& d, WindInputs* out);

// draw_record.h: after `skip`, in the tail padding
  uint16_t wind_index = kNoSideIndex;
static_assert(sizeof(DrawRecord) == 616 && offsetof(DrawRecord, wind_index) == 614);
```

`MakeWindInputs`'s rules, in this order (the first that fails names the reason; `*out` is then
default): a spec (`kNoEntry`); `literals_match` (`kLiteral`); every register of `spec->reg` that
is not `kWindNoRegister` read (`kRegisterUnread`), finite (`kRegisterNotFinite`) and in bounds
(`kRegisterRange`); a plant: `instanced` (`kInstance`) and no element; a tree: each element the
table names found by `LightFetch`, in a format `LightElementBytes` knows, inside its vertex
(`kElement`), its stream resolved and holding `max_vertex` (`max_vertex >= 0` and
`(max_vertex + 1) * stride <= vb.size`: `kStream`), and not `instanced` (`kInstance`).
A plant with `c25.x` = 0 and `c26` = 0 has inputs: its formula moves nothing (ledger ruling 26).
`WindInputs` does not carry the literals and stays 132 bytes: they are the row's, which the
capture has held against the shader object's own table (ledger ruling 31).

`FrameScene` gains `std::vector<capture::WindInputs> wind`, `shadow_wind[2]`, `bool wind_records`,
`uint32_t wind_family[size_t(capture::WindFamily::kCount)]`,
`wind_reason[size_t(capture::WindReason::kCount)]`, `shadow_wind_reason[...]`, and
`WindOf(scene, r)`, `ShadowWindOf(scene, cascade, r)` (null for `kNoSideIndex` or an index past
the array). `FrameBuilder` gains `WindOn(bool)`; `Add` and `AddShadow` each take
`const capture::WindInputs* wind = nullptr, capture::WindReason wind_reason =
capture::WindReason::kOff` as last parameters: with the wind on, a drawable record's valid inputs
are appended and the record gets the index; an array at 65,535 entries gives `kOverLimit`; every
other record gets `kNoSideIndex` whatever index it came with.

In `capture.cpp`: `VsInfo` gains `const WindSpec* wind` and `WindLiteralVerdict wind_literals` (as
built: three states, read at the object's first wind draw and asked again once a wind frame, never
in `FillShader`, which runs with the switch off; as first written `bool wind_literals`, set once in
`FillShader` / at the first lookup of the object: the object's literal table read over
`c248..c255` and held against the row literal by literal, at the row's register and component:
ledger ruling 31); `g_wind_wanted`, `g_wind_frame` latched with the records at the frame boundary
(as the fog's); `FillWindInputs(uint32_t device, const DeviceSnapshot& dev, const VsInfo* vs, bool
instanced, int64_t max_vertex, WindInputs* out)` called in `RecordDraw` beside `FillLightInputs`
(for a draw that is not skipped and whose shader has a row) and in `RecordShadowDraw` (as built; as
first written `ObserveShadowDraw`) for a caster record; the discovery row's `"wind"` member (the inputs as the record gets them, or the
reason), the frame row's `"wind":{"records", "by family", "by reason"}` (`"off"` with the switch
off; as built the keys are those the frame map's "Wind inputs" lists, and since Task 10 the stored
entries), and the periodic line `[native] capture: frame N wind: trunk a, leaf b, plant c, casters d |
at rest {reason: n} | by vs {hash: n}`.

- [ ] Tests first. `tests/native/test_wind_inputs.cpp` (see it fail):

```cpp
#include "../../src/native/capture/wind_inputs.h"
#include <cmath>
#include <limits>
#include <vector>
using namespace fable2::native::capture;

// A draw that passes every rule for `spec`, built from the row itself: the registers at the middle
// of their bounds, a stream of 100 vertices behind each element.
struct Made {
  float bank[256 * 4] = {};
  std::vector<VertexFetch> fetches;
  WindDraw d;
};
static void Fill(Made& m, const WindSpec& spec) {
  m.d = WindDraw{};
  m.d.spec = &spec;
  m.d.literals_match = true;
  m.d.vs_regs.bank = m.bank;
  for (uint32_t i = 0; i < kWindRegisters; ++i) {
    if (spec.reg[i] == kWindNoRegister) continue;
    for (int c = 0; c < 4; ++c) m.bank[spec.reg[i] * 4 + c] = 0.5f;
    m.d.vs_regs.Mark(spec.reg[i], 1);
  }
  m.fetches.assign(8, VertexFetch{});
  for (uint32_t e = 0; e < kWindElements; ++e) {
    if (spec.element[e].fetch < 0) continue;
    VertexFetch& f = m.fetches[size_t(spec.element[e].fetch)];
    f.format = spec.element[e].format;
    f.offset_dwords = int32_t(spec.element[e].offset_dwords);
    f.stride_dwords = 9;
    m.d.stream[e].resolved = true;
    m.d.stream[e].vb = {0x1000000u + 0x10000u * e, 100u * 36u};
    m.d.stream[e].endian = 2;
  }
  m.d.fetches = &m.fetches;
  m.d.max_vertex = 99;
  m.d.instanced = spec.family == WindFamily::kPlant;
}

int main() {
  const WindSpec* trunk = FindWind(0x475EC9F795E5EDBBull);
  const WindSpec* leaf = FindWind(0xA5846836C90E1192ull);
  const WindSpec* grass = FindWind(0x8123C16DBF583F92ull);
  if (!trunk || !leaf || !grass) return 1;
  if (trunk->family != WindFamily::kTrunk || leaf->family != WindFamily::kLeaf || grass->family != WindFamily::kPlant) return 2;
  if (FindWind(0x36B543CECAE5C781ull) || FindWind(0x6AD4108C3FF07966ull) || FindWind(0)) return 3;   // push only; still; none
  const WindSpec* caster = FindWind(0x432563420047C96Cull);
  if (!caster || !caster->caster || caster->family != WindFamily::kLeaf) return 4;
  const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
  for (const WindSpec* spec : {trunk, leaf, grass}) {
    Made m;
    WindInputs out;
    Fill(m, *spec);
    if (MakeWindInputs(m.d, &out) != WindReason::kNone || out.family != spec->family) return 10;
    if (out.reg[0][0] != 0.5f || out.spec == 0xFF) return 21;
    if (out.element[kWindWeights].stride_bytes != (spec->family == WindFamily::kPlant ? 0 : 36)) return 22;
    // every rule's rejection leaves default inputs
    auto refused = [&](WindReason want) {
      WindInputs o;
      o.family = WindFamily::kPlant;
      return MakeWindInputs(m.d, &o) == want && o.family == WindFamily::kNone && o.spec == 0xFF;
    };
    Fill(m, *spec); m.d.spec = nullptr;                              if (!refused(WindReason::kNoEntry)) return 11;
    Fill(m, *spec); m.d.literals_match = false;                      if (!refused(WindReason::kLiteral)) return 12;
    Fill(m, *spec); m.d.vs_regs = RegisterView{}; m.d.vs_regs.bank = m.bank;
                                                                     if (!refused(WindReason::kRegisterUnread)) return 13;
    for (float bad : {kNaN, kInf, -kInf}) {
      Fill(m, *spec); m.bank[spec->reg[1] * 4 + 1] = bad;            if (!refused(WindReason::kRegisterNotFinite)) return 14;
    }
    Fill(m, *spec); m.bank[spec->reg[0] * 4] = 2e7f;                 if (!refused(WindReason::kRegisterRange)) return 15;   // the clock past its bound
    Fill(m, *spec); m.d.instanced = !m.d.instanced;                  if (!refused(WindReason::kInstance)) return 16;
    if (spec->family != WindFamily::kPlant) {
      Fill(m, *spec); m.fetches[size_t(spec->element[kWindWeights].fetch)].offset_dwords += 1;
                                                                     if (!refused(WindReason::kElement)) return 17;
      Fill(m, *spec); m.d.stream[kWindWeights].resolved = false;     if (!refused(WindReason::kStream)) return 18;
      Fill(m, *spec); m.d.max_vertex = 100;                          if (!refused(WindReason::kStream)) return 19;   // one past the stream
      Fill(m, *spec); m.d.max_vertex = -1;                           if (!refused(WindReason::kStream)) return 20;
    }
  }
  // the bounds themselves (spec Design 5): the captured values inside, four times them outside or at the edge
  const float c15[4] = {63.1964f, 0.63f, 0.72f, 0.32f}, c17[4] = {0.4816f, -0.4636f, 2.0f, 126.637f};
  if (!WindRegisterInBounds(WindFamily::kTrunk, 0, c15) || !WindRegisterInBounds(WindFamily::kLeaf, 2, c17)) return 30;
  const float first_frame[4] = {12.5f, 0.0f, 2.0f, 126.0f};       // ledger ruling 28: the world's first frame; inside
  if (!WindRegisterInBounds(WindFamily::kTrunk, 2, first_frame)) return 31;
  const float gust[4] = {16.5f, 0.0f, 2.0f, 126.0f};              // ledger ruling 28: the bound is 16 (was 8, with 8.5 here)
  if (WindRegisterInBounds(WindFamily::kTrunk, 2, gust)) return 31;
  const float c25_zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  if (!WindRegisterInBounds(WindFamily::kPlant, 3, c25_zero)) return 32;   // ledger ruling 26: c25.x in [0, 64], zero inside (was outside)
  return 0;
}
```

      `tests/native/test_draw_record.cpp`: a
      record `AssembleRecord` makes has `wind_index == kNoSideIndex` (return 150).
      `tests/native/test_frame_scene.cpp` (returns 151 to 156; as built 151 to 159, and 160 since Task 10): `Add` with valid inputs and the
      wind on gives index 0 then 1 and `scene.wind.size() == 2`; with the wind off, or inputs of
      family none, or a skipped record: `kNoSideIndex` and no entry; a record that comes with a
      stale `wind_index` of 7 and no inputs leaves with `kNoSideIndex`; `AddShadow`'s index names
      `shadow_wind[cascade]`, not `wind`; the 65,536th entry is refused and counted
      `over-limit`; `WindOf` is null for an index at or past the array.
- [ ] Implement; the generated table committed with its generator's inputs. Suites; build.
- [ ] NEEDS_RUN: BRIDGE, THIRD and SUNWARD, each with POST and WIND (nothing is drawn with the
      inputs yet: the picture must be unchanged), and BRIDGE with POST alone. From them:
      `wind_check.py inputs` on each capture (criterion 3); the frame row's `"wind":"off"` and
      no `wind:` line in the off-run; the capture's periodic medians on against off, as measured
      (no verdict from a run that is not a timing run).
- [ ] Frame map; commit "Wind inputs: the game's wind registers and elements beside the record".

### Task 9: Wind words in the geometry cache

**Files:** `src/native/render/wind_decode.h` (new, pure), `src/native/render/geometry_cache.h` /
`.cpp`, `src/native/render/geometry_cache_index.h`, `src/native/render/clay_logic.h`
(`ClayStats`, `GrowBounds`), `tests/native/test_wind_decode.cpp` (new; two lines in the runner),
`tests/native/test_geometry_cache_index.cpp`, `tests/native/test_clay_logic.cpp`, frame map "Wind
words".

**Consumes:** `WindInputs`, `WindSpec`; the element decoders of `normal_decode.h` (as `BuildProbe`
reads a `LightElement`); `capture::InstanceIndex`, `capture::DecodePositions`
(`ExpandInstanceBend`, `normal_decode.h` line 738, is the pattern); `PositionBounds`.

**Produces (amended to ledger rulings 30 and 32, each amended line marked; the names of the
amended lines are those of the Task 9 implementer's `wind_decode.h` and `geometry_cache.h` as they
stood uncommitted on 2026-10-10: where the committed files differ, the requirement in the comment
binds, not the name):**

```cpp
// wind_decode.h, namespace fable2::native::render
constexpr uint32_t WindWordsPerVertex(capture::WindFamily f) {   // trunk 4, leaf 10 (ruling 30; was 9), plant 2, else 0
  return f == capture::WindFamily::kTrunk ? 4u : f == capture::WindFamily::kLeaf ? 10u : f == capture::WindFamily::kPlant ? 2u : 0u;
}
// The words a draw of `count` vertices reads; 0 when the product does not fit 32 bits.
constexpr uint32_t WindWordsNeeded(capture::WindFamily f, uint32_t count);
enum class WindBuild : uint8_t { kNone, kElement, kStream, kValue };   // the builder may add a reason of its own (the cache's upload)
// Ruling 32: what a tree mesh's words say about how far the wind can move its vertices, taken while the words
// are built: the largest of the four weights over the vertices built (light_formulas.wind_reach_tree's w4_max)
// and, for a leaf, the largest |P - Q| (wind_reach_leaf's length_max; 0 for a trunk). A plant has none.
struct WindExtent { float weight_max = 0.0f; float length_max = 0.0f; };
// A tree's words, `count` vertices: a trunk (w.x, w.y, w.z, w.w); a leaf (q.x, q.y, q.z, w.x, w.y, w.z, w.w, v.x, v.y, v.z)
// (ruling 30: ten words, the flutter's source x, y, z; was nine), each value as the fetch reads it (the
// element's format, sign, normalisation, endian and source components). Ruling 32: a leaf's builder also
// takes the vertices' decoded positions P (for |P - Q|) and gives the extent.
WindBuild BuildTreeWindWords(const capture::WindInputs& wind, const uint8_t* const streams[capture::kWindElements],
                             const size_t sizes[capture::kWindElements], const capture::Float4* positions, uint32_t count,
                             float* out, WindExtent* extent);
// A plant's words, one pair a flat position: (A3 + B3 of its copy, z of its mesh vertex).
WindBuild ExpandPlantWindWords(const capture::Float4* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                               const capture::InstanceSet& s, uint32_t flat_count, float* out);
// clay_logic.h
PositionBounds GrowBounds(const PositionBounds& box, float reach);   // xyz by reach; invalid stays invalid; a reach that is no number: invalid
// GeometryCache
// Ruling 32: the cache keeps the extent with the buffer and gives it with the buffer (a cached buffer too).
nrhi::Buffer* WindWords(nrhi::Device* dev, const capture::DrawRecord& r, const capture::WindInputs& wind, uint32_t need,
                        ClayStats& st, WindBuild* why, uint32_t* count, WindExtent* extent = nullptr);
```

Requirements behind the amended lines (ledger ruling 32; review of Tasks 5 to 7, item 4): the
extent is over the vertices the words were built for, never over fewer; a length or a weight that
is no number gives no buffer (`kValue`); the extent of a cached buffer is the one it was built
with; Task 10's reach reads it from here and from nowhere else. Measured over whole leaf streams
of captures `182130` and `182313`: `|P - Q|` up to 19.3 and 19.9 units (not the 12.4 of frame-map
section 12), so a test of the extent on a made stream should reach a length of that order.

All or nothing: a value that is not finite, a stream that holds fewer than `count` vertices, a
copy past the instance stream: no buffer. Keys `WindKey` (the elements and the streams' content
hashes; a plant's: the mesh's position stream and layout and the instance set as the flat normals
have it) with their test in `test_geometry_cache_index.cpp` (as built: `WindKey` lies in
`clay_logic.h` and its test is `tests/native/test_clay_logic.cpp`, cases 681 to 685).

- [ ] Tests first (`tests/native/test_wind_decode.cpp`): a made 8_8_8_8 stream of three vertices
      read through a made element gives the twelve values `byte / 255` in the element's source
      order (and the other order under another `source`); a made half4 pivot; `count` one past
      the stream is `kStream`; an element of an unknown format is `kElement`; a half that is a NaN
      is `kValue` and nothing is written past the failing vertex's start; a leaf's ten words a
      vertex with the flutter's three values in source order x, y, z, and its extent (the largest
      weight, the largest `|P - Q|`: ledger rulings 30 and 32); a plant with two copies
      of a three-vertex mesh gives `(A3 + B3, z)` per flat position with each copy's own sum;
      `WindWordsNeeded(kLeaf, 0x20000000u) == 0`. `test_clay_logic.cpp`: `GrowBounds` (the box
      wider by exactly the reach in x, y, z, w unchanged; an invalid box; a NaN reach).
- [ ] Implement; the log's cache line gains the wind words' bytes. Suites; build (no run: nothing
      binds the buffer yet).
- [ ] Commit "Wind words: the per-vertex wind elements in the geometry cache".

### Task 10: The wind program and the pass

**Files:** `src/native/fable2_native_shaders.h`, `src/native/render/clay_logic.h`,
`src/native/render/clay_pass.cpp` / `.h`, `src/native/fable2_native_render.cpp`,
`tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`,
`tests/native/wind_pins.inc` (generated by `wind_pins.py`), `tests/test_wind_literals.py` (new, as
`test_post_literals.py`), README, frame map "Native wind".

**Consumes:** `WindOf`, `ShadowWindOf`; `GeometryCache::WindWords`; `light_formulas.wind_plant`,
`wind_trunk`, `wind_leaf`, the reach functions and `wind_pins.lines()`; `kClayBindings`
(parameter 8 is `t9`), `kClayObjectSlotBytes` (512), `ClayObjectConstants` (288 bytes),
`ClayObjectSlotOffset`, `ClayConstantBufferBytes`, `PipelineKey`, `CreatePipeline`,
`PrepassPipeline`, `EnsureShadowObjects`, `DrawPrepared`, `DrawShadows`, `PrepassBeyondReach`.

**Produces** (`clay_logic.h`, section "Native wind"; amended after Tasks 5 to 7 to ledger
rulings 30 to 32, 35 and 36, each amended line marked. Nothing of this task is built yet: where a
line below names something Task 9 has not committed (`WindExtent`), the requirement in its
comment binds, not the name):

```cpp
inline constexpr uint32_t kLightModeWindShift = 12, kLightModeWindMask = 3u << kLightModeWindShift;   // a WindFamily
struct ClayWindConstants {
  float reg[3][4];   // trees: c15, c16, c17. plants: (c10.x, c25.x, c26.x, c26.y), (c21.xyz, c7.x + c7.y), (c22.xyz, 0)
  // Ruling 31: the family's literals by SLOT, slot k at lit[k / 4][k % 4] (light_formulas.WIND_LITERAL_ORDER: 28
  // for a leaf, 18 for a trunk, 7 for a plant; a slot the family does not have is zero), each the bits of the
  // row's literal of that slot. (Was: float lit[5][4], whole registers.)
  float lit[7][4];
};
// Ruling 31: 3 + 7 rows = 160 bytes (was 128); 288 + 160 = 448 <= 512.
static_assert(sizeof(ClayWindConstants) == 160 && sizeof(ClayObjectConstants) + sizeof(ClayWindConstants) <= kClayObjectSlotBytes);
inline constexpr uint32_t kClayWindOffset = 288;   // == sizeof(ClayObjectConstants): the tail of cbuffer Object
ClayWindConstants MakeClayWindConstants(const capture::WindInputs& wind, const capture::WindSpec& spec, const capture::DrawRecord& r);
enum class ClayWind : uint8_t { kOff, kNone, kBuffer, kPipeline, kSlot, kDrawn, kCount };
struct ClayWindInputs { bool on, has_inputs, terrain, has_words, has_colour_pipeline, has_depth_pipeline, has_slot; };
constexpr ClayWind DecideClayWind(const ClayWindInputs& in);
void WindPlantMirror(const float p[3], float seed, float z, const ClayWindConstants& c, float out[3]);
void WindTrunkMirror(const float p[3], const float w[4], const ClayWindConstants& c, float out[3]);
// Ruling 30: v is the flutter's THREE values (source x, y, z of fetch 5); was v[2].
// Ruling 36: where the moved offset D' has zero length in this function's own (single) precision, out is the
// moved pivot Q'; nothing is divided by that length.
void WindLeafMirror(const float p[3], const float q[3], const float w[4], const float v[3], const ClayWindConstants& c, float out[3]);
// Ruling 32: the reach of a tree needs the mesh's extent, which no register holds: the largest weight
// (wind_reach_tree's w4_max) and, for a leaf, the largest |P - Q| (wind_reach_leaf's length_max), as Task 9's
// cache gives them with the wind words. Written here with Task 9's struct; two float arguments serve as well.
// (Was: WindReach(wind, spec), which cannot bound a leaf.) A plant's reach reads its registers alone.
float WindReach(const capture::WindInputs& wind, const capture::WindSpec& spec, const WindExtent& extent);   // a number >= 0; infinite or NaN inputs: +inf
```

**Arithmetic of the three mirrors and of the three HLSL functions (ledger ruling 35):** single
precision throughout, in the reference's order of operations (`light_formulas.wind_plant`,
`wind_tree_terms` / `wind_tree_offset`, `wind_leaf_lift` / `wind_leaf`, term for term as they are
written there), the HLSL marked `precise`; the clock is not carried in double and no phase is
reduced before it is narrowed. A mirror is held to each pinned position of `wind_pins.py` within
**2e-5** of a unit (the pins are the double reference rounded to single; a single-precision
evaluation is within 7e-6 of them at the captured clocks: `Pins.test_a_single_precision_evaluation_is_near`);
the test says that the bound is ruling 35's. No pin is made at another clock.

**The zero-length rule (ledger ruling 36):** `WindLeafMirror` and the HLSL `WindLeaf` return the
moved pivot `Q'` where `dot(D', D')` computed in their own precision is 0, and divide nowhere
else by a length that can be 0. Held by: the made leaf pin at its pivot with `c15.w` = 0 and
`c16.w` = 0 (the last made line of `// wind leaf pins`: the position is `Q'`, finite); a mutation
that removes the guard from the mirror must fail that pin (the result is then no number); and a
test of the vertex program's guard (the compiled program on that pin where the shader test can
execute it, else the method of case (c) below: the division lies inside a conditional on the
squared length).

**Literal slots (ledger ruling 31; review of Tasks 5 to 7, item 1):** the mirrors and the HLSL
index the constants' literals by slot, and a slot's name is its place in
`light_formulas.WIND_LITERAL_ORDER[family]` (a leaf's first 18 are a trunk's). A hand-copied
index can drift where two literals have one value (`flutter_y` and `one` are both 1.0, which no
pin tells apart): `tests/test_wind_literals.py` holds every index the HLSL and the mirrors use to
that order by name.

`PipelineKey` gains `uint8_t wind = 0` (1: the wind vertex program). `shaders::kClayWindVs` =
`"#define CLAY_WIND 1\n"` + the clay vertex shader's text, in which under `CLAY_WIND` alone: `t9`
is `StructuredBuffer<float> wind_words` and the terrain block is not compiled; `cbuffer Object`
ends in `float4 o_wind_reg[3]; float4 o_wind_lit[7];` (seven rows: ledger ruling 31; was 5; the object macro's tail under the same
`#ifdef`, so that no other program's text changes); the functions `WindPlant`, `WindTrunk`,
`WindLeaf` (each `precise`, each the reference term for term); and after the cut's test on the
position at rest, inside `[branch] if (in_range && family != 0)` with `family = (light_mode >>
12) & 3`, the position that goes through the rows is the moved one, while `o.world` and every
light input stay those at rest (spec ruling 21). The words are read only for a vertex below
`vertex_count` (that is `in_range`), and the pass binds a wind buffer only when it was built for
at least `vertex_count` vertices (Task 9's `need`); otherwise the record is at rest (`buffer`).

In the pass: `wind_vs_` compiled on the first frame with a wind record (never with the switch
off); a wind record's colour pipeline, pre-pass pipeline (the flat form too) and shadow pipeline
are the record's keys with `wind = 1`; `shadow_wind_pipelines_[kShadowPipelines]`. Slots: a wind
record takes a large slot (`ClayObjectSlotOffset(plan.object0, i)`): a lit object, trunk or leaf
the one it has, with the wind block at `kClayWindOffset`; a lit plant its plant constants at the
slot's head and the block behind; an unlit wind record and a wind caster a slot with the block
alone; `ClayConstantBufferBytes` is given the sum. `DecideClayWind` is taken once per record at
prepare and its verdict holds for every pass that draws the record (a scene record: colour and
pre-pass; a caster: its map): anything but `kDrawn` is today's record with today's program and
`light_mode`'s wind bits clear. The box that `PrepassBeyondReach` takes is `GrowBounds(box,
WindReach(...))` for a wind record. `ClayStats` and the F3 text: `Wind: on, moved N (trunk a,
leaf b, plant c), casters M | at rest: {reason: n}` and `Wind: off`.

- [ ] Tests first. `tests/native/test_clay_logic.cpp` (CHECK ids from 720; if taken, the next
      free hundred):

```cpp
  // DecideClayWind: all or none (plan Task 10; Review Focus 1 and 2)
  for (uint32_t bits = 0; bits < 128; ++bits) {
    const ClayWindInputs in{(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0,
                            (bits & 16) != 0, (bits & 32) != 0, (bits & 64) != 0};
    const ClayWind w = DecideClayWind(in);
    const bool all = in.on && in.has_inputs && !in.terrain && in.has_words && in.has_colour_pipeline &&
                     in.has_depth_pipeline && in.has_slot;
    CHECK(720, (w == ClayWind::kDrawn) == all);
    CHECK(721, !in.terrain || w != ClayWind::kDrawn);
    CHECK(722, in.on || w == ClayWind::kOff);
  }
  // The constants of a plant: the registers' packing and the seed's constant part
  {
    capture::WindInputs wi;
    wi.family = capture::WindFamily::kPlant;
    const float regs[5][4] = {{58.0f, 58.0f, 58.0f, 58.0f}, {0.9024f, 0.4099f, 0.1329f, 0.0f}, {-0.35f, 1.53f, 0.0f, 0.0f},
                              {1.0f, 0.0f, 0.0f, 0.0f}, {0.025f, 0.0637f, 0.8687f, 0.0333f}};
    std::memcpy(wi.reg, regs, sizeof(regs));
    capture::DrawRecord r;
    r.instances.offset[0] = 0.1f;
    r.instances.offset[1] = 0.2f;
    capture::WindSpec spec{};
    spec.family = capture::WindFamily::kPlant;
    const ClayWindConstants c = MakeClayWindConstants(wi, spec, r);
    CHECK(723, c.reg[0][0] == 58.0f && c.reg[0][1] == 1.0f && c.reg[0][2] == 0.025f && c.reg[0][3] == 0.0637f);
    CHECK(724, c.reg[1][0] == 0.9024f && c.reg[1][3] == 0.1f + 0.2f && c.reg[2][1] == 1.53f && c.reg[2][3] == 0.0f);
    // a vertex at the root stands, a tip moves, and never past the reach
    float out[3];
    const float p[3] = {10.0f, 20.0f, 3.0f};
    WindPlantMirror(p, 0.75f, 0.0f, c, out);
    CHECK(725, out[0] == p[0] && out[1] == p[1] && out[2] == p[2]);
  }
#include "wind_pins.inc"   // CHECK(730..): each pinned line of wind_pins.py against the three mirrors and WindReach
```

      (Amended: a mirror agrees with a pin within 2e-5 of a unit, ledger ruling 35; a leaf line
      has three flutter values, ruling 30; a reach line gives the largest weight and the largest
      `|P - Q|` that `WindReach` takes, ruling 32, at the lengths 0, 1.48 and 19.91, the median
      and the largest over the captures' whole leaf streams; the leaf lines include the vertex at
      its pivot with no lift, ruling 36. `MakeClayWindConstants` fills `lit` from the row's
      literal bits by slot, ruling 31: the made `spec` above then needs its literals, or the
      table's row of a plant shader is used instead of `capture::WindSpec spec{}`.)

      `tests/native/test_native_shaders.cpp`: (a) with `CLAY_WIND` undefined every clay program
      (the vertex shader, the four colour programs, the three depth-only ones, the shadow
      program) compiles to the bytes it compiled to before this task (the baseline's hashes are
      recorded by the test's first step in the task, from the commit before; the pattern of case
      253); (b) `kClayWindVs` compiles; its reflection has `t9` as a buffer of 4-byte elements,
      `cbuffer Object` of 448 bytes (ledger ruling 31; was 416) with `o_wind_reg` at 288 and
      `o_wind_lit` at 336 over seven rows, and no
      resource the clay layout does not bind for the vertex stage; (c) in its compiled text every
      load from `t9` lies inside a conditional block whose condition reads `light_mode`'s bits 12
      and 13 and a comparison with `vertex_count` (the method of the 128-value light buffer
      test). `tests/test_wind_literals.py`: the HLSL's numbers in the three wind functions equal
      the reference's (none of the table's literals appears as HLSL text: they come from
      `o_wind_lit`); `kClayWindOffset` equals the HLSL offset; and
      `test_one_vertex_program_for_every_pass`: in `clay_pass.cpp` the wind key's pipelines
      (colour, pre-pass, shadow) all take `wind_vs_`, and no pipeline takes `wind_vs_` under a
      key without `wind`.
- [ ] Mutation checks, alone, on scratch copies: `c16` for `c15` in `WindTrunkMirror`; the
      weights' bytes exchanged in `WindLeafMirror`; `cos` for `sin` in one phase; the plant's
      `0.1` gain taken as `1`; the zero-length guard taken out of `WindLeafMirror` (ledger ruling
      36); the flutter's source x left out of `WindLeafMirror` (ledger ruling 30: the made leaf
      line with the hundredfold byte neither 0 nor 255 must catch it): each must fail a pin of
      `wind_pins.inc`.
- [ ] Shader text, the pass, the F3 line. Suites; build.
- [ ] NEEDS_RUN: WARP with POST, WIND; WARP-SUN with POST, SUN, WIND; then STAND with POST,
      WIND and STAND with POST; BRIDGE, THIRD and SUNWARD with POST, WIND, DUMP, READBACK; BRIDGE
      with POST, DUMP, READBACK (the off-run).
- [ ] From the runs: the Wind line's counts; `foliage_replay.py motion` (criterion 6, first
      reading); the holes count of the three dumps by a short script (criterion 7; Task 17's tool
      takes it over); the shots viewed and described honestly (do the crowns, the grass and their
      shadows move; anything torn, detached or flickering); cost as measured.
- [ ] Frame map; commit "Native wind: one vertex program for colour, depth and shadow".

### Task 11: Validation and documents (11a)

*As executed (annotated at the close, 2026-10-11; the list below is the plan as written):* no run was made on
a quiet machine at width 1942 (no timing evidence: ledger rulings 49, 65); the TREES run was not made; the
phase was validated on binary `9EE2090F124FC916`, built before the merge of the frame-time work (ledger
ruling 45). The user checks named 79 to 82 below were appended as **81 to 84** (79 and 80 are the
performance session's).

- [ ] Runs by the controller on a quiet machine, shot width 1942, no agent beside them: BRIDGE
      and THIRD with POST, each with WIND and without (timing: criterion 9); STAND with POST,
      WIND and without (if Task 10's pair was not a like-for-like pair); TREES with POST, WIND.
- [ ] Criterion 5's second part: `foliage_replay.py wind` on Task 4's sunward and bridge
      captures (the microcode's positions against the rest positions, against the depth copy).
- [ ] Criteria 1 to 9 each with its evidence and an exact verdict word; the default of
      `--fable2_native_wind` stays off (spec ruling 22), with the recommendation to the
      controller written beside the evidence.
- [ ] Spec status and "Implementation notes"; frame map "Validation (11a)" and "Limits (11a)";
      `lighting-formulas.md`; `docs/native-renderer/shadow-casters.json` (the two "stands still"
      limitations); `docs/native-renderer/user-checks.md` from check 79: 79 the trees' sway
      (crowns and trunks move together, slowly, as the emulated half's; no crown detached from
      its trunk; no tree jumping at the view switch); 80 grass and bushes (tips sway, roots
      stand; no patch standing while its neighbours move); 81 the shadows of trees and bushes on
      the ground move with them, and no shadow lies beside its caster; 82 the switch off (the
      trees stand, `Wind: off`, the picture of checks 57 to 62); README.
- [ ] Commit "Wind validation (11a): the criteria's verdicts". Then the phase's review.

## Phase 11b: coverage

### Task 12: Coverage rules (pure) and the captures' answers

**Files:** `src/native/render/clay_logic.h` (section "Coverage"), `tests/native/test_clay_logic.cpp`,
`tools/xdk_sigmatch/light_formulas.py` (`covered_samples`), `tests/test_coverage.py` (new),
`lighting-formulas.md`, frame map "Coverage: the rule and the
order".

**Consumes:** `AlphaToMaskCovers` (`clay_logic.h` 1373), `capture::RenderState`
(`alpha_to_mask`, `alpha_to_mask_offsets`, `alpha_to_mask_samples`), `SkyPlace`,
`PlanSunDepthPlace`; Task 4's captures.

**Produces:**

```cpp
// How many of the scene's two samples the game covers at pixel (x, y): 0, 1 or 2 (two samples alone).
constexpr uint32_t CoveredSamples(uint8_t offsets, uint32_t x, uint32_t y, float alpha);
inline constexpr uint64_t kCoveragePs[] = {0x7CD57B81550F19E3ull, 0x00E09D1BC5295D52ull, 0x58D3FAAAA84A48AAull,
                                           0x2FBE9B4EE822609Aull};   // family A: leaf, trunk and branch
enum class CoverageKind : uint8_t { kNone, kEarly, kLate };
// A coverage record: the switch on, a family A pixel shader, a textured material whose alpha is the albedo's, the
// state valid with alpha-to-mask on two samples, no blend, a depth write, and an alpha scale that is a number in
// [0, 1). Late for offsets 0xAA, early for any other.
CoverageKind CoverageOf(bool on, const capture::DrawRecord& r, const capture::RenderState& rs);
// Whether any pixel of a record of this scale can have both samples covered (its alpha is at most `scale`).
constexpr bool CoverageHasFull(uint8_t offsets, float scale);
struct CoverageItem { CoverageKind kind; bool blends; };          // one drawn record, in the frame's order
inline constexpr size_t kNoCoverageTail = ~size_t(0);
struct CoverageOrder { size_t sky; size_t tail; };                // drawn before the record at this index (count: after the last)
// `sky_place`: SkyPlace's index. `depth_place`: the index of the first drawn record at or beyond the frame's
// depth place, `count` when the layer's depth is taken after the last record or not at all.
constexpr CoverageOrder PlanCoverageOrder(const CoverageItem* items, size_t count, size_t sky_place, size_t depth_place);
```

`PlanCoverageOrder`: with no early and no late item: `{sky_place, kNoCoverageTail}` (today's
order). Else `first_late` = the first late item's index (`count` if none); `tail` =
`min(sky_place, depth_place, first_late)` when an early item lies before that index, else
`kNoCoverageTail`; `sky` = `min(sky_place, tail, first_late)`. An early item at or beyond `tail`
is drawn as a late one (blended in place). At index `i`: the sky step if `sky == i`, then the
early tail if `tail == i`, then the layer's depth step if it is due, then record `i`.

- [ ] Tests first (see them fail):

```cpp
  // CoveredSamples: the emulator's two thresholds, and the partition of today's set (plan Task 12; Review Focus 4)
  for (uint32_t offsets = 0; offsets < 256; ++offsets) {
    for (uint32_t parity = 0; parity < 4; ++parity) {
      const uint32_t x = parity >> 1, y = parity & 1u;
      const float o = float((offsets >> (2 * parity)) & 3u);
      for (int i = 0; i <= 1280; ++i) {
        const float a = float(i) / 1024.0f;
        const uint32_t n = CoveredSamples(uint8_t(offsets), x, y, a);
        CHECK(700, n == (a >= 0.5f - 0.125f * o ? 1u : 0u) + (a >= 1.0f - 0.125f * o ? 1u : 0u));
        CHECK(701, (n >= 1u) == AlphaToMaskCovers(uint8_t(offsets), x, y, a, 1));
      }
    }
  }
  CHECK(702, CoveredSamples(0x87, 0, 0, std::numeric_limits<float>::quiet_NaN()) == 0);
  CHECK(703, !CoverageHasFull(0xAA, 0.5f) && CoverageHasFull(0xAA, 0.75f) && !CoverageHasFull(0x87, 0.6f) &&
                 CoverageHasFull(0x87, 0.625f));
  // PlanCoverageOrder: every arrangement of up to six records
  for (size_t count = 0; count <= 6; ++count) {
    for (uint32_t code = 0; code < (1u << (2 * count)); ++code) {
      CoverageItem items[6];
      size_t sky_place = count, first_late = count, first_early = count;
      bool any = false;
      for (size_t i = 0; i < count; ++i) {
        const uint32_t k = (code >> (2 * i)) & 3u;                  // 0 opaque, 1 early, 2 late, 3 blended
        items[i] = {k == 1 ? CoverageKind::kEarly : k == 2 ? CoverageKind::kLate : CoverageKind::kNone, k == 3};
        if (k == 3 && sky_place == count) sky_place = i;
        if (k == 2 && first_late == count) first_late = i;
        if (k == 1 && first_early == count) first_early = i;
        any = any || k == 1 || k == 2;
      }
      for (size_t depth_place = 0; depth_place <= count; ++depth_place) {
        const CoverageOrder o = PlanCoverageOrder(items, count, sky_place, depth_place);
        if (!any) {
          CHECK(740, o.sky == sky_place && o.tail == kNoCoverageTail);
          continue;
        }
        CHECK(741, o.sky <= sky_place && o.sky <= first_late);                      // the sky before every blend
        if (o.tail != kNoCoverageTail) {
          CHECK(742, o.sky <= o.tail && o.tail <= depth_place && o.tail <= first_late && o.tail <= sky_place);
          CHECK(743, first_early < o.tail);                                         // a tail only for an early record before it
        } else {
          CHECK(744, first_early >= std::min(std::min(sky_place, depth_place), first_late));
        }
      }
    }
  }
```

      and `CoverageOf`'s table test (each condition switched off in turn gives `kNone`; a scale
      of exactly 1, of a NaN, of -0.1; offsets `AA` late, `87` early). Python:
      `lf.covered_samples` equals a transcription of the SDK's lines 1681 to 1790 for the same
      grid.
- [ ] Open question 6: from Task 4's captures, the `"bin_mask_lo"` of every fading and late row
      against `xdk::HasColourPass`'s and the pre-pass bins' bits: are they in the game's
      pre-pass. The answer, with rows named, goes into the frame map and decides the wording of
      limit "the mask at a half-covered pixel".
- [ ] The captured alpha scale: in every family A row of Task 4's captures the record's
      `material.alpha_scale` against pixel `c15.w` x `c26.w` (`wind_check.py inputs` prints both
      beside the wind's): if `c26.w` is not 1 in some row and the record's scale lacks it, report
      to the controller before Task 13.
- [ ] Commit "Coverage rules: covered samples, the coverage record, the order of the colour
      pass".

### Task 13: Coverage, the late draws

**Files:** `fable2_native_shaders.h`, `clay_logic.h`, `clay_pass.cpp` / `.h`,
`fable2_native_render.cpp` (the switch `fable2_native_coverage`, off),
`tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`, README, frame map
"Native coverage".

**Produces:** `PipelineKey` gains `uint8_t coverage = 0` (1 half, 2 full).
`shaders::kClayTexturedHalfPs` = `"#define CLAY_COVERAGE 1\n" FABLE2_CLAY_TEXTURED_HLSL`: in the
alpha-to-mask block, under two samples, `covered = (a >= t0) + (a >= t0 + 0.5)` and `if (covered
!= 1) discard;`, and every colour return's alpha is `0.5`. The key of a half draw: the record's
key with `coverage = 1`, blend on, source alpha, inverse source alpha, the record's depth test and
depth write. `ClayCoverage` verdict per record (`kOff`, `kNone`, `kPipeline`, `kDrawn`),
`ClayStats` and the F3 line `Coverage: on, late N, early M (tail before record K) | not drawn
(coverage: pipeline) n`. In this task: late records alone. A late record is left out of the
pre-pass when `!CoverageHasFull(offsets, scale)`; in the colour pass it is drawn once, with the
half program, at its own place; the sky step is drawn at `PlanCoverageOrder(...).sky`.

- [ ] **First, in writing, before any code** (spec ruling 14's condition): the colour pass's loop
      as it is (`clay_pass.cpp` lines 3187 to 3271: the layer's depth step, `draw_sky`, the
      quads' places) with the three insertions marked, and the two cases checked by hand: sun on
      at the bridge (three late twins, a depth place) and sun off towards the sun (one late
      record, no place, the sky's place after it). If an insertion cannot be made without
      changing what a frame without coverage records draws: stop, report, no code.
- [ ] Tests first: `CoverageOf` feeding the key (a late record's half key; a record at scale 1
      keeps today's key bit for bit); the shader test: with `CLAY_COVERAGE` undefined the
      textured colour and depth-only programs are the baseline's bytes; `kClayTexturedHalfPs`
      holds both thresholds (0.5 apart) and writes the constant alpha 0.5; no other program names
      `covered`.
- [ ] Implement. A late record whose half pipeline cannot be made is today's record in both
      passes (`coverage: pipeline`). Suites; build.
- [ ] NEEDS_RUN: WARP-SUN with POST, SUN, COV; WARP with POST, SUN, COV (the bridge: three late
      twins a frame); then two SUNWARD runs with POST, READBACK, DUMP, one with COV and one
      without (sun off: criterion 12); SUNWARD with POST, SUN, COV, READBACK, DUMP.
- [ ] Criterion 12 from the pair (the emulated shots' agreement first; then the four regions'
      on / off against the band, with the 55 s bracket named as such); criterion 11's layer
      depth from the sun-on dump; the holes count; the late tree viewed beside the emulated
      shot (a see-through shape over the sky; the hero behind it; anything darker than before
      is said).
- [ ] Frame map; commit "Native coverage: the late draws at half over what lies behind".

### Task 14: Coverage, the fading draws

**Files:** as Task 13.

**Produces:** `shaders::kClayTexturedFullPs` (`CLAY_COVERAGE 2`: `if (covered != 2) discard;`,
otherwise today's program) and `kClayTexturedFullDepthPs` (the same with `CLAY_DEPTH_ONLY`). An
early record: in the pre-pass its full depth-only program (left out when `!CoverageHasFull`); in
the colour pass its full program in place (left out likewise) and its half program in the tail at
`PlanCoverageOrder(...).tail`, the early records in their own order; the layer's depth step, when
due at that index, after the tail.

- [ ] Tests first: the shader test (the full programs' one threshold is the second sample's; the
      three coverage programs' discard sets partition the baseline's over the grid of Task 12,
      by evaluating the compiled thresholds as the existing alpha-to-mask shader test does);
      `test_clay_logic.cpp`: an early record's three keys; an early record at or beyond the tail
      is treated as late.
- [ ] Implement; suites; build.
- [ ] NEEDS_RUN: WARP-SUN and WARP with POST, SUN, COV; SUNWARD with POST, COV, READBACK, DUMP
      (sun off; against Task 13's coverage run: criterion 13); BRIDGE and THIRD with POST, COV
      and without (the dissolve band's trees: viewed).
- [ ] Criterion 13 with the replay's pixel counts per region beside it; criterion 11 again
      (the fading trees must still be in the layer's depth: the dump's layer plane against the
      replay's first-sample cover of the early records, at least 95% of that cover closed).
      Viewed: a dissolving tree against the sky and against a hill.
- [ ] Frame map; commit "Native coverage: the fading draws by covered samples".

### Task 15: Validation and documents (11b)

*As executed (annotated by Task 18, 2026-10-11; the list below is the plan as written):* the runs were not made on a
quiet machine at width 1942 (the window stood on the other monitor: no timing evidence in the sub-project). The user
checks named 83 and 84 below were appended as **85 and 86** (83 and 84 were taken by phase 11a), with 87 and 88 beside
them. Checks 66 and 72 were reworded and FM 1898 and 2414 item 5 marked by Task 18. Task 14's "full depth-only program"
for an early record's pre-pass was not built: an early record keeps today's pre-pass draw (spec, implementation notes).
`lighting-formulas.md` was not changed by this task or by Task 18 (no formula changed).

- [ ] Runs by the controller on a quiet machine, shot width 1942, no agent beside them: BRIDGE
      and THIRD with POST, each with COV and without; SUNWARD with POST, SUN, WIND, COV.
- [ ] Criteria 10 to 14 with evidence and exact verdicts; the default stays off (ruling 22) with
      the recommendation written.
- [ ] Spec, frame map "Validation (11b)" and "Limits (11b)" (the draw-order errors of spec
      Design 11, with what the runs showed of each), FM 1898 and 2414 item 5 marked as withdrawn
      for family A, `lighting-formulas.md`; user checks 83 (a tree between you and the hero: a
      see-through shape in both halves, the hero visible through it, and no dark outline or box
      about it) and 84 (a tree dissolving in the distance as you walk towards it: it thickens
      gradually in both halves; no tree that flickers between solid and absent; no dark
      silhouette where the emulated half has a faint one); checks 66 and 72 reworded; README.
- [ ] Commit "Coverage validation (11b): the criteria's verdicts". Then the phase's review.

## Phase 11c: the tree occluder, the darkness, criterion 19

*Not in this plan:* the mend of the half blend (ledger ruling 60, commits `5f66094`, `b37b77d`, `ab3e53f`;
spec implementation notes, "Design 11 amended": a coverage record's half part is two draws, its depth and
then its colour, so the Architecture's "two draws" above is three at most), and the fix wave after the
whole-branch reviews (ledger ruling 67; frame map section 18, "The fix wave").

### Task 16: The tree occluder's redraw

**Files:** `src/native/render/frame_scene.h`, `src/native/capture/draw_record.h` (comment only),
`clay_logic.h`, `clay_pass.cpp` / `.h`, `tests/native/test_frame_scene.cpp`,
`tests/native/test_clay_logic.cpp`, `tools/xdk_sigmatch/foliage_replay.py` (`occluder`),
`tests/test_foliage_replay.py`, README, frame map "The tree occluder".

**Consumes:** `FrameBuilder::LayerOccluderSeen` (`frame_scene.h` line 692), `FindOccluderTwin`,
`kMaxLayerOccluders`; the pre-pass's textured depth-only program (`depth_ps_[1]`) and the
one-sample alpha-to-mask form (`DecideShadowDraw`'s alpha fields, `clay_logic.h` 5689 to 5712, are
the pattern: sample bits 0); the sun layer's depth step and its viewport's half-pixel origin;
`wind_vs_` and the twin's wind verdict (Task 10); Task 2's two layer planes.

**Produces:** `FrameScene::layer_twins[capture::kMaxLayerOccluders]`, `layer_twin_count` (each
found twin's index once, in the order seen; none with a failed call: the frame then has no layer,
as today). `SunTreeDraw DecideSunTree(const capture::DrawRecord& twin, const capture::RenderState&
rs, bool textured)` (`skip` reasons `state`, `alpha`, `texture`; the alpha fields: from the
albedo's component, the test with the record's function and reference, alpha-to-mask with the
record's offsets on one sample, the record's scale; no cull). `sun_tree_pipelines_[2]` (rest,
wind): the clay layout, no colour target, the layer's depth with its test and write. The draws:
after the cloud occluders, before layer 1. The Sun line gains `tree occluders drawn K of N`.

- [ ] Tests first: `test_frame_scene.cpp` (three calls with three twins give three indices in
      order; the same twin twice gives it once; a failed call empties the list; the seventeenth);
      `test_clay_logic.cpp` (a twin with the leaf cards' word `0xAA00001C` and scale 0.5 gives
      the threshold 0.5 at every parity through `ShadowAlphaToMaskThreshold`; an untextured twin
      is skipped `texture`); `test_foliage_replay.py` (`occluder_cover` on a made card: only
      texels of alpha exactly 1 are kept).
- [ ] Implement; suites; build.
- [ ] NEEDS_RUN: WARP with POST, SUN, WIND, COV (three twins a frame); WARP-SUN with the same;
      PAIR-SUN with WIND, COV; BRIDGE with POST, SUN, WIND, COV, DUMP, READBACK.
- [ ] Criterion 15 from the two dumps' layer planes against `foliage_replay.py occluder` on the
      paired frames, at the base level and at each stored level Task 1 kept; the best-fitting
      level reported (open question 7). The beams viewed through the faded tree.
- [ ] Frame map; commit "Tree occluder: the late twins drawn into the layer's depth".

### Task 17: The darkness trace

**Files:** `tools/xdk_sigmatch/foliage_trace.py` (new), `tests/test_foliage_trace.py` (new),
frame map "The darkness about the sun"; code only under ruling 5 (then its own test first and its
own commit).

**Consumes:** Task 2's file format (`"F2NS"`), `post_replay.py`'s oracle scene of a capture
frame, `foliage_replay.py`'s `raster` and `alpha_cover`, `docs/native-renderer/post-regions.json`.

**Produces:** `read_dump(path)` -> head, `scene[h, w, 3]`, `depth[h, w]`, layer planes;
`classes(capture, frame)` -> an array of class ids (0 open sky, 1 late tree over sky, 2 leaf,
3 trunk and wood, 4 ground, 5 other) from the replay's nearest draw per pixel and the game's
depth copy; `trace(dump, capture, frame, regions)` -> per region and class: pixels, the game's
and the native mean linear luminance (`0.2125 r + 0.7154 g + 0.0721 b`), their ratio `r_k`, and
the class's part of the region's ratio, `delta_k = s_k (r_k - 1)` with `s_k` the class's share of
the game's luminance in the region (the parts sum to the region's ratio less 1, exactly);
`verdict(rows)`: `assigned` when what class 5 and the pixels counted out hold moves the region's
ratio by at most 0.05 in logarithm (`|ln R - ln(R - delta_5)| <= 0.05`, fewer than 1% of the
pixels counted out), else `not assigned` (spec ruling 18); `holes(dump)` (criterion 7); `halfpixel(dump, capture, frame)` (the shift in (-1, 1)
pixels that best aligns native and game edges of unmoving geometry; reported); `predict(...)`
(criterion 17's figure per region from T16-5's table, the measured coverage ratios and the
redraw's kept set, through the screen formula as `out/tests/post-task18/separate.py` lays a factor
over a scene); subcommands of the same names and `head`.

- [ ] Tests first, on made arrays (`tests/test_foliage_trace.py`):

```python
class Trace(unittest.TestCase):
    def test_shares_reproduce_a_made_shortfall(self):
        game = np.ones((4, 4, 3))
        native = np.ones((4, 4, 3))
        cls = np.zeros((4, 4), dtype=np.uint8)
        cls[:2] = 2                      # the upper half is leaf
        native[:2] *= 0.5                # and half as bright natively
        rows = ft.trace_arrays(game, native, cls, {"all": (0, 0, 4, 4)})["all"]
        self.assertAlmostEqual(rows["region"]["ratio"], 0.75)
        self.assertAlmostEqual(rows[2]["ratio"], 0.5)
        self.assertAlmostEqual(rows[0]["ratio"], 1.0)
        self.assertAlmostEqual(rows[2]["delta"], -0.25)
        self.assertAlmostEqual(sum(r["delta"] for k, r in rows.items() if k != "region"), 0.75 - 1.0)
        self.assertEqual(ft.verdict(rows), "assigned")
        cls[:2] = 5                      # the same pixels unclassified: the shortfall is in no named class
        rows = ft.trace_arrays(game, native, cls, {"all": (0, 0, 4, 4)})["all"]
        self.assertEqual(ft.verdict(rows), "not assigned")

    def test_a_class_without_pixels_and_values_that_are_no_numbers(self):
        game = np.ones((2, 2, 3))
        native = np.ones((2, 2, 3))
        native[0, 0] = np.nan
        rows = ft.trace_arrays(game, native, np.zeros((2, 2), dtype=np.uint8), {"all": (0, 0, 2, 2)})["all"]
        self.assertEqual(rows[0]["pixels"], 3)            # the NaN pixel is counted out, and said
        self.assertEqual(rows["region"]["bad"], 1)
        self.assertNotIn(2, rows)

    def test_holes(self):
        scene = np.full((2, 2, 3), 0.3)
        depth = np.full((2, 2), 16777215, dtype=np.uint32)
        clear = (0.02, 0.02, 0.03)
        scene[0, 0] = clear                               # clear colour at the clear depth: sky, not a hole
        scene[1, 1] = clear
        depth[1, 1] = 1000                                # clear colour under a nearer depth: a hole
        self.assertEqual(ft.holes_arrays(scene, depth, clear), 1)
```

      (`rows` is a dict keyed by class id, with `"region"` beside them; the file's head imports
      `numpy as np` and `foliage_trace as ft` as `tests/test_foliage_replay.py` imports its
      tool.)
- [ ] NEEDS_RUN, if Task 16's PAIR-SUN dump is not of the frame its capture begins with: PAIR-SUN
      with WIND, COV once more. Then `trace` on the paired frame for "sun", "beams left", "beams
      right", "beam root": criterion 16's table; `halfpixel`; the same with COV off from Task
      13's dump (how much of each class's ratio coverage moved).
- [ ] For each class outside 0.85 to 1.18: the candidate causes of R B7's table against the
      rows of that frame (the leaf's light: `light_check.py` on the frame's leaf rows towards
      the sun, front and back term apart; shadowed wood: the mask's value in that class from the
      dump against the game's `tf8` if a row holds it). **A mend is made only when an existing
      formula's own check fails on those rows** (ruling 5): then a failing test first, the fix,
      WARP if shader text changed, SUNWARD again, and the trace again. Anything else: a named
      roadmap item with its evidence, no code.
- [ ] Frame map; commit "Darkness trace: native over game by class about the sun" (and, if made,
      a separate commit for the mend).

### Task 18: Validation and documents (11c); criterion 19 again; closing

*As executed (2026-10-11; the list below is the plan as written):* eight closing runs by the controller, every shot 1296
wide, so no timing verdict (ledger ruling 65); the third spot and the trees were not run. The prediction of criterion
17 was written before the closing shots and was not blind. The frame map's new sections are "Validation (11c)",
"Limits (11c)" and "Follow-ups (11)". The user checks named 85 and 86 below were appended as **89 and 90**, with 91 (the
cost) and 92 (everything off; the defaults); the roadmap's line reads "User checks 40 to 92".

- [ ] Runs by the controller on a quiet machine, shot width 1942, no agent beside them: SUNWARD
      with POST, SUN, WIND, COV and with POST, SUN alone (three brackets each; the emulated
      shots' agreement first); BRIDGE and THIRD with POST, SUN, WIND, COV and with POST alone
      (timing: criterion 9 for the whole); TREES with POST, SUN, WIND, COV.
- [ ] Criteria 15 to 19 with evidence and exact verdicts. Criterion 17: `foliage_trace.py
      predict` first, written down before the shots are measured; then the regions; then
      sub-project 10's criterion 19 as it falls, and the recommendation for
      `--fable2_native_sun`'s default. The root's sign (open question 9).
- [ ] Every open question of the spec with what became of it; every limit of "Limits known now".
- [ ] Closing documents: spec status and "Implementation notes (deviations)" with the closing
      table of all 19 criteria; frame map section 18 "Validation (11c)", "Limits (11)",
      "Follow-ups (11)", and the stale statements R D12 lists marked where they stand (FM
      "Wind and displacement" 963; "Limits (10c)" 1 to 3 and "Follow-ups (10c)" 1 and 3; FM
      6820); `docs/native-renderer/roadmap.md` (item 1 ticked with its outcome; what the trace
      named added where it belongs; "User checks 40 to 78" becomes "40 to 86");
      `lighting-formulas.md`; `user-checks.md` 85 (the beams through a faded tree with the sun
      behind it: as strong as the emulated half's, not stronger) and 86 (a town, an interior,
      night, a storm if the game has one: with all four switches on, do trees and banners move
      as in the emulated half, and does the Wind line count `register-range` or `no-entry`);
      README (the three switches' rows, the Wind and Coverage lines, the scene dump).
- [ ] Commit "Foliage validation (11c): criterion 19 judged again, the closing table". Then the
      whole-branch code review and the documents review.
