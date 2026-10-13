# Native renderer: local lights (lanterns and torches)

Date: 2026-10-13. Branch `native-renderer-lanterns` (worktree `.claude/worktrees/lanterns`,
based on `00d0d29`, the water closing). Post-closure follow-up of the lighting sub-project
(sub-project 7; `docs/superpowers/specs/2026-10-07-native-renderer-lighting-design.md`), rebased on
`3c84229` (the campfire seven-light pairs and the water ripple fix, committed by the presentation session on 2026-10-13),
raised by the user on 2026-10-13: "it seems that the lanterns and torches aren't illuminating
nearby areas".

## Purpose

Light the native view with the game's local lights: the lanterns and torches that the game's
two-, four- and seven-light shaders add to the sun. Today the native pass draws every such
draw with the frame's sun alone (lighting design ruling 5: "sun and ambient only; the further
lights of the 2- and 4-light variants are not drawn ... cost if wrong: lantern light is missing
near the path"). That cost is now paid: near a lantern or a torch the native half shows no pool
of light while the emulated half does (user checks 32a, 38 and 59 describe exactly this).

Success: at a lit lantern or torch the native view shows the game's pool of light on the
ground, the props, the trunks, the plants and the leaves around it, in the game's colour and
reach, proven offline against the shaders' microcode for every light slot and in the game by a
paired capture at the Bower Lake bridge, where two lanterns stand by day.

## What the game does (as far as it is known)

From `docs/native-renderer/frame-map.md` section 14 ("Pixel constants", "The light set:
`c80` is not always the sun") and `docs/native-renderer/lighting-formulas.md`:

- The engine loads one light set of 36 pixel registers `c80..c115` per material group,
  61 to 65 times a frame; a frame has about four distinct sets. A set lists its lanterns
  first and the sun last: slot `j` has position or direction `c(80 + j)` (w = 1 a position,
  w = 0 a direction), colour `c(87 + j)`, and per-slot components of `c94` (1 when the range
  and cone apply), `c96` (the range term), `c(98 + j)` (the cone axis), `c105` and `c107`
  (the cone's cosine and slope) and `c112` (how much the screen mask shadows that light:
  one-hot at the sun). Slots 4 to 6 of the seven-light shaders use the second bank: `c84..c86`,
  `c91..c93`, `c95`, `c97`, `c102..c104`, `c106`, `c108`, `c113`. The vertex side carries the
  same set four registers lower (`c76..c107`) for the leaf shaders.
- The two-light shaders are drawn with one lantern in slot 0 and the sun in slot 1; the
  four-light ones with two lanterns and the sun in slot 2; the seven-light ones (the campfire's
  terrain and bark, 2f1001b) with the sun in slot 4. In all 1,520 + 944 captured
  `-multi` rows, the slots before the sun are lanterns and the slots after it are unused.
- At the bridge by day the two lanterns are at (93.75, 161.32, 42.38) and (115.88, 158.23,
  41.46), colour about (1.6, 0.75, 0.09), range term 0.25 (the light reaches four units) and
  no cone (`c105` = -1, `c107` = 1); they flicker (24 values over eight frames).
- The pixel form of light `j`, read from the microcode for light 0 and checked there
  (`sun_gate_multi`; `light_check.py --family *-multi`), "light 1 has the same form with `c81`,
  `c88`, `c99` and component y":

  ```
  V        = c(80+j).xyz - P * c(80+j).w          P the world position
  n_dot    = sat(N . normalize(V))
  shadow   = (1 - facing * mask) * c112[j]        (facing: the terrain layers' sat(8 N . c109); 1 elsewhere)
  range    = sat(1 - |V| * c96[j])
  cone     = sat((normalize(V) . c(98+j).xyz - c105[j]) * c107[j])
  gate     = (1 - shadow) * (1 + c94[j] * (cone * range - 1))
  term     = c(87+j).xyz * n_dot * gate
  ```

  The family's colour adds `A * term` where it adds the sun's `A * sun_col * n_dot_sun * sun_gate`
  (the objects, trunks and lit plants inside the `* c49` factor). With the sun in its slot
  (`c112` one-hot there, `c94` zero, w = 0) the sun's term is the one-light function; that is
  what the pass draws today.
- The leaves compute their light per vertex: for light `j`, `Ld = normalize(c(76+j) - P * w)`,
  `spot = 1 + c90[j] * (cone * range - 1)` with `range = sat(1 - |light vector| * c92[j])`,
  `cone = sat((Ld . c(94+j).xyz - c101[j]) * c103[j])`, and the front and back scalars of
  `leaf_light` times the light's colour; interpolators `o2` and `o3` are the sums over the
  lights, and the pixel shader multiplies both by its mask literals (`m1`, `m2`).
- Specular (objects), the per-light shadow map at slot 9 and the bool `b135` path are not
  read by the native pass and stay out (lighting design ruling 4; shadows design).

## Scope

In:
1. **Offline proof per slot.** `light_check.py --multi-lanterns`: every pair of the multi-light
   families at the game's sun slot with random lanterns in the slots before it, against the
   family's one-light function fed the sun plus the sum of the lantern terms. Tests.
2. **The light set per draw.** The capture reads the set at each draw of a multi-light family,
   once per load of the set (keyed by the upload the load model already numbers), and stores it
   in a side array of the frame. A draw's light inputs name its set.
3. **World positions for the lit objects.** The translation `c4..c6.w` and the inverse scale,
   today carried for the leaves alone, for every object and trunk; the vertex shader hands the
   world position to the pixel shader for every lit draw.
4. **The pass.** A per-frame buffer of lanterns; the lit pixel paths of the terrain layers, the
   objects and trunks, the plants, and the leaf vertex path add the lantern terms.
5. **Validation.** Offline (item 1), the software adapter under the debug layer, a paired
   capture at the bridge with a measure at the two lanterns, and user checks for night, the
   campfire and interiors.
6. **Documents.** `frame-map.md` section 14, `lighting-formulas.md`, `user-checks.md`,
   `roadmap.md`, the lighting spec's ruling 5 marked superseded.

Out (each with where it goes):
- Specular per light, the per-light shadow map, `b135`: as before (lighting ruling 4, shadows).
- Lanterns in the water's reflection draws: the reflection pass draws with the sun alone
  (the plane constant shares the draw word; see ruling 6).
- The swayed position: the lantern vector is taken at the vertex at rest, as every other light
  input is under the wind (frame-map section 18).

## Design

### 1. Formulas and the checker

`light_formulas.py` gains `local_light(P, N, pos, w, col, blend, range_scale, axis, cone_cos,
cone_scale, shadow_strength, mask=1, facing=1)` returning the term above (three channels,
finite for any input, `sat` where the microcode saturates), and `local_lights(...)` summing a
list. Every lit family function takes an optional `lights=()` (a list of three-channel terms
already multiplied out, or the sum) that is added to its sun term: `terrain_layer` adds
`A * (sun + lights)`, `lit_object` `A * (c27w * amb + sun + lights) * c49`, `plant`
`A * (vs_ambient + sun + lights)`, `terrain_far` has no multi form. The leaves: `tree_leaf`
takes `front` and `back` as three-channel sums (colour times scalar, summed over lights) in
place of the scalar pair and `sun_col`; the one-light caller passes `sun_col * front`.

`light_check.py --multi-lanterns [--trials N]`: for every pair of `MULTI_SUN_FAMILIES`, the
light set as `_sun_in_slot` makes it at the game's slot, and every slot before the sun a random
lantern: position in the world near the vertex (so that the range term is not always zero:
`|V|` from 0 to 2 / c96), w = 1, colour in [0.2, 2], `c94` = 1 (and in a third of the trials a
random blend in (0, 1), to hold the general form), `c96` in [0, 2], a random cone axis, `c105` in
[-1, 0.5], `c107` in [0.5, 3], `c112` = 0 at the lanterns (and in a third of the trials a random
strength, to hold the shadow form). The formula is the family's one-light function with the sun
plus `local_lights` over the lanterns. Pass: 400 of 400 at the family's tolerance for every
pair. The leaves' driver feeds the vertex shader the same set four registers lower. The output
is `out/native-evidence/lighting/lanterns/light_check_multi_lanterns.txt`. A pair that fails stays
sun-only in the pass (ruling 2) and is named in the documents.

Tests (`tests/test_light_check.py`, `MultiLanterns`): the mode runs on every pair and agrees;
with a lantern's colour wrong, its position moved, or the term dropped the check fails; the
pinned numbers of the C++ and HLSL mirrors (`PinnedForTheRenderer`) gain `local_light` cases.

### 2. The light set in the capture

`light_block.h` gains:

```
struct LocalLight {
  float pos[3]; float w;            // c(80+j); w 1 a position, 0 a direction
  float col[3]; float blend;        // c(87+j).xyz; c94[j] (c95 for slots 4..6)
  float range;                      // c96[j] (c97)
  float cone_cos, cone_scale;       // c105[j], c107[j] (c106, c108)
  float shadow;                     // c112[j] (c113)
  float axis[3]; float pad;         // c(98+j).xyz
};                                   // 64 bytes: the GPU's layout too (four float4)
struct LightSet {
  LocalLight lights[7];
  uint32_t count;                   // lanterns: the slots before the sun's, in order
  uint32_t sun_slot;
  bool valid;
};
```

`MakeLightSet(reg, have, light_set_this_scene)` (pure, tested): refuses as a whole (`valid`
false, counted by reason) when the set was not loaded this scene, when `c112` / `c113` is not
one-hot, when the slot it names is not a sun (w = 0, `c94` zero there), when a register of a
lantern slot was not read, or when any value of a lantern slot is not a number
of usable size (`LightValueUsable` for the colours and terms, `LightPositionUsable` for the
positions: the bounds the leaves' translation already uses). A lantern is every slot before
the sun's; a lantern whose colour is zero in all three channels is dropped (an unused slot),
the rest packed in order. Slots after the sun are not read (ruling 3). The set carries the
sun's direction; whether it is the frame's sun is judged in the pass, where the frame's block is
known (`MakeLanternBuffer`, ruling 4): at capture time the frame's block may not have been read
yet (it comes from a one-light draw, which may follow the multi-light ones).

`capture.cpp`: at a draw of a multi-light family (`PsLightSpec::multi`), or of the leaf family
(its pixel shader is one program for one, two and four lights and the table holds no light
count per vertex shader: with the sun in slot 0 the set has no lanterns and costs the read of
`c112` alone), `LightSetUpload()` gives the set's upload number (0: not loaded this scene, no set). A
per-thread table of the frame's sets (`upload -> index`, at most eight a frame as the block's
tries are) returns the index when the set was read already; otherwise the 31 registers of
slots 0..6 that the sun's slot requires are read through `ReadPixelConstant` (the slot bound
first, from `c112` alone, so that a two-light set costs 9 reads) and `MakeLightSet` runs. The
set is handed to the builder under the scene's mutex with the record (`g_builder.AddLightSet`,
giving its index in `FrameScene::light_sets`), and `LightInputs::light_set` (a `uint16_t`,
`kNoSideIndex` for none) names it. A set read in one frame is never the next frame's: the
per-thread table is keyed by the frame like `LightBlockTries`. Discovery rows gain
`"lset": {"upload", "sun_slot", "count", "reason"}`.

`FrameScene` gains `std::vector<capture::LightSet> light_sets` (spares like `lights`), and
the tallies `light_set_draws`, `light_set_reason[]` (none, not-this-scene, no-sun, unread,
unusable) for the `Light inputs:` line.

### 3. World positions

`MakeLightInputs` takes `world_pos` (the fourth components of vertex `c4..c6`) for every
`kObject`, `kTreeTrunk` and `kTreeLeaf` draw (today the leaf alone; the registers are already
read). `MakeClayObjectConstants` writes the translation into `rot[r][3]` and the inverse scale
into `leaf[1]` for every object and trunk as it does for a leaf (an instanced plant keeps its
bend constants there: its positions are in the world already). The vertex shader, for a lit
object or trunk, hands on `world = (M p) * leaf[1] + translation` in place of the object-space
`p.xyz` (the terrain's and the plants' `world` is the world already; the unlit paths do not
read it). The mirror text's plane height and the terrain's `world` are untouched.

### 4. The pass and the shaders

- **Buffer.** The clay root signature has no binding parameter left (12 of 12, 56 of 64
  words), so the frame's lanterns go into the frame constant buffer (cbuffer `Frame`, b2) as a
  tail after the fog tables: `float4 f_lanterns[256]`, four float4 a lantern (`(pos, w)`,
  `(col, blend)`, `(range, cone_cos, cone_scale, shadow)`, `(axis, 0)`), at most 64 lanterns a
  frame, filled per frame from `light_sets` in order (`MakeLanternBuffer`: set `i` -> `first[i]`,
  `count[i]`; a set whose sun is not the frame's, or past the room, is refused and counted). The
  buffer grows from 5,344 to 9,440 bytes (37 small slots); the vertex shader declares the tail
  after a pad across the fog's (the leaf path reads it), the lit pixel programs after the fog.
- **Draw word.** `ClayConstants` does not grow (root words). `light_mode` bit 11
  (`kLightModeLanterns` = 2048) says the draw has lanterns, bits 16 to 21 hold the first lantern's
  index and bits 22 to 24 the count (`LightModeLanterns(first, count)`: 0 for an empty range or
  one past the buffer). Set only with bit 2 (lit), from `LightSetOf(scene, inputs)` and the
  frame's plan, never for a reflection draw and not in the depth pre-pass's word. The shader reads
  the tail inside the lantern branch only; a test compiles the programs over the lantern bits.
- **Pixel paths.** `LocalLights(P, N, mask, facing, first, count)` in the light HLSL: a loop
  of at most seven over the lanterns, `local_light` statement for statement. The terrain layer
  adds it to its sun term with the layer's facing factor; the object, trunk and both plant forms
  with facing 1; the far terrain has no multi form. The world position is the interpolator
  `world` (section 3).
- **Leaves.** The vertex path sums the lights: `bend.xyz = sun_col * front_sun + sum col_j *
  front_j`, and a new interpolator `float3 back : TEXCOORD7` the back sums (with `bend.w` unused
  the layout stays one float4 plus one float3); the pixel shader's `LightLeaf` takes the two
  colour sums. Per lantern the vertex shader forms `Ld`, the range and cone from the same
  buffer entries (the vertex set is the pixel set four registers lower, taken as the sun is:
  ruling 5), and `spot = 1 + blend * (cone * range - 1)` into `LeafLight`'s `spot`.
- **C++ mirrors** (`clay_logic.h`): `LocalLight`, `LocalLights` pinned to the Python numbers;
  `LightSetOf`; `MakeLanternBuffer` (pure, from `light_sets`), `FormatLightDrawnText` gains the
  lanterns' counts.
- **Switch.** `--fable2_native_local_lights` (on | off), default on (ruling 7): off draws
  as today (no set is read; the bit is never set). Logged at start-up with the other light
  switches.

### 5. Validation

1. Offline: section 1's run, every pair 400 of 400; the native test suite and the Python suite.
2. Software adapter, debug layer: 0 errors with the switch on and off; the programs of before
   at their bytes with the switch off (the test's byte pin).
3. The bridge by day (`-BootSave Hero000`, the usual plan): a paired capture (the native scene
   dump and the game's stored linear scene) with the switch on and one with it off. Measure, in
   the linear scene before post, the mean colour in a disc of radius 2 units (projected) around
   each lantern's ground point, native over game: the ratio moves from its value with the
   switch off (expected well under 1 where the pool is) towards 1 with it on; report both
   lanterns, both captures, and the frame-wide measures of sub-project 12 unchanged outside
   the discs. The lanterns flicker, so the paired frames bracket a flicker (two captures a
   second apart; the spread stated).
4. Cost: the frame time at the bridge, switch on against off, within the usual bound
   (+0.5 ms; the loop runs on one draw in five at most, over at most two lanterns there).
5. User checks (user-checks.md, "Pending"): a street lamp at night, a torch indoors, the
   campfire (seven lights), and checks 32a, 38, 59 re-read with the new
   behaviour.

## Success criteria

1. `light_check.py --multi-lanterns`: every pair of the six families agrees 400 of 400 at every
   lantern slot of the game's set; the test suite holds the mode's refusals.
2. In the bridge capture: at each lantern's disc the native/game ratio with the switch on lies
   in [0.8, 1.25], and improves on the ratio with it off; outside the discs the frame measures
   are unchanged within their spread.
3. 0 debug-layer errors; the byte pin holds with the switch off; the native and Python suites
   pass.
4. Frame cost at the bridge, on against off: within +0.5 ms, or stated.
5. The documents name what is drawn, what is not (reflections, the swayed position, specular,
   the crowd pairs pending), and the user checks.

## Rulings (made by the controller on the user's standing instruction; correct them where they are wrong)

1. **A follow-up of sub-project 7, not a new numbered sub-project**: it fills ruling 5's gap
   with the lighting sub-project's own tools — why: the formulas, the checker, the light block
   reader and the family table are all in place; the work is plumbing one more register group
   through them — cost if wrong: the roadmap carries it as a post-closure entry rather than
   a numbered item.
2. **Only pairs the checker proves at every lantern slot are drawn with lanterns**; a pair
   that fails stays sun-only and is named — why: the pattern of controller amendment C2
   (`--multi-sun`): nothing lit from a guessed form — cost if wrong: a family stays dark near
   lanterns until its form is read.
3. **Lanterns are the slots before the sun's slot**; slots after it are not read — why: every
   captured set lists lanterns first and the sun last, and the shader's light count equals the
   sun's slot plus one at every draw seen; no per-shader light count is needed in the table —
   cost if wrong: a lantern after the sun (unobserved) is missed, or a shader drawn under a set
   with more lights than it reads shows a lantern the game does not (unobserved); both counted
   as `sun_slot` statistics in the `Light inputs:` line.
4. **A set is drawn only when its sun is the frame's** (bit-equal direction at the `c112`
   slot), judged in the pass (`MakeLanternBuffer`), not at capture — why: the draw is lit by the
   frame's sun today and the lantern terms belong to the same set; a set whose sun differs would
   mix two frames' light; at capture time the frame's block may not be read yet — cost if
   wrong: at a draw whose set holds another sun (unobserved: one sun value per frame in 60 + 60
   frames) the lanterns are missing, counted.
5. **The leaves take the pixel set for their vertex-side lights** — why: lighting ruling 15:
   the vertex set equals the pixel set bit for bit where read, and the leaves already take the
   sun that way; no run-time reader of vertex light constants — cost if wrong: a leaf draw whose
   vertex set differs from the pixel set is lit by the wrong lanterns, without a message (as the
   sun would be).
6. **No lanterns in the reflection draws** — why: the mirrored programs use the draw word's
   last float as the plane height and the reflection shows the sun alone today; the water
   sub-project's measures must not move — cost if wrong: a lantern's pool is missing in its
   reflection.
7. **Default on, with `off` restoring today's picture** — why: the user asked for the light;
   the form is proven offline before it is drawn, and the sun's path is untouched; the cost is
   bounded (one draw in five, a loop of at most seven) — cost if wrong: a visible change at the
   user's next build without their say; `off` is one switch away.
8. **The lantern range is packed into `light_mode`'s upper bits and the lanterns into the frame
   constant buffer's tail** — why: the root signature is at 12 of 12 binding parameters and
   56 of 64 words (`clay_pass.cpp`): no buffer binding and no root constant is free; the wind
   bits set the precedent for packing; `pad2` already carries three meanings — cost if wrong:
   a frame with more than 64 lanterns (sixteen sets of four) drops whole sets, counted.
9. **Lanterns are not drawn in the water's reflection, the swayed position is not used, and no
   specular**: carried as limits, not built — why: out of this follow-up's purpose — cost if
   wrong: none beyond what the documents state.

## Risks

- The slot forms for lights 1 to 3 (and 4 to 6) were read for light 0 and stated for light 1
  by inspection; a shader whose later slots differ fails the checker and stays sun-only.
- The bridge lanterns are lit by day with range 4 units: the pool may be faint against daylight
  and the measure's disc may show a small move. Then the measure is reported as it is and the
  user's night checks decide.
- The presentation session's crowd and building pairs (four- and seven-light object shaders,
  Murgo) are still uncommitted in the main tree: they get lanterns only after that work lands
  and `--multi-lanterns` is run on them (a line in the roadmap's follow-ups).
- Capture cost: at most eight set reads a frame of at most 31 registers each; the load model
  lookups are the cost the frame block already pays. Measured by the capture thread's time
  line as before.

## Order of work

1. Formulas and checker (`--multi-lanterns`), tests, evidence file.
2. `LightSet` reader (pure) and tests; capture plumbing; scene side array; discovery field;
   statistics.
3. World positions for objects and trunks; vertex shader `world`.
4. Lantern buffer, draw word, pixel and leaf paths, C++ mirrors, switch; shader tests.
5. Build, software adapter, bridge captures, measures, cost; documents and user checks;
   closing.
