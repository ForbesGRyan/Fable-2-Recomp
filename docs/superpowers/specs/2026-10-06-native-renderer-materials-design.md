# Native renderer sub-project 6: materials

Status: implemented on branch `native-renderer-materials` (2026-10-06 and 2026-10-07);
validated by autoplay: in gated runs up to commit `6a027bc`, and at the last commit,
`4d74e0e` (steep ground), in runs made with the user present under the user's
authorisation (`RUN UNCHECKED`, 2026-10-07). The user checks are pending. What differs
from this design is in "Implementation notes (deviations)" at the end.

Design of 2026-10-06. Scope, fidelity, coverage target, capture method and approach
were chosen by the user in brainstorming; the user approved the first two design sections
and then asked for the sub-project to be carried through to a verified result on the
recommended options without further approval stops. The remaining sections below are those
recommended options; choices made on the user's behalf are listed under "Rulings made
without the user". Builds on sub-projects 3 to 5
(`2026-10-01-native-renderer-clay-pass-design.md`,
`2026-10-02-native-renderer-textures-design.md`,
`2026-10-05-native-renderer-coverage-skinning-design.md`); evidence so far is in
`docs/native-renderer/frame-map.md` sections 8 to 12.

## Goal

Every draw of the native view shows its correct unlit base colour and its correct shape:

- the ground is textured with the game's own layer blend;
- cut-out geometry (grass, ferns, leaves) has the game's silhouettes instead of rectangles;
- translucent layers are blended as the game blends them, or left out when their colour is
  not known, instead of being drawn as opaque clay;
- trees, and the pixel shaders seen away from the bridge, are textured.

Shading stays flat and unlit. The long-term purpose (the user's, restated 2026-10-06) is to
get off the emulated GPU; a native picture that is right in colour and shape is a
precondition for switching emulated passes off. This sub-project brings no frame-rate gain
by itself (frame-map section 12, follow-up 8).

## Context

What the user saw on 2026-10-06 (`docs/native-renderer/user-checks.md`, checks 8, 9, 11):
placement and animation are correct; the ground, tree trunks and leaves are untextured;
grass has no transparency; the dog is a blotch of opaque fur shells.

Facts this design rests on (read-only fact sheet of 2026-10-06; file and line references
are in the plan):

- **Render state is not captured.** The game's D3D library keeps the pending GPU register
  values in its device object and flushes eight register ranges before each draw. From the
  flush code the shadow offsets are: `RB_COLOR_MASK` (0x2104) +0x28DC, `RB_BLEND_RED..ALPHA`
  (0x2105 to 0x2108) +0x28E0 to +0x28EC, `RB_ALPHA_REF` (0x210E) +0x2904, `RB_DEPTHCONTROL`
  (0x2200) +0x2934, `RB_BLENDCONTROL0` (0x2201) +0x2938, `RB_COLORCONTROL` (0x2202) +0x293C,
  `PA_SU_SC_MODE_CNTL` (0x2205) +0x2948, `RB_MODECONTROL` (0x2208) +0x2954. These are static
  derivations: no capture holds the range and no discovery row holds GPU register values.
  The engine is known to write `RB_MODECONTROL` straight into the command buffer in one
  place, so the shadow may not be the whole truth.
- **The native pipeline has one fixed state**: depth test Less with depth write, no
  culling, no blending, one pipeline object. The RHI can describe blending, depth write and
  culling per pipeline, but its enums are thin: four blend factors, two blend operations,
  three compare functions. There is no alpha test in the RHI; it has to be a `discard`.
- **Texture alpha already reaches the GPU** (DXT1, DXT2/3, DXT4/5 and 8_8_8_8 upload
  unchanged); the textured pixel shader samples a `float4` and drops `.a`. Formats `k_8`,
  `k_8_8`, `k_DXN`, `k_DXT3A`, `k_DXT5A` and `k_16` are rejected by the texture cache.
- **The material table is one albedo fetch per pixel shader** (`ps-albedo.json`, 13 albedo
  entries and 4 `no_albedo` entries). Nothing traces what feeds `oC0.w` or where a shader
  kills pixels.
- **Terrain is multi-pass, not multi-texture.** About 567 draws per frame in the bridge
  scene, eight pixel shaders. Six layer shaders (draw builder `8221C9C8`) each draw one
  detail texture (tf0, DXT1, wrapped, 2 to 10 distinct textures per shader) over the same
  patches, with colour `tf0^2 x (pow(tf3, c21.w) x c21.xyz x lighting + c20.xyz) + ...` and
  alpha `weight x distance fade`; two far shaders (`82207C30`) are opaque with
  `tf13^2 x (pow(tf3) x light + c20)`. tf3 is one 768 x 768 DXT1 base-colour map for every
  `8221C9C8` draw, addressed by `(world.xy - c47.zw) x c14.xy`. The layer weight is
  `c253.w x (tf18.w - c253.y)` read per vertex from tf18, a tiled 1650 x 1815 `k_DXT3A`
  atlas addressed through `c115`; the same value against `c114.x` is the hole test. tf18
  has never been decoded, the terrain rows of the captures do not record `c14`, `c8`,
  `c115` or the tf0/tf3 fetch constants, and no terrain texture has been dumped. Terrain
  holes are not cut today (frame-map section 9 and 10: "hole masks are ignored").
- **The other untextured shaders.** Leaves `0x7CD57B81550F19E3` (tf0 DXT1, UV not traceable
  by the tool, `oC0.w = c15.w x r0.x`); dog fur shells `0x014F8A02DB7B19CA` (colour from
  two textures that read all zero in guest memory); `0xA17D8AEC3A817D45` and
  `0xF6D98C7B4D98438B` (only a `k_8` mask traced, a predicated `kill`); six pixel shaders
  seen only in the user's session near the trees (`0x58D3FAAAA84A48AA`,
  `0x44B567635EB10620`, `0x2FBE9B4EE822609A`, `0x14217F5C1118D00F`, `0x09CB69BD265B58D1`,
  `0xF96A4494958BFA12`), in no capture, five with a dump whose tf0 UV looks traceable.
- **Autoplay cannot walk.** The in-process driver presses buttons only; its state struct
  already carries stick axes, always zero. Both plan parsers split on `,` and `:`.

## Decisions

- **Scope (user): unlit base colour, done properly.** Ground, cut-outs, blended layers,
  missing shaders. No normal maps, specular, detail tints from lighting, fog, shadows.
- **Ground fidelity (user): the exact base-colour blend.** The terrain shaders' colour up
  to lighting is ported: same layers, same weights, same tiling.
- **Coverage (user): at least 90% of all drawn draws textured**, ground included, in the
  scenes autoplay reaches.
- **Captures (user): autoplay gains stick movement**, so captures and validation can leave
  the bridge. Same in-process channel, same script; no keyboard or foreground input.
- **Approach (user): extend the offline tables; terrain is one hand-ported material.** The
  runtime looks entries up; every entry carries its evidence. Rejected: translating the
  game's pixel shaders automatically (rebuilds emulation inside the native renderer and
  drags in lighting and post passes that are not native), and a tool that extracts the
  base-colour expression (the boundary between colour and lighting is not mechanical; may
  come later once several hand ports show the patterns).

### Rulings made without the user

Recommended options taken under the user's standing instruction of 2026-10-06; each is
reversible and is restated in the final summary.

1. **Terrain holes are cut in this sub-project.** The hole test reads the same tf18 value
   as the layer weight, so decoding tf18 serves both. (During brainstorming the user was
   told holes were already cut; that was wrong, and this ruling closes the gap.)
2. **A blended draw whose colour is unknown is not drawn.** Drawing a translucent layer as
   opaque clay is a guess about its opacity, and it hides what is behind it (the dog).
   Such draws are counted under their own reason, `blend-unknown`.
3. **Render state comes from the device shadow, proven against the GPU side first.** If
   the proof fails for a register, that register is taken from a shadow the capture keeps
   itself by hooking the functions that store it; the choice is made per register on the
   evidence.
4. **The layer weight is sampled per pixel** from tf18 uploaded as a single-channel
   texture, not evaluated per vertex as the game does. The native terrain grid is fixed at
   16 x 16 per patch while the game tessellates adaptively, so per-vertex weights would
   interpolate differently anyway.
5. **Draw order stays capture order.** The game already orders its blended draws; the
   native pass does not sort.
6. **Only the texture formats this scope needs are added** (`k_DXT3A` as a single-channel
   texture; `k_8` and `k_DXT5A` only if an accepted entry needs them).

## Architecture

```
guest draw ──► capture ──► DrawRecord ──► clay pass ──► native target
                 │              ▲              ▲
   render state read       material record   pipeline per state, discard for
   from the device         from the table    alpha test, terrain material shader
```

Units, each with one job and its own tests:

| Unit | Kind | Job |
|---|---|---|
| `src/input/autoplay.h` | pure | plan grammar with stick entries; sticks at time t |
| `src/native/capture/render_state.h` | pure, new | raw register words to a typed `RenderState` |
| `src/native/capture/capture.cpp` | runtime | read the words per draw; terrain inputs |
| `src/native/capture/material.h` | pure | material record: albedo, alpha recipe, kind |
| `docs/native-renderer/ps-albedo.json` and generator | table | per pixel shader: albedo, alpha, kind, evidence |
| `src/native/render/clay_logic.h` | pure | pipeline key from `RenderState`; constants |
| `src/native/render/clay_pass.cpp` | runtime | pipeline cache, bindings, draw |
| `src/native/fable2_native_shaders.h` | HLSL | alpha test, terrain pixel shader |
| SDK `native_rhi.h` and D3D12 backend | SDK | missing blend factors, operations, compare functions |
| `src/native/render/texture_cache.cpp`, `texture_decode.h` | runtime, pure | single-channel formats |
| `tools/xdk_sigmatch/shader_trace.py`, `albedo_finder.py` | tools | alpha trace; terrain rows |

Unchanged on purpose: the geometry cache, instancing, skinning, the composite views, and
the emulated frame, which is never modified.

Order of work: autoplay movement and render-state discovery first (they unblock the
rest), then the state in the record and the pipeline, alpha test and blending, the missing
shaders, terrain, validation.

## Autoplay movement

- Grammar: a plan token is `seconds:Name`. New names, which contain neither `,` nor `:`:
  `LS=x/y/ms` and `RS=x/y/ms`, with x and y in percent of full deflection (-100 to 100,
  positive y forward, positive x right) and `ms` the hold time of this entry (1 to 60000).
  Example: `52:LS=0/100/4000` walks forward for four seconds from second 52.
- `Step` gains the stick fields; `SticksAt(steps, t)` returns the left and right stick at
  time t (the latest started entry per stick that is still held wins; outside every entry
  the stick is centred); `EndTime` includes the entries' own durations. Buttons keep the
  global hold.
- The app publishes buttons and sticks together; the driver and the snapshot struct need
  no change. `tools/drive_game.ps1` passes `LS=`/`RS=` tokens through unchanged and still
  rejects unknown keys.
- Paths are dead reckoning. Every path used for a capture or a measurement is checked with
  a screenshot of where it ended, and is kept short. Plans used by this sub-project are
  recorded in the frame map so runs can be repeated.

## Render state

Captured per draw, raw: `RB_COLORCONTROL`, `RB_ALPHA_REF`, `RB_BLENDCONTROL0`,
`RB_COLOR_MASK`, `RB_DEPTHCONTROL`, `PA_SU_SC_MODE_CNTL` (six words, 24 bytes in the
`DrawRecord`). The blend constant (`RB_BLEND_RED..ALPHA`) is added only if a draw of the
validation scenes uses a constant blend factor.

**Proof before use.** Discovery rows gain the candidate words read from the device at the
draw. The GPU side is recorded with a temporary SDK log (as for the shader constants in
sub-project 3) or a discovery-gated one: the same six registers as the emulated GPU holds
them when it executes the draw. Required: equality in every sampled draw of a capture of
the bridge scene and of one scene reached by walking. A register that fails is taken from
a capture-side shadow of its setter functions instead (ruling 3), with the same proof.

**Decoding** (`render_state.h`, pure):

```
struct RenderState {
  bool alpha_test; uint8_t alpha_func; float alpha_ref; bool alpha_to_mask;
  bool blend; uint8_t src, dst, op, src_alpha, dst_alpha, op_alpha;
  uint8_t color_mask;            // target 0, RGBA bits
  bool depth_test, depth_write; uint8_t depth_func;
  uint8_t cull;                  // none, front, back, after the front-face bit
  bool valid;                    // false: unknown enum value or unreadable word
};
```

Unit-tested with real captured words. An invalid state draws as today (opaque, depth Less,
write on, no cull) and is counted.

**What the pass does with it:**

- *Pipelines.* State is part of the pipeline, so the pass keeps a small cache keyed by
  (textured, kind, blend factors and operations, colour mask, depth test, write and
  function, cull). The key function is pure and tested; the cache is bounded (a state
  combination beyond the bound draws with the default pipeline and is counted).
- *Alpha test.* The pixel shader discards when `alpha <compare> ref` fails, with the
  captured compare function and reference in the draw constants (two free words exist).
  Alpha-to-mask is treated as an alpha test against 0.5.
- *Blending.* The captured factors and operations, mapped to the RHI. Factors, operations
  and compare functions the RHI lacks are added to the SDK (enums and the three D3D12
  mappers; no new exports).
- *Depth.* Captured test, write and function. Terrain layers rely on this (layers over the
  same patch at equal depth).
- *Colour mask.* A draw that writes no colour paints nothing; it still writes depth if its
  state says so.
- *Cull.* Captured mode. The clay pass culls nothing today; if the proof shows the
  captured mode removes faces the emulated image shows (a winding convention difference),
  culling is flipped once, globally, with the evidence recorded.

## Material table

`ps-albedo.json` entries gain two optional keys:

- `"alpha"`: where the output alpha comes from. Forms:
  `{"source": "albedo", "component": "w", "scale": "c15.w"}` (a component of the albedo
  sample, times an optional pixel constant), `{"source": "one"}` (opaque), or
  `{"source": "fetch", "slot": n, "component": "x", ...}` (another fetch with the same UV
  rules as the albedo). Without the key, alpha is 1.
- `"kind"`: `"plain"` (default) or `"terrain-layer"` / `"terrain-far"` (see Terrain).

`shader_trace.py` gains `trace_ps_alpha`: it walks `oC0.w` back to one fetch component
times at most one constant, and reports every `kill`. It refuses anything else; refused
shaders are traced by hand with instruction numbers, as before. A shader with a `kill`
that is not predicated on alpha test state gets its recipe only if the kill is the same
comparison as the alpha recipe; otherwise the kill is recorded as a known limitation of
that entry.

Rules that follow from "never guess":

- alpha test or blending enabled, entry has an alpha recipe: applied;
- alpha test enabled, no entry or no recipe: the draw is drawn without the test and
  counted `alpha-unknown` (shape may be too large, colour is still right or clay);
- blending enabled, no colour known for the draw (`no_albedo`, `ps-unknown`, texture not
  usable): the draw is not drawn, counted `blend-unknown` (ruling 2);
- blending enabled, colour known, no alpha recipe: drawn with alpha 1 and counted
  `alpha-unknown`.

Evidence rule for an entry, extending sub-project 4's: the dump reading with instruction
numbers; a decoded thumbnail of the texture; for the alpha recipe, the share of sampled
draws of that shader whose captured state has alpha test or blending on; and a native-view
screenshot against the emulated frame for at least one draw of the shader.

**Shaders to enter**

| Pixel shader | Work |
|---|---|
| `0x7CD57B81550F19E3` (leaves) | hand trace of the tf0 UV and of `oC0.w`; `"uv"` entry for `0xA584...` |
| the six near the trees | capture by walking; `albedo_finder` and traces; entries with alpha; the one without a dump is dumped in the same run |
| `0x014F8A02DB7B19CA` (fur shells) | stays `no_albedo`; falls under `blend-unknown` if its state blends |
| `0xA17D...`, `0xF6D9...` | read the dump and the captured state; entry or a recorded reason |
| grass and plant shaders already textured | alpha recipes |

## Terrain

**Inputs captured per terrain draw** (added to the terrain record and to the discovery
rows): the tf0, tf3 and tf13 fetch constants; the constants the colour and the weight use
(`c8`, `c14`, `c20`, `c21`, `c31`, `c114`, `c115`, and the camera position the fade uses);
the tf18 fetch constant is already read for the heightmap path. Shader literals (`c253`)
come from the literal table as in sub-project 5.

**Reading first.** All eight pixel shaders and the three vertex shaders are read
instruction by instruction before any code: which terms are base colour, which are
lighting, sun, fog; what each constant holds (sampled values recorded); how the weight and
the fade are formed; what the far shaders do. The reading is written into the frame map.
Unlit rule: the lighting factor is 1; additive light, sun and fog terms are dropped; what
is dropped is listed.

**Native material.** Two terrain pixel shader variants in the clay pass:

- layer: `colour = detail^2 x base'`, where `detail` is tf0 at the game's detail UV,
  `base'` is the base map term as the reading establishes it (`pow(tf3, c21.w) x c21.xyz`
  from the first reading), and `alpha = weight x fade` with
  `weight = c253.w x (tf18 - c253.y)` sampled per pixel and
  `fade = 1 - sat(dist^2 x c31.x + c31.y)`;
- far: opaque, `colour = tf13^2 x base'`.

World xy is already the clay vertex position for terrain; the vertex shader passes it on.
Blending and depth for the layers come from the captured render state like any other draw.
The same facet shade as other textured draws is applied.

**Holes.** The layer and far shaders discard where the hole test holds
(`tf18 < c114.x` at the pixel), which cuts the ground where the game writes NaN positions.

**tf18.** `k_DXT3A` (4-bit alpha blocks), tiled: decoded on the CPU to an 8-bit
single-channel texture through the texture cache (new format path, content-hashed and
budgeted like the others). The decode is validated by picture: the weight and hole map of
the bridge area must be recognisable against the emulated ground (paths, the lake shore).

**Bindings.** Terrain draws use a second texture table (base, detail or far texture,
weight) so that plain draws keep their one-texture table. Constants go in the draw
constants if they fit, otherwise a constant buffer.

**Checks.** A Python replay of the layer formula on dumped textures and constants for a
sampled patch produces an image that is compared with the emulated ground of the same
area with lighting flattened by eye, and the native overlay is compared at the bridge, by
the trees and at one more spot. Terrain draws count as textured when their kind is known
and their textures are usable.

## Stats (F3 and log)

- `Textures:` line: terrain counts as textured; new reasons `alpha-unknown`,
  `blend-unknown`; the share is of all drawn draws.
- `Native:` line: draws left out as `blend-unknown` are counted as skipped with that
  reason.
- One new line or suffix: pipelines in the cache, draws with alpha test, draws blended,
  draws with an invalid state.
- Log breakdowns by pixel shader for the two new reasons.

## Failure handling

- Unreadable or invalid render state: default state, counted. Never a crash, never a
  skipped draw for that alone.
- Pipeline cache full: default pipeline, counted.
- Terrain texture unusable (pending, bad, over budget): the draw falls back to flat clay
  for that frame, counted under the existing texture reasons; holes need tf18, so without
  it no hole is cut.
- A table entry that does not match what the decoder finds: the existing reasons
  (`uv-unsupported`, `format-unsupported`); nothing is drawn from guessed data.
- With `fable2_native_view=off` nothing of this runs per draw.

## Testing

- Pure units by unit test, red first: plan grammar and `SticksAt`; `RenderState` decode
  from real words including garbage; pipeline key; material record and generator; alpha
  recipe evaluation rules; DXT3A decode against a hand-computed block and a real dumped
  region; terrain constants packing.
- Tools: `trace_ps_alpha` on real dumps (accepts the simple shaders, refuses the rest with
  a reason); generator rejects malformed `alpha` and `kind`.
- Offline proofs on captures: render state equality with the GPU side; terrain formula
  replay.
- Gameplay only through `tools/drive_game.ps1`, with the freeze signature checked before
  any number is used (`docs/freeze-investigation.md`).

## Success criteria

1. **Ground:** in overlay view the native ground pattern (paths, grass, dirt, rock) lies
   on the emulated ground at the bridge, by the trees and at one more spot reached by
   autoplay walking; holes are cut where the emulated ground has them.
2. **Cut-outs:** grass, ferns and leaves show the emulated silhouettes, not rectangles;
   the dog is no longer a blotch.
3. **Trees:** trunks and leaves are textured near the trees.
4. **Coverage:** at least 90% of all drawn draws are textured, ground included, in the
   scenes autoplay reaches.
5. **Cost:** 30 fps with a native view on at the default cap; view-off capture overhead
   under 0.5 ms per frame.
6. **User checks** (logged in `docs/native-renderer/user-checks.md`, never blocking): the
   same things in town and in an interior.

## Non-goals

Lighting, shadows, fog, normal and specular maps, vertex-colour tints that depend on
lighting, water, particles and billboards, sorting of translucent draws, the 2D passes,
switching off emulated passes, any frame-rate work.

## Risks

- **The device shadow may not hold every register** (direct command-buffer writes). The
  proof step exists for this; the fallback is a capture-side shadow.
- **Terrain layers at equal depth** need the game's depth function and blending to be
  reproduced exactly on a grid that differs from the game's tessellation; a coarser grid
  can put a layer slightly above or below its base. If z-fighting shows, the layers get a
  depth bias or draw with depth test off against a terrain depth pre-pass; decided on the
  screenshot.
- **tf18 has never been decoded**, and its atlas addressing (`c115`) is read from one
  vertex shader only.
- **The base-colour boundary is a judgement.** What is "lighting" in the terrain shaders
  is read from the dump; the unlit result cannot be compared number for number with the
  lit emulated frame.
- **Dead-reckoned autoplay paths** can end somewhere else after a game or timing change;
  every use checks a screenshot.
- **RHI changes touch the SDK submodule** (rebuild, both the game and the plugin use it).
- **The six session shaders may need more than one capture** to be found by walking.

## Implementation notes (deviations)

Written at the end of the sub-project (plan Task 11, 2026-10-07). The design above is left
as it was approved; this section lists every difference between it and what was built, and
every ruling of the execution ledger
(`.superpowers/sdd/2026-10-06-native-renderer-materials/progress.md`). Rulings are quoted
from the ledger, shortened to the decision and its reason. Measurements, screenshots and
the known limitations are in `docs/native-renderer/frame-map.md` section 13,
"Validation";
in short: criteria 1 to 5 are met at three spots of one region. Flat ground, cut-outs,
trees, coverage and cost were measured in gated runs up to `6a027bc`. The last commit,
`4d74e0e`, was run on 2026-10-07 from 12:32 to 12:44 with the user present ("Process",
below): the steep-ground rock shows in four small areas; in the two where the emulated
frame is readable it has rock in the same place (in the other two it is too dark to tell).
The flat ground is unchanged, 98 to 99% of the drawn draws are textured and every window
without a capture reads 30.0 fps. The user checks are pending.

### Success criteria as ruled

The list under "Success criteria" was changed by rulings. What was validated is this:

1. **Ground:** the emulated ground's materials in the same places at the bridge, by the
   trees and at a third spot, without seams. "Holes are cut" is withdrawn (no ground
   square has a hole). The base map tf3 and its tint are not drawn (they are light). Steep
   ground has the game's side projection (plan Task 10b).
2. **Cut-outs:** the alpha test, and alpha-to-mask by the game's sample count at one
   sample per pixel: closed foliage is closed; a fading draw shows a dot grid or is more
   solid than the emulated one.
3. **Trees:** leaves and trunks textured, and the canopy drawn (plan Task 7d).
4. **Coverage:** unchanged, at least 90% of all drawn draws textured at each place.
5. **Cost:** 30.0 fps in live windows, with single native-view windows below 30.0 at the
   third spot accepted as a limitation of the debug view; view-off capture median under
   0.5 ms; the capture median with records on is reported, and its rises are accepted.
6. **User checks:** unchanged; `user-checks.md` checks 17, 18 and 23 to 26.

### Process

- "Ruling: execution by subagent-driven development, the recommended method, without the
  plan-review stop — per the standing instruction."
- The user stopped the game runs during Task 7 and later allowed them only while away from
  the machine. "Ruling: under the no-game instruction a task is reviewed and marked
  complete on its offline evidence, with its screenshot and gameplay checks listed as
  'pending game runs' in the ledger; the batched game session then runs them all, and any
  failure reopens the task that owns it." "Ruling: 'finished' for sub-project 6 under
  no-game mode means every task done and reviewed on offline evidence, a whole-branch
  review with its fix wave, the pending game runs collected in one list (frame-map and
  user-checks), and a local merge to master ...; validation in the game ... stays open and
  is run in one batched session when the user allows the game." The second was overtaken:
  the runs were allowed again the same day and were made task by task.
- "Ruling: 'idle' is 300 seconds without keyboard or mouse input before a run, and no
  input during it." "Ruling: the gate moves into the driver — drive_game.ps1 refuses to
  launch unless the idle gate passes and watches presence signals for the whole run,
  closing the game and marking the run disturbed on any signal; fail closed everywhere ...
  enforcement by tool, not by each agent's discipline." "Ruling: I5 — audio output activity
  on the default playback device is added as a presence signal." "Ruling: the idle floor is
  300 s for any run without -UserPresent (no parameter lowers it)." "Ruling: audio needs
  the same 300 s of silence as the other signals (one constant) — a short pause in a video
  must not open the gate." "Ruling: the two tests that pass -UserPresent stay (they prove
  the refusal and the aborted path in an empty folder and cannot reach a launch)." This is
  plan Task 7b, which the design does not have: `tools/idle_check.ps1`, the gate, watch,
  job object and dead-man switch in `tools/drive_game.ps1`, `tests/test_idle_gate.py`,
  README "Rule for automated agents". "Ruling (R9): wording fix [the gate's
  `user present (audio)` progress line while it is only observing] deferred to the final
  fix wave with Task 7b's minors." Done there: the line now reads `idle gate: observing:
  N of 300 s without sound; waited ...` ("Final fix wave", below).
- "Ruling: add a small task (Task 7a) after Task 7: autoplay plan tokens that switch the
  native view mid-run (`seconds:VIEW=off|split|overlay|native`) and arm a discovery capture
  at a given time, so one run can tour bridge / trees / a third spot in every view with
  emulated and native shots from the same camera position." Not in the design; README,
  `FABLE2_AUTOPLAY`.
- "Ruling: Task 10b's game check is carried into Task 11 as its first step (B0), with a
  hard stop if the ground is broken — why: the user is at the machine, and both tasks need
  the same three runs."
- **Runs with the user present.** The idle gate refused every gated launch for `4d74e0e`
  (five, the user being at the machine). On the user's word ("run it anyways. i'm fine
  with it", 2026-10-07) the controller started six runs in the user's session between
  12:29 and 12:44 with `tools/drive_game.ps1 -UserPresent`, the switch the rules reserve
  for the person at the keyboard. The authorisation covered the runs then pending for
  this validation and nothing else: the rule that agents never pass `-UserPresent` and
  run the game only while the user is away stands unchanged, and no agent started these
  runs. They are not gated runs: their verdict is `RUN
  UNCHECKED: -UserPresent given, no idle gate and no watch`, nothing watched for input,
  F6 presses reached the game in two of them and one froze. Each was checked window by
  window for the freeze signature, and for the camera position, before anything of it was
  used (frame-map section 13, "Validation").
- **The plans were shifted by 6 s for four of those runs.** In the first run the game's
  loading phase ended at 24.70 s (22.74 to 22.91 s in the gated runs), 1.7 s after the
  plans' first menu press at 23.0 s: the press was lost and the run stopped in the main
  menu at a "New Game" dialog. The four runs with a native view were then started with
  every plan time 6 s later. The renderer-off run was started with the default plan, not
  shifted; its loading phase ended 0.02 s after the press, which was still taken. In the
  gated runs the press comes 0.09 to 0.26 s after the end of the loading phase; where
  between 0.02 s and 1.7 s before that end a press starts to be lost is not known.
  Fixed-time plans are a limitation. The driver's `-PlanShift` (final fix wave) moves a
  plan by whole seconds; waiting for the menu is a follow-up.
- "Ruling: the next sub-project is lighting and shadows (roadmap 6) ...; post-processing
  and 2D replay (roadmap 7, with the user's native menus/cutscenes TODO) follows." It
  takes over what this sub-project left to lighting (below).

### Autoplay movement

Built as designed (`LS=x/y/ms`, `RS=x/y/ms`, `SticksAt`, `EndTime`). `Parse` takes an
error pointer. `tools/drive_game.ps1` checks stick tokens by shape only; an out-of-range
value reaches the game, which rejects the whole plan (a deferred minor). The `VIEW=` and
`DISC=` tokens were added by ruling (above).

### Render state

- **Words captured.** The design has six words read from the device shadow. Built: nine
  state words per record and, since Task 7c, the surface word. "Ruling: RB_COLOR_MASK is
  taken from the device shadow — it is the value of the colour passes, and the native pass
  draws each captured draw once as a colour pass; the checker compares it on colour-pass
  executions and reports the pre-pass separately." "Ruling: the viewport's Z scale word
  (PA_CL_VPORT_ZSCALE, register 0x2113 ...) joins the captured words, and Task 6 mirrors
  the depth compare function when it is negative (GreaterEqual becomes LessEqual on the
  clay pass's own depth) instead of assuming reversal always." No capture-side shadow was
  needed (design ruling 3): thirteen registers are equal to the GPU's in every colour-pass
  execution of the proof captures. Not compared with the GPU, device shadow only:
  `RB_SURFACE_INFO` (the sample count), `RB_COLOR_INFO` (the target format) and the bool
  constant word.
- **Depth pre-pass draws** (not in the design). "Ruling: the roughly three terrain draws
  per frame that the GPU executes only in the depth pre-pass are not separated in this
  task ...; Task 10 checks whether they show", then "Ruling (replaces the earlier
  deferral): pre-pass-only draws are settled now, in Task 2's fix round ... a draw the
  device marks as pre-pass-only is then not drawn by the native pass — a full colour shader
  on depth-only patches would paint ground the game does not show", and "Ruling: a
  main-scene draw the device marks as pre-pass-only (no colour-pass bit in the bin mask low
  word) is skipped at record assembly with a new skip reason `depth-only`; the bin mask low
  word and PA_CL_VTE_CNTL join the captured words."
- **The RHI was not extended** (the design's Architecture row "SDK `native_rhi.h` and
  D3D12 backend" and "Factors, operations and compare functions the RHI lacks are added to
  the SDK"). "Ruling: Task 4 (SDK RHI enums) is dropped — the census needs only factors,
  operations and compare functions the RHI already has (after mirroring, depth uses Less /
  LessEqual; alpha test is a shader discard), except the constant-colour blend factors used
  by about one draw per frame in the walking scene; draws with a blend state the RHI cannot
  express are left out and counted blend-unknown ... recorded as a follow-up." "Ruling:
  draws whose blend state uses a constant-colour factor are counted blend-unknown, keyed on
  the blend factors (their vertex shader 0xECD6... is also the main opaque mesh shader)."
  The blend constant is therefore not captured.
- "Ruling: stencil state is not captured (not needed for base colour or shape) — recorded
  as a known limitation."
- **Cull.** "Ruling: the front-face bit — PipelineKey gets no separate field; Task 6
  resolves cull to none/front/back for the pipeline after applying the front-face
  convention it establishes." Built as one mapping for meshes and terrain (a clockwise
  front face keeps the captured mode, a counter-clockwise one exchanges front and back),
  not the design's global flip.
- **Alpha-to-mask.** The design says "Alpha-to-mask is treated as an alpha test against
  0.5". That rule deleted geometry the game shows and was replaced three times. "Ruling:
  alpha-to-mask is applied as alpha >= 0.5 — exact for the 0xAA offsets without MSAA, an
  approximation for the 0x87 dither pattern." "Ruling (replaces 'alpha-to-mask as alpha >=
  0.5'): alpha-to-mask is applied with the game's per-pixel dither thresholds — covered iff
  alpha >= 1 - offset/4, the 2-bit offset taken from bits 24-31 of the captured
  RB_COLORCONTROL indexed by (x&1)<<1 | (y&1), as the emulator does without MSAA ... the
  game fades objects in and out through this dither (level-of-detail cross-fades), and a
  flat 0.5 threshold deletes geometry the game shows." "Ruling: the no-multisampling form
  of the alpha-to-mask rule is the target (one threshold per pixel of the 2x2 pattern)."
  "Ruling: the no-multisampling form stays although the game uses 2x." Game session 1 then
  showed a grid of holes on closed foliage, and the rule in force is: "Ruling (R1): the
  clay pass has one sample per pixel; it draws a pixel iff at least half of the game's
  samples would be covered: 2x alpha >= 0.5 - offset/8; 4x alpha >= 0.5 - offset/16; no
  multisampling alpha >= 1 - offset/4. The sample count comes from RB_SURFACE_INFO (0x2000,
  bits 16-17) of the draw — why: matches the emulated frame's 'closed, slightly
  translucent' crowns without multisampling the clay target; a half-covered pixel shows as
  solid instead of 50% — cost if wrong: fading objects look more solid than emulated until
  alpha drops under the threshold (then a dot grid); the exact fix is a 2x clay target, a
  later decision." "Ruling: sample value 3 stays 'no multisampling' until a capture shows
  it; the comment notes the emulator's reading."
- **Invalid state.** The design draws an invalid state "as today (opaque, depth Less,
  write on, no cull)". Built so for a state that cannot be read; for a depth function the
  renderer cannot express only the depth state falls back; and a terrain record with an
  unusable state is not drawn with the default at all: it makes its ground square fall back
  to flat clay, drawn with depth less-equal (Task 10, fix round 1).
- "Ruling: terrain layers are drawn as opaque flat clay until Task 10 gives them a material
  (the implementer's flat_terrain flag) — the blend-unknown rule taken literally would
  remove the ground ... for the duration of Tasks 6 to 9." Temporary; since Task 10 the
  flag is only the fallback.

### Material table

- Design ruling 2 (a blended draw whose colour is unknown is not drawn and is counted
  `blend-unknown`) and the four "never guess" rules are built as designed.
- "Ruling: one macro for the kind — Task 5 emits FABLE2_PS_KIND(hash, kind) only for plain
  entries that set a kind; Task 9 replaces it for terrain entries with
  FABLE2_PS_TERRAIN(hash, kind, base_slot, detail_slot) and removes FABLE2_PS_KIND if
  nothing else uses it." `FABLE2_PS_TERRAIN` has three more fields since Task 10b (side
  slot, steep threshold, steep epsilon), and a third terrain kind, `terrain-far-fade`.
- **The alpha recipe.** The design has "a component of the albedo sample, times an
  optional pixel constant". "Ruling: the alpha recipe takes up to two pixel constants
  (schema 'scale': one reference or a list of two; alpha_scale is their product) and the
  trace tool follows such chains." "Ruling: recipes may ask for the colour to be divided by
  the texture alpha (table key divide_by_alpha, from the dump reading), applied in the
  native pixel shader — the game's shaders do it, and without it bilinear edge texels of
  punch-through textures would leave dark rims." The with-and-without comparison that
  ruling asks for was not made (it needs a scratch build); no rim was seen either way in
  Task 6's trial or in the shots. "Ruling: divide_by_alpha is false for 0x8D90 and 0x648B —
  their division sits under cexec b128 and the device's bool constant word reads b128 clear
  in all 52,435 draw rows (device shadow, not compared with the GPU)." Not settled on
  screen.
- "Ruling: the instanced plants' distance dissolve is left at 1 — it needs the eye
  position, a noise texture and screen-space sampling for a thinning effect an unlit debug
  view can do without — plants stay solid out to the vertex shader's cut radius."
- **Shaders entered.** As the design's table, with these differences. "Ruling (R2): enter
  the two leaf vertex shaders now (Task 7d), not as a follow-up — why: they are the
  dominant difference of the trees view." "Ruling (R3, R4): root-cause the flower / fern
  order from the capture ... and enter 0x44B5 in the same fix task as R1 (Task 7c)."
  "Ruling: the 0x44B5 entry stands without its screenshot — why: same terms as Task 7's
  unseen entries (dump trace, row constants, texture rows all agree; the entry says 'not
  seen'); 16 of its 20 rows are not drawn anyway (blend-unknown)." `0xF96A4494958BFA12` is
  in no capture and has no entry. `0xA17D...` and `0xF6D9...` keep a recorded reason (a
  mask-texture discard that fits no recipe): their draws are `blend-unknown` and
  `alpha-unknown`. The fur shells stay `no_albedo` and are left out as `blend-unknown`, as
  designed.
- **The evidence rule's fourth item** (a native-view screenshot per shader) is not met for
  `0x44B5...`, `0x648B...`, `0x1421...`, `0x09CB...` and the ivy draw of `0x00E0...`: their
  draws could not be seen or told apart in any shot. Their entries rest on the dump traces,
  the capture rows and the textures.
- "Ruling (R3): wind sway of instanced plants is not built in this sub-project — why: it is
  vertex animation (sub-project 5's deformation follow-up), not a material; a static plant
  a few pixels off is not a material defect."

### Terrain

The reading (plan Task 9) and the first capture with terrain textures (game session 1)
contradicted the fact sheet this design was written from in five places; frame-map section
13, "Terrain", "What differs from the fact sheet", has them. What changed in the design:

- **Holes** (design ruling 1, the "Holes" paragraph and criterion 1). "Ruling: spec ruling
  1 ('terrain holes are cut') is withdrawn — the reading shows the test removes single
  layers' triangles, not the ground; the layer discard is part of the layer material
  instead." "Ruling (Task 10, holes): no hole discard is built (the spec's 'holes cut'
  criterion is withdrawn): no ground square of the two captures has an open texel; the
  `c114.x > tf18.w` test is kept as a discard because it is one line and is the game's own
  cull of zero-weight layer pixels."
- **The colour** (the "Native material" paragraph: `detail^2 x base'`). Preliminary:
  "Ruling (preliminary ...): Task 10 draws the flat-ground form for every layer (A = tf0^2)
  and the far and far-fade kinds with their materials; the steep-ground side projection of
  0x9C21 / 0x4524 / 0x04F2 becomes Task 10b", and "Ruling (preliminary): the sun colour
  c21.xyz, the factors c11.x and c20.w are 1 and c20.xyz is dropped in the unlit rule;
  layers are blended in display space." In force: "Ruling (Task 10, unlit rule): layer
  colour = the detail texture alone (A = tf0^2 in the game's linear space, written to the
  clay target as tf0, the stored value); the tf3 term, the tint c21.xyz, c20.xyz, c11.x and
  c20.w are dropped — why: tf3 is baked light and the tint is the ambient colour: both
  belong to lighting (sub-project 7)." So the base map is not sampled and has no binding;
  the table keeps `base_slot` for the lighting sub-project. "Ruling (Task 10, blending):
  each layer draw is drawn with its captured blend state, alpha = fade x weight, colours in
  display space — why: weights partition 1 and are nearly binary, so display-space mixing
  differs from the game's linear mixing only in transition bands about one weight texel
  wide." A negative weight is clamped to 0, where the game's float target lets it subtract.
- **Steep ground** (not in the design; "the exact base-colour blend" needs it). "Ruling
  (Task 10 / 10b, steep ground): Task 10 draws every terrain pixel with the flat-ground
  form; Task 10b adds the steep-ground block (side projections weighted by the normal)
  directly after it, inside this sub-project — why: the block touches about a third of the
  near ground here." Built: a third texture per layer (its rock-face texture), the game's
  normal map (`k_DXN`) decoded on the CPU and handed to the shader per vertex, and two more
  literals in the table.
- **The far ground** (the design's far variant, `tf13^2 x base'` addressed by the world
  position). Its colour texture is addressed through two `k_16` tables, read per vertex;
  the far shader `2F8A...` is the layers' underlay and fades in where they fade out, its
  own kind.
- **The weight.** "Ruling: terrain layer weight sampled per pixel from tf18 as a
  single-channel texture, not per vertex (spec ruling 4)." Built so, with a linear filter.
  "Ruling: review Minor 2 ('0xC30A reads tf18 component x') is rejected — the dump reads
  `tfetch2D r0.___w, r1.yx, tf18` at instr 24."
- **Bindings.** The second texture table holds colour, weight and side texture (the design:
  base, detail or far texture, weight). The constants do not fit the draw constants and are
  a constant buffer, as the design allows.
- **Fallback** (the design: "the draw falls back to flat clay for that frame"). "Ruling
  (Task 10, fallback): a terrain square whose inputs or textures are not usable is drawn as
  flat clay as a whole (all its layers), never half textured." New reasons `terrain-inputs`
  and `terrain-square`, and a `Terrain:` log line that names the cause.
- **Depth** (the design's risk "terrain layers at equal depth"). No bias and no pre-pass
  was needed: underlay and layers are built from the same heightmap on the same grid
  points.
- **Formats** (design ruling 6). `k_DXT3A` as designed. `k_DXN` and the `k_16` tables are
  decoded on the CPU for the normals and the far coordinate; they are not texture-cache
  formats. `k_8` and `k_DXT5A` were not needed.
- "Ruling (R5, R6 tool part): `--check-dumps` classifies textures the unlit replay does not
  read (fog tables, sun mask) as 'not needed' and must end with 0 for the needed ones; the
  legend gets distinct colours."
- **Checks.** The Python replay exists (`tools/xdk_sigmatch/terrain_replay.py`) and was
  compared as designed; a number-for-number comparison with the lit emulated ground is not
  possible, as the design's risk says.

### Draw order, stats, cost

- Draw order is capture order (design ruling 5); nothing sorts blended draws.
- Stats as designed, plus the `Terrain:` log line, the skip reasons `depth-only` and
  `terrain-square`, and the reasons `terrain-inputs` and `terrain-square` on the
  `Textures:` line.
- "Ruling (R8): accepted — 1.285 ms with records on, view off 0.04-0.05 ms (limit 0.5 ms);
  the rise is the price of the scale constants and terrain inputs." The capture median with
  records on was 1.20 ms before the sub-project.
- "Ruling (I3): no build of 54fb4f6 for an A/B (agents do not switch branches or make build
  trees); instead ... one run of the third-spot walk with alternating view-off and native
  windows (same run, same place), repeated up to three times." "Ruling (I3 closed): 30.0
  fps holds in 8 of 10 native windows and 8 of 8 view-off windows at the third spot;
  single-window dips in the native view are an accepted limitation of the debug view (the
  native pass adds about 1.6 ms of guest-thread work and GPU load on top of the emulated
  GPU)."

### Final fix wave

After the whole-branch review (2026-10-07; three commits after `0244c8d`: code and tools,
the driver, documents). "Ruling (fix wave scope): done in the wave: I1 for both generators,
minors 1, 2, 3, 5, 6, 8, 9, Task 7c m1, Task 10 n1-n4, Task 10b p4 and p5, Task 7a (t = 0,
'(changed)'), Task 9 (_locate, _cull_edges), the driver's plan validation, verdict wording
and Task 7b minors m3-m10, a -PlanShift option for slow starts, and every documentation
item of the Task 11 review and of the earlier doc minors. Not done, recorded as follow-ups
in the frame map: moving TerrainMaterialInputs out of DrawRecord (minor 7), the per-square
allocation (minor 4), a full hash of the weight atlas, heap-exhaustion behaviour,
pixel-level shader tests, Task 7b m1 / m2 (millisecond gaps), the older test gaps of Tasks
2, 5, 8."

What a user of the debug view or of the driver sees of it:

- The table generators refuse malformed entries by name (an unknown key, a shader named
  twice, a slot that is not an integer, a missing `"manual": true` or evidence); the
  generated tables did not change.
- The log's `Terrain:` line also names ground draws without geometry (`no-positions`,
  `no-indices`), and the State line's overflow counts a ground draw that found no pipeline.
- The ground's plan has a work budget; over it, and when a far draw has more patches than a
  grid is built for, the near ground of that frame is flat clay as a whole.
- `tools/drive_game.ps1` refuses a plan the game would reject before anything is launched
  (exit 2), ends `RUN ABORTED: the game did not accept the plan` when the game's log lacks
  its `[autoplay] armed` line, has `-PlanShift N` for a slow start, always prints its
  verdict as the last line, and its gate reports what it has observed (`idle gate:
  observing: N of 300 s without sound; ...`) where it used to print `user present (audio)`.
- A plan entry at second 0 runs; a `VIEW=` entry naming the view the game is in is logged
  as unchanged.

The game check of the final code was not made: the gated driver refused both launches of
2026-10-07 (14:31 and 14:45; the user was at the machine: input and sound). The wave's
changes to the capture and the clay pass rest on unit tests and reading, and two gated runs
(bridge and trees) are pending: frame-map section 13, "Final fix wave".
