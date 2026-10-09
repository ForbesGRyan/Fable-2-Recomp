# Native renderer sub-project 8: sun shadows (two cascades and the screen mask)

Status: design of 2026-10-08, not implemented.

Written under the user's standing instruction to proceed on the recommended option at each
choice and to log it as a ruling ("when these tasks are finished. move onto the next subproject
and use the recommended option until completion"). Nothing here was approved section by
section; the rulings are listed in "Rulings" so they can be corrected.

Sources: `docs/native-renderer/frame-map.md` (sections 4, 13, 14), `docs/native-renderer/lighting-formulas.md`,
the lighting spec (`2026-10-07-native-renderer-lighting-design.md`, what it moved to shadows), and a
research note made for this spec from existing captures, GPU logs and shader dumps
(`.superpowers/sdd/shadows-research.md`, untracked; its facts marked NEW were read once by
scripts in `out\tests\shadows-research\` and are not yet in the frame map: the first tasks of this
sub-project check them and write them there).

## Purpose

After lighting, the native view has the game's ambient, baked and sun light, but no shadow:
regions the game shadows are 1.3 to 2.3 times too bright (ground under the trees, the planks
under the rail, the ground under the hero). The long-term goal is to draw the game without the
emulated GPU, so the shadow has to be made from the game's own inputs, as the light was.

This sub-project makes the native view shadow its sun light where the game does: the game's
casters, drawn into two native shadow maps with the game's light matrices, and a native mask
with the game's mask arithmetic.

## What the game does (as far as it is known)

- Before the main scene, every frame, the game draws two sun shadow maps (cascades), each
  1024 x 1024 depth, with an orthographic light matrix: the near map covers 30 world units, the
  far one 70. At the bridge the two passes are 111 and 122 draws, about 460,000 indices.
- Casters are props, characters, bushes and trees. Terrain, grass and fern cards, water and fur
  are never drawn into the maps. The trees are drawn from vertex buffers no main-scene draw
  uses, so the maps cannot be rebuilt from the main scene's records.
- The light matrix of a pass is `c0..c3 * inverse(c4..c6)` of any of its draws (one matrix per
  pass to 1e-6). The same two matrices, mapped to texture space with a half-texel shift, linger
  in the pixel bank at every scene draw as `c36..c39` (near) and `c40..c43` (far). Their depth
  axis is the sun direction.
- Between the depth pre-pass and the first colour draw, two full-screen rectangles write one
  screen-size mask (1 = lit). Shader `0x0F99...`: world position from depth; distance `d` from
  the eye; where `d < c12.x + c12.z`, 12 taps of the near map on six literal offset pairs
  rotated per pixel by a 32 x 32 texture and scaled by `c77.x` (2.5 texels); where
  `c12.x < d`, 4 point taps of the far map with bilinear weights; the two blended over
  `c12.z` units; beyond `c12.y` a fade to the constant `c12.w` (0.486, not 1). Shader
  `0x1B4B...` is the far form alone for distant pixels. `c12` = (20, 35, 2, 0.486) at the bridge.
- Every lit family except far terrain multiplies only its sun term by the mask. Leaves use
  `0.6 * mask + 0.4`, applied once to the front light and twice to the back light. Ambient and
  baked terms do not read the mask. Far terrain scales its sun by `c11.x` = `c12.w` instead.
- Not known: the mask draws' texture bindings and state (they have no decoded capture row),
  the shadow passes' depth bias (polygon offset registers are in no row), the literal values
  `c248..c255` of the mask shaders, whether the cascade origin snaps to texels, and anything at
  night, in town or indoors (the per-light map at slot 9 is a 1 x 1 dummy in every daylight row).

## Scope

In:
1. **The unknowns of the mask and the bias**, settled by discovery: a decoded discovery row for
   the two mask rectangles, the polygon offset registers in the render state, the mask
   shaders' literals from the dumps. Written into the frame map.
2. **Formulas, machine-checked.** The two mask shaders as closed formulas checked against the
   microcode on random inputs; the casters' alpha forms; vertex-table entries for the caster
   vertex shaders that have none.
3. **Shadow passes in the capture.** The two passes become record lists of their own in the
   frame's scene, with a per-frame shadow block (the two matrices, `c12`, `c13`, the bias).
4. **Native shadow maps.** Two fixed 1024 x 1024 depth targets drawn from those records,
   depth-only, with alpha test where the game has it.
5. **A native mask and its use.** A depth pre-pass of the scene's depth-writing draws, one
   full-screen step that computes the mask from that depth and the two maps, and the lit
   shaders sampling the mask at their pixel as the game's do.
6. **Validation** at the bridge, by the trees and at the third spot, against the emulated
   frame, and offline against the microcode.

Out (each with where it goes):
- The per-light shadow map at slot 9 and anything that only exists at night, in town or
  indoors: recorded if a capture shows it, drawn in the further-lights work.
- The blended characters' own cascade taps (`0x401A...`) and water: those families are unlit
  today; they come with their lighting.
- Sway of tree casters (their vertex shaders are drawn without the wind deformation, as the
  main scene's trees are): the animation work.
- Terrain self-shadow: the game has none.
- Multisampling or supersampling: its own small sub-project after this one. Nothing built here
  may assume the game's resolution (see Design 5).
- Suppressing the emulated shadow passes: the suppression sub-project.

## Design

### 1. Discovery (the first task; needs one game run)

- `BeginVertices` draws get a decoded discovery row (shader pair, `"tf"`, `"pfull"`, `"rs"`
  with the stencil reference), discovery only: the product never needs the mask draws.
- `"rs"` gains the polygon offset registers (`0x2380..0x2383`), read by the path the render
  state proof of materials established; proven against the emulated GPU's register file the
  same way (a temporary SDK diagnostic, removed at the task's end).
- The literal tables of `0x0F99...` and `0x1B4B...` are decoded from the binary dumps offline.
- The existing GPU constant logs of lighting Task 1 are mined first, without a run, for
  `c12`, `c13`, `c46`, `c47`, `c77` over their 60 and 16 frames.
- One capture of 8 frames at each of the three spots answers: what tf6, tf7, tf13, tf14 of the
  mask draws are; `c77.x`, `c46`, `c47`; how the two rectangles share the screen; the bias; and
  whether the cascade matrices snap to texels while walking.

Not a gate: if the rotation texture is not readable from guest memory, the 12 taps are drawn
unrotated; if the bias registers cannot be proven, a fixed slope-scaled bias is calibrated on
the plank shadows. Either fallback is a ruling logged with its evidence.

### 2. Formulas and tables

- `ucode_eval.py` learns `getWeights2D` and fetches with texel offsets. `light_formulas.py`
  gains `mask_near(...)`, `mask_far(...)`, `mask_combine(d, near, far, c12, slope)`;
  `light_check.py` checks them against `0x0F99...` and `0x1B4B...` with synthetic depth maps.
- The lit families gain the mask argument in their checked formulas (it was fixed to 1) and
  are checked against the microcode with random mask values.
- Vertex-table entries for `0xA8A5...` (instanced bushes), `0x5781...`, `0x87E0...`, checked
  with `position_check.py`; the existing entries for `0xF160...`, `0xFBD3...`, `0x4325...`,
  `0x3A0F...`, `0x82F6...`, `0x5F44...` are reused.
- A caster alpha table: `0xF1EB...` (`tf0.a * c15.w`), `0x3347...` (`tf0.a`), `0x85A9...`
  (texture alpha with a distance dissolve and a dither texture: drawn as the texture alpha
  test alone, the dissolve stated as a limitation unless it is one comparison of constants).

### 3. Capture

- A draw outside the scene bracket whose surface pitch is 1040 and whose colour mask is 0 is a
  shadow-pass draw. The clear that precedes each pass tells the cascades apart (first pass of
  the frame = near, second = far; a third or later pass is counted and not recorded).
- `FrameScene` gains `shadow_draws[2]` (records of the same `DrawRecord` type, with the side
  arrays they need) and `ShadowBlock { float world_to_map[2][3][4]; float split[4]; float
  texel[2]; float bias[4]; bool valid; }`: the matrices from the pixel bank `c36..c43` at the
  first scene draw, `split` = `c12`, `texel` = `c13.x, c14.x`.
- Proof inside the capture, every frame: each pass's `c0..c3 * inverse(c4..c6)` (first record
  with readable world rows) must equal the block's matrix after the texture-space mapping
  within 1e-3; otherwise the block is invalid for the frame, counted `shadow-block`.
- `DrawRecord` does not grow. Shadow records are taken only when the native view is on and
  shadows are on. A record that cannot be read is skipped and counted by reason and shader,
  as in the scene.

### 4. Native shadow maps

- Two `D32_FLOAT` targets, 1024 x 1024 (the game's size, a constant of the pass, not derived
  from the window), cleared and redrawn every frame the block is valid.
- Depth-only pipelines of the clay vertex path (static, skinned, instanced) with the cascade's
  matrix in the place of the view-projection; the game's depth convention kept (reversed
  range, greater-equal), cull as captured (none for leaf cards), the captured bias.
- Alpha-tested casters bind their base texture and discard below the captured reference.
- Tree caster buffers enter the geometry cache through the same keys as scene geometry.
- A debug view (a composite token, not a key) shows the two maps and the mask.

### 5. The mask and the lit pass

The order of the game: depth pre-pass, mask, colour.

- **Depth pre-pass.** Every scene record that writes depth is drawn depth-only into the clay
  depth target before the colour draws, with the pipelines' vertex paths unchanged;
  alpha-tested and alpha-to-mask records bind their base texture and discard as their colour
  form does. The colour pass then draws as today (same depth function, depth still written);
  it does not rely on the pre-pass for correctness, only the mask does.
- **Mask step.** One full-screen draw into a single-channel target of the clay target's size:
  world position from the pre-pass depth and the frame's inverse view-projection (derived
  once per frame from the records' own matrices and required to be one matrix within a
  tolerance across the frame's static draws; otherwise mask 1 and `shadow-view` counted); the
  mask by the checked arithmetic (12 rotated taps of the near map, 4 weighted taps of the far
  map, the blend and the fade to `split.w`). Where the depth is the clear value the mask is 1.
- **Use.** The lit pixel shaders sample the mask at their pixel (linear, clamped, normalised
  coordinates of the native target) and multiply their sun terms as the checked formulas say.
  The binding table stays at twelve parameters: the albedo parameter becomes a pair (albedo,
  mask) and the registers after it move up by one. Unlit forms do not sample it.
- All screen addressing is in normalised coordinates of the native target; the rotation tile
  is addressed in the game's pixel units scaled from the native size, so the pattern keeps the
  game's scale at any resolution. The kernel radius is in shadow-map texels.
- The depth bias of the shadow maps is applied in the depth-only shaders (constant part in the
  vertex shader, slope part through the output depth), since the RHI's pipeline has no bias.
  No SDK change is needed by the product.
- `--fable2_native_shadows=on|off` (default on once the mask is verified in a run). Off, or an
  invalid shadow block, binds a white mask and skips the pre-pass, the maps and the mask
  step: the lighting picture. With the light off nothing of this is built.
- Stats: a log and F3 line `Shadow: on|off, block <valid|invalid>, near N draws (skipped
  {...}), far M draws (skipped {...}), split (a, b, c, d)`.

### 6. Validation

- Offline: the mask formulas and every family's form with the mask as an input pass
  `light_check.py`.
- In the game (the `-BootSave Hero000` plans of the lighting validation, with bracketing
  emulated shots): `region_compare.py` on the shadowed regions the lighting validation listed
  and on its six sunlit regions; named shadow edges measured for position; one walking run for
  shimmer; the shadows-off picture against the lighting picture.
- User checks for town, night and interior are added to `docs/native-renderer/user-checks.md`.

## Success criteria

1. In every frame of the captures at the three spots the pass-derived light matrices equal the
   block's within 1e-3 (count stated), and the bias read at run time equals the emulated GPU's
   registers in every compared shadow draw (or the fallback is ruled and stated).
2. `light_check.py` passes for `mask_near`, `mask_far`, `mask_combine` against both mask
   shaders and for every lit family with a random mask.
3. At each of the three spots at least 90% of the captured shadow-pass draws of each cascade
   are drawn into the native maps; the rest are listed by shader and reason.
4. The shadowed regions of the lighting validation (ground under the trees, planks under the
   rail, ground at the hero's feet where it is steady) come within 25% of the emulated mean
   luminance; the six sunlit regions stay within 0.05 of their lighting ratios.
5. Three named static shadow edges (rail post on the planks, a trunk's shadow on the ground,
   the bridge's edge) lie within 6 pixels of the emulated frame's at 1120 x 720.
6. With shadows off the picture equals the lighting picture (pixel-equal outside movers).
7. 30.0 fps in live windows; view-off capture median under 0.5 ms; records-on capture median
   at the bridge not above 2.2 ms on a quiet machine (1.44 to 1.50 ms without the shadow
   records); clay CPU, guest work and target memory reported against the lighting figures.
8. One clean 240 s run on the software adapter with the debug layer before any run on the
   real GPU of a build that changes bindings, targets or shader text.
9. Nothing is called verified without a named log, capture or picture.

## Rulings (made by the controller; correct them where they are wrong)

1. Native maps from the game's captured shadow-pass draws and a native mask (design (a) of the
   research note), not the emulator's mask or maps — why: the goal is to leave the emulated
   GPU; the mask's pixels do not exist in guest memory; the casters and matrices are all in
   evidence — cost if wrong: more capture and draw work than borrowing one texture.
2. The game's order (depth pre-pass, mask step, lit shaders sample the mask), not a mask
   composed after the lit pass through a second colour target and not the taps inline in each
   lit shader — why: the RHI has one colour target and no change to the SDK is wanted; the
   mask costs one texture in an existing pair instead of three bindings the full table does
   not have; a blended or alpha-tested draw takes the mask of the pre-pass surface at its
   pixel exactly as in the game — cost if wrong: the scene's depth-writing draws are
   submitted twice (clay CPU rises; measured in criterion 7); if that is too dear, the
   second-target form needs two colour targets in the RHI.
3. The game's sizes and arithmetic: 1024 x 1024 maps, the game's 12 + 4 taps, split distances
   and fade — why: the acceptance is the game's picture — cost if wrong: none seen; better
   filtering is a later choice.
4. Shadow maps and the mask's addressing do not depend on the game's resolution — why: the
   user wants supersampling or multisampling later — cost if wrong: none.
5. The matrices come from the pixel bank and are proven each frame against the pass's own
   draws — why: one read per frame, and a mismatch is detected instead of drawn — cost if
   wrong: a frame without shadows where the two disagree.
6. Temporary SDK diagnostics (GPU-side logs) are allowed in Task 1 and removed at its end, as
   in earlier sub-projects — cost if wrong: none; they never ship.
7. Fallbacks, not gates, for the rotation texture and the bias — why: neither changes the
   design — cost if wrong: banding in the penumbra, or acne and detached shadows until the
   registers are read.
8. Tree casters without sway, the bushes' distance dissolve possibly not drawn — why: the
   scene's own trees stand still and the dissolve is a detail of far bushes — cost if wrong:
   canopy shadows do not move; a far bush's shadow pops instead of dissolving.
9. Blended characters, water, the per-light map, night and interiors stay out — why: those
   families are unlit and no capture reaches those places — cost if wrong: a term that only
   matters there is missing unnoticed; user checks cover it.
10. Validation at the three daylight spots reachable by autoplay — cost if wrong: as ruling 9.

## Risks

- The mask draws' unknowns turn out different from the inference (for example the far
  rectangle covers more than distant pixels): the formulas of Task 2 are corrected from the
  discovery rows before anything is drawn.
- Depth bias wrong or unreadable: acne on sunlit surfaces, which criterion 4 (sunlit regions
  within 0.05) catches.
- The native depth and the shadow maps come from native geometry, the emulated ones from the
  game's: edges differ where native geometry differs (no sway, skipped draws).
- Capture time: about 230 to 290 more records a frame.
- The pre-pass doubles the submission of depth-writing draws, and the binding registers
  move: a change across the whole pass; the software-adapter run is its guard.
- Depth written twice by two shader forms (depth-only, colour) must agree, or the colour pass
  fails its own depth test in places: both forms share the vertex code, and the colour pass
  keeps a less-equal style test, not equal.
- User-present runs only: numbers under machine load; timing runs are made on a quiet machine.

## Order of work

1. Discovery: mask rows, bias registers, literals, the mined logs; frame map section 15.
2. Formulas and tables: mask shaders, the families with the mask, caster vertex and alpha entries (offline).
3. Capture: shadow records and the shadow block, proven per frame.
4. Native shadow maps and their debug view.
5. Depth pre-pass and the mask binding with a white mask (no picture change), then the mask
   step and the families' sun terms.
6. Validation, documents, user checks.
