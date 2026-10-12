# Native renderer sub-project 13: water (the lake's surface with a native reflection)

Status: **execution complete with concerns, 2026-10-12; phase13a formulas,
phase13b surface/sky and mandatory13c own far-terrain/blur built and run.
Fresh Task18 source/runtime review approved; tool/test commit9e6096f. Whole-lake
colour, records, cover/bed, sun-layer depth, common land, debug gates and13c
frame-cost/floor pass their stated measures. Target silhouettes and pools are
not met as written; original tiny-base replay, sky subset, near/glint views,
old-image control and capture increment remain not decidable. Six sky-horizon
misses remain. Task19 initial closing commit and two fresh whole-branch reviews
are pending, with criterion18 closing gate pending. Water stays off by default.**
Ledger rulings 26 (the `D1CC` cubes are not drawn:
their design, ruling 22 and their part of criteria 6, 13 and 16 are struck, each
passage marked "ledger ruling 26") and 27 (the surface tests and writes depth:
ruling 14 stands) continue to apply. Branch
`native-renderer-water` (from master `ce61f2d`). Research: `.superpowers/sdd/water-research.md`
(untracked; its scripts and outputs in `out/native-evidence/water/`, "WE" below: `b_*`). Ledger:
`.superpowers/sdd/2026-10-12-native-renderer-water/progress.md` (rulings 1 to 8 are the controller's
and are not reopened here; where this design finds something that bears on one of them it says so
under "Decisions that are not this design's"). Plan:
`docs/superpowers/plans/2026-10-12-native-renderer-water.md`. Game launches wait for
the user's leave (ledger ruling 7): every part below says whether it needs a run.

Every fact carries the research's label or names what it was read from: **[measured]** printed by a
named script from a capture, a stored scene or a log; **[microcode]** read from
`out/shader_dump/shader_<HASH>.ucode.vert` / `.frag`, not run; **[code]** read from the repository's
source; **[inferred]**; **[unknown]**. "FM" is `docs/native-renderer/frame-map.md`; "R" with a
section is the research; "CAP `<stamp>`" a discovery capture `native_discovery_20261011_<stamp>.jsonl`
in `out/build/win-amd64-release/logs/`. Checks made for this design are the scripts `WE/s_*.py` with
their outputs `WE/s_*.txt` (untracked evidence, as the research's).

## Goal

The lake's surface stands in the native view as the game draws it: the blended draw `BEAD84BD72072E0E`
/ `A17D8AEC3A817D45` by the game's own formulas (two scrolling normal fetches, a reflection lookup, a
base colour by fresnel, the sun's glint, fog, exposure, the shore masks' alpha), late in the frame over
the real lake bed, with the reflection taken from a **native** 256 x 256 target that the pass renders
itself: first the sky, then the far terrain. The dead opaque twin `F6D98C7B4D98438B`, which today is
the flat-clay placeholder, is no longer drawn. *(Struck by ledger ruling 26: "The `D1CC6DC28244C55A`
cubes go into the native sun layer's depth." The cubes are not drawn.)* Behind `--fable2_native_water`
(ledger ruling 5), default off. No read-back, no SDK change (ledger rulings 1 and 2).

The work is done in three phases (ruling 9): **13a** (no game: the discovery additions, the formulas
and their machine checks, the offline replays on the captures in hand, the decoders), **13b** (the
capture's side block, the switch, the surface, the reflection target with the sky, their validation;
the cubes struck by ledger ruling 26), **13c** (the far terrain in the reflection target; entered
only on the conditions of Design 9).

**Non-goals**: the far lake `5763211D63BBFA3D` / `2B091BD0C45A7454` (ledger ruling 4); the game's
picture through an SDK copy interface (ledger ruling 2); objects, flares and the pair `6954` / `A4A9`
in the reflection; the texture at slot 14 as anything but what the dumps hold; the shadow term under
bool `b137`; an opaque water form (a live `F6D9`); shadow correctness (ledger ruling 6); the other
monitor's swap (ledger ruling 8); the unidentified pair `43ED` / `7F65` that the game draws between
its clouds and its water; MSAA (roadmap 5); any other region, town, interior or time of day.

## What is known (research; evidence there)

- **The draws [measured, R B1; `WE/b_water_d1_f1.txt`, read]**: twelve flat meshes a frame at the
  bridge (eleven at the third spot, three by the trees and on the sun side), 28 to 456 vertices,
  position `32_32_32_FLOAT` and a `32_32_FLOAT` pair at a stride of 20 bytes; ten at height 20.1024,
  one at 40.474, one at 40.0. Each is drawn twice with the main scene's `c0..c3`: by `F6D9` as the
  first rows of the scene (no blend, depth `0x00708766`), and by `A17D` late (blend `0x07060706`,
  source alpha and inverse source alpha; depth `0x00708746`: tested, **written**, function 4; alpha
  test greater than `210E` = 15 / 255; colour control `0x0000000C`: no alpha-to-mask).
- **`F6D9` draws nothing [microcode + measured, R B1]**: the two pixel shaders differ in one
  instruction (14: `seq` against `sgt`; `diff` of the two listings, made again for this design). Both
  form `alpha = 1 - saturate(c141.x - (1 - m) * c141.y)` of the mask sample `m`; `F6D9` kills where
  alpha is below 1, `A17D` where it equals 1. With `c141` = (2.003, 2, 0.0015, 0.0588) alpha is at
  most 0.997; no texel of the twelve masks gives 1 (`WE/b_masks_d1.txt`, read); and the game's depth
  copy holds the bed on 17,840 of the 18,536 pixels where the native placeholder stands
  (`WE/b_gap_fw_on.txt`, read).
- **Textures of a water draw [measured, R B1]**: slot 1 the mesh's shore mask (`k_8`, 8 x 16 to
  64 x 64, twelve addresses, static); slot 2 the normal map (`k_DXN` 256 x 256 with levels,
  `0x14D40000`, static); slots 4 and 5 the frame's fog tables; slot 8 the screen shadow mask (read
  only under `b137`, clear in every row); slot 13 a 256 x 256 `k_8_8_8_8` picture the game renders
  every frame, one a water body (`0x13401000`, `0x13481000`, `0x13501000`, `0x13581000`; 6, 1, 4 and 1
  draws at the bridge); slot 14 the picture's base + `0x40000`, all zero in every dump, with no
  resolve to it in the frame.
- **The reflection views [measured, R B1; `WE/b_views_*.txt`]**: before the main scene, at surface
  pitch 280 with four samples, one view a body; far terrain (`5003` / `7DDA`), a pair `6954` /
  `A4A9`, up to 19 objects (`ECD6` / `8D90`), two to six flares (`81BA` / `D72F`) and the sky's cube
  (`8262` / `F760`); 138 rows a frame at the bridge. Some views are followed by a seven-tap blur
  (`B2D9` / `F186`).
- **The gap today [measured, R B2]**: native over game on the lake's footprint in the linear scene
  before post: red 1.35 to 1.40, green 0.98 to 1.02, blue 0.84 to 0.86, hue +17 degrees; luminance
  flat (native 5th to 95th percentile 1.57 to 1.66 where the game's is 1.28 to 1.85). The lake is
  2.3 to 2.6% of the picture, 120 to 250 units from the eye.
- **Why it is so [code, R B2]**: both pixel shaders are `no_albedo`; `F6D9` is drawn as flat clay
  without its test, over the whole mesh, with depth, fogged by the unlit form; `A17D` is left out as
  `blend-unknown` (`DecideAlpha`, `src/native/capture/material.h` line 123: "blended and the colour
  unknown").

**Read for this design** (not in the research, or other than it says; each is repeated under "What the
research has differently"):

- *The mirrored camera is a closed product of things the main scene already has [measured:
  `WE/s_views.py`, `WE/s_views_*.txt`]*. With `M` the main scene's `c0..c3`, `h` the body's height and
  `Mirror(h)` the reflection in the plane z = h, the view's rows are
  `diag(2/3, 2/3, 1, 1) * M * Mirror(h)`: in every row of the matrix, the depth row included, within
  1e-6 relative, in 16 of 16 views of five captures (`155547`, `155731`, `221449`, `155929`,
  `160104`). No oblique near plane is in the matrix. The factor is the water draw's own `c31.xy`
  (0.666667 in every row).
- *The picture is not flipped against the screen, and its black rows are the half below the horizon
  [inferred from the above; by hand]*. With the main camera 10.2 degrees down and a vertical scale of
  1.7376 the horizon of the mirrored view lies at row 101.3 of 256; the dumps are black in rows 0 to
  about 101 (R B1). So what is not drawn is the mirror of everything below the water plane, and the
  sky fills the rest. `WE/b_noreflection_d1.txt` could not tell the two orientations apart (30.3
  against 28.9%); criterion 4 does.
- *The views are drawn at exposure 1 [measured: `WE/s_views_*.txt`]*: `c20.w` is 1.0 in every row of
  every view, where the main scene's is 9.157 (CAP `155547`). The picture holds the scene before
  exposure, saturated to 8 bits; the water shader multiplies by `c20.w` at its end. The picture's
  fetch has no gamma sign (`tf13` dword 0 = `0x82000002`: sign fields 0) and wraps in both axes.
- *The views' sky is the main sky's own [measured: `WE/s_viewsky_155547.txt`]*: the view's `F760` row
  has the main row's pixel registers but for the eye (`c19.z` = -9.966, the mirrored centre),
  `c20.w`, `c0` and registers the sky does not read; `c31`, `c46`, `c47`, `c64` to `c68` and the
  textures are the same.
- *Where the water stands in the game's order [measured: `WE/s_order_155547.txt`]*: the sky (`F760`),
  the moon (`18D3`), three cloud quads (`6197`), two draws of `43ED` / `7F65`, **the twelve `A17D`
  rows**, the far lake (`2B09`), then the blended group (`401A`, `DBFD`, `C300`, the fur `014F`,
  `983E`, the late leaves, the sun's disc `0498`).
- *The native layer's depth is taken after the water, not before it [code + FM "The layer's depth
  place"]*. The pass takes the layer's depth before the first drawn record at or beyond the smallest
  twin index, or after the last record (`PlanSunDepthPlace`, `clay_logic.h` line 5522); the first
  twin stands at 946 to 952 of the bridge's rows, after the `A17D` rows (FM line 6730: "11 or 12
  `A17D` rows with depth write lie between"). A native `A17D` that writes depth as the game's does
  therefore puts the surface (20.1024) into the native layer's depth, where the game's layer has its
  cubes (20.0024) over the bed. R B4 point 4 and ledger ruling 3 assume the bed there. *(Ledger
  ruling 26 withdrew ruling 3 on this ground; ledger ruling 27 confirms the depth write.)*
- *The stored scene's depth is the end of the frame's [code: `scene_dump.h`; `clay_pass.cpp` line
  4778]*. R's "4.4% of footprint pixels agree with the game's depth copy" compares the game's copy,
  taken before `A17D`, with the native depth after the last draw. Once the native water writes depth
  that figure stays near 4.4% with everything right. Criterion 7 uses a plane taken before the water
  step.
- *Unread register components hold garbage [measured: `WE/b_water_d1_f1.txt`]*: `c129.z` is no
  number, `c129.w`, `c139.w` and `c140.w` are about 1e22 to 1e29, `c136.zw` and `c137.yzw` are
  denormals. Only the components a formula reads are judged or touched (sub-project 12's ledger
  rulings 34 and 38).
- *Some of the surface's registers move with time [measured: `WE/s_rows_155547.txt`]*: vertex
  `c115`, `c116` (the scroll: R B3) and vertex `c113.x` (0.10408 in frame 413, 0.10475 in 414), the
  value pixel `c116.x` holds (the fresnel bias, 0.104081). Everything is read from the draw; the
  native side has no clock.
- *Parts of the check already exist [code]*: `ucode_eval.py` interprets `kill_gt`, `setp_ne_push`
  and `setp_gt_push` ("the two water shaders", its docstring); `fog_check.py` already runs both water
  pixel shaders for their fog block against `light_formulas.fog` (plain form); `post_check.py` holds
  `D1CC` to `post_cube_position` (`light_formulas.py` lines 1411 to 1430).
- *The textures the pass decodes today [code: `texture_decode.h` lines 28 to 34]*: `k_DXT1`,
  `k_DXT2_3`, `k_DXT4_5`, `k_8_8_8_8` with their levels, `k_DXT3A` at its base level. Not `k_8`, not
  `k_DXN` (decoded on the CPU only, into the terrain's normals: `DecodeDxnLevel`), not `k_1_5_5_5`
  (the impostor step's own decoder).
- *The RHI can do this without a change [code: `rex/graphics/native_rhi.h`]*: a texture that is a
  render target and is sampled later, `kR8G8B8A8_UNORM`, `kR8_UNORM`, `kR8G8_UNORM`, `kD32_FLOAT`,
  the compare functions less and less-or-equal, source alpha and inverse source alpha, wrap and
  clamp. It has no border mode and makes no mip levels.
- *The sky step can be turned to another camera without a new pixel program [code: `clay_logic.h`
  lines 3702 to 3751; `fable2_native_shaders.h` lines 1244 to 1261]*: it draws one triangle and takes
  its ray from three vectors made on the CPU from rows 0, 1 and 3 of a view-projection
  (`SkyRayBasis`); it reads no depth, no eye and no inverse matrix. The exposure and the target's
  reciprocal size come from the frame block.

## Open questions, each with what settles it

| | Open | Settled by | If it cannot be settled |
|---|---|---|---|
| 1 | the formulas as formulas (R read them by hand; so did this design) | Design 2's machine checks; no run | no formula, no surface drawn (ruling 11) |
| 2 | the picture's channel order and orientation at the fetch | criterion 4 (offline, captures in hand) | the lookup stays unproven: phase 13b is not started |
| 3 | the normal map's stored levels (CAP `155547` holds its base level alone) and the level a fetch takes | discovery run W1 (Design 1); until then levels made by a 2 x 2 mean | criterion 4's bound is the looser one |
| 4 | whether the in-scene entry "cmdgap 840 after draw A7E4" is a second resolve to `0x13401000` | criterion 4 read apart for the pixels of `0x13401000` and of `0x13501000`; then W1's per-frame pictures | a limit; the native target does not depend on it |
| 5 | the views' viewport, scissor and clip registers | W1 | the native clip stays "the plane itself"; the difference is at the reflected shoreline alone |
| 6 | what `6954` / `A4A9` draws (one row before each far-terrain row of a view; not in the main scene) | reading its microcode and geometry in CAP `155547`; no run | phase 13c is not entered without it (Design 9) |
| 7 | which views the game blurs (two of four at the bridge, one of four at the third spot) | the captures in hand, then W1 and W2 | the controller rules blur for all or for none (Design 9) |
| 8 | the near branch: what slot 14 holds within 20 units; the shore fade on screen | discovery run W3, if a walk to the water exists | criterion 11 is "not decidable"; slot 14 stays zero (ruling 21) |
| 9 | whether any view of this save shows the glint | the replay on the sunward captures in hand (`160104`, `160251`), then W4 | criterion 12 is "not decidable"; user check |
| 10 | whether the water is drawn the same in another region | no run of this sub-project | a draw outside the measured shape is refused, counted |

**Execution reconciliation, 2026-10-12 (Task 19 preparation).** The original
questions above are retained. Tasks16–18 capture/static/quantitative gates are
complete with the named misses; fresh Task18 review approves. Task19 closing
commit and whole-branch reviews remain pending; this table claims no such approval.

| Question | What became of it | Evidence and remaining limit |
|---|---|---|
| 1, formulas | Settled on made inputs and captured rows. The plan's glint sign and saturated mix factor win. | Tasks 2–4, `t2_*`, `t3_*`, `t4_rows.txt`; console equivalence/fusion remains an arithmetic assumption. |
| 2, picture channels/orientation | Constant's own channels; no vertical flip. | Task 6, `t6_variants.txt`, `t6_sky_*.txt`. Criterion 4 as written has too few pixels at the named base; the substitute whole set and independent controls are disclosed. |
| 3, normal levels/LOD | W1/W2 supply nine stored levels; native upload is BC5, not rounded RG8. LOD follows the SDK's coarse derivative rule. | `t8_levels_083950.txt` / `t8_levels_084135.txt`: largest stored/mean RG8 difference 3, mean up to 0.561; levels 7/8 equal. Actual unrounded stored-level game-scene replays pass ruling 35's substitute in all four frames. `task8-continuation-report.md`; residual remains, not attributed to levels alone. |
| 4, in-scene `0x13401000` resolve | Remains unexplained. The dumped picture as sampled reproduces the earlier game scene within the disclosed substitute bound. | Task 6 replays; no proof of what the command-ring entry means. Native targets do not depend on it. |
| 5, viewport/scissor/clip | W1/W2 shadows show 256x256 viewport/scissor; clip-control `0x00080001` on export, terrain, sky and objects, `0x00080000` on flares. | `t8_vp.json` / `t8_pictures.json`; actual window and plane words have no shadow. Plane `z=h` remains a disclosed fallback; Task17 wires own cull and world.z-height clipping; Task18's fixed classifier finds all eight game-to-native cover shares below85%; no clipping equivalence claim is substituted. |
| 6, `6954` / `A4A9` | Settled as tessellation-factor memory export with position (0,0,0,0), 0% of the picture. | Task 15 listing/geometry, `t15_export_155731.txt`, `t15_view_*.txt`; not run through the interpreter's unsupported export/opcodes. Ruling 49 admits phase 13c. |
| 7, blur choice | The F186 row marks each blurred view: two at bridge, one at third; seven offsets, weight and integral coordinate measured. | Task15 formula/own-picture comparison; Task16 captures actual guest inputs and exact checks pass214records over four frames (blur1/1/0/0 bridge,1/0/0/0 third). Task17 now consumes/render-blurs these own inputs; Task18 final target metrics are disclosed above: C10game-to-native cover misses, reverse/common RGB passes. |
| 8, near branch/shore | No W3 after six bounded walks. Criterion 11 not decidable. | Task 14 focused counts and eye-to-AABB bounds. Some new slot-14 dumps are nonzero without a matching frame resolve; meaning remains unknown. Native slot 14 stays zero. |
| 9, glint view | No qualifying W4: held-sun capture113949 has no A17D row. Criterion 12 not decidable. | Task 14; earlier sun-side replay has no qualifying kept glint. User check101 judges motion/sparkle. |
| 10, other regions | Unseen; no closure inferred from one region. | User check105 and named refusal reasons. Water remains off by default. |

## Design

In the order work is done. Parts 1 to 3 and the decoders of part 6 are phase 13a and need no game run
but for the discovery runs themselves; parts 4 to 8 and 10 are phase 13b; part 9 is phase 13c.

1. **Discovery.**
   - *What the captures in hand hold* (CAP `155547` = D1 at the bridge, `155731` = D2 at the third
     spot, `221449` at the bridge on the fix wave's binary; each made under `--readback_resolve=full`
     with a stored native scene): every water row with its bank, pixel registers, literals, bools,
     fetch constants and state words; the twelve masks; the normal map's base level; the four
     reflection pictures as first sampled in the capture (frame 1's); the views' rows with their
     matrices; the game's scene (`0x199E0000`) and depth copy (`0x1A02A000`); the stored native scene
     of the paired frame. That is enough for Designs 2 and 3 and criteria 1 to 5.
   - *What is missing*: the pictures of frame 2 (one dump an address: R B1); the normal map's levels
     1 to 8; the views' viewport, scissor and clip registers (the `"vp"` member is written for
     out-of-scene draw rows at pitch 320 alone: `capture.cpp` lines 2193 to 2208); a view within 20
     units of water; a view with the glint; a native depth plane taken before the water.
   - *Discovery additions* (capture only, armed captures only; the pass draws nothing new):
     a 2D `k_8_8_8_8` texture at slot 13 or 14 of a `BEAD` draw is kept once per content
     (`DumpKeyedByContent`), so that each frame's pictures are in the capture, with a budget of 32
     frames of eight; a `k_DXN` texture at slot 2 of a `BEAD` draw is dumped with its stored levels;
     out-of-scene `draw`, `tess` and `indx` rows at pitch 280 gain the `"vp"` member, extended by the
     window scissor, the screen scissor, `PA_CL_CLIP_CNTL` and the six user clip planes;
     `tools/xdk_sigmatch/check_discovery.py` knows the new members. Nothing in the product reaches
     these paths.
   - *Discovery runs*, each two frames, `-BootSave Hero000`, the camera standing for at least five
     seconds before the capture, `--readback_resolve=full`, `--fable2_native_scene_dump=on`, the
     water's switch off:

     | | View | What it must show |
     |---|---|---|
     | W1 | the bridge, standing (the plan of the implementer rules) | four pictures in each of the two frames, every one with content below row 101; the normal map with nine levels; `"vp"` on the views' rows; the game's scene and depth copy; the stored native scene of the same frame. It is the baseline of every "before" figure at the bridge on this binary |
     | W2 | the third spot (`33:LS=0/-100/3500`) | the same for its four bodies (20.1024 twice, 27.52, 40.0) |
     | W3 | **the water's edge: conditional.** No plan in hand reaches it. The nearest mesh lies at x 61 to 96, y 204 to 228 (`WE/b_water_d1_f1.txt`), about 47 units in +y from the bridge's eye (82.07, 157.34), 30 units below it; whether the hero can walk there, and by which stick tokens, **the plan's author must find** (from the tree places and the terrain of D1, then by trial runs) | at least 1,000 footprint pixels within 20 units of the eye, at least 1,000 with a surface alpha between 0.0588 and 0.99, and whether any resolve of the frame goes to a slot-14 base |
     | W4 | **sunward over water: conditional.** Tried first on the captures in hand (open question 9); a run only if they have water towards the sun and no stored pair | at least 500 footprint pixels where the replay's glint term is above 5% of the pixel |

     If W3 or W4 cannot be had, the criteria on them (11, 12) read "not decidable" and go to the user
     checks; nothing else waits for them.
   - A view with the far lake on screen is not asked for (ledger ruling 4).
2. **The formulas** (no run). `water_vertex`, `water_pixel` and `water_alpha` in
   `tools/xdk_sigmatch/light_formulas.py`, transcribed from `BEAD`'s 17 and `A17D`'s 80 instructions
   and held to the microcode by a new `tools/xdk_sigmatch/water_check.py` with `ucode_eval.py` as the
   oracle, as the wind's and the impostors' were. What the microcode shows **[microcode, read for this
   design, not run: the check is what makes it a formula]**, with the literals `c254` = (0, 2, 0, 0)
   and `c255` = (0.5, -1, 1, -2):
   - *Vertex* (`BEAD`): `oPos = (c0 . p, c1 . p, c2 . p, c3 . p)` of the fetched position, nothing
     added (5 to 8); `o0.xyz` = the position; `o1.xy = p.xy * c117.xy + c115.xy`,
     `o1.zw = p.xy * c118.xy + c116.xy` (12, 13); `o3.xy` = the fetched pair, the mask's coordinate
     (17). The vertex fetch of the mask (slot 16, instruction 9), `o0.w` (through `c9`, `c113`) and
     `o2` (through `c119` to `c122`) are written and read by neither pixel shader: the native program
     has none of them.
   - *Alpha* (10 to 18): `m` = slot 1 at `o3.xy`; `alpha = 1 - saturate(c141.x - (1 - m) * c141.y)`.
     `A17D` kills where `alpha == 1`; the colour is computed only where `alpha > c141.w`, and the
     state's test keeps a pixel where alpha is greater than the reference. The output's alpha is
     `alpha`.
   - *Normal* (19, 20, 24, 25, 33 to 43): `a` = slot 2 at `o1.xy`, `b` = slot 2 at `o1.zw`, two
     channels each; `s = (2a - 1) + (2b - 1)`; `z = sqrt|1 - |2a - 1|^2| + sqrt|1 - |2b - 1|^2|`;
     `N = normalize(s.x / 2, s.y / 2, (c128.x / 2) * z)`. With `c128.x` = 0.3 the third component is
     made smaller, which tilts the normal further (R calls it "flattened").
   - *Reflection lookup* (21 to 32): with `q` the pixel's integral position,
     `u = (q.x * c0.z + c0.x - 1/2) * c31.x + 1/2`, `v` likewise with `c0.w`, `c0.y`, `c31.y`; slot 13
     at `(u + N'.x * c129.x / 2, v + N'.y * c129.y / 2)` and slot 14 at the same with `c130`, where
     `N'` = `s` (the summed pair before the normalisation). `c0.xy` is 0 in every row.
   - *Base colour* (37 to 47): `V = normalize(c19 - position)`, `F = saturate(1 - V . N + c116.x)`,
     `base = c127 + (c126 - c127) * F`.
   - *Mix* (48 to 59): `r` = slot 13's colour times `k` (`k` = 1, or slot 8's sample times `c139.x`
     plus `c139.y` under bool `b137`); `far = base + (r - base) * c131.x`;
     `near = base + (lerp(slot 14, r, saturate(F + c117.x)) - base) * c131.x`;
     `colour = lerp(near, far, saturate(c140.x * distance))`. With the captured values: half the base
     and half the reflection from 20 units on.
   - *Sun* (60 to 75, under bool `b136`, set in every row): a second normal
     `N2 = normalize(N.x, N.y, c136.x * N.z)`; `R = 2 (N2 . L) N2 - L` with `L = -c142`;
     `colour += pow(saturate(-(V . R)) , c138.x) * c137.x * c143`, the power by `exp(c138.x * log)`.
     `c136.x` (0.279) is not in the research. The signs of `L` and of the cosine are what the check
     settles.
   - *Fog* (76 to 87): the ground form by the amount `c28.x` with `c66`, `c67`, `c68`, slots 4 and 5:
     `light_formulas.fog`, to which `fog_check.py` holds this shader today.
   - *Out* (88, 89): `colour * c20.w`, alpha as above.

   **The checks**, all in `water_check.py`:
   - `formulas`: 2,000 of 2,000 made inputs: `oPos`, `o0`, `o1`, `o3` within `1e-5 * max(1, |value|)`;
     the pixel's colour within `1e-5 * max(1, |value|)` with `b136` clear and within 1e-3 relative
     with it set (a power of 128 multiplies the logarithm's rounding); the alpha within 1e-6; killed,
     skipped and reached pixels counted apart and equal in both (as `fog_check.py` counts them). The
     made inputs draw every register a formula reads from a range about its captured value, the mask
     over [0, 1] with 0 and 1 among the samples, both bools in both states, distances on both sides of
     `1 / c140.x` and of the fog's start.
   - *Mutations that must fail*: vertex: `c116` for `c115`; `c118` for `c117`; the coordinate from
     `p.xz`; two rows of `c0..c3` exchanged. Pixel: `c126` and `c127` exchanged; the scale about 0
     instead of the centre; `c130` for `c129` at slot 13; the two lookups' axes exchanged; `c128.x`
     left out; the fresnel bias with the other sign; the mix's `c131.x` left out; `near` and `far`
     exchanged; `c140.x` changed by 1%; slot 14 left out of `near`; `c136.x` left out; the power
     changed by 1%; `c142` with the other sign; `c137.x` left out; `c28.x` left out; `c20.w` left
     out. Alpha: `c141.x` and `c141.y` exchanged; `m` for `1 - m`; `F6D9`'s comparison for `A17D`'s.
   - `rows`: for every `A17D` and `F6D9` row of the captures in hand (both frames of `155547`,
     `155731`, `221449`, `155929`, `160104`, `160251`), at 200 pixel positions inside the row's mesh,
     with the row's own bank, pixel registers, literal table and bools, and one sampler callback for
     both sides that reads the capture's dumps: the same bounds; and **no component a formula reads is
     not finite in any row**.
   - `dead` (**the proof that `F6D9` draws nothing**): (a) on the microcode: for 2,000 made inputs
     whose `c141` satisfies `min(c141.x, c141.x - c141.y) >= 1e-6` with `|c141.x|`, `|c141.y| <= 4`,
     and `m` anywhere in [0, 1], `ucode_eval` reports `F6D9` killed in 2,000 of 2,000; as a control,
     with `c141.x - c141.y <= 0` and `m` = 0 it is not killed; (b) on the rows: the condition holds
     for every `F6D9` row of every capture; (c) on the dumps: no texel of any dumped mask gives an
     alpha of 1 with its row's `c141` (R's `b_masks_d1.txt`, made again by the check for every
     capture). The condition is the pure function `WaterOpaqueDead` of Design 5; the margin 1e-6
     stands above the rounding of the two single-precision steps at magnitudes up to 4 (2.4e-7 each).
   - `views` (the mirrored camera, Design 7): `WE/s_views.py` as a mode: every view of every capture
     against `diag(c31.x, c31.y, 1, 1) * M * Mirror(h)`, with `h` the height of the meshes that sample
     the view's picture: within 1e-5 relative in every row; mutations that must fail: the factor left
     out; the mirror about z = 0; `h` moved by 0.01.
   - `water_pins.py` writes the numbers the C++ mirror (`WaterMirror`, `WaterOpaqueDead`,
     `WaterViewRows`) and the HLSL are pinned to; the native mirror and its pins are held to the
     single-precision reference (sub-project 12's ledger ruling 26), the double one stays the
     formula's check against the microcode. `tests/test_water_formulas.py` holds a reduced count;
     `tests/test_water_hlsl.py` translates the HLSL text of the water programs line by line and holds
     it to `light_formulas` by value, as `tests/test_wind_hlsl.py` and `tests/test_impostor_hlsl.py`
     do ("a line no rule knows stops the translation"); `tests/test_water_literals.py` holds names and
     numbers across Python, pins, C++ and HLSL.
   - **A formula that does not close draws nothing** (ruling 11).
3. **The offline replays** (no run; captures in hand). `tools/xdk_sigmatch/water_replay.py`: the
   rows of a frame through `water_vertex`, rasterised by `foliage_replay.py`'s rasteriser at the
   sampling place fitted on the capture's own unmoving draws, depth-tested against the game's depth
   copy, shaded by `water_pixel` with the capture's dumps (mask, normal map, the picture of the row's
   own slot 13), blended over a bed colour at the row's alpha. It is the oracle of criteria 4, 7, 8,
   11 and 12 and gives, before any product code: the footprint as the game's formula has it (kept
   pixels and their alpha), each kept pixel's lookup place in its picture, the glint's share, and
   `WE/b_gap.py` again as the "before" of the current binary.
   - The bed under the water is not in any capture (the game's scene is after the blend). The replay
     judges only pixels whose alpha is 0.99 or more, where the bed is at most 1% of the pixel.
   - Until W1 the normal map's levels are a 2 x 2 mean of the decoded base, and the level is chosen
     from the coordinate's screen derivatives as the SDK's translator computes it (the plan's first
     step of this part reads that rule and names it).
   - The same script evaluates `light_formulas.sky` along the mirrored view's rays for each texel of
     a 256 x 256 picture (criterion 5).
4. **Capture: a bounded side block of water records.** **`DrawRecord` stays 616 bytes and is not
   changed**; a water draw keeps making the draw record it makes today (a `no_albedo` record: the
   frame's fog block is read at the first `F6D9` draw, FM line 3934, and that stays).
   - *Where*: in `RecordDraw`, on the branch a draw takes when its material's status is `no-albedo`
     (`capture.cpp` lines 3584 to 3586), in a frame with the water wanted (latched at the frame's
     boundary in `EndMainSceneFrame`, as the impostors' word is). A draw whose pixel shader has an
     albedo entry executes nothing new, with the switch on or off; a `no_albedo` draw (the water's 24,
     the fur's 15 and a few more of the bridge's 977 rows) executes one load and one branch more with
     the switch off. That is the smallest cost the place allows and is said here because "nothing new
     per draw" does not hold to the letter.
   - *Which draw*: the vertex shader's microcode hash is `BEAD...` and the pixel shader's `A17D...`
     (kind `surface`) or `F6D9...` (kind `opaque`); each literal the formulas read (`c254`, `c255` of
     the pixel shader; none of the vertex shader) is held to the shader object's own literal table
     once an object, as the wind's are.
   - *What a record carries* (`capture::WaterRecord`, about 600 bytes; at most **64 a frame**, a
     little more than twice the largest captured frame's 24): its kind; `place` = the `DrawRecord::seq`
     of its draw (the impostors' link: no index in the draw record); the hashes; vertex `c115` to
     `c118`; pixel `c0`, `c19`, `c20`, `c28`, `c31`, `c66` to `c68`, `c116`, `c117`, `c126` to `c131`,
     `c136` to `c143` through `ReadPixelConstant`; the pixel bool word of the last writer; the fetch
     constants of slots 1, 2, 13 and 14; the vertex fetch list (one stream, 5 dwords, position at 0,
     the pair at 3). The state words are the draw record's. A static assert, as the quads and the
     impostors have, that no register read from the bank can be named by a constant-buffer load.
   - *Rules of `MakeWaterRecord`* (pure, under test; the first that fails names the reason, counted
     under `water-record`): `unread`; `shader`; `literal`; `fetch-list` (not the measured stream
     shape); `register` (a component **that a formula reads** is not finite or outside its sanity
     bound by kind: a place within 1e6, a matrix term within 1e7, a factor, direction or exponent
     within 65,536; `c31.xy` in (0, 4]; `c131.x`, `c128.x`, `c136.x` in [0, 64]; `c141.w` in [0, 1];
     `c0.zw` equal to the reciprocal of the scene's size within 1e-6; the bounds are fitted to no
     capture: sub-project 12's ledger ruling 54); `scale` (`c20.w` not in (0, 64]); `state` (kind
     `surface`: anything but blend source alpha and inverse source alpha, depth tested and written
     with function 4 under the reversed range, alpha test "greater" with a reference in [0, 1], no
     alpha-to-mask, four colour channels; kind `opaque`: the measured words of `F6D9`); `fetch` (slot
     1 not a 2D `k_8` texture of at most 256 x 256; slot 2 not a 2D `k_DXN` texture of at most
     1024 x 1024 with its levels; slot 13 not a 2D `k_8_8_8_8` texture of 256 x 256 without a gamma
     sign); `shadow-term` (bool `b137` set: ruling 21); `fog` (`c28`, `c66` to `c68` or slots 4 and 5
     are not the frame's validated fog block, bit for bit); `over-limit` (the 65th).
   - **The frame's water is whole or it is today's** (ruling 11): one refused record of either kind
     and the scene holds no water record; the frame is then drawn record for record as today
     (`F6D9` as clay, `A17D` left out), and the reason is counted.
   - *Evidence a capture carries*: the frame row's `"water"`: `on`, `seen` (by kind), `taken`, the
     reasons' counts, and for each record its place, kind, registers as the floats' bits, fetch
     constants and bool word. `water_check.py records` holds each record to its `"draw"` row bit for
     bit.
   - *Log*: `[native] water: off|on` at install; every 300 frames `[native] capture: frame N water:
     seen S (opaque O), taken T | water-record in F of W frames {reason: n}`.
   - *The views' capture* is Design 9's (phase 13c). *(Struck by ledger ruling 26: "The cubes'
     capture is Design 8's.")*
5. **The switch, and the rule that removes the dead draw.** `--fable2_native_water` = `off` (default;
   also false, 0) or `on` (also true, 1). With `off`: nothing is read beyond the one branch of Design
   4, no record, target, texture, slot or program is made, the frame's slot plan is today's byte for
   byte, and every compiled program of the pass equals today's.
   - *The frame's verdict*, made before any target is bound (beside the sky's and the furniture's):
     `drawn`, or the first reason of: `off`; `no-records`; `light` (the frame has no light: the
     linear target's meaning); `opaque-live` (a record of kind `opaque` for which
     `WaterOpaqueDead(c141)` is false: `min(c141.x, c141.x - c141.y) >= 1e-6` and both magnitudes at
     most 4, in single precision); `pair` (the frame's `opaque` and `surface` records are not the same
     number); `plane` (a mesh that is not flat: Design 7); `view` (the records' rows are not the
     frame's one view-projection; JUDGED BEFORE `plane`, ledger ruling 46: a record that fails it
     has no height); `reflection` (no usable target: Design 7); `texture` (a mask or
     the normal map is not ready or was refused: Design 6); `order` (Design 8); `pipeline`; `slot`; `scale` (a record's
     `c20.w` is not the frame's output scale within a thousandth, as the impostors' rule has it).
   - **Only with the verdict `drawn`** are the frame's `F6D9` records left out of the colour pass and
     of the depth pre-pass, and the `A17D` records drawn by the water step. With any other verdict
     the frame is today's picture, record for record, and the reason is counted on the Water line and
     F3. So the dead draw is removed only under the switch, only where it is proven dead for that
     frame's own registers, and only together with the surface that replaces it.
   - `alpha-unknown` and `blend-unknown` fall by the water's counts in a drawn frame (12 and 12 at
     the bridge); with the switch off both lines are today's.
6. **The water's textures.** `TexFormatFromXenos` is not changed (a format entered there would
   texture records that are `format-unsupported` today, with the switch off: sub-project 12's ruling
   14).
   - *The shore mask*: `DecodeK8Level` in `texture_decode.h` (pure, beside `DecodeDxt3aLevel`): one
     byte a texel at `TiledOffset2D`, to `R8_UNORM`; false, with nothing written, for a source too
     small.
   - *The normal map*: every stored level through the existing `DecodeDxnLevel`, to `R8G8_UNORM`
     (ruling 20).
   - *The path*: the texture cache gains an entry that takes the decode kind from its caller (`k8`,
     `dxn`), used by the water step alone; residency, hashing and the dynamic rule are the cache's
     own (the masks and the normal map have no resolve in a frame). The sampler is the one
     `SamplerIndex` names for the fetch constant (the mask clamps, the normal map and the picture
     wrap); the fetched channels go through the fetch constant's swizzle in the program, as the
     impostor's picture does.
   - *Slot 14* is not a texture of the native draw: the near term takes zero for it (ruling 21).
   - A mask or normal map that is not ready, was refused (`texture-dynamic`, unread, too large) or
     failed to upload makes the frame's verdict `texture`.
7. **The native reflection target, phase 1: the sky.**
   - *Which picture a draw samples, and how that is known*: by the draw's own slot-13 fetch constant.
     The game keeps one picture a water body; the body has no other name in the capture. **A target is
     made for each distinct slot-13 base of the frame's `surface` records, at most 4 a frame** (four
     in every capture at the bridge and the third spot, two elsewhere; a fifth base is `reflection`).
     A record samples the target of its own base. Nothing is read from that address.
   - *The plane*: the height of the meshes that sample the base. A pure function (`WaterMeshHeight`)
     gives a mesh's height from the vertex bytes the geometry cache already holds, once a buffer:
     valid iff the third coordinate of every vertex of the draw's range lies within 1e-3 of the
     first's. All records of one base must agree within 1e-3, else the verdict is `plane`.
   - *The mirrored camera* (ruling 15): `WaterViewRows(M, c31.xy, h)` =
     `diag(c31.x, c31.y, 1, 1) * M * Mirror(h)`, with `M` the frame's view-projection, made on the CPU
     in double; held to the game's own views by `water_check.py views` (16 of 16 today). It is derived,
     not captured: phase 1 captures nothing of the game's reflection passes.
   - *Size and format*: 256 x 256, `kR8G8B8A8_UNORM`, one sample, no levels, cleared to 0: the game's
     resolved picture's size and format; the 8-bit saturation is the game's own. With them one
     256 x 256 `kD32_FLOAT` depth target, used from phase 13c on.
   - *Content*: the native sky step's own pixel program, byte for byte, with: the ray basis from
     `WaterViewRows` (`SkyRayBasis` reads rows 0, 1 and 3: so the sky's picture does not depend on
     `h`, and two bodies' targets are equal in this phase, which a test holds); **a second frame
     block** that is the frame's with the exposure 1 and the target's reciprocal size (the views are
     drawn at `c20.w` = 1); a new vertex program (`kSkyMirrorVs`) that adds the ray's third component
     as a clip distance, so that the half of the picture below the horizon stays 0 as in the game's
     dumps. The game's blur is not applied in this phase (ruling 17).
   - *When*: after the shadow maps, before the pre-pass, once a frame; the targets rest in the
     pixel-shader-resource state as the pass's others do.
   - *Lifetime*: an `EnsureWaterObjects` / `ForgetWaterObjects` / `DestroyWaterObjects` triple and a
     `water_objects_failed_` flag, made in the first frame whose verdict needs them; four targets and
     the depth are 1.3 MB.
   - **A frame without a usable target does not draw the water** (ruling 18): no sky (a frame
     without fog has no sky: FM line 4051), a failed target or pipeline, a fifth base: the verdict is
     `reflection` and the frame is today's. The surface is never drawn with an empty picture.
   - *Cost expected* **[inferred]**: four triangles over 65,536 pixels each and twelve small draws:
     well under 0.1 ms of GPU time at the bridge; criterion 17 measures it.
8. **The surface draw and the sun layer.**
   - *One step with its own layout and programs* (ruling 13; the impostors' step is the pattern):
     the record's constants at `b0` (one small slot a record, reserved only in a drawn frame), the
     frame's block at `b2` (its fog tables, eye and output scale), three textures (mask, normal map,
     the body's target), the four static samplers. `kClayWaterVs`: the position by the record's rows
     as the clay program forms it, the world position, the two coordinates, the pair. `kClayWaterPs`:
     `water_alpha`, the test, `water_pixel` in single precision in the reference's order with `q` =
     the floor of the pixel's position, the fog by the frame's tables (the ground form's function,
     unchanged), written as the lit draws write the linear target, times the frame's output scale;
     no component of a register that the formula does not read is touched.
   - *State*: the pipeline key from the draw record's own state words, as every record's: blend
     source alpha and inverse source alpha, the alpha test's reference, the cull, **depth tested and
     written** (the game's function 4 under the reversed range is the RHI's "less"; `F6D9`'s 6 was
     "less or equal"). The dispatch for this design speaks of a "depth test without write as the
     game's"; the state word has the write bit set (`0x00708746`), and ruling 14 follows the word.
     *(Ledger ruling 27: so ruled. The surface tests and writes depth; the dispatch's text was
     wrong.)*
   - *Its place in the colour pass*: before the first drawn record at or beyond the smallest `place`
     of the frame's `surface` records, after the steps already due at that index. The water's
     place counts as a blended record's does for the two rules that order the frame: the sky's place
     ("before the first drawn record that blends and is no terrain record": `SkyPlace`) and the
     coverage's tail (`PlanCoverageOrder`) come no later than it. Today the water's rows are not
     drawn and the sky's place lies just after them (912 of 916 records at the bridge: FM line
     4072), with no drawn record between; so among the drawn records the sky stands where it stood.
     In the bridge's order that gives the game's: the sky step, the moon, the clouds, the water,
     then the blended group, the late leaves, the tail and the glare as today. A pure function gives
     the place, under test with `PlanCoverageOrder`'s and `ColourStepsDue`'s arrangements; a frame
     in which the sky or the tail cannot be put before the water has the verdict `order` and is
     today's.
   - *The pre-pass and the shadows*: the water is in neither. The pre-pass's depth has one reader,
     the shadow mask, and the game's mask is drawn before any water row; with `F6D9` left out the
     mask within its 45 units is taken at the bed, as the game's depth copy holds it. The water casts
     no shadow and reads the mask only under `b137`.
   - *What lies under the surface*: the terrain the dead draw hid. The native view draws the bed
     today (the lake bed is terrain: FM line 1745) and overwrites it with clay; once the dead draw is
     gone the bed's own records show through the 0.3% the blend leaves and wherever the shore mask
     fades. Criterion 7 holds the bed's depth to the game's copy.
   - *Fog and the depth's readers* (checked in the code and in R's evidence): the native fog is per
     draw, from the drawn pixel's own position (`FogCoords`; no screen pass, no depth read), so the
     water is fogged at the surface as in the game and the bed under it at the bed; bloom, the
     composite and the ramp read no depth. Two things read the native depth: the mask step (the
     pre-pass's, above) and the sun layer (below). In the game the depth copy is taken before `A17D`
     and its three readers (the mask `0F99`, the `43ED` / `7F65` pair, the layer's `5D67`) see the
     bed; R's "anything that reads the native depth before the blended draws sees the bed" holds for
     the mask and does not hold for the layer.
   - *The sun layer's depth*: the layer's depth place is **not changed** (it stands after the
     water's rows: "Read for this design"). In a drawn frame the layer's depth therefore holds the
     water's surface at 20.1024 where a mesh's alpha passes the test, where the game's holds the cube
     at 20.0024 over the bed: 0.1 unit apart, at 120 to 250 units. That is what today's dead draw
     gives as well.
   - *The `D1CC` cubes into the layer's depth*: **struck by ledger ruling 26 (which withdraws
     ledger ruling 3).** The passage designed a cube record (recognised by the vertex shader's hash
     on a draw without a pixel shader at the layer's target, at most 4 a frame, whole or empty, with
     the reasons `cube {...}`), a depth-only program `kClaySunCubeVs` drawn after the cloud occluders,
     and their drawing whatever the water's verdict. None of it is built: where the water is drawn
     the layer's depth holds the surface (the paragraph above), and where it is not drawn the frame
     is today's (ruling 11), whose dead draw writes the surface's depth. No cube is captured, no
     program is added to the sun layer, and the Sun line is unchanged.
   - *Not drawn, counted on the Water line and F3*: the verdict's reasons. An RHI failure turns the
     step off for the run, not the pass. Line: `Water: drawn D of T, targets K, opaque left out O |
     not drawn {reason: frames}`. *(Struck by ledger ruling 26: "the Sun line gains `cubes C of
     S`".)*
9. **The reflection target, phase 2: the far terrain (phase 13c).**
   - *Entry conditions*, written in the ledger before its first task: phase 13b's criteria 6 to 8
     are judged; open question 6 is closed (what `6954` / `A4A9` draws, and its share of the game's
     pictures' texels that are neither sky nor black: if it is more than a tenth of them and cannot
     be drawn by a pipeline the pass has, the phase is not entered and goes to the roadmap); the
     rule "the k-th view of a frame is the picture at the k-th smallest slot-13 base" holds in every
     frame of every capture, by the resolves' destinations (`WE/b_resolves_155547.txt` and
     `_155731.txt`: it holds in both) and by the heights; the views' `7DDA` rows pass
     `light_check.py`'s far-terrain family with their own registers (`c20.w` = 1, the mirrored eye in
     `c19`, `c11.x` = 0.4704 where the main scene's is 1: `WE/s_viewsky_155547.txt`).
   - *Which records, by what rule* (ruling 19): **the game's own reflection-view draws**, captured as
     records of their own as the shadow casters are, not main-scene records drawn again. A view
     tracker (the pattern of `ShadowPassTracker`) follows the hooked draws before the main scene: a
     view is a run of draws on one surface that is neither the scene's nor a shadow map's and ends
     with the sky's draw (`F760` through the indexed builder) and its flares; of its draws, those
     whose pixel shader is the far terrain's (`7DDA`, the main scene's own far-terrain shader and
     light family) become `FrameScene::reflection_draws` (draw records with their terrain and light
     side blocks; at most **256 a frame**: 55 at the bridge, 52 at the third spot), with one header a
     view (first, count, whether a `BeginVertices` call with pixel shader `F186` followed it; at most
     8). Objects, flares and `A4A9` make no record.
   - *Tied to a target, fail closed*: view k belongs to the k-th smallest base; it is used only if
     every one of its records' rows equals `WaterViewRows(M, c31.xy, h)` of that base within 1e-5
     relative. A view that fails, an over-limit, a refused terrain record: that target holds the sky
     alone (phase 1's picture) and is counted `reflection: terrain`. A view's terrain is whole or
     absent.
   - *Why not the main scene's records mirrored*: the game's views use the far terrain for every
     patch with their own textures and light factor, and the two views of one height hold different
     sets (the same lookups find a silhouette in 30.3% of `0x13401000` and in 13.5% of `0x13501000`:
     `WE/b_noreflection_d1.txt`); and a mirrored redraw has geometry only where the main camera
     looks, which fails as soon as the camera pitches down at the water's edge.
   - *The draw*: the native far-terrain pipeline with its light inputs, into the body's target with
     the 256 x 256 depth, under the second frame block (exposure 1; the mirrored view's inverse and
     eye, for the fog), with the cull the record's own state word asks for (the game has it flipped
     already), and a variant of the terrain's vertex program that adds `z - h` as a clip distance
     (new program text; every program of before at its bytes). After the sky's triangle or before
     it: the sky tests less-or-equal at the far plane and writes no depth.
   - *The blur*: a view the game blurs (the header's flag) is blurred by one step transcribed from
     `F186`'s microcode, closed by `post_check.py` as the bloom's blur is, through one scratch
     target. If open question 7 shows the flag is not what decides, the controller rules blur for
     all or for none.
   - *Cost expected* **[inferred]**: about 55 terrain draws more a frame in the capture (the capture
     median, about 2 ms for 950 records today, by about a twentieth) and in the pass, into targets of
     65,536 pixels.
10. **Validation aids** (ruling 23; dump only, nothing in a frame without the dump): the stored
    scene (`.f2ns`) gains flag 8, **the scene's depth taken just before the water step** (one
    1120 x 720 plane, by the existing `DepthPackStep`), and flag 16, the frame's reflection targets
    (up to four 256 x 256 RGBA8 planes with their bases and heights). `scene_dump.h`'s version goes to
    2; the readers of version 1 keep working.
11. **Validation**: the criteria below. Every "before" figure is of an off-run of the same session
    and binary as its "after"; the research's figures are the expectation, not the baseline. Runs,
    all `-BootSave Hero000`, the camera standing before each capture or shot:

    | Run | What |
    |---|---|
    | none | criteria 1 to 5: machine checks and replays on the captures in hand |
    | W1 to W4 | Design 1: discovery captures, the switch off |
    | V-BRIDGE, V-THIRD | the view of W1 / W2 under `--readback_resolve=full` with the scene dump on and a capture: the switch `off` and `on`, post and bloom on; at the bridge once more with the sun's switch on, the water off and on; after phase 13c the on-runs again |
    | V-EDGE, V-SUN | the views of W3 / W4, off and on, if they exist |
    | WARP | the software adapter under the debug layer, 240 s at the bridge, the switch on with the sun, the coverage and the wind on; again for the build of phase 13c |
    | OFF | every switch at its default |
    | TIME | criterion 17's four bridge runs |

## Fail-closed rules (collected)

- No value read from the guest reaches a shader without having been read whole, a finiteness check
  and a bound, and only the components a formula reads are judged or touched. Reasons, counted in
  the frame's scene, on F3 and in the log: `water-record {unread, shader, literal, fetch-list,
  register, scale, state, fetch, shadow-term, fog, over-limit}`; the frame's verdict `{off,
  no-records, light, opaque-live, pair, plane, view, reflection, texture, order, pipeline, slot,
  scale}`; in phase 13c `reflection: terrain`. *(Struck by ledger ruling 26: the reasons `cube
  {unread, shader, literal, shape, register, over-limit, pending}`.)*
- The frame's water is whole or it is today's picture, record for record. The dead draw is left out
  only in a frame whose water is drawn.
- At most 64 water records, 4 targets, 256 reflection draws and 8 views a frame. *(Struck by
  ledger ruling 26: "4 cubes".)*
- The surface is drawn by the formula, with a target that holds at least the sky, or not at all: no
  flat colour, no empty picture, no sky colour "along the ray" in the fetch's place.
- A target's terrain is the game's own view's, whole, under the derived camera, or the target is the
  sky's alone.
- Nothing is read from the game's reflection pictures, and no address is added to any read-back list.
- With the switch off nothing of this sub-project is reached but one branch on the `no-albedo` path.

## Rulings

Rulings 1 to 8 are the controller's (ledger): 1 the scope and the native target, sky first, far
terrain second; 2 no SDK change; 3 the cubes (**withdrawn by ledger ruling 26**); 4 the far lake
later; 5 the switch; 6 shadow correctness stays out; 7 game launches wait for the user's leave; 8 the
other monitor's swap is not pursued. From 9 on they are this design's, each on the recommended
option. Ledger rulings 26 to 28 were made on this design: 26 the cubes are not drawn (ruling 22 below
falls); 27 the surface tests and writes depth (ruling 14 below stands); 28 phase 13a starts with the
plan, and entry into phase 13c is ruled after phase 13b's validation.

- Ruling 9: three phases, 13a without the game, 13b the surface with the sky, 13c the far terrain
  under the entry conditions of Design 9 - why: ledger ruling 7 leaves only work without the game for
  now, and phase 13c rests on four things no one has read (`A4A9`, the view's rule, the blur, the
  views' light) - cost if wrong: the sub-project closes at 13b with the far shore's reflection
  missing, which ledger ruling 1 prices.
- Ruling 10: water records are a side block of at most 64 a frame, of both pixel shaders, linked by
  the draw's sequence number; the draw record is untouched; the capture's branch stands on the
  `no-albedo` path - why: the draw record has no free index and must stay 616 bytes; the dead draw's
  `c141` is needed to prove it dead - cost if wrong: one load and one branch on about 30 draws a
  frame with the switch off.
- Ruling 11: the frame's water is whole or the frame is today's; a formula that does not close draws
  nothing - why: a missing patch with the dead draw gone is a hole to the bed in the middle of the
  lake, which neither the game nor today's view has; today's placeholder is the known state - cost if
  wrong: one refused record turns the whole lake back to clay for that frame, counted.
- Ruling 12: `F6D9` is left out only where `WaterOpaqueDead` holds for the frame's own `c141`, with a
  margin of 1e-6; a live `F6D9` (a region whose `c141` lets alpha reach 1) makes the frame today's;
  no opaque water form is built - why: no capture has a live one, and a form no run exercises is the
  kind of code sub-project 12 closed with on its "never exercised" list - cost if wrong: in such a
  region the water stays the placeholder, counted `opaque-live`.
- Ruling 13: one step with its own layout and programs, at the place of the first surface record -
  why: the clay layout's twelve parameters are taken, the impostors' step is the proven pattern, and
  the game's order (sky, moon, clouds, water, blended group) is the place's own - cost if wrong: a
  frame whose arrangement differs is today's (`order`).
- Ruling 14: the surface tests **and writes** depth, as the game's state word says, and the layer's
  depth place is not moved - why: the game writes it; what the game draws after the water is tested
  against the surface; moving the layer's depth before the water would reopen sub-project 11's order
  of tail, sky and late records for 0.1 unit of height - cost if wrong: the native layer's depth at
  the lake is the surface's, 0.1 unit above the game's cube; and the stored scene's last depth is the
  surface, which is why criterion 7 has a plane of its own. **Confirmed by ledger ruling 27.**
- Ruling 15: the mirrored camera is derived natively from the main view-projection, the draw's
  `c31.xy` and the mesh's height; it is not read from the game's reflection passes - why: it equals
  the game's rows within 1e-6 in 16 of 16 views; phase 13b then captures nothing outside the main
  scene; and it is the form the long-term goal needs - cost if wrong: a view with an oblique plane or
  another factor elsewhere; `water_check.py views` and phase 13c's tie would show it.
- Ruling 16: one target for each slot-13 base of the frame, at most 4, 256 x 256 `R8G8B8A8_UNORM`,
  one sample, at exposure 1, the half below the horizon left 0 by a clip distance; the sky's pixel
  program unchanged through a second frame block - why: the base is the game's own name for a body's
  picture and two bodies of one height have different pictures; the format and the exposure are the
  game's, measured - cost if wrong: a place with more than four bodies in view has the verdict
  `reflection` and today's lake; the game's four samples a texel are not reproduced.
- Ruling 17: phase 13b applies no blur - why: a sky alone has nothing to blur but the horizon's
  edge, and the bridge's lookups lie 14 texels and more below it (rows 115 to 141 against 101) - cost
  if wrong: a view that looks along the water to the horizon shows a harder edge than the game's.
- Ruling 18: a frame without a usable target does not draw the water; the surface is never drawn
  with an empty or a stand-in picture - why: half of the far colour is the picture; without it the
  lake is half as bright as the game's, a new wrong picture where today's is a known one - cost if
  wrong: a frame without fog (which has no sky) keeps the placeholder.
- Ruling 19: phase 13c draws the game's own reflection-view draws of the far-terrain family, captured
  as the shadow casters are; main-scene records are not drawn again mirrored - why: Design 9, "why
  not" - cost if wrong: about 55 records more a frame in the capture; the game's views stay in the
  frame as guest draws until the presentation work.
- Ruling 20: the mask and the normal map have decode kinds of their own beside the texture cache's
  table, and the normal map is decoded on the CPU to two 8-bit channels with its stored levels - why:
  the table must not change for other records; `DecodeDxnLevel` exists and is tested, and the map is
  static - cost if wrong: the interpolated block values are rounded to 8 bits (at most 1 / 510 of a
  normal's component); a block-compressed upload is the later step.
- Ruling 21: the near term takes zero for slot 14, and a draw with bool `b137` set is refused - why:
  slot 14 is zero in every dump and nothing resolves to it; `b137` is clear in every row and its
  term reads the game's screen mask - cost if wrong: within 20 units of water the surface is darker
  than the game's wherever the game does fill slot 14 (W3 says); a region with `b137` set keeps the
  placeholder.
- Ruling 22: **struck by ledger ruling 26.** It ruled how the cubes were recognised, held to a
  shape and drawn under the water's switch whatever the water's verdict, on the ground of ledger
  ruling 3, which ledger ruling 26 withdraws: the cubes are not drawn.
- Ruling 23: the stored scene gains the depth before the water and the reflection targets, in dump
  runs only - why: no plane in hand can be held to the game's depth copy or to the game's picture
  once the water writes depth - cost if wrong: a file format's second version.
- Ruling 24: every before-and-after figure pairs an off-run and an on-run of one session, one binary
  and one plan; measures are taken in the linear scene before post against the game's own frame of
  the same dump wherever such a pair exists; a threshold without a measured precedent is marked, the
  figure reached is reported whatever it is, and a verdict is "met", "not met as written", "not
  decidable" or "by a substituted measure" - why: sub-project 12's closing table - cost if wrong:
  more runs.
- Ruling 25: the cost's bound is "no steady native window below 30.0 fps, and at most 0.5 ms more
  mean frame with the switch on, in a session whose two runs of each configuration agree within 1.0
  ms", read beside the pass's own time for the step - why: nothing here waits for the GPU, so the
  expected cost is far below what a frame's spread can show - cost if wrong: a threshold without a
  precedent; "not decidable" twice goes to the controller.

## Success criteria

Each with its reference, checked in the evidence named, and the run that decides it. Evidence in
`out/native-evidence/water/`. *(no precedent)* marks a threshold set here without a measured one
before it. "Footprint" is defined at each criterion: the research's ("pixels where the native depth is
at 20.1024") is the placeholder's and does not exist in an on-run. Criteria 9 and the second parts of
10 are phase 13c's; 11 and 12 are **conditional** on a view.

1. *Formulas.* `water_check.py formulas`: 2,000 of 2,000 made inputs within the bounds of Design 2,
   every listed mutation fails; `rows`: every `A17D` and `F6D9` row of the six captures.
   **Reference**: the microcode dumps (exist, read) and `ucode_eval.py` (interprets the three
   operations these shaders need). **Decided by**: no run.
2. *The dead draw.* `water_check.py dead`: parts (a), (b), (c) of Design 2. **Reference**: the same;
   `WE/b_masks_d1.txt` (read: `alpha==1 0.00` in twelve of twelve). **Decided by**: no run.
3. *The mirrored camera.* `water_check.py views`: every view of every capture within 1e-5 relative;
   the three mutations fail. **Reference**: `WE/s_views_*.txt` (made for this design: 16 of 16 within
   1e-6). **Decided by**: no run.
4. *The formula with the game's own picture gives the game's scene (no precedent).* `water_replay.py`
   on frame 1 of CAP `155547` and `155731`, on the pixels the replay keeps with alpha of 0.99 or
   more, at least 5,000 of them: replay over the game's scene (`0x199E0000` of the same frame), mean
   red, green and blue each within **0.93 to 1.07**, and the correlation of the two luminances over
   those pixels at least **0.7**; as controls that the measure can fail, the same with the picture
   read upside down and with red and blue exchanged must each miss one of the two bounds. Read apart
   for the pixels that sample `0x13501000` (one resolve a frame) and `0x13401000` (open question 4):
   the criterion is decided on the first; a miss on the second alone is open question 4's answer,
   not the formula's failure. **Reference**: the dumps `13401000_95994CC3A59E386C.bin` and
   `13501000_47365B02B9AE386C.bin` and the scene `199E0000_*.bin` in `native_tex_20261011_155547`
   (listed; R viewed the pictures); the normal map's levels are missing there (open question 3),
   which is why the band is 0.07 and not narrower. **Decided by**: no run; made again on W1 with the
   stored levels and both frames.
5. *The mirrored sky against the game's picture (no precedent).* `light_formulas.sky` along the
   derived view's rays at exposure 1, against the dumped pictures of CAP `155547` and `155731`: on
   the game's sky texels (below the horizon row, not darker than 0.8 of the picture's sky, and at
   least three texels from any texel that is darker) each channel within **0.95 to 1.05** in the mean; and the first row
   with content within one row of the derived horizon. **Reference**: the same dumps; the native
   main sky reads 0.995 to 0.997 of the game's in the linear scene (FM "Validation (12), closing",
   criterion 9). **Decided by**: no run.
6. *Records.* In every frame of a capture at the bridge and the third spot with the switch on: each
   water record equals its `"draw"` row bit for bit (`water_check.py records`); `seen` = `taken` =
   the frame's `BEAD` rows (24 and 22 expected); no reason occurs. *(Struck by ledger ruling 26:
   "the cube records equal their `"indx"` rows (3 and 3)".)* `sizeof(DrawRecord)` is 616 (static
   assert). **Reference**: the row
   counts of R B1 (`WE/b_survey_*.txt`). **Decided by**: V-BRIDGE, V-THIRD.
7. *Cover, and the bed's depth.* In the on-run's stored scene, "water pixels" are those whose last
   depth is nearer than the plane taken before the water. (a) *Cover*: against the replay's kept
   pixels on the game's depth copy of the same frame: the water pixels farther than **one pixel**
   from a kept pixel, and the kept pixels farther than one pixel from a water pixel, are each at
   most **2%** of the kept pixels *(no precedent; the tolerance is stated beforehand because the
   shore is long: 8,533 of 18,536 footprint pixels lie within 3 pixels of land, and the native
   picture is the game's sampled a quarter pixel away)*. (b) *The bed*: on the water pixels, the
   plane taken before the water is within 1% of the game's depth copy on at least **80%** *(no
   precedent; the whole picture stands at 90.9% and the land within 3 pixels of the water at 86.4%
   by the same measure: `WE/b_gap_fw_on.txt`)*; the off-run's figure by R's measure (4.4%) is
   reported as the "before". **Decided by**: V-BRIDGE, V-THIRD.
8. *Colour on the lake after phase 13b, linear scene before post.* On the water pixels of criterion
   7 whose lookup, by the replay, lands on a sky texel of the game's own picture of that frame with
   every texel within 8 texels of it sky as well ("sky-sampling pixels"; at least 5,000 or "not
   decidable"): native over game, red and blue each within **0.92 to 1.08** and each nearer to 1
   than half its off-run distance on the same pixels; green within 0.92 to 1.08 (it stands at 0.98
   to 1.02 before: the band alone binds it); the hue within **6 degrees** of the game's (before:
   +17) *(no precedent)*. **The pixels that sample the far shore are reported, not bound**: at the
   bridge the same lookups find a silhouette in 13.5% (`0x13501000`) to 30.3% (`0x13401000`) of the
   footprint, and there phase 13b has sky where the game has dark hills; their share, their ratio
   and the whole footprint's ratio and percentiles are printed beside. **Reference**: R B2's table
   (`WE/b_gap_fw_on.txt`, `b_gap_d1.txt`, `b_gap_d2.txt`, read); the per-frame pictures need W1's
   capture code. **Decided by**: V-BRIDGE, V-THIRD.
9. *Colour on the lake after phase 13c.* On all water pixels: red, green and blue within **0.92 to
   1.08**, the hue within 6 degrees, and the 5th, 50th and 95th percentile of the native luminance
   each within **0.93 to 1.07** of the game's (before: 1.57, 1.61, 1.66 against 1.28, 1.62, 1.85:
   ratios 1.23, 0.99, 0.90) *(no precedent; objects, flares and `A4A9` are not in the target)*.
   **Decided by**: V-BRIDGE, V-THIRD after phase 13c.
10. *The reflection target against the game's own picture.* The stored targets against the capture's
    pictures of the same frame, by base. After phase 13b: criterion 5's two measures on the product's
    targets. After phase 13c: of the game's texels that are neither sky nor black, at least **85%**
    lie within one texel of a native texel that is neither, and the reverse; and on the texels that
    are silhouette in both, each channel within **0.85 to 1.15** *(no precedent)*. **Decided by**:
    V-BRIDGE, V-THIRD.
11. *The shore fade and the near branch (conditional on W3).* In V-EDGE: criterion 7(a) on that
    view; on the water pixels within 20 units, and on those with an alpha between 0.0588 and 0.99,
    native over game per channel **reported** with the bed's share named; bound only if the game's
    slot 14 is zero in W3 and at least 1,000 such pixels exist: then 0.90 to 1.10 *(no precedent)*.
    No such view: "not decidable", user check. **Reference**: none in hand (the nearest lake pixel
    of any capture is over 100 units away). **Decided by**: W3, V-EDGE.
    **Task 14 verdict (2026-10-12): not decidable.** Six bounded exploratory walks,
    logs 726–728 and 730–732, found no W3. Frame-1 kept/fading/near counts:
    10,059/727/0; 0/0/0; 0/0/0; 18,426/2,057/0; 33,871/3,410/0;
    10,347/318/0 (`out/native-evidence/water/t14_edge_counts.txt` and `.json`).
    The geometry/alpha-only replay uses the established rasterizer, sampler and
    formula on each frame's own game depth copy at declared (-0.25, -0.25);
    it matches the old full-replay control 155547 exactly (18,429 kept, 2,064
    fading, 0 near). Independently, eye-to-mesh-AABB lower bounds exceed 35 units
    for every captured mesh, pools included, so the near count has upper bound 0.
    No V-EDGE pair was made. Shoreline/near-water motion remains a user check.
    **Run qualifier:** e4/e5 remain SUSPECT under the work/swap-only heuristic,
    with no geometry/decode windows to establish the complete freeze signature.
    The controller accepts their static endpoints after fresh manual audit:
    guest clock/scroll/exposure inputs advance in the capture, emulated72/96s
    shots change, and guest draws/resolves continue to each run's end
    (`review-task14.md`, "Follow-up: manual audit", in the water ledger).
    Nonzero native redraws alone are insufficient. No continuous motion,
    uninterrupted physics, absence of brief hitches/user input or timing claim.
12. *The glint (conditional on a sunward view over water).* First the replay on CAP `160104` and
    `160251`; a view counts if at least 500 kept pixels have a glint term above 5% of the pixel.
    Then, in such a pair: native over game on those pixels within **0.85 to 1.15** in luminance, and
    with the glint's term left out of the replay the same measure must miss *(no precedent)*. No
    such view: "not decidable", user check. **Reference**: none in hand. **Decided by**: the
    captures in hand; W4, V-SUN.
    **Task 14 verdict (2026-10-12): not decidable.** W4 (log 729, capture
    `native_discovery_20261012_113949.jsonl`) has zero A17D rows in frame 1,
    hence exactly zero kept and qualifying glint pixels; all four held-look shots
    show bridge rails and sunward sky, no water. No V-SUN pair or no-glint colour
    control was made. Evidence: `out/native-evidence/water/t14_w4_audit.txt`,
    `t14_edge_counts.json`; the cancelled full glint tool's empty file is not
    evidence. The glint's sparkle remains a user check. All seven exploratory
    controller runs are RUN UNCHECKED (UserPresent), not timing evidence.
13. *The sun layer's depth at the lake.* At the bridge with the sun's switch on, on the layer's
    texels over the water pixels: the stored native layer depth (the plane before the tree occluders)
    lies at the lake's level (a height between 19.95 and 20.15) on at least **95%** with the water
    on *(no precedent)*. **No off-side bound is set**: with the water off the dead draw closes the
    same texels (sub-project 12's criterion 12 was written with an off-side bound that could not
    hold). *(Struck by ledger ruling 26: the second part, "What the cubes add is read where the
    surface is not drawn ... closed at the cube's level on at least 90% with the switch on ...
    fewer than 200 such texels: not decidable for the cubes". No cube is drawn; the texels over
    pixels that are no water pixel are reported for both runs and not bound.)* **Reference**: the
    game's layer depth as the depth copy with the cubes of `post_cube_point` (both exist; the game's
    side still has its cubes); `FM` "Stored native scene" for the native plane. **Decided by**:
    V-BRIDGE with the sun on.
14. *The land beside the water, and the pools.* Off-run against on-run of one session, on pixels
    that are no water pixel in the on-run and no placeholder pixel in the off-run: the land within 3
    pixels of the water, the regions "island in the lake" and "far peak" of
    `docs/native-renderer/fog-regions.json`, and the whole picture outside the lake: native over
    game moves by at most **0.005** between the two runs, **on the pixels common to both runs'
    sets** (sub-project 12's criterion 9 failed its letter on a set that changed). The pools at
    40.0, 40.474 and 27.52 (hidden or nearly so: 1.00 to 1.015 today) stay within 0.97 to 1.03.
    **Reference**: 0.976 and 1.032 for the land, 1.000 to 1.015 for the pools (`WE/b_gap_*.txt`,
    read). **Decided by**: V-BRIDGE, V-THIRD.
15. *Off is today's picture.* With the switch off: every compiled program of the pass equals the
    program of the commit before the first product commit, byte for byte (shader test); the slot
    plan is today's for every input and no target or texture of the water is made (tests); no line
    of the water beside `[native] water: off`, the frame row says `"water":"off"`, the
    `alpha-unknown` and `blend-unknown` counts are today's (12 and 27 at the bridge); and a defaults
    run's native shot against a run of the commit before, in the same session at the same width, in
    the bridge's regions: each within **0.02 of 1** (the measure sub-project 12 ended with), the two
    regions of moving leaf shadow left out beforehand; as the control that the measure can tell, the
    on-run must miss it at the lake. **Decided by**: tests; run OFF.
16. *Errors.* Before any real-GPU run of a build that changes bindings, targets or shader text: 240
    s on the software adapter under the debug layer at the bridge with the switch on and the sun,
    the coverage and the wind on: 0 `d3d ERROR` lines, warnings listed by id; **and the log shows
    the step drew** (`Water: drawn 12 of 12, targets 4`; *struck by ledger ruling 26: "`cubes 3 of
    3`"*) and the shot shows the lake (viewed), so that the programs shaded pixels. Once for phase 13b's build, once for 13c's.
    Nothing here depends on read-back, so the adapter's empty read-back (sub-project 12's ruling 43)
    does not stand in the way and no feed is needed. **Decided by**: runs WARP.
17. *Cost.* Shot width 1942, **the window on the usual monitor** (on the other one every frame is a
    multiple of 4.17 ms and the plain view already stands at 37 ms: R A4; a session there is no
    timing evidence), each run made alone, the head's uncapped default stated, one session at the
    bridge, post and bloom on in all: `off`, `on`, `on`, `off`. No steady 150-frame native window
    below 30.0 fps; the mean frame over steady native windows, averaged over a configuration's two
    runs: `on` over `off` at most **0.5 ms** *(no precedent)*, only if the two runs of each
    configuration agree within 1.0 ms, else "not decidable". **What the cost is expected to depend
    on**: the pass's own draws and the capture thread's reads, not the monitor and not the read-back
    mode: no forced resolve, no wait for the GPU and no entry in a read-back list is added. Because
    the off-runs may stand at the display's 60 Hz (sub-project 12's did: its figure was a lower
    bound), two measures that do not depend on the swap are read beside: the capture median with
    records on (a difference under a third of a millisecond is "not decidable": sub-project 10's
    ruling 42) and the step's own CPU time, which the Water line prints. Phase 13c: the session
    again. **Reference**: logs 690 to 693 of sub-project 12's binary (off 16.76 and 16.99 ms at that width). **Decided by**:
    runs TIME.
18. *Evidence rule.* Every number names its file; inference is marked; a run made with a user at the
    machine is labelled `RUN UNCHECKED`; a criterion not met as written is said to be not met.

**Executed criteria, 2026-10-12.** This table retains all eighteen original
numbers. “Met” binds only the stated measure; substituted measures, shortages
and missed bounds are separately named. Final Task18 measurements/review:
`task18-report.md`, `task18-controller-report.md`, `review-task18.md`;
tool/test commit9e6096f, runtime unchanged from Task17's4554F15C SHA. Earlier
reports and all `tN_*` evidence below are in the water ledger/evidence folders.
Whole-branch closing approvals are still pending.

| Criterion | Executed verdict | Evidence and practical limit |
|---|---|---|
| 1, formulas | met within the interpreter reference | `t2_formulas.txt`/`t3_formulas.txt`, made2000/2000 and mutations; `t4_rows.txt`:21784vertices/32800positions over164rows; Task8 adds14236vertices/18400positions over92own-frame rows. Console equivalence/fusion remains an assumption. |
| 2, dead draw | met under the captured fetch rule | `t2_dead.txt`, `t4_dead.txt`:82dead rows/41masks, maxalpha.997,16400captured positions killed; Task8 continuation46rows/23masks. No live opaque support claimed. |
| 3, mirrored camera | met | `t4_views.txt`:32/32within1.4e-6, three mutations fail; Task8 adds16views, max1.437e-6. |
| 4, game-picture scene replay | original deciding base not decidable; ruling35 substitute met | Task8 actual stored-level four own-frame replays RGB1.006–1.011,correlation.848–.863; every wrong variant misses. Named13501000sets191/191/137/136pixels remain below5000. `t8_scene_*`; no silent criterion rewrite. |
| 5, mirrored sky | colour met; horizon **not met as written** | Task8 sixteen own pictures RGB.996–1.005;10/16horizons within1row, six miss by2. `t8_sky_*`; tolerance unchanged. |
| 6, records | met | `t18_records.txt`:92/92 own water rows; `t18_reflection.txt`:214/214 independent reflection rows, four views/frame and own blur exact. DrawRecord616. Rows-only discovery excludes unlogged shader bindings. |
| 7, cover/bed | met with GIVEN prior sampling | `t18_*_cover.txt`:bridge.36%/.14%,bed95.4%;third.74%/.05%,bed94.9%. Place(-.25,-.25) declared, not refitted. Off placeholder4.4%/4.3% uses a different plane/set, report-only. |
| 8,13b sky-sampling colour | not decidable as defined | `t13_*_colour_given_place.txt`:1173/3200sky pixels below5000. Whole-footprint13b RGB1.150/1.141/1.107bridge,1.154/1.146/1.113third,hue-1.5both, report-only. C9 is a different all-kept measure. |
| 9,13c whole-lake colour | met | `t18_bridge_colour_all.txt`:18433pixels RGB.984900/.983597/.971809,hue-.629,P5/50/95ratios.948298/.995727/.992886. Third20824pixels RGB.981218/.979685/.968927,hue-.562,ratios.968218/.995434/.996679. Includes shore/low-alpha; no sky exclusion/5000floor. |
| 10, targets |13b met;13c **not met as written** | `t18_*_silhouette.txt`:all8 game-to-native shares36.5709–84.6931% miss85%; reverse97.2272–100/common RGB.921708–1.114320pass. R55 own-sky/own-blur classifier fixed before figures, no erosion. Four terrain targets/no fallbacks each spot. |
| 11, shore/near conditional | not decidable | Task14 six bounded routes726–728/730–732 found no near pixel/W3. No V-EDGE pair; slot14 stays nativezero. User check102. |
| 12, glint conditional | not decidable | Task14 held-sun729/113949 has zeroA17D/qualifying pixels; no V-SUN/no-glint control. Earlier replay has no qualifying glint; check101. |
| 13, sun-layer lake depth | met on13b measured plane | `t13_sun_layer.json`:99.9787% of4705lake texels at19.95–20.15,median20.10103. No new13c depth measurement claimed; D1CC cube portion struck by26, no off bound. |
| 14, land/pools | common land met; pools **not met as written** | `t18_*_land.json`:bridge changes-.000203806/-.000360667/+.000062067/+.000248211,third+.000464624/-.000356498 within.005. Pool off/on:bridge1.029566/1.055547 and1.036011/1.041345;third1.043455/1.048152 and1.214655/1.162564. Each misses.97–1.03 on at least one side. |
| 15, off invariants/control | source/log/flags/count parts met; old image not decidable | Task17 native41PASS/39old byte pins/off slots; Python1895pass. `t18_off_flags.txt`:741/747water off/no records,741/747/749no Water-step lines. Defaults749State12/27; third747initial11/26 with transient12/27 from other families; bridge741State rows absent. `t18_off_control.json`:EXPO/config/width mismatch, no old launch/resize/numeric acceptance. |
| 16, error/drawn/lake gates | met in prescribed parts |13b711/712 and13c739/740,240s WARP debug before corresponding hardware,0errors/lake drawn and viewed. Warnings1328both,1008in13cdump retained;319/320dump unpaired. No timing or uninterrupted motion inferred. |
| 17, cost/floor |13c frame cost/floor met; capture increment not decidable | `t18_timing.json`:68steady windows,min47.6fps,off17.932941/on17.928824ms,delta-.004117647;repeat.42/.118824<=1. All20shots1942wide, usual restored monitor, solo/uncapped. Capture+.1675<1/3ms not decidable; CPUmedian.077ms separately.13b floor40.6met/costnotdecidable due off-repeat2.0214ms. |
| 18, evidence/closing | execution evidence documented; closing gates pending | Each number names evidence, inference and RUN UNCHECKED are retained. Bridge/WARP whole-freeze NOT EXCLUDED; third/timing/defaults actual guest windows/no known signature seen. Initial Task19 closing commit and two fresh whole-branch reviews still pending; no approval claimed. |

User checks (from 101, in `docs/native-renderer/user-checks.md`): the lake in motion, native against
emulated (the scroll, the glint's sparkle); the shoreline on a walk along the water; the far shore's
reflection (after phase 13c); beams over the lake with the sun's switch on; the water in another
region and at another time of day.

## Implementation notes (deviations)

**Plan departures reconciled, 2026-10-12 (Task 19 preparation).** All thirteen
numbered departures in the plan are accounted for below. “Stood” describes the
executed route through Task18, with each measured miss retained above. Whole-
branch closing approvals are pending; execution review approval is distinct.

| Departure | Stood or changed | As executed |
|---|---|---|
| 1, no-albedo branch location | stood | Observer runs inside FillMaterial's no-albedo branch; record handoff occurs under the scene mutex in RecordDraw. Task 9. |
| 2, content dump key | stood | Task 1 adds the water row/slot rule and reflection budget beside address-based decoding. Other dumps keep their earlier policy. |
| 3, unshadowed clip words | stood | Only available shadows are emitted; absent window/user-plane words stay absent. W1 and the measured plane fallback are disclosed. |
| 4, flat mesh height | stood | WaterMeshHeight uses cached position bounds within 1e-3; no invented geometry read. Task 10. |
| 5, constant slots/reservation | changed | 432 became448 then464bytes: eighteen pixel registers and indexed vertex bounds. Two surface slots per record;13b153total becomes Task17's472 =128surface+4target+84frame+256terrain. Sky112bytes and blur128bytes at offset128 share each256-byte target slot. Reservation still uses scene verdict before final sky/texture/order verdict. |
| 6, verdict before target binding | stood | Scene rules first, remaining rules once inputs/place are known; dead twins are omitted only under the final drawn verdict. Tasks 10–12. |
| 7, mask coordinate buffer | stood | GeometryCache::Uvs decodes the actual pair from byte 12 of the 20-byte vertex through a copied material layout. Tasks 10–12. |
| 8, depth target timing | changed | Task11 allocates D32 and blur scratch with targets before Task17. Task17 now clears/uses D32 for complete own terrain and uses sharp scratch plus final sampled target for flagged blur;13b did not use scratch. |
| 9, stored-scene version | changed | Version2 only with a new flag, now 8/16/32; otherwise old version 1 bytes. Separate read_dump_water keeps earlier reader unpacking. Tasks 11/12. |
| 10, old executable comparison | changed | Ruling29 uses old 706/696 evidence; no old-folder copy/launch. The defaults 706 dimensions differ from new 724; old-image criterion 15 remains not decidable. |
| 11, W1/W2 order | stood | Oracle completed without a game; W1/W2 are first in the water validation batch, before real-GPU water on, on the combined 11/12 build. |
| 12, locked handoff branch | stood | One thread-local byte check remains for recorded draws; capture delta +0.010 ms is below the one-third-ms decision threshold. Task 13. |
| 13, machine settles readings | stood; spec readings changed | Task 3 decides `sat(+(V.R))` and `sat(c131.x)`; made controls discriminate. The original opposite sign/bare factor are not used. |

Phase 13b evidence and its limits are recorded in frame-map section 20,
"Validation (13b)" and "Limits (13b)", and the water ledger's
`task13-report.md`. Fresh full read-back pairs, logs 718–725, record binary
`2F1980B20F7D3D11`; their driver labels remain RUN UNCHECKED. Frozen first-batch
bridge713 is diagnostic only and is replaced by bridge718/719.

[measured] Common-land changes pass criterion14's0.005 limit in both views;
the absolute pool luminance ratios fail0.97–1.03 even in the off frames
(bridge40/40.474 about1.04; third27.52 about1.21 off/1.30 on). This is an
unmet bound, not an authorised change to its meaning. Criterion13's native
first layer plane has99.9787% of4,705 lake texels at19.95–20.15 with water on.
The main-by-value check applies ruling46's explicitly named nearer rounding;
nearest-rounded replay still misses blue. Coverage/colour use the existing
identical-view calibration(-0.25,-0.25), explicitly given, not a fresh fit.
Criterion7's required off-side before figures are4.4% of18,788 bridge pixels
and4.3% of21,103 third pixels with native placeholder depth within1% of the
game's depth copy (`t13_*_off_gap_given_place.txt`): the research's final-depth
footprint measure, report-only, distinct from the on-side pre-water bed plane.

Ruling29's old defaults baseline706 is1944x1139 while the current defaults
shots are1942x1136. Its image part remains not decidable as written; the new
same-width on/off lake control is sensitive (ratio1.1470). An old-executable
copy or launch is not made. Phase13c is required by rulings36/38, but its
reflection build, final validation and cost are not established by these13b
results. Ruling49 admits phase13c after the five entry conditions; Task16's
capture passes; Task17 static/debug evidence and Task18 final quantitative
verdicts are recorded below/above. No motion, shoreline fade or glint claim follows from
still shots. Default remains off.

[measured] Criterion 17, solo timing logs 733–736, recorded binary
`2F1980B20F7D3D11`: all 20 shots are 1942 wide, uncapped default logged,
POST/BLOOM/EXPO and no read-back argument; no known freeze or D3D error.
The controller attests the usual monitor and no competing work. Of 61 steady
150-frame native windows, none falls below 30 fps (minimum 40.6): floor met.
The off means 18.5350 and 20.5564 ms differ by 2.0214 ms, exceeding the 1.0 ms
agreement condition; on means 19.4131 and 18.9353 differ by 0.4778 ms.
Cost is **not decidable** despite nominal on-minus-off -0.3715 ms. Records-on
capture medians at native levels are 1.907 off / 1.917 on, delta +0.010 ms:
also not decidable at the one-third-ms threshold. Water CPU readings have
medians 0.0270 / 0.0275 ms, reported separately from GPU or frame cost.
Evidence: `t13_timing_results.json` / `.txt`, `t13_timing_config_extract.txt`;
original driver labels remain RUN UNCHECKED. Criterion 15's program/slot tests
passed in sampler_fix_native_full.txt (exit0,40PASS lines); full Python1877 OK
skipped7 in sampler_fix_python_full_escalated.txt. Controller attribution:
sampler fix e5edbda, release build SHA256
8A71AE4360DF0D9739579A0A2C3FC801C085DAFBC38D804B7DF963098451F612.
The old-image comparison remains not decidable.

**Task16 capture reconciliation, 2026-10-12.** Approved fresh review and
controller runs737/738 establish214/214 exact own reflection records:55per
bridge frame (views36/4/12/3, blur1/1/0/0),52per third frame (33/4/12/3,
blur1/0/0/0). Water checks pass48/48 and44/44. Both runs captured
SHA33A7E12AC139BB42ECFC3707CF52F9D4007F0BCDF3C9DACF51D6CDAD2AA05056;
the later E3CE579D relink has no runtime capture. Endpoint clocks, scrolling,
emulated pictures and continued draws/resolves are evidenced; viewed native
footprints remain sky-only before Task17. Both1296x759 readback drivers are
RUN UNCHECKED, and whole-run freeze is NOT EXCLUDED without aggregate guest
windows. No continuous motion/input-absence claim or cost comparison with
the differently configured1942-wide13b captures follows.

Fix2 corrects the independent checker's float32-before-unary handling of JSON
integer zero; strict bit comparisons remain and product source is unchanged.
Six focused regressions pass. The controller's earlier combined Task8/16 full
Python suite passes1885tests in536.686s with7skips; the final fix2 full suite
passes1886tests in527.866s with7skips, exit0 (`t16_fix2_python_full.txt`).
Task8/16 fresh reviews are approved; the combined ten source/test paths are
committed as `2fcd472`. Evidence:
`task16-report.md`, `review-task16.md`, `review-task8-continuation.md`,
`t16_static_audit.txt`, `t16_python_full_root.txt`, `t16_fix2_*`.
These capture/checker results alone do not close phase13c acceptance.

**Task17 reconciliation, 2026-10-12.** Eight source/test paths committed
`fb45e2b`; fresh source/runtime review approved. Complete own far-terrain views,
own mirrored frame blocks, cull/clip and captured seven-tap blur are wired.
Reservation472 replaces153; the eighth path fixes the stale literal assertion.
Independent case860 was strengthened after implementation, not claimed red-first.
Ruling53 resets the blur layout on device change; hot replacement is not tested
at runtime. Native41PASS; final Python1888tests in539.238s,7skips, exit0;
runtime SHA4554F15CCADD016573709ED81B6334D0888B963196F28C515B2B76C92C453768.

WARP739/740 meet criterion16's zero-error/drawn/viewed-lake gate with four
terrain targets. Warnings1328 and dump1008 remain; the latter is a nonblocking
unattributed duplicate-barrier observation. Capture319/stored320 is not paired.
GPU741/742 full-readback bridge pair has own same-frame437 target evidence,
reflection110/110 and water48/48 bit equal, four terrain targets, no fallback.
Viewed coast bands broadly match place; objects remain absent and native water
looks smoother. These static early-look judgments precede Task18; its final
C9met/C10not-met verdicts are recorded above. Driver labels RUN UNCHECKED and whole-run
freeze NOT EXCLUDED remain; no continuous motion/input-absence/performance claim.

Ruling54 defers four13c timing runs because current1024x768/100% inventory and
1296-wide shots cannot attest usual1942-wide/150% setup: C17(13c) not decidable,
no substituted cost/floor verdict. Ruling55 fixes the C10 classifier before
actual acceptance: independently derived own-frame/view sky with own blur,
maxRGB distance>=8/255 and exact nonblack, no horizon mask/erosion, all boundary
and native-only content, square one-texel comparison both directions and means
on exact common content. The tolerance is disclosed, not tuned to results.
Evidence: `task17-report.md`, `task17-runtime-report.md`, `review-task17.md`,
`t17_*`; the final eighteen-criterion table is above, whole-branch closing pending.

**Superseding rulings56/57, 2026-10-12.** Fresh Task18 review closes the
invalid-sky-reference issue before actual acceptance: consumed own-view
matrix/basis/rays are checked before baseline generation, including degeneracy.
The real-sky CLI regression is meaningful red/green; unread row2/w NaNs remain
permitted. Final tool suite1895tests551.878s,7skips passes; ResourceWarning
retained. No native/product/binary change. The user then identified display
sleep as the resolution change and restored7680x2160 at144DPI/150%. Ruling57
supersedes54's earlier deferral; four timing runs743–746 were made alone,
all agents idle, before third747/748 and defaults749 to use the restored setup.
All20timing shots are1942wide and viewed; uncapped pacing/guest windows audited.
Third's6/7 and defaults'8 actual guest windows supersede no missing-window
assumption for those runs; earlier bridge/WARP caveats remain. Final Task18
measurements and fresh source/runtime review are approved, tool/test9e6096f.
Whole-branch closing reviews remain pending; those approvals are not inferred.

## Limits known beforehand

- **The far lake** (`2B09`) is not drawn; beyond the meshes the native picture has the bed or the
  sky where the game has far water (at most 0.1% of any picture in hand).
- **The reflection holds the sky alone after phase 13b**, and after 13c the far terrain: never the
  objects, the flares or what `6954` / `A4A9` draws; one sample a texel where the game has four.
- **The game's views stay in the frame**: phase 13c draws the game's own view draws natively; the
  views are still issued by the guest, and their culling is the game's.
- **Two bodies of one height** are tied to their views by the order of the picture addresses, a rule
  seen in two places and held by the heights alone.
- **Slot 14 is zero** natively; what the game means it to hold is not established.
- **The shadow term at slot 8** (`b137`) is not modelled: such a draw is refused.
- **The in-scene resolve to `0x13401000`** ("cmdgap 840") is unexplained; the native target does not
  depend on it, the game's picture may.
- **The native layer's depth holds the surface** (20.1024) where the game's holds its cube (20.0024).
- **An opaque water** (a live `F6D9`) is not drawn: such a frame keeps the placeholder.
- **The normal map is 8-bit** natively, and the level a fetch takes is the host's, not the console's.
- **The picture's border**: the game's fetch wraps; a lookup beyond the picture's edge wraps natively
  as well, into rows that are black.
- **One save, late morning, three spots, a standing camera.** Other regions, other bodies of water
  (rivers, the sea, pools indoors), other times of day and the moving picture are unseen. Outside
  the measured shape a draw is refused and counted.
- **`43ED` / `7F65`**, which the game draws just before its water and which multiplies the picture
  from the depth copy, stays unidentified and undrawn.
- **The native picture is the game's sampled a quarter pixel away** (roadmap 5), as for every record.

**Known-limit reconciliation, 2026-10-12 (Task 19 preparation).** These rows
refer to the historical list immediately above; final phase13c metrics are in
the executed-criteria table, derived from Task18, not the offline proposal/early look.

| Known limit | What execution showed |
|---|---|
| Far lake `2B09` | Stays unbuilt; earlier tiny cover is retained as background evidence, not a new far-water validation. |
| Sky-only then terrain-only reflection | Phase 13b's sky-only lake lacks the shore as expected; mandatory13c terrain/blur is built and passes static/debug early look under Task17; Task18 final C9whole-lake colour passes, C10target cover misses on all eight. Objects/flares remain omitted under ruling 38. A4A9 is now identified as export with0% of the picture, so omitting it is not a missing colour layer. |
| Guest still issues reflection views | Stands: Task16 captures the guest's views rather than scheduling independent native culling. Runs737/738 pass214/214 exact own reflection records; Task17 renders the guest's complete far-terrain views, and Task18 final214own rows are exact; independent native scene/culling remains follow-up. |
| Equal-height bodies tied by order | Task 15 verifies ascending-base assignment in 10/10 frames, with 32/32 height checks; Task17 maps the k-th complete view to the k-th smallest target, with own-record matrix checks and no fallback in bridge742; third748 also has no terrain fallback; final C10target cover misses as named above. |
| Native slot14 zero | Stands as a native substitution. Task 14 finds some nonzero guest dumps without a matching resolve; their meaning and the near branch remain unknown after six failed approaches. |
| `b137` shadow term | Made-input formula checked; no captured row exercises it. Product refuses it as `shadow-term`; no game support claimed. |
| In-scene `0x13401000` resolve | Task 8's own-frame pictures change (302 W1 / 2,153 W2 texels at first base), and the lake matches by ruling 35. No separate resolve-moment picture versus later in-scene picture exists, so the command-gap meaning remains unexplained. Native route does not consume it. |
| Lake depth versus game's cube | Task 13 native layer plane has 99.9787% of 4,705 lake texels in 19.95–20.15, median 20.10103. The native surface/game cube height difference remains named; no D1CC work. |
| Live opaque water | All82old F6D9rows satisfy dead proof. A live opaque form is unexercised and keeps the placeholder by the refusal rule. |
| Rounded normal/LOD | Changed by ruling32: native BC5 keeps stored palette fractions and all levels; normal RG8 decoder is test-only. Native main passes the named nearer-rounding substitute, while nearest-rounded blue misses. Task 8's stored/mean census is measured; actual stored-level game-picture scene variants pass ruling 35 on four own-frame sets, with the residual still present. |
| Picture wrap/border | Wrap follows the accepted captured fetch; no new edge-wrap game view was separately judged. Unsupported sampler states are now refused by e5edbda. |
| One save/time/motion | Six route searches added, no near view reached; still one region at late morning. Motion and other places/times remain user checks101–105. |
| `43ED` / `7F65` | Remains unidentified and undrawn; not silently credited to water validation. |
| Sampling quarter-pixel difference | No MSAA/supersampling fix. Coverage/colour disclose the existing given(-0.25,-0.25)calibration; nearer-rounding selection is separately named. |

## What goes back to the roadmap

Item 3 closes as far as the criteria say. New or kept: the far lake's cube and its 165-instruction
shader (ledger ruling 4); objects and flares in the reflection, and `6954` / `A4A9`; the reflection
views issued natively (with roadmap 6: today they are the game's); other water bodies and regions,
an opaque water form, the term under `b137`; slot 14; the unexplained in-scene resolve to
`0x13401000`; the layer's depth place against a depth-writing blend; the normal map uploaded
block-compressed; the SDK's copy interface B2 as the user's separate decision (ledger ruling 2); the
monitor experiments (ledger ruling 8).

## Risks

1. The formulas were read twice by hand and never run: the first task of phase 13a may move a sign,
   a channel order or the lookup's orientation. Criteria 1 and 4 exist for that; nothing is built on
   the formulas before both are read.
2. Criterion 4's band depends on the normal map's levels, which no capture holds; if it cannot be
   decided on the captures in hand, W1 comes before phase 13b's surface.
3. Phase 13c may not be entered (`A4A9`, the blur, the view's rule): the sub-project then closes
   with 13.5 to 30% of the bridge's lake lighter than the game's, where the far shore should stand.
4. The frame-wide rule (ruling 11) makes one bad register flicker the whole lake between water and
   clay; the counters show it, a still picture does not.
5. The water's place meets sub-project 11's order of sky, tail and late records; a frame arranged
   otherwise is today's (`order`), and how often that happens is known only from a run.
6. *(Struck by ledger ruling 26: the risk that the cubes change almost no texel at the bridge. They
   are not drawn.)*
7. W3 and W4 may not exist from this save: criteria 11 and 12 read "not decidable".
8. Every run is likely `RUN UNCHECKED`; timing needs the usual monitor and a quiet machine.

## Decisions that are not this design's

- **The user's leave for game launches** (ledger ruling 7): W1 to W4 and every V, WARP, OFF and TIME
  run wait for it. Phase 13a does not.
- **Ledger ruling 3, in the light of ruling 14.** The research's ground for the cubes ("once the dead
  draw no longer writes depth, the native depth at the layer's depth place is the bed") does not
  hold for a surface that writes depth as the game's does. The cubes are designed in as ruled; what
  they then buy is the level where and when the surface is not drawn. The controller may keep them,
  strike them, or ask instead for the layer's depth to be taken before the water (ruling 14 advises
  against that). **Decided by ledger ruling 26: struck.** The cubes are not drawn; the layer's depth
  place is not moved.
- **The dispatch's "depth test without write"**: the state word has the write bit; ruling 14 follows
  the word. If the controller wants no write, criteria 7 and 13 change with it. **Decided by ledger
  ruling 27: the surface tests and writes depth;** criteria 7 and 13 stand as written (13 without its
  struck second part).
- **Entry into phase 13c**, and the blur for all or for none if open question 7 does not settle.
- **The default of `--fable2_native_water`**: the user's, after validation; off until then.
- **The SDK's copy interface B2** and the monitor experiments (ledger rulings 2 and 8): the user's.
- **What is pushed.**

## What the research has differently

Found while checking it for this design; the first two bear on a ledger ruling and on a criterion.

1. "Once the dead `F6D9` no longer writes depth, the native depth at the layer's depth place is the
   bed" (R B4 point 4; ledger ruling 3): `A17D` writes depth (R B1 says so itself) and the native
   layer's depth is taken after its rows (FM line 6730).
2. "The native depth at the layer's depth place equals the game's copy on the footprint (today
   4.4%)" (R B4's validation table): the 4.4% is of the stored scene's last depth, not of the
   layer's place, and a correct native water leaves it near 4.4%. Criterion 7 has a plane of its
   own.
3. The picture's orientation and channel order were not decided: `b_noreflection_d1.txt` reads
   30.3% and 28.9% for the two orientations, and the hand check of R B3 uses the sky's colour in the
   reverse order of the script's print ((0.17, 0.21, 0.33) against [0.3294 0.2039 0.1529]). The
   matrix says "not flipped"; criterion 4 decides both.
4. "13.5% (body 3) to 30% (body 1) of the lake's pixels" (R B4): the script runs the whole footprint
   (18,788 pixels) against each of the two pictures, not each body's pixels against its own. What it
   does show is that two views of one height hold different silhouettes.
5. "Two with a seven-tap blur" (ledger, R B1's table): at the bridge; at the third spot one view of
   four is followed by `F186` (`WE/b_views_155731.txt`).
6. "Flattened by `c128.x`" (R B3): the factor multiplies the normal's third component by 0.3, which
   tilts it further. Read, not run.
7. Not in R B3: the sun's second normal with `c136.x`; the lookup's `c0.xy` (0 in every row); that
   the lookup uses the pixel's integral position; that the vertex shader's mask fetch, `o0.w` and
   `o2` reach neither pixel shader.
8. "`6954` / `A4A9` ... one row before each far-terrain row": they are colour draws (mask `0xF`, no
   blend), 55 a frame in the bridge's views, and no main-scene row of CAP `155547` has that pixel
   shader (`WE/s_viewsky_155547.txt`); what they draw is open question 6.
9. R's option (iii) lists "sky, then far terrain, then terrain and objects": `7DDA` is the only
   terrain family in the views; there is no second terrain step.
10. Supported as written, checked again: the two shaders differ in one instruction; `c141` and the
    masks; the views' centres, heights and field of view; the order "after the sky, just before the
    far lake's cube"; the three cubes' heights; the four picture addresses and their draw counts.
