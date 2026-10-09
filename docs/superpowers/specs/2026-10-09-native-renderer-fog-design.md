# Native renderer sub-project 9: fog, in-scatter and sky

Status: implemented 2026-10-09 (plan Tasks 1 to 6 and a fix wave after the two whole-branch
reviews, branch `native-renderer-fog`); measured in `docs/native-renderer/frame-map.md` section 16,
"Validation" (binary `7242edb`) and "After the reviews: the fix wave" (the final binary, `71f7f4e`:
runs 432 to 435), with its "Limits" and "Follow-ups". `--fable2_native_fog` is on by default. Every
run was made with the user at the machine (`RUN UNCHECKED`), on one daylight save. Of the nine
success criteria four are met (2, 3, 8, 9), with these said beside them: 2, one fog block was read
and refused in a software-adapter run (`tf4-texel`, run 416: counted, not explained beyond
inference; none in 25,409 real-GPU frames); 3 as it is written, on the pictures' medians, with one
texel band of the sunward view outside it (green 1.11), the cause given being an inference; 8 for
the builds that changed shader text or bindings (the pairing of run and build is by the logs'
times), with one warning, id 1008, in two of those runs, cause not established. One is shown by a
substituted measure and not as written (6: the compiled programs and region means against Task 4's
fog-off run; no run of sub-project 8's binary, the bridge only, three leaf-shadowed regions over
0.002, and not repeated by region on the final binary), as is the third part of 4 (near regions
within 0.002 only where no leaf shadow moves). Four are not met as written: 1 (the sky shader's
`b129` path is read by no machine check; everything the pass uses is), 4 (far ground within 25%
fails in luminance on two ranges that are wooded in the game and bare natively, 1.28 and 1.45, and
in red on four regions, up to 1.66; the sky within 20% fails in the band nearest the sun, 0.75 to
0.80, where the game has its sun and clouds, measured on Task 5's binary), 5 (the ratios are all
inside 0.75 to 1.25; the hue is 33 and 21 to 24 degrees off on two grass regions), 7 (30.0 fps but
for one window at 29.2 by the trees, and at the third spot one at 29.6, one at 29.7 and, on the
final binary, one at 29.2; the records-on capture median there is 2.48 to 2.73 ms against 2.3, 2.64
to 2.92 on the final binary, and 2.43 to 2.64 with the fog off on Task 6's afternoon; view off 0.07
to 0.13 ms is met). What was ruled during the work and what was built differently is at the end,
"Implementation notes (deviations)". Research: `.superpowers/sdd/fog-research.md`
(untracked; its scripts in `out/tests/fog-research/`).

## Goal

The native picture gets the game's haze and its sky. Today far ground reads 0.4 to 0.7 of the
game's and is not blue, and behind the scene the native target shows its clear colour (frame
map section 14, "Not light: haze and leaves"). After this sub-project every lit native draw is
fogged with the game's own arithmetic and tables, and the sky is drawn by a native pass.

Not in this sub-project: the game's post chain (its exposure table, bloom, desaturation, gamma,
the half-resolution sun-coloured quads: sub-project 10; the fixed exposure of section 14
stays), water and the lake, the sun disc and glare, sparkles, blob shadows, particles, the
distant-tree billboards, the ground-fog volume under bool `b129`, anti-aliasing.

## What the game does (research; evidence there)

- **Pixel fog**, one block in 40 of 41 fogged pixel shaders, before the output scale `c20.w`:

  ```
  V = eye - P;  d = |V|;  dD = d - c67.x
  if dD > 0:
    S = tf4(sat((-V/d) . c66.xyz)).rgb          in-scatter by angle to a sun-like axis (1D, 64, half floats)
    T = tf5(c67.w * dD).rgb                      transmittance by distance (1D, 256, RGBA8, swizzled)
    F = c68.rgb + sat(c67.z * dD) * (S - c68.rgb)
    C = C + c28.x * (C * T + (1 - T) * c28.y * F - C)
  ```

  Machine-checked on 38 shaders (5,728 of 5,728 trials). Ten blended-character shaders scale the
  in-scatter by the pixel's alpha; the native pass does not light those draws yet.
- **Vertex fog** for trunks, leaves and tree billboards: the same quantities per vertex from
  tables tf16, tf17 and the vertex twins `c110` = `c66`, `c111` = `c67`; the pixel shader does
  `colour * o6 * c49 + o7`. Whether tf16 = tf5 and tf17 = {`c68`, tf4} is not known.
- **Per frame**: `c28`, `c64..c68` have one value per frame in 720 frames of 91 captures and
  drift with the clock. The tables sit in guest memory in a ring of three addresses, a new set
  each frame. **No texel of any table has been captured.**
- **Sky**: one draw, a cube around the eye (`8262` / `F760`), through the undecoded emitter
  `DrawIndx:82217EE8`, after the terrain layers, depth-tested, no depth write, no blend, before
  the blended group. Colour: `lerp((1 - E) * tf4(angle), lerp(tf13^2, tf14^2, c47.x), alpha)`,
  `E = exp(-(l1 * c64 + l2 * c65))`, lat-long textures tf13 / tf14. Read by hand only; its
  textures and vertex constants are in no capture.
- **No haze in post** at the bridge by day (no post shader reads depth or a fog constant).
- **Oracle**: with the emulator's `--readback_resolve=full` the scene colour before post is in
  guest memory at `0x199E0000` (1120 x 720, format 29, tiled); 64 KB of it were seen.

## Design

1. **Discovery first.** A capture-only change (nothing the pass draws changes): 1D textures are
   dumped; draw rows' `"tf"` include the vertex shader's fetch slots; a `DrawIndx:82217EE8` draw
   gets a decoded row; format 29 is dumped in full. Three runs (bridge, third spot, bridge with
   read-back) give the tables, the tree tables, the sky's inputs and the oracle picture.
2. **Formulas.** `light_formulas.py` gains `fog`, `fog_tables` decode and `sky`; the research's
   differential check moves into the tracked tools with tests; `F760` is machine-checked (the
   interpreter gains what it lacks for it, or the gap is stated).
3. **Fog block** (per frame, like the light and shadow blocks). Read once a frame at the first
   main-scene draw whose pixel shader has the block: `c28`, `c64..c68` from the pixel bank, the
   fetch constants of slots 4 and 5, and the two tables' bytes from guest memory (512 + 1,024
   bytes), bounded and fail-closed. `MakeFogBlock` validates (every value finite; `c66` unit
   within 1e-3; `c67.x >= 0`, `c67.w > 0`, `c67.z >= 0`; `c28.xy` in [0, 4]; both fetch constants
   1D with the expected width, format, endian) and decodes the tables to floats. `DrawRecord`
   does not grow; nothing is read with the view off.
4. **Native fog.** The two tables are uploaded each frame they change as two small textures
   (64 x 1 float, 256 x 1), linear, clamped. Every lit native pixel shader calls one shared
   `Fog(colour, world position)` after the lit colour and before the exposure that stands in for
   `c20.w`; terrain layers fog each layer before its blend, far terrain before its cross-fade.
   Trees use the same per-pixel function. A frame without a valid block is drawn without fog
   and counted (`fog-block`); never another frame's tables.
5. **Sky block and pass.** Read at the `82217EE8` draw whose pixel shader is `F760...`:
   `c31.x`, `c46.x`, `c47.x`, the fetch constants of tf13 / tf14. The pass draws one triangle at
   the far depth after the opaque records and the terrain layers and before any blended record,
   depth-tested (drawn only where nothing was), no depth write; the view ray comes from the
   frame's inverse view-projection (the shadow mask's); the arithmetic is `F760`'s with its
   literals. Textures through the texture cache when their format decodes; otherwise the
   clear-sky term alone, counted (`sky-texture`). No sky block: the clear colour, counted
   (`sky-block`).
6. **Switch.** `--fable2_native_fog` (on / off, the shadows switch's parser; default on once
   validated) turns fog and sky off together: the picture is then sub-project 8's.
7. **Validation.** In order: function against microcode; function on the CPU against the
   oracle's sky pixels; native target against the oracle by region in linear values; final
   shots by ratio and hue. Shot brackets of 3 s or less (shadows ruling: the sun moves).

## Rulings

1. Scope: scene side only; the post chain is sub-project 10 — why: no haze is made in post in
   the scenes seen; the post chain is separate passes — cost if wrong: a scene (night, town)
   whose haze is made in post stays unhazed until then.
2. Fog as a function in each lit shader with a per-frame block (research option A), not a
   full-screen pass over depth — why: it is the game's structure and stays right under blends
   and cross-fades — cost if wrong: one more texture pair and a few instructions in every lit
   shader.
3. The tables are read from guest memory, never computed from `c64` / `c65` — why: what they
   hold is unproven — cost if wrong: 1.5 KB read a frame.
4. Trees are fogged per pixel with the pixel form — why: the registers are identical to the
   vertex twins; per-vertex against per-pixel differs only by interpolation over a trunk or a
   card — cost if wrong: if discovery shows tf17 is not {`c68`, tf4}, tree haze differs in
   colour; then tf16 / tf17 go into the block as further tables (decided after discovery).
5. The sky is a native far-plane pass (research option S1), not the captured cube — why: the
   cube only supplies a direction — cost if wrong: a clipped or partial cube in some scene
   would be drawn whole.
6. Not drawn here: tree billboards, water, the lake, sun disc, glare, sparkles, blob shadows,
   particles, the `b129` volume — why: each is geometry or material work of its own — cost if
   wrong: against a hazed ground and a sky they are the largest remaining differences of the
   far picture (billboards make the game's far hills wooded).
7. One switch for fog and sky — cost if wrong: they cannot be compared apart without a build.
8. The fixed exposure stays; final-picture criteria for haze and sky are loose ratios and hue,
   the tight comparison is against the pre-post oracle.
9. User-present runs need the user's yes for this sub-project (asked 2026-10-09); until then
   agents use the idle gate or report NEEDS_RUN.

## Success criteria

1. The pixel fog function equals the microcode in every trial of the 38 checked shaders, in a
   tracked tool with a test; the sky function is machine-checked against `F760` (or the gap is
   stated and the criterion is not met as written).
2. The fog block: valid in every lit frame of the three spots' runs (invalid frames counted and
   explained); its registers equal the discovery rows' `"pfull"` and its table bytes equal the
   dumps of the same frames.
3. Sky on the CPU against the oracle: median within 10% per channel over sky pixels of the
   read-back picture.
4. Native target against the oracle, linear values, by region: far ground within 25%, sky
   within 20%; regions nearer than `c67.x` unchanged within 0.002 against fog off.
5. Final shots: the far-ground regions that read 0.4 to 0.7 of the game's read 0.75 to 1.25,
   hue within 20 degrees; sky regions reported.
6. Fog off equals sub-project 8's picture (program comparison and regions within 0.002).
7. Frame rate 30.0 fps at the bridge and by the trees and not below 29.8 at the third spot on a
   quiet machine; records-on capture median at most 2.3 ms at the third spot; view off under
   0.5 ms.
8. No D3D error in a 240 s software-adapter run with the debug layer for each build that
   changes bindings, targets or shader text.
9. Evidence rule: every number names its file; inference is marked; a criterion not met as
   written is said to be not met.

## Risks

Tables never seen (endian, half floats, swizzle); the moment of the table read within the ring
of three; HDR in-scatter clipping under the fixed exposure; the sky's 91 instructions with a
lat-long seam; binding slots (the RHI has 12 parameters); all evidence is one daylight save.

## Implementation notes (deviations)

Written 2026-10-09 with plan Task 6. The measurements and their files are in
`docs/native-renderer/frame-map.md` section 16; this part records what was ruled while the work
went on (each ruling by the controller under the user's standing instruction, with why and what
it costs if it is wrong) and where the built thing differs from the design above.

### Rulings made during the work

Numbered on from the nine above.

10. *Non-finite input means unfogged* (`fog`, `vertex_fog`; `sky` counts the value as 0) — why:
    fail closed like the rest of the pass — cost if wrong: a pixel with a bad input shows
    unfogged instead of black.
11. *Ruling B: trees take the game's own vertex-form arithmetic per pixel* (`T = tf5`,
    `add = (1 - T) * tf4(angle) * c8.x`; no blend from `c68`, no `c28.x`), not the shared function
    of ruling 4 — why: tf16 is tf5 and tf17 is tf4 itself in 28 of 28 frames, so ruling 4's
    premise is false — cost if wrong: within one unit past the fog's start a tree's haze colour
    differs slightly from the ground's.
12. *The capture's added rules stay* (a base address that is not 0; the sky's `c31.x`, `c46.x`
    not negative, `c47.x` in 0 to 1; the magnitude bound, narrowed after review to the
    components the fog reads) and *the sky's own rules do not veto the fog* — why: misses are
    counted; a night or interior that leaves a sky register negative keeps its fog — cost if
    wrong: a scene without sky or fog, visible in the counters.
13. *The two tables are arrays in the frame's constant block*, read by a shader function equal to
    `table_1d`, not textures (Design 4) — why: the clay layout uses 12 of 12 binding parameters
    and no SDK change is allowed — cost if wrong: 5.3 KB of constants a frame and dynamically
    indexed loads.
14. *The pixel's world position comes from its own depth* through the frame's inverse
    view-projection — why: the stored position is model space for objects, trunks and skins; one
    path for all families — cost if wrong: an error that grows with distance (6 units at 2,000
    by CPU arithmetic); a frame without a single view (an interior) is unfogged, counted.
15. *The clear-sky term is built fully, the texture term only without a new binding parameter*
    (it got a layout of its own) — why: tf13 / tf14 are a texture of zeros with `c47.x` = 0 in
    every capture — cost if wrong: a scene with a painted sky shows clear sky, counted
    `sky-texture`; the term has never been seen with content.
16. *The sky's view ray is formed from the view-projection's rows*, not from its inverse
    (Design 5) — why: 0.00006 pixel against 0.14 through the single-precision inverse — cost if
    wrong: none found; it is the same line.
17. *Unlit draws whose pixel shader has the fog block are fogged too*, the ground form on the
    unlit colour with the in-scatter under the lit draws' exposure (after the review of plan
    Task 4) — why: the game fogs them; the water and two unlit hills stood unhazed against hazed
    ground — cost if wrong: the unlit programs gained a branch; the colour under the haze is a
    placeholder. Built with the blended form's alpha factor for the ten shaders the table marks
    (the implementer's addition, an approximation, stated). The lake's far surface `2B09...` is
    not a record of the pass and is not drawn: nothing was decided for it.
18. *Clouds are not drawn in this sub-project* (after the review of plan Task 5) — why: they
    are separate draws after the sky, not part of the sky shader, whose textures are zeros —
    cost if wrong: the sky is plain where the game's has clouds; sky regions against the oracle
    hold only between clouds.
19. *The switch is on by default* (plan Task 6, its last code change) — why: the far ground and
    the sky are the game's within the limits measured, the frame rate held with it on and off
    alike, and no D3D error occurred in four software-adapter runs — cost if wrong: a scene
    never seen (night, town, interior) may be drawn worse than without it;
    `--fable2_native_fog=off` gives sub-project 8's picture back. When this was ruled the binary
    with the default had not been run; it has been since (run 431, and the final binary's runs 432
    to 434 with default arguments).
20. *User-present runs* were allowed for this sub-project by the user on 2026-10-09 ("user
    present, but you can run it anyway"; the controller's reading, ruling 9's question): made by
    the controller only, every one `RUN UNCHECKED`.

21. *A lit draw is fogged only if its own pixel shader in the game has fog* (after the whole-branch
    code review, its I1, fix a): a lit record that is no terrain record and no tree and whose pixel
    shader is not in the fog table is left unfogged and counted — why: the game does not fog
    `26052DEFBA688AF2`, a plant form without a fog block — cost if wrong: a later shader with fog
    that the fog table misses is drawn unhazed, visible in the count `lit N not fogged`.
22. *No retry across frames and no fallback to an earlier frame's tables* for a fog block that was
    read and refused (after run 416's `tf4-texel` frame) — why: a stale table must never reach a
    shader; one unfogged frame at the world's start is invisible — cost if wrong: a rare single
    unfogged frame.

### Built differently

- **Tables as constants, not textures** (ruling 13): no upload, barrier or fallback object; the
  frame's block grew from 112 to 5,344 bytes.
- **The position from depth, not an interpolator** (ruling 14).
- **Trees in the game's own form** (ruling 11), not the shared `Fog` of Design 4.
- **Unlit draws fogged** (ruling 17): Design 4 says "every lit native pixel shader"; the flat
  shader and the unlit returns now fog under a per-draw bit. An unlit trunk or leaf takes the
  trees' form with its own scale (the fix wave).
- **Not every lit draw is fogged** (ruling 21): Design 4 and the Goal say "every lit native draw";
  the one lit shader without a fog block in the game is left unfogged.
- **The sky's ray from the matrix's rows; the sky at the far plane** where the game has a cube of
  half-size 2,850 units (ruling 5's cost, the other way round: far ground is drawn natively where
  the game covers it; none lies that far in the views measured). **A frame without fog has no
  sky** (the step reads the fog's part of the frame's block), which is narrower than Design 5.
  A sky texture that cannot be had is counted also when its content would not have mattered.
- **Validation order** (Design 7): the oracle comparison used window shots of the mapped target
  taken back to linear values, 5 s from the capture, not the linear target itself.
- **Not built, as ruled**: everything in ruling 6, and clouds, the sun's disc and glow, the moon
  (ruling 18; frame map, "Follow-ups").

### Criteria

| | Verdict | Evidence (frame map section 16; files in `out\native-evidence\fog\`) |
|---|---|---|
| 1 | not met as written | `fog_check.py` with its test: 40 pixel shaders, the trees' form, and `F760` 218 of 218 agree (`task2_fog_check.txt`); the gap that the criterion names: `F760`'s `b129` path is unchecked |
| 2 | met | `task6_<stamp>_block.txt`: 8 of 8 frames at each of the three spots, 4 of 4 twice; `fix_165011_block.txt`: 8 of 8 on the final binary; no invalid frame in any real-GPU fog-on run (0 of 25,409 frames); one refused block (`tf4-texel`) in the software-adapter run 416, counted, not explained beyond inference |
| 3 | met | `task6_145136_replay_frame1.txt`: 1.007, 0.998, 0.982; towards the sun `task6_134312_replay_frame1.txt`: 1.031, 1.071, 1.022 (one texel band's green at 1.11: `task6_sunward.txt`; that the excess is the game's clouds and sun is an inference) |
| 4 | not met as written; its third part by a substituted measure | `task6_oracle_regions_45s.txt`: far ground in luminance 0.99 to 1.22 on six regions, 1.28 and 1.45 on two wooded ranges; in red over 1.25 on four regions (1.27, 1.32, 1.44, 1.66); sky 0.85 to 1.05, and 0.75 to 0.80 nearest the sun (`task6_sunward.txt`; the native shot there is of Task 5's binary, run 413); near regions within 0.002 where no leaf shadow moves (`task6_regions_summary.txt`) |
| 5 | not met as written | `task6_regions_summary.txt`: 0.84, 1.01, 0.99 on the three named regions, all far regions 0.76 to 1.23; hue -33 and -21 to -24 degrees on two; sky 0.80 to 1.0 reported |
| 6 | met by a substituted measure | shader test cases 166, 201, 210; `task6_old_new_programs.txt`; `task6_fogoff_against_task4.txt` (within 0.0003 on seventeen of 22 regions at the bridge at 45 s; three leaf-shadowed regions over 0.002); the final binary's fog-off run 435 has another camera and is not compared by region; shader test cases 210 and 213 on the final text |
| 7 | not met as written | the logs' 150-frame windows and capture lines of runs 426 to 429 and 417, 422, 430 (`task6_logs_summary.txt` has the capture and 300-frame lines), and of the final binary's runs 433 to 435 (`fix_logs_summary.txt`): one window at 29.2 by the trees, 29.6, 29.7 and 29.2 at the third spot; capture median 2.48 to 2.92 ms against 2.3 |
| 8 | met | runs 397, 405, 415 / 416 and, for the fix wave's shader text, 432: no error (the run-to-commit pairing is by the logs' times); warning id 1008 once in runs 416 and 432, cause not established; the default-on binary `792caf9` was first run as run 431 (real GPU, no error), not on the software adapter |
| 9 | met | the frame map's "Validation" and "After the reviews: the fix wave", after the documents review's corrections (a refused block that three places had denied, run 413's binary, the far ground by channel, an inference that stood as fact) |
