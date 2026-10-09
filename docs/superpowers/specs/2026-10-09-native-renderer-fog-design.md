# Native renderer sub-project 9: fog, in-scatter and sky

Status: design, 2026-10-09. Not built. Research: `.superpowers/sdd/fog-research.md` (untracked;
its scripts in `out/tests/fog-research/`). Branch `native-renderer-fog`.

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
