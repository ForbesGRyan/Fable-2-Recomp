# Native renderer water (the lake's surface with a native reflection): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. One fresh
> implementer and one fresh reviewer per task; steps use checkboxes.

**Goal:** the lake's surface stands in the native view as the game draws it: the blended draw
`BEAD84BD72072E0E` / `A17D8AEC3A817D45` by the game's own formulas (two scrolling normal fetches, a
reflection lookup, a base colour by fresnel, the sun's glint, fog, exposure, the shore masks'
alpha), late in the frame over the real lake bed, with the reflection taken from a native
256 x 256 target that the pass renders itself: first the sky (phase 13b), then the far terrain
(phase 13c, on its entry conditions). The dead opaque twin `F6D98C7B4D98438B`, today's flat-clay
placeholder, is left out in a frame whose water is drawn and only there. Behind
`--fable2_native_water` (default off). Phase 13a makes the oracle without the game and without a
renderer change.

**Architecture:** a water draw keeps making the draw record it makes today (616 bytes, untouched).
On the `no-albedo` branch of the material's lookup, in a frame with the water wanted, a draw of the
pair is read into a bounded side block of the frame's scene (at most 64 records of both pixel
shaders, linked to their draw records by sequence number; one refused record and the scene holds
none). The pass makes one verdict a frame; only with `drawn` it leaves the `F6D9` records out of the
pre-pass and the colour pass, renders one 256 x 256 target for each slot-13 base of the frame (at
most 4) with the sky step's own pixel program under a mirrored camera derived on the CPU from the
frame's view-projection, the draw's `c31.xy` and the mesh's height, and draws the `A17D` records in
one step with a layout and programs of its own, blended, depth tested and written, at the first
surface record's place. Every formula is a Python reference held to the microcode by machine check
with mutations, a single-precision C++ mirror pinned to it, and HLSL text held to the reference by a
line-by-line translation.

**Tech stack:** C++ (capture in `src/native/capture`, pass in `src/native/render`, the switch in
`src/native/fable2_native_render.cpp`), HLSL in `src/native/fable2_native_shaders.h`, Python tools in
`tools/xdk_sigmatch` (numpy), tests in `tests/` and `tests/native/`.

**Spec:** `docs/superpowers/specs/2026-10-12-native-renderer-water-design.md` (its Design, ruling and
criterion numbers are cited; amended for ledger rulings 26 and 27). Ledger:
`.superpowers/sdd/2026-10-12-native-renderer-water/progress.md` (rulings 1 to 28). Research:
`.superpowers/sdd/water-research.md` ("R" with a section; read it before any task; its scripts and
outputs and the spec author's `s_*.py` are in `out/native-evidence/water/`, "WE" below). "FM" is
`docs/native-renderer/frame-map.md`. "CAP `<stamp>`" is
`out/build/win-amd64-release/logs/native_discovery_20261011_<stamp>.jsonl`; the captures in hand,
all made under `--readback_resolve=full` with a stored native scene: D1 = `155547` (bridge), D2 =
`155731` (third spot), `221449` (bridge, the fix wave's binary), `155929` (trees), `160104` and
`160251` (sun side).

## Global Constraints

- Branch `native-renderer-water`. **No stash, reset, push, amend or rebase by an agent**; no branch
  switch. **Never stage `fable_2_manifest.toml`; never `git add -A`**; `*.ps1` is git-ignored and
  needs `git add -f`. Every commit is a pathspec commit: `rtk git add <paths>` then
  `rtk git commit --only -F <message file> -- <paths>`. The commit trailer is exactly these two
  lines after a blank line: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`. New files have LF line
  endings.
- **Every shell command is prefixed with `rtk`** (`rtk git ...`, `rtk python ...`,
  `rtk clang++ ...`, `rtk .\build.cmd ...`). **No Windows backslash path inside an inline Python or
  heredoc string**: write forward slashes there, or put the script in a file.
- **`DrawRecord` stays 616 bytes** (`static_assert` in `src/native/capture/draw_record.h`, lines 137
  and 138); a water draw keeps making the draw record it makes today (a `no_albedo` record: the
  frame's fog block is read at the first `F6D9` draw, and that stays).
- **No SDK change** (`thirdparty/rexglue-sdk`), **no read-back** and no address added to any
  read-back list by this sub-project (ledger rulings 1 and 2). Nothing is read from the game's
  reflection pictures by the product. A seeming need: stop and report.
- **The `D1CC` cubes are not drawn** (ledger ruling 26): no cube record, no program in the sun
  layer, the Sun line unchanged. **The surface tests and writes depth** (ledger ruling 27); the
  layer's depth place is not moved.
- **Game launches wait for the user's leave** (ledger ruling 7), which is still awaited: **phase 13a
  (Tasks 1 to 7) needs no launch.** **The game starts only through `tools/drive_game.ps1`**, with
  `-BootSave Hero000`. **Implementers never launch the game and never pass `-UserPresent`**: a task
  that needs a run ends that step with `NEEDS_RUN`, the exact lines below, what each run must
  produce and where it wants it copied; the controller makes the run and its evidence is labelled
  `RUN UNCHECKED`. Every run is checked for the freeze signature before a number or picture is used.
- **Run shape**: `-Total` at most 100 s (240 s on the software adapter); **no shot within 3 s of a
  view switch**; **one stick entry holds at most 60000 ms**; `--mouse_look=false` in every run's
  arguments; only the plan tokens `tools/drive_game.ps1` has (`<s>:VIEW=off|split|overlay|native`,
  `<s>:DISC=<frames>/<every>`, `<s>:LS=x/y/ms`, `<s>:RS=x/y/ms`). The camera stands for at least 5 s
  before a capture; **a view that exists only while a stick is held has its shots and its capture
  inside the hold** (the sunward look: `41:RS=0/100/59000`).
- **What a run's arguments must say**: `--fable2_exposure_readback=on` wherever shots are judged;
  `--readback_resolve=full,--native_render_collect_stats=true` wherever the game's own scene and
  depth of the dump's frame are needed (**a full read-back run is said to be one at its run line**);
  `--fable2_native_scene_dump=on` wherever a stored native scene is needed. `check_discovery.py` is
  run with `--rows-only` on a capture whose run did not log the vertex bindings.
- **Timing runs count only at shot width 1942, the window on the usual monitor, each made alone**
  (no agent, no analysis, no build, no test, no other game beside it), with the head's uncapped
  default (`seeded guest_vblank_pacing=false` in the log) stated. **This is said again at each
  timing run.** Every other run is "no timing evidence"; a read-back run and a run on the other
  monitor are never timing runs (read-back and the swap depend on the monitor the window stands
  on).
- **Before any real-GPU launch of a build that changes bindings, targets or shader text: one clean
  240 s software-adapter run with the debug layer**, 0 `d3d ERROR` lines, **and the log shows the
  step drew** (`Water: drawn 12 of 12, targets 4`) and the shot shows the lake. Tasks 12 and 17 are
  such builds.
- **No step whose only output is a picture nobody would turn on.** The sky-only reflection is
  judged on shots in its first real-GPU run (Task 12's EARLY LOOK) before any validation work is
  spent on it; if it looks wrong in a way the criteria would not catch: stop and report to the
  controller.
- **Antivirus rule**: test executables are built only under `out\tests\runtime-unit-tests\`. A
  binary that is removed, locked or refused: stop and report BLOCKED with file, time and message; no
  retry loop, no rename, no change of options. While the locked
  `out\tests\runtime-unit-tests\native_shaders.exe` exists, the suite runs through a scratch copy of
  the runner in which only the shader test's output name differs
  (`out\tests\run_native_tests_t<N>.cmd`, generated from the present script each time).
- **One builder at a time** (one process building the game or the test executables in this tree;
  another Claude session may share the machine: detect a running build or game before starting
  one). **Reviewers are read-only.** **A mutation check is run alone**: on a scratch copy, never on
  a tracked file, with no other test, build or game running.
- **Fail-closed rules (the spec's, collected; binding as written, the cubes' struck):**
  - No value read from the guest reaches a shader without having been read whole, a finiteness
    check and a bound, **and only the components a formula reads are judged or touched** (unread
    components hold garbage: `c129.z` is no number, `c129.w`, `c139.w`, `c140.w` are 1e22 to 1e29).
    Reasons, counted in the frame's scene, on F3 and in the log: `water-record {unread, shader,
    literal, fetch-list, register, scale, state, fetch, shadow-term, fog, over-limit}`; the frame's
    verdict `{off, no-records, light, opaque-live, pair, plane, view, reflection, texture, order,
    pipeline, slot, scale}`; in phase 13c `reflection: terrain`.
  - **The frame's water is whole or it is today's picture, record for record. The dead draw is left
    out only in a frame whose water is drawn.**
  - At most 64 water records, 4 targets, 256 reflection draws and 8 views a frame.
  - The surface is drawn by the formula, with a target that holds at least the sky, or not at all:
    no flat colour, no empty picture, no sky colour "along the ray" in the fetch's place.
  - A target's terrain is the game's own view's, whole, under the derived camera, or the target is
    the sky's alone.
  - With the switch off nothing of this sub-project is reached but one load and one branch on the
    `no-albedo` path.
- **With `--fable2_native_water=off`** (the default; also `false`, `0`): no record, target, texture,
  slot or program is made, the frame's slot plan is today's byte for byte, and every compiled
  program of the pass equals today's.
- **A formula that does not close draws nothing** (spec ruling 11): if Task 4's `rows` check fails
  on any captured row, or Task 6 cannot decide criterion 4 on the pixels of `0x13501000`, Tasks 10
  to 14 are not started; Task 9 (the capture) is still built and the sub-project ends at the
  capture and the formula's report.
- **New targets and textures are resolution-independent**; nothing of this plan takes a size from
  the window. Texel sizes reach a shader as constants.
- Tests first where a pure function can hold the rule; a Python reference, machine-checked against
  the shader dumps, and a C++ mirror pinned to it come before any shader text.
- Evidence goes to `out/native-evidence/water/` (a task's files are named `t<N>_...`); every number
  names its file; the labels [measured], [microcode], [code], [inferred], [unknown] are kept where
  a fact is repeated; a verdict is "met", "not met as written", "not decidable" or "by a
  substituted measure"; every before-and-after figure pairs an off-run and an on-run of one
  session, one binary and one plan (spec ruling 24); a threshold marked *(no precedent)* in the
  spec is reported with the figure reached, whatever it is.
- New switch: `--fable2_native_water` = `off` (default; also `false`, `0`), `on` (also `true`,
  `1`). New reason strings: those of the fail-closed rules above, exactly.

**The workspace's rule files** (`.superpowers/sdd/2026-10-12-native-renderer-water/`:
`global-constraints.md`, `implementer-rules.md`, `reviewer-rules.md`) were copied from an earlier
sub-project and bind every task. Where they name another sub-project, read: branch
`native-renderer-water`; plan and spec the two files above; research
`.superpowers/sdd/water-research.md`; the rules folder
`.superpowers/sdd/2026-10-12-native-renderer-water/`; the evidence folder
`out/native-evidence/water/` (previous: `out/native-evidence/distant-trees/`; FM section 19 names
its files); the plan's "Additions" are this plan's run lines. Their "only while the user is away"
binds agents as written; the controller's runs follow ledger ruling 7. The environment paragraph of
`global-constraints.md`, verbatim:

> Environment: PATH begins with `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin`,
> then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin`.
> Game build: `& .\build.cmd -release fable_2` (no `fable_2.exe` running; never kill one you did not
> start). Python tests: `python -m unittest discover -s tests -p "test_*.py"`. Native tests: the
> implementer rules' section "Antivirus on this machine" (test executables only in
> `out\tests\runtime-unit-tests\`; the shader test under another output name through a scratch copy
> of the runner while the locked file exists; any binary removed, locked or refused: stop and report).
> Start the game with `-BootSave Hero000` (world from about 23 s, views from 32 s, captures from 33 s).

**Commands.** Python suite: `rtk python -m unittest discover -s tests -p "test_*.py"`; one file:
`rtk python -m unittest tests.test_water_formulas -v`. Native suite: `rtk cmd /c
tests\run_native_tests.cmd` (or the scratch copy, above); one test: `rtk clang++ -std=c++23
tests\native\<file>.cpp -o out\tests\runtime-unit-tests\<name>.exe` then `rtk
out\tests\runtime-unit-tests\<name>.exe` (`test_clay_logic.cpp` needs the runner's own flags, line
51 of `tests/run_native_tests.cmd`: `-fsanitize=float-cast-overflow
-fsanitize-trap=float-cast-overflow -Wl,/STACK:16777216`; the shader test `-ld3dcompiler`, line
24). A new test executable is entered in `tests/run_native_tests.cmd` as two lines before the final
`exit /b 0`, in the form of its last two (`clang++ -std=c++23 "%~dp0native\test_<name>.cpp" -o
"%OUT%\<name>.exe" || exit /b 1`, then `"%OUT%\<name>.exe" || exit /b 1`). Game build: `rtk
.\build.cmd -release fable_2` (no `fable_2.exe` running; never kill one you did not start).

**Run lines** (after `tools\drive_game.ps1 -BootSave Hero000`; the controller adds `-UserPresent`;
`-GameArgs` is a list of quoted elements, **each element in its own quotes** because one of them
holds a comma; `BASE` is `"--fable2_native_render=true","--mouse_look=false"`):

| Name | Arguments |
|---|---|
| BRIDGE | `-Total 90 -Shots "36,44,74,86" -Plan "32:VIEW=off,40:VIEW=native,48:DISC=2/1,70:VIEW=off,82:VIEW=native"` |
| THIRD | `-Total 96 -Shots "44,50,80,92" -Plan "33:LS=0/-100/3500,40:VIEW=off,46:VIEW=native,54:DISC=2/1,76:VIEW=off,88:VIEW=native"` |
| SUNHELD | `-Total 100 -Shots "47,53,84,96" -Plan "33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/59000,43:VIEW=off,49:VIEW=native,57:DISC=2/1,80:VIEW=off,92:VIEW=native"` |
| EDGE-1 | `-Total 100 -Shots "62,68,96" -Plan "33:LS=-36/93/20000,58:VIEW=off,64:VIEW=native,70:DISC=2/1,92:VIEW=off"` |
| EDGE-2 | `-Total 100 -Shots "63,69,97" -Plan "33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/2500,44:LS=77/63/12000,59:VIEW=off,65:VIEW=native,71:DISC=2/1,93:VIEW=off"` |
| EDGE-3 | `-Total 100 -Shots "63,69,97" -Plan "33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/16000,59:VIEW=off,65:VIEW=native,71:DISC=2/1,93:VIEW=off"` |
| TIME | `-Total 100 -Shots "36,42,57,60,96" -Plan "32:VIEW=off,39:VIEW=native,90:VIEW=off"` |
| TIME-CAP | `-Total 100 -Shots "36,42,57,60,96" -Plan "32:VIEW=off,39:VIEW=native,90:VIEW=off,91:DISC=2/1"` |
| OFF | `-Total 70 -Shots "40,50,60" -Plan "32:VIEW=native,45:VIEW=split,55:VIEW=off" -GameArgs BASE` |
| WARP | `-Total 240 -Shots "225" -Plan "32:VIEW=native"` with `-GameArgs BASE,"--d3d12_adapter=-2","--d3d12_debug=true","--fable2_native_d3d_messages=true"` and the suffixes the task names |
| WARP-DUMP | `-Total 240 -Shots "225" -Plan "32:VIEW=native,150:DISC=1/1"` with WARP's arguments, DUMP and the suffixes the task names |

Suffixes (elements appended to `-GameArgs BASE`): READBACK
`"--readback_resolve=full","--native_render_collect_stats=true"` (**full read-back**); POST
`"--fable2_native_post=on","--fable2_native_bloom=on"`; SUN `"--fable2_native_sun=on"`; WIND
`"--fable2_native_wind=on"`; COV `"--fable2_native_coverage=on"`; DUMP
`"--fable2_native_scene_dump=on"`; EXPO `"--fable2_exposure_readback=on"`; WATER
`"--fable2_native_water=on"`. PAIR is `POST, READBACK, DUMP, EXPO`: a capture under full read-back
in the native view, whose first frame the stored native scene pairs with, with shots that may be
judged.

What the lines hold to: BRIDGE, THIRD and the three EDGE lines stand from their last stick entry
(none; 36.5 s; 53 s; 56 s; 56 s) to their capture (48; 54; 70; 71; 71 s), at least 5 s, and leave
the capture 22 s before the next view switch, as BRIDGE does; every shot
is at least 4 s after a view switch, except TIME's shot at 42 s (3 s after the switch at 39 s, as
logs 654 to 657 and 690 to 693 have it). SUNHELD's look exists only while the stick is held
(sub-project 11, FM section 18): its hold runs from 41 s to the run's end at 100 s (59000 ms), and
its four shots and its capture lie inside it. BRIDGE, THIRD, SUNHELD, TIME, TIME-CAP and OFF are
the lines of sub-project 12's plan, which ran as written. **The EDGE lines are guesses and are
labelled so** (Task 14 says where each comes from and what replaces it). A capture of two frames at
`/1` has finished within 20 s in every earlier run of these plans; a run whose capture file holds
fewer than two `"frame"` rows is said to hold so and is made again. WARP-DUMP's capture of one
frame may not finish on the software adapter: Task 12 says what then.

## Review Focus

The five failure modes most likely to pass unnoticed, each pinned by a test in its owning task:

1. **A formula check that cannot fail, or a measure that cannot tell** (a tolerance that passes a
   wrong term; made inputs on which a branch is never taken; the picture's orientation and channel
   order "decided" by a figure that reads 30.3% one way and 28.9% the other; a mirror checked
   against itself). Owner: Tasks 2, 3, 4, 6. Pin: `test_mutations_fail` and the floors of
   `check_pixel` in `tests/test_water_formulas.py` (killed, skipped and reached pixels each at least
   a tenth of the trials; both bools in both states; distances on both sides of `1 / c140.x` and of
   the fog's start, each at least a fifth); criterion 4's two controls (the picture upside down;
   red and blue exchanged), each of which must miss a bound **or the measure is said not to
   discriminate and nothing is built on it**; pins generated from the Python reference alone
   (`water_pins.py`), never from the C++ mirror.
2. **The dead draw removed where it is not dead, or removed without the surface** (a hole to the
   bed in the middle of the lake). Owner: Tasks 2, 10, 12. Pin: `water_check.py dead` parts (a) to
   (c); `WaterOpaqueDead`'s test at the margin (CHECK 840); `DecideClayWater`'s exhaustive test and
   `WaterLeavesOut` (CHECK 843: an opaque record is left out iff the verdict is `drawn`, for every
   verdict); `FrameBuilder`'s test that one refused record of either kind leaves the scene without
   any (Task 9, returns 222 to 226).
3. **A register component that no formula reads reaching a result** (IEEE arithmetic has no
   zero-product rule: one garbage component in a dot product makes the pixel no number). Owner:
   Tasks 4, 9, 10, 11, 12. Pin: `water_check.py rows` ("no component a formula reads is not finite"
   counted apart from the bound); `MakeWaterRecord` accepting a record whose unread components are
   no numbers and refusing one read component that is (Task 9, returns 20 to 24);
   `tests/test_water_hlsl.py`'s made inputs in which every unread component is no number; the
   shader test's "the pixel program reads no unread component" (case 55, part (e)).
4. **A switch that is off and still costs or changes something** (the slot plan; a program's
   bytes; `k_8` or `k_DXN` entering `TexFormatFromXenos` and texturing records that are
   `format-unsupported` today; a per-draw read outside the `no-albedo` branch; the discovery
   additions reaching a product path). Owner: Tasks 1, 5, 9, 10, 11, 12. Pin:
   `TexFormatFromXenos(2)` and `(49)` still `kUnsupported` and `DecodeTextureFetch` still `kFormat`
   (Task 5, returns 220 and 221); `PlanClayFrameSlots(t, p, i, 0)` equal to today's arithmetic over
   a grid (CHECK 845); the shader test "every program of the pass compiles to the baseline's
   bytes" (case 55, part (a)); the frame row's `"water":"off"` and the unchanged `alpha-unknown`
   and `blend-unknown` counts on an off-run (criterion 15).
5. **The step at a wrong place, or a frame that is partly water** (the surface drawn before the
   sky, so that the sky's test passes over it or the clouds blend under it; after a record the game
   draws over it; the layer's depth or the pre-pass taking a record the verdict left out; one
   target sampled by a record of another base; a fifth base drawn with a wrong picture). Owner:
   Tasks 10, 11, 12. Pin: `PlanWaterPlace` under `PlanCoverageOrder`'s and `ColourStepsDue`'s
   arrangements (CHECK 846 to 848); `WaterTargets` (CHECK 849: a fifth base is `reflection`, a
   record's target is its own base's, two heights on one base are `plane`); the two bodies'
   targets equal in phase 13b (CHECK 842 on the basis; criterion 10 on the stored targets).

Also for every reviewer: guest reads whole or not at all, copied before they are judged; the frame
rechecked under the scene's mutex; evidence that names its file; what a run did not show is said.

## Numbers that are provisional, and the task that fixes each

Sub-project 11's interface blocks were written before its formulas closed and had to be amended.
Here every number of that kind is named beforehand. **Nobody builds on a provisional number: the
task that consumes it starts by reading the fixing task's report and amends the line in this file
(one line, marked "As fixed by Task N") in its own first commit.**

| Number or choice | Written here as | Fixed by | Consumed by |
|---|---|---|---|
| the vertex registers the formulas read beyond the draw record's rows | 4: `c115`, `c116`, `c117`, `c118`, components x and y of each [microcode, read by the plan's author: instructions 12 and 13] | Task 2 (`WATER_SHADER["reg"]`, from the interpreter's `used_consts`) | Tasks 4, 9 |
| the pixel registers the formulas read, and which components | 24 carried: `c0`, `c19`, `c20`, `c28`, `c31`, `c66`, `c67`, `c68`, `c116`, `c117`, `c126` to `c131`, `c136` to `c143`; read: `c0` all four, `c19.xyz`, `c20.w`, `c28.xy`, `c31.xy`, `c66.xyz`, `c67.xzw`, `c68.xyz`, `c116.x`, `c117.x`, `c126.xyz`, `c127.xyz`, `c128.x`, `c129.xy`, `c130.xy`, `c131.x`, `c136.x`, `c137.x`, `c138.x`, `c139.xy` (under `b137` alone), `c140.x`, `c141.xyw`, `c142.xyz`, `c143.xyz` [microcode, read by the plan's author] | Task 3 (`WATER_SHADER["ps_read"]`), confirmed on the rows by Task 4 | Tasks 4, 9, 10, 11 |
| the glint's signs | `pow(saturate(+(V . R)), c138.x)` with `R = 2 (N2 . L) N2 - L`, `L = -c142` [microcode, read by the plan's author: instructions 66 to 69 form `L - 2 (N2 . L) N2` and take `saturate(-(V . that))`]; the spec writes `saturate(-(V . R))` with the same `R` and says the check settles it | Task 3 (`formulas`, the mutation "`c142` with the other sign") | Tasks 6, 10, 11 |
| the mix factor | `saturate(c131.x)` [microcode: instruction 14, `maxs_sat r3.w, c131.xx`]; the spec writes `c131.x` (0.5 in every row: no difference in hand) | Task 3 | Tasks 10, 11 |
| which bit of the pixel bool word is `b136`, `b137`, and which word carries them at the draw | bits 8 and 9 (`0x100`, `0x200`: bit `N & 31` of the word of `b128..b159`); the last writer's word (the rows' `"pbool_load"`), not the device's shadow `"pbool"` (`0x00000100` in every row) | Task 4 (`water_check.py bools`: both members of every row) | Task 9 |
| `sizeof(WaterRecord)` | 584 (16 + 16 + 64 + 384 + 96 + 8: the hashes; place, kind, bool word, stride; the vertex registers; the pixel registers; four fetch constants; the two fog tables' bases) | Task 4's report (the register lists stand or change) | Task 9 |
| `F6D9`'s measured state words; the alpha test's reference at an `A17D` row | depth `0x00708766`, blend `0x00010001`, colour control `0x0000000C`; reference `0x3D70F0F1` = 15 / 255 [R B1] | Task 4 (`water_check.py states`: every row of the six captures) | Task 9 |
| the picture's orientation and channel order at the fetch (open question 2) | not flipped against the screen [inferred, spec]; channel order unknown (the spec author could not decide it: 30.3% against 28.9%) | Task 6 (criterion 4 with its two controls) | Tasks 6 (criterion 5), 10, 11, 12. **If Task 6 finds the picture flipped, the native lookup flips `v` (one line of `WaterLookup`); the channel order binds the replay's reading of the game's dump alone: the native target holds red, green, blue as the sky step writes them** |
| the normal map's channels at the fetch (which block is `a.x`), the masks' tiling and endian | the fetch constant's own swizzle over (first block, second block) as `DecodeDxnTexel` gives them; the mask by its constant's tiled bit [code] | Task 5 (the captured constants of slots 1 and 2, printed) | Tasks 6, 10, 12 |
| the normal map's stored levels and the level a fetch takes (open question 3) | levels made by a 2 x 2 mean of the decoded base until W1; the level by the SDK translator's rule, which Task 6's first step reads and names (`thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`) | Task 6 (the rule), Task 8 (W1: the stored levels) | Tasks 6, 13 |
| `sizeof(ClayWaterConstants)`, the small slots a record takes, `kClayWaterStepSlots` | 432 bytes (64 + 64 + 272 + 16 + 16), 2 slots a record, 153 = 2 x 64 + 4 + 21 (the records', the targets' sky constants, one second frame block) | Task 10 (after Task 4's components) | Tasks 10, 11, 12; Task 17 amends the count for a frame block a target |
| the viewport, scissor and clip registers of the views (open question 5) | not in any capture; of the registers the spec names, `0x2080` to `0x2082` (window offset and scissor) and `0x2388` to `0x239F` (the six user clip planes) have no device shadow in `xdk::DeviceRegShadowOffset` (`xdk_layout.h` line 309: it covers `0x2000..0x200F`, `0x2100..0x2114`, `0x2200..0x220B`, `0x2380..0x2387`) | Task 1 (which registers a row can carry), Task 8 (W1: their values) | Task 17 (the reflected shoreline's clip) |
| the shader test's case number | 55 (the next free number below 256 after the impostors' 54) | Task 11 | Tasks 11, 12, 17 |
| `test_clay_logic.cpp`'s CHECK numbers | 838 to 852 (the file's last is 837) | Task 10 | Tasks 10, 11, 12 |
| the water step's index at the bridge, and whether any frame reads `order` | expected: the sky step, the moon, the clouds, the water, then the blended group (912 of 916 records at the bridge: FM line 4072) | Task 12's first real-GPU run | Task 13 |
| a walk to the water's edge (W3) | **none found**: no capture holds the ground between the bridge and the shore; three guessed walks (EDGE-1 to EDGE-3) | Task 14 | Task 14 (criterion 11) |
| a sunward view over water (W4) | SUNHELD (sub-project 11's view), if the replay finds a glint there | Task 6 (the replay on `160104`, `160251`), Task 14 | Task 14 (criterion 12) |

## Where this plan departs from the spec's letter, and why

1. *"In `RecordDraw`, on the branch a draw takes when its material's status is `no-albedo`
   (`capture.cpp` lines 3584 to 3586)"* (Design 4): those lines are in `FillMaterial`, which
   `RecordDraw` calls (line 5154). The one load and one branch stand there, inside
   `if (albedo->slot < 0)`; what is read is kept in a thread-local and handed to the frame under
   the scene's mutex in `RecordDraw`, as an impostor call is.
2. *"Kept once per content (`DumpKeyedByContent`)"* (Design 1): that function is given the fetch
   constant alone and a `k_8_8_8_8` texture is one the renderer decodes, kept once an address.
   Task 1 adds a row-and-slot rule beside it (`DumpKeyedByContentAt`) and a content kind with a
   budget of its own, so that no other texture's dump changes.
3. *The clip registers* (Design 1): see the provisional table. Task 1 writes what the device
   shadows; what it does not shadow is named in the frame map, and open question 5's fallback
   ("the native clip stays the plane itself") stands for it.
4. *"From the vertex bytes the geometry cache already holds"* (Design 7, `WaterMeshHeight`): the
   cache holds, once a buffer, the box of the buffer's positions (`PositionBounds`). A mesh is
   flat iff `hi[2] - lo[2] <= 1e-3`, which implies the spec's "within 1e-3 of the first's".
5. *"One small slot a record, reserved only in a drawn frame"* (Design 8): the record's constants
   are 432 bytes, two small slots; and the frame's buffer is sized (`FinishTerrain`,
   `clay_pass.cpp` line 3524) before the sky's verdict exists (line 3657). The slots are reserved
   in a frame whose water is not yet refused when the buffer is sized (the switch on, records, a
   lit frame, the dead draw dead, the pairs whole, the scale): every other frame's plan is
   today's.
6. *"Made before any target is bound"* (Design 5): so, in two parts: the scene's own rules at the
   head of `Render`, the rest once the sky, the textures and the place are known. The `F6D9`
   records are prepared as today and carry a mark; a marked record is left out of the pre-pass and
   the colour loop only when the final verdict is `drawn`.
7. *The pair's buffer* (Design 8): the mask's coordinate is read through `GeometryCache::Uvs` with a
   copy of the record's material whose `uv_vb` is the record's vertex buffer and whose `uv` is the
   pair's layout (`k_32_32_FLOAT` at byte 12 of a 20-byte vertex); no new cache entry kind.
8. *The 256 x 256 depth target* (Design 7): made by Task 17, which is the first to use it.
9. *The stored scene's version* (Design 10): a file is version 2 only when it carries a new plane
   (flag 8 or 16); a dump of a frame without them is version 1, byte for byte as today. Readers
   take both.
10. *Criterion 15's "a run of the commit before, in the same session"*: the controller keeps a copy
    of the build folder as it is before Task 9's first build
    (`out\build\win-amd64-release-pre13b`) and runs it with `-ExeDir`, which is the controller's to
    pass and never an implementer's.
11. *W1 and W2* (Design 1) are phase 13b's first task here (Task 8): they need the game, and
    phase 13a must be completable without it. Nothing in phase 13a waits for them; criterion 4 is
    decided on the captures in hand and made again on W1.
12. *"A draw whose pixel shader has an albedo entry executes nothing new, with the switch on or
    off"* (Design 4): the reads stand on the `no-albedo` branch as the spec has them, but the
    record is handed to the frame under the scene's mutex with the draw's sequence number, and
    that number exists only in `RecordDraw`'s locked part (`capture.cpp` line 5240). Every recorded
    draw therefore tests one thread-local byte there (one load and one branch more, with the
    switch on or off). Task 13's capture medians say whether it shows.
13. *The glint's cosine and the mix factor* (Design 2): the plan's author reads instructions 66 to
    69 as `saturate(+(V . R))` and instruction 14 as `saturate(c131.x)`; the spec has
    `saturate(-(V . R))` and `c131.x`. The provisional table carries both; Task 3's machine check
    decides, not either reading.

---
## Phase 13a: the oracle (no game, no renderer change)

Order: Tasks 1 and 2 start together (Task 2 is Python alone and touches none of Task 1's files).
Task 3 follows Task 2 (the same files). Task 5 follows Task 1 (both edit `texture_decode.h` and its
test). Task 4 needs Task 3; Task 6 needs Tasks 4 and 5; Task 7 needs Task 4. One builder at a time
(Tasks 1 and 5 build test executables; Task 1 builds the game once and does not run it).

### Task 1: Discovery additions (capture only)

**Files:** `src/native/render/texture_decode.h`, `src/native/capture/discovery_format.h`,
`src/native/capture/capture.cpp`, `tests/native/test_texture_decode.cpp`,
`tests/native/test_discovery_format.cpp`, `tools/xdk_sigmatch/check_discovery.py`,
`tests/test_check_discovery.py`, `docs/native-renderer/frame-map.md` (new section 20 "Water: the
lake's surface", before "Pending", line 8492; subsection "Discovery").

**Consumes:** `DumpTexture` (`capture.cpp` line 1819: the content branch at 1860 to 1879, the
levels at 1905 to 1915), `DumpTextureMips` (1766), `AppendTextureFetches` (1942),
`g_tex_scene_verts_row` (344) as the pattern of a row flag, `g_tex_content_use`,
`AppendPictureViewport` (2196; called at 2412 for an out-of-scene `"draw"` row alone),
`kViewportRegisters` (2194), `WriteIndxRow` (2825), `WriteTessRow` (2958), `ChooseVs`, `LookupVs`;
`DumpKeyedByContent` (`texture_decode.h` line 239), `ContentDumpKind` and its three functions (266
to 308), `kContentDumpLimits` (284), `DumpFetch` (318), `DumpFormatInfo` (157: format 49 is
`{4, 16, 4}`); `RegisterWords` (`discovery_format.h` line 68); `xdk::DeviceRegShadowOffset`
(`xdk_layout.h` line 309); `check_verts_data` (`check_discovery.py` line 189) as the model of a
check.

**Produces:**

```cpp
// texture_decode.h
inline constexpr uint64_t kWaterVsHash = 0xBEAD84BD72072E0Eull;      // the lake's vertex shader (discovery's row rule)
// A reflection picture as a water draw samples it: a 2D k_8_8_8_8 texture of 256 x 256, by the constant alone.
inline bool WaterPictureFetch(const uint32_t fc[6]);
// Whether a discovery dump keeps the texture at `slot` of a row once per content: DumpKeyedByContent(fc) as
// before, or slot 13 or 14 of a water row (`water_row`: the row's vertex shader is kWaterVsHash) holding a
// WaterPictureFetch. The game renders those pictures again every frame (frame-map section 20).
inline bool DumpKeyedByContentAt(const uint32_t fc[6], bool water_row, uint32_t slot);
enum class ContentDumpKind : uint8_t { kTable, kTarget, kPicture, kReflection };
// `reflection`: DumpKeyedByContentAt took the constant by its row and slot (it is then no table and no picture).
inline ContentDumpKind ContentDumpKindOf(const uint32_t fc[6], bool reflection = false);
// ContentDumpUse gains reflection_files, reflection_bytes; ContentDumpLimits the same two;
// kContentDumpLimits = {1024, 512ull << 20, 448, 448ull * 131072ull, 256, 256ull * 262144ull}: 32 frames of eight.
// Whether the stored levels of a texture kept once an address are dumped beside its base: `decoded` (a format
// the renderer decodes: as before), or slot 2 of a water row holding a 2D k_DXN texture (the normal map).
inline bool DumpMipsAt(const uint32_t fc[6], bool decoded, bool water_row, uint32_t slot);

// discovery_format.h
// The surface pitches whose out-of-scene rows carry "vp": 320 (an impostor's picture) and 280 (a reflection view).
constexpr bool PictureViewportPitch(uint32_t surface_info) { return (surface_info & 0x3FFF) == 320 || (surface_info & 0x3FFF) == 280; }
// What a row at pitch 280 carries beyond kViewportRegisters: PA_CL_CLIP_CNTL and the six user clip planes.
inline constexpr uint32_t kClipRegisters[] = {0x2204, 0x2388, 0x2389, 0x238A, 0x238B, 0x238C, 0x238D, 0x238E, 0x238F, 0x2390, 0x2391, 0x2392, 0x2393,
                                              0x2394, 0x2395, 0x2396, 0x2397, 0x2398, 0x2399, 0x239A, 0x239B, 0x239C, 0x239D, 0x239E, 0x239F};
```

Rows (inside the discovery gate; nothing a record or the pass reads changes):
- `DumpTexture(fc)` becomes `DumpTexture(fc, slot)` and reads a file-scope `g_tex_water_row`, set by
  `AppendTextureFetches` for the row it writes (the row's vertex shader's hash is `kWaterVsHash`;
  false when the shader cannot be told). A slot-13 or slot-14 picture of a water row is hashed and
  kept once per content, counted against the reflection budget; its texture row carries `"frame"`
  and `"content"` as a content-kept texture's does, and the draw's `"tfc"` names the content.
  Slot 14's memory is all zero in every dump: one file a base.
- The normal map at slot 2 of a water row: its texture row gains `"mips"` (`DumpTextureMips` with
  `DumpFetch`'s layout for format 49), as a decoded texture's has.
- An out-of-scene `"draw"`, `"indx"` or `"tess"` row whose surface pitch is 280 gains `"vp"` with
  `kViewportRegisters` and `kClipRegisters`; a register without a device shadow is left out (as
  today), and the frame map names those. A row at pitch 320 keeps the member it has (a `"draw"` row,
  `kViewportRegisters` alone).
- `check_discovery.py`: `check_water_rows(rows)` -> `(checked, problems)`.

- [ ] Write the tests first and see them fail. `tests/native/test_texture_decode.cpp` (after the
      file's last number, 206):

```cpp
  // --- Water discovery (plan Task 1): the reflection pictures by content, the normal map's levels ---
  {
    FcArgs a;
    a.format = 6; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 2; a.base = 0x13401000; a.mip = 0;
    a.mip_max = 0; a.packed = false; a.clamp_x = 0; a.clamp_y = 0;
    uint32_t p[6];
    MakeFc(a, p);
    if (DumpKeyedByContent(p)) return 210;                                       // by its constant alone: once an address, as before
    if (!WaterPictureFetch(p)) return 211;
    if (!DumpKeyedByContentAt(p, true, 13) || !DumpKeyedByContentAt(p, true, 14)) return 212;
    if (DumpKeyedByContentAt(p, false, 13) || DumpKeyedByContentAt(p, true, 0) || DumpKeyedByContentAt(p, true, 2)) return 213;   // another row, another slot
    if (ContentDumpKindOf(p, true) != ContentDumpKind::kReflection || ContentDumpKindOf(p) != ContentDumpKind::kTarget) return 214;
    a.w = 128; a.h = 128; a.pitch = 128;                                         // the 128 x 128 passes before the views: not a picture
    MakeFc(a, p);
    if (WaterPictureFetch(p) || DumpKeyedByContentAt(p, true, 13)) return 215;
    a.w = 256; a.h = 256; a.pitch = 256; a.format = 3;                           // an impostor's picture keeps its own rule and kind
    MakeFc(a, p);
    if (!DumpKeyedByContentAt(p, false, 0) || ContentDumpKindOf(p) != ContentDumpKind::kPicture || WaterPictureFetch(p)) return 216;
    // the reflection budget is its own: full, it refuses a reflection and nothing else; the others full, it still has room
    ContentDumpUse use;
    use.reflection_files = kContentDumpLimits.reflection_files;
    if (ContentDumpFileRoom(ContentDumpKind::kReflection, use, kContentDumpLimits) ||
        !ContentDumpFileRoom(ContentDumpKind::kTable, use, kContentDumpLimits) ||
        !ContentDumpFileRoom(ContentDumpKind::kPicture, use, kContentDumpLimits)) return 217;
    use = ContentDumpUse{};
    use.files = kContentDumpLimits.files;
    use.picture_files = kContentDumpLimits.picture_files;
    if (!ContentDumpHasRoom(ContentDumpKind::kReflection, 262144, use, kContentDumpLimits)) return 218;
    use = ContentDumpCounted(ContentDumpKind::kReflection, 262144, ContentDumpUse{});
    if (use.reflection_files != 1 || use.reflection_bytes != 262144 || use.files != 0 || use.bytes != 0 || use.picture_files != 0) return 219;
    if (kContentDumpLimits.reflection_files != 256 || kContentDumpLimits.reflection_bytes != 256ull * 262144ull) return 207;
    // the normal map's levels: slot 2 of a water row alone
    a.format = 49; a.mip = 0x14D50000; a.mip_max = 8; a.base = 0x14D40000;
    MakeFc(a, p);
    if (!DumpMipsAt(p, false, true, 2) || DumpMipsAt(p, false, false, 2) || DumpMipsAt(p, false, true, 1)) return 208;
    if (!DumpMipsAt(p, true, false, 0)) return 209;                              // a decoded texture: as before, whatever the row
  }
```

      `tests/native/test_discovery_format.cpp` (after the file's last number, 89): `90`:
      `PictureViewportPitch(0x04020118)` (the views' word: pitch 280) and `(0x00000140)` are true,
      `(0x11810460)` (the scene's, pitch 1120) and `(0)` are false; `91`: `kClipRegisters` is
      ascending, starts with `0x2204` and holds the 24 registers `0x2388..0x239F`.
      `tests/test_check_discovery.py`:

```python
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
```

      (How a row names its shaders, `vs_hash` as here or through `capture_replay.vs_hash(row)`, is
      taken from `check_discovery.py`'s present code and a row of CAP `155547`; the test's rows are
      made the way that code reads them.)
- [ ] Run them: `rtk clang++ -std=c++23 tests\native\test_texture_decode.cpp -o
      out\tests\runtime-unit-tests\texture_decode.exe` (expected: does not compile:
      `WaterPictureFetch` undeclared); the same for `test_discovery_format.cpp`
      (`PictureViewportPitch` undeclared); `rtk python -m unittest tests.test_check_discovery -v`
      (expected: `AttributeError: ... check_water_rows`).
- [ ] Implement the pure functions; rerun the three: exit 0, and the Python test OK. The existing
      cases of both native tests pass unchanged (`ContentDumpKindOf(p)` with one argument is
      today's function).
- [ ] **Which clip registers a row can carry**: read `xdk_layout.h` lines 300 to 315 and FM's
      account of the register shadows; write into the frame map which of `kViewportRegisters` and
      `kClipRegisters` have a shadow (expected: `0x200E`, `0x200F`, `0x210F` to `0x2114`, `0x2204`)
      and which have none (expected: `0x2080` to `0x2082`, `0x2388` to `0x239F`), and mark the
      provisional table's line. No shadow is invented and no new offset is guessed.
- [ ] Implement the rows in `capture.cpp`: `g_tex_water_row` set and cleared in
      `AppendTextureFetches`; `DumpTexture`'s content branch entered for `DumpKeyedByContentAt`;
      the levels' call for `DumpMipsAt`; `AppendPictureViewport` by `PictureViewportPitch`, with the
      clip registers at pitch 280, called from `WriteIndxRow` and `WriteTessRow` too for a row
      outside the scene. Native suite and Python suite green; `rtk .\build.cmd -release fable_2`
      (the build is not run: Task 8's captures are its first run).
- [ ] The old captures still pass: `rtk python tools/xdk_sigmatch/check_discovery.py --rows-only
      out/build/win-amd64-release/logs/native_discovery_20261011_155547.jsonl` prints its lines of
      before and, new, `water rows: N checked, M problems` with the problems being the missing
      per-content pictures and `"vp"` members of a capture made before this task (said so in the
      report: that is what the check is for).
- [ ] Frame map section 20, "Discovery": the members, the budget (256 files, 64 MB), which
      registers have no shadow, what is still not kept. Commit "Water discovery: the reflection
      pictures by content, the normal map's levels, the views' viewport and clip registers".

*No run in this task: Task 8 makes its captures.*

### Task 2: The vertex and alpha formulas on made inputs; the dead draw on the microcode (no game; parallel with Task 1)

**Files:** `tools/xdk_sigmatch/light_formulas.py` (a new part "The lake's surface"),
`tools/xdk_sigmatch/water_check.py` (new), `tests/test_water_formulas.py` (new),
`docs/native-renderer/lighting-formulas.md` (part "Water").

**Consumes:** `ucode_eval.Machine(text, consts, sampler, regs=, bools=, vfetch=, loops=, tex_size=,
zero_product=)` (its `killed`, `out`, `used_consts`, `fetches`) and `ucode_eval.load(dump_dir,
hash_, kind)`; `capture_replay.DUMPS`; `impostor_check.py` as the model of form (`Case`,
`made_case`, `run_microcode`, `check_position`, `trace`, `used_registers`, its `Result`);
`fog_check.WATER`, `WATER_LITERALS` and `_water_setup` (lines 63, 64, 248: the two shaders already
run there for their fog block); `out/shader_dump/shader_BEAD84BD72072E0E.ucode.vert` (instructions
3 to 19) and `shader_A17D8AEC3A817D45.ucode.frag`, `shader_F6D98C7B4D98438B.ucode.frag`
(instructions 10 to 89; they differ in instruction 14 alone).

**Produces** (in `light_formulas.py`):

```python
WATER_SHADER = {
    "vs": "BEAD84BD72072E0E", "ps": "A17D8AEC3A817D45", "ps_opaque": "F6D98C7B4D98438B",
    "reg": (115, 116, 117, 118),                                  # vertex registers beyond the rows c0..c3, in the record's order
    "ps_reg": (0, 19, 20, 28, 31, 66, 67, 68, 116, 117, 126, 127, 128, 129, 130, 131,
               136, 137, 138, 139, 140, 141, 142, 143),           # PROVISIONAL: Task 3
    "ps_lit": {254: (0.0, 2.0, 0.0, 0.0), 255: (0.5, -1.0, 1.0, -2.0)},     # fog_check.WATER_LITERALS
    "b136": 0x00000100, "b137": 0x00000200,                       # PROVISIONAL: Task 4
}
def water_vertex(p, pair, c):
    """p: the fetched position (3 floats). pair: the fetched 32_32_FLOAT pair. c: {register: [4 floats]} for
    0..3 and WATER_SHADER["reg"]. Returns {"pos": oPos (4), "place": o0.xyz (3), "coord": o1 (4), "mask": o3.xy (2)}:
        pos   = (c0 . (p, 1), c1 . (p, 1), c2 . (p, 1), c3 . (p, 1))            instr 5 to 8: nothing added
        place = p                                                             instr 11
        coord = (p.x * c117.x + c115.x, p.y * c117.y + c115.y,
                 p.x * c118.x + c116.x, p.y * c118.y + c116.y)                instr 12, 13
        mask  = pair                                                          instr 17
    o0.w, o2 and o3.z are written and read by neither pixel shader: not returned."""
def water_alpha(m, c141):
    """alpha = 1 - sat(c141.x - (1 - m) * c141.y)   (instr 10 to 13). m: the shore mask's sample (slot 1)."""
def water_kept(alpha, c141, kind="surface"):
    """What becomes of a pixel: "killed" (kind "surface", A17D: alpha == 1; kind "opaque", F6D9: alpha < 1;
    instr 14 to 16), "skipped" (not killed and alpha <= c141.w: the colour is 0 and the alpha `alpha`;
    instr 17, 18), or "shaded"."""
def water_opaque_dead(c141, margin=1e-6):
    """Whether F6D9 kills every pixel whatever the mask holds: min(c141.x, c141.x - c141.y) >= margin with
    |c141.x|, |c141.y| <= 4 (the argument of the saturate is linear in m between those two ends)."""
```

`water_check.py`: `made_vertex(seed=1, trial=0)`, `run_vertex(case)`, `check_vertex(trials=2000,
seed=1, mutation=None, dumps=DUMPS)` -> `Result(trials, failed, worst, first_failure)` (bound:
`|value - formula| <= 1e-5 * max(1, |value|)` in every component of `oPos`, `o0.xyz`, `o1` and
`o3.xy`); `VERTEX_MUTATIONS = ("rows", "scales", "axes", "matrix")` (`c116` for `c115`; `c118` for
`c117`; the coordinate from `p.xz`; two rows of `c0..c3` exchanged); `check_alpha(trials=2000,
seed=1, mutation=None, kind="surface")` -> `AlphaResult(trials, failed, killed, skipped, shaded,
worst)` (the interpreter's `killed` and output alpha against `water_alpha` and `water_kept`, within
1e-6; the three fates counted apart and equal in both); `ALPHA_MUTATIONS = ("ramp", "mask",
"comparison")` (`c141.x` and `c141.y` exchanged; `m` for `1 - m`; `F6D9`'s comparison for
`A17D`'s); `check_dead(trials=2000, seed=1)` -> `DeadResult(trials, killed, control_kept)`;
subcommands `formulas` (this task: the vertex and the alpha; Task 3 adds the pixel), `dead`,
`trace <hash> --instr N`, `used` (the interpreter's `used_consts` over 8 made cases a shader), each
exiting 1 on a failure.

Made inputs: `p` within 2,000 units; the pair in [0, 1]; `c0..c3` a view-projection of the
captures' shape (entries within 200); `c115`, `c116` in [-4, 4]; `c117`, `c118` in [0.01, 0.5];
every vertex register the formula does **not** read (`c9`, `c113`, `c119` to `c122`) random, so
that a formula that read one would differ. For the alpha: `m` over [0, 1] with 0 and 1 among the
samples (every 16th trial each); `c141` as `fog_check._water_setup` draws it for a third of the
trials (which kills some, skips some, shades the rest), the captured (2.003, 2, 0.0015, 0.0588)
for a third, and `c141.x` in [-1, 3], `c141.y` in [-1, 3], `c141.w` in [0, 1] for the rest. For
`dead`: `c141` drawn so that `water_opaque_dead` holds (`c141.y` in [-4, 4], `c141.x` in
[max(0, c141.y) + 1e-6, 4]), `m` anywhere in [0, 1]; the control: `c141.x - c141.y <= 0` with
`m` = 0. The ranges are constants at the file's head with this paragraph beside them.

- [ ] Tests first (`tests/test_water_formulas.py`), see them fail (`AttributeError`: the names do
      not exist):

```python
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "xdk_sigmatch"))

import light_formulas as lf  # noqa: E402
import water_check  # noqa: E402

DUMPS = ROOT / "out" / "shader_dump"
NO_DUMPS = f"no shader dumps at {DUMPS} (made by fable_2.exe --dump_shaders=<dir>)"
HAVE = (DUMPS / "shader_A17D8AEC3A817D45.ucode.frag").exists()
C141 = (2.003, 2.0, 0.0015, 0.0588)        # every captured row (R B1)


def vconsts(over=None):
    c = {0: [1.0, 0.0, 0.0, 0.0], 1: [0.0, 1.0, 0.0, 0.0], 2: [0.0, 0.0, 1.0, 0.0], 3: [0.0, 0.0, 0.0, 1.0],
         115: [1.0, 2.0, 9.0, 9.0], 116: [-3.0, 4.0, 9.0, 9.0], 117: [0.5, 0.25, 9.0, 9.0], 118: [0.125, 2.0, 9.0, 9.0]}
    c.update(over or {})
    return c


class Vertex(unittest.TestCase):
    P = (100.0, 200.0, 20.0)

    def test_the_position_is_the_rows_and_nothing_is_added(self):                # instr 5 to 8, 11
        out = lf.water_vertex(self.P, (0.25, 0.75), vconsts())
        self.assertEqual(tuple(out["pos"]), (100.0, 200.0, 20.0, 1.0))
        self.assertEqual(tuple(out["place"]), self.P)
        moved = lf.water_vertex(self.P, (0.25, 0.75), vconsts({3: [0.0, 0.0, 1.0, 5.0]}))
        self.assertEqual(moved["pos"][3], 25.0)

    def test_two_coordinates_scroll_apart(self):                                 # instr 12, 13
        coord = lf.water_vertex(self.P, (0.25, 0.75), vconsts())["coord"]
        self.assertEqual(tuple(coord), (100.0 * 0.5 + 1.0, 200.0 * 0.25 + 2.0, 100.0 * 0.125 - 3.0, 200.0 * 2.0 + 4.0))

    def test_the_mask_coordinate_is_the_fetched_pair(self):                      # instr 17
        self.assertEqual(tuple(lf.water_vertex(self.P, (0.25, 0.75), vconsts())["mask"]), (0.25, 0.75))

    def test_the_height_does_not_reach_a_coordinate(self):
        a = lf.water_vertex((100.0, 200.0, 20.0), (0.0, 0.0), vconsts())["coord"]
        b = lf.water_vertex((100.0, 200.0, 99.0), (0.0, 0.0), vconsts())["coord"]
        self.assertEqual(tuple(a), tuple(b))


class Alpha(unittest.TestCase):
    def test_open_water_is_just_below_one_and_land_is_nothing(self):
        self.assertAlmostEqual(lf.water_alpha(0.0, C141), 0.997, places=6)     # m = 0: 1 - sat(2.003 - 2)
        self.assertEqual(lf.water_alpha(1.0, C141), 0.0)                        # m = 1: 1 - sat(2.003)
        self.assertEqual(lf.water_alpha(0.5, C141), 0.0)

    def test_the_two_shaders_kill_complementary_pixels(self):
        for alpha in (0.0, 0.5, 0.997):
            self.assertEqual(lf.water_kept(alpha, C141, "opaque"), "killed")
            self.assertNotEqual(lf.water_kept(alpha, C141, "surface"), "killed")
        self.assertEqual(lf.water_kept(1.0, C141, "surface"), "killed")
        self.assertNotEqual(lf.water_kept(1.0, C141, "opaque"), "killed")

    def test_at_or_below_the_threshold_nothing_is_shaded(self):
        self.assertEqual(lf.water_kept(0.0588, C141, "surface"), "skipped")
        self.assertEqual(lf.water_kept(0.0589, C141, "surface"), "shaded")

    def test_the_dead_condition(self):
        self.assertTrue(lf.water_opaque_dead(C141))
        self.assertFalse(lf.water_opaque_dead((2.0, 2.0, 0.0, 0.0588)))         # m = 0 gives alpha 1
        self.assertFalse(lf.water_opaque_dead((0.0, -1.0, 0.0, 0.0588)))        # m = 1 gives alpha 1
        self.assertFalse(lf.water_opaque_dead((5.0, 1.0, 0.0, 0.0588)))         # outside the magnitudes the margin was reasoned for
        self.assertFalse(lf.water_opaque_dead((math.nan, 2.0, 0.0, 0.0588)))
        self.assertTrue(lf.water_opaque_dead((2.000002, 2.0, 0.0, 0.0588)))      # above the margin of 1e-6
        self.assertFalse(lf.water_opaque_dead((2.0000005, 2.0, 0.0, 0.0588)))    # below it (the single-precision rule is Task 10's)


@unittest.skipUnless(HAVE, NO_DUMPS)
class Microcode(unittest.TestCase):
    TRIALS = 200                                                                 # the reduced count; `formulas` runs 2,000

    def test_the_vertex_formula_is_the_microcode(self):
        r = water_check.check_vertex(trials=self.TRIALS)
        self.assertEqual((r.failed, r.trials), (0, self.TRIALS), r.first_failure)

    def test_the_alpha_is_the_microcode_for_both_shaders(self):
        for kind in ("surface", "opaque"):
            r = water_check.check_alpha(trials=self.TRIALS, kind=kind)
            self.assertEqual(r.failed, 0)
            self.assertGreaterEqual(min(r.killed, r.skipped, r.shaded), self.TRIALS // 10)   # every fate is reached

    def test_mutations_fail(self):
        for m in water_check.VERTEX_MUTATIONS:
            self.assertGreater(water_check.check_vertex(trials=self.TRIALS, mutation=m).failed, 0, m)
        for m in water_check.ALPHA_MUTATIONS:
            self.assertGreater(water_check.check_alpha(trials=self.TRIALS, mutation=m).failed, 0, m)

    def test_the_dead_draw_is_dead_on_the_microcode(self):
        r = water_check.check_dead(trials=self.TRIALS)
        self.assertEqual(r.killed, r.trials)                                     # F6D9 killed in every trial
        self.assertGreater(r.control_kept, 0)                                    # and not killed where the condition fails

    def test_the_vertex_shader_reads_what_the_table_says(self):
        used = water_check.used_registers("vs")
        self.assertTrue(set(lf.WATER_SHADER["reg"]) | {0, 1, 2, 3} <= used)
        self.assertEqual(used - set(lf.WATER_SHADER["reg"]) - {0, 1, 2, 3}, {9, 113, 119, 120, 121, 122, 255})   # o0.w and o2: read by neither pixel shader
```

- [ ] `rtk python -m unittest tests.test_water_formulas -v`: fails as stated. Implement
      `water_vertex`, `water_alpha`, `water_kept`, `water_opaque_dead`, `water_check.py`; rerun:
      OK.
- [ ] `rtk python tools/xdk_sigmatch/water_check.py formulas >
      out/native-evidence/water/t2_formulas.txt`: 2,000 of 2,000 for the vertex and for the alpha
      of each shader, the three fates' counts, each mutation's failures. `water_check.py dead >
      t2_dead.txt`: part (a) of criterion 2, 2,000 of 2,000 killed and the control. `water_check.py
      used > t2_used.txt`: the registers each of the three dumps reads. **Any disagreement: stop;
      the report names the instruction at which the trace first differs (`trace <hash> --instr N`);
      nothing is tuned.**
- [ ] Mark the provisional table's first line ("As fixed by Task 2"). `lighting-formulas.md`,
      "Water": the vertex and the alpha, instruction by instruction, what stands on inference
      (nothing may). Commit "Water formulas: the vertex shader and the alpha against the
      microcode; the dead draw's proof on made inputs".

### Task 3: The pixel formula (no game)

**Files:** `light_formulas.py`, `water_check.py`, `tests/test_water_formulas.py`,
`lighting-formulas.md`.

**Consumes:** Task 2; `lf.fog(C, P, eye, c28, c66, c67, c68, tf4, tf5)` (line 609: the plain form,
to which `fog_check.py` holds both water shaders today), `lf.sat`, `lf.dot`, `lf.normalize`;
`fog_check.check_pixel_text` (line 132) for how the interpreter is given interpolators, samplers
and bools; `impostor_check.check_pixel` (line 616) as the model.

**Produces:**

```python
def water_normal(a, b, c128x):
    """a, b: slot 2's two channels at coord.xy and coord.zw (instr 19, 20). Returns (s, N):
        s = (2a - 1) + (2b - 1)                                               instr 24, 25
        z = sqrt|1 - |2a - 1|^2| + sqrt|1 - |2b - 1|^2|                       instr 33 to 39
        N = normalize(s.x / 2, s.y / 2, (c128x / 2) * z)                      instr 27, 36, 40 to 43"""
def water_lookup(q, s, c0, c31, c129, c130):
    """q: the pixel's integral position (the param-gen register's magnitude). Returns ((u13, v13), (u14, v14)):
        u = (q.x * c0.z + c0.x - 1/2) * c31.x + 1/2;  v = (q.y * c0.w + c0.y - 1/2) * c31.y + 1/2     instr 21 to 28
        slot 13 at (u + s.x * c129.x / 2, v + s.y * c129.y / 2); slot 14 the same with c130           instr 26, 29, 30"""
def water_base(P, eye, N, c116x, c126, c127):
    """(base, F, V, d): V = normalize(eye - P); d = |eye - P|; F = sat(1 - V . N + c116x);
    base = c127 + (c126 - c127) * F                                           instr 37 to 47, 51"""
def water_mix(base, r, near14, F, d, c117x, c131x, c140x):
    """far = base + (r - base) * sat(c131x); near = base + (lerp(near14, r, sat(F + c117x)) - base) * sat(c131x);
    colour = lerp(near, far, sat(c140x * d))                                  instr 14, 51 to 59"""
def water_sun(N, V, c136x, c137x, c138x, c142, c143):
    """N2 = normalize(N.x, N.y, c136x * N.z); L = -c142; R = 2 (N2 . L) N2 - L;
    the term = exp2(c138x * log2(sat(V . R))) * c137x * c143.xyz             instr 60 to 75 (signs PROVISIONAL)"""
def water_pixel(q, P, coord_a, coord_b, m, c, tex, bools=(136,)):
    """The whole pixel of A17D (kind "surface") from its interpolators: P (o0.xyz), the two samples' callbacks
    and the registers. c: {register: [4 floats]} for WATER_SHADER["ps_reg"]. tex: {"normal": f(u, v) -> (x, y),
    13: f(u, v) -> rgb, 14: f(u, v) -> rgb, 8: f(u, v) -> float, "tf4": f(a) -> rgb, "tf5": f(s) -> rgb}.
    Returns {"fate": "killed" | "skipped" | "shaded", "alpha": alpha, "colour": rgb (after fog and c20.w),
    "glint": the sun term's share before fog, "lookup": (u13, v13)}. With 137 in `bools` the reflection is
    multiplied by tex[8] * c139.x + c139.y (instr 48 to 50); with 136 the sun term is added."""
```

`water_check.py`: `PixelCase`, `made_pixel(seed, trial)`, `run_pixel(case)`,
`check_pixel(trials=2000, seed=1, mutation=None)` -> `PixelResult(trials, failed, killed, skipped,
shaded, sun, shadow, near, far, fogged, worst, first_failure)`; bounds: the colour within
`1e-5 * max(1, |value|)` with `b136` clear and within 1e-3 relative with it set (a power of 128
multiplies the logarithm's rounding); the alpha within 1e-6; the fates counted apart and equal in
both. `PIXEL_MUTATIONS` (the spec's list, each a name): `"base"` (`c126` and `c127` exchanged),
`"centre"` (the scale about 0 instead of the centre), `"distort"` (`c130` for `c129` at slot 13),
`"lookup_axes"` (the two lookups' axes exchanged), `"flatten"` (`c128.x` left out), `"bias"` (the
fresnel bias with the other sign), `"mix"` (`c131.x` left out), `"near_far"` (exchanged),
`"range"` (`c140.x` changed by 1%), `"slot14"` (left out of `near`), `"second_normal"` (`c136.x`
left out), `"power"` (changed by 1%), `"sun_sign"` (`c142` with the other sign), `"sun_scale"`
(`c137.x` left out), `"fog"` (`c28.x` left out), `"exposure"` (`c20.w` left out).

Made inputs (constants at the file's head): every register a formula reads from a range about its
captured value (`WE/b_water_d1_f1.txt`: `c126` (0.027, 0.075, 0.043), `c127` (0.059, 0.129, 0.122),
`c128.x` 0.3, `c129.xy` 0.84, `c131.x` 0.5, `c136.x` 0.279, `c137.x` 0.5, `c138.x` 128, `c140.x`
0.05, `c116.x` 0.104, `c31.xy` 2/3, `c0.zw` (1/1120, 1/720), `c20.w` 8.6): half to twice the
value, `c131.x` also above 1 in a tenth of the trials (the saturate), `c138.x` in [1, 256];
**every component no formula reads is drawn at random and large (1e22)**, so that a formula that
read one would differ; the mask as Task 2's; both bools in both states (`b137` set in a quarter);
`P` so that the distance lies on both sides of `1 / c140.x` and of the fog's start `c67.x`; the
normal's two samples anywhere in [0, 1]^2, with (0.5, 0.5) (a zero sum) every 20th trial; the sun's
direction so that `V . R` is positive in at least a third of the trials with `b136` set.

- [ ] Tests first (appended to `tests/test_water_formulas.py`); see them fail:

```python
def pconsts(over=None):
    c = {r: [1e22, 1e22, 1e22, 1e22] for r in lf.WATER_SHADER["ps_reg"]}        # unread components are garbage
    c[0][:] = [0.0, 0.0, 1.0 / 1120.0, 1.0 / 720.0]
    c[19][:3] = [0.0, 0.0, 30.0]
    c[20][3] = 2.0
    c[28][:2] = [0.0, 1.0]                                                      # the fog's amount 0: the block changes nothing
    c[31][:2] = [2.0 / 3.0, 2.0 / 3.0]
    c[66][:3] = [0.0, 0.0, 1.0]
    c[67][:] = [1e6, 1e22, 0.0, 0.0]                                            # the fog starts beyond everything
    c[68][:3] = [0.0, 0.0, 0.0]
    c[116][0], c[117][0] = 0.0, 0.0
    c[126][:3] = [0.2, 0.4, 0.6]
    c[127][:3] = [0.1, 0.1, 0.1]
    c[128][0] = 1.0
    c[129][:2] = [0.0, 0.0]
    c[130][:2] = [0.0, 0.0]
    c[131][0] = 0.5
    c[136][0], c[137][0], c[138][0] = 1.0, 0.0, 128.0
    c[139][:2] = [1.0, 0.0]
    c[140][0] = 0.05
    c[141][:] = [2.003, 2.0, 1e22, 0.0588]
    c[142][:3] = [0.0, 0.0, -1.0]
    c[143][:3] = [1.0, 1.0, 1.0]
    c.update(over or {})
    return c


FLAT = (0.5, 0.5)                              # a normal sample without tilt: s = (0, 0), N = (0, 0, 1)
TEX = {"normal": lambda u, v: FLAT, 13: lambda u, v: (0.8, 0.6, 0.4), 14: lambda u, v: (0.0, 0.0, 0.0),
       8: lambda u, v: 1.0, "tf4": lambda a: (0.0, 0.0, 0.0), "tf5": lambda s: (1.0, 1.0, 1.0)}


class Pixel(unittest.TestCase):
    def test_a_flat_normal_is_straight_up_and_the_lookup_is_the_screen_place_scaled_about_the_centre(self):
        s, N = lf.water_normal(FLAT, FLAT, 0.3)
        self.assertEqual(tuple(s), (0.0, 0.0))
        self.assertEqual(tuple(N), (0.0, 0.0, 1.0))
        c = pconsts()
        (u, v), _ = lf.water_lookup((560.0, 360.0), s, c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(u, 0.5)
        self.assertAlmostEqual(v, 0.5)
        (u, v), _ = lf.water_lookup((0.0, 720.0), s, c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(u, 0.5 - 0.5 * 2.0 / 3.0)
        self.assertAlmostEqual(v, 0.5 + 0.5 * 2.0 / 3.0)

    def test_the_distortion_moves_each_slot_by_its_own_register(self):
        c = pconsts({129: [0.8, 0.4, 1e22, 1e22], 130: [0.2, 0.1, 1e22, 1e22]})
        at13, at14 = lf.water_lookup((560.0, 360.0), (0.5, -0.25), c[0], c[31], c[129], c[130])
        self.assertAlmostEqual(at13[0], 0.5 + 0.5 * 0.8 / 2)
        self.assertAlmostEqual(at13[1], 0.5 - 0.25 * 0.4 / 2)
        self.assertAlmostEqual(at14[0], 0.5 + 0.5 * 0.2 / 2)
        self.assertAlmostEqual(at14[1], 0.5 - 0.25 * 0.1 / 2)

    def test_looking_straight_down_is_the_deep_colour_and_grazing_the_other(self):
        base, F, V, d = lf.water_base((0.0, 0.0, 0.0), (0.0, 0.0, 30.0), (0.0, 0.0, 1.0), 0.0, (0.2, 0.4, 0.6), (0.1, 0.1, 0.1))
        self.assertEqual((F, d), (0.0, 30.0))
        self.assertEqual(tuple(base), (0.1, 0.1, 0.1))                           # c127
        base, F, _, _ = lf.water_base((1e6, 0.0, 0.0), (0.0, 0.0, 30.0), (0.0, 0.0, 1.0), 0.0, (0.2, 0.4, 0.6), (0.1, 0.1, 0.1))
        self.assertAlmostEqual(F, 1.0, places=4)
        self.assertAlmostEqual(base[2], 0.6, places=4)                           # c126

    def test_beyond_the_range_half_the_base_and_half_the_reflection(self):
        col = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 30.0, 0.0, 0.5, 0.05)
        for got, want in zip(col, (0.45, 0.35, 0.25)):
            self.assertAlmostEqual(got, want)
        near = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 0.0, 0.0, 0.5, 0.05)
        for got in near:
            self.assertAlmostEqual(got, 0.05)                                    # at the eye: slot 14 (zero) for the reflection
        over = lf.water_mix((0.1, 0.1, 0.1), (0.8, 0.6, 0.4), (0.0, 0.0, 0.0), 0.0, 30.0, 0.0, 7.0, 0.05)
        self.assertAlmostEqual(over[0], 0.8)                                     # c131.x is saturated: the reflection alone

    def test_the_whole_pixel_far_from_the_eye(self):
        out = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 0.0, pconsts(), TEX)
        self.assertEqual(out["fate"], "shaded")
        self.assertAlmostEqual(out["alpha"], 0.997, places=6)
        for got, want in zip(out["colour"], (0.45 * 2.0, 0.35 * 2.0, 0.25 * 2.0)):   # the mix times c20.w; the sun's scale is 0
            self.assertAlmostEqual(got, want, places=6)
        self.assertEqual(out["glint"], 0.0)

    def test_land_is_skipped_with_no_colour(self):
        out = lf.water_pixel((560.0, 360.0), (0.0, 0.0, 0.0), (0.0, 0.0), (0.0, 0.0), 1.0, pconsts(), TEX)
        self.assertEqual((out["fate"], out["alpha"], tuple(out["colour"])), ("skipped", 0.0, (0.0, 0.0, 0.0)))

    def test_the_sun_mirrored_into_the_eye_is_its_whole_colour(self):
        # the eye straight above, the surface flat, the light coming straight down (c142 = (0, 0, -1): L = (0, 0, 1))
        term = lf.water_sun((0.0, 0.0, 1.0), (0.0, 0.0, 1.0), 1.0, 0.5, 128.0, (0.0, 0.0, -1.0), (1.0, 0.5, 0.25))
        for got, want in zip(term, (0.5, 0.25, 0.125)):
            self.assertAlmostEqual(got, want)
        away = lf.water_sun((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), 1.0, 0.5, 128.0, (0.0, 0.0, -1.0), (1.0, 0.5, 0.25))
        self.assertEqual(tuple(away), (0.0, 0.0, 0.0))

    def test_no_unread_component_reaches_the_pixel(self):
        a = lf.water_pixel((100.0, 500.0), (40.0, 60.0, 0.0), (0.3, 0.1), (0.7, 0.9), 0.1, pconsts(), TEX)
        other = pconsts()
        for reg, comps in water_check.UNREAD.items():                           # {register: the components no formula reads}
            for k in comps:
                other[reg][k] = -3.5
        b = lf.water_pixel((100.0, 500.0), (40.0, 60.0, 0.0), (0.3, 0.1), (0.7, 0.9), 0.1, other, TEX)
        self.assertEqual(tuple(a["colour"]), tuple(b["colour"]))
        self.assertTrue(all(math.isfinite(x) for x in a["colour"]))


@unittest.skipUnless(HAVE, NO_DUMPS)
class PixelMicrocode(unittest.TestCase):
    TRIALS = 200

    def test_the_pixel_formula_is_the_microcode(self):
        r = water_check.check_pixel(trials=self.TRIALS)
        self.assertEqual((r.failed, r.trials), (0, self.TRIALS), r.first_failure)
        tenth, fifth = self.TRIALS // 10, self.TRIALS // 5
        self.assertGreaterEqual(min(r.killed, r.skipped, r.shaded), tenth)       # (Review Focus 1: every branch is reached)
        self.assertGreaterEqual(min(r.sun, r.shaded - r.sun), tenth)             # b136 in both states among the shaded
        self.assertGreaterEqual(r.shadow, 1)                                     # b137 set somewhere
        self.assertGreaterEqual(min(r.near, r.far), fifth // 2)                  # both sides of 1 / c140.x
        self.assertGreaterEqual(min(r.fogged, r.shaded - r.fogged), fifth // 2)  # both sides of the fog's start

    def test_each_pixel_mutation_fails(self):
        for m in water_check.PIXEL_MUTATIONS:
            self.assertGreater(water_check.check_pixel(trials=self.TRIALS, mutation=m).failed, 0, m)

    def test_the_pixel_shader_reads_what_the_table_says(self):
        used = water_check.used_registers("ps")
        self.assertEqual(used - {254, 255}, set(lf.WATER_SHADER["ps_reg"]))
```

- [ ] Implement; `rtk python -m unittest tests.test_water_formulas -v`: OK. **The glint's signs are
      whatever makes `formulas` agree**: if the test `test_the_sun_mirrored_into_the_eye` as written
      above contradicts the microcode, the microcode wins, the test's expectation is rewritten to
      what the interpreter gives for that input (quoted in the report with the trace of
      instructions 66 to 69), and the provisional table's line is marked.
- [ ] `water_check.py formulas > t3_formulas.txt` (now with the pixel: 2,000 of 2,000, the
      counts, the sixteen mutations' failures); `water_check.py used > t3_used.txt`. **Criterion 1's
      first half.** `rtk python tools/xdk_sigmatch/fog_check.py` still passes for both water
      shaders (its lines quoted). Any disagreement: stop, as in Task 2.
- [ ] `water_check.UNREAD` and `WATER_SHADER["ps_read"]` (`{register: components read}`) are
      written from the interpreter's own account (a made case run once with each component of each
      register changed in turn: a component whose change moves no output of any of 64 cases is
      unread), and the provisional table's second, third and fourth lines are marked. The
      components the plan's author listed are the expectation; what the machine says stands.
- [ ] `lighting-formulas.md`, "Water": the pixel, block by block with its instructions; the
      spec's "What the research has differently" points 6 and 7 as they turned out. Commit "Water
      formulas: the pixel shader against the microcode, sixteen mutations, the read components".

### Task 4: The formulas against the captured rows; the dead draw on rows and masks; the mirrored camera; pins and the table

**Files:** `water_check.py`, `tools/xdk_sigmatch/water_pins.py` (new, as `impostor_pins.py`),
`tools/xdk_sigmatch/gen_water_table.py` (new, as `gen_impostor_table.py`),
`src/native/capture/water_table.inc` (generated, committed; no C++ includes it before Task 9),
`tests/test_water_formulas.py`, `tests/test_gen_water_table.py` (new), `lighting-formulas.md`, frame
map section 20 "The formulas against the captures", this plan (the provisional table's lines).

**Consumes:** the six captures in hand; Tasks 2 and 3; `capture_replay.rows(path, kinds, frame)`,
`vs_hash`, `ps_hash`, `Draw` (line 153: a row's vertex stream and bank); `impostor_check.row_bank`,
`row_pixel_bank`, `row_literals`, `pixel_register` (lines 939, 950, 927, 791) as the pattern of a
row reader; `WE/s_views.py` (the mirrored camera, to become a mode), `WE/s_rows.py`,
`WE/b_masks.py` (the masks' alpha census), `WE/b_water.py`; `impostor_pins.lines` / `inc_lines`
and `gen_impostor_table.generate` / `main --check` as the models.

**Produces:**
- `water_check.py rows <capture>...`: for every `A17D` and `F6D9` row of both frames, at 200 pixel
  positions inside the row's mesh (triangles from the row's index and vertex dumps, the position by
  `water_vertex`, barycentric points whose screen place lies inside the picture), with the row's
  own vertex bank, pixel registers (`"pfull"`), literal table (`"ps_lit"`) and bools, and **one
  sampler callback for both sides** that reads the capture's dumps (the mask, the normal map's base
  level, the picture of the row's own slot 13, zero for slot 14, the frame's fog tables):
  `water_pixel` against the interpreter, Task 3's bounds; **and, counted apart, every component a
  formula reads is finite in every row** (`UNREAD` is not looked at). Mutations as in Tasks 2 and
  3, each failing on the capture.
- `water_check.py dead <capture>...`: parts (b) and (c) of criterion 2: `water_opaque_dead` of every
  `F6D9` row's `c141`; and for every dumped mask, with its row's `c141`, the count of texels whose
  alpha is 1 (must be 0) and the largest alpha (R's `b_masks_d1.txt`, made again for every
  capture).
- `water_check.py views <capture>...`: criterion 3. `WE/s_views.py` as a mode: every view (a run of
  rows at surface pitch 280 between resolves) against `diag(c31.x, c31.y, 1, 1) * M * Mirror(h)`,
  `M` the main scene's `c0..c3` of a `BEAD` row, `c31` that row's pixel register, `h` **the height
  of the meshes that sample the view's picture** (the k-th view's picture is the k-th smallest
  slot-13 base; the height from the `BEAD` rows' vertex dumps): within 1e-5 relative in every row
  of the matrix; `VIEW_MUTATIONS = ("factor", "plane", "height")` (the factor left out; the mirror
  about z = 0; `h` moved by 0.01), each of which must fail.
- `water_check.py bools <capture>...`, `states <capture>...`, `bounds <capture>...`: both bool
  members of every water row and the bits of `b136`, `b137` in each; the rows' state words by pixel
  shader (`2200`, `2201`, `2202`, `210E`, the cull, the colour mask), each "one value in N of N
  rows" or its values; the smallest and largest of each read component over the captures given.
- `water_pins.py`: `lines()` with the sections `// water vertex pins`, `// water alpha pins`,
  `// water pixel pins` (captured rows' pixels and made ones: the inputs as hex float bits, then
  the outputs of the **single-precision** reference, `water_pixel(..., single=True)`: sub-project
  12's ledger ruling 26), `// water view pins` (three captured views: `M`, `c31.xy`, `h`, the
  sixteen floats), `// water dead pins`; `inc_lines()` for `tests/native/water_pins.inc` (Task 10
  commits the file).
- `gen_water_table.py` -> `src/native/capture/water_table.inc`, X-macro lines:
  `FABLE2_WATER_SHADERS(vs, ps, ps_opaque)`, `FABLE2_WATER_VS_REGISTER(reg, read_mask)`,
  `FABLE2_WATER_PS_REGISTER(reg, read_mask, kind)` (in the record's order; `read_mask`: bit k set
  when component k is read by a formula; `kind`: 0 a place within 1e6, 1 a matrix term within 1e7,
  2 a factor, direction or exponent within 65,536: fitted to no capture, sub-project 12's ledger
  ruling 54), `FABLE2_WATER_LITERAL(reg, bits_x, bits_y, bits_z, bits_w)`,
  `FABLE2_WATER_BOOL(name, mask)`, `FABLE2_WATER_FETCH_SLOT(index, slot)`; `--check` exits 1 when
  the committed file differs.

- [ ] Tests first: `tests/test_gen_water_table.py` (a made shader entry gives every line above
      once, registers ascending; a register named twice, a read mask of 0 on a register a bound is
      asked for, and a bool mask with two bits are refused; the committed file equals the
      generator's output); `tests/test_water_formulas.py`: `rows` on a made capture of one row
      (two triangles of a made flat mesh, a made mask of two texels) counts 200 positions and no
      failure; the same row with a read component made no number is counted `not-finite` and fails
      the run; with an **unread** component made no number it passes; `views` on a made capture
      (a made `M`, `h` = 20, the view's rows `diag(2/3, 2/3, 1, 1) * M * Mirror(20)`) agrees and
      each of the three mutations fails; `dead` on a made mask with one texel of 0 and `c141` =
      (2, 2, 0, 0) reports one texel of alpha 1 and fails.
- [ ] `rtk python tools/xdk_sigmatch/water_check.py rows native_discovery_20261011_155547.jsonl
      native_discovery_20261011_155731.jsonl native_discovery_20261011_221449.jsonl
      native_discovery_20261011_155929.jsonl native_discovery_20261011_160104.jsonl
      native_discovery_20261011_160251.jsonl > out/native-evidence/water/t4_rows.txt`: every row
      (24, 22, 24, 6, 6 and 6 a frame expected: R B1) agrees; each mutation fails. **Criterion 1's
      second half.** Any row that disagrees: stop; ruling 11 applies; the report names the rows and
      the instruction the trace first differs at.
- [ ] `water_check.py dead <the six> > t4_dead.txt`: **criterion 2**, parts (b) and (c) (expected:
      the condition in every `F6D9` row; no texel of alpha 1; the largest alpha 0.997).
- [ ] `water_check.py views <the five with views> > t4_views.txt`: **criterion 3** (expected: 16
      of 16 within 1e-6 as `WE/s_views_*.txt`; the three mutations fail). CAP `160251` is added if
      its views are whole.
- [ ] `water_check.py bools`, `states`, `bounds` on the six: `t4_bools.txt`, `t4_states.txt`,
      `t4_bounds.txt`. Mark the provisional table: the bool word and its bits; `F6D9`'s state
      words and the alpha reference; `sizeof(WaterRecord)` (the lists stand, or the number is
      recomputed and written).
- [ ] `rtk python tools/xdk_sigmatch/gen_water_table.py` and commit `water_table.inc`;
      `water_pins.py > out/native-evidence/water/t4_pins.txt`.
- [ ] Frame map "The formulas against the captures"; commit "Water formulas against the captures:
      rows, the dead draw, the mirrored camera, the bools and states, pins and the table".

### Task 5: The mask's and the normal map's decoders (after Task 1)

**Files:** `src/native/render/texture_decode.h`, `tests/native/test_texture_decode.cpp`,
`tools/xdk_sigmatch/water_textures.py` (new), `tests/test_water_textures.py` (new), frame map
"The water's textures".

**Consumes:** `capture::TiledOffset2D` (`xenos_tiling.h`), `DecodeDxt3aLevel` (`texture_decode.h`
line 399) and `Decode1555Level` (445) as the models, `DecodeDxnLevel` (520), `DecodeTextureFetch`
(67), `TexFormatFromXenos` (28), `SamplerIndex` (542); the tool-mode pattern of
`FABLE2_TF17_BIN` / `FABLE2_TF17_FC` (`test_texture_decode.cpp` lines 740 to 810);
`impostor_pictures.py` (`decode_1555`, `compare`) as the Python twin's model;
`terrain_replay.py`'s DXN reader, if it has one, for the normal map's Python side.

**Produces:**

```cpp
// texture_decode.h, beside Decode1555Level. NEITHER format is entered in TexFormatFromXenos (spec Design 6;
// sub-project 12's ruling 14): no record's material and no Resolve of the cache changes.
inline constexpr uint32_t kXenosK8 = 2, kXenosDxn = 49;
// k_8 (Xenos format 2: the lake's shore masks) -> R8, width * height bytes row-major: one byte a texel at
// TiledOffset2D(x, y, pitch_texels, 0), or linear at y * ((pitch_texels + 31) & ~31) + x. False, with out_r8
// left empty, for a null source or output, an empty image, a side above 8,192, a pitch below the width or a
// texel outside src_size (nothing is read past it).
inline bool DecodeK8Level(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height, uint32_t pitch_texels,
                          bool tiled, std::vector<uint8_t>* out_r8);
// A k_DXN level as two 8-bit channels (spec ruling 20): DecodeDxnLevel's pairs, each value v in 0..1 written as
// uint8(v * 255 + 0.5). width * height * 2 bytes row-major. False, with out_rg8 left empty, as DecodeDxnLevel.
inline bool DecodeDxnLevelRg8(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height, uint32_t pitch_blocks,
                              bool tiled, uint32_t endian, std::vector<uint8_t>* out_rg8);
enum class WaterDecode : uint8_t { kK8, kDxn };
// A 2D fetch constant of the kind's format, as DecodeTextureFetch reads one (the same fields from the same
// bits); out->format stays kUnsupported and out->xenos_format is the kind's. kFormat for any other format,
// kNotTexture / kNot2D / kSize as DecodeTextureFetch; and kSize for a mask above 256 x 256 or a normal map
// above 1,024 x 1,024 (the capture's `fetch` rule again).
inline FetchError WaterTextureFetch(const uint32_t fc[6], WaterDecode kind, TextureFetch* out);
```

`water_textures.py`: `decode_k8(data, width, height, pitch, tiled)`, `decode_dxn_rg8(...)` (numpy;
the Python transcriptions), `mask(capture_dir, texture_row)`, `normal(capture_dir, texture_row,
level)`, `mean_levels(base)` (the 2 x 2 mean pyramid that stands in for the stored levels until
W1), `sheet <capture>` (the twelve masks and the normal map as one PNG), `fetches <capture>` (the
fetch constants of slots 1, 2, 13, 14 of every water row, decoded: size, pitch, tiled, endian,
swizzle, clamp, filter, levels), `compare <bin> <fc>` (against the C++ test's `<bin>.cpp.r8` or
`.cpp.rg8`).

- [ ] Tests first (`tests/native/test_texture_decode.cpp`, after Task 1's numbers; the tiled
      places are `TiledOffset2D`'s own, which `tests/native/test_uv_decode.cpp`'s neighbours and
      the SDK's rule hold; here the decoder is held to reading **at** them):

```cpp
  // --- DecodeK8Level, DecodeDxnLevelRg8, WaterTextureFetch (plan Task 5) ---
  {
    if (TexFormatFromXenos(kXenosK8) != TexFormat::kUnsupported) return 220;      // (Review Focus 4: no record's material changes)
    if (TexFormatFromXenos(kXenosDxn) != TexFormat::kUnsupported) return 221;
    const uint32_t w = 64, h = 64, pitch = 64;
    std::vector<uint8_t> src(16384, 0);                                           // more than the level spans in any tiling
    auto at = [&](uint32_t x, uint32_t y) { return size_t(uint32_t(fable2::native::capture::TiledOffset2D(int32_t(x), int32_t(y), pitch, 0))); };
    if (at(0, 0) != 0 || at(1, 0) != 1) return 222;                               // the two places that need no rule
    src[at(0, 0)] = 7; src[at(1, 0)] = 255; src[at(0, 1)] = 31; src[at(33, 2)] = 99; src[at(63, 63)] = 200;
    std::vector<uint8_t> out;
    if (!DecodeK8Level(src.data(), src.size(), w, h, pitch, true, &out) || out.size() != size_t(w) * h) return 223;
    if (out[0] != 7 || out[1] != 255 || out[w] != 31 || out[2 * w + 33] != 99 || out[63 * w + 63] != 200 || out[2] != 0) return 224;
    if (DecodeK8Level(src.data(), at(63, 63), w, h, pitch, true, &out) || !out.empty()) return 225;       // the last texel outside: nothing
    if (DecodeK8Level(nullptr, src.size(), w, h, pitch, true, &out) || DecodeK8Level(src.data(), src.size(), 0, h, pitch, true, &out) ||
        DecodeK8Level(src.data(), src.size(), w, h, 32, true, &out) || DecodeK8Level(src.data(), src.size(), 8193, 1, 8224, false, &out)) return 226;
    // linear: rows of the pitch aligned to 32 texels, one byte a texel (the masks of 8 x 16 are such, if their constant says so)
    std::vector<uint8_t> lin(32 * 16, 0);
    lin[32 * 3 + 5] = 77;
    if (!DecodeK8Level(lin.data(), lin.size(), 8, 16, 8, false, &out) || out.size() != 128 || out[3 * 8 + 5] != 77 || out[0] != 0) return 227;
    // the normal map's level as two bytes a texel: DecodeDxnLevel's values, rounded to nearest
    uint8_t blk[16] = {255, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24};   // x: all a0 = 255; y: all a1 = 128
    std::vector<float> xy;
    std::vector<uint8_t> rg;
    if (!DecodeDxnLevel(blk, sizeof(blk), 4, 4, 1, false, 0, &xy) || !DecodeDxnLevelRg8(blk, sizeof(blk), 4, 4, 1, false, 0, &rg)) return 228;
    if (rg.size() != 32) return 229;
    for (size_t i = 0; i < 32; ++i) {
      if (rg[i] != uint8_t(xy[i] * 255.0f + 0.5f)) return 230;
    }
    if (rg[0] != 255 || rg[1] != 128) return 231;
    if (DecodeDxnLevelRg8(blk, 15, 4, 4, 1, false, 0, &rg) || !rg.empty()) return 232;
    // the fetch: the kind's format alone, the cache's own reader still refusing both
    FcArgs a;
    a.format = kXenosK8; a.w = 64; a.h = 64; a.pitch = 64; a.endian = 0; a.base = 0x1BD03000; a.mip = 0; a.mip_max = 0;
    a.packed = false; a.clamp_x = 2; a.clamp_y = 2;
    uint32_t p[6];
    MakeFc(a, p);
    TextureFetch t;
    if (DecodeTextureFetch(p, &t) != FetchError::kFormat) return 233;
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 64 || t.height != 64 || t.base_phys != 0x1BD03000u ||
        t.format != TexFormat::kUnsupported || t.xenos_format != kXenosK8 || t.clamp_x != 2) return 234;
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kFormat) return 235;
    a.w = 512; a.h = 512; a.pitch = 512;
    MakeFc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kSize) return 236;        // above 256 x 256
    a.format = kXenosDxn; a.w = 256; a.h = 256; a.pitch = 256; a.base = 0x14D40000; a.mip = 0x14D50000; a.mip_max = 8;
    MakeFc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNone || t.mip_max != 8 || t.mip_phys != 0x14D50000u || LevelCount(t) != 9) return 237;
    a.w = 2048; a.h = 2048; a.pitch = 2048;
    MakeFc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kSize) return 238;
    a.dim = 0; a.w = 256; a.h = 256; a.pitch = 256;
    MakeFc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNot2D) return 239;
  }
  // FABLE2_K8_BIN / FABLE2_K8_FC and FABLE2_WDXN_BIN / FABLE2_WDXN_FC: a dumped mask and the dumped normal map's
  // base level through the two decoders, written to <bin>.cpp.r8 and <bin>.cpp.rg8 for tests/test_water_textures.py
  // (as FABLE2_TF17_BIN above). Unset: one SKIPPED line each.
```

      (The BC4 block's index bytes above are the plan author's: indices all 1 for the second block
      is `0x49, 0x92, 0x24` repeated, indices all 0 for the first is zeros. The step's red run
      shows whether they are; if `rg[1]` is not 128 the implementer writes the block with the
      test's own packing helper and says so. The assertion that binds is 230: the bytes are
      `DecodeDxnLevel`'s values.)
      `tests/test_water_textures.py`: the same five mask texels through `decode_k8` give the same
      bytes; a short source raises `ValueError`; `mean_levels` of a 4 x 4 base of one value gives
      levels 2 x 2 and 1 x 1 of that value, and of a base whose left half is 0 and right half 255
      a 2 x 2 level of (0, 255) rows.
- [ ] Run: expected failures (`DecodeK8Level` undeclared; `ModuleNotFoundError:
      water_textures`); implement; rerun: exit 0 and OK.
- [ ] On the real bytes, no run needed (CAP `155547`'s dumps): `rtk python
      tools/xdk_sigmatch/water_textures.py fetches native_discovery_20261011_155547.jsonl >
      out/native-evidence/water/t5_fetches.txt` (the masks' tiled bit, size and clamp; the normal
      map's swizzle, endian, levels and filter; the pictures' constants: **the swizzle of slot 13
      is written into the provisional table as one of criterion 4's two candidates**);
      `water_textures.py sheet` and the sheet viewed (twelve masks: land dark or light, a shore
      line; the normal map: a ripple pattern, not noise; said honestly). Each of the twelve masks
      and the normal map's base through the C++ tool mode and `water_textures.py compare`: equal
      byte for byte. The masks' alpha census with `c141` equals `WE/b_masks_d1.txt`.
- [ ] Mark the provisional table's lines (the normal map's channels, the masks' layout). Frame
      map "The water's textures". Commit "Water textures: the shore mask's decoder, the normal map
      as two bytes a texel, their fetch, the Python twins".

### Task 6: The offline replays: the game's own picture, the mirrored sky, the gap before

**Files:** `tools/xdk_sigmatch/water_replay.py` (new), `tests/test_water_replay.py` (new), frame map
"The replays" (criteria 4 and 5 with their figures), this plan (the provisional table).

**Consumes:** Tasks 3 to 5; `foliage_replay.fragments` (line 214), `raster` (285), `fit_place`
(607), `round_place` (649), `load_frame` (866), `Frame`, `window`, `stored_depth`,
`FIXED_TOLERANCE`, `DEPTH_BASE` (`0x1A02A000`: the game's depth copy), `draw_indices` (362);
`foliage_trace.read_dump` (line 99: a stored native scene) and `scene_frame_of` (279);
`light_formulas.sky` (749), `fog_tables.py` (the frame's two tables from a capture);
`post_replay.py`'s reader of the game's scene `0x199E0000` (`k_16_16_16_16_EXPAND`);
`WE/b_gap.py` (the measure of R B2), `WE/b_noreflection.py`; the SDK translator's level rule
(`thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`).

**Produces** (`water_replay.py`; every subcommand prints its inputs' file names and exits 1 when a
bound it states is missed):
- `scene <capture> <frame> [--flip] [--swap-rb] [--no-glint] [--levels mean|stored]`: the frame's
  `A17D` rows through `water_vertex`, rasterised at the sampling place fitted on the capture's own
  unmoving draws, depth-tested against the game's depth copy, shaded by `water_pixel` with the
  capture's dumps (the mask, the normal map at the level the fetch takes, the picture of the row's
  own slot 13 read by the row's fetch constant: its swizzle, wrap and filter), over a bed of 0.
  Printed: kept pixels; those with alpha of 0.99 or more ("judged"); for the judged pixels, whole
  and apart by slot-13 base: replay over the game's scene, mean red, green, blue; the correlation
  of the two luminances; the verdict against criterion 4's bounds (0.93 to 1.07 a channel,
  correlation at least 0.7, at least 5,000 judged pixels). `--flip` reads the picture upside down,
  `--swap-rb` exchanges red and blue: the two controls.
- `sky <capture> <frame>`: criterion 5. `light_formulas.sky` along the rays of
  `diag(c31.x, c31.y, 1, 1) * M * Mirror(h)` for each texel of a 256 x 256 picture (the ray of a
  texel's centre, as `SkyRayBasis` forms it: rows 0, 1 and 3), at exposure 1, saturated to 8 bits,
  zero where the ray's third component is below 0; against each dumped picture on the game's sky
  texels (below the horizon row; not darker than 0.8 of the picture's sky; at least three texels
  from any darker texel): the mean ratio a channel (bound 0.95 to 1.05) and the first row with
  content against the derived horizon (bound: within one row).
- `gap <capture> <scene.f2ns>`: `WE/b_gap.py`'s figures again on the current pair (the "before" of
  every later figure), with the replay's kept pixels as the footprint beside R's.
- `lookup <capture> <frame>`: for each kept pixel the place its slot-13 lookup lands, and whether
  that texel and every texel within 8 of it is sky in the game's picture ("sky-sampling pixels":
  criterion 8's set), written as `t6_lookup_<stamp>.npy`.
- `glint <capture> <frame>`: the kept pixels whose glint term is above 5% of the pixel
  (criterion 12's first part).
- `edge <capture> <frame>`: the kept pixels within 20 units of the eye, and those with an alpha
  between 0.0588 and 0.99 (criterion 11's sets; W3's test).

- [ ] **The level rule first.** Read the translator's fetch (`dxbc_translator_fetch.cpp`: how a
      `tfetch2D` without `UseComputedLOD=false` forms its level from the coordinate's derivatives,
      the fetch constant's LOD bias and the mip range) and write the rule into the frame map with
      the file's lines; `water_replay.level(du_dx, dv_dx, du_dy, dv_dy, size, fetch)` is that
      rule. If the rule cannot be read as a formula: the level is the one of the larger screen
      derivative in texels, rounded down, said to be so, and criterion 4's band stays the loose
      one.
- [ ] Tests first (`tests/test_water_replay.py`): a made frame of one flat quad of 40 x 20 pixels
      with a made mask (left half 0: water; right half 1: land) keeps the left half's pixels and
      none of the right; with a made picture whose upper half is (0.2, 0.4, 0.8) and lower half 0,
      a made game scene equal to the replay's own output passes criterion 4's measure, the same
      scene with `--flip` misses the correlation bound, and with `--swap-rb` misses the channel
      band; `level` of a coordinate that moves one texel a pixel is 0 and of one that moves four
      texels is 2; `sky` of a made view looking 10 degrees up puts the first row with content at
      the row the horizon's formula gives; a judged set below 5,000 pixels reads "not decidable",
      never "met".
- [ ] **Criterion 4**, in this order, every output kept (`t6_scene_<stamp>_<variant>.txt`):
      1. `water_replay.py scene native_discovery_20261011_155547.jsonl 1` with the picture read by
         its fetch constant's own swizzle, not flipped (the spec's inference): the two bounds on
         the pixels of `0x13501000`, and apart on those of `0x13401000`.
      2. The same with `--flip`, and with `--swap-rb`, and with both.
      3. The same four on `native_discovery_20261011_155731.jsonl`.
      **The verdict's rule, stated before the numbers are seen:** the orientation and the channel
      order are those of the one variant that meets both bounds on the pixels of `0x13501000` in
      both captures **while each of the other three misses a bound in at least one of them**. If
      the spec's variant is that one: criterion 4 is met and open question 2 is closed as the
      spec inferred. If another single variant is: the provisional table's line is rewritten, the
      report says the spec's inference is refuted, and the controller is told before Task 10
      starts. If more than one variant meets both bounds, or none does: **the measure does not
      discriminate or the formula does not close; phase 13b's Tasks 10 to 14 are not started
      (ruling 11) and the report says which.** The pixels of `0x13401000` are read as open
      question 4's answer (a miss there alone is not the formula's failure).
- [ ] **Criterion 5**: `water_replay.py sky` on both captures, each of the four pictures:
      `t6_sky_<stamp>.txt`. The sheet of one picture beside the replay's (`t6_sky_155547.png`),
      viewed and described.
- [ ] `gap` on the three pairs of R B2's table (`221449` + `221453.f2ns`, `155547` +
      `155550.f2ns`, `155731` + `155734.f2ns`): `t6_gap_<stamp>.txt` (expected: R's figures; a
      difference is said). `lookup` on D1 and D2: the share of kept pixels that are sky-sampling,
      by base (the spec's "13.5% to 30.3%" was the whole footprint against each picture: this is
      each body's pixels against its own).
- [ ] **Criterion 12's first part, and W4**: `glint` on `native_discovery_20261011_160104.jsonl`
      and `_160251.jsonl`, frame 1: the count of kept pixels with a glint above 5% (a view counts
      with at least 500). **W3's test on the captures in hand**: `edge` on all six (expected: none
      within 20 units; the nearest distance printed). Both results into the provisional table.
- [ ] Frame map "The replays"; mark the provisional table. Commit "Water replays: the formula
      with the game's own picture, the mirrored sky, the gap before; orientation and channel
      order decided".

### Task 7: The translator's rules for the water text, and the test's frame (after Task 4)

**Files:** `tools/xdk_sigmatch/wind_hlsl.py`, `tests/test_water_hlsl.py` (new),
`tests/test_water_literals.py` (new), `tests/test_wind_hlsl.py` and `tests/test_impostor_hlsl.py`
(must stay green, unchanged).

**Consumes:** `wind_hlsl.translate(header_text, begin, end, functions)` (line 120: its rules),
`Program`, `Region`, `Vec`, `Untranslated`, `IMPOSTOR_VERTEX`, `IMPOSTOR_PIXEL` (lines 34 to 37);
`tests/test_impostor_hlsl.py` as the model (its `MUTANTS` tuple, `test_a_line_no_rule_knows_stops_
the_translation`, `test_an_unread_component_reaches_no_result`); `water_pins.py`,
`gen_water_table.py`.

**Produces:** in `wind_hlsl.py`: `WATER_VERTEX = ("// WATER VERTEX TEXT BEGIN\n", "// WATER VERTEX
TEXT END\n", ("WaterCoords",))` and `WATER_PIXEL = ("// WATER PIXEL TEXT BEGIN\n", "// WATER PIXEL
TEXT END\n", ("WaterAlpha", "WaterNormalSum", "WaterNormal", "WaterLookup", "WaterFresnel",
"WaterBase", "WaterMix", "WaterSun"))`; the rules the water text needs and the tool does not have
yet, each added with a test: `sqrt`, `abs` and `rsqrt` of a scalar or a `Vec`, `dot` of two
`Vec`s, `lerp(a, b, t)`, `normalize` **refused** (the text must write the reference's own
`x * rsqrt(dot(x, x))`), a callback argument (`WaterSample13(uv)` translated to a call of a Python
callable the test supplies). `tests/test_water_hlsl.py`: `Texts` (reads the header's two regions),
`cases()` (200 made inputs in which every register component differs and **every component no
formula reads is no number**: `water_check.UNREAD`), `reference(case)` (the `light_formulas`
values the functions must give), `compare(program, case)`, `MUTANTS` (empty until Task 12 fills
it), and the tests below. `tests/test_water_literals.py`: the register lists and literals across
`light_formulas.WATER_SHADER`, `water_table.inc` and `water_pins.py` (names, order, bits); Tasks
9, 10 and 11 add the C++ and HLSL sides.

- [ ] Tests first. The translator's new rules are held on a **fixture text inside the test** (so
      that they can fail today), and the header's own text is held by tests that **skip, with a
      message that names Task 12, while the header has no water region**:

```python
FIXTURE = '''// WATER PIXEL TEXT BEGIN
float WaterAlpha(float m, float4 c141) {
  precise float ramp = saturate(c141.x - (1.0 - m) * c141.y);
  return 1.0 - ramp;
}
float2 WaterNormalSum(float2 a, float2 b) {
  return (a * 2.0 + -1.0) + (b * 2.0 + -1.0);
}
float3 WaterNormal(float2 s, float2 a, float2 b, float c128x) {
  precise float2 da = a * 2.0 + -1.0;
  precise float2 db = b * 2.0 + -1.0;
  precise float z = sqrt(abs(1.0 - dot(da, da))) + sqrt(abs(1.0 - dot(db, db)));
  precise float3 n = float3(s.x * 0.5, s.y * 0.5, (c128x * 0.5) * z);
  return n * rsqrt(dot(n, n));
}
// WATER PIXEL TEXT END
'''


class Rules(unittest.TestCase):
    NAMES = ("WaterAlpha", "WaterNormalSum", "WaterNormal")

    def program(self, text=FIXTURE):
        return wind_hlsl.Program(text, "// WATER PIXEL TEXT BEGIN\n", "// WATER PIXEL TEXT END\n", self.NAMES)

    def test_the_fixture_is_the_reference(self):
        p = self.program()
        self.assertAlmostEqual(p.call("WaterAlpha", 0.0, wind_hlsl.Vec(2.003, 2.0, math.nan, 0.0588)), lf.water_alpha(0.0, (2.003, 2.0, 0.0, 0.0)), places=12)
        s, n = lf.water_normal((0.3, 0.9), (0.6, 0.2), 0.3)
        got = p.call("WaterNormal", wind_hlsl.Vec(*s), wind_hlsl.Vec(0.3, 0.9), wind_hlsl.Vec(0.6, 0.2), 0.3)
        for a, b in zip(got, n):
            self.assertAlmostEqual(a, b, places=12)

    def test_a_line_no_rule_knows_stops_the_translation(self):
        for bad in ("  return normalize(n);", "  float3 n = mul(m, v);", "  clip(a - 0.5);"):
            with self.assertRaises(wind_hlsl.Untranslated):
                self.program(FIXTURE.replace("  return n * rsqrt(dot(n, n));", bad))

    def test_a_missing_function_stops_it(self):
        with self.assertRaises(wind_hlsl.Untranslated):
            wind_hlsl.Program(FIXTURE, "// WATER PIXEL TEXT BEGIN\n", "// WATER PIXEL TEXT END\n", self.NAMES + ("WaterSun",))

    def test_a_wrong_text_is_not_the_reference(self):
        p = self.program(FIXTURE.replace("(1.0 - m) * c141.y", "m * c141.y"))
        self.assertNotAlmostEqual(p.call("WaterAlpha", 0.25, wind_hlsl.Vec(2.003, 2.0, 0.0, 0.0588)), lf.water_alpha(0.25, (2.003, 2.0, 0.0, 0.0)))


HEADER = (ROOT / "src" / "native" / "fable2_native_shaders.h").read_text(encoding="utf-8")
NO_TEXT = "the header has no water text yet: plan Task 12 writes it and these tests must then RUN (a skip after Task 12 is a failure of that task)"


@unittest.skipUnless(wind_hlsl.WATER_PIXEL[0] in HEADER, NO_TEXT)
class TheText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vertex = wind_hlsl.Program(HEADER, *wind_hlsl.WATER_VERTEX)
        cls.pixel = wind_hlsl.Program(HEADER, *wind_hlsl.WATER_PIXEL)

    def test_the_translation_reads_both_texts_whole(self):
        for program, names in ((self.vertex, wind_hlsl.WATER_VERTEX[2]), (self.pixel, wind_hlsl.WATER_PIXEL[2])):
            self.assertEqual(sorted(program.functions), sorted(names))

    def test_the_text_is_the_reference_at_made_inputs(self):
        for case in cases(MADE):
            self.assertEqual(compare(self.vertex, self.pixel, case, TOLERANCE), [], case.name)

    def test_the_text_is_the_reference_at_the_pins(self):
        pins = [pin_case(line) for line in water_pins.lines() if not line.startswith("//")]
        self.assertGreaterEqual(len(pins), 24)
        for case in pins:
            self.assertEqual(compare(self.vertex, self.pixel, case, PIN_BOUND), [], case.name)

    def test_an_unread_component_reaches_no_result(self):
        for case in cases(MADE):                                   # every UNREAD component of a made case is no number
            got = evaluate(self.vertex, self.pixel, case)
            self.assertTrue(all(math.isfinite(x) for values in got.values() for x in values), case.name)

    def test_each_wrong_text_fails(self):
        self.assertGreaterEqual(len(MUTANTS), 12)
        for name, text, changed, breaks in MUTANTS:
            self.assertEqual(HEADER.count(text), 1, name)          # each site exactly once in the header
            wrong = HEADER.replace(text, changed)
            vertex, pixel = wind_hlsl.Program(wrong, *wind_hlsl.WATER_VERTEX), wind_hlsl.Program(wrong, *wind_hlsl.WATER_PIXEL)
            self.assertGreater(max(leave(vertex, pixel, case, breaks) for case in cases(MADE)), MUTANT_FLOOR, name)
```

      The helpers these five use are written by this task and are exercised today through
      `Rules` and a stand-in program made of `FIXTURE`: `MADE = 200`, `TOLERANCE = 1e-9`,
      `PIN_BOUND = 1e-4`, `MUTANT_FLOOR = 1e-4` (as `tests/test_impostor_hlsl.py` has them);
      `cases(n)` (made inputs: a position, the pair, the mask's and the normal map's samples, the
      picture's texel, the pixel's place, both bools, the registers of `WATER_SHADER` with every
      component different and every `UNREAD` component no number); `evaluate(vertex, pixel, case)`
      (a dict of the texts' results by name: `coord`, `alpha`, `sum`, `normal`, `lookup`,
      `fresnel`, `base`, `mix`, `sun`, each flattened to floats); `reference(case)` (the same dict
      from `light_formulas`); `compare(vertex, pixel, case, bound)` (the names whose values differ
      by more than `bound * max(1, |value|)`); `leave(vertex, pixel, case, names)` (the largest
      such difference among `names`); `pin_case(line)` (a line of `water_pins.lines()` as a
      case). `wind_hlsl.Program`'s constructor, `functions` and `call` are written here as the
      plan's author reads `wind_hlsl.py` lines 182 to 228; the implementer uses the tool's real
      names and says where they differ. `MUTANTS` is empty until Task 12 writes the header's
      text and fills it.
- [ ] `rtk python -m unittest tests.test_water_hlsl tests.test_water_literals -v`: `Rules` fails
      (`Untranslated` for `sqrt`, or `AttributeError: WATER_PIXEL`), `TheText` reports 5 skipped
      with the message. Implement the rules; rerun: `Rules` OK, `TheText` skipped (5), the
      literals test OK; `tests.test_wind_hlsl` and `tests.test_impostor_hlsl` OK and unchanged.
- [ ] Commit "Water HLSL test frame: the translator's rules for the water text, the made inputs,
      the literals across Python and the table".

*Phase 13a ends here: criteria 1 to 5 are read (Tasks 3, 4, 6), nothing in the renderer has
changed, no game was started. The phase's review reads Tasks 1 to 7 together.*

---
## Phase 13b: the capture, the surface, the reflection target with the sky

Entry conditions: the user's leave for game launches (ledger ruling 7) for every step marked
NEEDS_RUN; Task 4's `rows` check agreed on every captured row and Task 6 decided criterion 4
(ruling 11). If either formula condition failed, Task 9 alone is built and the sub-project ends
there with the formula's report. Order: Task 8 is the controller's runs on Task 1's build and may
be made as soon as the leave is given, during phase 13a too; Tasks 9 and 10 may run side by side
(Task 9: the capture and `frame_scene.h`; Task 10: `clay_logic.h` and its test), one builder at a
time; then Tasks 11, 12, 13 in order. Task 14's search runs need only Task 1's build and are best
made in Task 8's session.

### Task 8: Discovery captures W1 and W2 and what they say (the controller's runs; no code)

**Files:** frame map section 20 "Discovery captures"; `tools/xdk_sigmatch/water_check.py`,
`water_replay.py`, `water_textures.py` and their tests only for what a capture shows to be wrong
in a reader; this plan (the provisional table). Evidence in WE, untracked.

- [ ] NEEDS_RUN, by the controller, on Task 1's build (its SHA-256's first 16 digits recorded),
      one session, **full read-back runs** (PAIR holds READBACK), the water's switch absent:
      1. `w1_bridge`: BRIDGE with `-GameArgs BASE,PAIR`
      2. `w2_third`: THIRD with `-GameArgs BASE,PAIR`

      Copies wanted in `out/native-evidence/water/`: each run's driver output, log, four shots
      (`w<n>_<name>_shot_<s>s.png`), capture (`.jsonl` with its `native_tex_` and `native_geo_`
      folders) and stored native scene (`.f2ns`). Task 14's three EDGE runs are made in the same
      session if the controller has the time (their lines and copies are Task 14's).
- [ ] Before anything is read: the freeze signature; `rtk python
      tools/xdk_sigmatch/check_discovery.py --rows-only <capture>` on both (`--rows-only`: these
      runs do not log the vertex bindings): no problem, and `water rows: N checked, 0 problems`
      (24 + the views' rows a frame at the bridge); two `"frame"` rows a capture; the stored scene's
      `scene_frame` is the capture's first frame's.
- [ ] **What W1 must show** (Design 1's table), each "shown" or "not shown" with its file:
      four slot-13 pictures in each of the two frames, each with content below row 101
      (`water_textures.py sheet`, viewed); the normal map with nine levels (`"mips"`); `"vp"` on the
      views' rows, and what it holds; the game's scene and depth copy; the stored native scene of
      the same frame. **W2** the same for its four bodies (20.1024 twice, 27.52, 40.0).
- [ ] The checks of phase 13a on the new captures, both frames: `water_check.py rows`, `dead`,
      `views`, `bools`, `states` (`t8_*.txt`): every one as on the captures in hand, or the
      difference said and, if it is a formula's, ruling 11.
- [ ] **Open question 3**: the stored levels against the 2 x 2 mean (`water_textures.py`: the
      largest and the mean difference a level); `water_replay.py scene ... --levels stored` on both
      frames of both captures with Task 6's variant and its three others: **criterion 4, made
      again**; the band reached with the stored levels is written beside Task 6's.
      **Open question 4**: the pictures of `0x13401000` in frames 1 and 2 against each other and
      against view 1's content. **Open question 5**: the views' viewport and scissor from `"vp"`
      (a 256 x 256 viewport, or what else), the clip control word. **Open question 7**: which views
      are followed by `F186`, in both frames of both captures and in the six captures in hand.
      **Criterion 5 again** on each frame's own pictures.
- [ ] `water_replay.py gap` on both pairs: **the "before" of this binary** (`t8_gap_*.txt`).
- [ ] Mark the provisional table (the levels; the viewport and clip registers). Frame map
      "Discovery captures". Commit "Water discovery captures: W1, W2, the stored levels, the views'
      viewport" (the frame map and this plan alone are tracked).

### Task 9: The capture's side block of water records; the switch

**Files:** `src/native/capture/water_record.h` (new, pure), `src/native/capture/water_table.inc`
(Task 4's), `src/native/capture/capture.cpp` / `capture.h`, `src/native/render/frame_scene.h`,
`src/native/fable2_native_render.cpp` (the switch; `SetWaterWanted` beside `SetWindWanted`, line
1293; the install line beside the wind's, line 1193), `tests/native/test_water_record.cpp` (new;
two lines in the runner), `tests/native/test_frame_scene.cpp`, `tests/native/test_draw_record.cpp`
(the size's assert restated), `tools/xdk_sigmatch/water_check.py` (`records`),
`tests/test_water_formulas.py`, `tests/test_water_literals.py` (the C++ side), README (the
switch's row, the counters), frame map "Water records".

**Consumes:** `impostor_record.h` as the pattern (`CheckImpostorCall`, `MakeImpostorRecord`,
`ImpostorReasonName`, `ImpostorFogMatches` at line 451); `FillMaterial` (`capture.cpp` line 3544:
the `no-albedo` branch at 3584 to 3586), `DrawShaders` (3235), `RecordDraw` (5121: `FillMaterial`
at 5154, `ReadStateWords` at 5155, the lock at 5227, `NextSeq` at 5240, the early return at 5228 to
5231), `EndMainSceneFrame`'s latch (5745 to 5760), `AppendFrameImpostors` (5640) as the row's
pattern; `ChooseVs`, `ChoosePs`, `LookupVs`, `LookupPs`, `ReadLiteralRegisters` (1198),
`ReadConstantRegisters` (3204), `VertexBank` (971), `ReadPixelConstant` (1618), `ReadTextureFetch`
(4192), `DecodeVertexFetches`; the followed bool word (`FollowedBoolWord`, `t_impostor_pbool`,
`g_pbool_follow`, `kFollowImpostors`, `g_impostor_epoch`: lines 164 to 175, 915 to 926, 1365 to
1390; `ImpostorBoolWord`, 4948); `ImpostorRegistersAreBankOnly` (4828) and `IsBufferLoadRegister`
(`light_block.h`); `StateWords`, `DecodeRenderState` (`render_state.h`); `FogBlock`
(`fog_block.h` line 39), `FrameBuilder::SetFog` and `fog_source.base` (`frame_scene.h` 819);
`FrameBuilder::ImpostorsOn` / `ImpostorSeen` / `AddImpostor` / `Finish` (935 to 975, 1176 to 1218)
and `ImpostorLine` (492); `CurrentWind` (`fable2_native_render.cpp` line 444) and `PollFrame`
(1293 to 1295).

**Interfaces (produce exactly; the lines marked PROVISIONAL are amended from Task 4's report in
this task's first commit, each amended line marked "As fixed by Task 4"):**

```cpp
// water_record.h, namespace fable2::native::capture
inline constexpr uint32_t kMaxWaterRecords = 64;                                   // spec ruling 10
inline constexpr uint32_t kWaterVsRegisters = 4, kWaterPsRegisters = 24;           // PROVISIONAL (Task 4)
// From water_table.inc (X-macro lines; a static assert holds the table well formed: registers ascending and
// below 248, no register named twice, a read mask not 0, each bool mask one bit):
//   kWaterVs, kWaterPs, kWaterOpaquePs (the hashes); kWaterVsRegister[], kWaterPsRegister[] (the registers, in
//   the record's order); kWaterVsRead[], kWaterPsRead[] (which components a formula reads); kWaterPsKind[]
//   (0 a place, 1 a matrix term, 2 a factor); the literals c254, c255; kWaterB136, kWaterB137.
enum : uint32_t { kWatV115, kWatV116, kWatV117, kWatV118 };
enum : uint32_t { kWatP0, kWatP19, kWatP20, kWatP28, kWatP31, kWatP66, kWatP67, kWatP68, kWatP116, kWatP117, kWatP126, kWatP127,
                  kWatP128, kWatP129, kWatP130, kWatP131, kWatP136, kWatP137, kWatP138, kWatP139, kWatP140, kWatP141, kWatP142,
                  kWatP143 };                                                      // PROVISIONAL with the count
enum : uint32_t { kWatMask, kWatNormal, kWatPicture, kWatNear };                   // WaterRecord::fetch: slots 1, 2, 13, 14
inline constexpr uint32_t kWaterFetchSlot[4] = {1, 2, 13, 14};
inline constexpr uint32_t kWaterStrideDwords = 5, kWaterPairOffsetDwords = 3;      // position 32_32_32_FLOAT at 0, the pair 32_32_FLOAT at 3
inline constexpr float kWaterPlaceMax = 1e6f, kWaterMatrixMax = 1e7f, kWaterFactorMax = 65536.0f;   // by kind; fitted to no capture
enum class WaterKind : uint8_t { kNone, kSurface, kOpaque };                       // A17D; F6D9
inline constexpr const char* kWaterRecordReason = "water-record";
enum class WaterReason : uint8_t { kNone, kUnread, kShader, kLiteral, kFetchList, kRegister, kScale, kState, kFetch,
                                   kShadowTerm, kFog, kOverLimit, kCount };
inline const char* WaterReasonName(WaterReason r);   // "none", "unread", "shader", "literal", "fetch-list", "register", "scale", "state",
                                                     // "fetch", "shadow-term", "fog", "over-limit"

// One water draw of the frame. An invalid record is all zero (ps_hash 0: WaterRecordValid).
struct WaterRecord {
  uint64_t ps_hash, vs_hash;
  uint32_t place;                            // DrawRecord::seq of its draw (FrameBuilder::NextSeq)
  uint32_t kind;                             // WaterKind
  uint32_t pbool;                            // the pixel bool word as its last writer left it on the GPU
  uint32_t stride_dwords;                    // the vertex stream's stride (kWaterStrideDwords)
  float vs[kWaterVsRegisters][4];            // c115..c118
  float ps[kWaterPsRegisters][4];            // the table's pixel registers, in its order; unread components as the game left them
  uint32_t fetch[4][6];                      // slots 1, 2, 13, 14
  uint32_t fog_base[2];                      // the base addresses of slots 4 and 5 at the draw
};
static_assert(sizeof(WaterRecord) == 584, "WaterRecord grew or shrank: the documents state its size");   // PROVISIONAL

// What was read at the draw (capture.cpp fills it on the no-albedo branch).
struct WaterDraw {
  bool read = false;                         // the device, both shaders, every register, constant and fetch were read
  bool pbool_known = false;                  // the last writer's bool word could be said (ImpostorBoolWord's rule)
  uint64_t ps_hash = 0, vs_hash = 0;
  bool literals_match = false;               // the pixel shader object's own literal table holds the table's c254 and c255
  uint32_t streams = 0, stride_dwords = 0;   // the vertex shader's fetch list
  uint32_t position_format = 0, position_offset = 0, pair_format = 0, pair_offset = 0;   // 57 at 0, 37 at 3
  float vs[kWaterVsRegisters][4] = {};
  float ps[kWaterPsRegisters][4] = {};
  uint32_t pbool = 0, fetch[4][6] = {}, fog_base[2] = {};
};
constexpr WaterKind WaterKindOf(uint64_t vs_hash, uint64_t ps_hash);   // kNone unless the pair is BEAD with A17D or F6D9
// kNone, or the first rule that fails, in this order: kUnread (the draw or its state words not read, or the bool
// word not known); kShader (not
// the pair); kLiteral; kFetchList (not one stream of 5 dwords with the position at 0 and the pair at 3 in their
// formats); kRegister (a component A FORMULA READS is not finite or beyond its kind's bound; c31.xy outside (0, 4];
// c131.x, c128.x, c136.x outside [0, 64]; c141.w outside [0, 1]; c0.zw not 1 / the scene's size within 1e-6
// relative); kScale (c20.w outside (0, 64]); kState (kind surface: anything but blend source alpha and inverse
// source alpha, depth tested and written with function 4, alpha test "greater" with a reference in [0, 1], no
// alpha-to-mask, four colour channels; kind opaque: anything but Task 4's measured words); kFetch (slot 1 not a
// 2D k_8 texture of at most 256 x 256; slot 2 not a 2D k_DXN texture of at most 1,024 x 1,024; slot 13 not a 2D
// k_8_8_8_8 texture of 256 x 256 without a gamma sign: render::WaterTextureFetch, render::WaterPictureFetch);
// kShadowTerm (bool b137 set). An unread component is never looked at.
WaterReason CheckWaterDraw(const WaterDraw& d, const StateWords& state, uint32_t scene_width, uint32_t scene_height);
// The record of a draw: CheckWaterDraw, then the copy. No record (all zero) on any failure. `place` is 0: the
// caller sets it (the draw's seq).
WaterRecord MakeWaterRecord(const WaterDraw& d, const StateWords& state, uint32_t scene_width, uint32_t scene_height, WaterReason* why);
inline bool WaterRecordValid(const WaterRecord& r) { return r.ps_hash != 0; }
// Whether a record's fog is the frame's validated block: its slots 4 and 5 at the frame's two tables by base
// address, and c28.xy, c66.xyz, c67.x, .z, .w and c68.xyz bit-equal to the block's amount, axis, range and near colour.
bool WaterFogMatches(const WaterRecord& r, const FogBlock& block, const uint32_t table_base[2]);

// frame_scene.h
//   FrameScene: std::vector<capture::WaterRecord> water;   // whole or empty (spec ruling 11)
//               bool water_on = false;                      // the switch as latched for this frame
//               uint32_t water_seen[2] = {};                // the frame's draws of the pair: [0] surface, [1] opaque
//               uint32_t water_taken = 0;                   // records that passed every rule (water.size() or, refused whole, what would have been)
//               uint32_t water_refused[size_t(capture::WaterReason::kCount)] = {};
//   inline std::string WaterLine(const FrameScene&);        // "off" | "seen S (opaque O), taken T | water-record {reason: n, ...}"
//   FrameBuilder::WaterOn(bool on); WaterSeen(capture::WaterKind kind);
//   FrameBuilder::AddWater(const capture::WaterRecord& record, capture::WaterReason why);
//     // why != kNone: counted by reason. A valid record past kMaxWaterRecords: kOverLimit.
//   FrameBuilder::Finish: each held record by WaterFogMatches (kFog; a frame without a valid fog block: every
//     record kFog); then, if ANY reason was counted this frame, the scene's `water` stays empty; else the
//     records are the scene's. The counts are written either way. Nothing of one frame is left in the next.

// capture.h
void SetWaterWanted(bool wanted);   // the consumer's switch: the native view on and fable2_native_water on
```

`capture.cpp` (**nothing below is reached with the switch off but the one load and branch named
first**):
- `g_water_wanted`, and `g_water_frame` latched at the frame's boundary in `EndMainSceneFrame`
  after the impostors' (line 5760): `records && g_water_wanted`; with it `g_builder.WaterOn`, and
  the bool word's following: a bit `kFollowWater = 4` in `g_pbool_follow`, set and cleared as
  `kFollowImpostors` is, `t_impostor_pbool` followed when either bit is set, `g_impostor_epoch`
  bumped whenever either following begins (the impostors may then lose one frame's records as
  `unread` when the water's switch comes on in a running game: said in the frame map).
- In `FillMaterial`, inside `if (albedo->slot < 0)`, before the `return`:
  `if (g_water_frame.load(std::memory_order_relaxed)) ObserveWaterDraw(device, shaders, m.ps_hash);`
  **That load and branch are all a `no_albedo` draw executes with the switch off; a draw whose
  pixel shader has an albedo entry executes nothing new there.** `ObserveWaterDraw` returns at once
  unless `WaterKindOf` names the pair; else it fills this thread's `t_water_draw` (a `WaterDraw` and
  its kind): the vertex registers from the bank (`ReadConstantRegisters(VertexBank(device), 115, 4,
  ...)`), the pixel registers through `ReadPixelConstant`, the four fetch constants and the bases
  of slots 4 and 5 through `ReadTextureFetch`, the literal verdict once a shader object (as
  `t_impostor_literals`), the fetch list from the vertex shader's decoded fetches, the bool word by
  `ImpostorBoolWord`'s rule. A static assert, as the quads and the impostors have, that no register
  read from the bank can be named by a constant-buffer load (`WaterRegistersAreBankOnly`).
- In `RecordDraw`: after `ReadStateWords` (line 5155), `if (t_water_draw.kind != WaterKind::kNone)`
  the record is made outside the lock (`MakeWaterRecord` with the draw's own state words and the
  scene's size, 1120 x 720: `ClayPass::kWidth`, `kHeight`'s source constant); under the lock, after
  `NextSeq` (5240): `WaterSeen(kind)`, the record's `place = r.seq`, a draw record that is itself
  skipped (`r.skip != kNone`) refused as `unread`, then `AddWater`. The early return of a draw the
  scene no longer holds (5228 to 5231) clears `t_water_draw` as it clears the impostor's look.
  **This is one thread-local load and one branch for every recorded draw, with the switch on or
  off**: the hand-over needs the draw's sequence number, which exists only there ("Where this plan
  departs", point 12).
- The frame row's `"water"`: `"off"`, or `{"on":true,"seen":{"surface":S,"opaque":O},"taken":T,
  "refused":{"<reason>":n,...},"records":[{"place":N,"kind":"surface"|"opaque","vs":[16 hex
  words],"ps":[96 hex words],"fetch":[[6 hex words] x 4],"fog_base":["0x...","0x..."],
  "pbool":"0x..."}]}` (the records also when the scene's block was emptied: evidence).
- Log: `[native] water: off|on (fable2_native_water)` at install; every 300 frames, only with the
  switch on, `[native] capture: frame N water: seen S (opaque O), taken T | water-record in F of W
  frames {reason: n}`.

`fable2_native_render.cpp`: `REXCVAR_DEFINE_STRING(fable2_native_water, "off", "Fable2", ...)` after
`fable2_native_impostor_feed` (line 144); `bool CurrentWater()` as `CurrentWind` (anything but the
switch's words is off, with one warning that names them: `render::kClaySwitchWords`);
`capture::SetWaterWanted(wanted && CurrentWater())` in `PollFrame`.

`water_check.py records <capture>`: every record of every frame row against the `"draw"` row
whose sequence place it names (the n-th recorded draw of the frame): the vertex registers against
the row's `"bank"`, the pixel registers against `"pfull"`, the fetch constants against `"tf"`, the
bool word against `"pbool_load"`, the hashes; bit for bit; `seen` against the frame's `BEAD` rows
by pixel shader; exit 1 on any difference.

- [ ] **Step 0, before the first build of this task**: ask the controller to copy the build
      folder as it stands (`out\build\win-amd64-release` to
      `out\build\win-amd64-release-pre13b`; criterion 15's "commit before"), and amend the
      PROVISIONAL lines from Task 4's report.
- [ ] Tests first. `tests/native/test_water_record.cpp` (new):

```cpp
// The water records' rules (sub-project 13, plan Task 9): pure, no game.
#include <cmath>
#include <cstring>
#include <limits>
#include "../../src/native/capture/water_record.h"
using namespace fable2::native::capture;

namespace {
uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
// A draw every rule accepts: the bridge's first A17D row in shape (values of WE/b_water_d1_f1.txt, rounded).
WaterDraw Good(WaterKind kind = WaterKind::kSurface) {
  WaterDraw d;
  d.read = d.pbool_known = d.literals_match = true;
  d.vs_hash = kWaterVs;
  d.ps_hash = kind == WaterKind::kOpaque ? kWaterOpaquePs : kWaterPs;
  d.streams = 1; d.stride_dwords = 5; d.position_format = 57; d.position_offset = 0; d.pair_format = 37; d.pair_offset = 3;
  const float garbage = std::numeric_limits<float>::quiet_NaN();
  for (auto& r : d.vs) for (float& v : r) v = garbage;                 // every component starts as garbage:
  for (auto& r : d.ps) for (float& v : r) v = garbage;                 // only what a formula reads is set below
  auto set = [](float* r, std::initializer_list<float> v) { int i = 0; for (float x : v) r[i++] = x; };
  set(d.vs[kWatV115], {1.0365f, 1.1775f}); set(d.vs[kWatV116], {-2.3504f, 1.0095f});
  set(d.vs[kWatV117], {0.08f, 0.08f});     set(d.vs[kWatV118], {0.06f, 0.06f});
  set(d.ps[kWatP0], {0.0f, 0.0f, 1.0f / 1120.0f, 1.0f / 720.0f});
  set(d.ps[kWatP19], {82.07f, 157.34f, 50.17f});
  d.ps[kWatP20][3] = 8.58f;
  set(d.ps[kWatP28], {1.0f, 1.0f});
  set(d.ps[kWatP31], {0.666667f, 0.666667f});
  set(d.ps[kWatP66], {-0.83f, 0.09f, 0.55f});
  d.ps[kWatP67][0] = 19.07f; d.ps[kWatP67][2] = 1.0f; d.ps[kWatP67][3] = 0.0005f;
  set(d.ps[kWatP68], {0.1f, 0.2f, 0.3f});
  d.ps[kWatP116][0] = 0.104081f; d.ps[kWatP117][0] = 0.0f;
  set(d.ps[kWatP126], {0.027f, 0.075f, 0.043f}); set(d.ps[kWatP127], {0.059f, 0.129f, 0.122f});
  d.ps[kWatP128][0] = 0.3f;
  set(d.ps[kWatP129], {0.84f, 0.84f}); set(d.ps[kWatP130], {0.84f, 0.84f});
  d.ps[kWatP131][0] = 0.5f; d.ps[kWatP136][0] = 0.279f; d.ps[kWatP137][0] = 0.5f; d.ps[kWatP138][0] = 128.0f;
  d.ps[kWatP140][0] = 0.05f;
  d.ps[kWatP141][0] = 2.003f; d.ps[kWatP141][1] = 2.0f; d.ps[kWatP141][3] = 15.0f / 255.0f;
  set(d.ps[kWatP142], {0.5f, -0.5f, -0.7071f}); set(d.ps[kWatP143], {1.0f, 0.9f, 0.8f});
  d.pbool = kWaterB136;
  // slot 1: 2D k_8 64 x 64; slot 2: 2D k_DXN 256 x 256; slots 13, 14: 2D k_8_8_8_8 256 x 256 (dword 0 of the rows: 0x82000002)
  auto fc = [](uint32_t* p, uint32_t format, uint32_t side, uint32_t base, uint32_t d0) {
    p[0] = d0 | ((side >> 5) << 22); p[1] = base | format; p[2] = (side - 1) | ((side - 1) << 13); p[3] = 0; p[4] = 0; p[5] = 1u << 9;
  };
  fc(d.fetch[kWatMask], 2, 64, 0x1BD03000u, 0x80000002u);
  fc(d.fetch[kWatNormal], 49, 256, 0x14D40000u, 0x80000002u);
  fc(d.fetch[kWatPicture], 6, 256, 0x13401000u, 0x80000002u);
  fc(d.fetch[kWatNear], 6, 256, 0x13441000u, 0x80000002u);
  d.fog_base[0] = 0x1A500000u; d.fog_base[1] = 0x1A501000u;
  return d;
}
StateWords State(WaterKind kind) {       // the measured words (Task 4's t4_states.txt); the helper sets exactly the fields CheckWaterDraw reads
  StateWords s{};
  s.read = true;
  s.depth = kind == WaterKind::kOpaque ? 0x00708766u : 0x00708746u;
  s.blend = kind == WaterKind::kOpaque ? 0x00010001u : 0x07060706u;
  s.colour_control = 0x0000000Cu;
  s.alpha_ref = 15.0f / 255.0f;
  s.colour_mask = 0xFu;
  return s;
}
}  // namespace

int main() {
  WaterReason why = WaterReason::kCount;
  // 1 to 5: the good draw of each kind is a record that carries what was read, garbage included, bit for bit
  {
    const WaterDraw d = Good();
    const WaterRecord r = MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
    if (why != WaterReason::kNone || !WaterRecordValid(r)) return 1;
    if (r.kind != uint32_t(WaterKind::kSurface) || r.place != 0 || r.pbool != kWaterB136 || r.stride_dwords != 5) return 2;
    if (std::memcmp(r.vs, d.vs, sizeof(r.vs)) != 0 || std::memcmp(r.ps, d.ps, sizeof(r.ps)) != 0 ||
        std::memcmp(r.fetch, d.fetch, sizeof(r.fetch)) != 0 || r.fog_base[1] != 0x1A501000u) return 3;
    const WaterRecord o = MakeWaterRecord(Good(WaterKind::kOpaque), State(WaterKind::kOpaque), 1120, 720, &why);
    if (why != WaterReason::kNone || o.kind != uint32_t(WaterKind::kOpaque) || o.ps_hash != kWaterOpaquePs) return 4;
    if (WaterKindOf(kWaterVs, kWaterPs) != WaterKind::kSurface || WaterKindOf(kWaterVs, kWaterOpaquePs) != WaterKind::kOpaque ||
        WaterKindOf(kWaterVs, 1) != WaterKind::kNone || WaterKindOf(1, kWaterPs) != WaterKind::kNone) return 5;
  }
  auto refused = [&](WaterDraw d, WaterReason want, WaterKind kind = WaterKind::kSurface) {
    WaterReason got = WaterReason::kCount;
    const WaterRecord r = MakeWaterRecord(d, State(kind), 1120, 720, &got);
    return got == want && !WaterRecordValid(r) && r.ps[0][0] == 0.0f;       // no record: all zero
  };
  // 10 to 16: the rules before the registers, each alone
  { WaterDraw d = Good(); d.read = false; if (!refused(d, WaterReason::kUnread)) return 10; }
  { WaterDraw d = Good(); d.pbool_known = false; if (!refused(d, WaterReason::kUnread)) return 11; }
  { WaterDraw d = Good(); d.ps_hash = 0x1234; if (!refused(d, WaterReason::kShader)) return 12; }
  { WaterDraw d = Good(); d.literals_match = false; if (!refused(d, WaterReason::kLiteral)) return 13; }
  { WaterDraw d = Good(); d.stride_dwords = 6; if (!refused(d, WaterReason::kFetchList)) return 14; }
  { WaterDraw d = Good(); d.pair_offset = 2; if (!refused(d, WaterReason::kFetchList)) return 15; }
  { WaterDraw d = Good(); d.streams = 2; if (!refused(d, WaterReason::kFetchList)) return 16; }
  // 20 to 24 (Review Focus 3): only the components a formula reads are judged
  {
    WaterDraw d = Good();                                         // every unread component IS no number in Good(): accepted (case 1)
    d.ps[kWatP129][2] = std::numeric_limits<float>::infinity(); d.ps[kWatP139][3] = 1e29f; d.ps[kWatP140][3] = 1e22f;
    if (MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why), why != WaterReason::kNone) return 20;
    d = Good(); d.ps[kWatP129][0] = std::numeric_limits<float>::quiet_NaN();
    if (!refused(d, WaterReason::kRegister)) return 21;           // one read component that is no number
    d = Good(); d.ps[kWatP19][1] = 2e6f;
    if (!refused(d, WaterReason::kRegister)) return 22;           // a place beyond 1e6
    d = Good(); d.vs[kWatV117][1] = std::numeric_limits<float>::infinity();
    if (!refused(d, WaterReason::kRegister)) return 23;
    d = Good(); d.ps[kWatP138][0] = 70000.0f;
    if (!refused(d, WaterReason::kRegister)) return 24;           // an exponent beyond 65,536
  }
  // 25 to 29: the named bounds
  { WaterDraw d = Good(); d.ps[kWatP31][0] = 0.0f; if (!refused(d, WaterReason::kRegister)) return 25; }
  { WaterDraw d = Good(); d.ps[kWatP131][0] = 65.0f; if (!refused(d, WaterReason::kRegister)) return 26; }
  { WaterDraw d = Good(); d.ps[kWatP141][3] = 1.5f; if (!refused(d, WaterReason::kRegister)) return 27; }
  { WaterDraw d = Good(); d.ps[kWatP0][2] = 1.0f / 1280.0f; if (!refused(d, WaterReason::kRegister)) return 28; }   // not 1 / the scene's width
  { WaterDraw d = Good(); d.ps[kWatP20][3] = 0.0f; if (!refused(d, WaterReason::kScale)) return 29; }
  // 30 to 36: the state, by kind
  {
    StateWords s = State(WaterKind::kSurface);
    s.blend = 0x00010001u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 30;      // a surface that does not blend
    s = State(WaterKind::kSurface); s.depth = 0x00708742u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 31;      // (ledger ruling 27) no depth write is not the game's water
    s = State(WaterKind::kSurface); s.alpha_ref = 1.5f;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 32;
    s = State(WaterKind::kSurface); s.colour_mask = 0x7u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 33;
    s = State(WaterKind::kSurface); s.read = false;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kUnread) return 34;
    if (MakeWaterRecord(Good(WaterKind::kOpaque), State(WaterKind::kSurface), 1120, 720, &why), why != WaterReason::kState) return 35;
    if (MakeWaterRecord(Good(), State(WaterKind::kOpaque), 1120, 720, &why), why != WaterReason::kState) return 36;
  }
  // 40 to 45: the fetches and the shadow term
  { WaterDraw d = Good(); d.fetch[kWatMask][1] = 0x1BD03000u | 49; if (!refused(d, WaterReason::kFetch)) return 40; }
  { WaterDraw d = Good(); d.fetch[kWatNormal][1] = 0x14D40000u | 18; if (!refused(d, WaterReason::kFetch)) return 41; }
  { WaterDraw d = Good(); d.fetch[kWatPicture][2] = 127u | (127u << 13); if (!refused(d, WaterReason::kFetch)) return 42; }   // 128 x 128
  { WaterDraw d = Good(); d.fetch[kWatPicture][1] = 0; if (!refused(d, WaterReason::kFetch)) return 43; }
  { WaterDraw d = Good(); d.fetch[kWatNear][1] = 0; if (refused(d, WaterReason::kFetch)) return 44; }   // slot 14 is no texture of the native draw: not judged
  { WaterDraw d = Good(); d.pbool |= kWaterB137; if (!refused(d, WaterReason::kShadowTerm)) return 45; }
  // 50 to 54: the fog rule, bit for bit
  {
    FogBlock b{};
    b.valid = true;
    const WaterDraw d = Good();
    const WaterRecord r = MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
    b.amount[0] = d.ps[kWatP28][0]; b.amount[1] = d.ps[kWatP28][1];
    for (int i = 0; i < 3; ++i) { b.axis[i] = d.ps[kWatP66][i]; b.near_colour[i] = d.ps[kWatP68][i]; }
    b.range[0] = d.ps[kWatP67][0]; b.range[2] = d.ps[kWatP67][2]; b.range[3] = d.ps[kWatP67][3];
    b.range[1] = 123.0f;                                           // c67.y: not read by the water, not compared
    const uint32_t base[2] = {0x1A500000u, 0x1A501000u};
    if (!WaterFogMatches(r, b, base)) return 50;
    FogBlock other = b; other.amount[0] = std::nextafter(b.amount[0], 2.0f);
    if (WaterFogMatches(r, other, base)) return 51;
    other = b; other.valid = false;
    if (WaterFogMatches(r, other, base)) return 52;
    const uint32_t swapped[2] = {0x1A501000u, 0x1A500000u};
    if (WaterFogMatches(r, b, swapped) || WaterFogMatches(r, b, nullptr)) return 53;
    other = b; other.range[3] = -b.range[3];
    if (WaterFogMatches(r, other, base)) return 54;
  }
  if (std::strcmp(WaterReasonName(WaterReason::kFetchList), "fetch-list") != 0 || std::strcmp(WaterReasonName(WaterReason::kShadowTerm), "shadow-term") != 0 ||
      std::strcmp(WaterReasonName(WaterReason::kOverLimit), "over-limit") != 0 || size_t(WaterReason::kCount) != 12 || Bits(1.0f) != 0x3F800000u) return 60;
  return 0;
}
```

      (`StateWords`' field names and the fetch constants' packing are written as the plan's author
      reads `render_state.h` and `texture_decode.h`; the implementer uses the struct's real fields
      and the file's own way of making a constant, and keeps every case's meaning and number.)
      `tests/native/test_frame_scene.cpp` (after the file's last number, 216): **220**: with
      `WaterOn(false)`, `WaterSeen` and `AddWater` leave the scene's counts 0, `water` empty and
      `water_on` false; **221**: 24 good records (12 of each kind) with a matching fog block are
      the scene's 24, in their order, `water_seen` {12, 12}, `water_taken` 24, no reason;
      **222**: the same with one record refused at `AddWater` (`register`): **`water` is empty**,
      `water_refused[register]` 1, `water_seen` unchanged; **223**: one record whose fog does not
      match: `water` empty, `fog` 1; **224**: a frame without a valid fog block: `water` empty,
      `fog` 24; **225**: the 65th good record: `water` empty, `over-limit` 1; **226**: a draw seen
      (`WaterSeen`) that got no `AddWater`: `water` empty, `unread` 1; **227**: nothing of one
      frame is in the next (a refused frame, then a good one); **228**: `WaterLine` of 221's scene
      is `seen 24 (opaque 12), taken 24 | water-record {}` and of an off scene `off`.
      `tests/native/test_draw_record.cpp` (151): `sizeof(DrawRecord) == 616` restated beside the
      water record's 584. `tests/test_water_formulas.py`: `records` on a made capture (a frame row
      with one record and its `"draw"` row) agrees; one register word changed fails; a record
      whose place names another row fails.
- [ ] Run: `rtk clang++ -std=c++23 tests\native\test_water_record.cpp -o
      out\tests\runtime-unit-tests\water_record.exe` (expected: `water_record.h` not found);
      `test_frame_scene.cpp` (expected: `WaterOn` undeclared). Implement the header and the
      builder; rerun: exit 0. Implement the capture and the switch; both suites green; `rtk
      .\build.cmd -release fable_2`.
- [ ] NEEDS_RUN (the build changes no binding, target or shader text: no software-adapter run is
      due), one session, **full read-back runs**:
      1. `r9_bridge_on`: BRIDGE with `-GameArgs BASE,PAIR,WATER`
      2. `r9_third_on`: THIRD with `-GameArgs BASE,PAIR,WATER`
      3. `r9_bridge_off`: BRIDGE with `-GameArgs BASE,PAIR`

      Copies as `r9_*` (driver output, log, shots, capture, stored scene).
- [ ] From the runs. `check_discovery.py --rows-only` on the three. **Criterion 6, first
      reading**: `water_check.py records` on runs 1 and 2: every record bit for bit; `seen` =
      `taken` = the frame's `BEAD` rows (24 and 22 expected); no reason. **Off**: run 3's frame
      rows say `"water":"off"`, its log has the one line `[native] water: off`, and its picture
      is today's (the pass draws nothing new in any of the three: the native shots of 1 and 3 are
      compared in the bridge's regions and said to agree or not). The capture medians of runs 1
      and 3 (the 300-frame lines), reported: this is not a timing run.
- [ ] README (the switch's row after `--fable2_native_impostors`, line 101; the counters); frame
      map "Water records"; `tests/test_water_literals.py` gains the C++ side (`water_record.h`'s
      enums against the table). Commit "Water records: a bounded side block of both pixel shaders
      linked by sequence number, whole or empty; the switch".

### Task 10: The draw's rules and mirrors (pure; may start beside Task 9)

**Files:** `src/native/render/clay_logic.h` (a new section "Native water" after "Native
impostors", before "Native wind", line 7474; `ClayStats`; `ColourOrder`, `ColourDue`,
`ColourStepsDue`, `ColourStepsAfter` at 5556 to 5594; `ClayFrameSlotPlan`, `PlanClayFrameSlots`,
`ClayFrameSmallSlots` at 5818 to 5838), `tests/native/test_clay_logic.cpp`,
`tests/native/water_pins.inc` (generated by `water_pins.py`, committed), frame map "The water's
rules".

**Consumes:** Tasks 4 and 9 (`water_record.h`; Task 10 may start on Task 4's table alone and take
`WaterRecord` when Task 9 has landed its header); `PositionBounds` (line 6975); `SkyRayBasis`
(3711), `MakeClaySkyConstants` (3751), `ClaySkyConstants`; `ClayFrameConstants` (2868) and
`ClayFogConstants`; `FogCoords` (3255) and the ground form's mirror; `SkyPlace`, `SkyOrderItem`
(276), `PlanCoverageOrder` (1868), `kNoCoverageTail`, `PlanImpostorPlace` (7359) and
`ImpostorPlacesOneRun` (7379) as the patterns; `ClayImpostorConstants` and
`MakeClayImpostorConstants` (7062, 7141), `ImpostorPixelMirror` (7340) as the pattern of a
single-precision mirror under `#pragma clang fp contract(off)`; `FormatImpostorText` (7423);
`kPostSceneMax`; the pins' reader of case 820 to 829 (`impostor_pins.inc`).

**Produces** (PROVISIONAL lines amended from Task 4's and Task 6's reports first):

```cpp
// --- Native water (frame-map section 20; sub-project 13) ---
// The dead draw (spec ruling 12): F6D9 kills every pixel, whatever the mask holds, when the saturate's argument
// is positive at both ends of the mask: min(c141.x, c141.x - c141.y) >= 1e-6 with |c141.x|, |c141.y| <= 4, in
// single precision (the margin stands above the rounding of the two single steps at magnitudes up to 4: 2.4e-7
// each). False for a value that is no number.
inline constexpr float kWaterDeadMargin = 1e-6f;
constexpr bool WaterOpaqueDead(const float c141[4]);
// The mirrored camera (spec ruling 15): diag(c31x, c31y, 1, 1) * M * Mirror(h), Mirror(h) the reflection in the
// plane z = h, computed in double from the frame's view-projection (rows . (p, 1)). False, `out` untouched, for
// a value that is no number, a factor outside (0, 4] or a height beyond 1e6.
inline bool WaterViewRows(const float view_proj[4][4], float c31x, float c31y, float h, float out[4][4]);
// A mesh's height from the box of its positions (GeometryCache: computed once a buffer): valid iff the box is
// valid and hi[2] - lo[2] <= 1e-3; h is lo[2].
struct WaterHeight { bool valid = false; float h = 0.0f; };
constexpr WaterHeight WaterMeshHeight(const PositionBounds& b);
// The targets (spec ruling 16): one for each distinct slot-13 base of the frame's surface records, ascending,
// at most kMaxWaterTargets. target_of[i]: record i's target. ok false with `why`: kPlane (a mesh that is not
// flat, or two heights on one base beyond 1e-3), kReflection (a fifth base).
inline constexpr uint32_t kMaxWaterTargets = 4, kWaterTargetSize = 256;
enum class ClayWater : uint8_t { kDrawn, kOff, kNoRecords, kLight, kOpaqueLive, kPair, kPlane, kView, kReflection, kTexture,
                                 kOrder, kPipeline, kSlot, kScale, kCount };
inline const char* ClayWaterName(ClayWater w);   // "drawn", "off", "no-records", "light", "opaque-live", "pair", "plane", "view",
                                                 // "reflection", "texture", "order", "pipeline", "slot", "scale"
struct WaterTargetPlan {
  bool ok = false;
  ClayWater why = ClayWater::kDrawn;
  uint32_t count = 0, base[kMaxWaterTargets] = {};
  float height[kMaxWaterTargets] = {};
};
inline WaterTargetPlan PlanWaterTargets(const capture::WaterRecord* const* surface, const WaterHeight* heights, size_t count, uint8_t* target_of);
// The frame's verdict (spec Design 5): kDrawn, or the first reason in the enum's order.
struct ClayWaterInputs {
  bool on = false;                     // the switch now, in a scene captured with it
  uint32_t surface = 0, opaque = 0;    // the scene's records by kind (0 and 0: the scene's block is empty)
  bool light = false;                  // the frame is lit: the linear target's meaning
  bool opaque_dead = true;             // WaterOpaqueDead of every opaque record's c141
  bool planes = true;                  // PlanWaterTargets did not say kPlane
  bool view = true;                    // every record's draw has the frame's one view-projection, row layout kDot
  bool targets = true;                 // PlanWaterTargets did not say kReflection, and the frame's sky is drawn with its constants
  bool objects_targets = true;         // the targets and the mirrored sky's objects exist
  bool textures = true;                // every mask and the normal map resolved, every pair buffer made
  bool order = true;                   // PlanWaterPlace: usable
  bool objects = true;                 // the surface step's layout, programs and pipelines exist
  bool slots = true;                   // the frame's buffer exists and holds the step's slots
  bool scale = true;                   // every record's c20.w is the frame's output scale within a thousandth
};
constexpr ClayWater DecideClayWater(const ClayWaterInputs& in);
//   off: !on. no-records: surface == 0. light. opaque-live: !opaque_dead. pair: surface != opaque. plane. view.
//   reflection: !targets || !objects_targets. texture. order. pipeline: !objects. slot. scale.
// Whether a record of `kind` is left out of the pre-pass and the colour pass (drawn by the water step, or not at
// all): an opaque record only with the verdict kDrawn; a surface record always (today it is blend-unknown).
constexpr bool WaterLeavesOut(ClayWater verdict, capture::WaterKind kind);
// What the head of Render can say before the frame's buffer is sized (the scene's own rules): the verdict with
// every later input true. The step's slots are reserved iff this is kDrawn.
constexpr ClayWater WaterSceneVerdict(const ClayWaterInputs& in);
// Where the step is drawn: before the prepared record at `at` (count: after the last), after the steps already
// due at that index (the tail, the layer's depth, the sky step). `seqs`: the prepared records' DrawRecord::seq
// in the frame's order, the dead draws left out; `place`: the smallest place of the frame's surface records.
// usable iff the sky step (`sky_at`, with `sky_drawn`) and the early records' tail (`tail_at`; kNoCoverageTail:
// none) lie at or before `at`: a frame in which either would come after the water is `order`.
struct WaterPlace { bool usable = false; size_t at = 0; };
constexpr WaterPlace PlanWaterPlace(const uint32_t* seqs, size_t count, uint32_t place, bool sky_drawn, size_t sky_at, size_t tail_at);
// ColourOrder gains `size_t water_at = ~size_t(0)`; ColourDue gains `bool water`; ColourStepsDue: due.water =
// order.water_at == index (the loop draws it LAST of the index's steps, after due.sky); ColourStepsAfter:
// due.water = water_at != ~size_t(0) && water_at >= count (the loop draws it after the sky and the tail).

// The record's constants (cbuffer Water of shaders::kClayWaterVs and kClayWaterPs, register b0 of the step's
// layout; shaders::kClayWaterFields is its layout). What the pixel program reads beyond them is the frame's
// block at b2 (the fog's tables and registers, held to the record's by the capture's `fog` rule).
inline constexpr uint32_t kClayWaterPsRegisters = 17;      // c0, c19, c31, c116, c117, c126..c131, c136, c137, c138, c140, c141, c142, c143
struct ClayWaterConstants {
  float rows[4][4];                     // the draw record's rows (layout kDot)
  float vs[4][4];                       // c115..c118
  float ps[kClayWaterPsRegisters][4];   // unread components as the game left them: the program names every register by component
  float alpha_ref, out_scale;           // the draw's alpha reference; the frame's output scale
  uint32_t sun_on, fog_on;              // bool b136; the frame's fog
  uint32_t sampler_mask, sampler_normal, sampler_picture;   // static samplers (SamplerIndex of each fetch)
  uint32_t swizzle;                     // the mask's channel in bits 0 to 2, the normal's x in 3 to 5, its y in 6 to 8 (0 to 3 a channel, 4: 0, 5: 1)
};
static_assert(sizeof(ClayWaterConstants) == 432);          // PROVISIONAL: 64 + 64 + 272 + 16 + 16
inline constexpr uint32_t kClayWaterSlots = (sizeof(ClayWaterConstants) + 255) / 256;                       // 2
inline constexpr uint32_t kClayWaterStepSlots = kClayWaterSlots * capture::kMaxWaterRecords + kMaxWaterTargets + kClayFrameSlots;   // 153
// False, `out` untouched, unless every component a formula reads is finite (again: nothing reaches a shader on
// the strength of the capture's rule alone), the scale is in (0, 64] and the alpha reference in [0, 1].
inline bool MakeClayWaterConstants(const capture::WaterRecord& r, const capture::DrawRecord& draw, float alpha_ref, float out_scale,
                                   bool fog_on, const uint32_t samplers[3], uint32_t swizzle, ClayWaterConstants* out);
// PlanClayFrameSlots(terrain_drawn, plants, impostor_slots = 0, water_slots = 0): ClayFrameSlotPlan gains water0 =
// impostor0 + impostor_slots; object0 = water0 + water_slots. ClayFrameSmallSlots gains water_slots likewise.
// Within the step's slots: record i's constants at water0 + kClayWaterSlots * i; target k's sky constants at
// water0 + kClayWaterSlots * kMaxWaterRecords + k; the second frame block at the next kClayFrameSlots slots.
// The second frame block: the frame's with the exposure 1 and 1 / the target's size (spec Design 7).
inline ClayFrameConstants WaterFrameBlock(const ClayFrameConstants& frame);
// WATER MIRRORS BEGIN (single precision, the reference's order, fp contract off; pinned by water_pins.inc)
inline void WaterCoordsMirror(const float p[3], const ClayWaterConstants& c, float coord[4]);
inline float WaterAlphaMirror(float m, const ClayWaterConstants& c);
struct WaterPixelIn { float q[2], place[3], a[2], b[2], m, picture[3]; float scatter[3], transmit[3]; };
// colour before the frame's output scale is applied is not returned: `out` is what the program writes
// ((v > 0) ? min(v, kPostSceneMax) : 0 a channel); `alpha` the output's; `kept`: not killed, alpha > reference.
inline void WaterPixelMirror(const WaterPixelIn& in, const ClayWaterConstants& c, const ClayFogConstants& fog, float out[3], float* alpha, bool* kept);
// WATER MIRRORS END
// ClayStats: water (the verdict), water_on, water_seen, water_drawn, water_targets, water_left_out,
//   water_frames[size_t(ClayWater::kCount)] (frames by verdict since the start), water_capture[] (the capture's
//   reasons), water_ms (the step's own CPU time this frame).
inline std::string FormatWaterText(const ClayStats& st);
//   "Water: off" | "Water: drawn 12 of 12, targets 4, opaque left out 12 | not drawn {reflection: 3 frames} |
//   water-record {register: 1}"
```

- [ ] Tests first (`tests/native/test_clay_logic.cpp`, CHECK 838 to 852; `water_pins.inc`
      generated by `rtk python tools/xdk_sigmatch/water_pins.py --inc >
      tests/native/water_pins.inc`):

```cpp
  // --- Native water (sub-project 13, plan Task 10) ---
  {
    CHECK(838, std::string(ClayWaterName(ClayWater::kDrawn)) == "drawn" && std::string(ClayWaterName(ClayWater::kOpaqueLive)) == "opaque-live" &&
                   std::string(ClayWaterName(ClayWater::kNoRecords)) == "no-records" && size_t(ClayWater::kCount) == 14);
    // 840: the dead draw at its margin, in single precision (Review Focus 2)
    const float captured[4] = {2.003f, 2.0f, 0.0015f, 0.0588f};
    CHECK(840, WaterOpaqueDead(captured));
    const float at_margin[4] = {2.0f + 4.0f * kWaterDeadMargin, 2.0f, 0.0f, 0.0f};      // (2 + 1e-6 is not a single: the step at 2 is 2.4e-7)
    const float below[4] = {std::nextafter(2.0f, 3.0f), 2.0f, 0.0f, 0.0f};               // 2.4e-7 above: below the margin
    const float equal[4] = {2.0f, 2.0f, 0.0f, 0.0f}, land[4] = {0.0f, -1.0f, 0.0f, 0.0f}, large[4] = {5.0f, 1.0f, 0.0f, 0.0f};
    const float nan[4] = {std::numeric_limits<float>::quiet_NaN(), 2.0f, 0.0f, 0.0f};
    CHECK(840, WaterOpaqueDead(at_margin) && !WaterOpaqueDead(below) && !WaterOpaqueDead(equal) && !WaterOpaqueDead(land) &&
                   !WaterOpaqueDead(large) && !WaterOpaqueDead(nan));
    const float garbage_w[4] = {2.003f, 2.0f, 1e29f, std::numeric_limits<float>::quiet_NaN()};   // c141.z and .w are not the rule's
    CHECK(840, WaterOpaqueDead(garbage_w));
    // 841: the mirrored camera at the pins (water_pins.inc, "water view pins": M, c31.xy, h, the sixteen floats)
    for (const WaterViewPin& pin : kWaterViewPins) {
      float got[4][4];
      CHECK(841, WaterViewRows(pin.m, pin.c31x, pin.c31y, pin.h, got));
      for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        CHECK(841, std::fabs(got[i][j] - pin.want[i][j]) <= 1e-5f * std::max(1.0f, std::fabs(pin.want[i][j])));
      }
      float moved[4][4], other[4][4];
      CHECK(841, WaterViewRows(pin.m, pin.c31x, pin.c31y, pin.h + 0.01f, moved) && std::memcmp(moved, got, sizeof(got)) != 0);
      CHECK(841, WaterViewRows(pin.m, 1.0f, 1.0f, pin.h, other) && std::memcmp(other, got, sizeof(got)) != 0);
      float keep[4][4] = {{7.0f}};
      CHECK(841, !WaterViewRows(pin.m, 0.0f, pin.c31y, pin.h, keep) && !WaterViewRows(pin.m, pin.c31x, pin.c31y, 2e6f, keep) && keep[0][0] == 7.0f);
      // 842: the sky's rays do not depend on the height (two bodies' targets are equal in phase 13b), and the
      // picture's centre looks up
      float b1[3][3], b2[3][3];
      CHECK(842, SkyRayBasis(got, b1) && SkyRayBasis(moved, b2) && std::memcmp(b1, b2, sizeof(b1)) == 0);
      CHECK(842, b1[0][2] > 0.0f);
    }
    // 843: the verdict, one reason at a time and in the enum's order; the dead draw is left out iff the water is drawn
    ClayWaterInputs good;
    good.on = good.light = true;
    good.surface = good.opaque = 12;
    CHECK(843, DecideClayWater(good) == ClayWater::kDrawn && WaterSceneVerdict(good) == ClayWater::kDrawn);
    struct { void (*breaks)(ClayWaterInputs&); ClayWater want; bool scene; } const kChain[] = {
        {[](ClayWaterInputs& i) { i.on = false; }, ClayWater::kOff, true},
        {[](ClayWaterInputs& i) { i.surface = 0; i.opaque = 0; }, ClayWater::kNoRecords, true},
        {[](ClayWaterInputs& i) { i.light = false; }, ClayWater::kLight, true},
        {[](ClayWaterInputs& i) { i.opaque_dead = false; }, ClayWater::kOpaqueLive, true},
        {[](ClayWaterInputs& i) { i.opaque = 11; }, ClayWater::kPair, true},
        {[](ClayWaterInputs& i) { i.planes = false; }, ClayWater::kPlane, false},
        {[](ClayWaterInputs& i) { i.view = false; }, ClayWater::kView, false},
        {[](ClayWaterInputs& i) { i.targets = false; }, ClayWater::kReflection, false},
        {[](ClayWaterInputs& i) { i.textures = false; }, ClayWater::kTexture, false},
        {[](ClayWaterInputs& i) { i.order = false; }, ClayWater::kOrder, false},
        {[](ClayWaterInputs& i) { i.objects = false; }, ClayWater::kPipeline, false},
        {[](ClayWaterInputs& i) { i.slots = false; }, ClayWater::kSlot, false},
        {[](ClayWaterInputs& i) { i.scale = false; }, ClayWater::kScale, true},
    };
    for (size_t a = 0; a < std::size(kChain); ++a) {
      ClayWaterInputs in = good;
      kChain[a].breaks(in);
      CHECK(843, DecideClayWater(in) == kChain[a].want);
      CHECK(843, (WaterSceneVerdict(in) == kChain[a].want) == kChain[a].scene && (kChain[a].scene || WaterSceneVerdict(in) == ClayWater::kDrawn));
      for (size_t b = a + 1; b < std::size(kChain); ++b) {      // two things wrong: the earlier decides
        ClayWaterInputs both = in;
        kChain[b].breaks(both);
        CHECK(843, DecideClayWater(both) == kChain[a].want);
      }
    }
    { ClayWaterInputs in = good; in.objects_targets = false; CHECK(843, DecideClayWater(in) == ClayWater::kReflection); }
    for (size_t v = 0; v < size_t(ClayWater::kCount); ++v) {
      CHECK(843, WaterLeavesOut(ClayWater(v), capture::WaterKind::kOpaque) == (ClayWater(v) == ClayWater::kDrawn));
      CHECK(843, WaterLeavesOut(ClayWater(v), capture::WaterKind::kSurface));
      CHECK(843, !WaterLeavesOut(ClayWater(v), capture::WaterKind::kNone));
    }
    // 845: the slot plan with no water slots is the plan of before, written out; with them the objects move behind
    for (uint32_t t : {0u, 1u, 37u, 300u}) for (uint32_t p : {0u, 5u}) for (uint32_t i : {0u, kClayImpostorStepSlots}) {
      const ClayFrameSlotPlan was = PlanClayFrameSlots(t, p, i), now = PlanClayFrameSlots(t, p, i, 0);
      CHECK(845, now.plant0 == kClayFrameTerrainSlot0 + t && now.mask == now.plant0 + p && now.sky == now.mask + 1 && now.post == now.sky + 1 &&
                     now.quad0 == now.post + kClayPostSlots && now.impostor0 == now.quad0 + kClayQuadFrameSlots && now.object0 == now.impostor0 + i &&
                     now.object0 == was.object0 && now.water0 == now.object0);
      const ClayFrameSlotPlan with = PlanClayFrameSlots(t, p, i, kClayWaterStepSlots);
      CHECK(845, with.water0 == now.object0 && with.object0 == now.object0 + kClayWaterStepSlots && with.impostor0 == now.impostor0);
      CHECK(845, ClayFrameSmallSlots(t, p, i, 0) == ClayFrameSmallSlots(t, p, i) && ClayFrameSmallSlots(t, p, i, kClayWaterStepSlots) == ClayFrameSmallSlots(t, p, i) + kClayWaterStepSlots);
    }
    CHECK(845, kClayWaterSlots == 2 && kClayWaterStepSlots == 2 * capture::kMaxWaterRecords + kMaxWaterTargets + kClayFrameSlots);
    // 846 to 848: the step's place (Review Focus 5). The bridge's shape: opaque records, the first surface place
    // at seq 1628, a blended group behind it.
    const uint32_t seqs[] = {10, 20, 700, 1115, 1641, 1642, 1649};
    WaterPlace w = PlanWaterPlace(seqs, std::size(seqs), 1628, true, 4, kNoCoverageTail);
    CHECK(846, w.usable && w.at == 4);                                         // before the first record at or beyond the place; the sky at the same index
    w = PlanWaterPlace(seqs, std::size(seqs), 1628, true, 2, 3);
    CHECK(846, w.usable && w.at == 4);                                         // the sky and the tail earlier: fine
    w = PlanWaterPlace(seqs, std::size(seqs), 1628, true, 5, kNoCoverageTail);
    CHECK(847, !w.usable);                                                     // the sky would come after the water: order
    w = PlanWaterPlace(seqs, std::size(seqs), 1628, true, 4, 6);
    CHECK(847, !w.usable);                                                     // the tail would
    w = PlanWaterPlace(seqs, std::size(seqs), 1628, false, 0, kNoCoverageTail);
    CHECK(847, w.usable && w.at == 4);                                         // (a frame without a sky is `reflection` before it is `order`; the rule itself does not ask)
    w = PlanWaterPlace(seqs, std::size(seqs), 5000, true, 7, kNoCoverageTail);
    CHECK(848, w.usable && w.at == std::size(seqs));                           // after the last record
    w = PlanWaterPlace(nullptr, 0, 5, true, 0, kNoCoverageTail);
    CHECK(848, w.usable && w.at == 0);
    ColourOrder order;
    order.sky_drawn = true; order.sky_at = 4; order.water_at = 4;
    const ColourDue due = ColourStepsDue(order, 4, 900, false, false);
    CHECK(848, due.sky && due.water && !ColourStepsDue(order, 3, 800, false, false).water && !ColourStepsDue(ColourOrder{}, 4, 900, false, false).water);
    order.water_at = std::size(seqs);
    CHECK(848, ColourStepsAfter(order, std::size(seqs)).water && !ColourStepsAfter(ColourOrder{}, std::size(seqs)).water);
    // 849: the targets
    capture::WaterRecord rec[6] = {};
    const capture::WaterRecord* ptr[6];
    WaterHeight hs[6];
    const uint32_t bases[6] = {0x13501000u, 0x13401000u, 0x13401000u, 0x13581000u, 0x13481000u, 0x13501000u};
    const float heights[6] = {20.1024f, 20.1024f, 20.1024f, 40.0f, 40.474f, 20.1024f};
    for (int i = 0; i < 6; ++i) { rec[i].ps_hash = 1; rec[i].fetch[capture::kWatPicture][1] = bases[i] | 6u; ptr[i] = &rec[i]; hs[i] = {true, heights[i]}; }
    uint8_t target_of[6] = {};
    WaterTargetPlan plan = PlanWaterTargets(ptr, hs, 6, target_of);
    CHECK(849, plan.ok && plan.count == 4 && plan.base[0] == 0x13401000u && plan.base[3] == 0x13581000u && plan.height[1] == 40.474f);
    CHECK(849, target_of[0] == 2 && target_of[1] == 0 && target_of[2] == 0 && target_of[3] == 3 && target_of[4] == 1 && target_of[5] == 2);
    hs[2] = {true, 20.2f};
    CHECK(849, !PlanWaterTargets(ptr, hs, 6, target_of).ok && PlanWaterTargets(ptr, hs, 6, target_of).why == ClayWater::kPlane);   // two heights on one base
    hs[2] = {false, 0.0f};
    CHECK(849, PlanWaterTargets(ptr, hs, 6, target_of).why == ClayWater::kPlane);                                                 // a mesh that is not flat
    hs[2] = {true, 20.1024f};
    capture::WaterRecord fifth = rec[0];
    fifth.fetch[capture::kWatPicture][1] = 0x13601000u | 6u;
    ptr[5] = &fifth;
    CHECK(849, !PlanWaterTargets(ptr, hs, 6, target_of).ok && PlanWaterTargets(ptr, hs, 6, target_of).why == ClayWater::kReflection);   // a fifth base
    // 850: a mesh's height
    PositionBounds box;
    box.valid = true; box.lo[2] = 20.1024f; box.hi[2] = 20.1024f;
    CHECK(850, WaterMeshHeight(box).valid && WaterMeshHeight(box).h == 20.1024f);
    box.hi[2] = 20.1040f;
    CHECK(850, !WaterMeshHeight(box).valid);
    box.hi[2] = 20.1024f; box.valid = false;
    CHECK(850, !WaterMeshHeight(box).valid);
  }
```

      and, in the manner of cases 820 to 829: **844** (`MakeClayWaterConstants`: a good record
      gives the 432 bytes with `alpha_ref` at 400 and `swizzle` at 428; a read component that is no
      number, a scale of 0 and a reference of 1.5 are refused with `out` untouched; unread
      components are carried bit for bit), **851** (`WaterCoordsMirror`, `WaterAlphaMirror` and
      `WaterPixelMirror` at every line of `water_pins.inc`: the pins' bound of `1e-4 * max(1,
      |value|)` a channel for the single-precision mirror, the fate and the alpha exact to 1e-6;
      and for each of five wrong mirrors compiled in under `#if`, the pins fail: the order of the
      spec's mutations "base", "centre", "near_far", "sun_sign", "fog"), **852**
      (`FormatWaterText`: `Water: off`; `Water: drawn 12 of 12, targets 4, opaque left out 12 |
      not drawn {} | water-record {}`; with frames by reason; `WaterFrameBlock`: the exposure 1,
      `mask_du` = `mask_dv` = 1 / 256, every other field the frame's bit for bit).
- [ ] Run (`rtk clang++ -std=c++23 -fsanitize=float-cast-overflow
      -fsanitize-trap=float-cast-overflow -Wl,/STACK:16777216 tests\native\test_clay_logic.cpp -o
      out\tests\runtime-unit-tests\clay_logic.exe`): expected: does not compile
      (`WaterOpaqueDead` undeclared). Implement; rerun: exit 0. **A mutation check, run alone**, on
      a scratch copy of `clay_logic.h`: `WaterLeavesOut` returning true for an opaque record with
      any verdict, and `PlanWaterPlace` returning usable always, each fail the test (the cases
      named in the report).
- [ ] Mark the provisional table (`sizeof(ClayWaterConstants)`, the slots, the CHECK numbers).
      Frame map "The water's rules". Commit "Native water: the verdict, the dead draw's rule, the
      mirrored camera, the targets, the place, the constants and their mirrors (pure)".

### Task 11: The reflection targets with the mirrored sky

**Files:** `src/native/fable2_native_shaders.h` (`kSkyMirrorVs`), `src/native/render/clay_pass.cpp`
/ `.h`, `src/native/render/texture_cache.h` / `.cpp` (the water's two decode kinds),
`src/native/render/scene_dump.h` (flag 16, version 2), `tests/native/test_native_shaders.cpp`
(case 55, parts (a) to (c)), `tests/native/test_scene_dump.cpp`,
`tools/xdk_sigmatch/foliage_trace.py` (`read_dump`: both versions), `tools/xdk_sigmatch/
water_replay.py` (`targets`), `tests/test_foliage_trace.py`, `tests/test_water_replay.py`, frame map
"The reflection targets".

**Consumes:** Task 10; `EnsureSkyObjects` / `ForgetSkyObjects` / `DestroySkyObjects`
(`clay_pass.cpp` lines 1748, 1733, 1739: the layout, the pipeline's creation and the failure line
are the pattern), `sky_layout_`, `sky_ps_`, `kSkyParamFrame`, `kSkyParamConstants`,
`kSkyParamTextures`, `draw_sky` (4347 to 4356), the sky's verdict (3646 to 3711: `sky_in`,
`sky_decision`, `sky_views`, `sky_constants`, `mask_view_proj`, `view_found`), `FinishTerrain`
(841: the buffer's size at 864, the plan at 989), `terrain_cb_`, `terrain_cb_mapped_`, `frame_`,
`DrawShadows` (4201: the targets are drawn right after it, before the pre-pass's targets are
bound at 4242); `kSkyVs` and `kClaySkyPs` (`fable2_native_shaders.h` lines 1163 and 1196: `main`
reads `f_mask_du`, `f_mask_dv` and `f_exposure` of the frame's block); the shader test's baseline
method (case 54, part (a), line 5276: hashes recorded at the commit before the task by a scratch
program, `out/tests/dt10/baseline_hashes.cpp`); `TextureCache::Resolve` and `Upload`
(`texture_cache.h` lines 43 and 86), `BuildPlan`, `r8_image_`; `BeginSceneDump`, `DumpLayerPlane`,
`TakeSceneDump` (3093, 3128, 3162), `SceneDumpBytes`, `ParseSceneDump`, `PackSceneDump`
(`scene_dump.h` 77, 88, 115); `TakeImpostorPictures` as the pattern of planes kept for a dump.

**Produces:**
- `shaders::kSkyMirrorVs`, defined after `kSkyVs`: the same triangle and position, and
  `SV_ClipDistance0` = the third component of the pixel's ray, `s_ray0.z + X * s_rayx.z + Y *
  s_rayy.z` with (X, Y) the vertex's own clip position (the ray is linear in them, so the clip is
  exact along the horizon): the half of the picture below the horizon is not drawn and stays 0.
  It declares cbuffer Sky at `b0` with `kClaySkyPs`'s fields. **`kSkyVs`, `kClaySkyPs` and every
  other program keep their text.**
- In the pass, made on the first frame whose scene verdict (`WaterSceneVerdict`) is `drawn`,
  **never with the switch off**: `EnsureWaterTargets(dev)` / `ForgetWaterTargets()` /
  `DestroyWaterTargets()` and `water_targets_failed_`: `water_target_[kMaxWaterTargets]`
  (256 x 256 `kR8G8B8A8_UNORM`, one sample, no levels, render target and shader resource, resting
  in the pixel-shader-resource state) with their views; `sky_mirror_vs_`; `sky_mirror_pipeline_`
  (the sky's layout, `kSkyMirrorVs` with `sky_ps_`, the target's format, no depth, no blend, one
  clip distance). A failure turns the water off for the run with one error line (the verdict
  `reflection`), not the pass.
- At the head of `Render`: `water_step_slots_ = kClayWaterStepSlots` iff the scene verdict is
  `drawn` (else 0: `FinishTerrain`'s size and plan are then today's); `water_slot0_` from the
  plan.
- After the sky's verdict (line 3711): for each target of `PlanWaterTargets`, `WaterViewRows` of
  the frame's view-projection with that body's `c31.xy` and height, `SkyRayBasis` of it,
  `MakeClaySkyConstants` with the frame's fog and sky blocks, the sky's textures as the main sky
  has them, into the target's slot; `WaterFrameBlock(frame_)` into the second frame block's slots.
  Any of it failing, or the frame's sky not drawn: `targets` false (the verdict `reflection`).
- After `DrawShadows`, before the pre-pass, **only with the final verdict `drawn`**: each target
  to the render-target state, cleared to 0, a 256 x 256 viewport, `sky_layout_` with the second
  frame block at `kSkyParamFrame` and the target's constants, `sky_mirror_pipeline_`, three
  vertices; back to the shader-resource state. (Until Task 12 the final verdict cannot be `drawn`:
  `objects` is false. This task's draw is reached in Task 12's build, and its own check is the
  shader test and the dump's test; **it has no run of its own**.)
- The water's textures (spec Design 6): `TextureCache::ResolveWater(cmd, dev, fetch, WaterDecode
  kind, &sampler, &status, tst)`: the cache's own residency, hashing and dynamic rule with the
  decode by kind (`DecodeK8Level` to `kR8_UNORM`, one level; `DecodeDxnLevelRg8` for every stored
  level to `kR8G8_UNORM`, the levels' layout by `BuildPlan` under the 4 x 4 blocks of 16 bytes
  that `DumpFormatInfo(49)` names); key: the texture's identity and the kind. `Resolve` and
  `TexFormatFromXenos` are not changed.
- `scene_dump.h`: `kSceneDumpTargets = 16u` (the frame's reflection targets), with
  `kSceneDumpPreWater = 8u` reserved for Task 12 and refused by the packer until then;
  `kSceneDumpVersion` stays 1 and `kSceneDumpVersion2 = 2`: **a file is version 2 iff it has a
  flag above 4**. With flag 16, after the planes of before: `u32 count` (1 to 4), then for each
  target `u32 base, f32 height` and 256 x 256 x 4 bytes (red, green, blue, alpha; rows from the
  top). `SceneDumpBytes(width, height, flags, targets = 0)`; `ParseSceneDump` takes both versions
  (a version-1 file with a flag above 4, a version-2 file without one, a count of 0 or above 4:
  refused); `SceneDumpPlanes` gains the targets' rows, bases and heights. The pass copies the
  targets for the frame of the dump as it copies the layer's planes.
- `foliage_trace.read_dump` returns the head, scene, depth, layers and, new, a dict with
  `"targets"` (a list of `(base, height, rgba)`) and `"pre_water"` (None until Task 12), for both
  versions; every caller of today keeps its result. `water_replay.py targets <capture>
  <scene.f2ns>`: criterion 10's 13b part: criterion 5's two measures on the stored targets against
  the capture's pictures of the same frame, by base; and that two targets of one frame are equal
  texel for texel in this phase.

- [ ] Tests first. `tests/native/test_native_shaders.cpp`, **case 55** (after case 54; parts (d)
      to (g) are Task 12's): **(a)** with this task's header every program the header had before
      (all of case 54's list and the impostors' two) compiles to the bytes it compiled to before
      the task: the hashes and sizes are recorded by this step from the commit before, by a
      scratch program in the manner of `out/tests/dt10/baseline_hashes.cpp`
      (`out/tests/w11/baseline_hashes.cpp`, its output kept as
      `out/tests/w11/baseline_hashes_<commit>.txt`), with case 54's canary; **(b)** `kSkyMirrorVs`
      compiles; its disassembly declares one clip-distance output and reads cbuffer Sky at `b0`
      (`s_ray0`, `s_rayx`, `s_rayy`: the three z components and no other field); its position
      output's code equals `kSkyVs`'s; **(c)** `kSkyMirrorVs` with `kClaySkyPs`: the pixel program
      is `kClaySkyPs`'s own bytes (it is the same string), and the pair's interface matches (the
      pixel program takes `SV_Position` alone).
      `tests/native/test_scene_dump.cpp` (after 101): **110**: `SceneDumpBytes(1120, 720, 1)` is
      today's number and `(1120, 720, 1 | 16, 4)` is that plus 4 + 4 * (8 + 262144); `(…, 16, 0)`
      and `(…, 16, 5)` are 0; flag 8 is 0 (refused until Task 12); **111**: a packed file with two
      targets parses as version 2 with its bases, heights and bytes in place, and a file without a
      new flag is version 1 and **byte for byte the file of before** (packed by the old argument
      list); **112**: a version-1 head with flag 16, a version-2 head without, a short file: not
      parsed. `tests/test_foliage_trace.py`: `read_dump` of a made version-1 file gives today's
      four results and empty extras; of a made version-2 file the two targets.
- [ ] Run: expected failures (`kSkyMirrorVs` undeclared; `kSceneDumpTargets` undeclared;
      `read_dump` returns four values). Implement; both suites green (the shader test through the
      scratch runner while the locked file exists); `rtk .\build.cmd -release fable_2`.
- [ ] Frame map "The reflection targets". Commit "Native water: the reflection targets with the
      mirrored sky, the water's two texture decodes in the cache, the stored scene's targets".

*No run in this task: nothing reaches its draw before Task 12, whose software-adapter run is the
first run of both.*

### Task 12: The surface step: programs and the pass; the software-adapter run; the EARLY LOOK

**Files:** `src/native/fable2_native_shaders.h` (`kClayWaterVs`, `kClayWaterPs`,
`kClayWaterFields`), `src/native/render/clay_pass.cpp` / `.h`, `src/native/render/clay_logic.h`
(what the pass needs beyond Task 10: nothing is expected), `src/native/render/scene_dump.h` (flag
8), `src/native/fable2_native_render.cpp` (`Render`'s argument, the F3 text, the log line),
`tests/native/test_native_shaders.cpp` (case 55, parts (d) to (g)), `tests/native/
test_scene_dump.cpp`, `tests/test_water_hlsl.py` (`MUTANTS`; the five tests now run),
`tests/test_water_literals.py` (the HLSL side), `tools/xdk_sigmatch/water_replay.py` (`cover`,
`colour`), README (the F3 line), frame map "The water step".

**Consumes:** Tasks 9 to 11; `EnsureImpostorObjects` (`clay_pass.cpp` line 2369: its layout,
shader and pipeline creation and its failure line are the pattern), `ForgetImpostorObjects` /
`DestroyImpostorObjects`; the prepare loop (3356 to 3447: `DecideDrawState` and `leave_out` at
3370 to 3379, `geometry_.Positions` at 3383 with its `PositionBounds`, `Indices` at 3386, `Uvs` at
3392); the pre-pass loop (4262 to 4275); the colour loop (4595 to 4679: `ColourStepsDue` at 4598,
`due.sky` at 4624, `bind_scene`, `bound`); `draw_impostors` (4571 to 4593) as the pattern of a
step's lambda; `sun_depth_copy` and the interruption of the colour pass around it (4609 to 4622)
as the pattern for the dump's plane; `DepthPackStep` (3069); `FormatImpostorText`'s place in the
F3 text (`fable2_native_render.cpp` line 864) and the log lines (927 to 933); `kClayImpostorVs`'s
neighbours in the shader header (`FABLE2_CLAY_IMPOSTOR_HLSL` at 1306, the `#undef` block at 1503:
a program is `"#define CLAY_FOG 1\n" FABLE2_CLAY_LIGHT_HLSL` and its own text; `fog_scatter`,
`fog_transmit` and the ground form's function in `FABLE2_CLAY_LIGHT_HLSL`, lines 217 to 386);
`wind_hlsl.WATER_VERTEX`, `WATER_PIXEL` (Task 7).

**Produces:**
- `shaders::kClayWaterVs`, `kClayWaterPs`, defined after the impostors' programs and before the
  `#undef` block, each `"#define CLAY_FOG 1\n" FABLE2_CLAY_LIGHT_HLSL` and its own text. **Nothing
  inside a `FABLE2_CLAY_*_HLSL` macro changes.** Their own text:

```hlsl
cbuffer Water : register(b0) {
  float4 w_rows[4];       // the draw record's rows
  float4 w_vs[4];         // c115, c116, c117, c118
  float4 w_ps[17];        // c0, c19, c31, c116, c117, c126..c131, c136, c137, c138, c140, c141, c142, c143 (PROVISIONAL with the count)
  float w_alpha_ref; float w_out_scale; uint w_sun_on; uint w_fog_on;
  uint w_sampler_mask; uint w_sampler_normal; uint w_sampler_picture; uint w_swizzle;
};
Texture2D<float4> water_mask : register(t0);      // R8: the shore mask (slot 1)
Texture2D<float4> water_normal : register(t1);    // R8G8 with its levels: the normal map (slot 2)
Texture2D<float4> water_picture : register(t2);   // the body's native target (slot 13); slot 14 is zero (spec ruling 21)
struct WaterVsOut {
  float4 pos : SV_Position;
  float3 place : TEXCOORD0;      // o0.xyz
  float4 coord : TEXCOORD1;      // o1
  float2 mask : TEXCOORD2;       // o3.xy
};
// #define W_C115 w_vs[0] ... W_C143 w_ps[16]: one name a register; THE TEXT NAMES EVERY REGISTER BY COMPONENT
// (W_C129.xy, never a dot or a product with a whole row that has an unread component).
// WATER VERTEX TEXT BEGIN
//   float4 WaterCoords(float3 p): (p.x * W_C117.x + W_C115.x, p.y * W_C117.y + W_C115.y, p.x * W_C118.x + W_C116.x, p.y * W_C118.y + W_C116.y)
// WATER VERTEX TEXT END
// vertex main: the position by w_rows as the clay vertex program forms a kDot record's (its arithmetic and its
//   depth convention, term for term); place = p; coord = WaterCoords(p); mask = the pair.
// WATER PIXEL TEXT BEGIN
//   float  WaterAlpha(float m)                      1 - saturate(W_C141.x - (1 - m) * W_C141.y)
//   float2 WaterNormalSum(float2 a, float2 b)       (2a - 1) + (2b - 1)
//   float3 WaterNormal(float2 s, float2 a, float2 b)    n = (s.x / 2, s.y / 2, (W_C128.x / 2) * z); n * rsqrt(dot(n, n))
//   float2 WaterLookup(float2 q, float2 s)          ((q.x * W_C0.z + W_C0.x - 0.5) * W_C31.x + 0.5 + s.x * W_C129.x * 0.5, the same in y)
//   float  WaterFresnel(float3 V, float3 N)         saturate(1 - dot(V, N) + W_C116P.x)
//   float3 WaterBase(float F)                       W_C127.xyz + (W_C126.xyz - W_C127.xyz) * F
//   float3 WaterMix(float3 base, float3 r, float F, float d)    near with zero for slot 14, far, lerp by saturate(W_C140.x * d); the factor saturate(W_C131.x)
//   float3 WaterSun(float3 N, float3 V)             the second normal by W_C136.x, the mirrored light, exp2(W_C138.x * log2(...)) * W_C137.x * W_C143.xyz
// WATER PIXEL TEXT END
// pixel main: m = the mask's channel by w_swizzle at i.mask (the static sampler w_sampler_mask);
//   alpha = WaterAlpha(m); discard where alpha == 1 (the shader's own kill) or !(alpha > w_alpha_ref) (the state's
//   test; at or below W_C141.w the game's colour is 0 and the test discards it: w_alpha_ref is W_C141.w's twin,
//   held by Task 4's `states`); a, b = the normal map's two channels by w_swizzle at i.coord.xy and i.coord.zw (the
//   level the host computes); q = floor(i.pos.xy); r = water_picture at WaterLookup(q, s) (wrap, as the fetch);
//   colour = WaterMix(...) and, with w_sun_on, + WaterSun(...); the fog by the frame's tables at i.place (the
//   ground form's function of FABLE2_CLAY_LIGHT_HLSL, unchanged, only with w_fog_on); out =
//   ((v > 0) ? min(v, 31.875) : 0) of colour * w_out_scale a channel, alpha `alpha`.
```

- In the pass: `EnsureWaterObjects(dev)` / `ForgetWaterObjects()` / `DestroyWaterObjects()` and
  `water_objects_failed_` (made on the first frame whose verdict needs them, **never with the
  switch off**; a failure turns the step off for the run with one error line, not the pass): the
  layout `water_layout_` (`b0` the record's constants and `b2` the frame's block, both stages;
  three textures for the pixel stage; the four static samplers), `water_vs_`, `water_ps_`, one
  pipeline a cull mode (`water_pipelines_[3]`: the colour pass's targets, **depth test "less" with
  the depth write** (the game's function 4 under its reversed range; ledger ruling 27), blend
  source alpha and inverse source alpha, the cull from the record's own state word).
- `ClayPass::Render` gains `bool water` after `impostors` (`clay_pass.h` line 136). At the head:
  the scene verdict (`WaterSceneVerdict`: the switch and `scene.water_on`, the records by kind,
  the frame's light, `WaterOpaqueDead` of every opaque record, the pair's counts, each record's
  `c20.w` against the frame's output scale) and the slots (Task 11).
- In the prepare loop, only when the scene verdict is `drawn`: a record whose `seq` is an opaque
  water record's `place` is prepared **as today** and marked `water_dead`; a surface record is
  left out as today (`blend-unknown`) and, apart, prepared for the step (`PrepareWater`): its
  positions (with their box: `WaterMeshHeight`), its indices, its pair buffer (`geometry_.Uvs`
  with a copy of the record's material whose `uv_vb` is `r.vb` and whose `uv` is the pair's
  layout: `UvFormat::kFloat2`, 12 bytes into a 20-byte vertex, the fetch slot of the position),
  its mask and the normal map (`ResolveWater`), its draw record's rows against the frame's
  view-projection (`view`). Any of them missing sets the input its reason names.
- The final verdict (`DecideClayWater`) after the sky's and the targets' (Task 11), the place
  (`PlanWaterPlace` over the prepared records' sequence numbers with the dead ones left out, the
  sky's and the tail's places as the colour loop has them) and `EnsureWaterObjects`. **Only with
  `drawn`**: the pre-pass loop and the colour loop skip a `water_dead` record (it is counted
  `water_left_out`, not `drawn`); the targets are drawn (Task 11); `colour_order.water_at` is the
  place; `draw_water` (a lambda beside `draw_impostors`): the step's layout, the frame's block, for
  each surface record its constants (`MakeClayWaterConstants`, written into its two slots before
  the pass), its three textures (the mask, the normal map, **the target of its own base**:
  `target_of`), its pipeline by cull, its index buffer; then `bind_scene()` and `bound = nullptr`.
  In the loop it is drawn last of an index's steps (`if (due.sky) draw_sky(); if (due.water)
  draw_water();`), and after the loop after the sky and the tail. In a drawn frame the
  `alpha-unknown` and `blend-unknown` counts fall by the water's records (12 and 12 at the
  bridge); with any other verdict the frame is today's, record for record, and the reason is
  counted.
- **The pre-pass and the shadow maps draw no water** (spec Design 8): the surface records are in
  neither loop; the dead records leave the pre-pass with the colour pass.
- `scene_dump.h`: flag 8 (`kSceneDumpPreWater`): one `width * height` plane of 24-bit levels after
  the layer's planes and before the targets: **the scene's depth taken just before the water
  step**, by `DepthPackStep` in an interruption of the colour pass of the kind the layer's depth
  uses (lines 4609 to 4622), only in the frame of a dump whose verdict is `drawn`. A file with it
  is version 2. `read_dump` returns it as `"pre_water"`.
- `water_replay.py cover <capture> <scene.f2ns>` (criterion 7: the water pixels, those whose last
  depth is nearer than the plane before the water, against the replay's kept pixels on the game's
  depth copy, each side's share farther than one pixel from the other; and the bed: the plane
  before the water within 1% of the game's depth copy on the water pixels) and `colour <on
  capture> <on.f2ns> <off capture> <off.f2ns>` (criterion 8: on the sky-sampling water pixels,
  native over game a channel, the hue, the off-run's distance on the same pixels; the far-shore
  pixels' share and ratio, the whole footprint's ratio and percentiles, reported).
- `ClayStats` filled; `FormatWaterText` as the F3 text's last line; the log line `[native] clay:
  Water: ... | the step's CPU time this frame N ms` every 300 frames only with the switch on, and
  one line when the line's form changes (at most 16).

- [ ] Tests first. `tests/native/test_native_shaders.cpp`, case 55: **(d)** `kClayWaterVs` and
      `kClayWaterPs` compile; reflection: cbuffer `Water` of 432 bytes at `b0` with
      `w_alpha_ref` at 400 and `w_swizzle` at 428 (`kClayWaterFields` against
      `ClayWaterConstants`; PROVISIONAL with the count), cbuffer Frame at `b2`, three textures at
      `t0` to `t2` of the pixel program alone, four samplers; **(e)** **the pixel program reads no
      unread component**: for every register of `w_ps` and component the table marks unread,
      `ReadsComponent(listing, "cb0[N]", c)` is false, and for three components the table marks
      read it is true (the method of case 54's unread-component part); the same for `w_vs` in the
      vertex program; **(f)** neither program declares a resource the water layout does not bind;
      the pixel program's output is one target with alpha, it has a `discard`, and no
      `oDepth`; **(g)** part (a) again with this task's header (every program of before, and
      `kSkyMirrorVs`, at its bytes). `tests/native/test_scene_dump.cpp`: **113**: flag 8 adds one
      plane of `width * height * 4` bytes, makes the file version 2, and stands before the
      targets when both flags are set. `tests/test_water_hlsl.py`: `MUTANTS` filled, **at least
      twelve**, each site exactly once in the header and each breaking the comparison it names:
      `c116` for `c115`; `c118` for `c117`; the mask's `1 - m` as `m`; the normal's `c128.x` left
      out; the lookup scaled about 0; `c130` for `c129`; the lookup's axes exchanged; the fresnel
      bias's sign; `c126` and `c127` exchanged; near and far exchanged; `c136.x` left out; the
      light's sign; `c137.x` left out; the power's `log2` and `exp2` exchanged.
      `tests/test_water_literals.py`: the HLSL cbuffer's fields and the `#define` names against
      `ClayWaterConstants` and the table's order.
- [ ] `rtk python -m unittest tests.test_water_hlsl -v`: **the five tests of `TheText` RUN** (none
      skipped) and fail for the missing text; implement the programs; they pass, with every
      mutant failing. Implement the pass, the dump's plane, the F3 line. Suites green; build.
- [ ] NEEDS_RUN, step 1 (**the software adapter under the debug layer; nothing else before it**):
      1. `warp_on`: WARP with the suffixes `POST, SUN, WIND, COV, WATER`
      2. `warp_dump`: WARP-DUMP with the suffixes `POST, SUN, WATER` (and DUMP: the stored scene's
         two new planes under the debug layer)

      Copies as `t12_warp_*`. **Criterion 16 (13b)**: 0 `d3d ERROR` lines in both, the warnings
      listed by id against sub-project 12's (FM "Validation (12)"); **the log shows the step
      drew** (`Water: drawn 12 of 12, targets 4, opaque left out 12`); the shot at 225 s viewed:
      the lake is there, and what it looks like is said. If `warp_dump`'s capture did not finish
      (no `.f2ns`): the dump's planes are unexercised under the debug layer, said so, and step 2's
      on-run is made first once with `"--d3d12_debug=true","--fable2_native_d3d_messages=true"`
      appended (a real-GPU debug-layer run; its log read for `d3d ERROR` before any other run).
      **Any error: stop; no real-GPU run.**
- [ ] NEEDS_RUN, step 2, **the EARLY LOOK** (only after a clean step 1; one session, **full
      read-back runs**; the shots are judged, so EXPO is on: it is in PAIR):
      3. `look_off`: BRIDGE with `-GameArgs BASE,PAIR`
      4. `look_on`: BRIDGE with `-GameArgs BASE,PAIR,WATER`

      Copies as `t12_look_*`.
- [ ] **The EARLY LOOK, before any validation task starts.** View the four native shots (44 s and
      86 s of each run) and the two emulated ones (36 s) with the Read tool, the lake's part
      enlarged (`texture_thumb.py` or a crop script in WE), and describe each honestly. **The
      questions, each answered in the report with the shot's name:**
      1. Is the lake water, or a sheet of sky colour? (hue and brightness against the emulated
         shot; the far shore's dark band, which this phase cannot have: how much of the lake it
         covers in the emulated shot and how its absence reads)
      2. Is there a pattern of ripples, and is its scale the game's? Or is the surface flat, or
         noisy?
      3. Are the meshes' seams, a rectangle's outline or a hard line of the reflected horizon
         visible on the water?
      4. Black or bright patches (a lookup that wrapped into the black half; the glint out of
         range)?
      5. The shoreline: does the water end where the emulated shot's does, with a fade, without a
         fringe or a gap to the bed?
      6. Does anything stand wrongly in front of or behind the water (the bridge, the hero, the
         island, the far trees, the clouds, the beams)?
      7. Is anything outside the lake changed against `look_off`?

      Then the first reading, labelled so: `water_check.py records` on `look_on`;
      `water_replay.py cover` and `colour` on the pair; `water_replay.py targets` on `look_on`
      (two targets equal; the sky's ratio); the Water line's verdicts over the run (how many
      frames read anything but `drawn`, and which reason: the provisional table's line on
      `order`).
      **The stop rule:** if the answer to any of questions 1 to 6 is "wrong", and the figures of
      criteria 7, 8 and 10 as written would still read "met" or would not see it (a flat sheet
      with the right mean colour; a pattern at the wrong scale; a seam; a hard horizon line; a
      fringe narrower than a pixel's tolerance): **stop. Do not start Task 13.** Report to the
      controller with the shots, the answers and what the criteria miss; the controller rules
      whether the phase goes on, a criterion is added, or the sub-project waits for phase 13c's
      terrain.
- [ ] Frame map "The water step" (with the EARLY LOOK's answers); README. Commit "Native water:
      the surface step with its own layout and programs at the first surface record's place; the
      dead draw left out in a drawn frame; the depth before the water in the stored scene".

### Task 13: Validation of phase 13b and its documents

**Files:** frame map section 20 ("Validation (13b)", "Limits (13b)"), the spec (status and
"Implementation notes"), `out/native-evidence/water/` (scripts and outputs, untracked).

- [ ] Runs by the controller (all `RUN UNCHECKED`; each pair one session and one binary, the
      binary's SHA-256's first 16 digits recorded):
      1. `v_bridge_off`, `v_bridge_on`: BRIDGE with `-GameArgs BASE,PAIR` and with WATER added
         (**full read-back**; Task 12's pair serves if it is of this binary).
      2. `v_third_off`, `v_third_on`: THIRD with `-GameArgs BASE,PAIR` (+ WATER) (**full
         read-back**).
      3. `v_bridge_sun_off`, `v_bridge_sun_on`: BRIDGE with `-GameArgs BASE,PAIR,SUN` (+ WATER)
         (**full read-back**).
      4. `v_off`: OFF (its `-GameArgs BASE`); `v_off_before`: OFF with `-ExeDir
         out\build\win-amd64-release-pre13b` (**the controller's alone**: the copy of Task 9's step
         0); `v_on_defaults`: OFF's plan with `-GameArgs BASE,WATER` (the control). One session,
         one window width.
      5. **Timing: each run alone, the window on the usual monitor, shots 1942 wide, nothing
         beside it (no agent, no analysis, no build, no test, no other game), one session**, in
         this order: `time_off_1`, `time_on_1`, `time_on_2`, `time_off_2`: TIME with `-GameArgs
         BASE,POST,EXPO` (+ WATER). No read-back switch: these are not read-back runs.
- [ ] Before anything is read: the freeze signature of every run; `check_discovery.py
      --rows-only` on every capture; in group 5, each run's five shots are 1942 wide (else the
      session is "no timing evidence" and is made again) and each log has `seeded
      guest_vblank_pacing=false`.
- [ ] **Criterion 6**: `water_check.py records` on runs 1 and 2's on-captures, every frame: bit
      for bit; `seen` = `taken` = the `BEAD` rows (24 and 22); no reason; `sizeof(DrawRecord)` 616
      (the static assert, quoted).
- [ ] **Criterion 7**: `water_replay.py cover` on runs 1 and 2: (a) each side's share farther
      than one pixel (bound 2% each, *no precedent*); (b) the bed (bound 80%, *no precedent*);
      the off-run's figure by R's measure reported as the "before".
- [ ] **Criterion 8**: `water_replay.py colour` on runs 1 and 2: red and blue within 0.92 to 1.08
      and nearer to 1 than half the off-run's distance on the same pixels; green within the band;
      the hue within 6 degrees; at least 5,000 sky-sampling pixels or "not decidable". **The
      far-shore pixels, the whole footprint's ratio and its percentiles are reported, not
      bound.**
- [ ] **Criterion 10, 13b part**: `water_replay.py targets` on runs 1 and 2's on-pairs.
- [ ] **Criterion 13 (without the cubes: ledger ruling 26)** from run 3: on the layer's texels
      over the water pixels, the stored native layer depth (the plane before the tree occluders)
      as a height: between 19.95 and 20.15 on at least 95% with the water on (*no precedent*);
      the off-run's figure reported (no off-side bound); the texels over pixels that are no water
      pixel and whose bed is below 20.0024 in the game's depth copy: reported for both runs, not
      bound (the game's cubes close them; the native layer has none). Script
      `out/native-evidence/water/t13_layer.py`.
- [ ] **Criterion 14** from runs 1 and 2, off against on, **on the pixels common to both runs'
      sets** (no water pixel in the on-run, no placeholder pixel in the off-run): the land within
      3 pixels of the water, "island in the lake", "far peak"
      (`docs/native-renderer/fog-regions.json`), the whole picture outside the lake: native over
      game moves by at most 0.005; the pools at 40.0, 40.474 and 27.52 within 0.97 to 1.03.
- [ ] **Criterion 15**: the shader test's case 55 parts (a) and (g), CHECK 845, the off scene's
      `WaterLine` (quoted from the suites' output); `v_off`'s log (no line of the water beside
      `[native] water: off`; the frame rows of an off capture of group 1 say `"water":"off"`; the
      `alpha-unknown` and `blend-unknown` counts as before: 12 and 27 at the bridge);
      `region_compare.py` on `v_off`'s native shot at 40 s against `v_off_before`'s in the
      bridge's regions, each within 0.02 of 1, the two regions of moving leaf shadow named
      beforehand left out; **the control**: `v_on_defaults` against `v_off_before` must miss the
      bound in a lake region, else the measure is said not to tell.
- [ ] **Criterion 16**: Task 12's software-adapter runs, restated with their files.
- [ ] **Criterion 17**, valid only at shot width 1942 on the usual monitor with nothing beside the
      runs: no steady 150-frame native window below 30.0 fps in any of the four; the mean frame
      over steady native windows, averaged over a configuration's two runs, on over off at most
      0.5 ms (*no precedent*), only if the two runs of each configuration agree within 1.0 ms,
      else "not decidable"; beside it the two measures that do not depend on the swap: the
      capture median with records on (a difference under a third of a millisecond is "not
      decidable") and the step's own CPU time from the Water line. Script
      `out/native-evidence/water/t13_timing.py` (made from
      `out/native-evidence/distant-trees/cost_timing.py`, whose line patterns it keeps).
- [ ] Each of criteria 6, 7, 8, 10 (13b), 13, 14, 15, 16, 17 with its evidence file and an exact
      verdict word ("met", "not met as written", "not decidable", "by a substituted measure");
      the default of `--fable2_native_water` stays `off`, with the recommendation written beside
      the evidence.
- [ ] Spec status and "Implementation notes"; frame map "Validation (13b)" and "Limits (13b)".
      Commit "Water validation (13b): the surface with the sky's reflection, the verdicts". Then
      the phase's review. **Then the controller rules the entry into phase 13c (ledger ruling
      28).**

### Task 14: The water's edge and the sunward view: looked for (criteria 11 and 12)

**No stick plan to the water's edge was found by the plan's author, and none could be without a
run.** What is in hand: the hero stands on a bridge about 28 units above the lake (eye (82.07,
157.34, 50.17) at the bridge; the water at 20.1024); the nearest mesh corner is 22 units away on
the map ((96, 174.5): the mesh x 96 to 216, y 174.5 to 273), the mesh the spec names (x 61 to 96,
y 204 to 228) 47 units; the bridge's camera looks along (0.851, 0.495) on the map; the "trees"
walk (`33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/2500`) ends at eye (84.46, 171.93, 49.76),
still at the bridge's height, in front of a fence (FM line 1636), with the camera along (-0.830,
0.515). No capture holds the ground between there and a shore, a way down, or the walking speed
(the trees walk covers about 15 units of map in 5.5 s of stick). **How one is looked for**: the
shore's place from the masks offline, then three guessed walks with a capture at each end, then
at most three corrected walks.

**Files:** `out/native-evidence/water/t14_shore.py` (evidence script), frame map section 20 "The
water's edge" and "The sunward view", the spec's criteria 11 and 12 (their verdicts), user checks
(Task 19 appends).

- [ ] **The shore, offline** (no run): `t14_shore.py` on CAP `155547`: for each of the twelve
      meshes, the texels of its mask whose alpha by `water_alpha` lies between 0.0588 and 0.99,
      mapped through the mesh's vertices and pairs to the map: a top view of the shoreline
      (`t14_shore.png`) with the eye's place at the bridge, at the trees and on the sun side, and
      the terrain's height where a capture's stored scene shows ground between them (the stored
      native scene's depth unprojected by the frame's inverse view-projection). Viewed and
      described: the nearest shore point to each known eye, its direction as a stick deflection
      against that view's camera, and what lies between.
- [ ] **Three guessed walks** (NEEDS_RUN; Task 1's build or any later one with the water's switch
      off; **full read-back runs**; best made in Task 8's session):
      1. `e1`: EDGE-1 with `-GameArgs BASE,PAIR` (from the bridge 21 degrees left of the camera
         for 20 s: the direction of the nearest mesh corner)
      2. `e2`: EDGE-2 with `-GameArgs BASE,PAIR` (the trees walk, then 12 s forward and right:
         the direction of the mesh at x 61 to 96 from the trees' place)
      3. `e3`: EDGE-3 with `-GameArgs BASE,PAIR` (the trees walk with its last leg held for 16 s:
         straight on along the fence's line)

      **If the offline step shows a better heading, its lines replace these before the runs**,
      under the run shape (the tokens of `tools/drive_game.ps1`; one stick entry at most 60000 ms;
      the camera standing 5 s before the capture; `-Total` at most 100), and the report gives the
      replaced lines in full. Copies as `t14_e<n>_*`.
- [ ] From each run: the three shots viewed and described (where the hero stands; whether water is
      in view); the eye from the capture's frame row; `water_replay.py edge <capture> 1`: the
      footprint pixels within 20 units of the eye and those with an alpha between 0.0588 and 0.99
      (**W3 wants at least 1,000 of each**); whether any resolve of the frame goes to a slot-14
      base (`check_discovery.py`'s resolve rows); what slot 14's dump holds.
- [ ] **At most one more round of three walks**, each a correction of the nearest miss by the
      eye's place and the shore's map, each given to the controller in full as NEEDS_RUN. After
      six walks without W3: **criterion 11 reads "not decidable"**, the walks and their end places
      are written into the frame map, and user check "the shoreline on a walk along the water"
      carries it.
- [ ] **If W3 exists** (after Task 12): `v_edge_off`, `v_edge_on`: the found line with `-GameArgs
      BASE,PAIR` (+ WATER), one session (**full read-back**). **Criterion 11**: criterion 7(a) on
      that view; on the water pixels within 20 units and on those with an alpha between 0.0588
      and 0.99, native over game a channel, **reported** with the bed's share named; bound (0.90
      to 1.10, *no precedent*) only if the game's slot 14 is zero in W3 and at least 1,000 such
      pixels exist. The shots viewed: the fade at the shore, the near water.
- [ ] **The sunward view (W4, criterion 12).** From Task 6: if `glint` found at least 500 pixels
      in CAP `160104` or `160251`, the view is the held look and its pair is: `v_sun_off`,
      `v_sun_on`: SUNHELD with `-GameArgs BASE,PAIR` (+ WATER), one session (**full read-back;
      the look exists only while the stick is held: the hold runs from 41 s to the run's end and
      the four shots and the capture lie inside it**). Then: native over game on the glint pixels
      within 0.85 to 1.15 in luminance, and with `--no-glint` in the replay's reference the same
      measure must miss (*no precedent*). If Task 6 found no such pixels in either capture: one
      run `w4`: SUNHELD with `-GameArgs BASE,PAIR`, `glint` on its capture; fewer than 500 pixels
      again: **criterion 12 reads "not decidable"**, user check "the glint's sparkle" carries it.
- [ ] Frame map "The water's edge", "The sunward view"; the spec's criteria 11 and 12. Commit
      "Water: the edge and the sunward view, looked for; criteria 11 and 12".

---
## Phase 13c: the far terrain in the reflection targets

**Entry conditions, all of them, written in the ledger before Task 16's first step (spec Design 9;
ledger ruling 28):**
1. Phase 13b's criteria 6 to 8 are judged (Task 13) and the EARLY LOOK did not stop the phase.
2. **The controller has ruled the entry** after reading Task 13's verdicts (ledger ruling 28).
3. Open question 6 is closed by Task 15: what `6954` / `A4A9` draws, and its share of the game's
   pictures' texels that are neither sky nor black. **If that share is above a tenth and the pass
   has no pipeline that can draw it, the phase is not entered** and goes to the roadmap.
4. The rule "the k-th view of a frame is the picture at the k-th smallest slot-13 base" holds in
   every frame of every capture, by the resolves' destinations and by the heights (Task 15).
5. The views' `7DDA` rows pass the far-terrain family's check with their own registers (`c20.w` =
   1, the mirrored eye in `c19`, `c11.x` = 0.4704 where the main scene's is 1) (Task 15).

If any fails, none of Tasks 16 to 18 is started: the sub-project goes from Task 15's report to
Task 19 and closes at phase 13b, and the closing says so for criteria 9 and 10's second part.
Task 15 needs no game and may be made while phase 13b's runs are waited for.

### Task 15: The entry conditions: `6954` / `A4A9`, the views' rule, the views' light, the blur (no game)

**Files:** `tools/xdk_sigmatch/water_check.py` (`view-rule`, `view-terrain`),
`tools/xdk_sigmatch/water_replay.py` (`view`), `tools/xdk_sigmatch/light_formulas.py`
(`water_blur7`), `tools/xdk_sigmatch/post_check.py` (the blur's check, beside the bloom's),
`tests/test_water_formulas.py`, `tests/test_water_replay.py`, `tests/test_post_formulas.py`,
`lighting-formulas.md` ("Water": the views), frame map section 20 "The reflection views".

**Consumes:** `out/shader_dump/shader_695413A9831D88DA.ucode.vert` (362 lines),
`shader_A4A965C189287B99.ucode.frag` (**three lines: `max oC0, r0, r0`, the first interpolator
written as the colour** [microcode, read by the plan's author]), `shader_F186D27249B462F8.ucode.frag`
(30 lines) with `shader_B2D99AC2C7FB40D6.ucode.vert`; `light_check.py`'s far-terrain family
(`FAMILIES["terrain-far"]`, line 65: `7DDA` with `5003`; its driver at line 833; `lf.terrain_far`);
`terrain_replay.py` (the patches' geometry from a `"tess"` row); `post_check.py`'s check of the
bloom's blur (`lf.post_blur13`) as the model; `WE/b_views.py`, `WE/b_resolves_*.txt`,
`WE/s_viewsky.py`; FM line 1654 (the pair `B6C9` / `A4A9` as point lists with colour mask 0 in the
main scene's bins: the same pixel shader elsewhere); the captures in hand and, when they exist,
W1 and W2.

- [ ] **What `6954` / `A4A9` draws.** Read `6954`'s vertex microcode (which streams and textures
      it fetches, what it writes to `oPos` and to interpolator 0) and one view's rows of CAP
      `155547` (their primitive, counts, state words, textures, the registers that differ from the
      `5003` / `7DDA` row behind each); run `6954` through `ucode_eval` on one row's inputs. Say
      what it is in one paragraph with the instructions it stands on (a base pass of the same
      patches that `7DDA` then draws over; a depth or colour seed; something else). Tests first
      for whatever formula is written down; none if it is only described.
- [ ] **Its share.** `water_replay.py view <capture> <frame> <k>`: view k replayed into a
      256 x 256 picture: the `A4A9` rows and the `7DDA` rows through the interpreter (the
      geometry by `terrain_replay.py`), depth-tested in their order, the sky by
      `light_formulas.sky`; for each texel of the game's picture that is neither sky nor black,
      which draw the replay has on top. Printed for views 1 and 3 of CAP `155547` and views 1 and
      3 of `155731`: the share whose top draw is an `A4A9` row, a `7DDA` row, an object or flare
      (not replayed: "nothing"), and the replay's `7DDA` texels against the game's (each channel's
      ratio). **Entry condition 3 is read from this figure**; test first: a made view of one
      `A4A9` quad wholly behind one `7DDA` quad gives a share of 0, and beside it a share of 1.
- [ ] **The views' rule.** `water_check.py view-rule <capture>...`: for every frame, the views in
      their order, each view's resolve destination (`"resolve"` rows) and height (Task 4's
      `views`), against the k-th smallest slot-13 base of the frame's `A17D` rows and the height
      of the meshes that sample it: "holds in N of N frames" over the six captures (and W1, W2).
      Test first on a made capture of two views (one in order, one exchanged: the second fails).
- [ ] **The views' light.** `water_check.py view-terrain <capture>`: every `7DDA` row of every
      view through the far-terrain family's check with the row's own registers; the registers
      that differ from the main scene's `7DDA` rows printed (`c20.w`, `c19`, `c11.x`, and any
      other). Expected: every row agrees.
- [ ] **The blur.** `lf.water_blur7(tex, x, y, weights, vertical)` transcribed from `F186`'s 30
      lines; `post_check.py` gains its check (made inputs, the bound of the bloom's blur, and
      three mutations: a tap's offset, a weight, the axis); `tests/test_post_formulas.py` holds a
      reduced count. Which views are blurred, in every frame of every capture (Task 8's census):
      whether a `BeginVertices` call with pixel shader `F186` right after a view decides it. **If
      it does not, the controller rules blur for all or for none (spec Design 9).**
- [ ] The report to the controller: each of entry conditions 3, 4 and 5 "holds" or "does not
      hold" with its file; the recommendation; what a not-entered phase leaves (13.5 to 30% of the
      bridge's lake lighter than the game's where the far shore should stand: the figure as
      Task 6's `lookup` has it for each body's own pixels). Frame map "The reflection views".
      Commit "Water phase 13c: the entry conditions (what A4A9 draws, the views' rule and light,
      the blur's formula)".

### Task 16: The capture of the reflection views' far terrain

**Files:** `src/native/capture/reflection_view.h` (new, pure), `src/native/capture/capture.cpp`,
`src/native/render/frame_scene.h`, `tests/native/test_reflection_view.cpp` (new; two lines in the
runner), `tests/native/test_frame_scene.cpp`, `tools/xdk_sigmatch/water_check.py`
(`reflection`), README, frame map "Reflection draws".

**Consumes:** `ShadowPassTracker` (`shadow_block.h`: the pattern of a tracker fed by every hooked
draw outside the scene), `ObserveShadowDraw` and `RecordShadowDraw` (`capture.cpp` lines 4108 and
4003: draw records of their own with their side blocks, made outside the main scene),
`FillTerrainInputs`, `FillLightInputs`, `AssembleRecord`; `FrameBuilder::AddShadow` and the
shadow records' side arrays (`frame_scene.h`, lines 738 to 800); `g_water_frame`; the far
terrain's entries (`ps_albedo_table.inc` lines 44, 86, 87: `7DDA` with `5003`); `OnXdkCall`'s
order of observers.

**Produces:**

```cpp
// reflection_view.h, namespace fable2::native::capture
inline constexpr uint32_t kMaxReflectionDraws = 256, kMaxReflectionViews = 8;      // spec ruling 19: 55 and 52 a frame, 4 views
inline constexpr uint64_t kReflectionTerrainPs = 0x7DDAD5C29B3F56EEull, kReflectionSkyPs = 0xF7601FBE485936FDull,
                          kReflectionBlurPs = 0xF186D27249B462F8ull;
struct ReflectionView {
  uint32_t first = 0, count = 0;     // its records in FrameScene::reflection_draws
  uint32_t blurred = 0;              // a BeginVertices call with pixel shader F186 followed it
};
enum class ReflectionDraw : uint8_t { kNone, kTerrain, kOther, kSky, kBlur };
// Follows the hooked draws before the main scene. A view is a run of draws on one surface that is neither the
// scene's nor a shadow map's and ends with the sky's draw (F760 through the indexed builder) and its flares.
// Observe: what this draw is to the view being followed (kTerrain: record it; kSky: the view is whole once the
// run ends; kBlur: the view just closed was blurred; kNone: not of a view, and an open run without a sky is
// dropped: its terrain records are taken out again). Begin a frame with Reset.
class ReflectionViewTracker {
 public:
  void Reset();
  ReflectionDraw Observe(uint32_t surface_info, uint32_t scene_surface_info, bool shadow_surface, uint64_t ps_hash,
                         bool indexed_builder, bool begin_vertices, bool in_scene);
  uint32_t views() const;                                    // whole views so far
  bool dropped() const;                                      // a run without a sky, a 9th view: the frame's set is refused
};
// frame_scene.h
//   FrameScene: std::vector<capture::DrawRecord> reflection_draws;  (with their terrain and light side blocks:
//               reflection_terrain, reflection_lights, indexed as a scene record's are)
//               std::vector<capture::ReflectionView> reflection_views;
//               uint32_t reflection_refused = 0;   // 0, or why the frame has none: 1 over-limit, 2 a view not whole, 3 a record refused
//   FrameBuilder::AddReflection(record, terrain, light, view) / CloseReflectionView(blurred) / Finish: the set is
//     whole (every view of the frame, every 7DDA draw of each) or empty, with the reason.
```

In `capture.cpp`, only in a frame with the water wanted: `ObserveReflectionDraw(id, a, device)`
beside `ObserveShadowDraw`, before the main scene's bracket; a `7DDA` draw of an open view is
assembled as a draw record with its terrain and light inputs (its own registers: `c20.w` = 1, the
mirrored eye) and handed to the builder under the scene's mutex after the frame's recheck.
Objects, flares and `A4A9` make no record. The frame row's `"water"` gains `"views":[{"first":F,
"count":C,"blurred":B}]` and `"reflection":N`; `water_check.py reflection <capture>` holds each
record to its row (rows, registers, state) and each view's count to the capture's `7DDA` rows of
that view.

- [ ] Tests first. `tests/native/test_reflection_view.cpp`: the bridge's order of D1 (R B1's
      table) fed draw by draw (view 1: 36 `A4A9`, 36 `7DDA`, 16 objects, 2 flares, the sky, then
      the blur; view 2: 4, 4, 2 flares, the sky, the blur; view 3: 12, 12, 2, the sky; view 4: 3,
      3, 2, the sky; then the scene's first draw) gives four views with 36, 4, 12 and 3 terrain
      draws, `blurred` 1, 1, 0, 0 (returns 1 to 6); a run on the scene's surface, a run on a
      shadow map's surface and a run of pitch-320 picture draws give none (7 to 9); a run that
      ends without a sky is dropped and `dropped()` is true (10); a ninth view is `dropped` (11);
      `Reset` forgets everything (12); a blur call with no view before it is nothing (13).
      `tests/native/test_frame_scene.cpp` (after Task 9's numbers): **230**: 55 records in four
      views are the scene's, in order, each view's `first` and `count`; **231**: the 257th record:
      the set is empty, reason 1; **232**: a view that was not closed: empty, reason 2; **233**:
      with the water off nothing is held; **234**: nothing of one frame is in the next.
- [ ] Run: expected failures; implement; suites green; build. **Off stays off**: `g_water_frame`
      guards the observer (the test of 233 and a reading of the code path in the report).
- [ ] NEEDS_RUN (no binding, target or shader text changed: no software-adapter run is due), one
      session, **full read-back runs**: `r16_bridge_on`: BRIDGE with `-GameArgs BASE,PAIR,WATER`;
      `r16_third_on`: THIRD with `-GameArgs BASE,PAIR,WATER`. From them: `check_discovery.py
      --rows-only`; `water_check.py records` and `reflection` on both (expected: 55 and 52
      records, 4 views, the blur flags of Task 15's census); the capture median against Task 13's
      on-runs (expected: about a twentieth more: spec Design 9), reported.
- [ ] README; frame map "Reflection draws". Commit "Water phase 13c: the reflection views'
      far-terrain draws captured as records of their own, a view whole or absent".

### Task 17: The far terrain in the targets; the blur; the software-adapter run; the look

**Files:** `src/native/fable2_native_shaders.h` (the far terrain's mirrored vertex variant; the
blur's program), `src/native/render/clay_pass.cpp` / `.h`, `src/native/render/clay_logic.h`
(`PlanReflectionViews`, `ReflectionViewUsable`, the blur's constants and mirror, the step's
slots), `tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp` (case 55,
parts (h) to (j)), `tests/test_water_hlsl.py` (the blur's text), frame map "The targets' terrain".

**Consumes:** Tasks 11, 15, 16; the far-terrain pipeline and `PrepareTerrain` / `FinishTerrain`
(`clay_pass.cpp` lines 743, 841), `FABLE2_CLAY_TERRAIN_FAR_HLSL` (`fable2_native_shaders.h` line
1082), `PrepareLight` (653); `WaterViewRows`, `WaterFrameBlock`, `kClayWaterStepSlots`; the bloom's
blur step as the pattern of a post step through a scratch target (`EnsureBloomObjects`, line
1986); `lf.water_blur7` and its pins.

**Produces:**
- `clay_logic.h`: `PlanReflectionViews(views, view_count, plan)` (view k belongs to target k: the
  k-th smallest base; more views than targets, or fewer: every target is the sky's alone);
  `ReflectionViewUsable(records, count, want_rows)` (every record's rows equal
  `WaterViewRows(M, c31.xy, h)` of the target within 1e-5 relative; else that target holds the sky
  alone, counted `reflection: terrain`); a second frame block **a target** (the frame's with the
  exposure 1, 1 / 256, the mirrored view's inverse and the mirrored eye, for the terrain's fog):
  `kClayWaterStepSlots` becomes `2 * 64 + 4 + 4 * kClayFrameSlots` plus the terrain records' slots
  (one a record: 256) and the provisional table's line is amended; `WaterBlurConstants` and
  `WaterBlurMirror`.
- The mirrored far-terrain vertex program: the far terrain's own text with `z - h` of the world
  position as `SV_ClipDistance0` (`h` from the target's constants). **Every program of before at
  its bytes** (case 55, part (a) again): if a guarded block inside the terrain's macro changes
  any program's bytes, the variant is a text of its own.
- In the pass: the 256 x 256 `kD32_FLOAT` depth target (`water_depth_`, made with the targets);
  after a target's sky triangle, **only for a target whose view is usable**: the depth cleared to
  the far value, the view's records by the native far-terrain pipeline with their own light
  inputs, under that target's frame block, with the cull the record's own state word asks for,
  the mirrored vertex variant; then, for a view the game blurs, the blur's one step through a
  scratch target (`water_blur_`). A view that fails, an over-limit, a refused terrain record: that
  target is phase 13b's picture, counted `reflection: terrain` on the Water line (`targets 4
  (terrain 3)`).
- The stored scene's targets (flag 16) are the targets as the surface sampled them.

- [ ] Tests first: `test_clay_logic.cpp` (after Task 10's numbers): **853**: `PlanReflectionViews`
      (four views to four targets in order; three views for four targets, five for four: none);
      **854**: `ReflectionViewUsable` at a pinned view's rows, with one record's row moved by 1e-3
      relative (not usable), with a record of another height (not usable); **855**: the slot plan
      with the amended count, and with no water slots today's (CHECK 845 again with the new
      number); **856**: `WaterBlurMirror` at the pins. `test_native_shaders.cpp`, case 55:
      **(h)** part (a) again; **(i)** the mirrored terrain program compiles, has one clip
      distance, and its code but for the clip equals the far terrain's vertex program's; **(j)**
      the blur's program compiles, reads one texture, has no loop the listing cannot bound.
      `tests/test_water_hlsl.py`: the blur's text against `lf.water_blur7` with three mutants.
- [ ] Implement; suites green; build.
- [ ] NEEDS_RUN, step 1 (**the software adapter under the debug layer; nothing else before
      it**): `warp_13c`: WARP with the suffixes `POST, SUN, WIND, COV, WATER`; `warp_13c_dump`:
      WARP-DUMP with `POST, SUN, WATER`. **Criterion 16 (13c)**: 0 `d3d ERROR` lines; the log
      shows the step drew (`Water: drawn 12 of 12, targets 4 (terrain 4)`); the shot viewed.
      **Any error: stop.** (The dump's fallback as in Task 12.)
- [ ] NEEDS_RUN, step 2 (only after a clean step 1; one session, **full read-back runs**):
      `look13c_off`, `look13c_on`: BRIDGE with `-GameArgs BASE,PAIR` (+ WATER).
- [ ] **The look, before Task 18 starts**: the native shots viewed against Task 12's and the
      emulated ones: is the far shore's dark band on the lake where the emulated shot has it, of
      its size and darkness; is anything in the reflection that the game's does not have (a hill
      upside down in the wrong place, a band along the picture's edge, terrain below the water
      line); the stored targets beside the game's pictures of the same frame (a sheet, viewed).
      First reading of criteria 9 and 10. If the reflection looks wrong in a way those two would
      not catch: stop and report to the controller.
- [ ] Frame map "The targets' terrain". Commit "Water phase 13c: the game's own far terrain in the
      reflection targets under the derived camera, the blur".

### Task 18: Validation of phase 13c and its documents

**Files:** frame map section 20 ("Validation (13c)", "Limits (13)"), the spec,
`tools/xdk_sigmatch/water_replay.py` (`colour --all`, `targets --silhouette`),
`tests/test_water_replay.py`.

- [ ] Tests first, then the two modes: `colour --all` (criterion 9's measure on every water
      pixel: a made pair whose native lake is the game's times 1.05 reads "met", times 1.2 "not
      met", and a pair whose luminance is right in the mean and flat where the game's spreads
      misses the percentile bound) and `targets --silhouette` (criterion 10's second part: a made
      target whose silhouette is the game's moved by one texel passes, by three fails; a target
      that is the sky alone fails the 85%).

- [ ] Runs by the controller (all `RUN UNCHECKED`):
      1. `v13c_bridge_off`, `v13c_bridge_on`: BRIDGE with `-GameArgs BASE,PAIR` (+ WATER)
         (**full read-back**; Task 17's pair serves if it is of this binary).
      2. `v13c_third_off`, `v13c_third_on`: THIRD with `-GameArgs BASE,PAIR` (+ WATER) (**full
         read-back**).
      3. `v13c_off`: OFF; `v13c_off_before`: OFF with `-ExeDir
         out\build\win-amd64-release-pre13b` (the controller's alone), one session.
      4. **Timing: each run alone, the window on the usual monitor, shots 1942 wide, nothing
         beside it, one session**: `time13c_off_1`, `time13c_on_1`, `time13c_on_2`,
         `time13c_off_2`: TIME with `-GameArgs BASE,POST,EXPO` (+ WATER).
- [ ] The freeze signature; `check_discovery.py --rows-only`; the shots' width in group 4.
- [ ] **Criterion 9**: `water_replay.py colour --all` on runs 1 and 2: on all water pixels red,
      green and blue within 0.92 to 1.08, the hue within 6 degrees, the 5th, 50th and 95th
      percentile of the native luminance each within 0.93 to 1.07 of the game's (*no
      precedent*).
- [ ] **Criterion 10, second part**: `water_replay.py targets --silhouette`: of the game's texels
      that are neither sky nor black at least 85% within one texel of a native texel that is
      neither, and the reverse; on the texels that are silhouette in both, each channel within
      0.85 to 1.15 (*no precedent*); by base, with the count of targets that are `reflection:
      terrain`.
- [ ] Criteria 6 (the reflection records beside the water's: `water_check.py reflection`), 7, 14
      and 15 again on this binary, each with its figure beside Task 13's; **criterion 16 (13c)**:
      Task 17's software-adapter runs; **criterion 17 (13c)**: the timing session by Task 13's
      rule and script, **valid only at shot width 1942 on the usual monitor with nothing beside
      the runs**.
- [ ] Each of criteria 9, 10, 16 and 17 with its file and an exact verdict word. Spec status and
      "Implementation notes"; frame map "Validation (13c)" and "Limits (13)". Commit "Water
      validation (13c): the far shore in the reflection, the verdicts". Then the phase's review.

---
## Closing

### Task 19: Documents, the closing table, the whole-branch reviews

Made whether the sub-project closes at phase 13c, at phase 13b or at the capture. **Every step
below is a numbered checkbox of its own, and the task's report quotes the list with each box's
commit or "nothing to change: <why>". No step is folded into another, and the report is not
accepted with a number missing.**

- [ ] 1. **Spec** (`docs/superpowers/specs/2026-10-12-native-renderer-water-design.md`): the
      status line; "Implementation notes (deviations)" with the closing table of all 18 criteria
      (verdict word, figure, file); every open question 1 to 10 with what became of it; every
      line of "Limits known beforehand" with what the runs showed; this plan's thirteen
      departures, each "stood" or "changed".
- [ ] 2. **This plan**: an "As executed" paragraph under each task whose runs, names, numbers or
      order differed from what is written; every line of the provisional table marked "As fixed
      by Task N".
- [ ] 3. **Frame map**, section 20 "Water: the lake's surface": its subsections in this order,
      each present or named as not built: "Discovery", "The formulas against the captures", "The
      water's textures", "The replays", "Discovery captures", "Water records", "The water's
      rules", "The reflection targets", "The water step", "Validation (13b)", "Limits (13b)",
      "The water's edge", "The sunward view", "The reflection views", "Reflection draws", "The
      targets' terrain", "Validation (13c)", "Limits (13)", "Follow-ups (13)".
- [ ] 4. **Frame map, stale statements marked where they stand** (a dated note at each, not a
      rewrite): section 13 "Alpha and missing shaders" (`F6D9` and `A17D` as complementary
      parts); section 16 "The lake" (`F6D9` as "the water near the shore"); section 17 ("14,769
      pixels below 18.47: the cube's floor is the lake's surface", "8 reflection views at the
      third spot", "all 8_8_8_8 textures of those shaders read as zero"); line 6730 and "The
      layer's depth place" (the `A17D` rows between: now drawn, with depth); section 19's and the
      roadmap's "the cubes need nothing built while the lake's meshes are drawn with depth"
      (ledger ruling 26: nothing is built; why); line 4072 (the sky's place: the water now stands
      behind it).
- [ ] 5. **`docs/native-renderer/lighting-formulas.md`**: the part "Water" complete (vertex,
      alpha, normal, lookup, base, mix, sun, fog, the mirrored camera, the blur; what stands on
      inference).
- [ ] 6. **README**: the row of `--fable2_native_water=off|on` in the switches' table (after
      `--fable2_native_impostors`, line 101); the F3 paragraph's count of native lines (line 104:
      one more, `Water:`); a paragraph "Water records" and one "The water step" beside the
      impostors'; the discovery members of Task 1.
- [ ] 7. **`docs/native-renderer/user-checks.md`**: a heading "Sub-project 13
      (native-renderer-water)" and the checks **appended without renumbering any earlier check**,
      from 101 (the file's last is 100; if 101 is taken by then, from the next free number, and
      the "As executed" paragraph says which): 101 the lake in motion, native against emulated,
      in split view (the ripples' scroll, the glint's sparkle; criterion 12 if it read "not
      decidable"); 102 the shoreline on a walk along the water (the fade, the near water;
      criterion 11 if it read "not decidable", with the walks that were tried); 103 the far
      shore's reflection (after phase 13c: where the emulated half has it; at phase 13b: its
      absence, and whether it disturbs); 104 beams over the lake with `--fable2_native_sun=on`;
      105 the water in another region and at another time of day (and the Water line there:
      `opaque-live`, `reflection`, `plane`, `shadow-term`); 106 everything off is the picture of
      before.
- [ ] 8. **`docs/native-renderer/roadmap.md`**: item 3 (line 65) ticked with **what was and was
      not achieved** (the footprint's ratios and hue before and after; phase 13b or 13c; every
      criterion not met as written); new or kept, each a line: the far lake's cube and its
      165-instruction shader (ledger ruling 4); objects, flares and `6954` / `A4A9` in the
      reflection; the reflection views issued natively (with item 6); other water bodies and
      regions, an opaque water form, the term under `b137`; slot 14; the in-scene resolve to
      `0x13401000`; the layer's depth place against a depth-writing blend; the normal map
      uploaded block-compressed; the unidentified pair `43ED` / `7F65`; the SDK's copy interface
      B2 as the user's separate decision (ledger ruling 2); the monitor experiments (ledger
      ruling 8); the line "User checks 40 to 100" with its new end.
- [ ] 9. **`docs/native-renderer/vs-transforms.json` and `ps-albedo.json`**: unchanged (`A17D`
      and `F6D9` stay `no_albedo`; the water is drawn from its side block); the closing says so
      in one line of the frame map, so that nobody looks for an entry.
- [ ] 10. **The research** (`.superpowers/sdd/water-research.md`, untracked): a dated line at
      each of the ten places the spec's "What the research has differently" names, and at B4
      point 4 (ledger ruling 26).
- [ ] 11. **The ledger** (`.superpowers/sdd/2026-10-12-native-renderer-water/progress.md`): the
      tasks' table complete; every ruling made during execution; the closing summary with the
      questions the user must answer (the default of `--fable2_native_water`; what is pushed; the
      SDK's B2; the monitor session).
- [ ] 12. Commit "Water closing: the criteria's verdicts, the documents" (tracked files by
      pathspec).
- [ ] 13. **The whole-branch code review** (a fresh reviewer, read-only, the branch against
      master `ce61f2d`): the five review-focus items first, then guest reads, the frame's
      recheck, the switch off. Findings are fixed test-first in a fix wave; a fix that changes
      bindings, targets or shader text gets its own software-adapter run before any real-GPU run.
- [ ] 14. **The documents review** (a fresh reviewer): every number against its file; every "met"
      against the criterion's own words; the closing table against the tasks' reports; steps 1
      to 11 of this list, one by one.
- [ ] 15. After the fix wave: the suites, the build, the runs the fixes call for, steps 1, 2, 3
      and 11 again where a figure changed. Commit.

---

## Self-review: every criterion and the task that decides it

| Criterion | Decided in | By |
|---|---|---|
| 1 formulas | Tasks 2, 3 (made inputs, mutations); Task 4 (`rows`) | `water_check.py formulas`, `rows` |
| 2 the dead draw | Task 2 (part (a): the microcode); Task 4 (parts (b), (c): rows and masks) | `water_check.py dead` |
| 3 the mirrored camera | Task 4 | `water_check.py views`, three mutations |
| 4 the formula with the game's own picture | Task 6 (the captures in hand: orientation and channel order decided); Task 8 (again on W1 with the stored levels) | `water_replay.py scene` with its two controls |
| 5 the mirrored sky against the game's picture | Task 6; Task 8 (each frame's own pictures) | `water_replay.py sky` |
| 6 records | Task 9 (first reading), Task 13; Task 18 (with the reflection draws) | `water_check.py records`, `reflection`; `static_assert(sizeof(DrawRecord) == 616)` |
| 7 cover, and the bed's depth | Task 12 (first reading), Task 13 | `water_replay.py cover` on the plane taken before the water |
| 8 colour on the lake after 13b | Task 12 (first reading), Task 13 | `water_replay.py colour` on the sky-sampling pixels |
| 9 colour on the lake after 13c | Task 18 | `water_replay.py colour --all` |
| 10 the target against the game's picture | Task 13 (the sky: 13b part), Task 18 (the silhouette) | `water_replay.py targets` on the stored targets |
| 11 the shore fade and the near branch (conditional) | Task 14 | the EDGE walks, `water_replay.py edge`, V-EDGE; else "not decidable" and user check 102 |
| 12 the glint (conditional) | Task 6 (the replay on `160104`, `160251`), Task 14 | `water_replay.py glint`, V-SUN; else "not decidable" and user check 101 |
| 13 the sun layer's depth at the lake (without the cubes: ledger ruling 26) | Task 13 | the layer plane of the bridge pair with the sun on (`t13_layer.py`) |
| 14 the land beside the water, and the pools | Task 13; Task 18 again | the pairs' stored scenes, on the pixels common to both runs |
| 15 off is today's picture | Task 5 (returns 220, 221), Task 10 (CHECK 845), Tasks 11 and 12 (case 55 (a), (g)), Task 13 (run OFF against the binary of before) | tests; `region_compare.py` with its control |
| 16 errors | Task 12 (13b), Task 17 (13c) | the software-adapter runs; "the step drew" |
| 17 cost | Task 13 (13b), Task 18 (13c) | the timing sessions: alone, 1942 wide, the usual monitor; the capture median; the step's CPU time |
| 18 the evidence rule | every task; Task 19's two reviews | - |

If phase 13c is not entered, criterion 9 and criterion 10's second part read "not built", criteria
16 and 17 have their 13b runs alone, and the closing table says so for each. If phase 13b is not
started (ruling 11: a formula did not close), criteria 6 to 17 read "not built" but 6, which Task
9's capture still decides.

Placeholders: none is left for an implementer to invent. Three places hand the implementer a name
the plan's author could not read to the end, and each says so where it stands: `StateWords`'
field names and the fetch constants' packing in Task 9's test; `wind_hlsl.Program`'s constructor
and `call` in Task 7's; the BC4 block's index bytes in Task 5's. Names across tasks: `WaterRecord`
(584 bytes, `place`, `kind`, `fetch[kWatPicture]`), `WaterDraw`, `WaterReason`, `ClayWater`,
`ClayWaterInputs`, `DecideClayWater`, `WaterSceneVerdict`, `WaterLeavesOut`, `WaterOpaqueDead`,
`WaterViewRows`, `WaterMeshHeight`, `PlanWaterTargets`, `PlanWaterPlace`, `ClayWaterConstants`
(432 bytes), `kClayWaterStepSlots`, `WaterFrameBlock`, `kSkyMirrorVs`, `kClayWaterVs`,
`kClayWaterPs`, `DecodeK8Level`, `DecodeDxnLevelRg8`, `WaterTextureFetch`, `ResolveWater`,
`kSceneDumpPreWater` (8), `kSceneDumpTargets` (16) are used with one meaning and one signature
from the task that produces each to the tasks that consume it.

## The runs, in order

Every run is the controller's, through `tools\drive_game.ps1 -BootSave Hero000`, after the user's
leave (ledger ruling 7). **Phase 13a (Tasks 1 to 7) has none.**

1. W1, W2 (Task 8; Task 1's build; full read-back) and, in the same session if there is time, the
   three guessed walks EDGE-1 to EDGE-3 (Task 14; full read-back).
2. `r9_bridge_on`, `r9_third_on`, `r9_bridge_off` (Task 9; full read-back). Before Task 9's first
   build the controller copies the build folder to `out\build\win-amd64-release-pre13b`.
3. `warp_on`, `warp_dump` (Task 12; the software adapter, 240 s each). Only if clean: the EARLY
   LOOK, `look_off` and `look_on` (Task 12; full read-back). **The shots are viewed and described
   before anything else is run.**
4. Task 13's five groups: the pairs at the bridge, the third spot and the bridge with the sun
   (full read-back); OFF on this binary and on the binary of before, and the defaults control;
   the timing session of four runs (each alone, 1942 wide, the usual monitor).
5. Task 14: at most three corrected walks; if a walk reached the water, `v_edge_off` and
   `v_edge_on`; if a sunward view has a glint, `v_sun_off` and `v_sun_on` (the held look), else
   one `w4`.
6. Only after the controller's ruling for phase 13c: `r16_bridge_on`, `r16_third_on` (Task 16);
   `warp_13c`, `warp_13c_dump`, then `look13c_off` and `look13c_on` (Task 17); Task 18's four
   groups, the timing session of four runs among them.
7. Whatever the fix wave of Task 19 calls for.


