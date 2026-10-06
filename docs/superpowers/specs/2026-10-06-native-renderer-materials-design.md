# Native renderer sub-project 6: materials

Status: design (2026-10-06). Scope, fidelity, coverage target, capture method and approach
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
