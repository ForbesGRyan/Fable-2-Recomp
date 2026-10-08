# Native renderer sub-project 7: lighting (sun, ambient and baked light, without shadows)

Status: implemented 2026-10-08 (plan Tasks 1 to 9 and the final fix wave after the whole-branch
review, branch `native-renderer-lighting`); measured in `docs/native-renderer/frame-map.md` section
14, "Validation" and its "After the review: the fix wave": criteria 1, 3, 6 and 8 met; 4 met for
the six regions with one rectangle corrected (the first one straddled a shadow edge), and again
after the fix wave within 0.01 of the same ratios; 5 met as written in all seven pairs, four of
them on steady regions, but it cannot fail as written (with the light off the bridge pairs read
the same: 1.47, 1.50; the trees' pairs are albedo): what supports the light is the ratio's size
(rope pole 3.60 in the game's frame, 3.03 lit, 1.47 unlit); 2 met at the bridge in the normal
display state, at the third spot and by the trees only in a fallback display state; 7 met for the
frame rate (30.0 fps, and 30.0 in the renderer-off control) and the view-off capture; its
records-on clause met against the baseline of the same kind (run 277, master's code, 1.47 to 1.62
ms; this branch 1.44 to 1.50 at the bridge), its first form not measurable (no gated run exists).
The exposure reaches the lit draws only since the fix wave (ruling 37). What was ruled and built
differently is at the end, "Implementation notes (deviations)".

Design of 2026-10-07. Written under the user's standing instruction to proceed on the
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

## Implementation notes (deviations)

Written by plan Task 9 (2026-10-08) from the controller's ledger of this sub-project
(`.superpowers/sdd/2026-10-07-native-renderer-lighting/progress.md` and
`controller-amendments.md`, both untracked). Every ruling made while the plan ran is here, in
the ledger's order, each with why and the cost if it is wrong; then what was built differently
from the design above. The measured result is in `docs/native-renderer/frame-map.md` section 14,
"Validation".

### Rulings made during the work

Process and runs:

1. The plan is executed task by task with a fresh implementer and a reviewer for each, fix
   rounds, and a final review of the whole branch — why: the user's standing choice — cost if
   wrong: tokens.
2. Tasks 1 and 2 ran at the same time, and Task 2 wrote its formulas into a document of its
   own, `docs/native-renderer/lighting-formulas.md`, which the frame map links to — why: Task 2
   is Python and documents only, Task 1 edited the frame map — cost if wrong: two documents to
   keep in step.
3. Game runs of this sub-project may be made by the controlling agent with `-UserPresent
   -BootSave Hero000` while the user is at the machine (the user, 2026-10-07: "yes, lgtm");
   task agents never pass `-UserPresent` and ask for runs instead. Such runs are labelled
   `RUN UNCHECKED`; the freeze check per window stays the guard — why: the idle gate refuses
   while the user works — cost if wrong: every number of this sub-project comes from runs with
   a person at the machine, none from a gated one.
4. Before a user-present run the controlling agent samples the graphics card's load from other
   programs and does not launch above about 20% — why: three runs were spent measuring another
   game's load — cost if wrong: a wait.
5. Task 6 was finished while the machine was in a fallback display state (one 1024 x 768
   display, the monitor off): its comparisons were made against that night's runs exactly and
   against the earlier full-size runs by resampling; the absolute 30.0 fps and the exact
   comparisons were repeated later at full size — why: waiting would have stalled the work for
   hours — cost if wrong: none remaining for the bridge (runs 328 against 292, 333 against 328,
   342 against 333). The third spot's only comparison with the materials picture, 0.001%, is from
   that fallback display state, as the trees' 0.82% is, and was not repeated at full size; by the
   trees the light-off comparison at full size was never made (the view there does not repeat).
6. Three agents worked beside the leaf task's implementer on files of their own and without game
   runs (the region tool, the replay check, the research for shadows); the calibration stayed
   serial after the leaves — why: the user asked for more agents; calibration needs runs with the
   leaves lit — cost if wrong: time only. The frame times of the runs made while they worked are
   not used (they loaded the machine); the series was repeated on a quiet machine.

The light constants (Task 1):

7. The temporary diagnostic was widened beyond the design's list (the registers `c109`, `c112`,
   `c94`, `c80.w`, the further lights, the shaders' literals, vertex constants, two more bools)
   — why: Task 2 found the shaders read them — cost if wrong: none; it was removed with the
   diagnostic.
8. The tracking of constant uploads runs only while draw records are on; with the view off
   nothing is tracked, and the first frame after the view comes on has no light block and is
   drawn unlit — why: the view-off promise (under 0.5 ms, nothing added) — cost if wrong: one
   unlit frame at each view switch.
9. A sun of zero is "no block" (`light-block`), not a night — why: zero is exactly the wrong
   data that motivated the task, and a light set that was never loaded reads as zero — cost if
   wrong: a night scene is drawn unlit until a night capture shows what night holds (user check
   30).
10. Registers the game sets through the device's bank (everything outside pixel `c0..c7`,
    `c80..c115` and vertex `c40..c43`, `c76..c107`) are always read from the bank, whatever the
    state of the upload tracking — why: a register the game sets once would otherwise stay
    unknown for good — cost if wrong: a new upload site naming another range would be read
    wrongly; such a site is counted and logged once.
11. A register of the light set loaded in an earlier scene stays readable one by one; only the
    block as a whole is invalid there — why: the block is what the renderer consumes — cost if
    wrong: a tool reading single registers sees a stale value.

Families and inputs (controller amendments C1 to C4, which replace parts of the design):

12. The frame's sun is direction `c109.xyz` with the colour at the hot slot of `c112`, not `c80`
    / `c87` as the design says: those are a draw's first light, a lantern in the two- and
    four-light shaders. The block is read once per frame at a one-light draw, never at the first
    scene draw — why: Task 1 proved it on the emulated GPU's registers (the review recomputed it
    on 29,548 and 28,720 colour draws) — cost if wrong: none seen.
13. Seven families instead of the design's five, each belonging to a pair of pixel and vertex
    shader: `terrain-layer`, `terrain-far`, `object` (baked colour from a stream or from probe
    weights), `tree-trunk`, `plant`, `plant-lit`, `tree-leaf`. A pair the checker did not run is
    `light-unknown` — why: the baked term is made in the vertex shader, differently per vertex
    shader — cost if wrong: a pair missing from the list is drawn unlit and counted.
14. The two- and four-light variants are drawn through their family's one-light function with
    the frame's sun, and only where `light_check.py --multi-sun` shows that the microcode equals
    it with the sun in the slot the game uses — why: design ruling 5 (no lanterns) without
    guessing — cost if wrong: such a draw lacks its lantern's light (it does, by design).
15. No run-time reader of vertex-side light constants: the pass uses the copies proven against
    the GPU (vertex `c18` = pixel `c21` with the exponent times 2.2, `c54` = pixel `c20`,
    `c19.xyz` = pixel `c23.xyz`; for leaves vertex `c76` / `c83` = the frame's sun) — why: those
    registers are uploaded past the bank — cost if wrong: these identities were seen in daylight
    scenes of one area only, and a scene in which one fails is lit wrongly without a message.
16. Shader literals are fixed per shader from the dump; bool flags are never read from the
    device (its bool word was refuted) and the blocks under `b133`, `b134`, `b135` are not drawn
    — why: no proven reader — cost if wrong: emissive, cube-map and per-light shadow terms are
    missing (they are out of scope anyway).
17. Probe colours are per-draw constants and the geometry cache stays mesh-intrinsic (normals,
    and per vertex either the stream colour or four weights and a set index); Task 5 was split
    into 5a and 5b and the order became 3, 4, 5a, 6, 7, 5b, 8 — why: the same mesh is drawn with
    different probe sets — cost if wrong: none seen.

Records and normals (Tasks 3 to 5):

18. Scene buffers are reused between frames, not allocated per frame — why: the plan said so and
    a benchmark showed the allocation cost as large as the gain the task existed for — cost if
    wrong: a small spare list.
19. Skinned normals are `normalize(B n)` with the blended bone matrix as it is, without the
    inverse transpose and without the game's tangent correction — why: it is the game's own
    arithmetic and the closer of the two on the captured meshes — cost if wrong: up to 12.8
    degrees from the game's normal at single vertices (377 of 14,620 beyond 2.6 degrees by the
    trees); the tangent step is a follow-up.
20. Objects, trunks and instanced plants use `N = normalize(M n)` for every kind of world matrix
    but a degenerate one (unlit, counted) — why: the same principle; for rotations and mirrors it
    equals the inverse transpose — cost if wrong: a skewed object (none seen) is lit slightly off
    perpendicular, as in the game.

The linear target (Task 6):

21. Frame rate first: the target is `R11G11B10_FLOAT`, 32 bits a pixel, with the mapping as a
    step of its own — why: in one night's experiment it was the float form that cost no more
    than the old 8-bit pass, the 64-bit form 2 to 3 ms a frame more; nothing reads destination
    alpha — cost if wrong: rounding (next ruling).
22. The choice between the 32- and the 64-bit target was deferred to Task 9 and is made there
    without a run of the 64-bit form: the 32-bit target stays (frame map, "Validation", "The
    target's format") — why: the code has one format constant behind the pipelines and the
    targets, not a switch; the two formats' arithmetic is known, and at the calibrated exposure
    the 32-bit form shows every display level below 96 in all three channels, so dark smooth
    regions cannot band; what it costs is at the bright end (blue up to 3 levels, red and green
    up to 2, darker; five stored blends up to 7 and 4) — cost if wrong: that rounding stays in a
    debug view. The earlier ruling asked for alternating runs on the display; they were not
    made. (Since the fix wave the target holds the value the mapping shows, so the arithmetic's
    rows for a factor of 1 apply: every level below 91 is shown in all three channels, blue lacks
    91, 95 and 67 more above them, red and green 11 from level 181 up; five stored blends up to 6
    and 3. The ruling stands.)
23. The per-view frame-time log line stays as a permanent diagnostic; the experiment's switches
    were removed.

Lit drawing (Tasks 7 and 8):

24. After the first lit build hung the graphics device three times: no further lit run until
    the cause was found offline, and at most three launches that could hang it. The cause is
    recorded as likely, not proven: vertex-stage loads from buffers bound by another draw
    (through root descriptors, which have no bounds check). The run that would have proven it
    (the old bindings again) was not made — why: each hang resets the user's graphics driver —
    cost if wrong: another cause is latent; the binding tests and every later run are the guard.
25. From then on, every first launch of a build that changes bindings or shader text is one run
    on the software adapter with the Direct3D debug layer (amendment C10), clean only if the
    layer is shown to be on and reports no error after lit frames of the new family — why: that
    run cannot hang the real device — cost if wrong: a fault the software adapter does not show.
    Its length: 300 s at first; 150 s after the user closed one such run because it loads the
    whole processor (that run was ruled clean on its 250 s of lit frames, without a picture); the
    game process at below-normal priority; 240 s while other agents were working. Task 9 changes
    one constant and no shader text or binding, so it has no such run. The fix wave changes the
    text of three pixel shaders (ruling 37) and has one, of 240 s.
26. A frame whose constant buffer cannot be made draws nothing and shows the clear colour — why:
    such a failure is device loss or exhaustion; keeping the previous picture would break "never
    a picture older than its scene" — cost if wrong: a dark frame until the next scene.
27. Task 8 was split into 8a (objects without a colour stream, and trunks), 8b (plants) and 8c
    (leaves, needed for criterion 3) — why: one binding parameter was left and the trees spot
    stood at 66% lit — cost if wrong: three tasks instead of one.
28. Four departures of the plants' task were accepted: a seventh interpolator in the clay vertex
    shader (the plants need 13 interpolated values, 12 existed), so every fixed-mode vertex
    program gained two lines; the object block's test changed so that the plant block is its
    exclusive sibling; the bend constants ride in the fourth components of the object block's
    rows, which keeps a plant in a 256-byte slot; the pixel shader's plant test has no `[branch]`
    (it holds no buffer load) — cost if wrong: one more interpolator on every clay draw; no cost
    was visible in the runs.
29. Leaves are drawn as probe records with one more mode bit: no new buffer and no new binding
    parameter. Five departures were accepted with them: the leaf shader's literals (0.4545, 0.6,
    0.4) are constants in code and not table fields (they move to the table with shadows, whose
    mask they scale); `LightInputs` grew from 312 to 328 bytes (`DrawRecord` did not); the sun's
    colour is multiplied in the pixel stage where the game does it per vertex; a function that
    names buffer sources changed for modes no pass makes; a frame whose eye is unusable stays lit
    without its leaves — cost if wrong: a second leaf shader with other literals would be lit
    with these.
30. The red dog in the software adapter's pictures is not followed up — why: it was seen before
    the dog was lit at all and is put down to that adapter's readback of a texture the game
    renders (inference); the graphics card shows the dog right — cost if wrong: a fault of the
    software path stays unexplained.

The antivirus (2026-10-08):

31. The machine's antivirus removed the shader test's executable (a generic static match on its
    content). It is the user's control and was not worked around: nothing was renamed, moved or
    altered to change how a binary is classified, and the game was not rebuilt until a backup
    copy of the working binary stood beside it. After the user restored the file and excepted
    the repository's build output, new binaries there build and run; the one restored file is
    locked, so the suite is run through a generated copy of its script in which only the shader
    test's output name differs, and the repository's script is unchanged — why: the detection is
    by content, which a name does not change, so this evades nothing the user has not allowed —
    cost if wrong: none to the product; `tests\run_native_tests.cmd` as committed cannot run to
    its end on this machine until the user deletes
    `out\tests\runtime-unit-tests\native_shaders.exe`.

Later, by the user:

32. Anti-aliasing by a higher internal resolution or multisampling is marked for later
    (2026-10-08) — cost if forgotten: the targets of this pass and the shadow mask of the next
    sub-project must stay independent of the game's resolution for it.

Made in Task 9:

33. The exposure is 2.22, from run 341's bracketed shots of the dirt patch (ratio 0.4495, the
    two emulated shots 1.8% apart), kept to two decimals — cost if wrong: every lit region
    shifts by the same factor. As built in Task 9 it was the output mapping's factor, so it also
    scaled the clear colour (a lit frame showed levels (30, 30, 37) instead of (20, 20, 25)) and
    every unlit draw of a lit frame: a defect, see ruling 37.
34. The rectangle "grass beside it" of the regions file was corrected: the first one straddled
    the edge of a moving leaf shadow. Both are reported — cost if wrong: criterion 4's grass
    figure rests on a rectangle chosen after the first one failed; the first one's figure is
    printed beside it for that reason.
35. No light-off run by the trees was asked for — why: the view there does not repeat from run
    to run (8 to 10 pixels), so it could not have given the pixel-registered unlit picture the
    leaf replay lacks — cost if wrong: that replay stays weak (levels and colour only).

Made after Task 9:

36. Task 9 (the calibration, the measurements and the documents) got no task review of its own;
    the whole-branch review read it as a named focus — why: it is measurements and text on top of
    one constant, and the whole-branch review read the same files next — cost if wrong: a fault
    of Task 9 is found one step later. It was: the two Important findings of that review are both
    Task 9's (rulings 37 and 38).
37. The exposure in the output mapping was a defect, not a property: it multiplied every pixel,
    so the unlit draws and the clear of a lit frame came out `sqrt(2.22)` = 1.49 times too bright
    on the display (clean water (216, 207, 187) for (145, 139, 125)). Fixed in the final fix wave
    as the whole-branch review specified: the frame's block carries the exposure and the three
    lit pixel paths multiply their own value by it; the mapping's factor is 1 — why this form:
    the vertex shader with all its buffer loads is untouched, no binding and no buffer size
    changes, every program without light is instruction for instruction what it was, the clear
    is right with no change, and lit and unlit values share one unit in the target, so blends of
    the two are consistent — cost if wrong: lit pixels differ from Task 9's by a level or two
    where the target's rounding falls otherwise (the lit value is now stored after the exposure);
    one more software-adapter run.
38. Criterion 5 is recorded as met as written and as weak evidence: with the light off the two
    bridge pairs also have the same member brighter (1.47, 1.50), and the three pairs by the
    trees compare cut wood with bark, so a test of which side is brighter cannot fail. What
    supports the light is the size of the ratio (rope pole 3.60 in the game's frame, 3.03 lit,
    1.47 unlit) — cost if wrong: none; the criterion should have asked for the ratio's size.
39. The final fix wave: one implementer, after the whole-branch review's verdict "ready to merge
    after the MUST items": the exposure (ruling 37), the text of criteria 2, 5 and 7, and the
    review's smaller items (no light buffers built with the light off, the block read marked
    under the lock, tests for the smallest index and others, dead code, stale text); the items
    it left and the review's later ones are follow-ups 7 to 15 of the frame map's "Validation".
    Criterion 7's records-on clause was re-read against the baseline of the same kind (run 277,
    master's code in a user-present boot-save session: 1.47 to 1.62 ms; this branch 1.46 to
    1.50): not a regression; the 1.35 ms of the criterion's first form is a gated menu-start
    run's figure and no gated run of this code exists.

### Built differently from the design

- **Design 1.** The source was found (the gate passed): `LoadAluConstants`, which uploads the
  registers from buffers in guest memory past the device's bank. The device's pixel bool word is
  wrong and is not used.
- **Design 2.** The table has seven families (ruling 13) and is said in three parts that must
  agree (the pixel shader's entry, the vertex shader's entry, the list of checked pairs). The
  terrain layers multiply the sun by `sat(8 * (N . c109))`; the objects' baked colour is the
  stream's value to the power 2.2 or, without a stream, a sum of probe colours.
- **Design 2, the row "(none)".** A draw without a family is NOT drawn with the frame's ambient
  and a baked term of 1: it keeps the unlit colour of the materials picture with its facet shade
  — why: measured at the bridge, the design's form is 3.7 times a lit object's mean and blue, the
  unlit colour 2.6 times — cost if wrong: an unlit draw (hair cards, water) does not sit at the
  brightness of its lit surroundings.
- **Design 3.** `DrawRecord` shrank from 1,064 to 616 bytes; the light inputs are a side array
  of 328 bytes per lit draw. The records-on capture median did not fall below the materials
  figure: what the side arrays won, the light inputs cost ("Validation", criterion 7).
- **Design 4.** `R11G11B10_FLOAT`, not the 64-bit format (rulings 21, 22). The lit value is the
  game's without its output scale of 16. A lit draw has no facet shade; an unlit one keeps it.
  The exposure is NOT where the design put it: the design's `display = sqrt(saturate(linear *
  E))` multiplies every pixel, and built so (Task 9) it brightened the unlit draws and the clear
  of a lit frame. Since the fix wave `E` = 2.22 multiplies the lit draws' own values before they
  are stored, and the mapping is `display = sqrt(saturate(linear))` in every frame (ruling 37).
- **Design 5.** The terrain replay has the lit form. Beyond the design: `replay_check.py`
  (captured probe draws through the game's vertex microcode against the renderer's decode) and
  `region_compare.py` with `docs/native-renderer/lighting-regions.json`.
- **Not lit**: the blended character cards `0x401A...` (hair and fur; their shadow comes from
  direct taps of the shadow maps), the water `0xF6D9...` (no albedo entry), and a few shaders
  with one or two draws: about 3% of the drawn draws at the three spots.
