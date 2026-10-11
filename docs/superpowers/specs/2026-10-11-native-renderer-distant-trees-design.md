# Native renderer sub-project 12: distant trees (the far woods' impostors)

Status: **design, 2026-10-11; nothing built, nothing run.** Branch `native-renderer-distant-trees`
(from master `d03f0be`). Research: `.superpowers/sdd/distant-trees-research.md` (untracked; its
scripts and outputs in `out/native-evidence/distant-trees/`, "DT" below). Ledger:
`.superpowers/sdd/2026-10-11-native-renderer-distant-trees/progress.md` (rulings 1 to 4 are the
controller's and are not reopened here; where this design finds something that bears on one of
them it says so under "Decisions that are not this design's"). Plan:
`docs/superpowers/plans/2026-10-11-native-renderer-distant-trees.md`.
Game launches wait for the user's leave (ledger ruling 4): every part below says whether it needs a
run.

**Amended 2026-10-11 for the controller's rulings 21 and 22** (ledger): ruling 21 strikes the
impostors' pre-pass draw; ruling 22 orders the work in phases (the shared work first; the pictures
by read-back last, and only if the cost pair passes its bound; otherwise the sub-project closes at
`flat`). Each amended passage is marked "(ruling 21)" or "(ruling 22)"; nothing else was changed.

Every fact carries the research's label or names what it was read from: **[measured]** printed by
a named script from a capture, a stored scene, a shot or a log; **[microcode]** read from
`out/shader_dump/shader_<HASH>.ucode.vert` / `.frag`; **[code]** read from the repository's
source; **[inferred]**; **[unknown]**. "FM" is `docs/native-renderer/frame-map.md`; "R" with a
section is the research; "CAP `<stamp>`" a discovery capture as R names it.

## Goal

The game's far woods stand in the native view: one camera-turned quad a tree, placed and sized by
the game's own vertex formula from the game's own per-tree data, cut out and coloured by the
game's own picture of that tree model, dissolved by distance, lit by the game's two scalars and
fogged once per tree as the game does; written into the native depth, so that the far ranges take
the game's brightness and the native sun layer's depth holds the woods. Behind
`--fable2_native_impostors` (ledger ruling 3), default off.

In ledger ruling 1's shape: option A of R 6.2, its flat-colour step C first; the pictures by forced
read-back of the game's textures if the read-back passes the cost bound of Design 3, which is the
first run made; option B (the pictures rendered natively) is the stated fallback and is not
designed here. *(Ruling 22)*: the work is done in three phases: 12a (discovery, the formulas, the
decoder; no renderer change), 12b (the capture's side block, the switch, the draw in `flat`, its
validation), 12c (the pictures by read-back: Design 12 and the half part of Design 10), and 12c
is entered only if run COST passes its bound. If COST fails or is twice not decidable the
sub-project closes at `flat` with the capture and the formulas, and the pictures rendered natively
(option B) become the next roadmap item; no SDK change and no `--readback_resolve=fast` are made
by this sub-project.

**Non-goals**: the pictures rendered natively (option B: Design 13); water, the far lake's cube,
the reflection views (roadmap 3); shadow correctness (roadmap 4; ledger ruling 2); the cubes at
20.0024 (nothing to build: R 4); the unidentified draws `43ED` / `7F65` and `7D1C` / `983E`, the
sprites and particles (R 2's table); the ground-fog volume of the impostor's pixel shader (its
block under bool `b129`, clear in every capture); MSAA and real alpha-to-coverage (roadmap 5);
any other time of day, region, town or interior; the near differences of R 5.4; the leaf's light
(0.88 of the game's, sub-project 11's follow-up).

## What is known (research; evidence there)

- **The draws [measured, R 1.1]**: `D3DDevice_BeginVertices` (`0x822060A0`), vertex shader
  `129F7F2793DA9084`, pixel shader `4B1D3E6E8E58D3AC`, primitive 13 (a quad list), `count` four
  times the quads, one vertex fetch (`vf0`, `FMT_16_16_16_16_FLOAT`, stride 2 dwords) at the
  computed index `floor((i + 0.5) * 0.25)`: one half4 a quad. Depth `0x00708766`, blend none,
  colour control `0xAA00001C`, the main scene's surface. Issued after the last opaque object,
  before the terrain layers and before the game's depth copy (14 of 14 and 10 of 10). No such row
  in any other pass. A frame: 14 draws and 928 quads at the bridge, 14 and 1,076 at the third
  spot, 10 and 112 or 117 in the two sunward views.
- **The pictures [measured, R 1.2]**: each draw's `tf0` is a 256 x 256 texture of format 3
  (`k_1_5_5_5`), tiled, endian 1, levels 0 to 3, at `0x12704000 + n * 0x2B000`, the lower levels
  from base + `0x20000`. The game renders each again every frame from the real tree meshes (an
  orthographic view, the tree's own wind constants, exposure 2.7261, shadow term 1, fog off) and
  resolves it; three copy passes make 128, 64 and 32. Half of the tree models have no draw in the
  main scene (17 of 34 rendering rows at the bridge).
- **Guest memory [measured + code, R 1.2]**: without full read-back only the first picture is in
  guest memory (its base is the one address of `readback_resolve_force_addresses` in a normal
  run); five of the others are noise, eight empty (CAP `054648`). Under `--readback_resolve=full`
  all ten of CAP `074119` are trees on a clear ground (sheet viewed; viewed again for this design).
- **Why nothing is drawn [code, R 2]**: `AssembleRecord` refuses the primitive
  (`draw_record.h` line 332, counted in `unsupported by hook`); no vertex bytes are read for this
  builder; no transform entry can serve a position built from a per-quad fetch; format 3 has no
  decoder; 13 of 14 pictures are not in guest memory.
- **The pixel formula [microcode + measured constants, R 3.2; read again for this design,
  instructions 7 to 15 and 58 to 60]**: with `d` the distance from pixel `c19` to interpolator
  `o0`: `colour = tf0.rgb * c31.x * (1 + saturate(0.1 * (d - c12.y)) * (c12.w - 1))`,
  `alpha = tf0.a * saturate((d - c29.x) * c29.y)`, `oC0.rgb = (colour * o2 + o3) * c20.w`,
  `oC0.a = alpha`. `c29` = (48, 0.125, 998.4, 25.6) at three views; `c12` = (20, 35, 2, 0.468 to
  0.499); `c31.x` = 0.3668, 0.3570, 0.3597; `c20.w` the frame's exposure.
- **The gap [measured on one frame pair, R 5.1]** (bridge, CAP `182313` beside the stored native
  scene `182317`, a binary before wind and coverage; `DT/depth_gap_182313.txt`, read again): far
  pixels where the game is nearer than the native depth by more than 3% or the native picture has
  sky: 48,051 (5.96%), native over game 1.433 there. Regions, whole: far mountains left 1.336,
  centre 1.151, right 1.117, far rock below the ridge 1.108; on their pixels at the same distance
  in both pictures 0.993, 1.002, 1.001, 1.000. After post, on shots: 1.494, 1.203, 1.134, 1.106 at
  the bridge (the emulated pair drifts by 6 to 10%), 1.259 and 1.085 at the third spot.

**Read for this design** (not in the research, or other than it says; each is repeated under
"What the research has differently" at the end):

- *The dissolve is three steps a tree, not a dither [code: FM "Coverage: the rule and the order";
  microcode]*. Under offsets `AA` the two samples' thresholds are 0.25 and 0.75 at every pixel
  (no parity term), and the dissolve factor is one value a tree (it comes from `o0`). So a tree's
  impostor is absent below 50 units, covers one sample of two from 50 to 54 where its picture's
  alpha is 1, and both from 54. And at factor 1 a pixel whose filtered picture alpha lies in
  [0.25, 0.75) is half covered: on a minified picture that is not "edge texels alone"
  (sub-project 11 measured 24.0% on one minified plant).
- *`o0` is not the fetched place [microcode, instructions 17, 28, 29, 31, 70, 88; read, not
  run]*. Instruction 17 makes `s * c8.z` (`s` the fetched size), the scalar chain of 28 and 29
  adds it to the fetched third coordinate, and the raised value is what the height comparison
  (31), the fog's eye vector (70) and `o0` (88) use. So `d`, the dissolve and the fog are taken
  from a point raised by `s * c8.z` above the fetched place. `c8.z`'s value is in no capture.
- *The impostor's fog block has no `c8.x` factor [microcode, instructions 70 to 85]*: `o2` is the
  transmittance read from `tf16`, `o3` is `(1 - o2) * tf17(angle)`; before the start `o2` = 1 and
  `o3` = 0. The trees' native form multiplies by the draw's `c8.x` (FM "Native fog"); this shader
  uses `c8` for the quad's height instead.
- *The picture's fetch has no gamma sign [measured: `DT/impostor_consts.txt`, `tf0` dword 0 =
  `0x8200D802`: sign fields 0, pitch 256, tiled, clamp mode 6 on both axes]*. Sub-project 10's
  base-map fault (a texture read as stored where the game applies gamma) does not repeat here.
- *`0x12704000` is the dog's texture's address too [code + document:
  `plans/hero-dog-shifty-texture-mipmap.md`, `thirdparty/rexglue-sdk/src/graphics/
  command_processor.cpp` lines 203 to 274]*. That plan found "a 256 x 256, fmt 3, packed-mips
  texture resolved every frame" at the forced address (`mip=0x12724000 max=3`) beside the dog's
  512 x 512 chain resolved once: it is impostor 0. So the addresses are a pool the game reuses,
  and the SDK's own comment warns that forcing the neighbours of this address (it names
  `0x12724000`, impostor 0's level 1) made the dog "glitchy / red / shifting". A forced resolve
  must be issued as `PM4_DRAW_INDX_2`, a rectangle list of three indices, with its destination in
  [entry, entry + 64 KiB): impostor 0's is (it is read back today), the other thirteen are
  resolves of the same kind **[inferred]**.
- *A cost prior exists [measured: FM "Exposure read-back", runs 441 to 446; "Cost: the two
  timing sessions"]*. Each forced resolve ends the submission and waits for the GPU. One more
  forced resolve a frame cost 2.4 to 13 ms a frame in the ungated window state (runs A against
  F); capped at 30 fps on the usual monitor two a frame held 30.0. A normal run already has one a
  frame (the hero's entry, which is impostor 0's base): the cost pair is fourteen against one.
- *The texture cache refuses what changes every frame [code: `texture_residency.h`,
  `kDynamicStreak` = 8]*: a texture whose sampled bytes change in more than eight consecutive
  frames is `texture-dynamic` and not drawn. The pictures need a path of their own.
- *The pre-pass's depth is the mask's alone [code + FM "Native shadows"]*: the colour pass clears
  the depth again after the mask step, a record wholly beyond the mask's reach is left out of the
  pre-pass (`PrepassBeyondReach`), and the reach is `c12.y + 10` = 45 units, inside the 50 units
  below which no impostor pixel exists. R 6.1's risk "must write the same depth in both passes"
  does not arise. *(Ruling 21)*: for the same reason the impostors have no pre-pass draw at all
  (Design 9).
- *The RHI can draw this without a change [code: `rex/graphics/native_rhi.h`]*: vertices by
  `SV_VertexID`, a buffer bound by address (`SetBufferSrv`), `kR8G8B8A8_UNORM` textures with mip
  levels, `CopyBufferToTexture` per level. It has no 1-5-5-5 format: the picture is decoded on
  the CPU.
- *The vertex storage [code: `xdk_layout.h` lines 317 to 336]*: the builder reserves
  `count * r6 / 4` dwords (four dwords a quad at `r6` = 4), keeps the pointer at device +
  `0x3488` and the dwords at + `0x3490`, and the game fills the storage after the hook returns.
  The fetch steps two dwords a quad, so the shader reads the first half of what is reserved if
  the fetch constant's base is the storage's start **[inferred; R: "which half is filled is
  unknown"]**.

## Open questions, each with the run that settles it

| | Open | Settled by | If it cannot be settled |
|---|---|---|---|
| 1 | whether forced read-back of 14 resolves a frame passes the cost bound | run COST (Design 3), before anything of Design 12 is built | "not decidable" twice: the controller chooses between A and B |
| 2 | the storage's bytes; that the fetch reads its first half; the fetch constant's endian and the half4's component order | capture D1 (`"vertsdata"` rows beside the row's fetch list and fetch constant) | the capture is not built; nothing is drawn |
| 3 | the values of vertex `c8`, `c46`, `c47`, `c72..c75`, `c110`, `c111`, `c113..c117` at a `129F` draw; which of them a constant-buffer load can name | D1 (the whole bank in the row) | as 2 |
| 4 | the position formula, the `log` / `exp` block included | Design 4's check: made inputs (no run), then every quad of D1 and D2 | no formula, no impostor drawn (ruling 18) |
| 5 | whether `c72..c75` are the scene's view-projection, vertex `c47` the eye of pixel `c19`, vertex `tf16` / `tf17` the frame's fog tables, `c110` / `c111` the fog block's axis and range | D1, D2: identities printed by the check | each is carried per draw as read; the fog falls back to "no fog for this draw", counted |
| 6 | the lower half of each picture, the three lower levels, how the game's levels follow from its base | D1, D2 (format 3 dumped whole with its levels) | the levels are made by a box filter, the cover criterion says how far that is off |
| 7 | whether `c31.x` times the rendering rows' `c20.w` is 1 in every frame | D1, D2 | reported; the formula takes `c31.x` as it is either way |
| 8 | the share of an impostor's pixels with one sample of two, at the level a fetch takes | the replay on D1 and D2 (Design 10) | the half part is built on ruling 13's word alone |
| 9 | which view has trees between 46 and 58 units from a standing camera | the tree places of D1 and D2 (world coordinates), then capture D3 | criterion 11 is "not decidable"; user check |
| 10 | whether any view of this save shows far woods inside the sun layer's lit texels | D4 (the sun stands about 47 degrees up and the woods a few degrees: such a view may not exist; inferred from the Light line's sun direction in log 657, (-0.673, 0.097, 0.733)) | criterion 12's second part is "not decidable"; its first part is judged at the bridge |
| 11 | whether the pool's addresses, the count of 14 and the fetch's shape hold in another view | D1 to D4 and the validation runs' counters | a draw outside the measured shape is not drawn, counted |
| 12 | whether the emulator's resolve into format 3 gives the console's bits | no run of this sub-project | a limit |

## Design

In the order work is done. Parts 1, 4, 5 and the decoder of part 12 need no game run; the first
runs are COST (part 3) and the captures D1 and D2 (part 2). *(Ruling 22)*: the phases are 12a =
parts 1 to 5 and the decoder of part 12; 12b = parts 6 to 9, the first sample's rule of part 10
and part 11; 12c = part 12 and the half part of part 10, entered only if run COST passes.

1. **Discovery additions** (capture only, armed captures only; the pass draws nothing new).
   - A `"verts"` row **inside the main scene** gains what a draw row has: the whole vertex bank
     (as `"consts"` holds `c0` today), the vertex shader's literals, its fetch list, the vertex
     fetch constant the builder wrote for the storage, and the vertex texture slots 16 and 17.
   - A **`"vertsdata"` row**: the storage's bytes (`storage_dwords` x 4), read when the drawing
     thread enters its next hooked draw, as `"indxdata"` rows are (`PendingIndexed`,
     `capture.cpp` line 2598). The row names its `"verts"` row and says how many bytes were
     read; a read that fails says so.
   - **Format 3 gets a layout** in `DumpFormatInfo` (`texture_decode.h` line 154: one texel a
     block, two bytes), so that the base level is dumped whole instead of its first 65,536
     bytes, and the stored levels follow by the existing mip dump. A 2D texture of format 3 is
     kept once per content (`DumpKeyedByContent`): its memory holds another picture each frame.
     Nothing in the product reaches `DumpFormatInfo` for this format (its one product caller
     asks for `k_DXN`).
   - A row that renders a picture (outside the scene, at the target of pitch 320) gains its
     viewport and scissor registers: for the fallback's later design, at no cost here.
   - `tools/xdk_sigmatch/check_discovery.py` knows the new row and members.
2. **Five discovery captures**, each two frames, `-BootSave Hero000`, the camera standing for at
   least five seconds before the capture (sub-project 11's sunward view existed only while a
   stick was held). D1 to D4 with `--readback_resolve=full` and
   `--fable2_native_scene_dump=on`, the impostors' switch off:

   | | View | What it must show |
   |---|---|---|
   | D1 | the bridge, standing | 14 `"verts"` rows of `129F` with bank, literals, fetch list and fetch constant; 14 `"vertsdata"` rows whose first `8 * quads` bytes are finite half4s (open question 2); 14 pictures whole with three levels each, in both frames; the game's depth copy and scene; the stored native scene of the same frame. It is the oracle of position, cover and colour and the baseline of every "before" figure at the bridge |
   | D2 | the third spot | the same for 1,076 quads; the gap in pixels there, which the research could not make |
   | D3 | trees between 46 and 58 units, chosen from D1's and D2's tree places | at least three quads in that band and their mesh twins with a fade word below 1: where mesh and impostor cross |
   | D4 | far woods inside the sun layer's reach, the sun's switch on in the native run | the game's layer planes and the stored native layer depth of one frame (open question 10) |
   | COST | the bridge, no capture needed for the timing | Design 3 |

   From D1 and D2, before any product code: `DT/depth_gap.py` again (the "before" of this binary,
   with wind and coverage on), the identities of open questions 5 and 7, the bounds of Design 6's
   registers, the picture rule's thresholds (Design 12).
3. **The cost pair: the first run, and the decision between A and B.** No new code: the list is
   given by hand. Configuration `one`: a normal run (the seeded list is the hero's address, which
   is impostor 0's base). Configuration `fourteen`:
   `--readback_resolve_force_addresses=` the fourteen bases `0x12704000 + n * 0x2B000` (the level
   addresses are not in it). The bridge plan of logs 654 to 656 (`k18b_time_*`: standing, native
   view from 39 to 90 s), post and bloom on, at shot width 1942, the head's uncapped default
   (`guest_vblank_pacing=false`), each run made alone, four runs in one session in the order
   `one`, `fourteen`, `fourteen`, `one`; a discovery capture at the end of each run, after the
   timing windows.
   - *That the read-back happened*: in each `fourteen` run's capture the 14 `tf0` dumps are
     trees (the sheet of `DT/impostor_tex.py`, viewed); in each `one` run's, the first alone.
     (The SDK's `Forced readback-resolve` lines cannot show it: the first 24 of a process are
     used up in the front end, FM line 4725.)
   - *The bound, stated before the run (ruling 15)*: the read-back **passes** iff no steady
     150-frame native window of a `fourteen` run is below 30.0 fps, and the mean frame over the
     steady native windows, averaged over the two runs of a configuration, is at most **2.0 ms**
     longer with `fourteen` than with `one`. If the two `one` runs differ by more than 1.0 ms in
     that mean the session is "not decidable" and is made once more; twice not decidable is the
     controller's to rule. Reported beside it: the guest's swap median (where the wait of a
     forced resolve shows: FM "Exposure read-back"), the emulated view's windows, the capture
     median.
   - *The emulated picture*: the `fourteen` runs' emulated shots against the `one` runs' in the
     bridge's regions (hero's shirt, the dog, the four far regions): criterion 16.
   - *Reported, not deciding*: one run with `--readback_resolve=fast` (the SDK's delayed read of
     every resolve), which would be another way to the same bytes and changes how the whole game
     is run: a decision that is not this design's. *(Ruling 22)*: this run is not made by
     this sub-project.
   - **Passes**: Design 12 is built. **Fails**: Design 12 is not built; the sub-project ends
     with the impostors in flat colour (the switch's value `flat`), the decoder as a tested
     tool, and the pictures as the fallback's work (Design 13).
   - *(Ruling 22)*: the run is made first among the runs, and its verdict is the entry
     condition of phase 12c and of nothing else: phases 12a and 12b are built whatever it says.
     "Fails" includes "twice not decidable": the sub-project then closes at `flat` and option B
     is the roadmap's next item. Until phase 12c is built, and for good if it is not, the
     switch's value `on` captures as `flat` does and draws no impostor: every record is counted
     `picture: unread` (no flat quad stands in for a picture: Design 12), no read-back is asked
     for, and one log line at install says so. Neither an SDK change nor
     `--readback_resolve=fast` is tried by this sub-project.
4. **The position formula** (no run for its first part). `impostor_position` in
   `tools/xdk_sigmatch/light_formulas.py`, transcribed from `129F`'s 81 instructions and held to
   the microcode by `tools/xdk_sigmatch/impostor_check.py` with `ucode_eval.py` as the oracle,
   as the wind's formulas were. What the microcode shows **[microcode, read, not run: the check
   is what makes it a formula]**:
   - the fetch delivers one half4 a quad, `(x, y, z, s)` after its swizzle; the corner is
     `k = i - 4 * floor((i + 0.5) / 4)` (instructions 10 to 12, 19 to 21);
   - corner offsets from `s` times `c46.x`, `c46.y`, `c46.z` and `c8.z`, two candidate points
     chosen by the corner (15 to 27); the third coordinate raised by `s * c8.z` (29);
   - **the block the research did not read** (13, 14, 28 to 57): `c115`, `c116`, `c117` as three
     rows applied to the place and to the corner, two powers `exp(c114.y * log|a|)` with a scale
     `c114.x` (40 to 46), a reciprocal (48), and one of two arrangements chosen by whether the
     raised height is above the eye's (`c47.z`: 31, 53 to 57); its results are mixed into the
     corner by the corner's selectors (58 to 66);
   - a last horizontal term `(2 * u - 1) * s` (67, 69), then
     `oPos = (c72 . p, c73 . p, c74 . p, c75 . p)` (72 to 76);
   - `o0` = the raised place, the same at four corners (88); `o1` = the corner's coordinate
     through `c113` (68, 87); `o2`, `o3` = the fog (Design 5).

   The check: `impostor_check.py formulas`: `oPos`, `o0` and `o1` within
   `1e-4 * max(1, |value|)` in every component on 2,000 of 2,000 made inputs; `rows`: on every
   quad and corner of every `129F` row of D1 and D2 (D3 and D4 when they exist), with the row's
   own bank and `"vertsdata"`. Mutations that must fail: `c116` for `c115`; the fetch's first
   two components exchanged; the size's sign; two corners exchanged; the exponent changed by 1%;
   the raise left out; two rows of `c72..c75` exchanged. Where the reference is not a number
   (a zero under the `log` with a zero exponent's product, a zero under the reciprocal) the
   formula returns what `ucode_eval` returns, and a quad with a corner that is no number is not
   drawn: its position stays no number in the native program, which the rasteriser drops.
   `impostor_pins.py` writes the numbers the C++ mirror and the HLSL are pinned to;
   `tests/test_impostor_formulas.py` holds a reduced count. **A formula that does not close
   draws nothing** (ruling 18).
5. **The pixel formula and the fog** (no run). `impostor_pixel` and `impostor_fog` in
   `light_formulas.py`, checked by `impostor_check.py pixel` against `4B1D`'s microcode on 2,000
   made inputs within 1e-5 and with each frame's captured constants:
   - `alpha = picture.a * saturate((d - c29.x) * c29.y)`: with the captured `c29`,
     `saturate((d - 48) / 8)`;
   - `light = 1 + saturate(0.1 * (d - c12.y)) * (c12.w - 1)`: 1 up to 35 units, the mean shadow
     term `c12.w` from 45;
   - `colour = picture.rgb * c31.x * light`: `c31.x` undoes the picture's own exposure;
   - `out = (colour * o2 + o3) * scale`, `scale` the game's `c20.w`;
   - the fog once a tree: from the raised place and the eye `c47`, the distance beyond the
     start, the angle to the axis `c110`, the two tables; `o2` the transmittance, `o3` the
     in-scatter times `(1 - o2)`; before the start 1 and 0. `fog_check.py` already lists `129F`
     among its vertex shaders (`light_formulas.py` line 845): the plan's first step of this part
     says whether that check holds `o2` and `o3` of this shader term for term, and extends it
     if not.

   Mutations that must fail: `c12.y` for `c29.x`; the literal 0.1 changed by 1%; `c31.x` left
   out; the light factor's sign; the alpha without the picture's; the fog's `(1 - o2)` left out;
   a `c8.x` factor put in. A draw with bool `b129` set is not modelled and is refused (ruling
   20).
6. **Capture: a bounded side block of impostor records** (the quad records of sub-project 10
   are the pattern: FM "Quad records"). **`DrawRecord` stays 616 bytes; no draw record is made
   or changed for these calls**, and the `unsupported by hook` line keeps counting them as it
   does today (ruling 10).
   - *Where*: at the return of an outermost `BeginVertices` call inside the main scene, in a
     frame with records and with the impostors wanted (latched at the frame's boundary), on the
     path that already counts the call as `unsupported-prim` (`capture.cpp` line 4738): a draw
     that makes a record executes nothing new, with the switch on or off.
   - *Which call*: the pixel shader's microcode hash is `4B1D...` and the vertex shader's
     `129F...`, both; each literal the formulas read is held to the shader object's own literal
     table once an object (`c254`, `c255` of the vertex shader; `c255` of the pixel shader), as
     the wind's are.
   - *At the return*: the builder's arguments (primitive 13, a count that is a multiple of 4,
     `r6` = 4) and the device's storage fields (the pointer not null, the dwords equal to the
     count); the vertex registers of the formula's generated list (`impostor_table.inc`, from
     `light_formulas`: expected `c8`, `c46`, `c47`, `c72..c75`, `c110`, `c111`, `c113..c117`,
     thirteen); pixel `c12`, `c19`, `c20`, `c29`, `c31` through `ReadPixelConstant`; the pixel
     bool word; `tf0`'s fetch constant and the vertex slots' 16 and 17; the state words: depth,
     blend, colour control, alpha reference, colour mask, surface info, the viewport's Z scale
     and `PA_CL_VTE_CNTL`. A static assert, as the quads have, that no register read from the
     bank can be named by a constant-buffer load; one that can is read by the light inputs'
     path instead (open question 3).
   - *One hooked draw late*: the first `8 * quads` bytes of the storage, whole or not at all,
     copied before they are judged, when the thread next enters a hooked draw; the pending test
     there is the word the pending quad already uses, so a draw's entry executes nothing new.
     The frame was told of the call at the call; a record that arrives after its frame ended is
     dropped and counted, and that draw is missing from that frame.
   - *The record* (`capture::ImpostorRecord`, about 400 bytes; at most **32 a frame**): the
     hashes, its place among the frame's records, the quad count and the offset of its words,
     the registers, the fetch constants, the state words. *The words*: four floats a quad
     (`x, y, z, s`, decoded from the half4 by the fetch's swizzle and endian), at most **4,096
     quads a frame** in one array of the scene (64 KB, allocated only in a frame that has
     impostors). The bounds are about twice the draws and four times the quads of the largest
     captured frame (14; 1,076).
   - *Rules of `MakeImpostorRecord`* (pure, under test; the first that fails names the reason,
     counted under `impostor-record`): `unread`; `shader`; `literal`; `storage` (the arguments
     and the device's fields); `quad` (a word that is not finite, a place beyond 1e6, a size
     outside (0, the bound D1 and D2 give times four]); `register` (not finite, or outside its
     bound: the dissolve's `c29.y` in (0, 4], `c29.x`, `c12.y` in [0, 4096], `c12.w` in [0, 4],
     `c31.x` in (0, 64], the matrix rows within 1e7, the others from D1 and D2 times four);
     `scale` (`c20.w` not in (0, 64]); `state` (anything but: depth test and write with the
     captured word under the reversed range, no blend, four colour channels, alpha test
     "greater" with a reference in [0, 1], alpha-to-mask with offsets `AA` on a two-sample
     surface); `fetch` (`tf0` is not a 2D texture of format 3, 256 x 256, tiled, endian 1, the
     captured swizzle, no gamma sign, highest level 3); `ground-fog` (bool `b129` set);
     `fog-table` (the vertex slots are not the frame's fog tables); `over-limit` (the 33rd draw,
     or a draw whose quads do not fit: that draw whole); `pending`.
   - **A draw is whole or not there; the frame's set is not all-or-none** (ruling 9): a refused
     draw is one tree model missing, which is today's picture for that model.
   - *Evidence a capture carries*: the frame row's `"impostors"`: `on`, `seen`, `taken`,
     `quads`, the reasons' counts, and for each record its place, hashes, registers and words
     as the floats' bits, the fetch constants and state words. `impostor_check.py records`
     holds each record to its `"verts"` and `"vertsdata"` rows bit for bit.
   - *Log*: `[native] impostors: off|flat|on` at install; every 300 frames
     `[native] capture: frame N impostors: seen S, taken T, quads Q | impostor-record in F of W
     frames {reason: n} | dropped D`.
7. **The switch** (ledger ruling 3; ruling 5). `--fable2_native_impostors` = `off` (default; also
   false, 0), `flat` (step C: capture, formulas and draw, one flat colour, no read-back asked
   for), `on` (also true, 1: the pictures; asks for the read-back at start-up). With `off`:
   nothing is read, no record, array, object, slot or program is made, the frame's slot plan is
   today's byte for byte, the read-back list is today's, and every compiled program of the pass
   equals today's.
8. **The native draw: one step with its own layout and programs** (ruling 11; the furniture's
   quad step is the pattern: FM "Scene furniture").
   - *Layout*: the model's constants at `b0` (the record's registers and state: one slot of the
     frame's constant buffer a draw, reserved only with the switch on), the frame's block at
     `b2` (the fog tables are in it: FM "Native fog"), the frame's quad words as a buffer bound
     by address, one texture (the picture), the four static samplers.
   - *Vertex program*: six vertices a quad by `SV_VertexID` (the quad's corners 0 1 2, 0 2 3),
     the quad's words read inside a branch that holds the quad's number below the draw's count,
     `impostor_position` in single precision in the reference's order, marked `precise`; the fog
     once a vertex from the raised place (the same value at a quad's corners, handed on without
     interpolation). The C++ mirror is pinned by `impostor_pins.py`; the program's text is held
     to the reference by value, as `tests/test_wind_hlsl.py` holds the wind's.
   - *Pixel program*: the picture's texel through the fetch's swizzle, `impostor_pixel`, the
     kept set of Design 10; written as the lit draws write the linear target
     (`(v > 0) ? min(v, 31.875) : 0` a channel, a texel that is no number reads 0), times the
     frame's output scale; the record's `c20.w` must equal that scale within a thousandth, as
     the moon's does, else the draw is not drawn (`scale`).
   - *State*: one depth-tested, depth-writing pipeline without blend and without cull; the
     depth word read under the reversed range as the other records' is.
   - *Not drawn, counted on the Impostors line and F3*: `pipeline`, `slot`, `buffer`, `scale`,
     `picture` (Design 12). An RHI failure turns the step off for the run, not the pass.
9. **Its place in the frame.**
   - *Colour pass*: the step is drawn at the records' place: before the first drawn record at
     or beyond the smallest `place` of the frame's impostor records, which is where the game
     issues them, before the terrain layers. It is opaque with a depth write, so its order
     among the opaque records changes no pixel; what the place guarantees is that it lies
     before the sky step, before every blend, before the coverage's tail and before the sun
     layer's depth is taken. A pure function gives the place, under test with
     `PlanCoverageOrder`'s arrangements.
   - *Pre-pass*: **struck (ruling 21)**. The impostors are drawn in the colour pass alone: no
     depth-only form of the programs, no depth-only pipeline, no reach test and no counter for
     it. The pre-pass's depth has one reader, the shadow mask, whose reach ends at 45 units,
     and no impostor pixel exists below 50. Cost if wrong (the ledger's): an impostor inside
     the mask's reach would not shade the mask. *(As designed before the ruling: a model's draw
     in the depth pre-pass under the pre-pass's own reach rule, by a depth-only form of the same
     programs, left out and counted when all its quads lie beyond the reach; ruling 12.)*
   - *Shadows*: the impostors cast none (no `129F` row in a shadow pass) and read no mask (the
     pixel formula has the mean term `c12.w`). No shadow-map draw.
   - *The sun layer's depth*: nothing is added for it. The layer's depth is the scene's, taken
     before the first late twin or after the last record (FM "The layer's depth place"); the
     impostors are in it by their place. This is the roadmap's "beams pass where the game's far
     woods stand".
   - *The sky step and the fog*: the sky is drawn behind them by its depth test; the pass's
     per-pixel fog is not applied to an impostor pixel (the impostor's own, per tree, is).
10. **Coverage, and the dissolve's cross with the real meshes** (ruling 13). The impostor is not
    a coverage record of sub-project 11 (`CoverageOf` asks for a family A draw record with a
    fade word): it is neither "late" (it is issued before the depth copy) nor "fading" (its
    scale is a tree's, in the shader). The rule itself applies as it stands:
    `covered = (alpha >= 0.25) + (alpha >= 0.75)` (`CoveredSamples` under `AA`), with `alpha`
    of Design 5.
    - *With `--fable2_native_coverage` off* (and always in `flat`): a pixel is kept iff the
      first sample is covered (`alpha >= 0.25`), drawn whole, with depth: today's rule for
      every alpha-to-mask draw. The native depth is then the game's first sample's, which is
      what its depth copy holds.
    - *With it on, and the pictures on*: the fully covered pixels (`covered` = 2) are drawn in
      place as above; the half-covered ones (`covered` = 1) in the early records' tail at one
      half over what lies behind, in two steps as sub-project 11's ledger ruling 60 has them: **every
      model's half depth first, then every model's half colour**, so that where two impostors'
      half parts meet the nearer blends once over the scene, as the game's two samples resolve.
      After the tail the depth is again the first sample's.
    - *The cross*: the mesh fades out as its impostor steps in (the twin's pixel `c15` is
      (48, 0.125, 24, w); that `w` runs from 1 at 48 units to 0 at 56 is **[inferred]** from the
      constants, one twin measured at 0.498). Mesh and impostor are drawn by their own rules
      and the depth test decides between them. **Known error**: where a fading mesh's half part
      and an impostor's half part meet (trees between 50 and 54 units), the tail blends twice
      where the game shows the nearer at half over the background (sub-project 11's limit of
      two records at one pixel, unchanged).
    - Built last of the draw's parts, after open question 8's figure is known.
11. **Step C: flat colour** (the switch at `flat`; the first thing drawn). The picture is a
    1 x 1 texture of one colour with alpha 1: the mean of the covered texels of D1's fourteen
    pictures, in the picture's own stored units, a literal with its source file named (ruling
    6). Everything else is the step as designed: the same programs, formulas, dissolve, light
    scalars, fog and depth. Whole quads: no tree shapes. It is what positions, depth, the
    counters and the debug layer are first judged on, and what the sub-project ends with if the
    read-back fails its bound.
12. **The pictures** (the switch at `on`; built only if run COST passes). *(Ruling 22)*: this
    part, with the half part of Design 10, is phase 12c, the last of the sub-project; its entry
    condition is run COST's verdict "passes", written in the ledger before its first task
    starts.
    - *The read-back*: `ForceReadbackList` (`src/core/readback_force_list.h`) gains a third
      argument and, with it, the fourteen bases `0x12704000 + n * 0x2B000` (the first is the
      hero's entry: not listed twice), with static asserts that no window reaches a
      neighbour's (the spacing is 172 KiB, the window 64) and that no level address
      (base + `0x20000`) lies in a window. Seeded once in `OnPostSetup`, under the conditions
      the exposure's switch has (a list given by hand wins). **The list is not rewritten while
      the game runs** (ruling 7): the SDK reads the string at every resolve on its own thread.
      No SDK change.
    - *Which pictures can be had*: a record whose `tf0` base is one of the seeded fourteen, in
      a run in which the list was seeded (or `--readback_resolve` is `full`). Any other:
      `picture: base`, not drawn.
    - *The decoder* (`Decode1555Level` in `texture_decode.h`, pure, beside `DecodeDxt3aLevel`;
      **not** entered in `TexFormatFromXenos`, so that no record's material changes: ruling
      14): 16-bit texels at `TiledOffset2D(x, y, pitch, 1)`, the 8-in-16 swap, then alpha in
      bit 15 and three 5-bit fields whose order is the SDK loader's (its table presents the
      format as `B5G5R5A1` after exchanging red and blue in the load shader:
      `d3d12/texture_cache.cpp` lines 96 to 100; the plan's first step reads that shader and
      fixes the order with a test on hand-made texels), each field `v * 255 / 31` rounded, to
      RGBA8. False, with nothing written, for a source too small. The upper halves that exist
      today (`DT/impostor_sheet_*`) are decoded by the same function in its test's tool before
      D1.
    - *The levels* (ruling 8): levels 1 to 3 are made natively from the decoded base by a pure
      function (`MakeImpostorLevels`): a 2 x 2 mean, the colour kept in 5 bits and the alpha
      thresholded to one bit, the rule fitted to D1's dumped levels before it is fixed
      (criterion 5 says how well it fits). The game's own levels are not read back: that would
      be 42 more forced resolves a frame at addresses the SDK's comment warns of.
    - *The texture set*: up to 32 host textures of 256 x 256 `R8G8B8A8_UNORM` with four levels,
      owned by the pass, outside the texture cache (whose dynamic rule would refuse them). Each
      frame, for each record that can have a picture: a bounded physical read of the base
      level, `SampleHash` of it, and only if it changed: decode, levels, upload (349 KB a
      picture; at most fourteen a frame captured). The sampler is the one `SamplerIndex` names
      for the fetch.
    - *Absent or stale* (ruling 7): a picture is used only if its bytes were read whole,
      decoded, and pass the content rule: of the base level's outermost ring of 1,020 texels at
      least a share **to be fixed from D1 and D2** has alpha 0, and the covered share of the
      level lies inside bounds from the same captures (noise has neither property; an empty
      picture has no covered texel). **A model without a usable picture is not drawn**
      (`picture: unread`, `content`, `base`, `upload`): never a flat quad in its place, which
      would write depth and colour where the game has neither. A picture that is whole but old
      cannot be told from a current one: nothing in the product sees the game's resolve.
      Criterion 6 measures the age in a capture; a limit otherwise.
    - *The cost*: the read-back's is run COST's; the decode and upload are measured as `on`
      against `flat` (criterion 15). If that part alone breaks the bound, the first remedy is a
      budget of pictures refreshed a frame (ruling 17), not the fallback.
13. **The fallback, B: not designed here.** If run COST fails, or when the presentation work
    removes the emulated GPU: the fourteen groups of rendering draws captured as records of
    their own and drawn by the native tree pipelines into fourteen native targets with levels
    (R 6.1, P6b). It is a later sub-project. What this one hands it: the capture, the formulas
    and the draw unchanged; the decoder as the check of its pictures against the game's; the
    rendering rows' viewport and scissor in D1; the knowledge that half of the models have no
    scene draw.
14. **Validation**: the criteria below. Every "before" figure is of an off-run of the same
    session and binary as its "after" (ruling 16); the research's figures are of a binary
    before wind and coverage and are the expectation, not the baseline. The runs the criteria
    name, all `-BootSave Hero000`, each with the camera standing before its capture or shots:

    | Run | What |
    |---|---|
    | COST | Design 3: four runs, today's list and fourteen addresses, no new code |
    | D1 to D4 | Design 2: discovery captures, the switch off |
    | V-BRIDGE, V-THIRD | the view of D1 / D2 under `--readback_resolve=full` with the scene dump on and a capture: the switch `off`, `flat`, `on`, and `on` with the coverage and the wind; at the bridge once more with the sun's switch on, impostors off and on |
    | V-BAND, V-SUN | the views of D3 / D4, the same way, off and on |
    | WARP | the software adapter under the debug layer, 240 s: `flat`; `on` with the coverage (phase 12c). *(Ruling 21)*: the run in D3's view is struck with the depth-only form it was for |
    | OFF | every switch at its default, the plan of log 657 |
    | TIME | criterion 15's six bridge runs and the third spot's pair, no read-back but the switch's own |

## Fail-closed rules (collected)

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

## Rulings

Rulings 1 to 4 are the controller's (ledger): 1 the scope and option A with step C first, B the
fallback; 2 water and shadow correctness stay out; 3 the switch, default off; 4 game launches
wait for the user's leave. From 5 on they are this design's, each on the recommended option.

- Ruling 5: the switch has three values, `off`, `flat` and `on` - why: step C needs no
  read-back and the read-back is asked for at start-up, so the value that draws without it has
  to be known then; and the two costs (the draw; the read-back and the pictures) can be measured
  apart - cost if wrong: a value nobody uses outside development; it is also what is left if run
  COST fails.
- Ruling 6: flat mode has one colour for every model, the mean of D1's covered texels - why: a
  model has no stable identity (ten or fourteen draws by the view, pool addresses) - cost if
  wrong: flat mode's regions are off by the models' spread; it is a step, not a picture.
- Ruling 7: the read-back list is seeded once from the fourteen measured bases and never
  rewritten at run time; a record whose picture base is not in it, or whose bytes fail the
  content rule, is not drawn - why: the SDK parses the string at every resolve on another
  thread, and a whole quad in a missing picture's place puts depth and colour where the game
  has none - cost if wrong: in a region whose pool lies elsewhere or holds more than fourteen
  models, those models are missing (counted), as today.
- Ruling 8: the three lower levels are made natively from the base, not read back - why: 42
  more forced resolves a frame, at addresses beside the dog's texture - cost if wrong: far
  trees, which take levels 2 and 3, have a cover that differs from the game's by the level
  rule's misfit (criterion 5 measures it; criteria 3 and 8 carry it).
- Ruling 9: impostor records are a side block of at most 32 draws and 4,096 quads; a draw is
  whole or missing; the frame's set is not all-or-none - why: a missing model is today's
  picture for that model, where a furniture set had to be whole for the layer - cost if wrong:
  one model flickers for a frame when its storage arrives late (counted as `pending`).
- Ruling 10: the `unsupported by hook` line and the draw-record path are not changed; the
  impostors have a line of their own - why: with the switch off the logs must be today's, and
  no draw record is made for these calls in any case - cost if wrong: R 6.4's criterion 6
  (`BeginVertices?: 2`) is replaced by `seen` and `taken` against the capture's rows.
- Ruling 11: one step with its own layout and programs, the quads' words in a buffer bound by
  address, the fog evaluated in the vertex program from the frame's tables - why: the clay
  layout is full (12 of 12), the furniture's step is the proven pattern, and the game evaluates
  this fog per vertex - cost if wrong: a second place that reads the fog tables, held to the
  first by the same mirror's test.
- Ruling 12: the impostors are in the pre-pass under its reach rule, and are expected to be
  left out by it in nearly every frame - why: ledger ruling 1 names the pre-pass, and the rule
  already exists; the mask's value beyond 45 units does not depend on depth - cost if wrong: a
  depth-only pipeline that seldom runs (D3's view and a software-adapter run there are its
  executions). Whether to drop it is put to the controller below. **Struck by ruling 21.**
- Ruling 13: with the coverage's switch off an impostor pixel is kept iff the game's first
  sample is covered, drawn whole; with it on, the half-covered pixels are drawn at half in the
  early tail, every model's depth step before any colour step - why: a minified one-bit picture
  has many pixels between the two thresholds, the far woods overlap each other, and the first
  sample's depth is the game's depth copy in both forms - cost if wrong: three pipelines for a
  share that may be small; open question 8's figure comes before they are built, and under 5%
  of the impostors' pixels the controller may strike the half part.
- Ruling 14: the format-3 decoder is a function of its own, not a format of the texture cache -
  why: a format added to `TexFormatFromXenos` would texture records that are `format-unsupported`
  today, with the switch off - cost if wrong: none seen; a later sub-project may enter it.
- Ruling 15: the read-back's bound is "no steady native window below 30.0 fps, and at most 2.0
  ms more mean frame than today's list, in a session whose two runs of today's list agree
  within 1.0 ms" - why: uncapped, the 30 fps bound alone no longer says "no cost" (FM "After the
  fix wave"); 2.0 ms is a tenth of the bridge's mean frame, and less than the least that one
  more forced resolve cost in the one earlier measurement (2.4 ms), so the bound can fail -
  cost if wrong: a threshold without a precedent; the
  figures are reported whatever they are, and "not decidable" twice goes to the controller.
- Ruling 16: every before-and-after figure pairs an off-run and an on-run of one session, one
  binary and one plan; a threshold without a measured precedent is marked, the figure reached
  is reported whatever it is, and a verdict is "met", "not met as written", "not decidable" or
  "by a substituted measure" - why: sub-project 11's closing table - cost if wrong: more runs.
- Ruling 17: every picture whose bytes changed is refreshed every frame; if the decode and
  upload alone break criterion 15, a budget of four a frame comes first - why: a far tree's
  sway a few frames late cannot be seen - cost if wrong: a picture up to four frames old.
- Ruling 18: a position formula that does not close on every captured quad draws nothing - why:
  an approximate billboard would be judged by eye alone and would put wrong depth into the
  layer - cost if wrong: the sub-project ends at the capture and the formula's report.
- Ruling 19: the replay judges cover with the picture's alpha at the level each quad's fetch
  takes, from the capture's dumped levels - why: sub-project 11's criterion on a base level had
  to be judged by a substitute - cost if wrong: none; the base-level figure is printed beside.
- Ruling 20: a draw with bool `b129` set is refused - why: the ground-fog block is 42
  instructions no capture exercises - cost if wrong: in a place with ground fog the far woods
  are missing, counted.
- Ruling 21 (the controller's; ledger): the pre-pass draw of the impostors is struck - why: the
  shadow mask's reach ends at 45 units and no impostor pixel exists below 50, so it has no
  reader; one pipeline and one source of mismatch fewer - cost if wrong: an impostor inside the
  mask's reach would not shade the mask; the dissolve says none exists. It replaces ruling 12.
- Ruling 22 (the controller's; ledger): the plan is written for the shared work first
  (discovery, formulas, decoder, capture, the flat step and its validation) with the pictures
  by read-back as a last phase that starts only if the COST pair passes its bound; if COST
  fails or is twice not decidable, the sub-project closes at `flat` plus the capture and the
  formulas, and native pictures (option B) become the next roadmap item; no SDK change and no
  `--readback_resolve=fast` by this sub-project - why: the prior says the pair may well fail
  (2.4 to 13 ms a forced resolve seen before), and the shared work is the same either way -
  cost if wrong: the far woods stay flat mean-coloured quads for a sub-project; that still
  closes the depth and the brightness gap.

## Success criteria

Each with its reference, checked in the evidence named, and the run that decides it. Evidence in
`out/native-evidence/distant-trees/`. Regions are those of `docs/native-renderer/
fog-regions.json`. *(no precedent)* marks a threshold set here without a measured one before it.
"Judged pixels" follow sub-project 11's criterion 5: one-sided on visible pixels, at least 1,000
of them or "not decidable".

1. *Formulas.* `impostor_check.py formulas` and `pixel`: 2,000 of 2,000 made inputs within the
   bounds of Designs 4 and 5; every mutation listed there fails. `rows`: every quad and corner of
   every `129F` row. **Reference**: the microcode dumps (exist) and `ucode_eval.py`.
   **Decided by**: no run for the made inputs; D1 and D2 for the rows.
2. *Records.* In every frame of a capture at the bridge and the third spot with the switch on:
   each record equals its `"verts"` and `"vertsdata"` rows bit for bit (`impostor_check.py
   records`); `seen` = `taken` = the frame's `129F` rows (14 and 14 expected), `quads` = their
   quads (928; 1,076); no reason occurs. `sizeof(DrawRecord)` is 616 (static assert).
   **Reference**: the row counts of R 1.1 (`DT/census_*.txt`, read). **Decided by**: runs
   V-BRIDGE and V-THIRD.
3. *Positions and cover against the game's depth, by the rasterising replay.* The quads of D1
   and D2 through `impostor_position`, rasterised by `foliage_replay.py`'s rasteriser at the
   sampling place fitted on the capture's own unmoving draws, kept by the first sample's rule on
   the picture's alpha at the level a fetch takes (ruling 19): at least 95% of the judged pixels
   hold the game's depth copy within 16 steps of 2^-24. Controls that must show the measure can
   fail: with the size scaled by 1.1, and with the raise left out, the share falls by at least
   10 points *(no precedent)*. **Reference**: the depth copies of D1 and D2 (new; the same kind
   as CAP `182313`'s, which `DT/depth_gap.py` reads). **Decided by**: D1, D2; no native run.
4. *The decoder.* `Decode1555Level` on hand-made texels (alpha's bit, each field's place, the
   swap, the tiled order, a short source) in the native tests; and on D1's fourteen dumps equal,
   texel for texel, to a Python transcription of the SDK loader's rule. **Reference**: the SDK's
   loader (`d3d12/texture_cache.cpp` lines 96 to 100 and its load shader), read. **Decided
   by**: tests; D1.
5. *The levels (no precedent).* `MakeImpostorLevels` of each dumped base against the dumped
   levels 1, 2, 3 of D1 and D2: the alpha bit equal on at least 97% of the texels of each level
   and each colour field within 2 of 31 on at least 95% of the covered ones. Not met: the rule's
   best fit is reported and criteria 3 and 8 carry the consequence. **Decided by**: D1, D2.
6. *The pictures the pass used.* With the scene dump on, the pass writes the pictures of the
   paired frame beside the stored scene; each equals, on at least 98% of its texels *(no
   precedent)*, the capture's dump of the same base in that frame or the one before; which is
   reported. **Decided by**: V-BRIDGE.
7. *The depth gap, the pass against the game.* `DT/depth_gap.py` on the on-run's pair: the far
   pixels (80 units or more) where the game is nearer than the native depth by more than 3% or
   the native picture has sky are at most **a tenth** of the off-run's count of the same
   session, and the far pixels where the native depth is nearer by more than 3% rise by no more
   than that tenth *(no precedent; a quarter pixel between the two pictures' sampling places is
   expected to hold about half of it)*. **Reference**: 48,051 pixels on CAP `182313` (read); the
   baseline is the off-run's. **Decided by**: V-BRIDGE, V-THIRD, in `flat` and in `on`.
8. *The far regions, linear scene before post.* With the pictures, the coverage and the wind on,
   native over game, mean linear luminance: far mountains left, centre, right and far rock
   below the ridge each within **0.93 to 1.07** (expected before: 1.336, 1.151, 1.117, 1.108),
   and each nearer to 1 than half its off-run distance; on the off-run's gap pixels within
   **0.92 to 1.08** (before: 1.433), with red, green and blue each inside that band *(no
   precedent)*. Why these: on a pixel both pictures cover with a tree the colour is the game's
   own texel through closed scalars; what is left is miscover, and 0.08 is a fifth of the tree
   pixels miscovered one way at the measured contrast of 1.43; trees are 45 to 83% of the four
   regions' pixels, which gives the regions' 0.07. With the coverage off the figure is reported
   beside open question 8's share, not bound. In `flat`: reported. **Reference**:
   `DT/depth_gap_182313.txt` (read, the figures above are its lines). **Decided by**: V-BRIDGE;
   V-THIRD for the third spot's two regions, whose "before" is that run's own.
9. *The shared far ground is unchanged.* In the same pairs the regions' pixels at the same
   distance in both pictures, and "far peak", "island in the lake" and "sky above the ridge",
   stay within 0.99 to 1.01 and move by at most 0.005 from the off-run. **Reference**: 0.993 to
   1.002, 0.995, 0.997, 0.995 (read). **Decided by**: V-BRIDGE.
10. *After post, on shots.* `region_compare.py`, native over the mean of the two emulated shots
    about it: the four bridge regions and the third spot's two within 0.90 to 1.12 *(no
    precedent)*. A region whose emulated pair drifts by more than 5% is "not decidable" on
    shots (the bridge's pair drifted by 6 to 10% in log 654) and stands on criterion 8.
    **Reference**: `DT/regions_k18b_time_on_bridge.txt`, `DT/regions_f4_third.txt` (read).
    **Decided by**: the TIME runs' shots.
11. *The dissolve band.* In D3: the replay of the quads whose `d` lies in 46 to 58 units, by
    the first sample's rule, against the game's depth copy: at least 95% of the judged pixels;
    and no judged pixel of a quad below 50 units. Then in run V-BAND, the pass against the game
    on the pixels between 40 and 80 units: criterion 7's measure, and native over game on the
    band's pixels reported with the coverage on and off, the known error of Design 10 named.
    Fewer than 1,000 judged pixels, or no such view: "not decidable", user check.
    **Reference**: none in hand (R: "which view has such trees is not known"); the view is
    chosen from D1's and D2's tree places. **Decided by**: D3, V-BAND.
12. *The sun layer against the far woods.* First part, at the bridge with the sun's switch on:
    of the layer's texels that lie on the off-run's gap pixels and are closed in the game's
    layer depth, the stored native layer depth is closed on at least 90% with the impostors on
    and on at most 10% with them off *(no precedent)*; at least 500 such texels or "not
    decidable". Second part, in D4's view: native over game in a region of beams across far
    woods, on against off, **reported, not promised** (the layer's sweeping shaft is open:
    roadmap). **Reference**: for the first part the layer planes of a bridge capture and the
    stored scene's layer depth (both exist as kinds: FM "Stored native scene" for the native
    plane, `post_sun_layer.py layers` for the game's layer 1); no frame in hand has been read
    this way, so the 500 texels are not known to exist; for the second none in hand (R 5.3). **Decided by**: V-BRIDGE with the sun on; D4 and V-SUN.
13. *Off is today's picture.* With the switch off: every compiled program of the pass equals
    the program of the commit before the first product commit, byte for byte (shader test); the
    slot plan and the read-back list are today's for every input (tests; the log's `seeded
    readback_resolve_force_addresses` line as log 657's); no line of the impostors beside
    `[native] impostors: off`, the frame row says `"impostors":"off"`; and a defaults run
    against log 657 (`k18b_defaults`, the same plan, 1942 wide) in the bridge's 24 regions:
    each within the emulated pair's range, the two regions of moving leaf shadow that
    sub-project 11's closing named left out beforehand. **Decided by**: tests; run OFF.
14. *Errors.* Before any real-GPU run of a build that changes bindings, targets or shader text:
    240 s on the software adapter under the debug layer at the bridge, 0 `d3d ERROR` lines,
    warnings listed by id; **and the log shows the step drew** (`Impostors: ... drawn 14`) and
    the shot shows quads or trees on the ranges (viewed), so that the programs shaded pixels.
    One such run for `flat`, one for `on` with the coverage on (phase 12c: ruling 22). *(Ruling
    21)*: the third run, in D3's view for the depth-only form, is struck with that form. The
    software adapter's pictures are not judged (the read-back texture at
    `0x12704000` is the flat red dog there). **Decided by**: runs WARP.
15. *Cost.* Shot width 1942, each run made alone, the head's uncapped default stated in the
    report, one session, the bridge: `off`, `flat`, `on` (with the coverage and the wind on in
    all three) each twice in mirrored order. No steady 150-frame native window below 30.0 fps
    in any; the mean frame over steady native windows: `flat` over `off` at most 0.5 ms, `on`
    over `off` at most 2.5 ms *(no precedent)*, each only if the two runs of a configuration
    agree within 1.0 ms, else "not decidable". The capture median stated beside the off-runs'
    (a difference under a third of a millisecond is "not decidable": sub-project 10's ruling
    42). The third spot: one pair, reported. **Reference**: logs 654 to 657 (FM "After the fix
    wave": 37.4 fps the lowest window, mean frame 19.36 to 21.66 ms). **Decided by**: runs TIME.
16. *The emulated picture under the read-back.* The emulated shots of the runs with fourteen
    addresses against those with one: the hero's shirt, the dog and the four far regions within
    the range the two runs of one configuration span. **Decided by**: run COST.
17. *Evidence rule.* Every number names its file; inference is marked; a run made with a user
    at the machine is labelled `RUN UNCHECKED`; a criterion not met as written is said to be
    not met.

User checks (from 93, in `docs/native-renderer/user-checks.md`): the ridges' tree line, native
against emulated; no popping where a tree crosses 48 to 56 units on a walk; beams against far
woods; the far woods in another region and at another time of day.

## Limits known beforehand

- **The pictures are the emulated GPU's**: rendered by the emulator, resolved by it into format
  3 (whether those are the console's bits is not known), read back one or two frames old. The
  native view does not leave the emulated GPU by this work.
- **Half of the tree models have no scene draw**: nothing native can make their pictures without
  the fallback.
- **One save, late morning, three spots.** Times of day, other regions, towns and interiors are
  unseen: whether impostors exist there, how many models, at which addresses, with which fetch
  shape. Outside the measured shape a draw is not drawn and counted.
- **`43ED` / `7F65` and `7D1C` / `983E`** stay unidentified and undrawn; so do the sprites and
  particles, the far lake's cube and the two mask rectangles.
- **The ground-fog block** (`b129`) is not modelled.
- **Five-bit colour and one-bit alpha** are the game's; the native levels are a fitted rule.
- **Half-covered pixels**: with the coverage off they are whole; with it on, a fading mesh's
  and an impostor's half parts at one pixel blend twice (Design 10).
- **A whole but old picture cannot be told from a current one.**
- **The fetch's border** (clamp mode 6) is drawn with the static samplers' clamp to the edge.
- **The native picture is the game's sampled a quarter pixel away** (roadmap 5), as for every
  record.
- **The pool is shared** with the dog's texture; what else the game puts at the fourteen
  addresses in other scenes is not known.

## What goes back to the roadmap

Item 2 closes as far as the criteria say; "the cubes at depth 20.0024" leaves it (identified, R
4: the lake's level in the sun layer's depth; nothing to build while the lake's meshes are drawn
with depth). New or kept: the pictures rendered natively (B), with roadmap 6; the two
unidentified pairs; the ground-fog block; the far woods at night and in other regions; the
levels read from the game if a cheap read-back ever exists; the red dog on the software adapter
now has a named neighbour (the picture resolved every frame at its address), not a cause.

## Risks

1. Run COST fails (the prior says one more forced resolve cost milliseconds): the sub-project
   then delivers flat quads, which the research calls "not a picture anyone would turn on".
2. The `log` / `exp` block does not close, or closes only with a register no capture bounds.
3. The storage is recycled before it is read (the quads' pattern has not failed; up to 2.3 KB
   read a draw is more than a quad's 80 bytes): `quad` refusals would show it, values that
   are plausible and wrong only criterion 7.
4. Thirteen more forced addresses disturb the emulated picture (the SDK's comment): criterion 16.
5. The content rule lets a noise picture through or refuses a real one: its thresholds wait
   for D1 and D2.
6. D3's and D4's views do not exist from a standing camera: criteria 11 and 12 read "not
   decidable".
7. Every run is likely `RUN UNCHECKED`; timing needs the usual monitor and a quiet machine.

## Decisions that are not this design's

- **The user's leave for game launches** (ledger ruling 4): everything from run COST on waits.
- **If run COST fails or is twice not decidable**: the fallback as a later sub-project (ledger
  ruling 1), or a change in the SDK (a forced read-back that does not wait, or one address in N
  frames), or running the game under `--readback_resolve=fast`: the first is ruled; the other
  two change the SDK or how the game is run, and are the controller's and the user's.
  **Decided (ruling 22)**: the sub-project closes at `flat`; option B is the next roadmap item;
  the other two are not tried by this sub-project.
- **The pre-pass draw** (ruling 12): ledger ruling 1 names it; by the mask's reach it has no
  reader in nearly every frame. Kept as ruled; the controller may strike it. **Decided (ruling
  21)**: struck.
- **The half part under the coverage's switch** (ruling 13) if open question 8's share is small.
- **The default of `--fable2_native_impostors`**, and that the value `on` adds thirteen entries
  to a start-up list of the SDK: the user's, after validation; off until then.
- **What is pushed.**

## What the research has differently

Found while checking it for this design; none changes its conclusion.

1. "Through alpha-to-mask that is the two-sample dither of sub-project 11b" (R 3.2): under `AA`
   there is no parity term; the impostor steps in whole, at 50 and at 54 units.
2. "`o0` = the tree's place" and the fog "at the tree's place" (R 3.1, 3.2): the microcode
   raises the third coordinate by `s * c8.z` first. Read, not run.
3. The fog is called "the trees' block" (R 3.1): it has no `c8.x` factor, and it begins at
   instruction 70, not 75.
4. "Cost unknown" (R 6.1, P6a): FM "Exposure read-back" has a measurement of one more forced
   resolve a frame, and a normal run already forces impostor 0's: "against not" is against one.
5. The address `0x12704000` (R 1.2): the earlier plan for the dog's texture had already seen
   the every-frame 256 x 256 format-3 texture there without knowing what it was, and the SDK
   warns about forcing its neighbours.
6. "Must write the same depth in both passes" (R 6.1, P5): the colour pass clears the depth
   after the mask; and the mask's reach ends before the first impostor pixel.
7. R 6.4's criterion 6 (`unsupported by hook {BeginVertices?: 2}`) would need the draw-record
   path changed; ruling 10 keeps it.
8. R 6.4's criteria 3 and 4 take their "before" from a binary before wind and coverage (R says
   so under "Not checked"); the baseline here is each session's own off-run.
9. "Which half is filled is unknown" (R 6.1): what the shader reads is fixed by its fetch (two
   dwords a quad from the fetch constant's base); what the game writes beyond it does not
   matter. Still to be seen in D1.
