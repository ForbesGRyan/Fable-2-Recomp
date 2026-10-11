# Native renderer sub-project 11: foliage (wind, part coverage, the tree occluder)

Status: **phase 11a (wind) built and validated by stills and by the native depth, 2026-10-11, with two parts left open by ledger ruling 49:
criterion 9's cost (no timing evidence yet) and criterion 8's fourth part (no pair of runs of the two commits at one width)** (verdicts with
each criterion and in FM section 18, "Validation (11a)"; the switch stays off by default); **phase 11b (coverage) built and validated on stills, 2026-10-11, with its cost left open: criteria 10 and 11 met, 12 not met as written and met in
direction (ledger ruling 56), 13 met by a substituted baseline, 14 met but for the cost; the fern is not coverage (ruling 57); a late tree's order
against blended records is a limit with a user check (ruling 55) (FM section 18, "Validation (11b)", "Limits (11b)"; plan Tasks 12 to 15)**; phases 11b and 11c: design, 2026-10-10 (branch `native-renderer-foliage`). Plan:
`docs/superpowers/plans/2026-10-10-native-renderer-foliage.md`. Research:
`.superpowers/sdd/foliage-research.md` (untracked; its scripts and outputs in
`out/native-evidence/foliage/research/`). Ledger:
`.superpowers/sdd/2026-10-10-native-renderer-foliage/progress.md` (rulings 1 to 6 are the
controller's and are not reopened here). Three phases on one branch (ruling 3): 11a wind, 11b
coverage, 11c the tree occluder's redraw and the trace of the darkness about the sun; each ends in a
working picture, its own validation and its own review.

Every fact below carries the research's label, or names the code it was read from:
**[measured]** printed by a named script from a capture, a shot or a stream dump; **[replay]**
measured by running the game's own vertex microcode on a capture row's real streams and constant
bank (a measurement of the microcode, not of a game picture); **[microcode]** read from a shader
dump; **[code]** read from the repository's source; **[model]** computed on the game's own scene
with stated assumptions; **[inferred]**; **[unknown]**. "FM" is
`docs/native-renderer/frame-map.md`; "R" with a section is the research.

## Goal

- **11a, wind.** Trunks, branches, leaf clumps, bushes and grass move in the native view as the
  game moves them: the game's own vertex-shader sway, from the game's own per-draw constants, in the
  colour pass, the depth pre-pass and the two shadow maps alike. Behind `--fable2_native_wind`.
- **11b, coverage.** Foliage the game shows part-covered (alpha-to-mask on its two-sample scene
  target while a tree fades) is drawn part-covered: a pixel of which the game covers one sample of
  two shows the foliage at half over what lies behind it, and the native depth stays the game's
  first sample's. First the tree the game fades between the eye and the hero (the "late" draws),
  then the trees that dissolve by distance. Behind `--fable2_native_coverage`.
- **11c, the tree occluder and the darkness.** The sun layer's tree-occluder redraw (`F1EB`) is
  drawn into the layer's depth, under `--fable2_native_sun`. The darkness of the native scene about
  the sun that neither coverage nor the glare explains (a factor of about 0.66, R B7) is traced on a
  stored native scene beside the game's of the same frame, and mended only where the cause is a
  fault of an existing native formula (ruling 5). Then sub-project 10's criterion 19 is judged
  again in the game.

**Non-goals** (each a roadmap item of its own): distant trees and the cubes at depth 20.0024
(roadmap 2); water, the far lake's cube and the reflection views' trees (`475E` / `A584` at pitch
320: roadmap 3); MSAA and supersampling, and with them the scene's half-pixel origin and real
alpha-to-coverage (roadmap 5); **a native clock** (the wind runs on the clock the game puts into
its own constants, per draw and per frame: a frame without those constants stands still); the
plants' push away from characters (`c[56+a0]..c[59+a0]`); the billboards `29B6`, the draw
`7D1C` / `983E`, the fur shells `7C57`; the dissolve band's far end (FM 1902); light through
leaves or any other missing light term (ruling 5); wind shaders of towns and interiors, which no
capture holds.

## What is known (research; evidence there)

**Wind.**

- **Nothing is computed natively today [code].** A table entry's `"deformed": true`
  (`docs/native-renderer/vs-transforms.json`) becomes `DrawRecord::deformed`
  (`src/native/capture/draw_record.h` line 90) and the record is drawn at rest: the geometry cache
  decodes the fetched position and the clay vertex shader multiplies it by the record's `c0..c3`.
  For the leaves the rest position is the fetched `P`, not the game's rebuilt
  `Q + |P - Q| * direction` (FM 963).
- **Twelve vertex shaders sway, in three families [measured, R A1]**: trunks and branches
  `475EC9F795E5EDBB`; leaf clumps `A5846836C90E1192`, `D7DD31B9BF194DE8`, `2D40B53C926109BE` (the
  last in none of the three captures); instanced plants `8123C16DBF583F92`, `B636821F95DC9D8E`,
  `48D30ECCF684F488`, `FC4F2EF6930768BE`, `33C00226C154C1F2`; the shadow casters
  `FBD39C64463E180B` (trunks), `432563420047C96C` (leaves; also the sun layer's tree occluder),
  `A8A5B2A89F70336A` (plants). Wind draws are 132 of 208 main-scene draw rows towards the sun, 178
  of 390 at the bridge, 228 of 456 at the third spot. A town or an interior was not captured:
  **[unknown]** there.
- **What the position depends on [replay, R A1]**: trunks: `c15`, `c16`, `c17`, the fetched
  position and all four bytes of fetch 4 (an 8_8_8_8 at dword 7). Leaves: `c15`, `c16`, `c17`, the
  position, the pivot of fetch 3 (a half4 at dword 5, three coordinates), the four bytes of fetch 4
  and two bytes of fetch 5 (an 8_8_8_8 at dword 8). Plants: `c7`, `c9`, `c13`, `c12` (as today)
  and `c10`, `c21`, `c22`, `c25`, `c26`, with the three instance rows and the mesh position:
  nothing the native flat stream is not already built from. Normals, UV, probe weights and the
  vertex textures are not on the position path.
- **The plants' formula is closed [microcode, checked by replay in direction and period, not
  term by term]**: with `p` the copy-transformed world position, `z` the mesh vertex's third
  coordinate, `A3`, `B3` the fourth stored components of the copy's first two rows:
  `f = frac(c7.x + c7.y + A3 + B3)`; `theta = 1 + f - c10.x * c25.x * (1 + 0.1 f)` wrapped to
  [-pi, pi); `h = saturate(5 z)` (`c255.w` in `8123`); `position = p + h * cos(theta) *
  (c26.x * c21.xyz + c26.y * c22.xyz)`; the distance cut tests `p` before the sway. **The trunks'
  and the leaves' closed formulas are [unknown]**: 42 and 77 instructions read in structure only
  (R A1); the plan produces them with a machine check before any shader text.
- **The casters and the occluder use the scene shaders' wind bit for bit [replay]**: a caster's
  microcode on a scene draw's streams and bank gives the scene shader's `oPos` with a largest
  difference of 0.000e+00 over 80 vertices for each of `FBD3` on `475E`, `4325` on `A584`, `A8A5`
  on `8123` and on `B636` (the scene `oPos` itself moves by 0.13 to 0.38 in 1.5 s, so the test can
  fail); `4325` with the layer row's own bank equals its twin's `A584` position in 8 of 8.
- **One clock, in per-draw constants [measured, R A2]**: trees `c15.x` (the clock plus a phase per
  tree instance; a trunk and its leaves share one), `c17.w` = twice the clock, `c17.xy` the wind's
  direction in the tree's own frame **[inferred]**; plants `c10.x`, with `c21` the wind's
  direction in the world. Over eight consecutive frames the clock reads 57.9933 to 63.3184, the
  same values in the scene rows, the caster rows and the occluder rows of one frame. That it is
  seconds of game time is **[inferred]** (the capture slows the game). Whether the direction and
  strength gust is **[unknown]**.
- **Size [replay]**: a slow sway. Over a sweep of the clock, peak to peak 3 to 12 pixels of
  1120 x 720 for the most displaced tree vertices and 7 to 18 for plant tips, 1 to 7 and about 2
  for median ones; periods 7.06 s and 6.67 s (a beat) for trees, 5.7 to 6.3 s for plants; 0.0 to
  0.3 pixels per 1/30 s. The largest world offset seen: 0.46 units (`D7DD`, third spot); the
  plants' is bounded by `|c26.x c21 + c26.y c22|` = 0.108.
- **None of the wind constants is in any record [code]**; the mechanism that reads register runs
  beside the record exists (`LightRegisterRuns`, `src/native/capture/material.h`).
- **The game's shadows sway [replay + measured]** and the caster meshes are other vertex buffers
  than the scene's.

**Coverage.**

- **The rule [code, FM 1602, 1878 to 1881]**: on the two-sample scene surface a sample is covered
  iff `alpha >= base - offset / 8`, base 0.5 for the first sample and 1 for the second, the offset
  by the pixel's parity. Under offsets `87` the thresholds are (0.125, 0.375, 0.5, 0.25) and
  (0.625, 0.875, 1.0, 0.75); under `AA` 0.25 and 0.75 at every pixel. Family A (leaf `7CD5`, trunk
  and branch `00E0`, `58D3`, `2FBE`): `alpha` = texture alpha x pixel `c15.w` x `c26.w`. Family B
  (plants): the texture's alpha after an alpha test at 128 / 255: no dither.
- **Census, frame 1 [measured, R B6]**: 110 / 171 / 213 alpha-to-mask draws a frame. Family A
  under `87` with a fade word below 1: 36 / 13 / 21 draws (values 0 to 0.94; a trunk and its
  leaves share one). Family A under `AA` after the game's depth copy, fade word exactly 0.5:
  1 / 3 / 3 draws (18,722 indices towards the sun). Every alpha-to-mask draw writes depth, and all
  are issued before the depth copy except those late draws. Pixels a frame: **[unknown]** (a
  capture row holds 512 of a draw's indices).
- **The native rule today [code]**: a pixel is kept iff the game's first sample would be covered
  (`AlphaToMaskCovers`, `clay_logic.h` line 1373), drawn whole, with depth; the depth-only
  programs make the same discards.
- **How much of the darkness it is [model, R B7]**: on the game's own scene, part coverage
  explains at most a factor 0.75 at "sun" (the late tree alone 0.94), 0.57 at the beams' root (the
  late tree), 0.89 and 0.83 at the two beam regions. The figure it was thought to explain, "0.30
  of the game's at the sun", compared a native picture without glare with a game scene that has
  its glare (x 1.49): like for like about 0.50 **[code + measured]**. **About 0.66 at "sun" is
  explained by neither [model]**; the candidates not excluded are the leaves' own light against
  the sun and shadowed wood (R B7's table).

**The tree occluder (`F1EB`).**

- **It is the leaf shadow caster's pair drawn depth-only at the layer's target [measured, R
  C9]**: `4325` / `F1EB`, colour mask 0, no multisampling, `0x2202` = `0xAA00001C`; one a frame
  towards the sun, three at the other spots; each has exactly one main-scene twin with its index
  buffer, range, vertex buffer, `c0..c3`, wind constants and pixel `c15` (fade 0.5). Without
  multisampling the threshold is 0.5 on an alpha of at most 0.5: kept only where the filtered
  texture alpha is exactly 1.
- **It could not be replayed** because no capture holds a draw's whole index buffer. Building it
  needs no new capture read: the twin's record has the geometry, the material and the state
  **[code]**.
- **It does not close "the reference's layer factor is 1.4 to 1.7 times the game's" [inferred
  from a model]**: bounded part at the root and "beams right", nothing at "sun" and "beams left".

**Read in the code for this design** (not in the research, or other than it says):

- *`DrawRecord` has room for one more 16-bit index without growing.* `skip` lies at byte 612 and
  the struct is 616 bytes with an alignment of 8: bytes 613 to 615 are padding (measured with a
  compile-only probe of `offsetof`, no binary made). `terrain_index` and `light_index` were put
  into padding the same way (`draw_record.h` lines 109 to 115).
- *The clay layout has no free binding parameter* (12 of 12, `kClayBindings`, `clay_logic.h`
  lines 1508 to 1521) and 56 of 64 root words. But `t9` (the terrain normals, vertex stage) is
  read by terrain records alone, and the draw's constant-buffer slot at `b1` is 512 bytes for an
  object, of which `cbuffer Object` takes 288 (`kClayObjectSlotBytes`, `ClayObjectConstants`).
  Buffers are bound by address without a stride, so one parameter can carry another element type
  under another vertex program.
- *One vertex program draws everything*: the colour pipelines, the pre-pass pipelines and the
  three shadow-map pipelines all take `vs_` (`clay_pass.cpp`, `CreatePipeline` line 208,
  `EnsureShadowObjects`); the shadow pass binds `b1` at offset 0.
- *The sky step is drawn after the opaque records*, before the first record that blends
  (`SkyPlace`, `clay_logic.h` line 268; `clay_pass.cpp` lines 2618, 3113, 3222), at the far depth,
  depth-tested, without a depth write. And with the shadows on a **depth pre-pass** of every
  depth-writing record comes before the colour pass (FM section 15, "Native shadows"). So a
  half-covered pixel blended at its record's own place would blend over the clear colour, and
  what lies behind it (sky or geometry) would then fail its depth test: the research's option 1
  ("exact for a fragment over what was drawn before it") is not enough by itself. Design 11
  answers this.
- *The pass can read a target back*: `CopyTextureToBuffer` (the oracle feed's dump,
  `clay_pass.cpp` lines 2184, 2222); the RHI's formats are `kR11G11B10_FLOAT`,
  `kR16G16B16A16_FLOAT`, `kR10G10B10A2_UNORM`, `kR8G8B8A8_UNORM`, `kD32_FLOAT`.
- *A discovery row already carries the device's bin mask* (`"bin_mask_lo"`, `capture.cpp` line
  1969), which says whether a draw runs in the game's pre-pass bins.

## Open questions, each with the run that settles it

| | Open | Settled by | If it cannot be settled |
|---|---|---|---|
| 1 | the clock's unit and rate at normal speed (`c10.x`) | plan Task 4, run CLOCK: two captures 20 s apart in one run | the wind needs no unit (it takes the game's values); stated as inference |
| 2 | whether `c17.xy`, `c21`, `c26` gust beyond the slow turn seen | Task 4, run CLOCK and the captures of run STAND | the register bounds of Design 5 stand on the spots measured; a value outside them draws the record at rest, counted |
| 3 | pixels of the fading and the late draws | Task 4, the rasterising replay on a capture with whole index buffers | footprints stay the proxy, said |
| 4 | whether the rasterising replay agrees with the game's depth copy at all (the research's vertex test did not: 36 of 1,293) | Task 4: the replay on draws that do not move, against the depth copy | criterion 5 is "not decidable"; wind stands on criteria 1 to 4, 6 and the user checks |
| 5 | whether a camera stands still enough at the bridge for a frame-to-frame figure | Task 4, run STAND (emulated pairs outside the wind mask) | criterion 6 is "not decidable"; user checks 79 and 80 decide |
| 6 | whether the game's fading and late draws are in its depth pre-pass (their bin mask), so that its shadow mask holds them | Task 12, from the rows' `"bin_mask_lo"` of the Task 4 captures | Design 11's limit stands as written |
| 7 | the texture level the game's `F1EB` fetch takes and the textures' stored levels | Task 4 (the stored mip chain in the capture, if the dump path reaches it); Task 16's kept-set figure | the native fetch stays the pre-pass program's own; criterion 14's figure is reported |
| 8 | what the remainder at "sun" is (R B7) | Task 17, run PAIR-SUN: the stored native scene beside the game's | named "not assigned", a roadmap item |
| 9 | the sign of the change at the beams' root with the sun on (R risk 2) | Task 18's runs | reported as it falls |
| 10 | `2D40` in a capture; wind shaders of towns and interiors; the plants' push points | no run of this sub-project; user check 86 | `2D40` is held by the formula check on made inputs alone, said |

## Design

Names: W x H is the native target's logical size (`ClayPass::kWidth` x `kHeight`). **No part of
this design makes a target whose size is a literal**; the two it makes (Design 3) are W x H and
W/2 x H/2, made only for a diagnostic.

1. **Discovery first** (capture only; nothing the pass draws changes). A draw row keeps its
   draw's **whole index buffer** as a file beside the stream dumps (the buffer whole when it is at
   most 1 MiB, else the draw's own range, else none: `IndexDumpRange`), named in the row's
   `"ib"`. A draw row **outside the main scene** whose vertex shader's table entry is `deformed`
   (the three casters, the occluder) gets the fetch list and the stream dumps an in-scene row has,
   so that the casters' own vertex layouts are evidence and not an assumption. The frame row gains
   `"scene_frame"`, the number of the scene published at that swap. The stored levels of an
   alpha-tested albedo are kept when the dump path reaches them (open question 7).
2. **The rasterising replay** (`tools/xdk_sigmatch/foliage_replay.py`; ruling 6 (2)): triangles of
   a capture row, positions from the game's own microcode (`ucode_eval`) or from the table entry
   at rest, rasterised on the CPU at the place where the game's depth copy holds its sample
   (measured in plan Task 4: a quarter pixel up and left of the pixel's integral position, fitted
   per capture; not the integral position of `PA_SU_VTX_CNTL` = 4 that this text first gave) and
   the first sample's alpha rule, against the game's depth copy of the same frame. It is the like-for-like oracle of the wind (silhouettes with wind
   against at rest), of the coverage's pixel counts, and of `F1EB`'s kept set. It first proves
   itself on draws that do not move (open question 4).
3. **A stored native scene before post** (diagnostic, `--fable2_native_scene_dump=on`; default
   off; follow-up 1's precondition of sub-project 10). With it on, each discovery capture asks
   the pass for one file, `logs/native_scene_<stamp>.f2ns`: the linear target of the Render whose
   scene is the capture's first frame (paired by `"scene_frame"`), the scene's depth, and with
   the sun layer drawn the layer's depth before and after the tree occluders. The depth planes
   are packed into an 8-bit target by a step that writes the 24-bit level
   `floor(d * 16777215 + 0.5)` as three bytes, each rounded explicitly. Without the switch no
   target, program or buffer of it exists.
4. **Formulas before shader text.** `wind_plant`, `wind_trunk`, `wind_leaf` in
   `tools/xdk_sigmatch/light_formulas.py`, each machine-checked against the microcode on the real
   rows of three captures and on made inputs (`wind_check.py`), with the cross-shader identities
   (caster = scene, occluder = twin: exact) as cases and with mutations that must fail (a wrong
   register, a wrong byte, a wrong sign). `position_check.py` compares positions for wind entries
   vertex by vertex. `wind_pins.py` writes the numbers the C++ mirrors are pinned to. A shader
   that does not reach agreement gets no wind entry and is drawn at rest, named.
5. **Wind inputs as side data** (ruling 7). A generated table (`wind_table.inc`, from a `"wind"`
   member of the twelve entries of `vs-transforms.json`) names per shader: the family, the vertex
   registers, each literal the family's formula reads, and the vertex elements (fetch, format,
   offset, source components). **A literal is held by its slot, its place in the family's order**
   (`light_formulas.WIND_LITERAL_ORDER`: 7 slots for a plant, 18 for a trunk, 28 for a leaf, whose
   first 18 are the trunk's), with the register and the component at which this shader's own
   table holds it and its bits: the shaders of one family hold the same numbers at different
   registers and components, so whole registers cannot feed one function (ledger ruling 31; as
   first written: "the shader's literal registers with their values", five registers). The table
   is X-macro lines, `FABLE2_WIND` a shader and `FABLE2_WIND_LITERAL` a literal, written by the
   generator alone (ledger ruling 33). At a draw whose vertex shader has a row, in a frame with
   the wind wanted, the capture reads the registers (`ReadConstantRegisters`, as built; as first written `ReadBankRegisters`) and resolves the
   elements' streams, and `MakeWindInputs` validates: every register read and finite and inside
   its bound (trees: `|c15.x|` <= 1e7, `c15.yzw` in [0, 4], `c16` components in [-4, 4],
   `|c17.xy|` <= 16 (ledger ruling 28: 12.5 occurs in the world's first frame after loading; as
   first written 8), `c17.z` in [0, 16], `|c17.w|` <= 2e7; plants: `|c10.x|` <= 1e7, `c21.xyz` in
   [-4, 4], `c22.xyz` in [-8, 8], `c25.x` in [0, 64] (ledger ruling 26: a draw with `c25.x` = 0
   and `c26` = 0 has inputs and is drawn at rest by its own formula, not counted out of range; as
   first written (0, 64]), `c26.xy` in [0, 4]: about four times the largest value captured; the
   native bounds are held against `wind_check.REGISTER_BOUNDS` by a test); the shader object's
   own literal table holding every literal of the row at the row's register and component, bit
   for bit, each literal alone (ledger ruling 31; checked once a shader object; what else the
   object's table holds is not asked: the plants' `c255.xyz` are no numbers); each element the table's fetch, in a format a reader
   decodes, inside its vertex, its stream holding the draw's largest vertex; a plant's instance
   set active. The result, `WindInputs` (132 bytes), goes into a side array of the frame
   (`FrameScene::wind`; `shadow_wind[2]` for the casters) and the record gets its index in
   `DrawRecord::wind_index`, a 16-bit word in the struct's tail padding: **`DrawRecord` stays 616
   bytes**. Anything that fails: no inputs, the record is drawn at rest with today's program,
   counted by reason and by vertex shader.
6. **Vertex elements in the geometry cache.** One buffer of 32-bit float words a wind mesh,
   mesh-intrinsic (no constant in it, so the wind rebuilds nothing): a trunk vertex 4 words (the
   four values of fetch 4 as the fetch reads them), a leaf vertex **10** (the pivot's three
   coordinates, the four of fetch 4, the three of fetch 5: source x, y, z; ledger ruling 30: the
   formula closes on every captured vertex only with the third, source x, which enters a
   hundredfold; as first written 9 words and two values), a plant's flat position 2 (the copy's
   `A3 + B3`, and the mesh vertex's `z`). All or nothing per buffer; a value that is no number
   gives no buffer. Keyed like the other light buffers (the element, the stream's content).
   **While a tree's words are built the mesh's extent is taken** (ledger ruling 32): the largest
   of the weights and, for a leaf, the largest `|P - Q|` over the vertices built; it is kept with
   the buffer and is what the reach of Design 8 is bounded from.
7. **One wind vertex program** (ruling 8): the clay vertex shader's own text compiled with
   `CLAY_WIND`, in which `t9` is the wind words (a wind record is never a terrain record), the
   object block at `b1` has a tail of **160 bytes** (`ClayWindConstants`: three rows of registers
   and seven rows of literals, the 28 slots of Design 5 in the family's order, a family with
   fewer leaving the rest zero; 288 + 160 = 448 bytes with the object block, inside the 512-byte
   slot; ledger ruling 31; as first written 128 bytes, five literals) and the position is moved
   by the family's function before the rows; the family is two bits of `light_mode`. **The wind
   function and its C++ mirror compute in single precision, in the reference's order of
   operations, marked `precise`**; the clock is not carried in double, and a mirror is pinned at
   the captured clocks with a tolerance of 2e-5 of a unit (ledger ruling 35: the clock arrives
   as a single-precision guest register and the game's own picture has the same loss).
   **Where a leaf's moved offset has zero length in the program's own precision the function
   and the mirror return the moved pivot `Q'`** and do not divide (ledger ruling 36: the
   console's product with a zero factor gives that, IEEE arithmetic no number; three such
   vertices are in each capture's drawn leaf meshes). Every pass that draws a record with wind inputs draws it with this
   one compiled program: the colour pass, the pre-pass, both shadow maps, and in 11c the tree
   occluder. So the pre-pass and the colour pass cannot disagree by a rounding: it is the same
   program on the same bytes (the wind function is marked `precise` all the same). A wind record
   takes a 512-byte slot of the frame's constant buffer (a lit trunk or leaf the one it has; a
   plant, an unlit tree and a caster one more). No binding parameter, root word or sampler is
   added.
8. **What follows the moved position, and what does not.** The distance cut tests the position
   before the sway (the game's order). The light's world position and the normal stay those at
   rest (a few hundredths of a unit: ruling 21, a stated approximation; the game lights the moved
   position). The fog and the shadow mask follow by themselves (per pixel, from the depth). The
   box of a wind buffer's positions grows by the record's reach (`WindReach`, an upper bound
   proved with the formula; a tree's is bounded from its mesh's extent of Design 6, a leaf's
   from the mesh's largest `|P - Q|`, which no register holds: ledger ruling 32) wherever a box
   decides something (the pre-pass's reach rule,
   `PrepassBeyondReach`). The plants' push is not drawn (ruling 10).
9. **A record at rest is today's record.** With the wind's switch off nothing is read, no wind
   buffer or program is made and every compiled program is today's, instruction for instruction
   (the wind's text is under `CLAY_WIND` alone). With it on, a record without valid inputs, or
   whose wind buffer or wind pipeline cannot be had, is drawn at rest in every pass alike, by
   today's program: never "wind with zero constants" (the leaf's rebuilt position at rest is not
   exactly `P` in float, R risk 4), and never moved in one pass and at rest in another.
10. **Coverage records** (ruling 13): a record whose pixel shader is of family A, with
    alpha-to-mask on a two-sample surface, no blend, a depth write, its alpha from the albedo, and
    an alpha scale (the fade word) below 1. *Late* when its offsets are `AA` (the tree between the
    eye and the hero: fade 0.5, issued after the game's depth copy), *early* otherwise (offsets
    `87`: a tree dissolving by distance). Per pixel the covered samples are
    `covered = (alpha >= t0) + (alpha >= t0 + 0.5)` with `t0` the first sample's threshold
    (`CoveredSamples`). Every other record keeps today's rule, a draw at fade 1 among them (its
    half-covered pixels are edge texels alone: a limit until MSAA).
11. **Two draws a coverage record, and their places** (ruling 14). A coverage record's fully
    covered pixels (`covered` = 2) are drawn in place, opaque, with depth, as today. Its
    half-covered pixels (`covered` = 1) are drawn by a second program that keeps those alone and
    blends them at one half over what the target holds (source alpha, inverse source alpha), with
    the record's depth test and depth write: for a late record at the record's own place, for the
    early records together in a **tail** that is drawn before the sun layer's depth is taken and
    before the first late record, after the opaque records before it. The sky step is drawn
    before the first such blend (it writes no depth, so drawing it earlier changes no pixel).
    In the depth pre-pass a coverage record writes its fully covered pixels alone. So what lies
    behind a half-covered pixel, sky or geometry, is drawn whole first and the foliage is laid
    over it at half: exactly the game's resolve of its two samples for everything drawn before the
    blend. After the tail the depth holds every pixel the first sample covers, which is the game's
    depth copy; after the late records it holds theirs too, which is today's end depth. **Known
    errors, limits**: an opaque record issued after the blend and behind a half-covered pixel is
    hidden where the game shows it at half; two half-covered pixels of different records at one
    place show the nearer at half over the scene, not a quarter each; the shadow mask, made from
    the pre-pass's depth, gives a half-covered pixel the sun's shadow of what lies behind it (open
    question 6 says whether the game does the same).
12. **The tree occluder's redraw** (11c; under `--fable2_native_sun`, no switch of its own:
    ruling 16). The frame keeps every twin's index (at most 16), not the smallest alone. After the
    layer's depth is taken and the cloud occluders are drawn into it, each twin record is drawn
    into the layer's depth with the textured depth-only program the pre-pass has, under the
    one-sample alpha-to-mask form (`0xAA`: kept iff `alpha >= 0.5`), the layer's viewport with its
    half-pixel origin, no cull, and the wind program when the twin has wind inputs. A twin that
    cannot be drawn (no texture, no pipeline) is left out and counted; the layer is still drawn
    (it is then today's layer).
13. **The darkness trace** (11c; ruling 5). `tools/xdk_sigmatch/foliage_trace.py` pairs the
    stored native scene with the game's scene of the same frame (the capture's oracle scene), and
    with the replay's coverage as the class of each pixel (open sky, late tree over sky, leaf,
    trunk and wood, ground, other) gives native over game per class and region, in linear
    luminance. A class is mended in this sub-project only when its cause is shown to be a wrong
    sign, level or constant of an existing native formula; a missing feature becomes a roadmap
    item. The same pair measures the scene's half-pixel offset (reported; not changed here).
14. **Switches.** `--fable2_native_wind` and `--fable2_native_coverage` (on, true, 1, off, false,
    0: the light switch's parser), both **off by default**; the redraw under
    `--fable2_native_sun`; the diagnostic `--fable2_native_scene_dump`. Wind and coverage are
    independent and are judged apart. With a switch off: nothing is read, made or drawn beyond
    today, and every compiled program equals today's.
15. **Validation** (ruling 6 for the wind): formula against microcode on real rows; the
    rasterising replay of one frame against the game's depth copy; in the game, standing-camera
    pairs of shots a known time apart, the native frame-to-frame difference in foliage regions
    against the emulated one's; user checks by eye. For coverage and the redraw: mirrors against
    the emulator's per-sample rule; the plan functions' tests; the stored native scene; in the
    game by region, **as the change a model predicts and a measured, named remainder** (ruling 4).

## Fail-closed rules (collected)

- No value read from the guest reaches a shader without having been read whole, a finiteness
  check and a bound; an element is followed only for the fetch the table names. Reason strings,
  counted in the frame's scene and on the F3 text: `wind {off, no-entry, register-unread,
  register-not-finite, register-range, literal, element, stream, instance, over-limit}`; in the
  pass `wind {buffer, pipeline, slot}`; `coverage {pipeline}`; on the Sun line
  `tree occluders drawn K of N`.
- A side array holds at most 65,535 entries; a record past that has no wind inputs, counted.
- A wind buffer is read only for a vertex below the count it was built with, inside a `[branch]`
  the family bits open (a buffer bound by address has no bounds check).
- A wind record is moved in every pass or in none (Design 9).
- No wind position is no number by the wind's own arithmetic: a leaf's zero-length offset gives
  the moved pivot (Design 7; ledger ruling 36).
- The scene dump writes only whole planes of the logical size, each value finite or written as 0
  with a count in the file's head; a frame other than the one asked for is named as such.
- With a switch off nothing of its phase is reached: no register read, no table lookup beyond the
  shader cache's own, no object, no slot.

## Rulings

Rulings 1 to 6 are the controller's (ledger, 2026-10-10); 7 on are this design's, each on the
recommended option.

1. Ruling: sub-project 11 is "foliage: wind and coverage": the vertex wind of trunks, branches,
   leaf clumps, bushes and grass; part-covered foliage in colour and in depth; the sun layer's
   `F1EB` redraw; then sub-project 10's criterion 19 judged again in the game — why: it is what
   the user noticed, and the last two are what keeps the sun switch off — cost if wrong: distant
   trees and water wait one sub-project longer.
2. Ruling: user-present game runs by the controller are allowed for this sub-project as for
   sub-project 10, with the same guards; confirmed by the user on 2026-10-10 ("ok"). Agents never
   pass `-UserPresent` — why: the user told the controller to work through the order — cost if
   wrong: a game window opens while the user works; the first run is announced.
3. Ruling: three phases on one branch: 11a wind (discovery with whole index buffers and the
   rasterising replay first; then formulas, constants as side data, vertex elements, one function
   a family shared by colour pass, pre-pass and shadow casters), 11b coverage (the late `AA` draws,
   then the `87` draws), 11c the `F1EB` redraw under the sun switch and the unexplained x 0.66
   traced; switches `--fable2_native_wind` and `--fable2_native_coverage`, default off until
   validated — why: the research's order; the oracle serves all three — cost if wrong: a long
   branch; each phase ends in a working picture.
4. Ruling: the spec does not promise sub-project 10's criterion 19; it promises the model's
   predicted change per region and a measured, named remainder — why: the research shows coverage
   cannot bring the sun region to the bound — cost if wrong: none; a better result is reported.
5. Ruling: the unexplained darkness at the sun is traced in 11c and mended there only if the cause
   is a fault of an existing native formula (a wrong sign, level or constant); a missing feature
   becomes a roadmap item — why: bounded scope — cost if wrong: the sun switch stays off one
   sub-project longer.
6. Ruling: wind is validated by (1) the closed formulas against the microcode evaluator on capture
   rows, exact to float tolerance, (2) the rasterising replay of one frame against the game's
   depth copy, (3) in the game, a standing-camera pair of shots a known time apart, native
   frame-to-frame difference in foliage regions against the emulated one's, and (4) user checks by
   eye — why: no video comparison exists and phase differs run to run — cost if wrong: a wrong
   amplitude within the bound is only caught by eye.
7. Ruling: the wind inputs are a side array of the frame (`WindInputs`, 132 bytes, like
   `LightInputs`), indexed from the record by a 16-bit `wind_index` placed in `DrawRecord`'s tail
   padding (byte 614), with the size's and the offset's asserts — why: 616 bytes are asserted and
   copied about 950 times a frame; the padding is there — cost if wrong: if another compiler lays
   the struct out otherwise the assert stops the build, and the index moves into a side list of
   (record, entry) pairs.
8. Ruling: one wind vertex program (the clay vertex shader compiled with `CLAY_WIND`) for the
   colour pass, the pre-pass, the shadow maps and the tree occluder; its words at `t9`, its
   constants in the tail of the object slot at `b1`; no new binding parameter — why: the layout
   is full (12 of 12), and one compiled program on the same bytes is the only arrangement in
   which two passes cannot differ by a rounding — cost if wrong: every wind record takes a
   512-byte slot (about 0.3 MB of constants a frame at the third spot), and a second vertex
   program doubles the pipelines of wind records.
9. Ruling: the shaders' literal registers travel as data from the generated table and are held
   against each shader object's own literal table at capture; a mismatch draws the record at rest
   — why: three leaf shaders hold the same numbers in other registers, the plants' five differ in
   arrangement, and a literal table is not part of the microcode's hash — cost if wrong: 80 bytes
   of constants a wind draw that could have been text. (Ledger ruling 31: the literals are held
   and compared one by one by slot, and the constants are seven rows, 112 bytes.)
10. Ruling: the plants' push away from characters is out; the four shaders that have sway and push
    get the sway alone, and `36B5` (push only) gets no wind entry — why: the push points are not
    identified (FM 1043) and are a separate input set — cost if wrong: grass does not part about
    the hero's feet, as today.
11. Ruling: a plant's wind words are a buffer of their own (two words a flat position), not the
    bend buffer's fourth component — why: an unlit plant and every caster have no bend buffer,
    and one rule is one test — cost if wrong: 4 bytes a flat position more than a lit plant
    needs.
12. Ruling: the register bounds of Design 5 are about four times the largest captured values, and
    a draw outside them stands still, counted — why: fail closed as everywhere; a gust (open
    question 2) is the one known way to leave them — cost if wrong: trees freeze in a storm; the
    count names it and Task 4 widens the bound before the capture is built.
13. Ruling: coverage records are family A records whose fade word is below 1; a draw at fade 1
    keeps the first-sample rule — why: only a fading draw has half-covered area, the measured
    items are those draws, and the two-draw scheme takes a record's half-covered pixels out of
    the shadow mask's depth, which must not happen to every leaf in the game — cost if wrong:
    leaf edges stay harder than the game's until MSAA.
14. Ruling: a coverage record is drawn as two draws (fully covered pixels in place; half-covered
    pixels blended in place for a late record and in a tail before the layer's depth for the
    early ones), the sky step moved before the first blend, the pre-pass holding the fully
    covered pixels alone; no new target — why: with the sky drawn after the opaque records and a
    pre-pass before the colour pass, a blend at the record's own place has nothing behind it; this
    is the smallest order that gives the game's resolve over sky and earlier geometry and leaves
    the depth's final content, and the layer's copy, what they are today — cost if wrong: up to
    two more draws for 16 to 37 records a frame; the mask's limit of Design 11; if the order
    cannot be built inside the colour pass's present structure, plan Task 13's first step says so
    before any code and the phase falls back to the late records alone.
15. Ruling: the shadow mask is not rebuilt for half-covered pixels — why: it would need the first
    sample's depth and the fully covered depth at once, a second depth target for one term of a
    fading tree's light — cost if wrong: a fading tree's half-covered leaves take the ground's
    shadow pattern; open question 6 measures whether the game differs.
16. Ruling: the `F1EB` redraw has no switch of its own and fails towards today's layer (a twin
    that cannot be drawn is left out); its effect is measured on the stored layer depth before
    and after it, not by an on / off pair of runs — why: it is part of the sun layer, which is
    off by default; a kept-set figure in one frame is better evidence than two runs of a moving
    scene — cost if wrong: its share of a region's change is a model's figure, not a measured
    one.
17. Ruling: the stored native scene is a diagnostic behind its own switch, paired with a capture
    by the scene's frame number — why: the trace, the holes count of criterion 7 and the
    redraw's kept set all need the native picture before post beside the game's of the same
    frame, and no evidence in hand has it — cost if wrong: one task; a path only a diagnostic
    reaches; one more software-adapter run.
18. Ruling: the trace's verdict is "assigned" when the named classes reproduce the region's
    ratio within 0.05 in logarithm (what unclassified pixels hold is no more than that) and every
    class outside 0.85 to 1.18 is named with its cause or as open; a mend needs a failing check
    of an existing formula first — why: ruling 5
    needs a line between a fault and a missing feature that a reviewer can hold — cost if wrong:
    a real fault that no existing check can show waits for the roadmap.
19. Ruling: the rasterising replay is Python with numpy on the CPU, proves itself on draws that
    do not move before it judges the wind, and judges trunks; leaves are reported — why: no tool
    rasterises a draw today, the research's vertex test failed, and a leaf's edge depends on a
    texture fetch the replay only approximates — cost if wrong: criterion 5 covers a third of
    the wind draws.
20. Ruling: thresholds set here without a measured precedent (criteria 5, 6, 12, 15, 16) are
    marked so, and the figures reached are reported whatever they are; a verdict is "met", "not
    met as written", "not decidable" or "by a substituted measure", never softened — why:
    sub-project 10's practice — cost if wrong: a criterion may read "not met" for a threshold
    that was a guess.
21. Ruling: the leaf's and the trunk's light keep the position and the normal at rest — why: the
    difference is a few hundredths of a unit on a light that turns over metres — cost if wrong:
    a shimmer of the game's leaf light in the wind is absent.
22. Ruling: both new switches stay off by default through this sub-project; turning either on is
    the controller's ruling at its phase's validation, put to the user as a recommendation — why:
    the convention of four sub-projects — cost if wrong: two more words on the command line.

## Success criteria

Each with how it is measured and which evidence decides. Regions are those of
`docs/native-renderer/post-regions.json` ("sun", "beams left", "beams right", "beam root") and of a
new `docs/native-renderer/foliage-regions.json`; a level is 1 / 255 of the display range.
Evidence in `out/native-evidence/foliage/`. A criterion can fail; one marked *(no precedent)* has a
threshold set here without a measured one before it (ruling 20).

**11a, wind**

1. *Formulas.* For each of the twelve shaders, `wind_check.py formulas` gives the microcode's
   `oPos` within `1e-4 * max(1, |oPos|)` in every component on 2,000 of 2,000 made inputs; and
   `wind_check.py rows` on every wind draw row of captures `120435`, `120714`, `121036` (at least
   64 vertices a draw, all of a smaller one) on every vertex. `2D40` on made inputs alone, said.
   *As judged (plan Tasks 5 to 7):* on captures `182130`, `182313` and `121036`: the first two,
   made in plan Task 4 with whole index files and the casters' own streams, stand in for
   `120435` and `120714` (the same two spots); `2D40` has three rows in `182130` (frame 8) and
   is judged on them and on made inputs. The formulas are those of ledger ruling 30 (a leaf
   reads three values of fetch 5) and the literals those of ruling 31. Result: met, 24,000 of
   24,000 made draws and 464,600 of 464,600 vertices of 7,377 rows (Implementation notes).
   Each of these mutations of a formula must fail the check: `c16` for `c15`; two bytes of fetch 4
   exchanged; the sign of the wind's direction; a literal changed by 1%. (Tool; a reduced count in
   `tests/test_wind_formulas.py`.)
   **Verdict (plan Task 11): met.**
2. *Identities.* A caster's microcode on a scene draw's streams and bank equals the scene
   shader's `oPos` exactly (largest difference 0.0) for `FBD3` on `475E`, `4325` on `A584`,
   `A8A5` on `8123` and on `B636`; the occluder's on its twin's exactly in every captured frame;
   and on the casters' own streams (new captures) the formula of the caster's family holds within
   criterion 1's bound. (`wind_check.py identities`.)
   **Verdict (plan Task 11): met**, every identity exact (0.0).
3. *Wind inputs.* In every frame of a capture at each of the three spots, with the switch on:
   each record's registers equal its row's bank bit for bit and its elements its row's fetches
   (`wind_check.py inputs`); every wind draw row has inputs or a counted reason, and no reason
   other than `no-entry` occurs in more than 1% of the wind draws; `sizeof(DrawRecord)` is 616
   (static assert). With the switch off the frame row's wind counts are all zero.
   *As judged so far (plan Tasks 8 to 10; review of Tasks 8 and 9, Important 1):* `wind_check.py
   inputs` holds a row's `"wind"` member, which is a re-read at the draw, not the record's stored
   entry: 8,949 of 8,949 member rows of captures `220514`, `220658`, `221114`. The record's entry
   itself is held by `inputs --entries` on captures whose frame rows carry the scene's stored
   entries (ledger ruling 40): 7,011 of 7,011 entries of 20 frames of captures `002046`, `002354`,
   `002534`, each equal to a member row of the same shader value for value. With the switch off the
   frame row says `"wind":"off"` (it has no counts) and no row has a member (captures `221257`,
   `002231`).
   **Verdict (plan Task 11): met.** On the validation session's captures `022417`, `022755`,
   `022921`, `023101`, `023243`: 10,502 of 10,502 stored entries of 32 frames and 11,606 of 11,606
   member rows; no reason beside `none` and `no-entry`; off-run `022606` says `"wind":"off"` with
   no member.
4. *Positions through the table.* `position_check.py` with the wind terms: over every wind draw
   of the three captures the entry's position against the microcode's `oPos`, 99th percentile
   below 0.05 pixel of 1120 x 720, and none above 0.5.
   *As judged, a deviation (plan Task 7; review of Tasks 5 to 7, I2 and M2):* **not judged as
   written.** "Every wind draw" has no pixel for a vertex off the picture or behind the eye
   (the near plants stand at the camera's feet, where x / w has no bound: 1e-5 units read as 5
   pixels). So: the bound in pixels is judged on the vertices the microcode puts **inside the
   clip volume**; a vertex **outside** it is held to `1e-4 * max(1, |oPos|)` in clip space
   (criterion 1's bound) instead; a vertex both sides cut is counted and not judged; a shader
   with rows and no vertex in view in a capture is "not judged" there and is not counted as met.
   Counts over captures `182130`, `182313`, `121036` (each row's first 512 indices, up to 64
   vertices a row): 245,565 vertices judged in pixels (largest 99th percentile 5.72e-4 pixel,
   `FC4F` in `121036`; largest single difference 7.5e-4), 165,739 judged in clip space alone
   (largest relative difference 8.5e-6), 53,296 beyond the cut on both sides; shaders with a
   vertex in view met 10 of 10, 10 of 10 and 8 of 8, with `FC4F` and `48D3` not judged in
   `182130` (no vertex in view) and judged in the other two; every one of the twelve is judged in
   at least one capture. What the deviation leaves open: for a vertex near the eye the
   clip-space bound allows more than 0.5 pixel in principle (1e-4 units over a small w); the
   largest measured difference is about a twelfth of that bound.
   **Verdict (plan Task 11): not judged as written; met inside this deviation**, also on the
   validation session's captures (10 of 10, 11 of 11, 11 of 11 shaders with a vertex in view;
   largest 99th percentile 7.35e-4 pixel).
5. *Against the game's depth, one frame (no precedent; amended by the ledger's rulings 24 and
   25, 2026-10-10).* The replay is sampled at the place fitted on the capture's own unmoving
   draws (ruling 25). The criterion is judged one-sidedly, on visible pixels: a pixel the replay calls
   nearest where the game's stored depth shows something NEARER by more than 16 steps of 2^-24 is
   "hidden, not judged" (terrain and immediate rows are not replayed); the rest are the judged
   pixels, and a judged set needs at least 1,000 of them or reads "not decidable". First the
   replay itself: on the depth-writing draws that do not move, issued before the depth copy, at
   least 95% of the judged pixels hold the stored depth within 16 steps of 2^-24. Then the
   trunk draws issued before the copy, sunward and bridge, frame 1: that share with wind is
   within 3 points of the unmoving draws' share, and the share at rest is at least 2 points below
   the share with wind. If the first part fails the criterion is "not decidable" (open question
   4). A replay in front of the game's depth still fails; a draw replayed too far back hides
   among the hidden pixels, which is why the two-sided shares with the wind and at rest are
   printed beside the judged ones. (`foliage_replay.py depth-relation`, `wind`. As first
   written: "at least 95% of the pixels the replay calls nearest hold the stored depth within the
   tolerance the tool prints".)
   **Verdict (plan Task 11): met** as amended (sunward and bridge: 99.59% and 99.99%; trunks
   100.00% with the wind against 18.08% and 61.43% at rest).
6. *In the game, a standing camera (no precedent).* Run STAND, with the wind on and with it off:
   shots 3 s apart, emulated pairs and native pairs; `D` = the share of pixels that differ by
   more than 8 levels, inside the mask of the wind draws' rasterised cover and outside it.
   Precondition: the emulated `D` outside the mask is below 3% (else "not decidable": open
   question 5). Then native-on `D` inside the mask is 0.6 to 1.4 of the emulated one; native-off
   `D` inside is at most half of the emulated one (so the measure can tell the two apart);
   outside the mask native-on and native-off agree within 1 point. (`foliage_replay.py motion`.)
   **Verdict (plan Task 11): not decidable as written** (the precondition: the emulated `D`
   outside the mask is 8.2 to 9.8% at a camera that provably stands); **met by the substituted
   measure of ledger rulings 27 and 29** (the control is the still post, 1.07 to 1.67%; 3 s pairs at
   the bridge, each run's own depth-tested mask): native with the wind 0.97 to 1.10 and, in the
   repeat, 0.99 to 1.09 of the run's emulated mean; without it 0.22 to 0.31; the rise 34.9 to 38.7
   points. Outside the mask on is 0.2 to 1.9 points above off (the 1 point as first written is not
   met on the later pairs). Shots 1296 wide, read-back runs at 2 to 5 frames a second, `RUN
   UNCHECKED`: FM section 18, "Validation (11a)".
   *What that verdict covers (ledger ruling 51):* the stills show that the foliage moves and that two runs
   repeat; `D` saturates, and half the game's amplitude would pass its bound. The amplitude rests on the
   native depth of the stored scenes against the replay: the fern's draw at 1.00 to 1.02 of the way from
   rest to the wind position, trunks 99 to 100%, leaves 60.5% with the wind against 2.4% at rest, `48D3`
   57.7% against 10.6% on 5,448 pixels, `33C0` 40% against 0.9% on 388, each reversed on the wind-off
   control (FM section 18, "Validation (11a)", the family table).
7. *No depth-test loss.* In a stored native scene with the wind on and the shadows on: the pixels
   whose depth is nearer than the clear depth and whose colour is the clear colour are at most
   0.01% of the picture, and not more than in the dump of the same run pattern with the wind off
   plus 0.01%. (`foliage_trace.py holes`.)
   **Verdict (plan Task 11): met as written (0 holes in six scene files), and the measure cannot
   fail for its reason** (in a post frame no pixel keeps the clear colour; the colour pass clears
   the depth again): the all-or-none rule is held by tests instead.
8. *Off is today's picture.* With `--fable2_native_wind=off`: every compiled clay program equals
   the program of the commit before the phase, instruction for instruction (shader test); no wind
   program, buffer or slot is made (`DecideClayWind` returns none for every input: test); the
   capture reads none of the wind registers (the frame row's `"wind"` is `off`, tries 0); bridge
   regions of an off-run against a run of the branch's first commit within the spread of the same
   two runs' emulated shots.
   **Verdict (plan Task 11): the first three parts met** (shader test case 52; `DecideClayWind`
   off; run 2's frame rows `"wind":"off"`, no member, no wind line); **the fourth not judged** (no
   pair of runs of the two commits at one width).
9. *Cost and errors.* On a quiet machine at shot width 1942, wind on: every steady 150-frame
   native window at 30.0 fps or more at the bridge, none below 29.2 at the third spot; the
   capture median stated beside the off-run's (a difference under a third of a millisecond is
   "not decidable": sub-project 10's ruling 42). No `d3d ERROR` in a 240 s software-adapter run
   under the debug layer for each build that changes bindings, targets or shader text; warnings
   listed by id.
   **Verdict (plan Task 11): cost not judged** (no run of the phase is timing evidence: 1296 wide
   or with read-back, a user present; to be repeated at width 1942, wind on and off); **errors
   met** (log 604: 0 `d3d ERROR` lines in 240 s on the software adapter, warning id 1328 only).

**11b, coverage**

10. *The rule.* `CoveredSamples` equals the emulator's per-sample rule (`dxbc_translator_om.cpp`,
    lines 1681 to 1790, mirrored in Python) for all 256 offset bytes, the four parities and
    alphas 0 to 1.25 in steps of 1 / 1024, on two samples; and for every such input: the fully
    covered and the half-covered sets are disjoint and their union is exactly the set
    `AlphaToMaskCovers` keeps. (Test; the shader test finds the two thresholds in the compiled
    programs.)
    **Verdict (plan Task 13): met** (1,311,744 inputs against the emulator's instructions; the compiled kept
    sets of the half and the full program against `CoveredSamples`).
11. *Order and depth.* `PlanCoverageOrder`'s test: for every arrangement of up to six records
    (opaque, early, late, blended) with every sky place and depth place, the sky precedes every
    blend, the early tail precedes the layer's depth step and every late record, and no record is
    drawn before a record it followed among the opaque ones. With the switch off the plan is
    today's order for every input. In the stored native scene of run PAIR-SUN with coverage on:
    the layer's depth before the tree occluders has no texel of the late twins' rasterised cover
    (at most 0.5% of that cover, the edge), and criterion 7's holes count holds with coverage on.
    **Verdict (plan Task 13): met for the late records** (the plan function's cases; run D's layer depth
    holds the late tree's own depth on 0.02% of its cover; 0 holes in four scene files). The early records'
    part is plan Task 14's, not yet run.
    **Verdict (plan Task 15): met.** The early records: the layer's depth before the occluders holds their
    first-sample cover on 100.00% (7,662 of 7,662 pixels the layer's texels are taken from; bound 95%) and
    the late tree's on 0.03 to 0.08% (run H); the same without coverage (run I).
12. *In the game, the late draws (no precedent for the band).* Sunward, sun off, post and bloom
    on, coverage on against off (two runs of one plan whose emulated shots agree within 0.97 to
    1.03 per region; three brackets; no shot within 3 s of a view switch): native on over native
    off per region inside the model's
    band, widened by that spread: "beam root" 1.30 to 2.05 (model 0.628 / 0.358 = 1.75), "sun"
    0.97 to 1.12 (1.06), "beams left" 0.97 to 1.13 (1.075), "beams right" 0.97 to 1.13 (1.07).
    **The predicted change at "sun" is small, and the criterion says so: this step is not
    expected to mend the sun region.**
    **Verdict (plan Task 13): not decidable as written** (in the view the shots were taken in, the late tree
    holds no pixel of any region, and without read-back the game's exposure stood at its ceiling of 16);
    **shown by a substituted measure**: on the tree's own pixels the stored linear scene with coverage is
    1.36 to 1.38 of the scene without, 1.04 to 1.06 of the half blend's prediction, the depth cover equal
    within a third of a point. The regions' band is still to be measured in the held sunward view (FM
    section 18, "Native coverage: the first runs"). (The 1.36 to 1.38 is a ratio of means over the tree's
    pixels; the median of the per-pixel ratio is 1.106 and 1.086, the review's figures: the bright sky
    behind a minority of the pixels carries the mean.)
    **Verdict (plan Task 14, the held sunward view, runs D against E): not met as written at "sun"** (stored
    linear scene 1.313, shots 1.47 and 1.40, against 0.97 to 1.12) **and, in the shots, at the root** (2.24
    and 2.57 against 1.30 to 2.05; the linear scene 1.926 is inside); **met at "beams left"** (1.095; 1.11,
    1.13); **"beams right" at the bound** (1.084; 1.13, 1.14). The precondition is met (the emulated shots
    agree within 0.999 to 1.007). The change is larger than the model's, towards the game in every region
    (native over emulated at "sun" 0.33 -> 0.47 to 0.49, at the root 0.19 to 0.21 -> 0.47 to 0.48) and past
    it in none; the model's frame had the tree on 27% of "sun", this view has it on 58.7%.
    **Recorded (ledger ruling 56): not met as written, met in direction.**
13. *In the game, the fading draws.* The same pair with the `87` draws built, against the late
    draws alone: per region between 0.97 and the model's upper bound times 1.03 (model B over C:
    "sun" 1.25, "beams left" 1.05, "beams right" 1.13, "beam root" 1.02), and the replay's pixel
    count of the fading draws in each region is stated beside it. A region that darkens by more
    than 3% fails.
    **Verdict (plan Task 14): met by a substituted baseline.** No run has "the late draws alone" on the
    binary with the fading draws; in the standing view the late tree holds no pixel of any region, so
    coverage on against off is the fading draws alone: "sun" 1.006 (472 fading pixels), "beams left" 0.998,
    "beams right" 1.014, "beam root" 1.004 (no fading pixel in the three). None darkens.
14. Criteria 8 and 9 again for `--fable2_native_coverage` (off: 11a's picture and programs; cost;
    the software-adapter run).
    **Verdict (plan Task 15): off met** (the programs' bytes: shader case 53; the pass's text; six off-runs
    without a line of it); **errors met** (logs 615 and 620: 0 `d3d ERROR` in 240 s each on the software
    adapter, warning id 1328 alone); **cost not judged** (no timing evidence: 1296 wide, a user present).

**11c, the tree occluder, the darkness, criterion 19**

15. *The redraw's kept set (no precedent).* On the paired frame of run PAIR-SUN: of the texels of
    the layer's depth that the replay's `F1EB` (the twin rasterised with the one-sample rule,
    alpha exactly 1 at the base level) closes, the native redraw closes at least 90%, and the
    texels the native redraw alone closes are at most 15% of the replay's count; with a sun-off
    run the redraw draws nothing (the Sun line). The texture level that fits best is reported
    (open question 7).
16. *The darkness trace (no precedent).* `foliage_trace.py trace` on the paired frame gives, for
    "sun" and the three other regions, native over game per class; the verdict is "assigned"
    when the named classes reproduce the region's ratio within 0.05 in logarithm (unclassified
    pixels hold no more than that) and every class outside 0.85 to 1.18 is named with its cause
    or as open. **What the criterion promises
    is the table and the names, not that the remainder is gone.**
17. *Sub-project 10's criterion 19, judged again.* Sunward, sun on, wind on, coverage on, three
    brackets, native over the emulated shot of the same run: "sun", "beams left", "beams right"
    (and the root, reported). **Not promised to lie in 0.75 to 1.25** (ruling 4). What can fail:
    each region lies within 0.20 of the figure `foliage_trace.py predict` gives for it from
    sub-project 10's run T16-5 (0.59 to 0.76, 1.23 to 1.35, 0.96 to 1.08), the coverage model of
    criteria 12 and 13 as measured, the redraw's kept set of criterion 15 and the screen formula;
    and the remainder at "sun" (measured over the game's) is stated with the trace's classes.
    The verdict of criterion 19 itself is recorded as it falls, and with it the recommendation
    for the default of `--fable2_native_sun`.
18. Criteria 8 and 9 again for 11c's builds (`--fable2_native_sun=off`: no twin list read, no
    redraw; the dump's switch off: no target, program or copy).

**All phases**

19. *Evidence rule.* Every number names its file; inference is marked; the research's labels are
    kept where its facts are repeated; a criterion not met as written is said to be not met.

## Limits known now

- **Draw order under coverage** (Design 11): an opaque record behind a half-covered pixel and
  issued after the blend; two half-covered records at one pixel; the mask's value at a
  half-covered pixel.
- **Leaf and plant edges at fade 1** stay the first sample's (ruling 13).
- **The leaf's light at the rest position** (ruling 21); normals do not turn with the wind.
- **The plants' push** (ruling 10); **`2D40`** held by made inputs alone; **towns and
  interiors** unseen.
- **The clock is the game's**: a paused game is a still tree in both pictures; a frame whose wind
  constants fail a bound stands still.
- **The redraw's "alpha exactly 1"** is a knife edge on the fetch's filter and level (open
  question 7): criterion 15 reports how near the native fetch comes.
- **Shadow maps**: every wind caster's map changes each frame, as the game's does; nothing caches
  a caster's map across frames today, and nothing may begin to without the reach.
- **The reference's layer factor (1.4 to 1.7 times the game's)** is not this sub-project's: the
  redraw is its one candidate not excluded and is bounded, not expected to close it.
- **One save, late morning, three spots**, as before.

## Risks

1. Criterion 19 may stay not met at "sun" after everything here (R risk 1): coverage is worth at
   most 0.75 there by the model, the measured 0.59 to 0.76 needs a factor 1.0 to 1.3.
2. The root may go further over (R risk 2): a half-transparent late tree brightens the root's
   scene where the native picture is already at 1.21 to 1.40 with the sun on; the redraw takes
   some away; the net sign is unknown until run (open question 9).
3. The colour pass's order under coverage (ruling 14) is the part of this design that no
   evidence in hand has exercised: the sky step earlier, a tail inside the colour pass beside the
   layer's depth step. It is held by a pure plan function under test and by the stored scene's
   holes count; its first execution is a software-adapter run.
4. The trunks' and leaves' closed formulas do not exist yet; a formula that will not close keeps
   its family at rest (Design 4), and a tree whose trunk sways under leaves that do not would
   look worse than a still tree: then the family pair is switched together (plan Task 7's rule).
5. A wind record with a wind pre-pass pipeline but a rest colour pipeline would open holes over
   every tree: Design 9's all-or-none rule and criterion 7.
6. The geometry cache grows (a leaf mesh by 40 bytes a vertex, ten words: up to 0.57 MB for
   the largest mesh seen, 14,334 vertices; ledger ruling 30; as first written 36 bytes, 0.52 MB); the plants' flat words by 8 bytes a flat position.
7. The stored scene's depth copy is a new use of the depth target (a sampled depth packed by a
   step): one more software-adapter run, and only the diagnostic reaches it.
8. Every run is likely `RUN UNCHECKED` (the user at the machine); timing needs a quiet machine at
   shot width 1942 with no agent beside it.

## Implementation notes (deviations)

Each phase's notes are added here when it is validated. Before that, from plan Task 4 (frame map
section 18, "Discovery runs"; figures and files there):

- Design 2 and criterion 5 are amended above (rulings 24 and 25): the sampling place is fitted,
  the bound is one-sided on visible pixels with a floor of 1,000 judged pixels. On the two
  captures with whole index buffers both parts of criterion 5 are met (99.59% and 99.99%; trunks
  100.00% with the wind against 18.08% and 61.43% at rest). Whether the quarter pixel is the
  console's first sample or this emulator's read-back convention is not known: it matters for
  Design 10's first-sample rule (plan Task 12).
- Open question 1: the clock is seconds of real time, one unit a second, unclamped; **its origin
  varies by run** (1.70 to 4.06 s after the log's first line over the runs of plan Task 4; the
  captures "at 88 s" of Task 11's session read 86.80, 86.88 and 86.96), so "from the process's start,
  two runs agree to 10 ms", as first written here, is withdrawn. Open question 2: the direction turns in steps of 2
  to 3.5 degrees inside 17 to 28 degrees of azimuth in 17 s of clock; the strength is constant.
  Open question 3: the pixel counts exist. Open question 4: yes. Open question 6: the fading and
  the late draws are in the pre-pass bins. Open question 7: not settled (base level alone in a
  capture). Open question 10: `2D40` is in capture `182130`, frame 8.
- **Design 5's bound of `c25.x`, (0, 64], does not hold on the plant caster**: `A8A5` rows with
  `c25.x` = 0 and `c26` = 0 (no sway) occur at the bridge, 4 to 5 a frame. Ruled (ledger ruling
  26): zero is admitted; Design 5 is amended above. Ledger ruling 28 widens `|c17.xy|` to 16.
- **Criterion 6's precondition does not hold as written** at a camera that provably stands (the
  capture's eye and rows are bit-equal over eight frames): the emulated `D` outside the mask is
  9 to 12% (characters, water, the leaves' shadows, the moving sun); in a still box it is 0.9 to
  1.3%. To be ruled: the still boxes of `docs/native-renderer/foliage-regions.json` as the
  control. (Ruled since: ledger rulings 27 and 29; criterion 6's text above is not amended by
  this note.)

From plan Tasks 5 to 7 and their review (`docs/native-renderer/lighting-formulas.md`, "Wind";
figures and files there). The design above is amended in place, each passage marked with the
ledger ruling's number:

- **Ruling 30** (Design 6, risk 6): a leaf vertex carries 10 wind words, three values of fetch 5.
- **Ruling 31** (Design 5, Design 7, this spec's ruling 9): literals per slot in the family's
  order, 28 slots for a leaf, each checked alone at capture; the constants block 160 bytes, 448
  with the object block.
- **Ruling 32** (Design 6, Design 8): a leaf's reach from the mesh's largest `|P - Q|`, taken
  when the wind words are built. Measured over whole leaf streams: 19.3 and 19.9 units at most,
  1.48 at the median.
- **Ruling 33** (Design 5): the table is X-macro lines, reshaped only through the generator.
- **Rulings 26 and 28** (Design 5): the bounds of `c25.x` and `|c17.xy|`.
- **Ruling 35** (Design 7): single precision in the reference's order, `precise`, pinned at
  captured clocks with tolerance 2e-5. Horizon (the review's measurement): the difference from
  the double reference is about 0.1% of the sway at 4 h of session and about 1% at two days;
  the game's own picture loses the same.
- **Ruling 36** (Design 7, fail-closed rules): the zero-length rule of a leaf.
- **Criterion 1** is judged on captures `182130`, `182313`, `121036`: met. **Criterion 2**: met,
  every identity exact (0.0). **Criterion 4** is not judged as written: its deviation and counts
  are with the criterion; inside that deviation every shader is met.

From plan Tasks 8 to 11 and their reviews (FM section 18, "Wind inputs", "Wind words", "Native
wind", "Validation (11a)", "Limits (11a)"; figures and files there):

- **Design 5 as built** (Tasks 8 and 9): `WindInputs` is 132 bytes with the 28 literals in the
  table's row; `DrawRecord` stays 616 bytes with `wind_index` at byte 614; the registers are read
  by `ReadConstantRegisters`; the clock travels as the register's bits. Criterion 3 is judged on
  the record's STORED entry (ledger ruling 40: an armed capture writes the scene's entries into its
  frame row), not on the re-read at the draw alone.
- **Design 6 as built** (Task 9; ledger ruling 40): everything of the wind words before the upload
  is a pure function (`PlanWindWords`, `BuildWindWords`) under test; the mesh's extent
  (`WindExtent`: the largest weight and the largest `|P - Q|`) is taken while the words are built.
- **Design 7 as built** (Task 10): one program, `kClayWindVs`, for the colour pass, the pre-pass and
  both shadow maps; `PipelineKey` gained `wind` (14 bytes); the scene's records are decided in a
  loop of their own after the shadows are prepared; an unlit record, a lit plant and a caster take a
  new large slot (at most 4,096 a frame; at the cap the casters are served first); the literal
  verdict of a shader object is asked again once a frame; F3 has a `Wind:` line. The program's text
  is held to the reference by value (`tests/test_wind_hlsl.py`: 32 pins and 1,200 made inputs;
  ledger ruling 44), since no earlier test saw a wrong component or sign in it.
- **Ledger ruling 48**: a plant's seed is summed in another association than the game's (a phase of
  about 1e-3 radian); logged as a limit, not fixed.
- **Ledger rulings 41, 45, 46**: phase 11a was validated on binary `9EE2090F124FC916`, built before
  the merge of the frame-time work, because the head did not build then; the head builds since
  commit `a227629` (plan Task 13's first step: binary `3D66CE8A74D4871F`, both suites passing).
- **Ledger ruling 47**: validation runs carry `--mouse_look=false`. With it the sunward plan's look
  did not finish rising before the shots at 2 to 5 frames a second (run 5 of Task 11).
- **Criterion 6** (ledger rulings 27, 29, 51): its text above is not amended; its verdict is by the
  substituted measure, and what that measure covers is said with the criterion.
- **Ledger ruling 49**: criterion 9's cost and criterion 8's fourth part stay open until the
  sub-project's closing runs at width 1942.
- **The fern** (ledger rulings 43, 44; review of Task 11): no code fix, on the native depth; the
  account of the shots by the sway's phase is consistent with them and not shown; its look at rest
  is a limit older than the wind (FM "Limits (11a)" 8).

From plan Tasks 12 to 14 (FM section 18, "Coverage: the rule and the order", "Native coverage", "Native
coverage: the first runs", "Native coverage: the fading draws", "Stored mip levels in a capture"):

- **Design 11 and ruling 14, amended (plan Task 14's design check): an early record's pre-pass draw is today's**
  (the first sample's pixels), not its fully covered pixels alone. The pre-pass feeds the shadow mask alone;
  the game's mask reads a depth copy that holds the first sample of every fading row and nothing of the late
  rows (which copy is not established: this frame's by the bins' order, or, as the emulator executes the rows
  in issue order with the mask draw issued before the copy, the one standing there, a frame late; either
  serves), so a half-covered pixel of a fading tree takes the shadow at the tree's depth in the game. So the mask is the one
  of before for every early record, ruling 15 holds with nothing given up, no depth-only full program exists,
  and Design 11's limit "the shadow mask ... gives a half-covered pixel the sun's shadow of what lies behind
  it" applies to no early record. A late record is in no pre-pass, which is likely the game's own behaviour
  [inferred from the bin masks: open question 6].
- **Open question 6**: the fading and the late rows are in the pre-pass bins (`bin_mask_lo` = `FFFFFFFF` in
  every family A row); the game's mask draw is issued under a colour bin alone and reads the depth copy's
  address.
- **The order of the game's rows**: the late rows are issued inside the game's blended group at a place that
  changes with the view and from frame to frame (after 30 to 35 blended draw rows in three captures, after
  none in the held sunward view; log 623 alternates within one run). The sky step is drawn before the first
  blend in every form. A blended record without depth write behind a late tree is shown at half or hidden by
  that order: a possible flicker, 0 pixels in the held view, not measured elsewhere (FM "Native coverage: the
  fading draws' runs"; a limit until ruled). (As first written here: "natively the sky's place lies before the
  late record once the frame's textures are there, and the sky step moves only in a run's first native
  frame": wrong.)
- **A late record whose scale would admit fully covered pixels** is today's record (`coverage: full`): a
  verdict the design did not have; none is captured.
- **The sunward view exists only while the stick is held**; released, the camera returns above the bridge
  within 5 s. Criterion 12's regions need shots taken during the hold.
- **Ruling 13's premise is in doubt** (ledger ruling 54): a minified alpha-to-mask plant at fade 1 may have
  half-covered pixels well inside its cut-out. The discovery capture now dumps a texture's stored mip levels
  so that this can be measured; the early records' scheme would carry such a draw by widening `CoverageOf`
  alone (its pre-pass is today's, so nothing would leave the mask's depth). Not built before the measurement.
  **Measured (run F): the premise holds weakly and the fern is not explained.** The stored levels' alpha is
  one bit at every level; on them 24.0% of the fern's held pixels have one sample of two (16.3% on the base
  level; not the half to two thirds a box filter suggests), the native cover already is the rule's on the
  stored levels (99.3%), and at one half the fern's mean would be 8% lighter, where the game's is darker.
  The third case is not built (FM "The fern on its stored mip levels").

From plan Task 15 (FM section 18, "Validation (11b)", "Limits (11b)"):

- **Ledger ruling 55**: a blended record without depth write behind a late tree, shown at half or hidden by the
  game's frame-to-frame order, is a limit with a user check (88), not rebuilt: 0 pixels in the one view
  measured; drawing the tree's half after such records would be wrong for those in front of it (38 pixels in
  the same view).
- **Ledger ruling 56**: criterion 12 is recorded as not met as written, met in direction.
- **Ledger ruling 57**: the fern's look at rest is not coverage; the third case (a family B draw at fade 1) is
  not built; a limit and a roadmap item.
- **The four combinations of the wind's and the coverage's switch** have each drawn in the game (runs F, C, G,
  B); none was judged for cost.
- **What the closing runs owe phase 11b**: cost at width 1942 with the window on the usual monitor (wind and
  coverage on against off). No verdict needs a repeat with full read-back; the game's scene under the late
  tree (criterion 12 like for like) comes with 11c's trace.

**Phase 11c, Tasks 16 and 17, and the mend of the half draw (ledger rulings 60 to 62; frame map section 18, "The
tree occluder (11c)" to "The half blend, once: in the game")**

- **Criterion 15: not met as written, met against the level a fetch takes** (ruling 62). Against the replay at
  the base level the native redraw holds 46.8 to 52.9% of the replay's texels (0.4 to 0.6% alone); against the
  replay that takes each triangle's own level between two stored levels, 95.9 to 97.2% (4.8 to 5.7% alone) in
  five dumps; with the wind off 61.6% (39.4% alone). Open question 7: no single stored level fits; the fetch's
  own does. "With a sun-off run the redraw draws nothing": met.
- **The occluder's level bias** (ruling 61): the game's fetch constant for the occluder names a bias of +1.0
  which the emulator does not apply; the native redraw keeps the emulator's fetch. A limit.
- **Design 12 as built**: the depth-only program is an object of the layer's own (the pre-pass's text); a twin
  is left out for `state`, `alpha`, `texture`, `record` or `pipeline`; a twin moved in the scene is never drawn
  at rest; nothing is redrawn on the oracle feed.
- **Design 11 amended (ruling 60)**: a coverage record's half-covered pixels are drawn in TWO steps where the
  design had one: their depth (the half program, no colour, the record's depth test and write), then their
  colour (the half blend, tested less-or-equal, no depth write), so that the nearest of a record's leaf cards
  blends once. The one draw blended once for every card nearer than all before it (measured: the sky's share
  0.45 / 0.19 / 0.08 under one, two, three cards; after the mend 0.45 at each). The end depth is unchanged.
  Left: two different records' half parts at one pixel.
- **Criterion 16: met** (the table and the names; every region assigned in both paired frames). Linear scene,
  native over game, by class over the picture after the mend: late tree over sky 0.986, late tree over closed
  0.856, leaf 0.881, open sky 0.951, trunk and wood 1.028, the picture 0.957. In the held framing before the
  mend (run E): sun 0.739, root 0.755; after it the game's scene of that framing is in no evidence (the
  read-back run did not reach it): about 0.79 and 0.97 by an estimate from two runs. Classes of 13 has two
  more than the plan's six (late tree over closed, plant).
- **Ruling 5's question is closed**: no existing light formula is at fault (the leaf formula's check passes);
  the one fault found was the half draw, mended. Open, to the roadmap: the leaf class at 0.88, open sky at
  edges at 0.95, a native beam at "beams right" the game's picture does not show.
- Criteria 17 to 19 and the cost are Task 18's.
