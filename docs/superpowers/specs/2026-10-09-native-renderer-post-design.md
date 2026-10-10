# Native renderer sub-project 10: the post chain (exposure, composite, bloom, sun layer, sky furniture)

Status: design, 2026-10-09, branch `native-renderer-post`. **Phase 10a is built and validated
with four things open when it was validated** (2026-10-10: criterion 9, the cost; ruling 2, the
read-back switch's default; criterion 6's real-GPU half, measured again since, after ruling 25's
change; and, waiting for those, the default of `--fable2_native_post`. Both switches of the phase
are off by default until the outstanding timing runs: frame map section 17, "Validation (10a)"
and "Outstanding: cost", and "Implementation notes (deviations)" at the end of this document).
**Phase 10b is built and validated with one thing open** (the cost: criterion 15's part of
criterion 9, so `--fable2_native_bloom` stays off by default; criterion 13 is not met as written
in one of the bridge's 26 regions: frame map section 17, "Validation (10b)"). 10c is not built. Plan:
`docs/superpowers/plans/2026-10-09-native-renderer-post.md`. Research:
`.superpowers/sdd/post-research.md` (untracked; its scripts and outputs in
`out/tests/post-research/`); every fact below is from it or from the code named beside it, and
inference is marked. Three phases on one branch: 10a exposure and composite, 10b bloom, 10c sun
layer and sky furniture; each ends in a working picture, its own validation and its own review.

## Goal

The native picture goes through the game's own post chain. Today a lit native draw writes its
light times one fixed number (2.22) and one step maps the float target to the display with a
square root (frame map section 14, "Linear target"). After this sub-project:

- **10a.** In a normal run the game measures its own picture again (its luminance resolve is read
  back, behind a switch), so its exposure adapts as on the console. The native lit draws write the
  game's own scale (`c20.w`), and one native step does what the game's composite `37B1` does with
  the game's own 512-texel table and registers, followed by the scan-out gamma ramp that the
  emulated picture gets and the native one has never had.
- **10b.** The game's bloom: its 4 x 4 downsample, its bright pass, its two 13-tap blurs with the
  game's weights of the frame, and the composite's screen blend.
- **10c.** The game's sun layer (sun-beam source, flares, moon glow, sun disc, two radial blurs)
  into the bright pass, and in the scene the moon, the clouds and the glare.

Not in this sub-project: anti-aliasing and supersampling (the design keeps their place, below),
the HUD, tree billboards, water, depth of field, the engine's streaks, sky overlay, tone-mapping
overlay texture, inverse bloom and saturation mask (none shows in a capture; research section 5),
night, towns and interiors as measured scenes (one daylight save), a native view with the emulated
passes suppressed (research risk 8).

## What the game does (research; evidence there)

- **One chain in all 72 frames of 11 captures** (research section 1): a half-size "sun layer"
  (560 x 360) with two 2 x 2 reductions and two radial blurs, the 4 x 4 downsample D of the scene
  (280 x 180), a luminance picture L (280 x 180, `k_32_FLOAT`), a bright pass BP, two 13-tap blurs
  H and V, one composite X at 1280 x 720, the HUD, and a last rectangle F that blends 10.2% of
  the previous frame's final picture over the new one. Destinations of the resolves are inferred.
- **Eight post pixel shaders are read and machine-checked** (2,000 values each, largest relative
  difference 6.3e-16; `out/tests/post-research/post_check.txt`). With `lum(c) = 0.2125 r +
  0.7154 g + 0.0721 b`:

  ```
  87E8 downsample   out = 0.25 * (t(P+(2.5,2.5)) + t(P+(2.5,0.5)) + t(P+(0.5,0.5)) + t(P+(0.5,2.5))),
                    P = 4 * pixel position, linear filter
  EE92 bright pass  L = lum(d);  out = (d * sat(L*c77.y - c77.x) / L * c31.x + tf1 * 6 * c46.x + tf2) / c47.x
  EA83, 28DB blurs  13 point taps at texel offsets -6..+6 along x, along y; weights c46.x (centre),
                    c46.yzw, c47.xyz
  4BE4 radial blur  14 point taps p_k = c31.xy + (uv - c31.xy) * s^k, weights 0.5 * g^k, sum * 4/7,
                    s = 1 + (4/7)(c47.x - 1), g = 1 + (4/7)(c46.x - 1); two passes
  0A96 reduce       one linear fetch at offset (1, 1): the mean of a 2 x 2 block
  5821 luminance    out = lum(downsample)
  37B1 composite    L = lum(scene);  e = c77.y * T(c46.x * L)          (1D, 512 texels, point)
                    c = sat(e * (L + sat(c47.x * L) * (scene - L)))
                    b = sat(bloom * c31.x);  s = c + b * (1 - c)
                    t = s + c77.x * (s - lum(s));  out.rgb = |t| ^ c78.x
  ```

- **The exposure is two things**: the scene's output scale `c20.w` and the composite's table `T`
  at `0x1FAA0000` (1D, 512 texels of one big-endian float, format 36, point filter, clamp), a
  gain by scene luminance that the CPU rewrites every frame. The CPU measures pass L's resolve
  (inference: no GPU pass samples it). In a normal run the emulator does not copy resolves to
  guest memory, the game measures nothing, `c20.w` sits at 16.000 and the table is flat (0.138
  above the toe). Under `--readback_resolve=full` the game adapts: `c20.w` 8.43 to 8.46 at the
  bridge, 7.41 towards the sun, and a rising table (0.20 to 0.32). The bright pass's `c77.y` is
  `1 / c20.w` in every frame.
- **Replay against the game's own final picture** (the front buffer `0x1ECFF000`, in the
  read-back dumps in full; research section 4). Variant A (table, grey fade, saturation, gamma;
  bloom 0): 26 bridge regions at 0.815 to 1.017 of the final luminance, whole picture median 3.10
  to 3.18 levels, 55.9 to 56.0% of the pixels within 8 levels. Variant B (A with the bloom):
  every region 0.975 to 1.029, median 0.73 to 0.75, 94.2 to 94.6% within 8, on three bridge
  captures (the research's figures, with the bloom sampled at aligned texel centres; at the
  game's own fetch, controller ruling 27: 0.994 to 1.027, 0.57 to 0.58, 94.5 to 94.8%:
  "Implementation notes", phase 10b). Towards the sun B lacks the beams and the sun: median 1.57, 69.9% within 8. A fixed
  gain: median 7.7.
- **The post chain turns no hue** (grass keeps its hue to 0.2 degrees through it).
- **Scan-out applies the guest's gamma ramp** in the SDK's swap blit, a 256-entry table for an
  8-bit front buffer: measured 124 -> 110, 180 -> 170, 236 -> 232. The native picture is drawn by
  the post-processor callback after that blit and has never passed through it.
- **Sun layer and furniture** (research section 5): all quads come through the indexed builder
  `0x82217EE8`, 4 vertices of 20 bytes, 6 indices. In the scene: the moon (`81BA` / `18D3`), three
  cloud layers (`C304` / `6197`, 58 instructions, read by hand only in part), the glare (`81BA` /
  `0498`, additive, no depth test, the scene's last draw). In the layer: a depth from the scene's
  depth (`5D67`), occluders (clouds `3FF7`, three unit cubes `D1CC`, tree geometry `F1EB`), the
  sun-beam source (`8E4D`), flares (`D72F`), moon glow (`4B58`), the sun disc (`3E77`, drawn only
  here). None of the furniture shaders is machine-checked.

**Read in the code for this design** (not in the research, or other than it says):

- *The forced read-back.* `CommandProcessor::ShouldForceReadbackResolve`
  (`thirdparty/rexglue-sdk/src/graphics/command_processor.cpp` lines 203 to 274) matches a
  resolve only when it was issued as `PM4_DRAW_INDX_2`, is a rectangle list of 3 indices, and its
  destination base lies in [entry, entry + **64 KiB**) (`kForceReadbackWindow = 1u << 16`; the
  switch's help text and the research say 1 MiB). With `readback_resolve` at `none` a forced
  resolve is read back synchronously: the D3D12 path waits for the queue
  (`d3d12/command_processor.cpp` lines 3262 to 3270). The application seeds the list in
  `OnPostSetup` (`src/core/fable_2_app.h` lines 155 to 171), only while the switch still has its
  default source.
- *The ramp.* The SDK already has public inline accessors: `GraphicsSystem::command_processor()`
  (`include/rex/graphics/graphics_system.h` line 72) and
  `CommandProcessor::gamma_ramp_256_entry_table()` (`include/rex/graphics/command_processor.h`
  line 167; 256 entries of 10 bits a channel), and the application has
  `runtime()->graphics_system()`. The swap blit takes the 256-entry table unless the front buffer
  is `k_2_10_10_10` (`d3d12/command_processor.cpp` lines 2086 to 2094); the post-processor is
  invoked on the same thread at the end of that blit (lines 2324 to 2344). No hooked guest call
  carries the ramp: `src/native/capture/xdk_hooks.inc` and
  `tools/xdk_sigmatch/skate3_tu3_reference.json` name no gamma-ramp setter (the guest writes the
  `DC_LUT` registers, which the SDK's `WriteRegister` decodes).
- *Frame pairing.* The capture finishes and publishes a frame's scene at the guest's swap
  (`capture.cpp`, `OnSwap` -> `EndMainSceneFrame` -> `PublishScene`). The composite is issued
  before that swap, so a block read at the composite's call is in the frame's own scene without
  any waiting. (That the swap hook follows pass F is an inference from the captures' frame rows,
  which hold the post rows of the same frame.)
- *The RHI.* 12 binding parameters a layout, 8 textures a table, 4 static samplers, one colour
  target a pass; blend factors are zero, one, source alpha and inverse source alpha only (no
  constant factor); formats `kR11G11B10_FLOAT`, `kR16G16B16A16_FLOAT`, `kR10G10B10A2_UNORM`,
  `kR8G8B8A8_UNORM`, `kD32_FLOAT` (`native_rhi.h`). The clay layout uses 12 of 12 parameters
  (`clay_logic.h`, `kClayBindings`). The sky step got a layout of its own with 3 parameters
  (`clay_pass.cpp`, `EnsureSkyObjects`: the frame's block at b2, its constants in a small slot of
  the frame's constant buffer at b0, a texture table, the four static samplers), as the mask step
  and the output mapping have theirs; the RHI has no layout destruction.
- *The linear target* is `R11G11B10_FLOAT`: no alpha channel, so no per-pixel flag can travel in
  alpha (the research's second option for unlit draws); 6 mantissa bits in red and green, 5 in
  blue, against the 7 of the game's `k_2_10_10_10_FLOAT`.
- *The light block already holds `c20.w`* (`light_block.h`, `LightBlock::out_scale`, valid in
  (0, 1024]).
- *The indexed builder's storage is empty when the builder returns*; the game fills it afterwards
  (`capture.cpp`, `PendingIndexed`: discovery reads it when the thread enters its next hooked
  draw).

## Known open before discovery

The research's open questions, each with the task that settles it and the rule for each outcome
(the plan has the steps).

| | Open | Settled by | If it cannot be settled |
|---|---|---|---|
| 1 | where pass L's luminance is resolved to, and that the CPU reads exactly that | Task 1 (resolve rows, run 1), proved by Task 2 (run 4: only that address forced) | no switch can be built without an SDK change: Task 2 stops and reports; 10a goes on with the blind values, as today's normal runs have them |
| 2 | the adaptation: speed, lag, whether `c20.w` and the table of one frame belong together | Task 1 (run 2: two turns towards the sun under read-back) | the block's pairing rule (Design 4) decides frame by frame; its count is the evidence |
| 3 | which radial-blurred buffer is the bright pass's tf1 and which tf2 | Task 10 (run 3, with Task 1's resolve rows) | by the weights and the engine's names (beams are tf1), stated as inference |
| 4 | the ramp's 256 entries and whether they change | Task 1 (the table written into every capture) and every later capture | a brightness option is a user check (48 on) |
| 5 | the table and `c20.w` in a dark view under read-back | Task 1 (run 5: under the trees, third spot) | `MakePostBlock`'s bounds stand on the bridge alone, stated |
| 6 | whether the downsample takes four texels of sixteen or a box | Task 1 (the downsample buffer kept by content, run 1) | the four-texel form stands on variant B's fit alone, stated |
| 7 | when the product can read the block relative to the frame's publication | settled above by code reading (publication at the swap); Task 4's cross-check confirms it per frame | - |

Opened by the code reading: whether pass L's resolve meets the SDK's three conditions for a
forced read-back (Task 2); whether the application can compile against the SDK's installed
headers for the ramp accessors (Task 1).

## Design

Names: W x H is the native target's logical size (`ClayPass::kWidth` x `kHeight`, the game's
1120 x 720); S is the supersampling factor of a later sub-project, 1 today. **Every target of
this design has a size that is a fraction of W x H** (W/2 x H/2, W/4 x H/4), never a literal.

1. **Discovery first, per phase** (capture only; nothing the pass draws changes). 10a: a row for
   every resolve with its destination, the small post buffers and the front buffer kept by
   content, the SDK's ramp table written into the capture, runs 1, 2 and 5 of the research. 10c:
   run 3 with those rows, and the furniture shaders read and machine-checked.
2. **Formulas before shader text.** The eight post formulas move into
   `tools/xdk_sigmatch/light_formulas.py`; `post_check.py` checks them against the dumps as
   `fog_check.py` does; `post_replay.py` is the tracked replay (variants A and B) and the source
   of the oracle files and of the predictions the in-game criteria use. C++ mirrors in
   `clay_logic.h` are pinned to the reference by generated numbers (`post_pins.py`, as
   `fog_pins.py`). The table's point addressing (which texel a luminance reads, with the
   interpreter's fetch convention) is stated and pinned at texel boundaries.
3. **The luminance read-back switch** `--fable2_exposure_readback` (ruling 2). The application
   seeds `readback_resolve_force_addresses` from a pure function of its two inputs (the hero and
   dog texture fix, this switch): the luminance resolve's base address A beside `0x12704000`.
   Works with the native renderer off. A user-given `--readback_resolve_force_addresses` wins, as
   today.
4. **Post block** (per frame; the fog block is the precedent). Read on the guest thread at hooked
   `BeginVertices` calls outside the main scene, told by the pixel shader's microcode hash as the
   sky draw is told: at the composite `37B186F4BD3288F7` its `c31.x`, `c46.x`, `c47.x`, `c77.xy`,
   `c78.x`, the fetch constant of slot 1 and the table's 2,048 bytes; at the bright pass
   `EE922CFB4426EA83` its `c31.x`, `c46.x`, `c47.x`, `c77.xy`; at the blurs `EA83C7E7E24EC4C8`
   and `28DBE18F313350BF` their `c46`, `c47.xyz`; in 10c at the radial blur `4BE4344C27016E04`
   its `c31.xy`, `c46.x`, `c47.x`. `MakePostBlock` validates and fails closed: the fetch constant
   exactly as captured (1D, 512, format 36, endian 8-in-32, swizzle `0xB68`, sign fields 1 1 1 1,
   exponent adjust 0, clamp, point); every texel finite and in [0, 4) (the block carries the texels; which
   texel a luminance reads is the reader's rule, the nearest: ruling 19, Design 6); `c46.x` > 0; `c78.x` in
   (0, 1]; every register finite and inside a stated bound; the bright pass's `c47.x` > 0 (it is
   a divisor). **Pairing**: `EE92`'s `c77.y` times the frame's `c20.w` (the light block's) is 1
   within 1e-3, or the frame has no block. The blur weights are a part of their own (not
   negative, each set summing to 1 within 1e-3): a block without them gives bloom 0, counted.
   A frame that read no block of its own takes the last valid one if it is at most
   `kPostBlockMaxAge` = 4 frames old and pairs with the frame's `c20.w` (rulings 3 and 5);
   otherwise the frame has none. `DrawRecord` does not grow; nothing is read with the view off
   or the switch off.
5. **The scale of the native target.** In a post frame (below) the frame's exposure word
   `f_exposure` is the game's `c20.w`, so a lit draw writes the game's own `colour * c20.w`, and
   the fog and sky functions, which work before that scale, are unchanged. Readers of the scene
   (composite, downsample) clamp it to [0, 31.875], the game's target's range, and take a value
   that is no number as 0.
6. **Unlit draws, the clear colour, placeholder clay** (ruling 9). An unlit draw writes what it
   writes today (the square of its display value, at most 1) times one number of the frame, the
   unlit scale `U = 1 / (c77.y * T[128])` (the reciprocal of the composite's gain at scene
   luminance 1, bounded at 64), before its fog. So an unlit pixel goes through the composite
   like a lit pixel of the same brightness and joins the bloom as one. The word is 1 in a frame
   without post. The clear colour is not changed (it is the target's creation-time clear value):
   in a post frame its scene luminance is 0.0064, between 1 / 256 and 1 / 128, so it reads the
   table's texel 1, not texel 0 (the table is read at the nearest texel, `floor(u * 512 + 0.5)`
   with `u = c46.x * L`: controller ruling 19, from the fetch's half-texel offset; not
   `floor(u * 512)` as the research has it, which would read texel 0, the 0 of every capture),
   and shows nearly black, near level 9 of 255 before the ramp with a read-back run's table,
   where it showed (20, 20, 25) (plan Task 5's mirror, test case 607; this design first said
   black).
7. **The composite and the ramp: two steps in place of `kOutputMapPs`.** After the scene's
   draws: (a) the composite, `37B1`'s arithmetic with the frame's table and registers, from the
   linear target, texel for texel, into a new **front target** (W x H, `R8G8B8A8_UNORM`: the
   game's front buffer, 8 bits, before the ramp), bloom 0 until 10b; (b) the ramp, each channel
   of the front target's 8-bit value through the 256-entry table, into the mapped target the
   composite views sample. The game's composite writes 1280 columns from a 1120-wide scene; the
   native one stays at W and the view scales, as today. The table and the ramp travel as arrays
   in the post steps' constant block (25 small slots of the frame's constant buffer, read by
   index: an exact point fetch, no upload, no barrier; the fog's ruling 13 is the precedent).
   **One layout for every post step** (3 of 12 parameters: the post constants at b0, 8 words of
   step constants at b1, a table of 4 textures at t0; static samplers point clamp and linear
   clamp), separate from the clay layout, with one pipeline a step.
8. **The ramp's source** (rulings 4 and 8): no hooked guest call carries it; the application
   reads the SDK's own 256-entry table through the SDK's existing public accessors, on the
   command-processor thread, in the post-processor callback. It is the table the emulated
   picture of the same frame went through. A table that cannot be read or is not monotonic
   gives a frame without post, counted `post-ramp`.
9. **A frame without post** (ruling 5; `DecideClayPost`, pure, decided before the frame's
   constants are written): the switch off, the light off, no light block, no valid post block
   within the age bound, no ramp, the post objects not made, or no frame constant buffer. Such a
   frame is today's: `f_exposure` 2.22, unlit word 1, `kOutputMapPs` as it is. Counted by reason
   (`post-block`, `post-ramp`, `post-objects`). Never another scene's table beyond the bound.
10. **Bloom (10b)**: three targets at **W/4 x H/4 whatever S is** (ruling 6): D
    (`R11G11B10_FLOAT`), P and Q (`R10G10B10A2_UNORM`, the game's own format: clamped at 1,
    10 bits). Steps between the scene and the composite: downsample (the game's four fetches;
    the pixel-position convention is one constant that discovery question 6 decides), bright
    pass (tf1 and tf2 unread until 10c; `L = 0` gives 0), blur along x (P to Q), blur along y
    (Q to P), then the composite's `b = sat(bloom * c31.x)`, `s = c + b (1 - c)` with the bloom
    sampled linearly, at the composite's own fetch: bloom texel coordinate `x / 4 + 0.5` at pixel
    `x`, not with the texel centres aligned (plan Task 8's finding; "Implementation notes", phase
    10b). Every step writes `(v > 0) ? min(v, 1) : 0` (D: its range is the scene's):
    no value that is not a number can enter or leave a blur.
11. **Quad record (10c)**: a side array of the frame's scene (at most 24 a frame), one for each
    call of the indexed builder whose pixel shader is in a generated table of furniture shaders:
    four corners with their coordinates as the game filled them (read when the thread enters its
    next hooked draw, the frame rechecked), the vertex registers `c0..c3`, the pixel registers
    the shader reads, its textures' fetch constants, its blend and depth words. Fail closed;
    counted `quad-record`.
12. **Scene furniture (10c)**: a quad step with its own layout. Moon and clouds after the sky
    step and before the first blended record, depth-tested without write; the glare after the
    last record, additive, no depth test. Each with its game shader's arithmetic, only once that
    shader is machine-checked.
13. **Sun layer (10c)**: targets at W/2 x H/2 (a depth, two colour layers, `R10G10B10A2_UNORM`)
    and at W/4 x H/4 (three for the reductions and the radial blur). The depth comes from the
    native depth; the sun-beam source is drawn depth-tested into layer 1, reduced and blurred
    radially twice into the bright pass's tf1; flares, moon glow and sun disc into layer 2,
    reduced into tf2. Occluders the native depth does not hold (cloud quads, the cubes, the tree
    occluder draws) are added last, each only if its shader is checked and its draw can be
    recorded inside the cost bounds.
    *Amended after the review of plan Task 10 (2026-10-10; controller rulings 33 and 35):* the
    reductions are two (the 8-bit one of a cleared picture, C1, has no reader and is not built)
    and the quarter-size targets three (the reduction of layer 1 and its blur partner, the
    reduction of layer 2). The game's layer depth is **not** its scene's end depth: it is the
    mid-frame depth copy plus the cloud quads at their own alpha reference (0.149; the scene's
    clouds write depth at 0.0196 and are not in it), the cubes (the far water) and the late leaf
    draws. The native layer takes the native depth after the scene's last record, which no cloud
    step may have written, and the cloud occluders are **not** "added last": they are built with
    the layer, because without them the layer is worse than none (61.6% of the pixels within 8
    levels against 63.9% without a layer). The tree occluder draws need no draw (the native depth
    holds the leaves); the far lake is missing from the native depth (a limit).
14. **The previous-frame blend (10c, ruling 7)**: a second front target; the composite writes
    `lerp(new, previous front, 0.102)` (the factor from the game's blend constant; in the
    shader, since the RHI has no constant blend factor). One kept target, one blend.
15. **Where an MSAA resolve goes later.** Between the scene's last draw and the first post step:
    a multisampled linear target is resolved into a single-sample one of W S x H S, which the
    downsample and the composite read; the sun layer's depth is taken from the resolved (or the
    multisampled) native depth. The composite is per pixel and needs no samples. With S > 1 the
    downsample becomes a box over 4 S x 4 S texels into the same W/4 x H/4 target, so the blur's
    weights, which are in texels of that target, keep the game's width on screen.
16. **Switches.** `--fable2_exposure_readback` (ruling 2). `--fable2_native_post`,
    `--fable2_native_bloom`, `--fable2_native_sun` (on / off, the light switch's parser), each
    off until its phase's validation rules it on; bloom needs post, the sun layer needs bloom,
    the scene furniture needs post. A diagnostic, `--fable2_native_post_oracle=<prefix>`: the
    post steps run on a stored oracle scene with its stored block, and the mapped target is
    read back to a file once (Design 17).
17. **Validation**, in this order: formula against microcode; the tracked replay against the
    game's front buffer (variants A, B); the C++ mirrors against the reference's pins; **the
    shaders themselves on the oracle scene** (the oracle feed: the GPU's mapped target against
    the reference picture and against the front buffer, pixel for pixel, on the software adapter
    and on the real GPU); then the native picture in the game by region, as the movement the
    replay predicts. Shot brackets of 3 s.

## Rulings

Rulings 1 to 7 are the controller's (ledger, 2026-10-09); 8 on are this design's, each on the
recommended option.

1. Ruling: sub-project 10 is one branch, one spec, one plan in three phases (10a exposure and
   composite, 10b bloom, 10c sun layer and sky furniture), each phase ending with its own
   validation runs and review — why: the user asked for sub-project 10 as a whole; the phases
   share the post block, the oracle and the tests; the research's own split keeps the unknowns of
   10c from stalling a and b — cost if wrong: a long branch; a phase can be merged alone since
   each ends in a working picture.
2. Ruling: the luminance read-back switch `--fable2_exposure_readback` defaults ON if the bridge
   holds 30 fps with it and the game is seen to adapt (`c20.w` leaves 16); otherwise OFF and
   reported — why: the user wants the console's exposure in normal runs; a default-off switch
   would leave every normal run blind — cost if wrong: the emulated picture's look changes for
   every run; the switch turns it back.
3. Ruling: the block of frame N's composite is used for the next published scene (latest valid
   block, no waiting for the frame's own) — why: the composite is issued after the scene's
   bracket closes; the constants drift by 1e-4 a frame — cost if wrong: one frame of lag in the
   exposure during fast adaptation; measured in the adaptation run. (By the code the next
   published scene is the frame's own: ruling 10.)
4. Ruling: the scan-out gamma ramp is taken from the guest side if a hooked call carries it (the
   XDK's gamma-ramp setter); only if no such call exists a minimal SDK accessor is allowed — why:
   "no SDK change without need" — cost if wrong: a ramp carried as a measured constant would be
   wrong if a brightness option changes it. (No hooked call carries it, and no SDK change is
   needed either: ruling 8.)
5. Ruling: a frame without a valid post block is drawn with today's fixed mapping and counted;
   never with another scene's table older than a bounded age (the plan states the bound: 4
   frames) — why: fail closed as in the fog — cost if wrong: a visible exposure step on such
   frames.
6. Ruling: bloom buffers are a quarter of the LOGICAL size (1120 x 720 / 4) whatever the
   target's sample count or supersampling factor — why: the blur weights are the game's, in
   texels of that buffer; MSAA and supersampling come later — cost if wrong: none today (S = 1).
7. Ruling: the previous-frame blend (10.2%) is in 10c only if it costs one kept target and one
   blend; otherwise a follow-up — why: invisible with a standing camera — cost if wrong: the
   native picture is slightly sharper in time than the emulated one.
8. Ruling: the ramp is read from the SDK's own decoded table through its existing public
   accessors (`GraphicsSystem::command_processor()`, `gamma_ramp_256_entry_table()`), with no
   SDK change and no new guest hook — why: no hooked call carries the ramp (ruling 4's first
   case does not exist), the accessors need nothing added, and it is the very table the emulated
   blit applies — cost if wrong: if the application cannot compile or link against those headers
   (Task 1 tries it first), ruling 4's last case applies: one accessor,
   `bool rex::graphics::GetGuestGammaRamp256(uint32_t out_rgb10[256])`, and an SDK rebuild.
9. Ruling: unlit draws and placeholder clay take one unlit scale a frame, the reciprocal of the
   composite's gain at scene luminance 1, and go through the composite and the bloom like lit
   pixels; the clear colour stays as it is and shows near black in a post frame (table texel 1,
   near level 9 of 255 before the ramp: Design 6; this ruling first said black) — why: the linear
   target has no alpha for a per-pixel flag, a second target or a mask would cost a binding and
   a pass for placeholders, and the gain is flat in a blind run — cost if wrong: under
   adaptation the table rises with luminance (0.20 to 0.32), so a dark placeholder is up to a
   third dimmer and a bright one brighter than today; bright placeholders (the water, flat clay)
   feed the bloom; and with a flat table (a blind run) every unlit value v shows as `v^0.4545`
   through the ramp where it showed as `sqrt(v)`.
10. Ruling: the block read at a frame's composite goes into that frame's own scene (the capture
    publishes at the swap, which follows the composite); the age bound of ruling 5 serves only a
    frame whose own read gave none — why: it is what the code allows, and it removes ruling 3's
    frame of lag — cost if wrong: if a block is ever read after its frame's swap it is dropped
    by the frame recheck and that frame takes the carried one (counted).
11. Ruling: the pairing rule (the bright pass's `c77.y` times the scene's `c20.w` is 1 within
    1e-3) is a condition of validity, and the bright pass's registers are read in 10a already —
    why: it is the one captured relation that ties the composite's frame to the scene's — cost
    if wrong: a frame in which the game itself breaks the relation falls back, counted
    `post-block {pairing}`; run 2 shows whether that happens while the exposure moves.
12. Ruling: the table and the ramp are arrays in the post steps' constant block, not textures —
    why: an indexed read is the game's point fetch exactly (research risk 3: half a texel
    crushes or lifts the darkest shadows), with no upload, barrier or fallback object — cost if
    wrong: 6.3 KB of constants a frame.
13. Ruling: the composite writes an 8-bit front target and the ramp is a step of its own, from
    10a on — why: it is the game's structure (front buffer, then scan-out), the ramp's index is
    then the front buffer's own 8-bit value, and ruling 7's blend needs the picture before the
    ramp — cost if wrong: one more target (3.1 MB) and one more full-screen step than a
    combined shader.
14. Ruling: the bright and blur targets are `R10G10B10A2_UNORM`, the game's own — why: the
    clamp at 1 and the 10-bit rounding of the replay's variant B come from the format — cost if
    wrong: if the RHI cannot create one as a render target (seen on the first software-adapter
    run), `R11G11B10_FLOAT` with the clamp and the rounding written in the shader.
15. Ruling: the scene is clamped to 31.875 where it is read, not where it is written — why: the
    lit paths' bound (64) is in every lit program and changing it would change the programs of a
    frame without post — cost if wrong: a blend over a value above 31.875 differs from the
    game's; none is known.
16. Ruling: three switches, one a phase, each off by default until its phase's validation — why:
    the phases must be measured apart (variant A needs bloom 0 after 10b exists) — cost if
    wrong: three more words on the command line.
17. Ruling: the oracle feed is built (a diagnostic switch, a stored scene and block, one
    read-back of the mapped target) — why: no test executes the HLSL (frame map section 16,
    "Limits"); for full-screen steps over a known input the GPU's result can be compared with
    the game's front buffer pixel for pixel, apart from every error of the native scene — cost
    if wrong: one task; a path that only a diagnostic switch reaches.
18. Ruling: a furniture item is drawn only if its game shader is machine-checked and its record
    can be read inside the cost bounds; otherwise it is a documented limit — why: 10c has the
    most unknowns and must not stall — cost if wrong: see "What may end as a limit".

## Success criteria

Each with how it is measured. "Regions" are the bridge's 26 of `lighting-regions.json`,
`shadow-regions.json` and `fog-regions.json`; a level is 1 / 255 of the display range. Evidence in
`out/native-evidence/post/`.

**10a**

1. *Formulas.* `tools/xdk_sigmatch/post_check.py` reproduces 2,000 of 2,000 values within 1e-9
   relative for each of the eight post shaders; `tests/test_post_formulas.py` runs a reduced
   count on every shader. (Test.)
2. *The game adapts in a normal run.* With `--fable2_exposure_readback=on` and no
   `--readback_resolve`: every main-scene row of an 8-frame bridge capture has `c20.w` below 12
   (16.000 blind; 8.43 to 8.46 under full read-back), and the table's `T(3) / T(0.1)` is at
   least 1.2 (1.49 under full read-back, 1.00 blind); the log has the SDK's `Forced
   readback-resolve` line for the address. With `=off`: `c20.w` 16.000. (Task 2, run 4 and its
   control; `post_tables.py table`.)
3. *The post block.* Valid in every lit frame of the runs at the three spots (invalid frames
   counted and explained); its registers equal the `"pfull"` of the composite's, bright pass's
   and blurs' rows of the same frame bit for bit, its 512 texels the dump with the row's
   content; its age is 0. (`post_tables.py block` on Task 4's and each validation's captures.)
4. *The ramp.* The table the product reads maps 23, 47, 63, 124, 156, 204, 236, 252 to 10, 32,
   47, 110, 144, 197, 232, 250 within 1 level (`ramp_092959.txt`). (Task 1's capture row; the
   product's log line.)
5. *The reference composite against the game.* `post_replay.py` variant A on frame 1 of
   `092959`, `121331`, `132250` gives the research's figures: each region's ratio within 0.002
   of `replay_092959_f1.txt`'s A column (0.815 to 1.017), whole-picture median 3.18, 3.10, 3.17
   within 0.05 and 55.9, 56.0, 55.9% within 8 levels within 0.5 points. (Tool, with a test on a
   stored excerpt.)
6. *The shaders on the oracle scene.* With the oracle feed and bloom off, the read-back mapped
   target equals the reference's variant A picture after the ramp within 1 level on at least
   99.9% of the pixels and within 2 on all, on the software adapter and on the real GPU.
   (Task 6; `post_replay.py compare`.)
7. *In the game.* Native shots with post on against post off (same plan, 3 s brackets, a
   read-back run): each region's ratio on / off is within 0.05 of the ratio `post_replay.py`
   predicts for it from the oracle scene (variant A after the ramp over today's mapping), on the
   regions where no leaf shadow moves; the others are reported.
8. *Off is today's picture.* With `--fable2_native_post=off` the lit programs equal today's line
   for line and the unlit ones but for one multiply by a word that is 1 (program comparison);
   bridge regions of a post-off run against a run of the fog's final binary within the spread
   of the same two runs' emulated shots, where no leaf shadow moves (controller ruling 22, the
   ledger: this criterion first said "within 0.002", which two sessions cannot decide, since
   their emulated shots differ by more; the figure is dropped).
9. *Cost.* Bridge: 30.0 fps in every steady 150-frame native window, with post on and with the
   read-back switch on. Third spot: no steady window below 29.2 fps (today's worst there);
   records-on capture median stated beside today's 2.64 to 2.92 ms, and post on against off in
   one session differing by at most 0.10 ms; view off under 0.5 ms. Quiet machine.
10. *No D3D error* in a 240 s software-adapter run with the debug layer for each build that
    changes bindings, targets or shader text; warnings listed by id.

**10b**

11. *The reference bloom against the game.* `post_replay.py` variant B on the three bridge
    captures: every region within 5% in luminance (research: 0.975 to 1.029), hue within 5
    degrees on 25 of 26 (the 7-pixel averted pole side, 14.3 degrees in the research, reported),
    whole-picture median at most 2 levels (0.73 to 0.75) and at least 90% within 8 (94.2 to
    94.6). (The bracketed figures are the research's, with the bloom sampled at aligned texel
    centres. The reference now samples at the game's own fetch, controller ruling 27; with it:
    every region 0.994 to 1.027, every hue within 4.7 degrees, the averted pole side's among
    them, median 0.57 to 0.58, 94.5 to 94.8% within 8: `t8_reference_B_<stamp>.txt`,
    "Implementation notes", phase 10b. The thresholds are unchanged.)
12. *The shaders on the oracle scene, with bloom.* The read-back mapped target against the
    game's front buffer through the ramp: the thresholds of criterion 11; and against the
    reference's variant B picture: within 2 levels on at least 99% of the pixels.
13. *In the game.* As criterion 7 with variant B's prediction (haze, sky and lake up by 3 to 10%
    against 10a).
14. *Nothing that is no number.* A test feeds each step's mirror a scene texel of NaN, of
    infinity, a negative one and `L = 0`: every output is a number in the target's range; the
    shader test finds the guards in the compiled programs.
15. Criteria 8 (with `--fable2_native_bloom=off`: 10a's picture), 9 and 10 again for 10b's
    builds.

**10c**

16. *Furniture formulas.* Each furniture shader the pass draws with is machine-checked (2,000 of
    2,000) in `post_check.py`; a shader that is not is named, and its item is not drawn.
17. *Quad records.* In every captured frame at the bridge and towards the sun each record's
    corners and coordinates equal the `"indxdata"` row of its draw bit for bit and its registers
    the row's; none over the bound. (`post_tables.py quads`.)
18. *The reference with the sun layer*, on run 3's oracle frame: the share of pixels within 8
    levels of the front buffer is at least 10 points above variant B's on that frame (69.9% on
    `134312` with the bloom sampled at aligned texel centres, as the tools did when this was written;
    70.7% at the fetch's own place, controller ruling 27, the tools' default since Task 8; on run 3's
    frame, capture `052158` frame 1: 63.9% for B and 82.7% with the layer, frame map section 17
    "Sun layer discovery"), and the regions "sun" and "beams" of a new `post-regions.json` are within 15% in
    luminance. (These two thresholds are set here without a measured precedent; the figures
    reached are reported whatever they are.)
19. *In the game, towards the sun.* Native against the emulated shot of the same run: the "sun"
    and "beams" regions at 0.75 to 1.25 in luminance (today the native picture has neither).
20. Criteria 8 (`--fable2_native_sun=off`: 10b's picture), 9 and 10 again for 10c's builds.

**All phases**

21. *Evidence rule.* Every number names its file; inference is marked; a criterion not met as
    written is said to be not met.

## What may end as a limit

Said now, with the finding that makes it one (ruling 18).

- **Tree occluder draws** (`4325` / `F1EB`, 9,000 to 24,000 indices outside the main scene): a
  limit if recording them needs a path that grows `DrawRecord` or costs more than 0.10 ms of the
  capture median. The native depth already holds the native trees; what the game adds with these
  draws is not known.
- **The three unit cubes** (`D1CC`): a limit unless Tasks 10 and 11 identify from the vertex
  registers and the shader what they are transformed to.
- **The 24 point draws** (`B6C9` / `A4A9`, colour mask 0): not identified, not drawn.
- **Clouds and their occluders** (`6197`, `3FF7`): a limit if the 58-instruction shader does not
  reach 2,000 of 2,000 or reads an input no capture holds. Then the sky stays plain and the
  beams are not cut by clouds.
  *Amended after the review of plan Task 10:* "the beams are not cut by clouds" cannot end as a
  limit with the sun layer on: without the `3FF7` occluders the layer is worse than none. Both
  shaders reached their check; the occluders are built with the layer (ruling 35), and the
  scene's clouds stay plan Task 16's.
- **The far lake in the layer's depth** (added after the review of plan Task 10): the cube that
  stands for it in the game's layer is not drawn, and the native depth does not hold the lake:
  a limit for a sun low over open water. **No distant trees**: beams pass where the game's far
  woods stand.
- **Moon, moon glow**: built from checked formulas but below the horizon in every capture: held
  by tests, unseen (a user check).
- **Flares**: 0, 2 or 4 small quads a frame; what they add to the picture has not been seen.
- **tf1 against tf2** if the small buffers cannot be told apart in run 3 (their memory is reused
  within a frame): by inference.
- **The previous-frame blend** under ruling 7.
- **The downsample's form** (question 6) and **the dark-view bounds** (question 5), as the table
  above says.

## Risks

1. The SDK's forced read-back may not match pass L's resolve (opcode, primitive, the 64 KiB
   window), and it waits for the GPU once a frame: ruling 2's 30 fps may fail. Other resolves
   with a base within 64 KiB above A are read back with it.
2. The ramp accessors: reachable only if the application compiles against the SDK's graphics
   headers; a front buffer in a 10-bit format would use the SDK's other (piecewise) table.
3. The table's addressing in the darks (texel 0 is 0, texels 1 to 12 rise steeply; one texel is
   scene luminance 0.0078): the reference's convention is pinned at boundaries, but only the
   oracle feed compares it with the game.
4. Game-scale values in `R11G11B10_FLOAT`: 5 mantissa bits in blue against the game's 7; a rising
   table and a bloom can show a step the square-root mapping hid.
5. Unlit draws under ruling 9; the clear colour turning near black (table texel 1).
6. A NaN in the linear target: each reader takes it as 0; the UNORM targets cannot hold one.
7. Capture cost: every hooked `BeginVertices` call outside the main scene is now looked at (the
   pixel shader's hash, cached by object) in a frame with post wanted.
8. The quad storage is read one hooked draw late; a frame's last quad needs a later draw in the
   same frame (pass F is one).
9. Everything measured is one save, daylight, three spots, a standing camera; the adaptation has
   never been seen at work. Night changes the grey fade and probably the bloom's threshold.
10. Every run is likely `RUN UNCHECKED` (the user at the machine); timing needs a quiet machine.

## Implementation notes (deviations)

### Phase 10b, plan Task 9 (validation; frame map section 17, "Validation (10b)")

No run was made and no code changed for the validation; the verdicts are read from plan Task 8's
runs (logs 469 to 483) and Task 8b's (logs 484 to 489, the binary with the base map's gamma fix),
and what the reviews had left open is computed in `out/native-evidence/post/t9_criteria.txt`.

- **Criterion 6, the real GPU's half: met** since ruling 25's rounding (100.000% of the pixels
  within 1 level, one channel at 2, the same channel on the software adapter).
- **Criterion 11: met** with the reference at the game's own fetch (ruling 27): 26 of 26 regions
  within 5% and within 5 degrees of hue on each of the three bridge captures, median 0.57 to
  0.58, 94.5 to 94.8% within 8. The figures in the criterion's text are the research's.
- **Criterion 12: met**, both parts, on both adapters. The dumps against the game's front buffer
  through the ramp, by region (computed in this task): 0.991 to 1.033, hue within 5 degrees on
  26, 25 and 26 of 26, median 1 level, 94.62 to 94.94% within 8; against the reference 99.92 to
  99.95% within 2 levels.
- **Criterion 13: not met as written at the bridge (25 of 26 regions), met at the third spot (18
  of 18)**, on the binary with the gamma fix. The one outside is the averted side of a thin pole
  (+46.3% for +32.8% predicted). The far and the sunlit ground now follow the prediction within
  0.01 (0.02 to 0.04 above it before the fix).
- **Criterion 14: met** by the tests (not run again).
- **Criterion 15**: its D3D part is met (logs 469 and 484); its picture part is met by ruling
  22's program comparison, and by ruling 22's regions 8 of 10 standing regions lie inside the two
  runs' emulated spread (planks and lake 1.6% and 0.3% above it; two sessions and two window
  sizes); **its cost part is open**: no run with the bloom counts for timing.
- **The plan's runs for Task 9 were not made as listed**: no run with the read-back switch in
  place of full read-back, no trees run and no blind run on the binary with the gamma fix, and no
  SUNWARD run (left to 10c). No criterion needs them.
- **The defaults stay off**: `--fable2_native_bloom` waits for criterion 15's cost part and
  `--fable2_native_post` for criterion 9. The one list of the timing runs for 10a and 10b, with
  the rules for criterion 9, ruling 2 and the two defaults, is frame map section 17,
  "Outstanding: cost".
- **Limits** (frame map, "Validation (10b)", "Limits of 10b"): the wooded far ranges 15 to 55%
  over (distant trees are not drawn; the bloom makes it worse); the lake and the unlit
  placeholders; ground in shadow beyond 100 units 11 to 19% over, not followed; near ground in
  shadow not decided and possibly too dark; the downsample target's tenth of a level (ruling
  28); the sun's layers absent until 10c; one save, late morning, two places.

### Phase 10b, plan Task 8 (the bloom's code; frame map section 17, "Native bloom")

Stated after plan Task 8's own runs (logs 469 to 483; frame map, "Native bloom", "Runs") and
before the validation runs of plan Task 9; the criteria's verdicts come with them.

- **The composite samples the bloom at its own fetch, not with the texel centres aligned.** The
  game's composite fetches the bloom at the scene's interpolated coordinate with `OffsetX =
  OffsetY = 0.5` (`37B1`, instruction 6), as instruction 3 fetches the scene: the two share
  coordinate and offset, each in its own texels, so the bloom is read 0.375 of a bloom texel
  further on than the scene whatever the interpolator is (what stays inference is that the
  emulator's fetch offsets are the console's); with the interpolator `x / width` at pixel `x` (which
  makes the scene's own fetch a texel centre) that is bloom texel coordinate `x / 4 + 0.5`, 0.375
  of a bloom texel (1.5 pixels) further on than "centres aligned", which the research's replay,
  Design 10 and plan Task 8's line assumed. Against the game's front buffer of the three bridge
  captures the replay's variant B fits at a median of 0.565 to 0.578 levels with the fetch's
  place and 0.716 to 0.732 with the centres aligned
  (`out/native-evidence/post/t8_bloom_fetch.txt`). Controller ruling 27 accepts it. The native
  composite, its mirror and `post_replay.py` (`--bloom-place fetch`, the default of `replay` and
  `predict` and of `bloom_sampler` itself, so of variant C and the stored excerpt; `aligned`
  reproduces the earlier figures) use the fetch's place. Criterion 11's figures with it: every
  bridge region 0.994 to 1.027, median 0.57 to 0.58, 94.5 to 94.8% within 8 levels
  (`t8_reference_B_<stamp>.txt`).
- **The downsample's pixel position is the integral one (`kPostDownsampleBias` 0)**: the
  emulated picture's translator hands the shader `floor(SV_Position.xy)`
  (`dxbc_translator.cpp`, `StartPixelShader`), and with the fetch's place above bias 0 fits the
  front buffer at 0.565 to 0.578 levels where bias 0.5 gives 0.753 to 0.765 and a box of the
  sixteen texels 0.608 to 0.620. Discovery question 6 itself (the game's own downsample buffer)
  stays unsettled; that the console's rasteriser hands the same position is inference.
- **Every step into a UNORM target writes the rounded level itself** (controller ruling 25, and
  the same at the bloom's 10-bit writes): `floor(v * levels + 0.5) / levels` as the last
  statement, the level count from the target's format in the step's words. The reference rounds
  each 10-bit stage to the nearest of 1,023 and the game's own targets hold only levels; what
  the real card does at a 10-bit write was not measured. Measured after it: the real card's
  dump of a stored scene (bloom off) differs from the software adapter's and from the reference
  in 7 of 2,419,200 channels, where 124,757 were low before (criterion 6's real-GPU half).
- **The downsample's target loses a tenth of a level, one-sided.** `R11G11B10_FLOAT` (Design
  10) holds 6 and 5 mantissa bits and its conversion cuts toward zero; the bloom dumps lie 0.08
  to 0.14 of a level under the reference's variant B for it (the reference with that storage
  equals the software adapter's dump in 99.9990% of the channels). Criterion 12 holds with it
  (99.92 to 99.95% of the pixels within 2 levels). A target of `R16G16B16A16_FLOAT` would take
  it away; not built (it needs its own software-adapter run): a follow-up.
- **With the bloom the native far ground is 14 to 70% over the game's at the bridge** (0.93 to
  1.33 without it): the bloom moves the native regions as the replay predicts (criterion 13's
  first reading: 24 of 25 and 18 of 18 within 0.05), and the native scene before the post steps
  is brighter there than the game's. Not the bloom's to mend; open with "Native composite"'s
  offset. *Corrected by the review of Task 8 and Task 8b:* "0.93 to 1.33 without it" set a native
  picture without bloom against the game's with it; like for like (native over the replay of the
  same capture) the far ground is 14 to 65% over without the bloom and 14 to 78% with it, and it
  is section 16's criterion 4 finding of the fog sub-project, there before any post step.
- **Task 8b: the cause of the ground's part, and a fix outside the post chain.** Every terrain
  draw fetches the baked light map tf3 with the gamma sign in its fetch constant; the console
  gives the shader the texel through its gamma-to-linear curve and the native pass read it as
  stored, so the ambient part of all lit ground was 1.8 to 2.5 times the game's. Per pixel against
  the stored scene with the fog taken off, on ground only: 21 to 69% over by channel with the
  stored reading, within 1 to 4% with the curve (frame map section 17, "The base map's gamma
  sign"). Fixed in the two terrain shaders (a deviation: the spec changes no scene shader). What
  it leaves: the wooded far hills (no tree billboards natively), the rock face, the lake. The
  post-off picture's exposure 2.22 was fitted with the old reading and is not fitted again.
  Measured (runs T8b-1 to T8b-6, logs 484 to 489; like for like against the replay of each run's
  capture): dirt patch 1.28 -> 0.95, rock face 1.32 -> 0.96, far slope 1.24 -> 0.98, island 1.22
  -> 0.97; planks, peak and sky unmoved; ground in shadow: not decided (the classification first quoted, 1.10 to 1.75 -> 0.98 to 1.14, does not isolate shadowed ground; the one shadowed terrain region read directly, the dirt by the palisade at the third spot, went from 1.14 to 0.67 to 0.70 of the emulated shots under a shadow of another shape: review of Task 8b, Important 1; frame map, the same part, "Runs", item 4); the
  wooded ranges still 1.15 to 1.45 (1.15 to 1.55 with the bloom); no D3D error.
- **Criterion 8 after Task 8b (controller's ruling 30).** The gamma fix changes the post-off
  picture (lit terrain: dirt patch 1.04 -> 0.83 of the adapting game's with the fixed exposure).
  It is accepted; the exposure 2.22 is not fitted again; "post off is today's picture" is judged
  against commit `5d0f974`'s binary from now on, and ruling 22's program comparison holds for
  every program but the lit programs of the two terrain shaders.
- **The bloom's default after Task 8b: still off, for the cost alone.** With the bloom every
  standing region of the bridge but the wooded ranges, the far rock below the ridge (1.12) and the averted pole
  side (1.10) is within 5% of the emulated picture (without it the far
  ground and the sky are 8 to 19% under); no run with the bloom counts for timing.
- **Cost is not measured**: all fifteen runs were on the monitor where the present gate does not
  hold.
- **With `--fable2_native_bloom=off` the composite and the ramp are plan Task 5's programs but
  for that rounding** (shader test case 237); criterion 15's "10a's picture" is therefore 10a's
  picture after ruling 25, which on the software adapter is the same bits and on the real card
  is what criterion 6 asked for.
- **The bloom's objects are part of the post steps' while the bloom's switch is on**: if they
  cannot be made, a frame with the switch on is today's (`post-objects`); with the switch off
  the post goes on without them. Spec ruling 14's fallback (`R11G11B10_FLOAT` for the bright
  and blur targets) is not built: it is a change of one format constant, to be made only if the
  software-adapter run shows `R10G10B10A2_UNORM` cannot be a render target.
- **The Post line** says the frame's bloom and counts it, names which of the pass's rules
  refused a valid block (`age`, `pairing`, `values`, `words`), and says the composite's `c46.x`
  when it is not 0.25 (reviews of plan Task 5, Minors 8 and 11).

### Phase 10a (plan Tasks 1 to 7; frame map section 17)

Built as designed but for the following; the criteria's verdicts with their figures are in frame
map section 17, "Validation (10a)".

- **The table is addressed at the nearest texel**, `floor(u * 512 + 0.5)`, not at `floor(u *
  512)` (controller ruling 19: it is what the shader dump's fetch does). With it the clear
  colour of a post frame reads texel 1 and is nearly black, not black (Design 6, ruling 9):
  seen in the game with the fog off as levels 3 to 4.
- **The ramp's accessor is protected** in the SDK; the application names it through a derived
  class's using-declaration (controller ruling 20). No SDK change.
- **Criterion 4** is held to 2 levels, not 1 (controller ruling 21): the product's table differs
  from the shot-measured pairs by up to 1.56 levels. Not met as written.
- **Criterion 8**'s "within 0.002" is dropped (controller ruling 22): the program comparison
  and the regions within the two runs' own emulated spread stand for it.
- **Criterion 6 is met on the software adapter and not met as written on the real graphics
  card** (99.33 to 99.49% of the pixels within 1 level, all within 2): the card truncates a
  float to 12 fractional bits before it takes the nearest 8-bit level. Controller ruling 25:
  both 8-bit writes write the rounded level themselves, in plan Task 8, and the criterion's
  real-GPU half is measured again there.
- **The oracle feed acts only in a frame that is a post frame by its own verdict**, with the
  live ramp; a run whose live frames are no post frames gives no dump. It shows that the two
  post shaders return the reference on the game's own scene and block. It does not run the
  scene-side words of a post frame (the exposure in the lit draws, the unlit scale) or the live
  block's path, so the one-sided offset of criterion 7's far regions (+0.007 to +0.048 above the
  prediction) is not the two shaders' and is otherwise unexplained.
- **`Render` takes a `ClayPostRequest`** (the switches and the ramp); the decision's inputs keep
  the name `ClayPostInputs`.
- **Ruling 2 is not decided.** The game adapts with `--fable2_exposure_readback=on` in every run
  made. In the one valid pair of timing runs (logs 451 to 454) 5 of the 6 windows after a
  capture's two read 29.8 to 30.1 fps with it and one reads 27.8 (the run with post on and the
  switch; the same window of the three other runs reads 29.9 to 30.0; unexplained); one pair,
  with that window in it, is not taken as enough to change every run's look, and since run 457
  the window has opened on a monitor on which the present gate does not hold. The switch stays
  default off; the runs and the rule are in frame map section 17, "Outstanding: cost".
- **Criterion 9 is open** for the same reason: the bridge's first reading is 29.8 to 30.1 fps in
  11 of 12 windows after a capture's two (not "30.0 in every" as written); the twelfth, 27.8,
  is the third window from the capture in the run with post on and the read-back switch, steady
  in the three other runs and unexplained. Controller ruling 26 has since set the bridge's bound
  at 29.7 fps in every steady window not within three windows after a capture (the valid
  post-off run itself reads 29.8 once); by it the first reading is 8 of 8 windows at 29.8 to
  30.1. The third spot, post on against off back to back and the view-off median are not
  measured, and nothing of a binary with the bloom or the gamma fix is.
- **The default of `--fable2_native_post` stays off** (plan Task 7: on only if criteria 3, 6,
  8, 9 and 10 are met; 9 is open and 6 waits for plan Task 8's feed run).
- **Criterion 7 at the third spot rests on seven regions** (the far ground, the sky and two
  unlit placeholders): every near region of that spot lies under moving leaf shadow, and under
  `--readback_resolve=full` that spot runs at a few frames a second, so its shots are stills.
- Not seen in any run: the `pairing`, `post-ramp`, `post-objects` and `no-buffer` fallbacks (held
  by tests; `no-light` has run), the exposure adapting while the camera moves, night, a town, an
  interior, a menu, a cutscene (`docs/native-renderer/user-checks.md`, checks 48 to 56).
