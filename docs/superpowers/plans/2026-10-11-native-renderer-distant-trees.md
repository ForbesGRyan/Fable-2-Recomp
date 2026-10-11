# Native renderer distant trees (the far woods' impostors): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. One fresh
> implementer and one fresh reviewer per task; steps use checkboxes.

**Goal:** the game's far woods stand in the native view: one camera-turned quad a tree, placed and
sized by the game's own vertex formula from the game's own per-tree data, dissolved by distance,
lit by the game's two scalars, fogged once a tree and written into the native depth, behind
`--fable2_native_impostors` (default off). Phase 12a makes the oracle (discovery, formulas,
decoder; no renderer change); phase 12b the capture and the draw in one flat colour, validated;
phase 12c, **only if the cost pair passes its bound**, the game's own pictures by forced
read-back.

**Architecture:** a `BeginVertices` call of the pair `129F` / `4B1D` inside the main scene makes
no draw record. Its registers, fetch constants and state are read at the call's return and its
storage one hooked draw later into a bounded side block of the frame's scene (at most 32 records
and 4,096 quads, a draw whole or missing). The pass draws the block in one step with a layout and
programs of its own: six vertices a quad by `SV_VertexID`, the quad's four floats from a buffer
bound by address, the position by a single-precision mirror of the machine-checked reference,
the fog once a vertex from the frame's tables. The step lies in the colour pass at the records'
place and nowhere else (no pre-pass draw: ruling 21). The picture is a 1 x 1 flat texture in
`flat`; in `on` it is the game's 256 x 256 picture of the model, decoded on the CPU from guest
memory that a seeded read-back list keeps current. A frame, a record or a switch that fails any
rule is today's picture for that model.

**Tech stack:** C++ (capture in `src/native/capture`, pass in `src/native/render`, the switch in
`src/native/fable2_native_render.cpp`, the read-back list in `src/core`), HLSL in
`src/native/fable2_native_shaders.h`, Python tools in `tools/xdk_sigmatch` (numpy), tests in
`tests/` and `tests/native/`.

**Spec:** `docs/superpowers/specs/2026-10-11-native-renderer-distant-trees-design.md` (its Design,
ruling and criterion numbers are cited; amended for ledger rulings 21 and 22). Ledger:
`.superpowers/sdd/2026-10-11-native-renderer-distant-trees/progress.md`. Research:
`.superpowers/sdd/distant-trees-research.md` ("R" with a section; read it before any task; its
scripts and outputs are in `out/native-evidence/distant-trees/`, "DT" below). "FM" is
`docs/native-renderer/frame-map.md`.

## Global Constraints

- Branch `native-renderer-distant-trees`. **No stash, reset, push, amend or rebase by an agent**;
  no branch switch. **Never stage `fable_2_manifest.toml`; never `git add -A`**; `*.ps1` is
  git-ignored and needs `git add -f`. Every commit is a pathspec commit:
  `rtk git add <paths>` then `rtk git commit --only -F <message file> -- <paths>`. The commit
  trailer is exactly these two lines after a blank line:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`. New files have LF
  line endings.
- **Every shell command is prefixed with `rtk`** (`rtk git ...`, `rtk python ...`,
  `rtk clang++ ...`, `rtk .\build.cmd ...`). **No Windows backslash path inside an inline Python
  or heredoc string**: write forward slashes there, or put the script in a file.
- **`DrawRecord` stays 616 bytes** (`static_assert` in `src/native/capture/draw_record.h`, lines
  137 and 138); **no draw record is made or changed for an impostor call**, and the
  `unsupported by hook` line keeps counting those calls as it does today (spec ruling 10).
- **No SDK change** (`thirdparty/rexglue-sdk`) and **no `--readback_resolve=fast`** by this
  sub-project (ledger ruling 22). A seeming need: stop and report.
- **Game launches wait for the user's leave** (ledger ruling 4). **The game starts only through
  `tools/drive_game.ps1`**, with `-BootSave Hero000`. **Implementers never launch the game and
  never pass `-UserPresent`**: a task that needs a run ends that step with `NEEDS_RUN`, the exact
  lines below, what each run must produce and where it wants it copied; the controller makes the
  run and its evidence is labelled `RUN UNCHECKED`. Every run is checked for the freeze signature
  before a number or picture is used.
- **Run shape**: `-Total` at most 100 s (240 s on the software adapter); **no shot within 3 s of
  a view switch**; **one stick entry holds at most 60000 ms**; `--mouse_look=false` in every
  run's arguments; only the plan tokens `tools/drive_game.ps1` has (`<s>:VIEW=off|split|overlay|
  native`, `<s>:DISC=<frames>/<every>`, `<s>:LS=x/y/ms`, `<s>:RS=x/y/ms`). The camera stands for
  at least 5 s before a capture; a view that exists only while a stick is held has its shots and
  its capture inside the hold.
- **Timing runs count only at shot width 1942, each made alone** (no agent, no analysis, no
  build, no test, no other game beside it), with the head's uncapped default
  (`seeded guest_vblank_pacing=false` in the log) stated. Every other run is "no timing
  evidence". A read-back run on the other monitor is slow and is not a timing run either.
- **Before any real-GPU launch of a build that changes bindings, targets or shader text: one
  clean 240 s software-adapter run with the debug layer** (the lines WARP-FLAT and WARP-ON), 0
  `d3d ERROR` lines, **and the log shows the step drew** and the shot shows quads or trees on the
  ranges. Tasks 10, 14 and 15 are such builds.
- **Antivirus rule**: test executables are built only under `out\tests\runtime-unit-tests\`. A
  binary that is removed, locked or refused: stop and report BLOCKED with file, time and message;
  no retry loop, no rename, no change of options. While the locked
  `out\tests\runtime-unit-tests\native_shaders.exe` exists, the suite runs through a scratch copy
  of the runner in which only the shader test's output name differs
  (`out\tests\run_native_tests_t<N>.cmd`, generated from the present script each time).
- **One builder at a time** (one process building the game or the test executables in this
  tree). **Reviewers are read-only.** **A mutation check is run alone**: on a scratch copy, never
  on a tracked file, with no other test, build or game running.
- **Fail-closed rules (the spec's, collected; binding as written):**
  - No value read from the guest reaches a shader without having been read whole, a finiteness
    check and a bound. Reasons, counted in the frame's scene, on F3 and in the log:
    `impostor-record {unread, shader, literal, storage, quad, register, scale, state, fetch,
    ground-fog, fog-table, over-limit, pending}`; in the pass `impostors not drawn {pipeline,
    slot, buffer, scale, picture: base, picture: unread, picture: content, picture: upload}`.
  - At most 32 draws and 4,096 quads a frame; a draw past either is not drawn, whole.
  - A quad's words are read only for a quad below the draw's count, inside a branch (a buffer
    bound by address has no bounds check).
  - A draw is drawn by the formula or not at all: no approximate billboard, no flat quad for a
    model whose picture is missing.
  - A position that is no number in the reference is no number natively; nothing else may be.
  - The read-back list is a seeded constant; a picture whose base is not in it is not used.
  - With the switch off nothing of this sub-project is reached.
- **With `--fable2_native_impostors=off`** (the default; also `false`, `0`): nothing is read, no
  record, array, object, slot or program is made, the frame's slot plan is today's byte for
  byte, the read-back list is today's, and every compiled program of the pass equals today's.
- **A formula that does not close draws nothing** (spec ruling 18): if Task 6's `rows` check
  fails on any captured quad, Tasks 9 to 16 are not started; Task 8 (the capture) is still
  built, and the sub-project ends at the capture and the formula's report.
- **New targets and textures are resolution-independent**; nothing of this plan takes a size
  from the window. Texel sizes reach a shader as constants.
- Tests first where a pure function can hold the rule; a Python reference, machine-checked
  against the shader dumps, and a C++ mirror pinned to it come before any shader text.
- Evidence goes to `out/native-evidence/distant-trees/`; every number names its file; the labels
  [measured], [microcode], [code], [inferred], [unknown] are kept where a fact is repeated; a
  verdict is "met", "not met as written", "not decidable" or "by a substituted measure"; every
  before-and-after figure pairs an off-run and an on-run of one session, one binary and one plan
  (spec ruling 16).
- New switch: `--fable2_native_impostors` = `off` (default; also `false`, `0`), `flat`, `on`
  (also `true`, `1`). New reason strings: those of the fail-closed rules above, exactly.

**The workspace's rule files** (`.superpowers/sdd/2026-10-11-native-renderer-distant-trees/`:
`global-constraints.md`, `implementer-rules.md`, `reviewer-rules.md`) were copied from an earlier
sub-project and bind every task. Where they name another sub-project, read: branch
`native-renderer-distant-trees`; plan and spec the two files above; research
`.superpowers/sdd/distant-trees-research.md`; the rules folder
`.superpowers/sdd/2026-10-11-native-renderer-distant-trees/`; the evidence folder
`out/native-evidence/distant-trees/` (previous: `out/native-evidence/foliage/`; FM section 18
names its files); the plan's "Additions" are this plan's run lines. Their "only while the user is
away" binds agents as written; the controller's runs follow ledger ruling 4. The environment
paragraph of `global-constraints.md`, verbatim:

> Environment: PATH begins with `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin`,
> then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin`.
> Game build: `& .\build.cmd -release fable_2` (no `fable_2.exe` running; never kill one you did not
> start). Python tests: `python -m unittest discover -s tests -p "test_*.py"`. Native tests: the
> implementer rules' section "Antivirus on this machine" (test executables only in
> `out\tests\runtime-unit-tests\`; the shader test under another output name through a scratch copy
> of the runner while the locked file exists; any binary removed, locked or refused: stop and report).
> Start the game with `-BootSave Hero000` (world from about 23 s, views from 32 s, captures from 33 s).

**Commands.** Python suite: `rtk python -m unittest discover -s tests -p "test_*.py"`; one file:
`rtk python -m unittest tests.test_impostor_formulas -v`. Native suite: `rtk cmd /c
tests\run_native_tests.cmd` (or the scratch copy, above); one test: `rtk clang++ -std=c++23
tests\native\<file>.cpp -o out\tests\runtime-unit-tests\<name>.exe` then `rtk
out\tests\runtime-unit-tests\<name>.exe`. A new test executable is entered in
`tests/run_native_tests.cmd` as two lines before the final `exit /b 0`, in the form of lines 97
and 98 (`clang++ -std=c++23 "%~dp0native\test_<name>.cpp" -o "%OUT%\<name>.exe" || exit /b 1`,
then `"%OUT%\<name>.exe" || exit /b 1`). Game build: `rtk .\build.cmd -release fable_2` (no
`fable_2.exe` running; never kill one you did not start).

**Run lines** (after `tools\drive_game.ps1 -BootSave Hero000`; the controller adds `-UserPresent`;
`-GameArgs` is a list of quoted elements, **each element in its own quotes** because one of them
holds commas; `BASE` is `"--fable2_native_render=true","--mouse_look=false"`):

| Name | Arguments |
|---|---|
| BRIDGE | `-Total 90 -Shots "36,44,74,86" -Plan "32:VIEW=off,40:VIEW=native,48:DISC=2/1,70:VIEW=off,82:VIEW=native"` |
| THIRD | `-Total 96 -Shots "44,50,80,92" -Plan "33:LS=0/-100/3500,40:VIEW=off,46:VIEW=native,54:DISC=2/1,76:VIEW=off,88:VIEW=native"` |
| TREES | `-Total 100 -Shots "58,64,90,97" -Plan "33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/2500,47:RS=-60/0/600,50:RS=60/0/600,54:VIEW=off,60:VIEW=native,68:DISC=2/1,86:VIEW=off,93:VIEW=native"` |
| SUNSIDE | `-Total 96 -Shots "44,50,80,92" -Plan "33:RS=-100/0/500,36:RS=-100/0/500,40:VIEW=off,46:VIEW=native,54:DISC=2/1,76:VIEW=off,88:VIEW=native"` |
| SUNHELD | `-Total 100 -Shots "47,53,84,96" -Plan "33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/59000,43:VIEW=off,49:VIEW=native,57:DISC=2/1,80:VIEW=off,92:VIEW=native"` |
| TIME | `-Total 100 -Shots "36,42,57,60,96" -Plan "32:VIEW=off,39:VIEW=native,90:VIEW=off"` |
| TIME-CAP | `-Total 100 -Shots "36,42,57,60,96" -Plan "32:VIEW=off,39:VIEW=native,90:VIEW=off,91:DISC=2/1"` |
| TIME-THIRD | `-Total 100 -Shots "43,57,96" -Plan "33:LS=0/-100/3500,40:VIEW=off,46:VIEW=native,90:VIEW=off"` |
| OFF | `-Total 70 -Shots "40,50,60" -Plan "32:VIEW=native,45:VIEW=split,55:VIEW=off" -GameArgs BASE` |
| WARP | `-Total 240 -Shots "225" -Plan "32:VIEW=native"` with `-GameArgs BASE,"--d3d12_adapter=-2","--d3d12_debug=true","--fable2_native_d3d_messages=true"` and the suffixes the task names |

Suffixes (elements appended to `-GameArgs BASE`): READBACK
`"--readback_resolve=full","--native_render_collect_stats=true"`; POST
`"--fable2_native_post=on","--fable2_native_bloom=on"`; SUN `"--fable2_native_sun=on"`; WIND
`"--fable2_native_wind=on"`; COV `"--fable2_native_coverage=on"`; DUMP
`"--fable2_native_scene_dump=on"`; EXPO `"--fable2_exposure_readback=on"`; FLAT
`"--fable2_native_impostors=flat"`; ON `"--fable2_native_impostors=on"`; FOURTEEN (one element,
the fourteen bases `0x12704000 + n * 0x2B000`, n = 0..13)
`"--readback_resolve_force_addresses=0x12704000,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000,0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000"`.
PAIR is `POST, READBACK, DUMP`: a capture under full read-back in the native view, whose first
frame the stored native scene pairs with.

What the lines hold to: BRIDGE, THIRD, TREES and SUNSIDE stand from their last stick entry (none;
36.5 s; 50.6 s; 36.5 s) to their capture (48; 54; 68; 54 s), at least 5 s; every shot is at
least 4 s after a view switch, except TIME's shot at 42 s (3 s after the switch at 39 s, as logs
654 to 657 have it) and TIME-THIRD's at 43 s. SUNHELD's look exists only while the stick is held
(sub-project 11, FM section 18): its hold runs from 41 s to the run's end, and its four shots and
its capture lie inside it. TIME is the plan of logs 654 to 656 (`k18b_time_*`: standing, the
native view from 39 to 90 s), unchanged; OFF is the plan of log 657 (`k18b_defaults`). A capture
of two frames at `/1` has finished within 20 s in every earlier run of these plans; a run whose
capture file holds fewer than two `"frame"` rows is said to hold so and is made again.

## Review Focus

The five failure modes most likely to pass unnoticed, each pinned by a test in its owning task:

1. **A read of the quad words that is not bounded** (a buffer bound by address has no bounds
   check; a count or an offset that came from the guest; a draw whose words lie past the
   buffer's end). Owner: Tasks 9 and 10. Pin: `MakeClayImpostorConstants` refuses
   `first_quad + quads > kMaxImpostorQuads` (CHECK 806), `FrameBuilder::AddImpostor` takes a
   draw whole or not at all (Task 8, returns 204 to 207), and the shader test "every load from
   `impostor_words` lies inside a conditional whose condition compares the quad's number with
   `i_quads`" (Task 10, return 262).
2. **Storage read too early, read after it was recycled, or decoded in a guessed order**
   (positions that are plausible and wrong). Owner: Tasks 3, 6 and 8. Pin:
   `impostor_check.py records` (every record equal to its `"verts"` and `"vertsdata"` rows bit
   for bit), `MakeImpostorRecord`'s `quad` rule (Task 8, returns 20 to 26), the mutation
   "the fetch's first two components exchanged" failing `rows`, and criterion 3's two controls
   (size x 1.1; the raise left out) each falling by at least 10 points.
3. **A formula check that cannot fail** (a tolerance that passes a wrong term; inputs on which
   the `log` / `exp` block or one branch of the height comparison is never taken; a mirror
   checked against itself). Owner: Tasks 4 to 6 and 9. Pin: `test_mutations_fail` and the floors
   of `check_position` in `tests/test_impostor_formulas.py` (each corner at least a fifth of the
   trials, each side of the height comparison at least a quarter, the `log` / `exp` block on
   numbers in at least half; for the fog, begun in at least half), and pins generated from the
   Python reference alone (`impostor_pins.py`), never from the C++ mirror.
4. **A switch that is off and still costs or changes something** (the read-back list with `off`
   or `flat`; the slot plan; a program's bytes; code added to a draw's entry; format 3 reaching
   a product path through `DumpFormatInfo`). Owner: Tasks 1, 8, 10 and 12. Pin:
   `ForceReadbackList(hero, exposure, false)` equal to today's string for all four inputs (Task
   12, returns 20 to 23); `PlanClayFrameSlots(t, p, 0)` equal to today's arithmetic, written
   out, over a grid (CHECK 812); the shader test "every program of the pass compiles to the
   baseline's bytes" (Task 10, return 260); `DecodeNormalMap` refusing a format-3 constant (Task
   1, `tests/native/test_terrain_patch.cpp`, return 260); the frame row's `"impostors":"off"`
   on an off-run (criterion 13).
5. **Depth and colour where the game has neither** (a flat quad in a missing picture's place in
   `on`; a record of another frame; a draw past a bound drawn in part; a picture that is noise).
   Owner: Tasks 8, 9, 13 and 14. Pin: `DecideImpostorPicture`'s exhaustive test (CHECK 815: in
   `on` anything but a usable picture is not drawn, never flat), `FrameBuilder`'s tests of the
   33rd draw and of the quad bound (Task 8, returns 204 to 206), a call whose record never
   came counted `pending` and nothing of one frame left in the next (Task 8, returns 210 and
   211), and `ImpostorPictureUsable` refusing an empty picture (Task 13, return 11) and, by the
   fit's evidence, every noise picture of capture `054648`.

Also for every reviewer: guest reads whole or not at all, copied before they are judged; the
frame rechecked under the scene's mutex; evidence that names its file; what a run did not show is
said.

## Numbers that are provisional, and the task that fixes each

Sub-project 11's interface blocks were written before its formulas closed and had to be amended
(word counts, literal counts, block sizes). Here every number of that kind is named beforehand.
**Nobody builds on a provisional number: the task that consumes it starts by reading the fixing
task's report and amends the block in this file (one line, marked "As fixed by Task N") in its
own first commit.**

| Number or choice | Written here as | Fixed by | Consumed by |
|---|---|---|---|
| the vertex registers the position and the fog read | 14: `c8`, `c46`, `c47`, `c72..c75`, `c110`, `c111`, `c113..c117` [microcode, read by the plan's author; the spec's "thirteen" miscounts `c113..c117`] | Task 4 (`IMPOSTOR_SHADER["reg"]`), confirmed by Task 6 | Tasks 8, 9 |
| `sizeof(ImpostorRecord)` | 440 | Task 6's report (registers), Task 3's (the fetch constant's source) | Task 8 |
| the half4's component order and the fetch's endian (`kImpostorWordSource`, `kImpostorWordEndian`) | (1, 0, 3, 2), endian 2 [inferred from `vfetch_full r6.yxwz`] | Task 3 (open question 2), held by Task 6's `rows` | Tasks 6, 8 |
| where the vertex fetch constant is read (the device's shadow of fetch slot 31, or the builder's packet) | the shadow | Task 3 | Task 8 |
| the bounds of `MakeImpostorRecord`'s `quad` and `register` rules that the spec leaves to D1 and D2 (size; `c8`, `c46`, `c47`, `c110`, `c111`, `c113..c117`) | four times the largest magnitude in D1 and D2 | Task 6 (`impostor_check.py bounds`) | Task 8 |
| which bit of the pixel bool word is `b129` | bit 1 (`0x00000002`). **As fixed by Task 5**: bit 1 (`light_formulas.IMPOSTOR_B129`), from the SDK's bool layout (bit `N & 31` of register `0x4900 + (N >> 5)`: `interpreter.cpp`) and `ucode_eval`'s numbering; clear in the captured word `0x00000100`. The rows' `"pbool"` is the device's shadow, which FM section 12 ("The bool word") refutes as the GPU's value: Task 8 tests the word the GPU is given | Task 5 | Task 8 |
| the alpha test's reference at a `129F` draw | unknown; carried in the record and applied (`alpha > ref`) | Task 3 | Tasks 6, 9 |
| `sizeof(ClayImpostorConstants)`, the slots a draw takes | 336 bytes, 2 slots | Task 6 (which components the formula reads) | Tasks 9, 10 |
| the flat colour (`kImpostorFlatColour`) | none: a literal that Task 7 writes with its source file | Task 7 (its D1 step) | Task 10 |
| the format-3 field order (which five bits are red) | red in bits 0 to 4 after the 8-in-16 swap [inferred from the SDK's name `R5G5B5A1ToB5G5R5A1`] | Task 7 | Tasks 13, 14 |
| the level rule's alpha threshold, the content rule's two bounds | none | Task 13 (fitted to D1 and D2) | Task 14 |
| the share of impostor pixels with one sample of two (open question 8) | unknown | Task 6 | Task 15 (below 5% the controller may strike it) |

## Where this plan departs from the spec's letter, and why

1. *Thirteen registers* (spec Design 6): the microcode reads fourteen (`c113`, `c114`, `c115`,
   `c116`, `c117` are five).
2. *"The stored levels follow by the existing mip dump"* (Design 1): `DumpTextureMips`
   (`capture.cpp` line 1713) is called for a texture the renderer decodes and never for one kept
   by content (line 1844). Task 1 gives a format-3 picture a level file of its own.
3. *"The plan's first step reads that shader"* (Design 12, the decoder): the SDK's load shader
   for format 3 is in the tree as compiled bytecode alone
   (`thirdparty/rexglue-sdk/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_cs.h`).
   Task 7 disassembles it, and holds the field order to the capture as well.
4. *`fog_check.py` "already lists `129F`"* (Design 5): `light_formulas.FOG_VERTEX_SHADERS` lists
   it (line 845) as "read only"; `fog_check.VERTEX_CHECKED` (line 281) has four shaders and not
   this one. Task 5 checks `o2` and `o3` itself.
5. *The fog's tables* (Design 6, `fog-table`): `capture::FogBlock` carries no fetch constant; the
   frame's table addresses are in `FrameScene`'s fog source. The rule is therefore taken at
   `FrameBuilder::Finish`, where both are in hand, by a pure function (Task 8).
6. *Run COST's configuration `one`* (Design 3) is "a normal run", while logs 654 to 656, whose
   plan it names, were made with `--fable2_exposure_readback=on` (three seeded addresses). The
   plan runs COST without that switch (one against fourteen, as the bound was reasoned) and
   criterion 15's runs with it (as their reference logs).
7. *The alpha test* (colour control `0xAA00001C`: test on, function "greater") is named by the
   spec's `state` rule and not by its kept set (Design 10). The plan's kept set is
   `alpha > ref` and the first sample covered.

---
## Phase 12a: the oracle (no renderer change)

Order: Tasks 1 and 4 start together (Task 4 is Python alone and touches none of Task 1's files).
Task 2 is a run of the controller's and needs no task before it. Task 5 follows Task 4 (the same
files). Task 7 follows Task 1 (both edit `texture_decode.h` and its test). Task 3 needs Tasks 1
and 4. Task 6 needs Tasks 3, 4 and 5; Task 7's last step needs Task 3.

### Task 1: Discovery additions (capture only)

**Files:** `src/native/capture/discovery_format.h`, `src/native/capture/capture.cpp`,
`src/native/render/texture_decode.h`, `tests/native/test_discovery_format.cpp`,
`tests/native/test_texture_decode.cpp`, `tests/native/test_terrain_patch.cpp`,
`tools/xdk_sigmatch/check_discovery.py`, `tests/test_check_discovery.py`,
`docs/native-renderer/frame-map.md` (new section 19 "Distant trees: the far woods' impostors",
before "Pending"; subsection "Discovery").

**Consumes:** `WriteVertsRow` (`capture.cpp` line 2546), `WriteIndxRow` (2625: the members
`"vfull"`, `"vs_lit"`, `"fetches"`, `"bank"` at 2683 to 2705 are the pattern), `PendingIndexed`
and `FlushPendingIndexed` (2599, 2718), `g_indx_rows` (2609), `g_capture_serial`,
`AppendVertexRegisters` (2226), `AppendFetches` (1970), `LiteralsJson` (1122), `ReadVsUcode`,
`DecodeVertexFetches`, `ReadTextureFetch` (3992), `RegisterWords` (`discovery_format.h` line
68), `xdk::DeviceRegShadowOffset`, `xdk::kDeviceBeginVerticesWriteOffset` /
`StorageOffset` / `DwordsOffset` (`xdk_layout.h` 334 to 336), `DumpTexture` (1761),
`DumpTextureMips` (1713), `DumpFormatInfo` (`texture_decode.h` 154), `DumpKeyedByContent` (235),
`OnXdkCall`'s discovery block (5828 to 5839).

**Produces:**

```cpp
// discovery_format.h
// BeginVertices writes fetch slot 31 (vertex fetch constants 93 to 95) for its storage as a
// type-0 packet of six dwords with this header (xdk_layout.h, "BeginVertices").
inline constexpr uint32_t kVertsFetchHeader = 0x000548BAu;
inline constexpr uint32_t kVertsCmdTailDwords = 96;      // the command dwords a "verts" row looks back over
inline constexpr uint32_t kMaxVertsDataBytes = 64u << 10;
// The six dwords that follow the LAST `header` among `dwords` big-endian command dwords at `be`;
// false (out untouched) when there is none with six dwords after it.
inline bool FindFetchPacket(const uint8_t* be, size_t dwords, uint32_t header, uint32_t out[6]);
// Bytes of a "vertsdata" row: the storage whole when it is at most `cap` bytes, else 0. 64-bit arithmetic.
inline uint32_t VertsDataBytes(uint32_t storage_dwords, uint32_t cap);

// texture_decode.h
// DumpFormatInfo: format 3 (k_1_5_5_5) -> {1, 2, 1}. DumpKeyedByContent: a 2D texture of format 3 -> true.
// A 2D fetch constant of any format DumpFormatInfo has, as DecodeTextureFetch reads one; `out->format`
// is TexFormatFromXenos's (kUnsupported for a format the renderer does not decode). False as
// DecodeTextureFetch is for a constant that is no 2D texture, has no base or a pitch below its width.
inline bool DumpFetch(const uint32_t fc[6], TextureFetch* out, TexFormatInfo* fi);
```

Rows (inside the discovery gate; nothing a record or the pass reads changes):
- A `"verts"` row **with `in_scene` true** gains `"n"` (from `g_indx_rows`, so that the numbers
  of `"indx"` and `"verts"` rows never collide), `"vfull"`, `"vs_lit"`, `"fetches"`, `"bank"` (the
  whole vertex bank, 1,024 floats, as an `"indx"` row has them) and
  `"vfetch":{"shadow":[six dwords of fetch slot 31 from the device],"packet":[six dwords] | null}`
  (`packet`: `FindFetchPacket` over the `kVertsCmdTailDwords` dwords that end at the builder's
  own write pointer; null when they cannot be read or hold no header). Its `"tf"` already holds
  the vertex shader's slots (`AppendTextureFetches` adds them: `"vs_tex_slots"`); the step below
  says so with a row of a capture in hand, or adds slots 16 and 17 if it does not.
- A `{"kind":"vertsdata","frame":F,"n":N,"storage":"0x...","want":B,"read":true,"bytes":"<hex>"}`
  row for each such `"verts"` row, written when the thread enters its next hooked draw
  (`PendingVerts`, beside `PendingIndexed`; `want` = `VertsDataBytes(storage_dwords,
  kMaxVertsDataBytes)`; `"read":false` and no `"bytes"` when the bytes cannot be read; no row
  when `want` is 0, the pointer is null or the capture is over).
- A 2D texture of format 3 is dumped whole and once per content, and with it its stored levels:
  `<BASE>_<IDENTITY16>_<XXH3>_mips.bin` and the texture row's `"mips"` member
  (`FormatMipsMember`), by `DumpTextureMips` given a `TexFormatInfo` and the content hash (today
  it takes `FormatInfo(t.format)` and is called for a decoded format alone, line 1844).
- A `"draw"` row outside the main scene whose surface pitch is 320 gains
  `"vp":{"<REG>":"0x<word>",...}` for the registers `0x210F` to `0x2114` (viewport scale and
  offset), `0x2080` to `0x2082` (window offset and scissor) and `0x200E`, `0x200F` (screen
  scissor), through `RegisterWords`: a register `DeviceRegShadowOffset` has no shadow for is left
  out, and the frame map names those.
- `check_discovery.py`: `check_verts_data(rows)` -> `(checked, problems)`.

- [ ] Write the tests first and see them fail. `tests/native/test_discovery_format.cpp` (after
      the file's last number, 79):

```cpp
  {
    // FindFetchPacket: the six dwords after the LAST header of the stretch (plan Task 1).
    uint8_t be[4 * 20] = {};
    auto put = [&](size_t i, uint32_t v) {
      be[4 * i] = uint8_t(v >> 24); be[4 * i + 1] = uint8_t(v >> 16); be[4 * i + 2] = uint8_t(v >> 8); be[4 * i + 3] = uint8_t(v);
    };
    put(1, kVertsFetchHeader);
    for (uint32_t k = 0; k < 6; ++k) put(2 + k, 0x1000u + k);
    uint32_t got[6] = {7, 7, 7, 7, 7, 7};
    if (!FindFetchPacket(be, 20, kVertsFetchHeader, got) || got[0] != 0x1000u || got[5] != 0x1005u) return 80;
    put(10, kVertsFetchHeader);                                   // a later header wins
    for (uint32_t k = 0; k < 6; ++k) put(11 + k, 0x2000u + k);
    if (!FindFetchPacket(be, 20, kVertsFetchHeader, got) || got[0] != 0x2000u || got[5] != 0x2005u) return 81;
    put(16, kVertsFetchHeader);                                   // a header with three dwords after it is no packet
    if (!FindFetchPacket(be, 20, kVertsFetchHeader, got) || got[0] != 0x2000u) return 82;
    uint32_t keep[6] = {9, 9, 9, 9, 9, 9};
    if (FindFetchPacket(be, 1, kVertsFetchHeader, keep) || keep[0] != 9) return 83;      // too short: out untouched
    if (FindFetchPacket(nullptr, 20, kVertsFetchHeader, keep) || FindFetchPacket(be, 0, kVertsFetchHeader, keep)) return 84;
    if (VertsDataBytes(1140, kMaxVertsDataBytes) != 4560) return 85;                     // the bridge's largest draw: 285 quads
    if (VertsDataBytes(16384, kMaxVertsDataBytes) != 65536 || VertsDataBytes(16385, kMaxVertsDataBytes) != 0) return 86;
    if (VertsDataBytes(0, kMaxVertsDataBytes) != 0 || VertsDataBytes(0xFFFFFFFFu, kMaxVertsDataBytes) != 0) return 87;   // (no 32-bit wrap)
  }
```

      `tests/native/test_texture_decode.cpp` (after the file's last number, 149):

```cpp
  // --- Format 3 (k_1_5_5_5): the impostors' pictures in a discovery dump (plan Task 1) ---
  {
    TexFormatInfo fi;
    if (!DumpFormatInfo(3, &fi) || fi.block != 1 || fi.bytes_per_block != 2 || fi.bpb_log2 != 1) return 150;
    if (TexFormatFromXenos(3) != TexFormat::kUnsupported) return 151;   // spec ruling 14: no record's material changes
    FcArgs a;
    a.format = 3; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 1; a.base = 0x12704000; a.mip = 0x12724000;
    a.mip_max = 3; a.packed = false; a.clamp_x = 6; a.clamp_y = 6;
    uint32_t p[6];
    MakeFc(a, p);
    if (BaseLevelDumpBytes(p) != 131072u) return 153;                   // whole: it was cut at 65,536 before
    if (!DumpKeyedByContent(p)) return 154;                             // its memory holds another picture each frame
    TextureFetch t;
    if (DecodeTextureFetch(p, &t) != FetchError::kFormat) return 155;   // the texture cache still refuses it
    TexFormatInfo dfi;
    if (!DumpFetch(p, &t, &dfi) || t.width != 256 || t.height != 256 || t.base_phys != 0x12704000u ||
        t.mip_phys != 0x12724000u || !t.tiled || t.endian != 1 || t.mip_max != 3 ||
        t.format != TexFormat::kUnsupported || dfi.bytes_per_block != 2) return 156;
    a.format = 4;                                                       // k_5_6_5: no layout, as before
    MakeFc(a, p);
    if (DumpFetch(p, &t, &dfi) || BaseLevelDumpBytes(p) != 0 || DumpKeyedByContent(p)) return 157;
    a.format = 3; a.dim = 0;                                            // a 1D constant: no 2D layout
    MakeFc(a, p);
    if (DumpFetch(p, &t, &dfi)) return 158;
  }
```

      `tests/native/test_terrain_patch.cpp` (return 260; the file's last is 254): a fetch
      constant that `DecodeNormalMap` accepts, with its format field made 3, is refused: the
      one product caller of `BaseLevelDumpBytes` (`terrain_patch.h` line 160) is not reached for
      the new format (Review Focus 4). `tests/test_check_discovery.py`:

```python
    def test_verts_rows_have_their_data(self):
        verts = {"kind": "verts", "frame": 1, "n": 7, "in_scene": True, "count": 8, "storage_dwords": 8,
                 "bank": [0.0] * 1024, "vfetch": {"shadow": ["0x00000000"] * 6, "packet": None}}
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
```

- [ ] Run them: `rtk clang++ -std=c++23 tests\native\test_discovery_format.cpp -o
      out\tests\runtime-unit-tests\discovery_format.exe` (expected: does not compile:
      `kVertsFetchHeader` undeclared); the same for `test_texture_decode.cpp` (expected:
      `DumpFetch` undeclared); `rtk python -m unittest tests.test_check_discovery -v` (expected:
      `AttributeError: ... check_verts_data`).
- [ ] Implement the pure functions; rerun the three: exit 0, and the Python test OK.
- [ ] Implement the rows in `capture.cpp`. `PendingVerts` is flushed inside the discovery block
      of `OnXdkCall` (line 5833, beside `FlushPendingIndexed`): a run without a capture executes
      nothing new. Native suite and Python suite green; `rtk .\build.cmd -release fable_2`.
- [ ] Frame map section 19, "Discovery": the members, the caps, what is not kept, which viewport
      registers have no shadow. Commit "Distant trees discovery: the builder's storage, the bank
      and the fetch constant of a verts row; format 3 whole with its levels".

*No run in this task: Task 3 makes its captures.*

### Task 2: The cost pair (run COST): the first run, and phase 12c's entry condition

**No code.** It needs no task before it and no new binary: all four runs on **one** binary, the
branch's head as built or Task 1's build, its SHA-256's first 16 digits recorded before and
after. **Files:** `out/native-evidence/distant-trees/cost_timing.py` (an evidence script, not
tracked: made from `out/native-evidence/foliage/t18b_timing.py`, whose line patterns it keeps),
`cost_timing.txt`, the ledger (the verdict as a ruling), frame map section 19 "The cost pair".

- [ ] NEEDS_RUN, by the controller, on a quiet machine, the window on the usual monitor, **each
      run alone**, one session, in this order (configuration `one`: today's seeded list, the
      hero's address, which is impostor 0's base; `fourteen`: the list given by hand, which
      wins over the seed):
      1. `cost_1_one`: TIME-CAP with `-GameArgs BASE,POST`
      2. `cost_2_fourteen`: TIME-CAP with `-GameArgs BASE,POST,FOURTEEN`
      3. `cost_3_fourteen`: the same as 2
      4. `cost_4_one`: the same as 1

      Copies wanted in `out/native-evidence/distant-trees/`: each run's driver output, log,
      five shots (`cost_<n>_<name>_shot_<s>s.png`) and capture (`.jsonl` with its `native_tex_`
      folder).
- [ ] Before anything is read: each run's five shots are 1942 wide (else the session is "no
      timing evidence" and is made again); the freeze signature; `seeded guest_vblank_pacing=false`
      in each log; in runs 1 and 4 the log's `seeded readback_resolve_force_addresses
      '0x12704000'` line, in runs 2 and 3 `readback_resolve_force_addresses was given by hand`.
- [ ] That the read-back happened: `rtk python out/native-evidence/distant-trees/impostor_tex.py
      <stamp>` on each capture, the sheet viewed: in each `fourteen` run all fourteen `tf0` dumps
      are trees on a clear ground; in each `one` run the first alone. A `fourteen` run with a
      picture that is noise or empty is not a `fourteen` run: the session is "not decidable".
      (The SDK's `Forced readback-resolve` lines cannot show it: the first 24 of a process are
      used up in the front end.)
- [ ] `cost_timing.py` prints, per run: the steady 150-frame native windows (the first native
      window, which holds the view switch's long frame, left out as in FM section 18), each
      window's fps, the mean frame in ms over them, the guest's swap median, the emulated view's
      windows, the capture median. **The bound, stated before the run (spec ruling 15), applied
      exactly:**
      - `M(one)` = the mean of the two `one` runs' means, `M(fourteen)` likewise.
      - If the two `one` runs' means differ by more than 1.0 ms: **not decidable**; the session
        is made once more; twice not decidable is the controller's to rule (ledger ruling 22:
        it then counts as "fails").
      - **Passes** iff no steady native window of either `fourteen` run is below 30.0 fps
        **and** `M(fourteen) - M(one) <= 2.0` ms. Otherwise **fails**.
- [ ] Criterion 16: `region_compare.py` on the emulated shots (36 s and 96 s) of the `fourteen`
      runs against the `one` runs': the hero's shirt, the dog and the four far regions
      (`docs/native-renderer/fog-regions.json`, the bridge) within the range the two runs of one
      configuration span. The shots viewed: the dog and the hero described honestly.
- [ ] The verdict (passes, fails, not decidable), every figure with its file, written to
      `cost_timing.txt`, to FM section 19 "The cost pair" and to the ledger as a ruling: "Phase
      12c is entered" or "Phase 12c is not entered: the sub-project closes at `flat`". Commit
      "Distant trees: the cost pair's verdict" (the frame map alone is tracked).

### Task 3: Discovery captures D1 to D4 and what they say

**Files:** `docs/native-renderer/frame-map.md` (section 19 "Discovery captures"),
`tools/xdk_sigmatch/impostor_check.py` and `tests/test_impostor_formulas.py` only for what a
capture shows to be wrong in Task 4's row reader. Evidence scripts in DT are not tracked.

**Consumes:** Task 1's build and `check_discovery.py`; Task 4's `impostor_check.quad_words`,
`words` and `places`; `DT/depth_gap.py`, `DT/census.py`, `DT/impostor_tex.py`;
`post_sun_layer.py layers`; `foliage_trace.py head`.

- [ ] NEEDS_RUN (Task 1's build; all `RUN UNCHECKED`; the impostors' switch does not exist yet):
      - `D1`: BRIDGE with `-GameArgs BASE,PAIR`
      - `D2`: THIRD with `-GameArgs BASE,PAIR`
      - `D3T`: TREES with `-GameArgs BASE,PAIR` (a candidate for D3)
      - `D4a`: SUNSIDE with `-GameArgs BASE,PAIR,SUN`
      - `D4b`: SUNHELD with `-GameArgs BASE,PAIR,SUN`

      Copies wanted in DT as `d1_*` ... `d4b_*`: the capture `.jsonl` with the names of its
      `native_geo_` and `native_tex_` folders, the stored scene `native_scene_<stamp>.f2ns`, the
      log, the driver output, the four shots.
- [ ] Each capture: `rtk python tools/xdk_sigmatch/check_discovery.py <capture> <log>` exits 0;
      the scene dump's frame equals the capture's first `"scene_frame"` (`foliage_trace.py
      head`); two `"frame"` rows. `DT/census.py` again: the `129F` rows of each frame (expected
      14 and 928 quads at the bridge, 14 and 1,076 at the third spot; another count is reported,
      not mended: open question 11).
- [ ] **Open question 2**: `impostor_check.py words <capture>`: for each endian (0 to 3) and
      each order of the four halves, how many quads of every `129F` row decode to four finite
      values with a size in (0, 1000] and a place within 10,000 units of the frame's eye (pixel
      `c19`). **Exactly one (endian, order) must hold for every quad of D1 and D2**; it is
      written into the provisional table's line ("As fixed by Task 3") and into
      `light_formulas.IMPOSTOR_SHADER`. That the fetch reads the storage's first `8 * quads`
      bytes: the `"vfetch"` member's address against the row's `"storage"`, and what the second
      half of the storage holds (zeros, the same again, something else: said). Where the fetch
      constant is: whether `"shadow"` equals `"packet"` in every row (then the product reads the
      shadow), else which of them names the storage. None holds for every quad: stop, report;
      nothing after this task is built on the storage.
- [ ] **Open question 3**: the values of `c8`, `c46`, `c47`, `c72..c75`, `c110`, `c111`,
      `c113..c117` in every `129F` row (`"bank"`), their spread within a frame and between the
      views, and whether any of them lies in a constant-buffer range
      (`light_block.h`, `kBufferLoadRanges`, line 136: none does by the table as it stands;
      `"vfull"` against `"bank"` shows whether a load named one). The alpha reference
      (`"rs"`, register `210E`) and the eight state words of every `129F` row.
- [ ] **Open question 6**: `DT/impostor_tex.py` on D1 and D2 (now whole, with three levels):
      the sheets viewed and described; each level's file size against 32,768, 8,192 and 3,072
      bytes at the offsets the `"mips"` member names.
- [ ] **Open question 9, D3**: `impostor_check.py places <capture>`: the quads of each capture
      by their distance `d` from the eye (of the fetched place; the raise is Task 6's), and the
      count with `d` in [46, 58]. **D3 is the first of D1, D3T, D2 with at least three such
      quads** whose mesh twins (the rows with pixel `c15.w` below 1) are in the same frame; none:
      criterion 11 reads "not decidable" and a user check stands for it. No other view is
      invented here.
- [ ] **Open question 10, D4**: for D4a and D4b, `DT/depth_gap.py` (the gap mask) laid over the
      game's layer 1 (`post_sun_layer.py layers`): the gap pixels that lie on lit texels of the
      layer. **D4 is the one with more, if it has at least 500**; else criterion 12's second part
      reads "not decidable", said so now.
- [ ] `DT/depth_gap.py` on D1 and D2 against their stored scenes: the "before" of this binary
      (wind and coverage off, as these runs were made), with the third spot's gap as pixels,
      which the research could not make.
- [ ] Frame map "Discovery captures" with every figure's file; commit "Distant trees discovery
      runs: the storage's order, the registers, the pictures whole, the views".

### Task 4: The position formula on made inputs (no game; parallel with Task 1)

**Files:** `tools/xdk_sigmatch/light_formulas.py` (a new part "The far woods' impostors"),
`tools/xdk_sigmatch/impostor_check.py` (new), `tests/test_impostor_formulas.py` (new),
`docs/native-renderer/lighting-formulas.md` (part "The impostors").

**Consumes:** `ucode_eval.Machine(text, consts, sampler, regs=, bools=, vfetch=, loops=,
tex_size=, zero_product=)` and `ucode_eval.load(dump_dir, hash_, kind)`; `capture_replay.rows`,
`capture_replay.DUMPS`; `wind_check.py` as the model of form (`check_formula`, `trace`,
`truncated`: its `trace <hash> --instr N` prints the interpreter's registers after an
instruction); `out/shader_dump/shader_129F7F2793DA9084.ucode.vert` (81 instructions, numbered 10
to 90).

**Produces** (in `light_formulas.py`):

```python
IMPOSTOR_SHADER = {
    "vs": "129F7F2793DA9084", "ps": "4B1D3E6E8E58D3AC",
    "reg": (8, 46, 47, 72, 73, 74, 75, 110, 111, 113, 114, 115, 116, 117),   # vertex registers, in the record's order
    "ps_reg": (12, 19, 20, 29, 31),                                           # pixel registers, in the record's order
    "vs_lit": {254: (0.5, 0.25, 2.0, 0.0), 255: (0.0, 1.0, -1.0, 4.0)},       # FM line 2655
    "ps_lit": {255: (0.0, 1.0, 0.1, 0.5)},
    "word_source": (1, 0, 3, 2), "word_endian": 2,                            # PROVISIONAL: Task 3
}
def impostor_corner(i):
    """(quad, k) of vertex i: quad = floor((i + 0.5) * 0.25), k = i - 4 * quad (instr 10 to 12, 19 to 21)."""
def impostor_position(k, word, c):
    """k: the corner, 0 to 3. word: (x, y, z, s), the fetch's result as register r6 holds it after instr 12.
    c: {register: [4 floats]} for IMPOSTOR_SHADER["reg"]. Returns {"pos": oPos (4), "place": o0 (3), "uv": o1 (2)}.
    A value the microcode makes no number is no number here (the zero under the log, the zero under the
    reciprocal: what ucode_eval returns)."""
```

`impostor_check.py`: `quad_words(verts_row, data_row, source=None, endian=None)` -> a list of
`(x, y, z, s)` (the row's `"vertsdata"` bytes, eight a quad, by the shader's order and endian or
the ones given); `check_position(trials=2000, seed=1, mutation=None, dumps=DUMPS)` ->
`Result(trials, failed, corners, above, below, powered, worst, first_failure)` (`corners`: the
trials at each of the four corners; `above` / `below`: by the height comparison of instr 31;
`powered`: the trials in which both `log` arguments are positive numbers; the bound is criterion
1's: `|value - formula| <= 1e-4 * max(1, |value|)` in every component of `oPos`, `o0` and `o1`);
`POSITION_MUTATIONS = ("rows", "components", "sign", "corners", "exponent", "raise", "matrix")`
(`c116` for `c115`; the word's first two components exchanged; the size's sign; two corners
exchanged; `c114.y` changed by 1%; the raise left out; two rows of `c72..c75` exchanged);
subcommands `formulas`, `trace --instr N`, `words <capture>`, `places <capture>`, each exiting 1
on a failure.

Made inputs: the word's place within 2,000 units, the size in (0.5, 40]; `c47` (the eye) within
2,000 units and so that the raised height is above it in about half of the trials; `c8.z` in
[0, 2]; `c46` a unit horizontal vector times a scale in [0.25, 2]; `c114.x` in [0.1, 4], `c114.y`
in [0.25, 4]; `c115` to `c117` rows of a matrix with entries in [-2, 2] and an offset in
[-50, 50], drawn so that both `log` arguments are positive in at least three quarters of the
trials; `c72..c75` a view-projection of the captures' shape (entries within 200); `c113` =
(1, 0, 0, 1) in half of the trials and random in [-2, 2] otherwise. The ranges are constants at
the file's head with this paragraph beside them.

- [ ] Tests first (`tests/test_impostor_formulas.py`), see them fail
      (`ImportError` / `AttributeError`: the names do not exist):

```python
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import impostor_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
ROWS = {72: [1.0, 0.0, 0.0, 0.0], 73: [0.0, 1.0, 0.0, 0.0], 74: [0.0, 0.0, 1.0, 0.0], 75: [0.0, 0.0, 0.0, 1.0]}


def consts(over=None):
    c = {r: [0.0, 0.0, 0.0, 0.0] for r in lf.IMPOSTOR_SHADER["reg"]}
    c.update(ROWS)
    c[8] = [0.0, 0.0, 0.5, 0.0]
    c[46] = [0.6, 0.8, 1.0, 0.0]
    c[47] = [10.0, 20.0, 5.0, 1.0]
    c[113] = [1.0, 0.0, 0.0, 1.0]
    c[114] = [1.0, 1.0, 0.0, 0.0]
    c[115] = [1.0, 0.0, 0.0, 3.0]
    c[116] = [0.0, 1.0, 0.0, 4.0]
    c[117] = [0.0, 0.0, 1.0, 5.0]
    c.update(over or {})
    return c


class Corner(unittest.TestCase):
    def test_four_vertices_share_a_quad(self):
        for i in range(48):
            self.assertEqual(lf.impostor_corner(i), (i // 4, i % 4))


class Position(unittest.TestCase):
    WORD = (100.0, 200.0, 30.0, 8.0)

    def test_the_place_is_raised_and_the_same_at_four_corners(self):       # instr 17, 29, 88
        for k in range(4):
            place = lf.impostor_position(k, self.WORD, consts())["place"]
            self.assertEqual(tuple(place), (100.0, 200.0, 30.0 + 8.0 * 0.5))

    def test_the_four_corners_take_the_four_picture_corners(self):         # instr 23 to 25, 33, 43, 68
        uv = [tuple(lf.impostor_position(k, self.WORD, consts())["uv"]) for k in range(4)]
        self.assertEqual(uv, [(0.0, 1.0), (0.0, 0.0), (1.0, 0.0), (1.0, 1.0)])
        scaled = lf.impostor_position(2, self.WORD, consts({113: [0.5, 0.25, 0.125, 2.0]}))["uv"]
        self.assertEqual(tuple(scaled), (1.0 * 0.5 + 0.25, 0.0 * 2.0 + 0.125))

    def test_w_is_one_under_unit_rows_and_the_rows_are_dotted_in_order(self):   # instr 71 to 76
        base = lf.impostor_position(1, self.WORD, consts())["pos"]
        self.assertEqual(base[3], 1.0)
        self.assertTrue(all(math.isfinite(v) for v in base))
        doubled = lf.impostor_position(1, self.WORD, consts({73: [0.0, 2.0, 0.0, 0.0]}))["pos"]
        self.assertEqual(doubled[1], 2.0 * base[1])
        self.assertEqual((doubled[0], doubled[2], doubled[3]), (base[0], base[2], base[3]))

    def test_the_four_corners_are_four_places(self):
        pos = {tuple(lf.impostor_position(k, self.WORD, consts())["pos"]) for k in range(4)}
        self.assertEqual(len(pos), 4)

    def test_no_number_in_is_no_number_out(self):
        pos = lf.impostor_position(0, (100.0, 200.0, 30.0, math.nan), consts())["pos"]
        self.assertTrue(any(math.isnan(v) for v in pos))

    def test_the_register_list_is_what_the_dump_reads(self):
        self.assertEqual(len(lf.IMPOSTOR_SHADER["reg"]), 14)
        self.assertEqual(lf.IMPOSTOR_SHADER["reg"], tuple(sorted(lf.IMPOSTOR_SHADER["reg"])))


class Words(unittest.TestCase):
    def test_eight_bytes_a_quad_by_the_given_order_and_endian(self):
        halves = bytes.fromhex("3C00" "4000" "4200" "4400")          # 1, 2, 3, 4 as big-endian halves
        verts = {"count": 8, "storage_dwords": 8}
        data = {"read": True, "bytes": (halves + halves).hex() + "00" * 16}
        self.assertEqual(impostor_check.quad_words(verts, data, source=(0, 1, 2, 3), endian=1),
                         [(1.0, 2.0, 3.0, 4.0), (1.0, 2.0, 3.0, 4.0)])
        self.assertEqual(impostor_check.quad_words(verts, data, source=(1, 0, 3, 2), endian=1)[0], (2.0, 1.0, 4.0, 3.0))
        with self.assertRaises(ValueError):
            impostor_check.quad_words(verts, {"read": False})
        with self.assertRaises(ValueError):
            impostor_check.quad_words({"count": 20, "storage_dwords": 20}, data)      # 5 quads want 40 bytes; the row has 32


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class AgainstMicrocode(unittest.TestCase):
    def test_the_position_on_made_inputs(self):
        r = impostor_check.check_position(trials=200, seed=11, dumps=DUMPS)
        self.assertEqual(r.failed, 0, r.first_failure)
        for corner in r.corners:
            self.assertGreaterEqual(corner, r.trials // 5)          # a check on one corner proves a quarter
        self.assertGreaterEqual(r.above, r.trials // 4)             # both arrangements of instr 53 to 57
        self.assertGreaterEqual(r.below, r.trials // 4)
        self.assertGreaterEqual(r.powered, r.trials // 2)           # the log / exp block on numbers

    def test_mutations_fail(self):
        for name in impostor_check.POSITION_MUTATIONS:
            with self.subTest(mutation=name):
                r = impostor_check.check_position(trials=200, seed=11, mutation=name, dumps=DUMPS)
                self.assertGreater(r.failed, 0, f"{name} was not caught")
```

      The three tests "written from the reading" (the raise, the picture corners, `w` = 1)
      state what the plan's author read in the dump and did not run. **Where the interpreter disagrees with one of them, the
      interpreter is right**: the test is corrected in the same commit and the report names the
      reading that was wrong.
- [ ] Transcribe instructions 10 to 76 and 86 to 88 register by register into Python first, the
      interpreter the oracle at each step (`impostor_check.py trace --instr N`), co-issued scalar
      operations and the previous-scalar register (`muls_prev`, `adds_prev`) included; then fold
      into `impostor_position`. The block the research did not read (13, 14, 28 to 57) is
      written out term by term in `lighting-formulas.md` as it is transcribed, with what each
      register holds.
- [ ] `rtk python tools/xdk_sigmatch/impostor_check.py formulas`: `position 2000 / 2000`, the
      four corners' counts, above and below, powered; every mutation fails. Expected output's
      last line: `all agree`. Output to `out/native-evidence/distant-trees/t4_formulas.txt`.
- [ ] **If the form does not close** on made inputs: `lighting-formulas.md` says which
      instructions disagree; the task reports DONE_WITH_CONCERNS; Tasks 9 to 16 are not started
      (spec ruling 18).
- [ ] Commit "Impostor formulas: the position of 129F against its microcode".

### Task 5: The pixel formula and the fog (no game)

**Files:** `light_formulas.py`, `impostor_check.py`, `tests/test_impostor_formulas.py`,
`lighting-formulas.md`.

**Consumes:** `lf.vertex_fog(P, eye, c8x, c110, c111, tf16, tf17)` (line 638), `lf.fog_coords`,
`lf.covered_samples(offsets, x, y, alpha)` (line 1758), `fog_check.check_vertex` and `_smooth`
as the model of the made tables; `out/shader_dump/shader_4B1D3E6E8E58D3AC.ucode.frag`
(instructions 7 to 15 and 58 to 60; 16 to 57 lie under `b129`).

**Produces:**

```python
def impostor_pixel(texel, d, c12, c29, c31x):
    """texel: tf0's four values as fetched (rgb, alpha). d: |c19 - o0|. Returns (colour (3), alpha):
       colour = texel.rgb * c31x * (1 + sat(0.1 * (d - c12.y)) * (c12.w - 1));  alpha = texel.a * sat((d - c29.x) * c29.y)"""
def impostor_out(colour, o2, o3, c20w):
    """oC0.rgb = (colour * o2 + o3) * c20w (instr 58, 59)."""
def impostor_fog(place, eye, c110, c111, tf16, tf17):
    """(o2, o3) of 129F, instr 70 to 85: vertex_fog with no c8.x factor. Before the start ((1, 1, 1), (0, 0, 0))."""
IMPOSTOR_B129 = 0x00000002      # PROVISIONAL: the pixel bool word's bit that is b129; this task fixes it
```

`impostor_check.check_pixel(trials=2000, seed=1, mutation=None, dumps=DUMPS)` (the whole pixel
shader with `b129` clear against `impostor_out(impostor_pixel(...))`, within 1e-5 of
`max(1, |value|)`; a `fogged` count of the trials beyond the start) and
`check_fog(trials=2000, ...)` (the vertex shader's `o2`, `o3` against `impostor_fog`, the tables
random as `fog_check` makes them); `PIXEL_MUTATIONS = ("start", "tenth", "exposure", "sign",
"picture", "one_minus", "c8x")` (`c12.y` for `c29.x`; the literal 0.1 changed by 1%; `c31.x` left
out; the light factor's sign; the alpha without the picture's; the fog's `(1 - o2)` left out; a
`c8.x` factor put in); subcommand `pixel`.

- [ ] First: `fog_check.VERTEX_CHECKED` (line 281) does not hold `129F`, and
      `FOG_VERTEX_SHADERS` (line 839) calls it "read only": this task's `check_fog` is the first
      machine check of its fog. Say so in `lighting-formulas.md`, and correct the comment at
      `light_formulas.py` line 835 ("75 to 85" is 70 to 85).
- [ ] Tests first, appended to `tests/test_impostor_formulas.py`; see them fail:

```python
C12, C29, C31X = (20.0, 35.0, 2.0, 0.46779), (48.0, 0.125, 998.4, 25.6), 0.366827     # the bridge, R 3.2
TEXEL = (0.5, 0.25, 0.125, 1.0)


class Pixel(unittest.TestCase):
    def test_the_formula_written_out(self):
        colour, alpha = lf.impostor_pixel(TEXEL, 40.0, C12, C29, C31X)
        light = 1.0 + 0.5 * (0.46779 - 1.0)                 # sat(0.1 * (40 - 35)) = 0.5
        for got, t in zip(colour, TEXEL):
            self.assertAlmostEqual(got, t * C31X * light, delta=1e-7)
        self.assertEqual(alpha, 0.0)                         # 40 units: nearer than c29.x

    def test_the_light_is_one_near_and_the_mean_term_far(self):
        near = lf.impostor_pixel(TEXEL, 30.0, C12, C29, C31X)[0]
        self.assertAlmostEqual(near[0], 0.5 * C31X, delta=1e-7)
        for d in (45.0, 300.0):
            self.assertAlmostEqual(lf.impostor_pixel(TEXEL, d, C12, C29, C31X)[0][0], 0.5 * C31X * 0.46779, delta=1e-7)

    def test_the_dissolve_is_three_steps_a_tree(self):       # spec, "Read for this design": no dither under offsets AA
        for d, want in ((49.9, 0), (50.0, 1), (53.9, 1), (54.0, 2), (200.0, 2)):
            alpha = lf.impostor_pixel((1.0, 1.0, 1.0, 1.0), d, C12, C29, C31X)[1]
            for x, y in ((0, 0), (1, 0), (0, 1), (3, 5)):
                self.assertEqual(lf.covered_samples(0xAA, x, y, alpha), want)

    def test_the_alpha_is_the_pictures_times_the_dissolve(self):
        self.assertAlmostEqual(lf.impostor_pixel((1.0, 1.0, 1.0, 0.5), 52.0, C12, C29, C31X)[1], 0.25)
        self.assertEqual(lf.impostor_pixel((1.0, 1.0, 1.0, 0.0), 500.0, C12, C29, C31X)[1], 0.0)

    def test_the_output(self):
        out = lf.impostor_out((0.25, 0.5, 1.0), (0.5, 0.5, 0.5), (0.125, 0.0, 0.25), 8.0)
        self.assertEqual(tuple(out), (2.0, 2.0, 6.0))


class Fog(unittest.TestCase):
    def tables(self):
        return (lambda s: [0.5, 0.25, 0.125, 0.0]), (lambda a, t: [0.2, 0.4, 0.8, 0.0])

    def test_before_the_start_nothing(self):
        tf16, tf17 = self.tables()
        got = lf.impostor_fog((0.0, 0.0, 5.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.1, 0.01), tf16, tf17)
        self.assertEqual(got, ([1.0, 1.0, 1.0], [0.0, 0.0, 0.0]))

    def test_it_is_the_trees_form_without_the_scale(self):
        tf16, tf17 = self.tables()
        args = ((300.0, 40.0, 5.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (10.0, 0.0, 0.1, 0.01), tf16, tf17)
        o2, o3 = lf.impostor_fog(*args)
        self.assertEqual((o2, o3), lf.vertex_fog(args[0], args[1], 1.0, *args[2:]))
        self.assertEqual(o2, [0.5, 0.25, 0.125])
        self.assertEqual(o3, [(1.0 - 0.5) * 0.2, (1.0 - 0.25) * 0.4, (1.0 - 0.125) * 0.8])


@unittest.skipUnless(DUMPS.is_dir(), NO_DUMPS)
class PixelAgainstMicrocode(unittest.TestCase):
    def test_pixel_and_fog_on_made_inputs(self):
        for check in (impostor_check.check_pixel, impostor_check.check_fog):
            r = check(trials=200, seed=11, dumps=DUMPS)
            self.assertEqual(r.failed, 0, r.first_failure)
            self.assertGreaterEqual(r.fogged, r.trials // 2)        # a fog check before the start proves nothing

    def test_pixel_mutations_fail(self):
        for name in impostor_check.PIXEL_MUTATIONS:
            with self.subTest(mutation=name):
                check = impostor_check.check_fog if name in ("one_minus", "c8x") else impostor_check.check_pixel
                self.assertGreater(check(trials=200, seed=11, mutation=name, dumps=DUMPS).failed, 0, f"{name} was not caught")
```

- [ ] Implement; `rtk python tools/xdk_sigmatch/impostor_check.py pixel`: `pixel 2000 / 2000`,
      `fog 2000 / 2000`, every mutation fails, `all agree`; and once more with each frame's
      captured constants (`c12`, `c29`, `c31.x` of `DT/impostor_consts.txt`'s three views).
      Output to `t5_pixel.txt`.
- [ ] `b129`: which bit of the pixel bool word the shader's `cjmp !b129` reads, from
      `ucode_eval`'s own bool numbering and the SDK's bool-constant layout (named in the
      report), against the captured word `0x00000100` (clear in every capture: FM line 4268).
      `IMPOSTOR_B129` is set and the provisional table's line marked "As fixed by Task 5". If
      the captured word has that bit set, stop: the ground-fog block would be live.
- [ ] Document; commit "Impostor formulas: the pixel shader 4B1D and the per-tree fog".

### Task 6: The formulas against the captures; the replay; pins and the table

**Files:** `impostor_check.py`, `tools/xdk_sigmatch/foliage_replay.py` (subcommand `impostors`),
`tools/xdk_sigmatch/impostor_pins.py` (new, as `wind_pins.py`),
`tools/xdk_sigmatch/gen_impostor_table.py` (new), `src/native/capture/impostor_table.inc`
(generated, committed), `tests/test_impostor_formulas.py`, `tests/test_foliage_replay.py`,
`tests/test_gen_impostor_table.py` (new), `lighting-formulas.md`, frame map "The formulas against
the captures", this plan (the provisional table's lines).

**Consumes:** D1, D2 (D3, D4 where they exist); Tasks 4 and 5; `foliage_replay.fragments`,
`raster`, `fit_place`, `round_place`, `sample_place`, `load_frame`, `Frame`, `agreement`,
`level_plane`, `DEPTH_STEP`, `FIXED_TOLERANCE`, `SHARE_MIN`, `JUDGED_MIN`;
`impostor_pictures.decode_1555` (Task 7; until it lands, the levels' alpha bit read directly:
bit 15 of the swapped word); `wind_pins.lines` and `inc_lines` as the model.

**Produces:**
- `impostor_check.py rows <capture>`: every quad and corner of every `129F` row, the row's own
  `"bank"` and `"vertsdata"` through the interpreter and through `impostor_position`, criterion
  1's bound; mutations as in Task 4, each failing on the capture; `identities <capture>` (open
  questions 5 and 7: `c72..c75` against the scene's view-projection `c0..c3` of a main-scene
  row, vertex `c47` against pixel `c19`, the vertex slots 16 and 17 against the frame's fog
  tables `tf5` and `tf4` by base address and content, `c110` / `c111` against pixel `c66` /
  `c67`, `c31.x` times the rendering rows' `c20.w` against 1); `bounds <capture>...` (the
  smallest and largest of each register component and of the size over the captures given, and
  four times the largest magnitude).
- `foliage_replay.py impostors <capture> <frame> [--scale S] [--no-raise] [--band LO HI]`:
  criterion 3. The quads through `impostor_position`, two triangles a quad (corners 0 1 2 and
  0 2 3), rasterised at the sampling place fitted on the capture's own unmoving draws
  (`sample_place`), each fragment's picture alpha read at the level the fetch takes (ruling 19:
  the level from the picture's texels a pixel along each screen axis; the base-level figure
  printed beside), kept where `alpha > ref` and `covered_samples(0xAA, x, y, alpha) >= 1`,
  against the game's depth copy within `FIXED_TOLERANCE`. Printed: judged pixels, the share
  within, the share of kept pixels with one sample of two (open question 8), the same per draw.
- `impostor_pins.py`: `lines()` with the sections `// impostor position pins` (captured quads
  and made ones: the word, the fourteen registers, the corner, then `oPos`, `o0`, `o1` as hex
  float bits), `// impostor pixel pins`, `// impostor fog pins`; `inc_lines()` for
  `tests/native/impostor_pins.inc` (Task 9 commits the file).
- `gen_impostor_table.py` -> `src/native/capture/impostor_table.inc`, X-macro lines:
  `FABLE2_IMPOSTOR_SHADERS(vs, ps)`, `FABLE2_IMPOSTOR_VS_REGISTER(reg)` (in the record's order),
  `FABLE2_IMPOSTOR_PS_REGISTER(reg)`, `FABLE2_IMPOSTOR_LITERAL(bank, reg, bits_x, bits_y, bits_z,
  bits_w)`, `FABLE2_IMPOSTOR_WORD(component, source_half)`, `FABLE2_IMPOSTOR_ENDIAN(e)`,
  `FABLE2_IMPOSTOR_BOUND(bank, reg, component, lo_bits, hi_bits)`, `FABLE2_IMPOSTOR_SIZE_MAX(bits)`,
  `FABLE2_IMPOSTOR_B129(mask)`; `--check` exits 1 when the committed file differs.

- [ ] Tests first: `tests/test_gen_impostor_table.py` (a made shader entry gives every line
      above once, registers ascending; a bound with `lo > hi`, a register named twice and a
      word source outside 0 to 3 are refused; the committed file equals the generator's output);
      `tests/test_foliage_replay.py` (a made quad of 8 x 8 pixels with a made 4 x 4 picture whose
      left half has alpha 1 keeps the left half's pixels and none of the right; at `d` = 52 a
      picture alpha of 1 gives one sample of two on every kept pixel; the level picked for a
      quad that covers 64 pixels of a 256-texel picture is 2); `tests/test_impostor_formulas.py`
      (`Result` of `rows` on a made capture of one row: 4 corners a quad counted; a made row
      whose `"vertsdata"` is unread is counted `unread` and fails the run).
- [ ] `rtk python tools/xdk_sigmatch/impostor_check.py rows <D1> <D2>` (and D3, D4 if they
      exist): every quad and corner agrees; each mutation fails. **Criterion 1's second half.**
      Any quad that disagrees: stop; ruling 18 applies; the report names the quads and the
      instruction the trace first differs at.
- [ ] `identities` on D1 and D2: each identity "holds in N of N rows" or its differences
      printed. **What does not hold is carried per draw as read**; for the fog that means the
      record's tables are not the frame's and the record is refused (`fog-table`, Task 8).
- [ ] `bounds` on D1, D2 (D3, D4): the table that `gen_impostor_table.py` takes; the spec's own
      bounds stand where it gives them (`c29.y` in (0, 4], `c29.x` and `c12.y` in [0, 4096],
      `c12.w` in [0, 4], `c31.x` in (0, 64], `c20.w` in (0, 64], the matrix rows within 1e7, a
      place within 1e6). Generate and commit `impostor_table.inc`.
- [ ] **Criterion 3**: `foliage_replay.py impostors` on D1 and D2, frame 1: the share (bound:
      at least 95% of at least 1,000 judged pixels); the two controls `--scale 1.1` and
      `--no-raise`, each of which must fall by at least 10 points or the measure is said not to
      discriminate. **Criterion 11's first part** if D3 exists: `--band 46 58`, and that no
      judged pixel belongs to a quad below 50 units. **Open question 8**: the share with one
      sample of two, at the fetch's level and at the base level; written into the provisional
      table.
- [ ] Mark every line of the provisional table this task fixes ("As fixed by Task 6: ..."):
      the register list (the registers and components `impostor_position` and `impostor_fog`
      read, from the interpreter's `used_consts`), `sizeof(ImpostorRecord)`,
      `sizeof(ClayImpostorConstants)`, the bounds.
- [ ] `impostor_pins.py > out/native-evidence/distant-trees/t6_pins.txt`; frame map; commit
      "Impostor formulas against the captures: rows, identities, bounds, the replay, the table".

### Task 7: The format-3 decoder (after Task 1; its last step after Task 3)

**Files:** `src/native/render/texture_decode.h`, `tests/native/test_texture_decode.cpp`,
`tools/xdk_sigmatch/impostor_pictures.py` (new), `tests/test_impostor_pictures.py` (new),
`src/native/render/clay_logic.h` (the flat colour's literal alone, in a new section "Native
impostors" that Task 9 fills), frame map "The pictures' format".

**Consumes:** `capture::TiledOffset2D` (`xenos_tiling.h`), `CopySwapped` (`texture_decode.h` line
245), `DecodeDxt3aLevel` (line 294) as the model; the SDK's host format table
(`thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp` lines 96 to 100:
`DXGI_FORMAT_B5G5R5A1_UNORM` through the load shader `kLoadShaderIndexR5G5B5A1ToB5G5R5A1`) and
that shader's bytecode
(`thirdparty/rexglue-sdk/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_r5g5b5a1_b5g5r5a1_cs.h`:
no source text is in the tree).

**Produces:**

```cpp
// texture_decode.h, beside DecodeDxt3aLevel. NOT entered in TexFormatFromXenos (spec ruling 14).
// k_1_5_5_5 (Xenos format 3) -> RGBA8, width * height * 4 bytes row-major. A texel is 16 bits at
// TiledOffset2D(x, y, pitch_texels, 1), or linear at y * ((pitch_texels + 31) & ~31) * 2 + x * 2; after the
// GPU's endian swap: alpha in bit 15, three 5-bit fields below it, each field v -> (v * 255 + 15) / 31.
// k1555LowFieldChannel: the output channel of bits 0 to 4 (0 red, 2 blue; the middle field is green).
// False, with out_rgba left empty, for a null source, an empty image or a texel outside src_size.
inline constexpr uint32_t k1555LowFieldChannel = 0;   // PROVISIONAL until this task's first step
inline bool Decode1555Level(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height,
                            uint32_t pitch_texels, bool tiled, uint32_t endian, std::vector<uint8_t>* out_rgba);
```

`impostor_pictures.py`: `decode_1555(data, width, height, pitch, tiled, endian, low_channel)`
(numpy; the Python transcription of the same rule), `picture(capture_dir, texture_row, level)`
(a dumped base or level as RGBA), `sheet <capture>` (the fourteen pictures and their levels as
one PNG), `mean <capture> <frame>` (the mean of the texels with the alpha bit set over the
frame's pictures, in stored units 0 to 1, red, green, blue), `compare <bin> <fc>` (against the
C++ test's `<bin>.cpp.rgba`).

- [ ] **The field order first.** Disassemble the load shader: an untracked scratch program
      `out/tests/disasm_1555.cpp` (includes the bytecode header, calls `D3DDisassemble`, prints
      the text), built as `rtk clang++ -std=c++23 out\tests\disasm_1555.cpp -ld3dcompiler -o
      out\tests\runtime-unit-tests\disasm_1555.exe`; its output to
      `out/native-evidence/distant-trees/t7_load_shader_disasm.txt`. Read the shifts and masks:
      which five bits of the swapped guest word become which bits of the host's `B5G5R5A1` word
      (blue in bits 0 to 4, green 5 to 9, red 10 to 14, alpha 15). Write the finding into the
      frame map with the disassembly's lines, set `k1555LowFieldChannel`, mark the provisional
      table. If the tool cannot be built (antivirus rule: stop that route, no retry): the order
      stays [inferred] from the shader's name, said so, and criterion 8's per-channel band is
      what holds it.
- [ ] Tests first (`tests/native/test_texture_decode.cpp`, after Task 1's numbers; the texel
      offsets are `TiledOffset2D`'s for a pitch of 256 two-byte texels: (1, 0) at 2, (0, 1) at
      16, (8, 0) at 64, (255, 255) at 130,942):

```cpp
  // --- Decode1555Level (plan Task 7): 16-bit texels, tiled, 8-in-16, alpha in bit 15 ---
  {
    std::vector<uint8_t> src(131072, 0);
    auto put = [&](uint32_t offset, uint16_t word) { src[offset] = uint8_t(word >> 8); src[offset + 1] = uint8_t(word); };   // guest order: high byte first
    put(0, 0x8000 | 31);                    // (0, 0): alpha, the low field full
    put(2, 31 << 5);                        // (1, 0): the middle field, no alpha
    put(16, 0x8000 | (31 << 10));           // (0, 1): the high field
    put(64, 0x8000 | (16 << 5) | 1);        // (8, 0): rounding: 16 -> 132, 1 -> 8
    put(130942, 0xFFFF);                    // (255, 255)
    std::vector<uint8_t> out;
    if (!Decode1555Level(src.data(), src.size(), 256, 256, 256, true, 1, &out) || out.size() != 256u * 256u * 4u) return 160;
    auto px = [&](uint32_t x, uint32_t y) { return &out[(size_t(y) * 256 + x) * 4]; };
    const uint32_t lo = k1555LowFieldChannel, hi = 2 - k1555LowFieldChannel;
    if (lo != 0 && lo != 2) return 161;
    if (px(0, 0)[lo] != 255 || px(0, 0)[1] != 0 || px(0, 0)[hi] != 0 || px(0, 0)[3] != 255) return 162;
    if (px(1, 0)[lo] != 0 || px(1, 0)[1] != 255 || px(1, 0)[hi] != 0 || px(1, 0)[3] != 0) return 163;
    if (px(0, 1)[lo] != 0 || px(0, 1)[1] != 0 || px(0, 1)[hi] != 255 || px(0, 1)[3] != 255) return 164;
    if (px(8, 0)[lo] != 8 || px(8, 0)[1] != 132 || px(8, 0)[hi] != 0) return 165;
    if (px(255, 255)[0] != 255 || px(255, 255)[1] != 255 || px(255, 255)[2] != 255 || px(255, 255)[3] != 255) return 166;
    if (px(2, 0)[0] != 0 || px(2, 0)[3] != 0) return 167;                               // an untouched texel
    if (Decode1555Level(src.data(), 130943, 256, 256, 256, true, 1, &out) || !out.empty()) return 168;   // one byte short: nothing
    if (Decode1555Level(nullptr, 131072, 256, 256, 256, true, 1, &out) || Decode1555Level(src.data(), src.size(), 0, 256, 256, true, 1, &out)) return 169;
    // linear, as stored (endian 0: the low byte first): texel (x, y) at y * 512 + 2 * x for a pitch of 256
    std::vector<uint8_t> lin(4 * 512, 0);
    lin[512 * 1 + 2 * 3] = 31;              // (3, 1): the low field
    lin[512 * 1 + 2 * 3 + 1] = 0x80;        // and alpha
    if (!Decode1555Level(lin.data(), lin.size(), 4, 4, 256, false, 0, &out) || out.size() != 64) return 170;
    if (out[(1 * 4 + 3) * 4 + lo] != 255 || out[(1 * 4 + 3) * 4 + 3] != 255 || out[0] != 0) return 171;
  }
  // FABLE2_1555_BIN + ".cpp.rgba" for tests/test_impostor_pictures.py's real-dump test (as FABLE2_TF17_BIN above).
```

      and the tool mode: with `FABLE2_1555_BIN` (a dumped base level) and `FABLE2_1555_FC` (its
      six dwords) set, decode it by the constant's own width, height, pitch, tiling and endian
      and write `<bin>.cpp.rgba`; unset: one `SKIPPED` line.
      `tests/test_impostor_pictures.py`: the same five texels through `decode_1555` give the
      same bytes; a short source raises `ValueError`; `mean` of a made pair of pictures (one
      covered texel of (31, 0, 0) and one of (0, 31, 0)) is (0.5, 0.5, 0.0) and counts 2 texels.
- [ ] Run: expected failures (`Decode1555Level` undeclared; `ModuleNotFoundError:
      impostor_pictures`); implement; rerun: exit 0 and OK. The upper halves that exist today
      (`DT`'s capture `074119`, 65,536 bytes each): `impostor_pictures.py sheet` decodes the
      half that is there and the sheet is viewed (trees on a clear ground, as
      `DT/impostor_sheet_20261011_074119.png`): the function runs on real bytes before D1.
- [ ] After Task 3: **criterion 4's second part**: each of D1's fourteen base dumps through the
      C++ tool mode and `impostor_pictures.py compare`: equal texel for texel. **The flat
      colour**: `impostor_pictures.py mean <D1> 1` -> `kImpostorFlatColour[3]` in `clay_logic.h`
      as a literal, with the command, the capture's stamp and the count of covered texels in
      its comment (spec ruling 6); mark the provisional table.
- [ ] Frame map; commit "Format 3: the impostor pictures' decoder, its Python twin, the flat
      colour's mean".

---
## Phase 12b: the capture and the draw in `flat`

Entry condition: Task 6's `rows` check agreed on every captured quad (spec ruling 18). If it did
not, Task 8 alone is built (the capture stands either way: ledger ruling 1) and the sub-project
ends there with the formula's report. Tasks 8 to 11 run in order; Task 9 may start beside Task 8
(it touches `clay_logic.h` and its test alone, Task 8 the capture and `frame_scene.h`), one
builder at a time.

### Task 8: The capture's side block of impostor records; the switch

**Files:** `src/native/capture/impostor_record.h` (new, pure),
`src/native/capture/impostor_table.inc` (Task 6's), `src/native/capture/capture.cpp` /
`capture.h`, `src/native/render/frame_scene.h`, `src/native/fable2_native_render.cpp` (the
switch; `SetImpostorsWanted` in `PollFrame`'s order of stores; the install line),
`tests/native/test_impostor_record.cpp` (new; two lines in the runner),
`tests/native/test_frame_scene.cpp`, `tests/native/test_draw_record.cpp` (the size's assert
restated), `tools/xdk_sigmatch/impostor_check.py` (`records`), `tests/test_impostor_formulas.py`,
README (the switch's row, the counters), frame map "Impostor records".

**Consumes:** `quad_record.h` as the pattern (`MakeQuadRecord`, `QuadInputs`,
`QuadRecordReasonName`); `PendingQuad`, `FlushPendingQuad`, `ObserveQuadDraw`,
`QuadRegistersAreBankOnly` (`capture.cpp` lines 4325, 4518, 4400, 4351); `RecordDraw` (4621: the
id chain of 4626 to 4649, the locked block of 4724 to 4741, the count of 4738);
`EndMainSceneFrame`'s latch (5144 to 5170); `OnXdkCall`'s pending test (5820); `ReadDevice`,
`ChooseVs`, `LookupVs`, `LookupPs`, `ReadLiteralRegisters` (1159), `ReadConstantRegisters`
(3004), `VertexBank` (932), `ReadPixelConstant` (1569), `ReadTextureFetch` (3992),
`ReadStateWords`, `ReadVirtual`, `ReadVirtualBe32`; `StateWords`, `DecodeRenderState`,
`Compare` (`render_state.h`); `HalfToFloat` (`position_decode.h` line 56);
`IsBufferLoadRegister`, `kVertexBank`, `kPixelBank` (`light_block.h` 121 to 142); `FogBlock`
(`fog_block.h` line 39) and the frame's fog source (`FrameBuilder::SetFog(block, source)`:
`source.base[0]` is `tf4`'s address, `[1]` `tf5`'s); `FrameBuilder::QuadsOn` / `QuadSeen` /
`AddQuad` / `Finish` (`frame_scene.h` 860 to 915) and `Add` (623: a record's `seq` is
`NextSeq()`); `AppendFrameQuads` (`capture.cpp` 5030) as the row's pattern; the wind's switch
(`CurrentWind`, `fable2_native_render.cpp` 405) and `PollFrame` (1086 to 1094).

**Interfaces (produce exactly; the lines marked PROVISIONAL are amended from the reports of
Tasks 3, 5 and 6 in this task's first commit, each amended line marked "As fixed by Task N"):**

```cpp
// impostor_record.h, namespace fable2::native::capture
enum class ImpostorMode : uint8_t { kOff, kFlat, kOn };
inline constexpr const char* kImpostorSwitchWords = "off, false, 0, flat, on, true, 1";
inline ImpostorMode ParseImpostorMode(std::string_view text, bool* recognized);   // anything else: kOff, *recognized false
inline const char* ImpostorModeName(ImpostorMode m);                               // "off", "flat", "on"

inline constexpr uint32_t kMaxImpostorRecords = 32, kMaxImpostorQuads = 4096;      // spec ruling 9
inline constexpr uint32_t kImpostorPrimitive = 13, kImpostorStride = 4;            // a quad list; BeginVertices' r6
inline constexpr uint32_t kImpostorVsRegisters = 14, kImpostorPsRegisters = 5;     // PROVISIONAL: 14 (Task 6)
// From impostor_table.inc (X-macro lines; a static assert holds the table well formed: registers ascending and
// below 248, no register named twice, every word source 0 to 3 once, a bound's low at most its high):
//   kImpostorVs, kImpostorPs (the hashes); kImpostorVsRegister[], kImpostorPsRegister[] (the registers, in
//   the record's order); kImpostorWordSource[4], kImpostorWordEndian; kImpostorSizeMax; kImpostorB129;
//   the literals and the bounds.
enum : uint32_t { kImpC8, kImpC46, kImpC47, kImpC72, kImpC73, kImpC74, kImpC75, kImpC110, kImpC111,
                  kImpC113, kImpC114, kImpC115, kImpC116, kImpC117 };              // PROVISIONAL with the count
enum : uint32_t { kImpP12, kImpP19, kImpP20, kImpP29, kImpP31 };
inline constexpr float kImpostorPlaceMax = 1e6f;                                    // a quad's place
inline constexpr uint32_t kImpostorFetch0 = 0x8200D802u;   // tf0's dword 0 in every captured row: a tiled 2D fetch, pitch 256, clamp 6 / 6, no sign field
inline constexpr uint32_t kImpostorSwizzle = 0;            // PROVISIONAL: tf0's dword 3 bits 1 to 12 in D1 (Task 3)

inline constexpr const char* kImpostorRecordReason = "impostor-record";
enum class ImpostorReason : uint8_t { kNone, kUnread, kShader, kLiteral, kStorage, kQuad, kRegister, kScale, kState,
                                      kFetch, kGroundFog, kFogTable, kOverLimit, kPending, kCount };
inline const char* ImpostorReasonName(ImpostorReason r);   // "none", "unread", "shader", "literal", "storage", "quad", "register",
                                                           // "scale", "state", "fetch", "ground-fog", "fog-table", "over-limit", "pending"

// One draw of the frame. An invalid record is all zero (ps_hash 0).
struct ImpostorRecord {
  uint64_t ps_hash, vs_hash;
  uint32_t place;                            // DrawRecord::seq its own call was counted under (FrameBuilder::NextSeq)
  uint32_t quads, first_quad;                // its words: FrameScene::impostor_words[4 * first_quad ...), 4 floats a quad
  uint32_t pbool;                            // the pixel bool word
  float vs[kImpostorVsRegisters][4];         // the table's vertex registers, in its order
  float ps[kImpostorPsRegisters][4];         // c12, c19, c20, c29, c31
  uint32_t fetch[6];                         // tf0
  uint32_t fog_fetch[2][6];                  // vertex tf16, tf17
  uint32_t vfetch[2];                        // the storage's vertex fetch constant
  uint32_t depth, blend, colour_control, colour_mask, surface_info;
  float alpha_ref;                           // RB_ALPHA_REF, in [0, 1]
};
static_assert(sizeof(ImpostorRecord) == 440, "ImpostorRecord grew or shrank: the documents state its size");   // PROVISIONAL: 16 + 16 + 224 + 80 + 24 + 48 + 8 + 20 + 4

// What was read at the call's return (capture.cpp fills it; every count and size is the arguments').
struct ImpostorCall {
  bool read = false;                         // the device, both shaders, every register, constant and state word were read
  uint64_t ps_hash = 0, vs_hash = 0;
  bool literals_match = false;               // both shader objects' own literal tables hold the table's literals, each once an object
  uint32_t prim = 0, count = 0, stride = 0;  // r4 & 0x3F, r5, r6
  uint32_t storage = 0, storage_dwords = 0;  // device + 0x3488, + 0x3490
  float vs[kImpostorVsRegisters][4] = {};
  float ps[kImpostorPsRegisters][4] = {};
  uint32_t pbool = 0, fetch[6] = {}, fog_fetch[2][6] = {}, vfetch[2] = {};
  StateWords state;
};
constexpr uint32_t ImpostorQuads(uint32_t count);          // count / 4 for a multiple of 4 in [4, 4 * kMaxImpostorQuads]; else 0
ImpostorReason CheckImpostorCall(const ImpostorCall& c);   // every rule but the words': kNone, or the first that fails
// The record of a call and its words (4 * ImpostorQuads(c.count) floats into `words`): CheckImpostorCall again,
// then `bytes` (the storage's first 8 * quads bytes, guest order) whole (`byte_count` exactly that: kUnread
// otherwise), each quad's four halves by kImpostorWordSource and kImpostorWordEndian, every value finite, a
// place within kImpostorPlaceMax, a size in (0, kImpostorSizeMax] (kQuad). No record (all zero) and `words`
// untouched on any failure. `place` and `first_quad` are 0: the caller sets the place (the call's seq), the
// frame the first quad. kImpostorWordSource[k]: which of the element's four halves, in the order the GPU
// reads them after render::CopySwapped by kImpostorWordEndian, is component k of (x, y, z, s).
ImpostorRecord MakeImpostorRecord(const ImpostorCall& c, const uint8_t* bytes, size_t byte_count, float* words, ImpostorReason* why);
// Whether a record's fog is the frame's: its slot 16 at the frame's tf5 and its slot 17 at the frame's tf4 by
// base address, c110.xyz bit-equal to the block's axis, c111.x, .z, .w bit-equal to the block's range.
bool ImpostorFogMatches(const ImpostorRecord& r, const FogBlock& block, const uint32_t table_base[2]);

// frame_scene.h
//   FrameScene: std::vector<capture::ImpostorRecord> impostors; std::vector<float> impostor_words;
//               capture::ImpostorMode impostor_mode = kOff; uint32_t impostor_seen = 0, impostor_taken = 0,
//               impostor_quads = 0; uint32_t impostor_refused[size_t(capture::ImpostorReason::kCount)] = {};
//   FrameBuilder: void ImpostorsOn(capture::ImpostorMode); void ImpostorSeen();
//                 void AddImpostor(const capture::ImpostorRecord&, const float* words, capture::ImpostorReason why);
//   std::string ImpostorLine(const FrameScene&);   // "off" | "seen S, taken T, quads Q | impostor-record {reason: n, ...}"
// capture.h:  void SetImpostorsWanted(ImpostorMode mode);
```

`CheckImpostorCall`'s rules, in this order (the first that fails names the reason): `read`
(`kUnread`); both hashes the table's (`kShader`); `literals_match` (`kLiteral`); primitive 13,
stride 4, `ImpostorQuads(count)` not 0, the storage's pointer not null, its dwords equal to
`count`, and the vertex fetch constant naming the storage with the table's endian (`kStorage`;
the address relation is the one Task 3 reports); every component of every register finite and
inside the table's bound (`kRegister`), but for `c20.w`, which is in (0, 64] (`kScale`);
`DecodeRenderState(state)` valid with the depth test and the depth write on, the function
`kGreaterEqual` under the reversed range, no blend, all four colour channels, the alpha test on
with the function `kGreater` and a reference in [0, 1], alpha-to-mask with offsets `0xAA` on a
two-sample surface (`kState`); `tf0` a 2D texture with dword 0 `kImpostorFetch0`, format 3,
endian 1, 256 x 256, the captured swizzle, highest level 3 and a base address (`kFetch`);
`pbool & kImpostorB129` clear (`kGroundFog`).

`FrameBuilder`: with the mode off every call does nothing and the scene says off.
`AddImpostor`: a reason other than `kNone` is counted (one that is no reason's name as
`kUnread`); a record that is not valid, has no quads or comes without words, offered as good,
is counted `kUnread`; a valid record past `kMaxImpostorRecords`, or one whose quads do not fit below
`kMaxImpostorQuads`, is counted `kOverLimit` and nothing of it is kept (**that draw whole**);
otherwise it is appended with its `first_quad` and its words. `Finish`: calls seen that gave
neither a record nor a refusal are counted `kPending`; then, in a frame whose fog block is
valid, every record that fails `ImpostorFogMatches` is taken out and counted `kFogTable` (a
frame without a valid fog block keeps them: the pass draws that whole frame unfogged, and the
impostors with it). **A draw is whole or not there; the frame's set is not all-or-none** (spec
ruling 9).

In `capture.cpp`: `g_impostors_wanted`, `g_impostors_frame` (latched after the wind's word at
5165, only in a frame with records; `g_builder.ImpostorsOn`). In `RecordDraw` the id chain gains
a last branch, `else if (id == kHook_D3DDevice_BeginVertices && g_impostors_frame...)`: a draw
that makes a record never reaches it. There: the pixel shader's hash first (`ChoosePs`,
`LookupPs`; any other shader, the two mask rectangles among them, costs those reads and
nothing more), then `ImpostorCall` whole, before the scene's mutex is taken. In the locked
block, after `g_builder.Add` (whose record, skipped `unsupported-prim` and counted at 4738 as
today, gave the `seq`): `ImpostorSeen()`, then a refusal at once (`CheckImpostorCall`) or the
call left pending with its `seq` and its frame. `constinit thread_local uint8_t t_pending_draws`
(bit 0: a quad, bit 1: an impostor) replaces `t_pending_quad.valid` as the word `OnXdkCall`
tests at 5820, so that a draw's entry executes nothing new; `FlushPendingImpostor` reads
`8 * quads` bytes from the kept pointer, whole or not at all, copies them into a thread-local
buffer (allocated on the first use: never with the switch off), makes the record, and hands it
to the builder under the scene's mutex after the frame's recheck; a record of a frame that
ended, and a call still pending when the next comes, are dropped and counted
(`g_impostor_dropped`). `ImpostorRegistersAreBankOnly()` with its static assert, as the quads'.
The frame row: `"impostors":"off"`, or `{"mode","seen","taken","quads","reason":{name: n},
"records":[{"place","ps","vs","quads","first","pbool","vs_regs","ps_regs","fetch","fog_fetch",
"vfetch","depth","blend","colour_control","mask","surface","alpha_ref","words"}]}` (registers,
the reference and the words as the floats' bits, eight hex digits each). Log:
`[native] impostors: off|flat|on (fable2_native_impostors)` at install, always (criterion 13
names the off line); with the mode not off, every 300 frames `[native] capture: frame N
impostors: seen S, taken T, quads Q | impostor-record in F of W frames {reason: n} | dropped D`.
Until phase 12c exists, `on` captures as `flat` does (spec Design 3, ruling 22) and the install
line says `on (no pictures in this build: nothing is drawn)`.

- [ ] Tests first. `tests/native/test_impostor_record.cpp` (new), see it fail (the header does
      not exist):

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/impostor_record.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
using namespace fable2::native::capture;

static uint16_t Half(float f) {   // exact for the small values used here
  uint32_t b;
  std::memcpy(&b, &f, 4);
  const int e = int((b >> 23) & 0xFF) - 127 + 15;
  if (f == 0.0f) return 0;
  return uint16_t(((b >> 16) & 0x8000) | (e << 10) | ((b >> 13) & 0x3FF));
}
// Quad `q`'s (x, y, z, s) into guest bytes, by the table's own order and endian (so that the test holds whatever Task 3 fixed).
static void PutQuad(std::vector<uint8_t>& bytes, uint32_t q, const float v[4]) {
  uint8_t host[8];   // what the GPU reads after its endian swap: four little-endian halves
  for (int k = 0; k < 4; ++k) {
    const uint16_t h = Half(v[k]);
    host[2 * kImpostorWordSource[k]] = uint8_t(h);
    host[2 * kImpostorWordSource[k] + 1] = uint8_t(h >> 8);
  }
  // render::CopySwapped's swaps (8-in-16, 8-in-32, 16-in-32), each its own inverse.
  const int flip = kImpostorWordEndian == 1 ? 1 : kImpostorWordEndian == 2 ? 3 : kImpostorWordEndian == 3 ? 2 : 0;
  for (int i = 0; i < 8; ++i) bytes[8 * q + i] = host[i ^ flip];
}
static ImpostorCall Good(uint32_t quads) {
  ImpostorCall c;
  c.read = true;
  c.ps_hash = kImpostorPs;
  c.vs_hash = kImpostorVs;
  c.literals_match = true;
  c.prim = kImpostorPrimitive;
  c.count = 4 * quads;
  c.stride = kImpostorStride;
  c.storage = 0x4A120000u;
  c.storage_dwords = 4 * quads;
  for (auto& r : c.vs) for (float& v : r) v = 0.5f;
  const float c12[4] = {20.0f, 35.0f, 2.0f, 0.468f}, c19[4] = {82.0f, 157.0f, 50.0f, 1.0f}, c20[4] = {0.005f, 0.07f, 0.09f, 8.6f},
              c29[4] = {48.0f, 0.125f, 998.4f, 25.6f}, c31[4] = {0.3668f, 0.0f, 0.0f, 0.0f};
  std::memcpy(c.ps[kImpP12], c12, 16); std::memcpy(c.ps[kImpP19], c19, 16); std::memcpy(c.ps[kImpP20], c20, 16);
  std::memcpy(c.ps[kImpP29], c29, 16); std::memcpy(c.ps[kImpP31], c31, 16);
  c.pbool = 0x00000100u;                       // every captured row's word
  const uint32_t tf0[6] = {kImpostorFetch0, 0x12704043u, 255u | (255u << 13), kImpostorSwizzle << 1, 3u << 6, (1u << 9) | 0x12724000u};
  std::memcpy(c.fetch, tf0, sizeof(tf0));
  c.vfetch[0] = (c.storage & 0x1FFFFFFCu) | 3u;
  c.vfetch[1] = kImpostorWordEndian;
  c.state.read = true;
  c.state.color_control = 0xAA00001Cu;
  c.state.alpha_ref = 0;                       // 0.0f
  c.state.blend_control = 0x00010001u;
  c.state.color_mask = 0xF;
  c.state.depth_control = 0x00708766u;
  c.state.vport_z_scale = 0xBF800000u;         // -1: the reversed range
  c.state.vte_cntl = 0x10;
  c.state.surface_info = 1u << 16;             // two samples
  return c;
}

int main() {
  bool known = true;
  if (ParseImpostorMode("off", &known) != ImpostorMode::kOff || !known) return 1;
  if (ParseImpostorMode("false", &known) != ImpostorMode::kOff || !known || ParseImpostorMode("0", &known) != ImpostorMode::kOff || !known) return 2;
  if (ParseImpostorMode("flat", &known) != ImpostorMode::kFlat || !known) return 3;
  if (ParseImpostorMode("on", &known) != ImpostorMode::kOn || !known || ParseImpostorMode("true", &known) != ImpostorMode::kOn ||
      ParseImpostorMode("1", &known) != ImpostorMode::kOn || !known) return 4;
  if (ParseImpostorMode("FLAT", &known) != ImpostorMode::kOff || known) return 5;              // unknown words are off, and said
  if (ParseImpostorMode("", &known) != ImpostorMode::kOff || known) return 6;
  if (std::strcmp(ImpostorModeName(ImpostorMode::kFlat), "flat") != 0 || std::strcmp(ImpostorReasonName(ImpostorReason::kGroundFog), "ground-fog") != 0 ||
      std::strcmp(ImpostorReasonName(ImpostorReason::kFogTable), "fog-table") != 0 || std::strcmp(ImpostorReasonName(ImpostorReason::kOverLimit), "over-limit") != 0) return 7;
  if (ImpostorQuads(4) != 1 || ImpostorQuads(3712) != 928 || ImpostorQuads(0) != 0 || ImpostorQuads(6) != 0 ||
      ImpostorQuads(4 * kMaxImpostorQuads) != kMaxImpostorQuads || ImpostorQuads(4 * kMaxImpostorQuads + 4) != 0) return 8;

  // The call's rules, each alone, in their order.
  if (CheckImpostorCall(Good(3)) != ImpostorReason::kNone) return 10;
  { ImpostorCall c = Good(3); c.read = false; c.ps_hash = 1; if (CheckImpostorCall(c) != ImpostorReason::kUnread) return 11; }   // unread before shader
  { ImpostorCall c = Good(3); c.vs_hash = kImpostorPs; if (CheckImpostorCall(c) != ImpostorReason::kShader) return 12; }
  { ImpostorCall c = Good(3); c.literals_match = false; if (CheckImpostorCall(c) != ImpostorReason::kLiteral) return 13; }
  for (int k = 0; k < 6; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.prim = 4;
    if (k == 1) c.stride = 20;
    if (k == 2) c.count = 14;                  // not a multiple of four
    if (k == 3) c.storage = 0;
    if (k == 4) c.storage_dwords = 6;          // another draw's field
    if (k == 5) c.vfetch[0] ^= 0x1000u;        // the fetch constant names other memory
    if (CheckImpostorCall(c) != ImpostorReason::kStorage) return 14;
  }
  { ImpostorCall c = Good(3); c.vs[kImpC115][2] = std::numeric_limits<float>::quiet_NaN(); if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 15; }
  { ImpostorCall c = Good(3); c.ps[kImpP29][1] = 0.0f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }   // c29.y in (0, 4]
  { ImpostorCall c = Good(3); c.ps[kImpP31][0] = 65.0f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }  // c31.x in (0, 64]
  { ImpostorCall c = Good(3); c.vs[kImpC72][0] = 2e7f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }   // a matrix row beyond 1e7
  for (float w : {0.0f, 64.5f, -1.0f, std::numeric_limits<float>::infinity()}) {
    ImpostorCall c = Good(3);
    c.ps[kImpP20][3] = w;
    if (CheckImpostorCall(c) != ImpostorReason::kScale) return 17;
  }
  for (int k = 0; k < 8; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.state.read = false;
    if (k == 1) c.state.depth_control &= ~0x4u;            // no depth write
    if (k == 2) c.state.blend_control = 0x07060706u;       // a blend
    if (k == 3) c.state.color_mask = 0x7;
    if (k == 4) c.state.color_control = 0xAA000014u;       // alpha-to-mask without the alpha test
    if (k == 5) c.state.color_control = 0x8700001Cu;       // other offsets
    if (k == 6) c.state.surface_info = 0;                  // one sample
    if (k == 7) c.state.vport_z_scale = 0x3F800000u;       // not the reversed range
    if (CheckImpostorCall(c) != ImpostorReason::kState) return 18;
  }
  for (int k = 0; k < 5; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.fetch[1] = 0x12704052u;                  // DXT1
    if (k == 1) c.fetch[2] = 127u | (127u << 13);          // 128 x 128
    if (k == 2) c.fetch[0] |= 1u << 2;                     // a sign field: the fetch is not as stored
    if (k == 3) c.fetch[4] = 2u << 6;                      // highest level 2
    if (k == 4) c.fetch[1] = 0x00000043u;                  // no base
    if (CheckImpostorCall(c) != ImpostorReason::kFetch) return 19;
  }
  { ImpostorCall c = Good(3); c.pbool |= kImpostorB129; if (CheckImpostorCall(c) != ImpostorReason::kGroundFog) return 9; }

  // The words (Review Focus 2): whole, in the table's order, every value judged.
  const float tree[3][4] = {{100.0f, 200.0f, 30.0f, 8.0f}, {-64.0f, 512.0f, 0.5f, 12.0f}, {1024.0f, -2.0f, 96.0f, 0.25f}};
  std::vector<uint8_t> bytes(24);
  for (uint32_t q = 0; q < 3; ++q) PutQuad(bytes, q, tree[q]);
  float words[12];
  ImpostorReason why = ImpostorReason::kPending;
  ImpostorRecord r = MakeImpostorRecord(Good(3), bytes.data(), bytes.size(), words, &why);
  if (why != ImpostorReason::kNone || r.ps_hash != kImpostorPs || r.quads != 3 || r.first_quad != 0 || r.place != 0) return 20;
  if (std::memcmp(words, tree, sizeof(words)) != 0) return 21;
  if (r.ps[kImpP29][0] != 48.0f || r.depth != 0x00708766u || r.colour_control != 0xAA00001Cu || r.colour_mask != 0xF || r.alpha_ref != 0.0f) return 22;
  float kept[12];
  std::memcpy(kept, words, sizeof(kept));
  r = MakeImpostorRecord(Good(3), bytes.data(), 23, words, &why);                        // a byte short: nothing
  if (why != ImpostorReason::kUnread || r.ps_hash != 0 || std::memcmp(kept, words, sizeof(kept)) != 0) return 23;
  r = MakeImpostorRecord(Good(3), nullptr, 24, words, &why);
  if (why != ImpostorReason::kUnread || r.ps_hash != 0) return 24;
  for (int k = 0; k < 4; ++k) {
    std::vector<uint8_t> bad = bytes;
    float v[4] = {100.0f, 200.0f, 30.0f, 8.0f};
    if (k == 0) v[3] = 0.0f;                                   // no size
    if (k == 1) v[3] = -8.0f;                                  // a negative size
    if (k == 3) v[3] = kImpostorSizeMax * 2.0f;                // beyond the bound
    PutQuad(bad, 1, v);
    if (k == 2) { bad[8 + 0] = 0x7C; bad[8 + 1] = 0x7C; bad[8 + 2] = 0x7C; bad[8 + 3] = 0x7C; }   // two halves 0x7C7C: no numbers, whatever the endian
    r = MakeImpostorRecord(Good(3), bad.data(), bad.size(), words, &why);
    if (why != ImpostorReason::kQuad || r.ps_hash != 0 || std::memcmp(kept, words, sizeof(kept)) != 0) return 25;
  }
  { ImpostorCall c = Good(3); c.literals_match = false;                                  // the call's rules again, before any byte
    r = MakeImpostorRecord(c, bytes.data(), bytes.size(), words, &why);
    if (why != ImpostorReason::kLiteral || r.ps_hash != 0) return 26; }
  static_assert(sizeof(ImpostorRecord) % 8 == 0);
  if (kMaxImpostorRecords != 32 || kMaxImpostorQuads != 4096) return 27;

  // The fog's identity with the frame's.
  FogBlock block{};
  block.valid = true;
  block.axis[0] = 0.25f; block.axis[1] = 0.5f; block.axis[2] = 0.75f;
  block.range[0] = 12.0f; block.range[1] = 1900.0f; block.range[2] = 0.05f; block.range[3] = 0.002f;
  const uint32_t base[2] = {0x1A100000u, 0x1A200000u};   // tf4, tf5
  ImpostorCall c = Good(3);
  std::memcpy(c.vs[kImpC110], block.axis, 12);
  c.vs[kImpC111][0] = 12.0f; c.vs[kImpC111][1] = 7.0f; c.vs[kImpC111][2] = 0.05f; c.vs[kImpC111][3] = 0.002f;   // (.y is not the fog's)
  c.fog_fetch[0][1] = base[1] | 0x1E;                    // slot 16 is tf5
  c.fog_fetch[1][1] = base[0] | 0x1E;                    // slot 17 is tf4
  r = MakeImpostorRecord(c, bytes.data(), bytes.size(), words, &why);
  if (why != ImpostorReason::kNone || !ImpostorFogMatches(r, block, base)) return 30;
  { ImpostorRecord o = r; o.fog_fetch[0][1] = base[0] | 0x1E; if (ImpostorFogMatches(o, block, base)) return 31; }   // the slots exchanged
  { ImpostorRecord o = r; o.vs[kImpC110][2] = 0.7500001f; if (ImpostorFogMatches(o, block, base)) return 32; }
  { ImpostorRecord o = r; o.vs[kImpC111][3] = 0.003f; if (ImpostorFogMatches(o, block, base)) return 33; }
  return 0;
}
```

      (`Good`'s vertex registers are 0.5 everywhere: if Task 6's bounds exclude 0.5 for a
      register, `Good` takes the middle of that register's bound instead, and the test says
      so.) `tests/native/test_frame_scene.cpp` (after the file's last number, 194; names
      qualified as the file's other cases qualify them):

```cpp
  {
    // Impostor records (plan Task 8): a draw whole or not there; the set is not all-or-none.
    auto record = [](uint32_t quads) {
      capture::ImpostorRecord r{};
      r.ps_hash = capture::kImpostorPs;
      r.vs_hash = capture::kImpostorVs;
      r.quads = quads;
      return r;
    };
    std::vector<float> words(4 * capture::kMaxImpostorQuads, 1.5f);
    render::FrameBuilder b;
    b.Open();
    b.ImpostorSeen();                                               // the mode is off: nothing
    b.AddImpostor(record(2), words.data(), capture::ImpostorReason::kNone);
    auto s = b.Finish(1);
    if (s->impostor_mode != capture::ImpostorMode::kOff || s->impostor_seen != 0 || !s->impostors.empty() || !s->impostor_words.empty()) return 200;
    b.ImpostorsOn(capture::ImpostorMode::kFlat);
    b.Open();
    for (uint32_t i = 0; i < 3; ++i) b.ImpostorSeen();
    b.AddImpostor(record(2), words.data(), capture::ImpostorReason::kNone);
    b.AddImpostor(capture::ImpostorRecord{}, nullptr, capture::ImpostorReason::kQuad);   // one model refused: the others stand
    b.AddImpostor(record(5), words.data(), capture::ImpostorReason::kNone);
    s = b.Finish(2);
    if (s->impostor_mode != capture::ImpostorMode::kFlat || s->impostor_seen != 3 || s->impostor_taken != 2 || s->impostor_quads != 7) return 201;
    if (s->impostors.size() != 2 || s->impostors[0].first_quad != 0 || s->impostors[1].first_quad != 2 || s->impostor_words.size() != 28) return 202;
    if (s->impostor_refused[size_t(capture::ImpostorReason::kQuad)] != 1 || s->impostor_refused[size_t(capture::ImpostorReason::kPending)] != 0) return 203;
    b.Open();                                                       // the 33rd draw
    for (uint32_t i = 0; i < 33; ++i) { b.ImpostorSeen(); b.AddImpostor(record(1), words.data(), capture::ImpostorReason::kNone); }
    s = b.Finish(3);
    if (s->impostors.size() != 32 || s->impostor_quads != 32 || s->impostor_refused[size_t(capture::ImpostorReason::kOverLimit)] != 1) return 204;
    b.Open();                                                       // a draw whose quads do not fit: that draw whole, the next still taken
    b.ImpostorSeen(); b.AddImpostor(record(4000), words.data(), capture::ImpostorReason::kNone);
    b.ImpostorSeen(); b.AddImpostor(record(97), words.data(), capture::ImpostorReason::kNone);
    b.ImpostorSeen(); b.AddImpostor(record(96), words.data(), capture::ImpostorReason::kNone);
    s = b.Finish(4);
    if (s->impostors.size() != 2 || s->impostor_quads != 4096 || s->impostors[1].quads != 96 || s->impostors[1].first_quad != 4000) return 205;
    if (s->impostor_words.size() != 4 * 4096 || s->impostor_refused[size_t(capture::ImpostorReason::kOverLimit)] != 1) return 206;
    b.Open();                                                       // an invalid record offered as good, a reason that is no reason
    b.ImpostorSeen(); b.AddImpostor(capture::ImpostorRecord{}, words.data(), capture::ImpostorReason::kNone);
    b.ImpostorSeen(); b.AddImpostor(record(1), nullptr, capture::ImpostorReason::kNone);
    b.ImpostorSeen(); b.AddImpostor(capture::ImpostorRecord{}, nullptr, capture::ImpostorReason::kCount);
    s = b.Finish(5);
    if (!s->impostors.empty() || s->impostor_refused[size_t(capture::ImpostorReason::kUnread)] != 3) return 207;
    b.Open();                                                       // a record's place is kept as given
    capture::ImpostorRecord placed = record(1);
    placed.place = 1059;
    b.ImpostorSeen(); b.AddImpostor(placed, words.data(), capture::ImpostorReason::kNone);
    s = b.Finish(6);
    if (s->impostors.size() != 1 || s->impostors[0].place != 1059) return 208;
    if (s->impostor_refused[size_t(capture::ImpostorReason::kFogTable)] != 0) return 209;   // a frame without a fog block keeps its records
    b.Open();                                                       // a call whose record never came (Review Focus 5)
    b.ImpostorSeen(); b.ImpostorSeen();
    b.AddImpostor(record(1), words.data(), capture::ImpostorReason::kNone);
    s = b.Finish(7);
    if (s->impostor_seen != 2 || s->impostor_taken != 1 || s->impostor_refused[size_t(capture::ImpostorReason::kPending)] != 1) return 210;
    b.Open();                                                       // nothing of one frame in the next
    s = b.Finish(8);
    if (s->impostor_seen != 0 || !s->impostors.empty() || !s->impostor_words.empty() || s->impostor_refused[size_t(capture::ImpostorReason::kPending)] != 0) return 211;
  }
```

      and one case more (return 212) with the frame's fog set as the file's fog cases set it
      (`SetFog` with a valid block and a source): of two records, the one whose `fog_fetch` and
      `c110` / `c111` are the frame's stays, the other is counted `fog-table`.
      `tests/native/test_draw_record.cpp`: `static_assert(sizeof(DrawRecord) == 616)` restated
      with "sub-project 12 adds nothing to it". `tests/test_impostor_formulas.py`:
      `impostor_check.check_records` on a made frame row and its made `"verts"` /
      `"vertsdata"` rows (equal: no problem; one register bit, one word bit, the quad count,
      `seen` not equal to the frame's `129F` rows: one problem each).
- [ ] Implement `impostor_record.h`; both native tests exit 0; the Python test OK.
- [ ] Implement the capture, the frame row, the log lines, the switch
      (`CurrentImpostors()` beside `CurrentWind()`: `capture::ParseImpostorMode`, one warning
      that names `kImpostorSwitchWords`; the cvar's help text; in `PollFrame`
      `capture::SetImpostorsWanted(wanted ? CurrentImpostors() : capture::ImpostorMode::kOff)`
      after the wind's store and before `SetRecordsWanted(true)`). Suites green; build.
- [ ] NEEDS_RUN (a capture-only build: no binding, target or shader text changes, so no
      software-adapter run comes first): `R-BRIDGE`: BRIDGE with `-GameArgs BASE,PAIR,FLAT`;
      `R-THIRD`: THIRD with `-GameArgs BASE,PAIR,FLAT`; `R-OFF`: BRIDGE with `-GameArgs
      BASE,PAIR`. Copies as `t8_*`.
- [ ] From the runs, **criterion 2's first reading**: `rtk python
      tools/xdk_sigmatch/impostor_check.py records <capture>` on both `flat` captures: every
      record equals its rows bit for bit; `seen` = `taken` = the frame's `129F` rows, `quads`
      their quads, no reason. The log's install line and the 300-frame line; `dropped`. In
      `R-OFF`: the frame rows say `"impostors":"off"`, the one line `[native] impostors: off`,
      and the `unsupported by hook {D3DDevice_BeginVertices?: 16, ...}` line as log 657 has it
      (ruling 10). The capture median of `R-BRIDGE` beside `R-OFF`'s (reported: these are not
      timing runs).
- [ ] Frame map "Impostor records"; README. Commit "Impostor records: a bounded side block of
      the far woods' draws, the three-valued switch".

### Task 9: The draw's rules and mirrors (pure)

**Files:** `src/native/render/clay_logic.h` (section "Native impostors"),
`tests/native/test_clay_logic.cpp`, `tests/native/impostor_pins.inc` (generated by
`impostor_pins.py`), `tests/test_impostor_literals.py` (new, as `tests/test_wind_literals.py`),
`lighting-formulas.md`.

**Consumes:** `capture::ImpostorRecord` and the table's constants (Task 8); `light_formulas`'
three functions and `impostor_pins.inc_lines()` (Tasks 4 to 6); `kImpostorFlatColour` (Task 7);
`ClayFrameConstants` (`clay_logic.h` 2848), `ClayFogConstants` (2826), `FogCoords` (3235),
`FogTableRead` (3181), `kFogScatterTexels`, `kFogTransmitTexels`; `CoveredSamples` (1786);
`PlanCoverageOrder` (1848), `ColourOrder`, `ColourDue`, `ColourStepsDue`, `ColourStepsAfter`
(5536 to 5567); `PlanClayFrameSlots`, `ClayFrameSmallSlots`, `ClayConstantBufferBytes`
(5773 to 5805); `kQuadRegisterMax` (4582) and `MakeClayQuadConstants`' scale rule (4619 to 4622)
as the pattern; `kPostSceneMax` (3924).

**Produces** (PROVISIONAL where it follows the register count: Task 6's report):

```cpp
// cbuffer Impostor: register b0 of the impostor step's own layout. One a drawn record.
struct ClayImpostorConstants {
  float vs[capture::kImpostorVsRegisters][4];   // the record's vertex registers, in the table's order
  float ps[capture::kImpostorPsRegisters][4];   // c12, c19, c20, c29, c31
  uint32_t first_quad, quads;                   // the record's words in the frame's buffer; first_quad + quads <= kMaxImpostorQuads
  uint32_t kept;                                // kImpostorKeepFirst, kImpostorKeepFull or kImpostorKeepHalf
  uint32_t sampler;                             // the static sampler of the picture's fetch (0 in flat)
  float alpha_ref, out_scale, fog_on, pad;      // the alpha test's reference; what the colour is multiplied by; 1: the frame's fog is on
};
static_assert(sizeof(ClayImpostorConstants) == 336);                               // PROVISIONAL: 224 + 80 + 16 + 16
inline constexpr uint32_t kClayImpostorSlots = (sizeof(ClayImpostorConstants) + 255) / 256;   // 2
inline constexpr uint32_t kClayImpostorFrameSlots = kClayImpostorSlots * capture::kMaxImpostorRecords;   // 64, only with the switch on
enum : uint32_t { kImpostorKeepFirst = 1, kImpostorKeepFull = 2, kImpostorKeepHalf = 3 };
// Which pixels a draw of `kept` shades: the alpha test passed and the first sample covered; both samples;
// exactly one. (covered: CoveredSamples under offsets 0xAA.)
constexpr bool ImpostorKept(uint32_t kept, bool alpha_test, uint32_t covered);

enum class ClayImpostorSkip : uint8_t { kNone, kPipeline, kSlot, kBuffer, kScale, kPictureBase, kPictureUnread,
                                        kPictureContent, kPictureUpload, kCount };
constexpr const char* ClayImpostorSkipName(ClayImpostorSkip s);   // "none", "pipeline", "slot", "buffer", "scale", "picture: base",
                                                                  // "picture: unread", "picture: content", "picture: upload"
enum class ImpostorPicture : uint8_t { kNone, kUsable, kBase, kUnread, kContent, kUpload };
// flat: drawn with the flat texture whatever the picture's state. on: drawn only with a usable picture;
// anything else is its reason, and no picture at all is "picture: unread". off: never asked.
constexpr ClayImpostorSkip DecideImpostorPicture(capture::ImpostorMode mode, ImpostorPicture picture);

// False, with `out` untouched, unless: the record is valid; first_quad + quads <= kMaxImpostorQuads and quads > 0;
// every register finite and inside the table's bound (checked again here); c20.w in (0, 64]; in a post frame
// (`post_frame`) c20.w equal to `out_scale` within 1e-3 of it; `out_scale` in (0, 64]; alpha_ref in [0, 1].
bool MakeClayImpostorConstants(const capture::ImpostorRecord& r, float out_scale, bool post_frame, bool fog_on,
                               uint32_t kept, uint32_t sampler, ClayImpostorConstants* out);
// Single precision, in the reference's order of operations (light_formulas.impostor_position, term for term).
void ImpostorPositionMirror(uint32_t k, const float word[4], const ClayImpostorConstants& c, float pos[4], float place[3], float uv[2]);
// (o2, o3) at the raised place from the record's c47, c110, c111 and the frame's two tables; (1, 0) with the
// frame's fog off, before the start, and for any value that is no number.
void ImpostorFogMirror(const float place[3], const ClayImpostorConstants& c, const ClayFrameConstants& frame, float o2[3], float o3[3]);
// The written colour ((v > 0) ? min(v, 31.875) : 0 a channel; a texel that is no number reads 0) and whether
// the pixel is kept by `c.kept`.
void ImpostorPixelMirror(const float texel[4], const float place[3], const float o2[3], const float o3[3],
                         const ClayImpostorConstants& c, float out[3], bool* kept);

// The slot plan: PlanClayFrameSlots and ClayFrameSmallSlots gain a last argument, `impostor_slots` (default 0):
//   p.impostor0 = p.quad0 + kClayQuadFrameSlots;  p.object0 = p.impostor0 + impostor_slots.
// Where the step is drawn: before the prepared record at the returned index (count: after the last).
// `seqs`: the prepared records' DrawRecord::seq in the frame's order; `place`: the smallest place of the
// frame's impostor records. The first record at or beyond the place, moved forward so that the step lies
// before the sky step, the early tail and the layer's depth (it is opaque and writes depth: earlier changes
// no pixel).
constexpr size_t PlanImpostorPlace(const uint32_t* seqs, size_t count, uint32_t place, size_t sky_at, size_t tail_at, size_t depth_at);
// ColourOrder gains `size_t impostors_at = ~size_t(0)`; ColourDue gains `bool impostors`, due first of an index's steps.
```

- [ ] Tests first (`tests/native/test_clay_logic.cpp`, CHECK ids from 800; the file's last is
      770), see them fail:

```cpp
  // Native impostors (plan Task 9).
  {
    capture::ImpostorRecord r{};
    r.ps_hash = capture::kImpostorPs;
    r.vs_hash = capture::kImpostorVs;
    r.quads = 928;
    r.first_quad = 0;
    for (auto& reg : r.vs) for (float& v : reg) v = 0.5f;
    const float ps[5][4] = {{20.0f, 35.0f, 2.0f, 0.468f}, {82.0f, 157.0f, 50.0f, 1.0f}, {0.005f, 0.07f, 0.09f, 8.6f},
                            {48.0f, 0.125f, 998.4f, 25.6f}, {0.3668f, 0.0f, 0.0f, 0.0f}};
    std::memcpy(r.ps, ps, sizeof(ps));
    ClayImpostorConstants c{};
    CHECK(800, MakeClayImpostorConstants(r, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    CHECK(801, c.first_quad == 0 && c.quads == 928 && c.kept == kImpostorKeepFirst && c.out_scale == 8.6f && c.fog_on == 1.0f);
    CHECK(802, c.ps[capture::kImpP29][0] == 48.0f && c.vs[capture::kImpC8][2] == 0.5f);
    ClayImpostorConstants same = c;
    CHECK(803, !MakeClayImpostorConstants(capture::ImpostorRecord{}, 8.6f, true, true, kImpostorKeepFirst, 0, &c) &&
                   std::memcmp(&same, &c, sizeof(c)) == 0);                                  // an invalid record: out untouched
    capture::ImpostorRecord bad = r;
    bad.vs[capture::kImpC116][1] = std::numeric_limits<float>::infinity();
    CHECK(804, !MakeClayImpostorConstants(bad, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    bad = r;
    bad.quads = 0;
    CHECK(805, !MakeClayImpostorConstants(bad, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    // Review Focus 1: no draw whose words lie past the buffer's end.
    bad = r;
    bad.first_quad = capture::kMaxImpostorQuads - 927;
    CHECK(806, !MakeClayImpostorConstants(bad, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    bad.first_quad = capture::kMaxImpostorQuads - 928;
    CHECK(806, MakeClayImpostorConstants(bad, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    bad.first_quad = 0xFFFFFFF0u;                                                             // no 32-bit wrap
    CHECK(806, !MakeClayImpostorConstants(bad, 8.6f, true, true, kImpostorKeepFirst, 0, &c));
    // The scale: another frame's record is not drawn in a post frame; without post the frame's own exposure stands.
    CHECK(807, !MakeClayImpostorConstants(r, 8.7f, true, true, kImpostorKeepFirst, 0, &c));   // 1.2% off
    CHECK(808, MakeClayImpostorConstants(r, 8.6f * 1.0005f, true, true, kImpostorKeepFirst, 0, &c));
    CHECK(809, MakeClayImpostorConstants(r, 1.25f, false, true, kImpostorKeepFirst, 0, &c) && c.out_scale == 1.25f);
    CHECK(809, !MakeClayImpostorConstants(r, 0.0f, false, true, kImpostorKeepFirst, 0, &c) &&
                   !MakeClayImpostorConstants(r, 65.0f, false, true, kImpostorKeepFirst, 0, &c));
    // The kept sets part the pixels: full and half are disjoint and together are the first sample's set.
    for (uint32_t covered = 0; covered <= 2; ++covered) {
      for (bool test : {false, true}) {
        const bool first = ImpostorKept(kImpostorKeepFirst, test, covered), full = ImpostorKept(kImpostorKeepFull, test, covered),
                   half = ImpostorKept(kImpostorKeepHalf, test, covered);
        CHECK(810, first == (test && covered >= 1) && full == (test && covered == 2) && half == (test && covered == 1));
        CHECK(811, !(full && half) && (full || half) == first);
      }
    }
    // Review Focus 4: without impostor slots the plan is today's, for every input.
    for (uint32_t terrain = 0; terrain < 40; ++terrain) {
      for (uint32_t plants = 0; plants < 12; ++plants) {
        const ClayFrameSlotPlan a = PlanClayFrameSlots(terrain, plants), b = PlanClayFrameSlots(terrain, plants, 0);
        CHECK(812, a.plant0 == b.plant0 && a.mask == b.mask && a.sky == b.sky && a.post == b.post && a.quad0 == b.quad0 &&
                       a.object0 == b.object0);
        // (today's arithmetic, written out: clay_logic.h as of the commit before this task)
        CHECK(812, a.plant0 == kClayFrameTerrainSlot0 + terrain && a.mask == a.plant0 + plants && a.sky == a.mask + 1 &&
                       a.post == a.sky + 1 && a.quad0 == a.post + kClayPostSlots && a.object0 == a.quad0 + kClayQuadFrameSlots);
        CHECK(812, ClayFrameSmallSlots(terrain, plants) == ClayFrameSmallSlots(terrain, plants, 0) &&
                       ClayFrameSmallSlots(terrain, plants) == uint64_t(kClayFrameTerrainSlot0) + terrain + plants + 2 + kClayPostSlots + kClayQuadFrameSlots);
        const ClayFrameSlotPlan on = PlanClayFrameSlots(terrain, plants, kClayImpostorFrameSlots);
        CHECK(812, on.impostor0 == a.object0 && on.object0 == a.object0 + kClayImpostorFrameSlots &&
                       on.object0 <= ClayFrameSmallSlots(terrain, plants, kClayImpostorFrameSlots));
      }
    }
    // Its place: at the game's place, never after the sky step, the tail or the layer's depth.
    const uint32_t seqs[6] = {10, 20, 1060, 1100, 1200, 1300};
    const size_t none = ~size_t(0);
    CHECK(813, PlanImpostorPlace(seqs, 6, 1059, 5, none, 6) == 2);       // the first record at or beyond the place
    CHECK(813, PlanImpostorPlace(seqs, 6, 5, 5, none, 6) == 0 && PlanImpostorPlace(seqs, 6, 2000, 6, none, 6) == 6);
    CHECK(814, PlanImpostorPlace(seqs, 6, 1059, 1, none, 6) == 1);       // the sky step lies earlier: before it
    CHECK(814, PlanImpostorPlace(seqs, 6, 1059, 5, 1, 6) == 1 && PlanImpostorPlace(seqs, 6, 1059, 5, none, 0) == 0);
    CHECK(814, PlanImpostorPlace(nullptr, 0, 1059, 0, none, 0) == 0);
    for (size_t sky = 0; sky <= 6; ++sky) {
      for (size_t tail = 0; tail <= 7; ++tail) {
        for (size_t depth = 0; depth <= 6; ++depth) {
          const size_t t = tail == 7 ? none : tail, at = PlanImpostorPlace(seqs, 6, 1059, sky, t, depth);
          CHECK(814, at <= 2 && at <= sky && at <= depth && (t == none || at <= t));
        }
      }
    }
    // Review Focus 5: in `on` nothing but a usable picture is drawn; never a flat quad in a picture's place.
    for (uint32_t p = 0; p <= uint32_t(ImpostorPicture::kUpload); ++p) {
      const ImpostorPicture picture = ImpostorPicture(p);
      CHECK(815, DecideImpostorPicture(capture::ImpostorMode::kFlat, picture) == ClayImpostorSkip::kNone);
      CHECK(815, (DecideImpostorPicture(capture::ImpostorMode::kOn, picture) == ClayImpostorSkip::kNone) == (picture == ImpostorPicture::kUsable));
    }
    CHECK(815, DecideImpostorPicture(capture::ImpostorMode::kOn, ImpostorPicture::kNone) == ClayImpostorSkip::kPictureUnread &&
                   DecideImpostorPicture(capture::ImpostorMode::kOn, ImpostorPicture::kBase) == ClayImpostorSkip::kPictureBase &&
                   DecideImpostorPicture(capture::ImpostorMode::kOn, ImpostorPicture::kContent) == ClayImpostorSkip::kPictureContent &&
                   DecideImpostorPicture(capture::ImpostorMode::kOn, ImpostorPicture::kUpload) == ClayImpostorSkip::kPictureUpload);
    // The fog: nothing with the frame's fog off and before the start.
    ClayFrameConstants frame{};
    CHECK(800, MakeClayImpostorConstants(r, 8.6f, true, false, kImpostorKeepFirst, 0, &c) && c.fog_on == 0.0f);
    const float place[3] = {300.0f, 40.0f, 5.0f};
    float o2[3], o3[3];
    ImpostorFogMirror(place, c, frame, o2, o3);
    CHECK(816, o2[0] == 1.0f && o2[1] == 1.0f && o2[2] == 1.0f && o3[0] == 0.0f && o3[1] == 0.0f && o3[2] == 0.0f);
    // The pixel: the dissolve's three steps with a whole picture (spec, "Read for this design").
    const float one[3] = {1.0f, 1.0f, 1.0f}, zero[3] = {0.0f, 0.0f, 0.0f}, texel[4] = {0.5f, 0.25f, 0.125f, 1.0f};
    for (uint32_t kept : {kImpostorKeepFirst, kImpostorKeepFull, kImpostorKeepHalf}) {
      CHECK(800, MakeClayImpostorConstants(r, 8.6f, true, false, kept, 0, &c));
      for (float d : {49.9f, 50.0f, 53.9f, 54.0f, 200.0f}) {
        const float at[3] = {82.0f + d, 157.0f, 50.0f};       // d units from the record's c19
        float out[3];
        bool keep = true;
        ImpostorPixelMirror(texel, at, one, zero, c, out, &keep);
        const uint32_t covered = d < 50.0f ? 0u : d < 54.0f ? 1u : 2u;
        CHECK(817, keep == ImpostorKept(kept, true, covered));
      }
    }
    {
      CHECK(800, MakeClayImpostorConstants(r, 8.6f, true, false, kImpostorKeepFirst, 0, &c));
      const float at[3] = {82.0f + 200.0f, 157.0f, 50.0f};
      float out[3];
      bool keep = false;
      ImpostorPixelMirror(texel, at, one, zero, c, out, &keep);
      CHECK(818, keep && std::fabs(out[0] - 0.5f * 0.3668f * 0.468f * 8.6f) < 1e-5f);    // far: the mean shadow term, the output scale
      const float nan_texel[4] = {std::numeric_limits<float>::quiet_NaN(), 0.25f, 0.125f, 1.0f};
      ImpostorPixelMirror(nan_texel, at, one, zero, c, out, &keep);
      CHECK(818, out[0] == 0.0f && out[1] > 0.0f);                                         // a texel that is no number reads 0
      const float big[4] = {1e6f, 0.25f, 0.125f, 1.0f};
      ImpostorPixelMirror(big, at, one, zero, c, out, &keep);
      CHECK(818, out[0] == kPostSceneMax);
    }
  }
#include "impostor_pins.inc"   // CHECK(820..): each pinned line of impostor_pins.py against the three mirrors
```

      (The alpha reference of `r` is 0 here; with Task 3's reference the dissolve's test keeps
      its thresholds as long as the reference is below 0.25, and says so if it is not.)
      `tests/test_impostor_literals.py`: `kClayImpostorSlots`, the cbuffer's offsets and the
      three keep words equal the HLSL's (Task 10 adds the HLSL side; here the C++ side against
      `light_formulas`: the literals 0.1, 0.5, 0.25, 2, 4 of the two shaders appear in the
      mirrors only where the reference has them).
- [ ] Pins: a mirror agrees with each pinned value within `1e-4 * max(1, |value|)` (criterion
      1's bound: the pins are the double reference rounded to single). If a captured quad's
      single-precision evaluation cannot meet it, the report gives the worst case with its quad
      and the controller rules; the bound is not widened by the implementer.
- [ ] Mutation checks, alone, on scratch copies, each failing a pin: `c116` for `c115` in the
      mirror; the raise left out; two corners exchanged; the exponent's register component
      exchanged with the scale's; `c12.y` for `c29.x` in the pixel mirror; the fog's
      `(1 - o2)` left out.
- [ ] Implement; the native suite green. Commit "Native impostors: the constants, the kept
      sets, the place, the mirrors pinned to the reference".

### Task 10: The impostor step: programs and the pass, in `flat`

**Files:** `src/native/fable2_native_shaders.h`, `src/native/render/clay_pass.cpp` / `.h`,
`src/native/render/clay_logic.h` (`ClayStats`, `FormatImpostorText`),
`src/native/fable2_native_render.cpp` (`Render`'s argument, the F3 text, the log line),
`tools/xdk_sigmatch/wind_hlsl.py` (a region and a function list as arguments),
`tests/native/test_native_shaders.cpp`, `tests/test_impostor_hlsl.py` (new, as
`tests/test_wind_hlsl.py`), `tests/test_impostor_literals.py`, README (the F3 line), frame map
"The impostor step".

**Consumes:** Task 9; `EnsureQuadObjects` (`clay_pass.cpp` 2025: its layout, shader and pipeline
creation and its failure line are the pattern), `ForgetQuadObjects` / `DestroyQuadObjects`;
`FinishTerrain`'s buffer size and plan (823 to 950); the colour loop (3988 to 4070:
`ColourStepsDue`, `bind_scene`, `bound`); `SetBufferSrv` (1224); the frame's constant buffer
`terrain_cb_`; `white_` and `CreateWhite` (328) as the pattern of a 1 x 1 texture; `kClayVs`'s
neighbours in the shader header: `FABLE2_CLAY_LIGHT_HLSL` under `CLAY_FOG` (lines 217 to 386:
cbuffer Frame at `b2` with the fog's tail, `fog_scatter`, `fog_transmit`), `kClaySkyPs` (1196:
a program made of `"#define CLAY_FOG 1\n" FABLE2_CLAY_LIGHT_HLSL` and its own text);
`wind_hlsl.translate` and `Program`; the shader test's baseline cases (253, 255) and its
conditional-load method (the wind's case (c)).

**Produces:**
- `shaders::kClayImpostorVs`, `kClayImpostorPs`, defined after `kClaySkyPs` and before the
  `#undef` block (line 1264), each `"#define CLAY_FOG 1\n" FABLE2_CLAY_LIGHT_HLSL` and its own
  text. **Nothing inside a `FABLE2_CLAY_*_HLSL` macro changes.** Their own text:

```hlsl
cbuffer Impostor : register(b0) {
  float4 i_vs[14];        // capture::kImpostorVsRegisters rows, the table's order (PROVISIONAL with the count)
  float4 i_ps[5];         // c12, c19, c20, c29, c31
  uint i_first_quad; uint i_quads; uint i_kept; uint i_sampler;
  float i_alpha_ref; float i_out_scale; float i_fog_on; float i_pad;
};
StructuredBuffer<float4> impostor_words : register(t1);    // vertex stage: bound by address, no bounds check
Texture2D<float4> impostor_picture : register(t0);         // pixel stage
struct ImpostorVsOut {
  float4 pos : SV_Position;
  nointerpolation float3 place : TEXCOORD0;                // o0: the raised place, one value a quad
  float2 uv : TEXCOORD1;                                   // o1
  nointerpolation float3 fog_factor : TEXCOORD2;           // o2
  nointerpolation float3 fog_add : TEXCOORD3;              // o3
};
// vertex main(uint id : SV_VertexID):
//   const uint quad = id / 6;  static const uint kCorner[6] = {0, 1, 2, 0, 2, 3};
//   float4 word = float4(0, 0, 0, 0);
//   [branch] if (quad < i_quads) word = impostor_words[i_first_quad + quad];   // the ONLY load of the buffer
//   else { o.pos = float4(0, 0, 0, 0) / 0; return o; }                          // (no number: the rasteriser drops it)
//   precise: ImpostorPosition(kCorner[id % 6], word, o.pos, o.place, o.uv);     // the reference, term for term
//   ImpostorFog(o.place, o.fog_factor, o.fog_add);                              // (1, 0) unless i_fog_on
// pixel main: ImpostorPixel: the picture's texel by i_sampler, the dissolve and the light from |i_ps[1].xyz - place|,
//   the kept set by i_kept (discard), then ((colour * fog_factor + fog_add) * i_out_scale) written as
//   (v > 0) ? min(v, 31.875) : 0 a channel, alpha 1.
```

- In the pass: `EnsureImpostorObjects(dev)` (made on the first frame that has a record to draw,
  **never with the switch off**; a failure turns the step off for the run with one error line,
  not the pass): the layout `impostor_layout_` (b0 the draw's constants and b2 the frame's
  block, both stages; `t1` a buffer for the vertex stage; `t0` one texture for the pixel stage;
  the four static samplers), `impostor_vs_`, `impostor_ps_`, **one pipeline** `impostor_pipeline_`
  (the colour pass's targets, depth test less-or-equal with the depth write, no blend, no cull:
  the game's greater-or-equal under its reversed range, as the quad step reads it), the words'
  buffer `impostor_words_` (16 bytes a quad for `kMaxImpostorQuads`, written each frame with the
  scene's `impostor_words`; a frame whose words do not fit or cannot be written: every record
  `buffer`), the flat picture `impostor_flat_` (1 x 1 `R8G8B8A8_UNORM`, `kImpostorFlatColour`
  rounded to bytes, alpha 255). The slots: `PlanClayFrameSlots(..., kClayImpostorFrameSlots)`
  only in a frame with the mode not off; record i's constants at `impostor0 +
  kClayImpostorSlots * i`. `DrawImpostors`: at `PlanImpostorPlace`'s index of the colour loop
  (`ColourDue::impostors`, first of that index's steps; after the last record if the place
  lies there), each record by `DecideImpostorPicture` and `MakeClayImpostorConstants`
  (`kImpostorKeepFirst`: in `flat` always), `6 * quads` vertices, then `bind_scene()` and
  `bound = nullptr`. **No pre-pass draw, no shadow-map draw** (ruling 21; spec Design 9).
- `ClayPass::Render` gains `capture::ImpostorMode impostors` after `coverage` (`clay_pass.h`
  line 125). `ClayStats`: `impostor_mode`, `impostors_seen`, `impostors_drawn`,
  `impostor_quads`, `impostor_skip[]`, `impostor_capture[]`. `FormatImpostorText`:
  `Impostors: off` | `Impostors: flat, drawn 14 of 14 (928 quads) | not drawn {scale: 1} |
  impostor-record {quad: 1}` (the F3 text's last line; the log line `[native] clay: ...` every
  300 frames only with the mode not off, and one line when the form changes, at most 16).

- [ ] Tests first. `tests/native/test_native_shaders.cpp` (after the file's last number, 255):
      **260**: with this task's header every program the pass compiled before (the vertex
      shader, the wind vertex shader, the colour, depth-only and coverage programs, the terrain
      programs, the shadow, sky, mask, quad, cloud, sun and post programs) compiles to the bytes
      it compiled to before the task: the baseline's hashes are recorded by this step from the
      commit before, in the manner of case 253; **261**: `kClayImpostorVs` and `kClayImpostorPs`
      compile; reflection: cbuffer `Impostor` of 336 bytes at `b0` with `i_first_quad` at 304
      and `i_alpha_ref` at 320 (PROVISIONAL with the count), `impostor_words` a buffer of
      16-byte elements at `t1` of the vertex program alone, `impostor_picture` at `t0` of the
      pixel program alone, cbuffer Frame at `b2`; **262**: in the vertex program's compiled text
      every load from `impostor_words` lies inside a conditional block whose condition compares
      a value derived from `SV_VertexID` with `i_quads` (the method of the wind's `t9` case);
      **263**: neither program declares a resource the impostor layout does not bind.
      `tests/test_impostor_hlsl.py`: the HLSL functions `ImpostorPosition`, `ImpostorFog` and
      `ImpostorPixel`, translated by `wind_hlsl` (given the region between this task's markers
      and the three names), equal `light_formulas`' three by value on 200 made inputs and on
      every pinned line, within the pins' bound; `tests/test_wind_hlsl.py` stays green with the
      generalised tool. `tests/test_impostor_literals.py`: the HLSL cbuffer's fields and the
      corner table `{0, 1, 2, 0, 2, 3}` against `ClayImpostorConstants` and the plan's order.
- [ ] Implement the programs, the pass, the F3 line. Suites green; build.
- [ ] NEEDS_RUN, in this order: `WARP-FLAT`: WARP with the suffixes POST, FLAT (240 s, the
      software adapter under the debug layer). Only if it is clean: `V-BRIDGE-FLAT`: BRIDGE
      with `-GameArgs BASE,PAIR,FLAT`, and `V-BRIDGE-OFF`: BRIDGE with `-GameArgs BASE,PAIR`,
      one session. Copies as `t10_*`.
- [ ] From the runs. **Criterion 14 (`flat`)**: 0 `d3d ERROR` lines, the warnings listed by id
      against the fix wave's (FM 7942: four of id 1328), **the log shows the step drew**
      (`Impostors: flat, drawn 14 of 14`), and the shot at 225 s viewed: quads on the ranges.
      The real-GPU shots at 44 s and 86 s viewed and described honestly (flat quads in the
      woods' places; anything torn, floating, standing in front of near ground, or missing).
      First reading of criterion 7: `DT/depth_gap.py` on the `flat` pair against the off pair.
- [ ] Frame map "The impostor step"; README. Commit "Native impostors: one step with its own
      layout and programs, drawn flat at the records' place".

### Task 11: Validation of `flat` (12b) and its documents

**Files:** frame map section 19 ("Validation (12b)", "Limits (12b)"), the spec (status and
"Implementation notes"), `out/native-evidence/distant-trees/` (scripts and outputs, untracked).

- [ ] Runs by the controller (all `RUN UNCHECKED`; each pair one session and one binary):
      1. `v_bridge_off`, `v_bridge_flat`: BRIDGE with `-GameArgs BASE,PAIR` and with FLAT added
         (Task 10's pair serves if it is of this binary).
      2. `v_third_off`, `v_third_flat`: THIRD with `-GameArgs BASE,PAIR` (+ FLAT).
      3. `v_bridge_sun_off`, `v_bridge_sun_flat`: BRIDGE with `-GameArgs BASE,PAIR,SUN` (+ FLAT).
      4. `v_band_off`, `v_band_flat`: the line of D3's view (Task 3) with `-GameArgs
         BASE,PAIR,WIND,COV` (+ FLAT). Not made if Task 3 found no D3.
      5. `v_sun_off`, `v_sun_flat`: the line of D4's view with `-GameArgs BASE,PAIR,SUN` (+ FLAT).
         Not made if Task 3 found no D4.
      6. `v_off`: OFF.
      7. **Timing, each run alone, the window on the usual monitor, one session**, in this
         order: `time_off_1`, `time_flat_1`, `time_flat_2`, `time_off_2`: TIME with `-GameArgs
         BASE,POST,EXPO,SUN,WIND,COV` (+ FLAT); then `time3_off`, `time3_flat`: TIME-THIRD with
         the same arguments.
- [ ] **Criterion 2**: `impostor_check.py records` on runs 1 and 2's `flat` captures: every
      record bit for bit; `seen` = `taken` = the `129F` rows; `quads`; no reason.
- [ ] **Criterion 7 (`flat`)**: `DT/depth_gap.py` on each pair: the far pixels (80 units or
      more) where the game is nearer than the native depth by more than 3% or the native picture
      has sky, `flat` against `off` (bound: at most a tenth), and the far pixels where the
      native depth is nearer by more than 3% (bound: a rise of no more than that tenth). Bridge
      and third spot.
- [ ] **Criterion 8 (`flat`: reported, not bound)** and **criterion 9**: native over game, mean
      linear luminance, in the four far regions and on the off-run's gap pixels; the shared far
      ground ("same distance" pixels, "far peak", "island in the lake", "sky above the ridge")
      within 0.99 to 1.01 and moved by at most 0.005 from the off-run.
- [ ] **Criterion 11's second part (depth alone in `flat`)** from run 4: criterion 7's measure
      on the pixels between 40 and 80 units, and the pixels of quads below 50 units (none may
      be drawn). **Criterion 12's first part** from run 3: of the layer's texels on the off-run's
      gap pixels that are closed in the game's layer depth, the stored native layer depth is
      closed on at least 90% with `flat` and on at most 10% with `off`; fewer than 500 such
      texels: "not decidable". Its second part from run 5: reported.
- [ ] **Criterion 13**: the shader test's case 260; the slot plan's and the list's tests; run
      6's log (no line of the impostors beside `[native] impostors: off`; `seeded
      readback_resolve_force_addresses '0x12704000'` as log 657's) and its native shot at 40 s
      against `k18b_defaults_shot_40s.png` in the bridge's 24 regions, each within the emulated
      pair's range, the two regions of moving leaf shadow named beforehand left out; judged
      only if the shots are 1942 wide.
- [ ] **Criterion 15's `flat` half**: valid only at shot width 1942 and with nothing beside the
      runs: no steady 150-frame native window below 30.0 fps in any of the four; the mean frame
      over steady native windows, `flat` over `off`, at most 0.5 ms, only if the two runs of a
      configuration agree within 1.0 ms (else "not decidable"); the capture medians (a
      difference under a third of a millisecond: "not decidable"); the third spot's pair
      reported. **Criterion 10 (`flat`: reported)**: `region_compare.py` on the timing runs'
      shots (57 s native over the mean of 36 s and 96 s emulated).
- [ ] Each of criteria 2, 7, 8, 9, 11, 12, 13, 14, 15 with its evidence file and an exact
      verdict word; the default of `--fable2_native_impostors` stays `off`, with the
      recommendation for `flat` written beside the evidence.
- [ ] Spec status and "Implementation notes"; frame map "Validation (12b)" and "Limits (12b)".
      Commit "Impostor validation (12b): the flat step's verdicts". Then the phase's review.

---
## Phase 12c: the pictures by read-back

**Entry condition (ledger ruling 22; spec Design 3): Task 2's verdict is "passes", written in
the ledger.** If run COST failed or was twice not decidable, none of Tasks 12 to 16 is started:
the sub-project goes from Task 11 to Task 17 and closes at `flat`; the switch's value `on`
stays as Task 8 left it (it draws nothing and says so), and the pictures rendered natively
(option B) become the roadmap's next item. Tasks 12 and 13 may run side by side (the list in
`src/core`; a pure header and Python); Tasks 14, 15 and 16 follow in order.

### Task 12: The read-back list, seeded behind the switch

**Files:** `src/core/readback_force_list.h`, `src/core/fable_2_app.h`,
`src/native/fable2_native_render.cpp` / `.h` (`ImpostorReadbackWanted`),
`tests/native/test_readback_force_list.cpp`, README, frame map "The read-back list".

**Consumes:** `ForceReadbackList(bool hero_dog, bool exposure)` (`readback_force_list.h` line
43), `kForceReadbackWindow`, `kHeroDogResolveBase`, `kLuminanceResolveBases`; the seeding in
`Fable2App::OnPostSetup` (`fable_2_app.h` lines 162 to 185); `ExposureReadbackWanted`
(`fable2_native_render.cpp` 953) as the pattern; `CurrentImpostors` (Task 8).

**Produces:**

```cpp
// readback_force_list.h
inline constexpr uint32_t kImpostorPictureBase = 0x12704000u, kImpostorPictureStep = 0x2B000u, kImpostorPictures = 14;
inline constexpr uint32_t kImpostorLevelOffset = 0x20000u;       // a picture's lower levels start here from its base
constexpr uint32_t ImpostorPictureBase(uint32_t n) { return kImpostorPictureBase + n * kImpostorPictureStep; }
static_assert(kImpostorPictureBase == kHeroDogResolveBase);      // impostor 0's base is the hero's entry
static_assert(kImpostorPictureStep >= kForceReadbackWindow);     // no window reaches a neighbour's
static_assert(kImpostorLevelOffset >= kForceReadbackWindow && kImpostorLevelOffset < kImpostorPictureStep);   // no level address lies in a window
static_assert(kLuminanceResolveBases[1] - ImpostorPictureBase(kImpostorPictures - 1) >= kForceReadbackWindow);
// `impostors`: the fourteen picture bases as well (the first is the hero's entry: not listed twice).
inline std::string ForceReadbackList(bool hero_dog, bool exposure, bool impostors = false);
// fable2_native_render.h
bool ImpostorReadbackWanted();   // --fable2_native_impostors is `on`; read once at start-up, the renderer installed or not
```

The list is seeded once in `OnPostSetup` under the conditions the exposure's switch has (a
list given by hand wins) and **is never rewritten while the game runs** (spec ruling 7: the SDK
parses the string at every resolve on its own thread). The seeding's log line is today's, byte
for byte, unless the third input is true: only then `, fable2_native_impostors=on` is appended
inside its parentheses (criterion 13 holds an off-run's line against log 657's).
`fable2::native::ImpostorPicturesAvailable()`: the list was seeded with the pictures, or
`--readback_resolve` is `full`; the pass asks it once.

- [ ] Tests first (`tests/native/test_readback_force_list.cpp`, after the file's last number,
      13), see them fail (a third argument does not exist):

```cpp
  // The impostors' pictures (plan Task 12). Review Focus 4: without the third input, and with it false, today's lists.
  if (ForceReadbackList(true, false) != "0x12704000" || ForceReadbackList(true, false, false) != "0x12704000") return 20;
  if (ForceReadbackList(true, true, false) != "0x12704000,0x1A385000,0x1A34F000") return 21;
  if (ForceReadbackList(false, true, false) != "0x1A385000,0x1A34F000") return 22;
  if (!ForceReadbackList(false, false, false).empty() || !ForceReadbackList(false, false).empty()) return 23;
  const std::string fourteen = "0x12704000,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000,"
                               "0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000";
  if (ForceReadbackList(true, false, true) != fourteen) return 24;        // the hero's entry is impostor 0's: once
  if (ForceReadbackList(false, false, true) != fourteen) return 25;       // without the hero's fix the base is listed all the same
  if (ForceReadbackList(true, true, true) != "0x12704000,0x1A385000,0x1A34F000" + fourteen.substr(10)) return 26;
  if (ForceReadbackList(false, true, true) != "0x1A385000,0x1A34F000," + fourteen) return 27;
  for (uint32_t n = 0; n < kImpostorPictures; ++n) {
    const uint32_t base = ImpostorPictureBase(n), level = base + kImpostorLevelOffset;
    if (n + 1 < kImpostorPictures && ImpostorPictureBase(n + 1) - base < kForceReadbackWindow) return 28;
    for (uint32_t m = 0; m < kImpostorPictures; ++m) {
      const uint32_t entry = ImpostorPictureBase(m);
      if (level >= entry && level - entry < kForceReadbackWindow) return 29;       // a level address inside a window
    }
    for (uint32_t lum : kLuminanceResolveBases) {
      if (base >= lum ? base - lum < kForceReadbackWindow : lum - base < kForceReadbackWindow) return 30;
    }
  }
  if (ImpostorPictureBase(13) != 0x12933000u) return 31;
```

- [ ] Implement; the test exits 0; the suites green; build.
- [ ] NEEDS_RUN (no binding, target or shader text changes): `t12_on`: OFF's line with
      `-GameArgs BASE,ON`; `t12_off`: OFF. From them: in `t12_on` the log's seeded line holds the
      fourteen addresses in the order of the test's string and `fable2_native_impostors=on`; in
      `t12_off` the line equals log 657's character for character. The emulated shots at 60 s
      of the two, viewed: the hero and the dog (criterion 16's eyes on this binary).
- [ ] Frame map; README. Commit "Read-back list: the impostors' fourteen picture bases behind
      the switch".

### Task 13: The native levels and the content rule (pure; fitted to D1 and D2)

**Files:** `src/native/render/impostor_picture.h` (new, pure),
`tests/native/test_impostor_picture.cpp` (new; two lines in the runner),
`tools/xdk_sigmatch/impostor_pictures.py` (`levels`, `content`),
`tests/test_impostor_pictures.py`, frame map "The pictures: levels and content".

**Consumes:** `Decode1555Level` and `decode_1555` (Task 7); D1's and D2's dumped bases and
levels (the texture rows' `"mips"` members: level 1 of 128 x 128 at offset 0 of the level file,
level 2 at 32,768, level 3 at 40,960, as each row names them); DT's capture `054648` (made
without full read-back: the first picture a tree, five noise, eight empty).

**Produces** (the three constants are PROVISIONAL until this task's own fit step):

```cpp
// impostor_picture.h, namespace fable2::native::render
inline constexpr uint32_t kImpostorPictureSize = 256, kImpostorPictureLevels = 4;
inline constexpr uint32_t kImpostorRingTexels = 1020;            // the base level's outermost ring: 4 * 255
inline constexpr uint32_t kImpostorLevelAlphaCount = 2;          // PROVISIONAL: a texel is covered when this many of its four parents are
inline constexpr uint32_t kImpostorRingClearMin = 0;             // PROVISIONAL: of the ring's texels, at least this many have alpha 0
inline constexpr uint32_t kImpostorCoverMin = 1, kImpostorCoverMax = 65535;   // PROVISIONAL: covered texels of the base level
// Levels 1 to 3 (128, 64, 32 texels square, RGBA8) from the decoded base level: the rule this task fits
// (spec ruling 8). False, the three left empty, unless `bytes` is 256 * 256 * 4.
bool MakeImpostorLevels(const uint8_t* base_rgba, size_t bytes, std::vector<uint8_t> levels[3]);
struct ImpostorPictureStats { uint32_t ring_clear = 0, covered = 0; };
ImpostorPictureStats MeasureImpostorPicture(const uint8_t* base_rgba, size_t bytes);   // all zero for a size that is not the base's
constexpr bool ImpostorPictureUsable(const ImpostorPictureStats& s);   // ring_clear >= the minimum and covered inside its bounds
```

- [ ] Tests first (`tests/native/test_impostor_picture.cpp`), see them fail:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/impostor_picture.h"
#include <cstring>
#include <vector>
using namespace fable2::native::render;

int main() {
  const size_t n = size_t(kImpostorPictureSize) * kImpostorPictureSize * 4;
  std::vector<uint8_t> base(n, 0), levels[3];
  if (!MakeImpostorLevels(base.data(), n, levels)) return 1;                       // an empty picture: empty levels
  if (levels[0].size() != 128u * 128u * 4u || levels[1].size() != 64u * 64u * 4u || levels[2].size() != 32u * 32u * 4u) return 2;
  for (const auto& level : levels) for (uint8_t b : level) if (b != 0) return 3;
  if (MakeImpostorLevels(base.data(), n - 4, levels) || !levels[0].empty() || !levels[2].empty()) return 4;   // a short base: nothing
  if (MakeImpostorLevels(nullptr, n, levels)) return 5;
  for (size_t i = 0; i < n; i += 4) { base[i] = 132; base[i + 1] = 255; base[i + 2] = 8; base[i + 3] = 255; }   // one colour, covered everywhere
  if (!MakeImpostorLevels(base.data(), n, levels)) return 6;
  for (const auto& level : levels) {
    for (size_t i = 0; i < level.size(); i += 4) {
      if (level[i] != 132 || level[i + 1] != 255 || level[i + 2] != 8 || level[i + 3] != 255) return 7;      // five-bit values stay themselves
    }
  }
  // The alpha's rule at its threshold: a level texel with exactly kImpostorLevelAlphaCount covered parents is covered,
  // with one fewer it is not.
  auto clear = [&](uint32_t x, uint32_t y) { base[(size_t(y) * 256 + x) * 4 + 3] = 0; };
  const uint32_t parents[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  for (uint32_t k = 0; k < 4 - kImpostorLevelAlphaCount; ++k) clear(parents[k][0], parents[k][1]);            // block (0, 0): exactly the count stays
  for (uint32_t k = 0; k < 5 - kImpostorLevelAlphaCount && k < 4; ++k) clear(2 + parents[k][0], parents[k][1]);   // block (1, 0): one fewer
  if (!MakeImpostorLevels(base.data(), n, levels)) return 8;
  if (levels[0][3] != 255 || levels[0][4 + 3] != 0 || levels[0][8 + 3] != 255) return 9;
  // The content rule's counts, against a count made here texel by texel.
  uint32_t ring = 0, covered = 0;
  for (uint32_t y = 0; y < 256; ++y) {
    for (uint32_t x = 0; x < 256; ++x) {
      const bool a = base[(size_t(y) * 256 + x) * 4 + 3] != 0;
      covered += a ? 1u : 0u;
      if ((x == 0 || y == 0 || x == 255 || y == 255) && !a) ++ring;
    }
  }
  ImpostorPictureStats s = MeasureImpostorPicture(base.data(), n);
  if (s.ring_clear != ring || s.covered != covered || ring == 0 || ring > 8) return 10;   // (the texels cleared above lie on the ring's top rows)
  std::vector<uint8_t> empty(n, 0);
  s = MeasureImpostorPicture(empty.data(), n);
  if (s.ring_clear != kImpostorRingTexels || s.covered != 0 || ImpostorPictureUsable(s)) return 11;            // an empty picture is not used
  s = MeasureImpostorPicture(empty.data(), n - 1);
  if (s.ring_clear != 0 || s.covered != 0) return 12;
  ImpostorPictureStats tree;
  tree.ring_clear = kImpostorRingTexels;
  tree.covered = 20000;
  if (!ImpostorPictureUsable(tree)) return 13;                                                                 // a clear ring, a covered middle
  tree.covered = kImpostorCoverMax + 1;
  if (ImpostorPictureUsable(tree)) return 14;
  return 0;
}
```

      (Case 13's 20,000 covered texels stand for a tree: the fit step replaces the number by
      the median of D1's covered counts, and adds one case: a picture covered to its edge, as
      the one of case 10, is refused when the fitted ring minimum is above its eight clear
      texels.)
      `tests/test_impostor_pictures.py`: `make_levels` (the Python twin, the rule's parameters
      as arguments) on a made 8 x 8 base against hand-computed 4 x 4, 2 x 2 and 1 x 1 levels for
      each rule of the family below; `content_stats` on a made picture.
- [ ] **The fit.** `rtk python tools/xdk_sigmatch/impostor_pictures.py levels <D1> <D2>`: for
      every rule of the family {a level from the level above, or from the base; the colour the
      mean of the four parents' five-bit fields, or of the covered parents' alone, rounded to
      five bits; a texel covered when 1, 2, 3 or 4 parents are} the share of texels of each
      dumped level whose alpha bit equals the rule's, and the share of covered texels whose
      three fields are within 2 of 31. **Criterion 5**: the best rule's figures against "97% of
      the texels of each level" and "95% of the covered ones"; not met: the best fit is
      reported, the rule is taken all the same, and criteria 3 and 8 carry the consequence.
      `content <capture>...`: the ring's clear texels and the covered texels of every base of
      D1, D2 and of `054648`'s fourteen (one tree, five noise, eight empty): the three constants
      are set so that every picture of D1 and D2 passes and every noise and empty picture of
      `054648` fails, with a margin stated; if no such bounds exist, say so and stop (spec risk
      5: a noise picture would be drawn).
- [ ] Write `MakeImpostorLevels` to the fitted rule; the C++ against the Python twin on D1's
      bases through the test's tool mode (`FABLE2_1555_BIN` of Task 7, extended to write the
      three levels); tests green. Mark the provisional table's line.
- [ ] Frame map; commit "Impostor pictures: the native levels' rule and the content rule,
      fitted to the captures".

### Task 14: The pictures in the pass (`on`)

**Files:** `src/native/render/clay_pass.cpp` / `.h`, `src/native/render/clay_logic.h`
(`ClayStats`, the text), `src/native/render/scene_dump.h` (the pictures' file),
`src/native/fable2_native_render.cpp` (the availability, the dump's writer, the install line),
`tests/native/test_scene_dump.cpp`, `tests/native/test_clay_logic.cpp`,
`tools/xdk_sigmatch/impostor_pictures.py` (`used`), `tests/test_impostor_pictures.py`, README,
frame map "The pictures in the pass".

**Consumes:** Tasks 7, 10, 12, 13; `capture::ReadPhysical` (`guest_read.h` line 70: bounded,
null when the range is not mapped); `SampleHash` (`texture_residency.h` line 30);
`DumpFetch` (Task 1) and `SamplerIndex` (`texture_decode.h` 389); the texture cache's upload of
levels (`texture_cache.cpp` 393 to 420: a texture with `mip_levels`, `CopyBufferToTexture` a
level) as the pattern; `DecideImpostorPicture` (Task 9); `WriteSceneDump`
(`fable2_native_render.cpp` 526) and `ParseSceneDump`.

**Produces:**
- In the pass, `impostor_pictures_[capture::kMaxImpostorRecords]`: a 256 x 256
  `R8G8B8A8_UNORM` texture with four levels, its view, the base address it holds, the hash of
  the bytes it was made from and its `ImpostorPicture` state; made on the first frame that
  needs one, **never in `off` or `flat`**, outside the texture cache (whose dynamic rule would
  refuse a texture that changes every frame: `kDynamicStreak`).
- Each frame, for each record, in this order (the first that fails names the state, and **the
  record is then not drawn: never a flat quad in a picture's place**): the run has the pictures
  (`ImpostorPicturesAvailable`) and the record's `tf0` base is one of the fourteen
  (`ImpostorPictureBase(n)`), else `kBase`; `ReadPhysical(base, 131072)` whole, copied before
  it is judged, else `kUnread`; `SampleHash` of the copy: unchanged since the texture was made
  means the texture is used as it is; changed: `Decode1555Level` (false: `kUnread`),
  `MeasureImpostorPicture` and `ImpostorPictureUsable` (false: `kContent`),
  `MakeImpostorLevels`, the upload of four levels (a failure: `kUpload`). The sampler is
  `SamplerIndex` of the record's fetch.
- `ClayImpostorConstants::kept` stays `kImpostorKeepFirst` (the half part is Task 15's); the
  pixel program samples the picture where `flat` sampled the 1 x 1 texture: no program text
  changes unless Task 10's text needs it, and then the shader test's baseline case is run again.
- The pictures the pass used, for criterion 6: with `--fable2_native_scene_dump=on`, beside
  each `native_scene_<stamp>.f2ns` a file `native_scene_<stamp>.f2np`: `u32` magic `'F2NP'`,
  `u32` version 1, `u64 scene_frame`, `u32 count`, `u32 reserved`, then per drawn record
  `u32 base, u32 place, u64 hash` and 256 x 256 x 4 bytes of the base level as uploaded
  (`scene_dump.h`: `PackImpostorPictures`, `ParseImpostorPictures`, pure).
  `impostor_pictures.py used <f2np> <capture>`: each picture against the capture's dump of the
  same base in the paired frame and in the one before: the share of equal texels, and which
  frame it equals.
- Stats and text: `Impostors: on, drawn 14 of 14 (928 quads), pictures 14 (refreshed 14) | not
  drawn {picture: content: 1}`.

- [ ] Tests first: `tests/native/test_scene_dump.cpp` (a packed file of two pictures parses to
      its two heads and bytes; a file one byte short, a count of 33 and a wrong magic are
      refused); `tests/native/test_clay_logic.cpp` (CHECK 819: the text of a made `ClayStats`
      with one `picture: content`); `tests/test_impostor_pictures.py` (`used` on a made file
      and a made capture folder: equal, one texel off, the base missing from the capture).
- [ ] Implement; suites green; build.
- [ ] NEEDS_RUN, in this order: `WARP-ON`: WARP with the suffixes POST, ON (240 s). If its log
      says `drawn 0` because the software adapter's pictures fail the content rule, once more
      with READBACK added; if the step still draws nothing there, criterion 14's `on` half is
      "not decidable on the software adapter" and is said so, and the debug layer's verdict
      stands on the objects that were made. Only after a clean run: `V-BRIDGE-ON`: BRIDGE with
      `-GameArgs BASE,PAIR,ON`, and `V-BRIDGE-OFF`: BRIDGE with `-GameArgs BASE,PAIR`, one
      session. Copies as `t14_*`.
- [ ] From the runs: **criterion 14 (`on`, without the half part)**; the Impostors line;
      **criterion 6**: `impostor_pictures.py used` on the pair (bound: each picture equal on at
      least 98% of its texels to the capture's dump of that frame or the one before; which is
      reported: the pictures' age); the shots at 44 s and 86 s viewed and described honestly
      (tree shapes on the ranges; any picture that is noise, a box, upside down or of the wrong
      colour; the red-and-blue order of Task 7 shows here if it is wrong); first reading of
      criteria 7 and 8.
- [ ] Frame map; README. Commit "Native impostors: the game's pictures, read back, decoded and
      held per model".

### Task 15: The half part under the coverage's switch

**The controller may strike this task** (spec ruling 13) if Task 6's share of impostor pixels
with one sample of two is below 5%: then `on` keeps the first sample's rule under either value
of `--fable2_native_coverage`, the frame map says so, and Task 16 reports criterion 8 beside
that share.

**Files:** `clay_pass.cpp` / `.h`, `clay_logic.h`, `fable2_native_shaders.h` (only if the pixel
program needs the half alpha written), `tests/native/test_clay_logic.cpp`,
`tests/native/test_native_shaders.cpp`, frame map "The impostors' half part".

**Consumes:** `ImpostorKept`, the keep words, `PlanImpostorPlace` (Task 9); `PlanCoverageOrder`
and `CoverageItem` (`clay_logic.h` 1830 to 1869); the early tail and its two steps (ledger
ruling 60 of sub-project 11; `clay_pass.cpp` 3959 to 3978: `half_depth_step`, `draw_tail`).

**Produces:** with `--fable2_native_coverage=on` and the pictures drawn: the step in its place
draws `kImpostorKeepFull`; the half-covered pixels are drawn in the early tail, **every model's
half depth first, then every model's half colour**, after the records' own halves:
`impostor_half_depth_` (the pixel program with `kImpostorKeepHalf`, no colour written, the depth
test and write) and `impostor_half_colour_` (the same kept set, source alpha over inverse source
alpha at the constant alpha one half, the depth test without a write): three pipelines with the
one of Task 10. `CoverageItemsWithImpostors(items, count, impostors_at)`: the frame's coverage
items with one early item at the step's place, so that `PlanCoverageOrder` gives a tail whenever
the step has a half part and the sky step lies before it. With the coverage off, in `flat`, and
with the task struck: `kImpostorKeepFirst`, one pipeline, as before. The known error stays and
is written down: where a fading mesh's half part and an impostor's meet (trees between 50 and
54 units) the tail blends twice.

- [ ] Tests first (`test_clay_logic.cpp`, CHECK from 860): for every arrangement of up to five
      items of the three kinds with a blended record at each place, an impostor step at each
      index and every sky and depth place: with the items of `CoverageItemsWithImpostors` the
      tail exists, lies at or after the step's place and at or before the sky's, the depth's
      and the first late record's; without a half part the order is `PlanCoverageOrder`'s of
      today, value for value. The shader test: the half colour's program writes alpha 0.5 and
      the three kept sets compile from one text (case 264); case 260's baseline still holds.
- [ ] Implement; suites; build. NEEDS_RUN: `WARP-ON-COV`: WARP with POST, COV, WIND, ON; then
      `V-BRIDGE-ONCOV`: BRIDGE with `-GameArgs BASE,PAIR,COV,WIND,ON` and its off-run BRIDGE
      with `-GameArgs BASE,PAIR,COV,WIND`. **Criterion 14's `on` run with the coverage on.** The
      shots viewed: the ranges' edges (softer than Task 14's, no dark outline, no box).
- [ ] Frame map; commit "Native impostors: the half-covered pixels at one half in the early
      tail".

### Task 16: Validation of `on` (12c) and its documents

**Files:** frame map section 19 ("Validation (12c)", "Limits (12c)"), the spec's notes,
evidence scripts (untracked).

- [ ] Runs by the controller (each group one session and one binary):
      1. `v_bridge_off`, `v_bridge_on`, `v_bridge_offcw`, `v_bridge_oncw`: BRIDGE with
         `-GameArgs BASE,PAIR`; with ON; with COV, WIND; with COV, WIND, ON.
      2. `v_third_offcw`, `v_third_oncw`: THIRD with `-GameArgs BASE,PAIR,COV,WIND` (+ ON).
      3. `v_bridge_sun_off`, `v_bridge_sun_on`: BRIDGE with `-GameArgs BASE,PAIR,SUN` (+ ON).
      4. `v_band_offcw`, `v_band_oncw`, `v_band_on`: D3's line with `-GameArgs
         BASE,PAIR,COV,WIND` (+ ON), and with `BASE,PAIR,WIND,ON`. Not made without a D3.
      5. `v_sun_off`, `v_sun_on`: D4's line with `-GameArgs BASE,PAIR,SUN` (+ ON). Not made
         without a D4.
      6. **Timing, each run alone, the usual monitor, one session**, in this order: `time_off_1`,
         `time_flat_1`, `time_on_1`, `time_on_2`, `time_flat_2`, `time_off_2`: TIME with
         `-GameArgs BASE,POST,EXPO,SUN,WIND,COV` (+ FLAT; + ON); then `time3_off`, `time3_on`:
         TIME-THIRD with the same arguments.
- [ ] **Criterion 6** (run 1's `on` pair, as Task 14); **criterion 7** in `on` (bridge, third
      spot); **criterion 8**: with the pictures, the coverage and the wind on, native over game,
      mean linear luminance: far mountains left, centre, right and far rock below the ridge each
      within 0.93 to 1.07 and nearer to 1 than half its off-run distance; on the off-run's gap
      pixels within 0.92 to 1.08 with red, green and blue each inside that band; with the
      coverage off: reported beside open question 8's share; the third spot's two regions
      against that run's own "before". **Criterion 9** in the same pairs.
- [ ] **Criterion 10**: `region_compare.py` on the timing runs' shots (57 s native over the mean
      of 36 s and 96 s emulated): the four bridge regions and the third spot's two within 0.90
      to 1.12; a region whose emulated pair drifts by more than 5% is "not decidable" on shots
      and stands on criterion 8.
- [ ] **Criterion 11's second part** (run 4): criterion 7's measure on the pixels between 40
      and 80 units; native over game on the band's pixels with the coverage on and off, the
      known error of Design 10 named. **Criterion 12** (runs 3 and 5), as Task 11, in `on`.
- [ ] **Criterion 15, whole**: valid only at shot width 1942 and with nothing beside the runs:
      no steady 150-frame native window below 30.0 fps in any of the six; the mean frame over
      steady native windows: `flat` over `off` at most 0.5 ms, `on` over `off` at most 2.5 ms,
      each only if the two runs of a configuration agree within 1.0 ms (else "not decidable");
      the capture medians; the third spot's pair reported. If the decode and upload alone break
      the bound (`on` over `flat`): the budget of four pictures refreshed a frame (spec ruling
      17) is proposed to the controller with its figures, not built here.
- [ ] **Criterion 16** restated from Task 2 with this binary's emulated shots (run 6's `on`
      runs against its `off` runs: the hero's shirt, the dog, the four far regions).
- [ ] Each of criteria 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16 with its evidence file and an
      exact verdict word; the default stays `off`; the recommendation between `flat` and `on`
      written beside the evidence, with what `on` adds to the SDK's start-up list said.
- [ ] Frame map "Validation (12c)", "Limits (12c)"; commit "Impostor validation (12c): the
      pictures' verdicts". Then the phase's review.

---

## Closing

### Task 17: Documents, the closing table, the whole-branch reviews

Made whether the sub-project closes at `on` or at `flat`. Every step below is a checkbox of its
own, and the task's report quotes the list with each box's commit or "nothing to change:
<why>". **No step is folded into another.**

- [ ] 1. **Spec** (`docs/superpowers/specs/2026-10-11-native-renderer-distant-trees-design.md`):
      the status line; "Implementation notes (deviations)" with the closing table of all 17
      criteria (verdict word, figure, file); every open question 1 to 12 with what became of
      it; every line of "Limits known beforehand" with what the runs showed.
- [ ] 2. **This plan**: an "As executed" paragraph under each task whose runs, names, numbers
      or order differed from what is written (the provisional table's "As fixed" lines among
      them).
- [ ] 3. **Frame map**, section 19 "Distant trees: the far woods' impostors": its subsections
      in this order, each present or named as not built: "Discovery", "The cost pair",
      "Discovery captures", "The formulas against the captures", "The pictures' format",
      "Impostor records", "The impostor step", "Validation (12b)", "Limits (12b)", "The
      read-back list", "The pictures: levels and content", "The pictures in the pass", "The
      impostors' half part", "Validation (12c)", "Limits (12)", "Follow-ups (12)".
- [ ] 4. **Frame map, stale statements marked where they stand**: line 1459 ("billboards,
      particles ... a non-goal") and 1460; the literal table's line of `129F` (2655); "beams
      pass where the game's far woods stand" (about line 6897); section 18's limits that name
      the far ranges' excess; the list of undrawn in-scene rows (R 2's table) wherever the frame
      map repeats it.
- [ ] 5. **`docs/native-renderer/lighting-formulas.md`**: the part "The impostors" complete
      (position with the block of instructions 13, 14 and 28 to 57 term by term; pixel; fog;
      what stands on inference).
- [ ] 6. **README**: the row of `--fable2_native_impostors=off|flat|on` in the switches' table
      (after `--fable2_native_coverage`, line 99); the F3 paragraph's count of native lines
      (line 102: one more, `Impostors:`); a paragraph "Impostor records" and one "The impostor
      step" beside the quad records' (lines 124 to 132); the read-back list's paragraph if
      phase 12c was built.
- [ ] 7. **`docs/native-renderer/user-checks.md`**: a heading "Sub-project 12 (native-renderer-
      distant-trees)" and the checks **appended without renumbering any earlier check**, from
      93 (the file's last is 92; if 93 is taken by then, from the next free number, and the "As
      executed" paragraph says which): 93 the ridges' tree line, native against emulated, in
      split view (trees where the emulated half has them, of their size, no boxes; in `flat`:
      flat shapes of the woods' colour); 94 a walk on which a tree crosses 48 to 56 units: no
      popping, no tree doubled or missing as mesh and impostor change places; 95 the beams
      against far woods with `--fable2_native_sun=on`; 96 the far woods in another region and
      at another time of day (and the Impostors line there: `over-limit`, `fetch`, `picture:
      base`); 97 everything off is the picture of before, and the cost by eye.
- [ ] 8. **`docs/native-renderer/roadmap.md`**: item 2 ticked with **what was and was not
      achieved** (the depth gap's and the regions' figures; `flat` or `on`; the criteria not
      met as written); "the cubes at depth 20.0024" taken out of it with R 4's finding (the
      lake's level in the sun layer's depth; nothing to build while the lake's meshes are drawn
      with depth); **option B (the pictures rendered natively) as the next item if phase 12c
      was not entered or its criteria failed**, with what this sub-project hands it (spec
      Design 13); new or kept: the two unidentified pairs (`43ED` / `7F65`, `7D1C` / `983E`),
      the ground-fog block, the far woods at night and in other regions, the levels read from
      the game if a cheap read-back ever exists, the red dog's named neighbour; the line "User
      checks 40 to 92" with its new end.
- [ ] 9. **`docs/native-renderer/vs-transforms.json` and `ps-albedo.json`**: unchanged (no
      entry is made for `129F` or `4B1D`: spec rulings 10 and 14); the closing says so in one
      line of the frame map, so that nobody looks for one.
- [ ] 10. **`plans/hero-dog-shifty-texture-mipmap.md`**: one dated note under its finding of
      the every-frame 256 x 256 format-3 texture: it is impostor 0 (frame map section 19).
- [ ] 11. **The research** (`.superpowers/sdd/distant-trees-research.md`, untracked) and
      `.superpowers/sdd/foliage-research.md`'s table A1 ("reflection views"): a dated line at
      each place the spec's "What the research has differently" names.
- [ ] 12. **The ledger**: the tasks' table complete; every ruling made during execution; the
      closing summary with the question the next sub-project needs answered (the default; what
      is pushed).
- [ ] 13. Commit "Distant trees closing: the criteria's verdicts, the documents" (tracked files
      by pathspec).
- [ ] 14. **The whole-branch code review** (a fresh reviewer, read-only, the branch against
      master `d03f0be`): the five review-focus items first, then guest reads, the frame's
      recheck, the switch off. Findings are fixed test-first in a fix wave; a fix that changes
      bindings, targets or shader text gets its own software-adapter run before any real-GPU
      run.
- [ ] 15. **The documents review** (a fresh reviewer): every number against its file; every
      "met" against the criterion's own words; the closing table against the tasks' reports;
      steps 1 to 12 of this list, one by one.
- [ ] 16. After the fix wave: the suites, the build, the runs the fixes call for, steps 1, 2, 3
      and 12 again where a figure changed. Commit.

---

## Self-review: every criterion and the task that decides it

| Criterion | Decided in | By |
|---|---|---|
| 1 formulas | Tasks 4, 5 (made inputs, mutations); Task 6 (`rows`) | `impostor_check.py formulas`, `pixel`, `rows` |
| 2 records | Task 8 (first reading), Task 11 | `impostor_check.py records`; `static_assert(sizeof(DrawRecord) == 616)` |
| 3 positions and cover by the replay | Task 6 | `foliage_replay.py impostors`, with its two controls |
| 4 the decoder | Task 7 | the native test; D1's fourteen dumps against the Python twin |
| 5 the levels | Task 13 (phase 12c) | `impostor_pictures.py levels` |
| 6 the pictures the pass used | Tasks 14, 16 (phase 12c) | `impostor_pictures.py used` |
| 7 the depth gap | Task 11 (`flat`), Task 16 (`on`) | `DT/depth_gap.py` on off and on pairs |
| 8 the far regions before post | Task 16 (Task 11: reported in `flat`) | the pairs' stored scenes |
| 9 the shared far ground | Tasks 11, 16 | the same pairs |
| 10 after post, on shots | Task 16 (Task 11: reported in `flat`) | `region_compare.py` on the timing runs' shots |
| 11 the dissolve band | Task 6 (the replay in D3), Tasks 11 and 16 (V-BAND) | `--band 46 58`; the pairs |
| 12 the sun layer against the far woods | Tasks 11, 16 | the layer planes of the bridge pair with the sun on; D4's view |
| 13 off is today's picture | Task 9 (CHECK 812), Task 10 (case 260), Task 12 (returns 20 to 23), Task 11 (run OFF) | tests; log 657 |
| 14 errors | Task 10 (`flat`), Tasks 14 and 15 (`on`) | the software-adapter runs; "the step drew" |
| 15 cost | Task 11 (`flat` half), Task 16 (whole) | the timing runs, alone, 1942 wide |
| 16 the emulated picture under the read-back | Task 2 (Tasks 12, 16 on the later binaries) | `region_compare.py` on emulated shots |
| 17 the evidence rule | every task; Task 17's two reviews | - |

If phase 12c is not entered, criteria 5 and 6 read "not built", criteria 8 and 10 are reported
in `flat` and not judged, criterion 14 has its `flat` run alone and criterion 15 its `flat`
half; the closing table says so for each.

## The runs, in order

1. COST: four runs (Task 2). First of all runs; any binary of this branch, one for all four.
2. D1, D2, D3T, D4a, D4b (Task 3; Task 1's build).
3. R-BRIDGE, R-THIRD, R-OFF (Task 8).
4. WARP-FLAT, then V-BRIDGE-FLAT and V-BRIDGE-OFF (Task 10).
5. Task 11's seven groups: the pairs at the bridge, the third spot, the bridge with the sun,
   D3's and D4's views; OFF; the timing session (four runs and the third spot's pair).
6. Only after "passes": `t12_on`, `t12_off` (Task 12); WARP-ON, V-BRIDGE-ON and its off-run
   (Task 14); WARP-ON-COV, V-BRIDGE-ONCOV and its off-run (Task 15); Task 16's six groups, the
   timing session of six runs and the third spot's pair among them.
7. Whatever the fix wave of Task 17 calls for.
