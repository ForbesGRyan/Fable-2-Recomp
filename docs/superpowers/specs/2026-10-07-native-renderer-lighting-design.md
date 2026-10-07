# Native renderer sub-project 7: lighting (sun, ambient and baked light, without shadows)

Status: design, 2026-10-07. Written under the user's standing instruction to proceed on the
recommended option at each choice and to log it as a ruling ("when these tasks are finished.
move onto the next subproject and use the recommended option until completion"). Nothing here
was approved section by section; the rulings are listed in "Rulings" so they can be corrected.

Sources: `docs/native-renderer/frame-map.md` (sections 4, 9, 12, 13), the materials spec
(`2026-10-06-native-renderer-materials-design.md`, what it moved to lighting), and a research
brief made for this spec from existing captures, GPU logs and shader dumps
(`.superpowers/sdd/2026-10-07-native-renderer-lighting/context-brief.md`, untracked; its facts
marked "new" were read by hand once and are not yet machine-checked: the first tasks of this
sub-project check them and write them into the frame map).

## Purpose

After materials, the native view draws the scene with unlit base colour and a fixed
screen-space "facet" shade. The emulated frame is lit: the dirt patch at the bridge is about
1.45 times brighter than the native one and warmer; rock and trunks have light and dark
sides; ground under trees is dark. The long-term goal is to draw the game without the
emulated GPU, so the native view has to compute the game's light from the game's own inputs
(constants, vertex data, textures in guest memory), not borrow the emulator's images.

This sub-project makes the native view's brightness and colour follow the game's ambient,
baked and sun light. Shadows are the next sub-project, because they need the game's shadow
passes replayed natively and stand on the lit pass built here.

## What the game does (as far as it is known)

- Every scene pixel shader, terrain and mesh, shares one constant layout: eye `c19`; ambient
  tint and exponent `c21`; additive light `c20.xyz`; output scale `c20.w` = 16; floor of the
  ambient factor `c22`; sun direction `c80` and colour `c87`; further lights `c81..c83`,
  `c88..c90` with range and cone constants; fog `c28`, `c66..c68` with two 1D tables; a
  screen-size sun mask (shadows) at slot 8 addressed through `c0`.
- Terrain (machine-checked in materials, Task 9):
  `colour = A * c11.x * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + A * sun and lights`,
  `L = c22 + (1 - c22) * t^2`, tf3 a baked light map over the region, `t` a clamped dot
  product of the normal with a vector near the sun direction.
- Static meshes (`0x8D90...`, 31% to 43% of mesh draws; read by hand): the same ambient with a
  per-vertex colour stream in the place of tf3, plus sun diffuse and specular times the mask.
- Instanced plants compute their ambient in the vertex shader; tree shaders read further
  constants. Neither is read yet.
- Colours are linear, written times 16 into float targets; a post chain (exposure, bloom,
  saturation, gamma) makes the displayed picture. The clay pass today holds display values in
  an 8-bit target.
- The device's pixel constant bank, which the capture reads, shows zero for `c0..c7`, `c80`,
  `c81`, `c87`, `c88`, `c109`, `c112`, where the game cannot have zero. So the sun's
  direction and colour are not readable today. Where the game writes them is not known.

## Scope

In:
1. **The light constants' source.** Find where the game puts the pixel constants the device
   bank misses, prove a run-time read of them against the emulated GPU's register file, and
   capture a per-frame light block (sun direction and colour, ambient tint, exponent, additive
   light, floor, the axis of the ambient factor, eye, output scale) plus what must stay per
   draw.
2. **Formulas, machine-checked.** The interpreter and checkers that live only in an ignored
   evidence folder (`ucode_eval.py`, `ps_check.py`, `vs_check.py`) become tracked tools with
   tests. Each light formula entered in a table is checked against the shader's microcode on
   random inputs before it is drawn.
3. **Vertex inputs.** Mesh normals, the per-vertex colour stream, the world rotation for
   normals; skinned and instanced normals through the transforms the pass already applies;
   terrain normals for every terrain draw (today only for steep-ground layers).
4. **A linear clay target.** The pass renders linear colour into a float target and a fixed
   output mapping makes display values. Terrain layers then add in linear space as in the
   game. With light set to 1 the picture must equal the materials picture.
5. **Lit shaders by family**, chosen by a table entry per pixel shader: terrain layer, far
   terrain, lit object, plant, tree. Ambient with the baked term and the factor L, plus
   unshadowed sun diffuse. The fixed facet shade goes away for lit draws.
6. **Validation** at the bridge, by the trees and at the third spot, against emulated shots
   bracketing each native shot in time, and offline against the microcode.

Out (each with where it goes):
- Shadows: the two shadow cascades, the screen mask at slot 8, the per-light shadow map at
  slot 9. Sub-project 8, directly after this one.
- Further lights (the 2- and 4-light shader variants are drawn with their sun and ambient
  only). Their constants are recorded if the source of item 1 yields them at no extra cost,
  for a later task.
- Normal maps, specular, the cube map and emissive blocks. A follow-up of this sub-project
  ("surface detail"), after shadows.
- Fog, sky, far haze, the post chain (exposure table, bloom), water, fur, particles: the
  post-processing and 2D sub-project.
- Two samples per pixel.

## Design

### 1. Light constants (the first task, and a gate)

A temporary diagnostic in the SDK logs the emulated GPU's pixel constant registers at each
draw of the main scene (the pattern of the materials sub-project's render-state proof,
frame-map section 13 "Render state": a log keyed by packet address, compared offline with
capture rows by a checker). It answers three questions: which registers the device bank
misses; what their values are in the two scenes; and, with a static search of the guest code
for the writers of those values, by which path the game uploads them (inference to test
first: constants set once per frame through a path that writes the command buffer without
updating the per-register shadow, as the shader literals do).

The outcome is a reader for the light block, proven like the render state: its values equal
the GPU's in every compared draw of both scenes, with the count stated. The pixel bool word
(`b128`, `b133..b135`) is proven or refuted by the same log.

Gate: if no guest-side source is found within the task, the implementer stops and reports.
The fallback to rule on then is to derive the sun direction from the readable fog axis `c66`
and the readable axis `c23`, with the sun colour calibrated; it is not chosen now.

### 2. Tools and formulas

`tools/xdk_sigmatch/ucode_eval.py` (interpreter of the dump text), `ps_check.py`,
`vs_check.py` are promoted with tests. New: `light_check.py`, which for each table entry
evaluates the entry's closed formula and the microcode on random inputs (lights other than
the sun set to zero, mask 1, fog off, the unread blocks' bools clear) and requires equality
within a tolerance, per family:

| Family (`"light"` in `ps-albedo.json`) | Shaders | Ambient | Sun |
|---|---|---|---|
| `terrain-layer`, `terrain-far` | the eight terrain shaders | `A * c11.x * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz)` | `A * c87 * sat(N . c80)` (layers) or `F * c11.x * c87 * sat(N . c80)` (far) |
| `object` | `8D90`, `2FD2`, and `648B`, `7892` with their extra lights off | `A * c27.w * (pow(vcolour, c21.w) * c21.xyz * L + c20.xyz)` | `A * c87 * sat(N . c80)` |
| `plant` | `A7E4`, `2605`, `165E`, `E99F`, `F525` | `A * ambient computed in the vertex shader` (to be read) | `A * c87 * sat(N . c80)` |
| `tree` | `00E0`, `7CD5`, `58D3`, `2FBE` | to be read | to be read |
| (none) | everything else | frame ambient with baked term 1 and L = 1; counted `light-unknown` | none |

The normal is the vertex normal (terrain: the normal map over the region, per vertex), not
the normal-mapped one; the checker therefore compares with the microcode's normal map input
set to "flat". The exact vector of L for each family comes from the vertex shader reading
(`o5` for objects, `o8` for terrain layers, `c23` for far terrain).

The entry carries the family and what the family needs beyond the frame block (for example
which vertex fetch holds the normal and the colour stream). The generator refuses an entry
that names a family without its inputs, in the strict style the materials fix wave gave it.

### 3. Capture and geometry

- `FrameScene` gains the light block, read once per frame at the first scene draw that sets
  it (the source of item 1 decides where).
- Per draw: the normal's fetch (format, offset; `FMT_10_11_11` first, others refused by
  name), the colour stream's identity, the world rotation (`c4..c6`), and the family. A draw
  whose light inputs cannot be read keeps its material and is counted `light-unknown`.
- `DrawRecord` must not grow: `TerrainMaterialInputs` (452 of 1,064 bytes in every record)
  moves to a side array indexed from the record, which the materials review listed as a
  follow-up; the light inputs of meshes go to the same kind of side array. Target: the
  records-on capture median at the bridge is not above the materials figure (1.32 to
  1.35 ms gated).
- Geometry cache: normals decoded with the positions, skinned by the bone palette and
  instanced by the instance transform exactly as positions are; vertex colours as a further
  buffer; the keys include what they must so a mesh drawn with two colour streams is two
  entries. Terrain: `(n.x, n.y)` for every terrain patch.

### 4. The pass

- Target `R16G16B16A16_FLOAT` (or `R11G11B10_FLOAT` if destination alpha is not needed after
  the terrain change: the implementer measures both and states the choice), linear colour.
  Every clay shader writes linear colour: `A = albedo^2` as the game does, alpha rules
  unchanged. Terrain layers add in linear space with their captured blend state.
- Output mapping, one full-screen step before the composite views:
  `display = sqrt(saturate(linear * E))`, `E` one number. With every light term replaced by
  1 and `E = 1` the result is the materials picture (a test mode keeps that comparison
  possible: `--fable2_native_light=off`). `E` for lit drawing is calibrated once against the
  emulated frame on one region (the dirt patch at the bridge) and then fixed in the code with
  its evidence; it stands in for the game's exposure, which belongs to the post chain.
- Lit shaders per family as in the table, sharing one HLSL light function per family with
  the frame block in a constant buffer. The facet shade remains only for flat (untextured)
  draws.
- Stats: an F3 and log line `Light:` with lit draws by family, `light-unknown` by shader,
  and the frame block's values.

### 5. Validation

- Offline: `light_check.py` passes for every entry; the terrain replay gains the lit form
  and shows a ground square with the captured constants.
- In the game (the `-BootSave Hero000` plans of the materials validation): the light moves
  during a run (`c21` changed by 0.35% over 8 frames in a capture), so each native shot is
  bracketed by two emulated shots a few seconds before and after, and the difference between
  those two is reported as the error bar.
- A new user check lists what a person should look at in town, at night and indoors, where
  no autoplay path leads.

## Success criteria

1. The light block read at run time equals the emulated GPU's registers in every compared
   scene draw of two captures (bridge, trees); the count and any exception are stated.
2. With light off, the linear pass reproduces the materials pictures at the three spots
   (share of pixels differing by more than 8 levels under 1% outside terrain blend bands).
3. At each of the three spots at least 90% of drawn draws are lit by a family; the rest are
   listed by shader as `light-unknown`.
4. In six named sunlit regions (dirt patch, grass beside it, bridge planks, a rock face, a
   trunk, the hero's shirt) the native mean luminance is within 25% of the emulated one and
   the hue within 10 degrees, after calibrating `E` on the dirt patch alone; the time drift
   between the bracketing emulated shots is reported beside each figure.
5. In five named pairs of surfaces (a sunlit and an averted side of the same object) the
   brighter one is the same in both frames.
6. Regions the game shadows (ground under the trees) are expected too bright and are listed,
   not counted as failures; they are the acceptance cases of sub-project 8.
7. 30.0 fps in live windows; view-off capture median under 0.5 ms; records-on capture median
   at the bridge not above 1.35 ms in a gated run (or, if only user-present runs exist, not
   above the same-session baseline); clay CPU and target memory reported.
8. Every formula drawn has passed `light_check.py`; nothing is called verified without a
   named log, capture or picture.

## Rulings (made by the controller; correct them where they are wrong)

1. Split: this sub-project is lighting without shadows; shadows are sub-project 8 — why: the
   shadow mask needs the game's shadow passes replayed (their draws are outside the capture
   bracket, their matrices are in no row) and multiplies a sun term that must exist first —
   cost if wrong: for one sub-project the native view is too bright where the game has
   shadow.
2. The game's own inputs, not the emulator's images: light is computed from guest constants,
   vertex data and guest textures — why: the goal is to leave the emulated GPU; the mask and
   shadow maps do not exist in guest memory anyway — cost if wrong: none seen.
3. A table entry per pixel shader names a light family with a closed formula checked against
   the microcode, instead of translating each shader — why: the shaders share one layout and
   four or five families cover over 90% of draws; the materials tables and their strict
   generator are the pattern — cost if wrong: a shader with an unusual term is lit slightly
   wrong until its family is refined.
4. Vertex normals only; normal maps, specular, cube and emissive later — why: they need
   tangents, two more textures per draw and unproven bool gates; brightness and hue come
   from ambient and diffuse — cost if wrong: rock and bark look smooth.
5. Sun and ambient only; the further lights of the 2- and 4-light variants are not drawn —
   why: the reachable scenes are daylight; the constants are unread — cost if wrong: lantern
   light is missing near the path.
6. A linear float target with a fixed square-root output mapping and one calibrated exposure
   number — why: light must be added in linear space, the layers then blend as the game's
   do, and the game's exposure belongs to the post chain — cost if wrong: brightness is
   right at the calibration region and drifts elsewhere when the game's exposure moves.
7. `DrawRecord` does not grow; terrain and light inputs move to side arrays — why: the
   capture time is the budget and the materials review already asked for it — cost if
   wrong: a refactor of record plumbing inside a feature sub-project.
8. Fog, sky and haze stay out although fog's inputs are small — why: they are screen-level
   effects with the post chain and the unsupported sky emitters — cost if wrong: far ground
   stays without haze for two sub-projects.
9. A temporary SDK diagnostic (GPU-side log of pixel constants) is allowed for the first
   task and removed at its end, as in earlier sub-projects — cost if wrong: none; it never
   ships.
10. Validation uses the three daylight spots reachable by autoplay; night, town and
    interiors are user checks — cost if wrong: a term that only matters at night is wrong
    unnoticed.

## Risks

- The constants' source is not found (the gate of design 1).
- Time of day moves faster than the bracketing shots can average.
- Normals in formats other than `FMT_10_11_11`, or skinned normals needing the inverse
  transpose where bones scale.
- The plant and tree families turn out to need vertex shader work as large as a family of
  their own; then they are split into their own tasks and criterion 3 is reported per
  family.
- User-present runs only: numbers under machine load; criterion 7 allows a same-session
  baseline for that case.

## Order of work

1. Light constants: diagnostic, source, proven reader (gate).
2. Tools promoted; formulas for terrain and objects checked; vertex shaders of plants and
   trees read.
3. Record side arrays (no behaviour change), then light inputs in capture.
4. Normals and vertex colours in the geometry cache.
5. Linear target and output mapping with light off (criterion 2).
6. Lit shaders: terrain, objects; then plants; then trees.
7. Calibration, validation, documents, user checks.
