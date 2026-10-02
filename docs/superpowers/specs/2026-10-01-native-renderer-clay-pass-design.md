# Native renderer sub-project 3: capture layer and clay pass

Status: approved design (brainstorming, 2026-10-01). Implements sub-project 3 of
`docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md`, using the
findings in `docs/native-renderer/frame-map.md`.

## Goal

Capture Fable 2's main-scene draw calls once per guest call (no tile replays) and
render them natively as untextured "clay" geometry in a debug view, matching the
emulated frame's camera and shapes. This proves the capture path; it does not
replace frames or claim performance.

## Context (from discovery)

- The main 3D scene renders to pitch 1120, a 1120x720 target, using Xenos predicated
  tiling with 3 tiles. The emulator replays tiled draws about 2.4 times each
  (6970 emulated draws per frame for 3174 guest draw calls).
- Every guest draw call is hooked: seven `DRAW_INDX` builders and seven
  `DRAW_INDX_2` builders (`tools\xdk_sigmatch\pm4_emitters.py`).
  `D3DDevice_DrawIndexedVertices` (`0x8221E0F0`) issues 2483 of the 3174.
- Hook arguments observed in gameplay (`r3` is always the device, e.g. `0x44134280`):
  - `SetPending_AluConstants?` (`0x8221DF98`): `r4` mask, `r5` bank (`0x4000` vertex,
    `0x4400` pixel), `r6` pointer to the device constant shadow (device + `0x780`).
  - `SetStreamSource` (`0x821B6DA0`): `r4` stream index, `r5` vertex-buffer object,
    `r6` offset, `r7`/`r8` one more argument than Skate 3's variant.
  - `SetIndices` (`0x8219CCD8`): `r4` index-buffer object.
  - `DrawIndexedVertices` (`0x8221E0F0`): `r4` primitive type (6 = triangle strip),
    `r5` base vertex, `r6` start index, `r7` index count.
  - `SetVertexShader?` (`0x82208CE8`): `r4` is sometimes 0; identification unverified.
- skate3recomp takes geometry and camera from Skate 3 engine structures and only
  per-object transforms from vertex-shader constants. Fable 2's engine is unmapped,
  so this design works at the XDK D3D layer instead.

## Decisions

- **Approach A: D3D-layer capture with our own HLSL.** Positions come from guest
  vertex buffers, decoded using the position element described by the game's vertex
  shader microcode; transforms come from vertex-shader constant rows identified by
  an offline matrix finder.
- Rejected **B** (re-issue draws with the SDK's translated vertex shaders at
  `IssueDraw`): exact geometry quickly, but binds the native path to emulator
  internals and keeps PM4 parsing in the hot path. It is the fallback if the matrix
  finder cannot identify transforms (D4).
- Rejected **C** (engine-level capture like skate3): cleanest long-term data but an
  open-ended reverse-engineering effort. Engine hooks may be added in later
  sub-projects for camera and materials.
- Success for sub-project 3 is a correct-geometry debug view. Characters may be
  wrong (skinning is sub-project 5). No textures, shadows or frame replacement.

## Architecture

```
game thread (guest)                            GPU thread (IssueSwap callback)
XDK hooks (shared dispatch)                    NativeGuestOutputRenderer
 |- SetStreamSource / SetIndices  -.            |- take latest FrameScene (shared_ptr)
 |- SetVertexShader               -| DrawState  |- GeometryCache: decode/upload VB+IB
 |- SetPending_AluConstants       -'            |   only when key or content hash change
 |- 14 draw functions -- DrawRecord -> FrameBuilder
 |- tiling bracket ----- main-scene flag        |- ClayPass: depth + flat shading, 1120x720
 '- census = observer of the same hooks         '- Composite: debug view over guest output
Swap hook -- publish FrameScene (immutable) ------^
```

### Components and files

Pure, unit-testable headers (no SDK or GPU dependencies) are marked (pure).

| File | Responsibility |
|---|---|
| `src/native/capture/xdk_dispatch.h` | One strong override per hooked XDK function. Calls the capture observer, then the census observer, then the original. Replaces the census-only overrides; `gen_census_hooks.py` emits dispatch entries instead of census hooks. |
| `src/native/capture/guest_read.h` | Bounds-checked guest-physical reads and big-endian loads; returns failure instead of dereferencing invalid addresses. |
| `src/native/capture/draw_state.h` (pure) | Current bindings on the game thread: streams 0..3 (object, offset, stride), index buffer object, vertex shader object, constant-bank pointer. |
| `src/native/capture/draw_record.h` (pure) | `DrawRecord`: draw function id, primitive type, base/start/count, stream and index buffer descriptors (guest address, size, stride, format, endian), vertex-shader hash, captured constant rows, skip reason. |
| `src/native/capture/frame_builder.{h,cpp}` | Main-scene bracket state, record appends, per-reason skip counters, discovery sampling. Publishes `FrameScene` at Swap. |
| `src/native/capture/vfetch_decode.h` (pure) | Decodes vertex-fetch instructions from Xenos microcode (using the SDK's header-only `ucode.h` definitions) into position element: fetch slot, offset, format, stride. |
| `src/native/capture/position_decode.h` (pure) | Big-endian position decoders: float3, float4, half4, signed 16-bit normalized x4. |
| `src/native/capture/index_convert.h` (pure) | 16/32-bit index byte swap, strip-cut preservation (`0xFFFF` / `0xFFFFFFFF`), fan and rect-list to triangle-list conversion. |
| `src/native/capture/vs_transform_table.inc` | Generated from `docs/native-renderer/vs-transforms.json`: vertex-shader hash -> constant base register and layout. |
| `src/native/render/frame_scene.h` | Immutable `FrameScene` (records + per-frame stats) and the `shared_ptr` publish/consume pair under a mutex (skate3 pattern). |
| `src/native/render/geometry_cache.{h,cpp}` | Decoded position and index buffers keyed by (guest address, byte size, stride, format) plus a content hash; fixed byte budget with LRU eviction. |
| `src/native/render/clay_pass.{h,cpp}` | 1120x720 color + depth targets, one pipeline, per-draw root-constant matrix, three color modes. |
| `src/native/render/composite.{h,cpp}` | Debug view composition onto the guest output texture. |
| `src/native/fable2_native_shaders.h` | Clay vertex/pixel HLSL and composite HLSL (raw strings, as today). |
| `tools/xdk_sigmatch/matrix_finder.py` | Offline transform discovery over discovery-mode dumps; writes `vs-transforms.json`. |
| `tools/xdk_sigmatch/gen_transform_table.py` | `vs-transforms.json` -> `vs_transform_table.inc`. |

`src/native/fable2_native_render.cpp` keeps the registration, F6 handling and
failure latch; it calls `ClayPass` and `Composite` instead of the test pattern when
a scene exists.

## Discovery tasks

Each task ends with evidence recorded in `docs/native-renderer/frame-map.md` and a
value or table checked into code. Discovery mode: environment variable
`FABLE2_NATIVE_DISCOVERY=<frames>` writes `logs/native_discovery_<timestamp>.jsonl`
(exe folder, like the census) with, for one sampled draw in N (environment variable
`FABLE2_NATIVE_DISCOVERY_EVERY`, default 64) inside the main-scene bracket: hook arguments, the first 16 dwords of each bound stream/index/shader
object, the vertex-shader microcode hash, address and decoded position element,
the full 256-row vertex constant bank, and the first 64 raw vertices of the
position stream.

| # | Question | Method | Fallback |
|---|---|---|---|
| D1 | Where does tiling begin (main-scene bracket)? | Static callers around the tile-replay function `0x82B9ED28` and the bin-mask builders; confirm 3 occurrences per frame and that the draws inside are about 2740 per frame on pitch 1120 (census `extents`, `pred_draws`). | Bracket on draw order between bin-select writes (`0x82B9E848`). |
| D2 | Vertex-buffer and index-buffer object layout (guest address, size, format, endian) | Dump objects from `SetStreamSource`/`SetIndices` in discovery mode; cross-check against the emulator's vertex fetch constants for the same draw (a debug log in the SDK at `IssueDraw`, enabled by the same discovery flag). | Device fetch-constant shadow (skate3 used device + `0x480`; the Fable 2 offset is found by the same dump). |
| D3 | Which hook binds the vertex shader, and where its microcode is | Verify the `SetVertexShader?` candidate; locate the microcode pointer and size in the shader object; decode the position element with `vfetch_decode.h`; cross-check with the emulator's shader for the same draw. | Engine vertex declarations, if the game sets them. |
| D4 | Which constant rows hold the world-view-projection transform | `matrix_finder.py`: for each sampled draw, try every 4-register window and both row/column layouts; keep the window whose projected vertices land inside the 1120x720 viewport with sane depth across all samples of that shader. | Approach B (translated vertex shaders). |

## Capture rules (game thread)

- Binding hooks only update `DrawState`. Draw hooks append a `DrawRecord` only while
  the main-scene bracket is open.
- A record copies only the constant rows named by the transform table for its shader
  (4 rows). In discovery mode, one sampled draw in N copies the whole bank.
- Unknown shader, unknown position format or unreadable memory: the record is kept
  with a skip reason and counted; nothing is guessed.
- Unbalanced or nested brackets are tolerated: a second open is ignored, a close
  without an open is ignored, and a frame ending with an open bracket closes it.
- The capture path must not allocate per draw in steady state (records go into a
  per-frame vector reserved from the previous frame's size).

## Renderer

- **Geometry cache:** on the GPU thread inside the swap callback. Content hashes are
  computed over the guest bytes referenced by the frame's records; static meshes
  upload once, dynamic buffers re-decode when their hash changes. Budget is a cvar
  (default 256 MB); eviction is LRU. Hash and decode time are reported in F3.
- **Primitives:** triangle list and strip map directly (strip cut enabled); fan and
  rect list are converted to lists on the CPU; other types are skip reasons.
- **Clay pass:** 1120x720 color + depth. Per draw a root-constant 4x4 matrix built
  from the captured rows; clip position = matrix x position; flat shading from
  screen-space derivatives. Captured order, depth test, no sorting.
- **Color modes** (cvar `fable2_native_clay_color`): `clay` (uniform), `draw` (hash of
  draw index), `shader` (hash of vertex-shader hash).

## Composite and controls

- Cvar `fable2_native_view`: `off`, `overlay` (native over emulated at 50%), `split`
  (left half emulated, right half native), `native` (native clay over the whole
  output; HUD hidden; emulation still runs underneath), `pattern` (the sub-project 1
  test pattern).
- F6 cycles the view modes while the native renderer is enabled
  (`--fable2_native_render=true`). `fable2_native_render_mode` (sub-project 1) is
  removed; README updated.
- The native 1120x720 image is stretched to 1280x720, like the game's composite.
- The emulated frame is never modified in any mode.

## Stats (F3)

New lines while the native renderer is enabled: draws captured / drawn / skipped
with the top three skip reasons, geometry cache uploads / hits / resident bytes, and
capture, decode and draw times.

## Failure handling

- RHI error or invalid guest memory in the renderer: trip the existing failure latch,
  fall back to the emulated output, log once. F6 retries (existing behaviour).
- Per-draw problems are skip reasons and never stop the frame.
- If no new `FrameScene` is ready at swap, reuse the last one.

## Testing

Unit tests (standalone clang++ via `tests\run_native_tests.cmd`, Python via
`python -m unittest discover -s tests -p "<file>.py"`; no game or GPU):

- `position_decode.h`: every format with big-endian fixtures and edge values (NaN,
  half denormals, s16 -32768).
- `index_convert.h`: 16/32-bit swap, strip-cut preserved, fan and rect-list to list.
- `vfetch_decode.h`: synthetic microcode words -> position slot, offset, format,
  stride; non-position and malformed fetches rejected.
- `frame_builder`: bracket logic (open/close, unbalanced, nested) and skip-reason
  counts.
- `geometry_cache`: hit, miss, content change, LRU eviction under budget.
- `frame_scene.h`: latest-wins publish and consume, single-threaded.
- `matrix_finder.py`: synthetic banks with a known matrix among noise (found, both
  layouts) and banks with no matrix (rejected).
- `gen_transform_table.py` and the dispatch generator: output format and skips.

Discovery checks: D1-D4 evidence as listed above, including 20 sampled draws whose
decoded addresses and position layout match the emulator's fetch constants.

## Success criteria

1. Geometry matches: in `split` and `overlay` views at three locations (town, open
   field, interior), the clay world aligns with the emulated frame: same camera,
   shapes in place to within a couple of pixels (user visual check).
2. Coverage: at least 90% of main-scene guest draws are drawn; the rest are skipped
   with reasons shown in F3.
3. One draw per call: native draw count equals captured guest-call count.
4. Stability: 10 minutes of play with the view on, no crash, no latch trips.
5. Cost: with `fable2_native_view=off` and the native renderer enabled, capture
   overhead is under 0.5 ms of guest time per frame (F3 work time, A/B against the
   renderer disabled).
6. Hands off when off: with the view `off` the emulated output is unchanged, and the
   existing suppression and census flags behave as before.

## Non-goals

Textures and materials (sub-project 4), skinning (5), lighting and shadows (6),
post-processing and UI (7), Vulkan (8), frame replacement, performance gains, render
scale above 1120x720.

## Risks

- **Transforms not a single matrix** (e.g. world and view-projection multiplied in
  the shader): the matrix finder also tries products of two 4-row windows; if that
  fails for a large share of draws, switch the affected shaders to approach B.
- **Shader microcode not reachable from the bound object** (D3): fall back to engine
  vertex declarations or to reading the emulator's shader for the same draw via a
  discovery-only SDK log.
- **Content hashing cost** on large dynamic buffers: measured in F3; if above budget,
  hash only the ranges referenced by the frame's draws and skip unchanged pages using
  the SDK's memory write tracking.
- **Geometry deforming in the vertex shader** (wind, water, skinning): shows as
  misaligned clay for those draws; acceptable for sub-project 3 and tagged by the
  `shader` color mode.

## Implementation notes (deviations)

Where the implementation differs from the design above (evidence in
`docs/native-renderer/frame-map.md` sections 8 and 9):

- **Primitives.** The RHI only has 16-bit index buffers, so every supported type
  (triangle list, strip, fan, quad list) is converted on the CPU to a uint32
  triangle list (`index_convert.h`) and the clay vertex shader pulls positions by
  index from a StructuredBuffer; there is no index-buffer copy. Rect lists are not
  supported (skip reason).
- **Files.** `draw_state.h` and `frame_builder.{h,cpp}` were folded into
  `src/native/capture/capture.cpp` (binding state, bracket, discovery) and
  `src/native/render/frame_scene.h` (`FrameBuilder`, `ScenePublisher`).
- **Transforms.** `matrix_finder.py` did not find the matrices on gameplay data;
  the transform table entries were confirmed by hand from the shaders'
  disassembly (manual entries in `vs-transforms.json`).
- **Terrain.** Tessellated terrain patches fetch no vertices; their grids are
  rebuilt from the heightmap texture and the shader's constants
  (`terrain_patch.h`).
- **Skinning.** One rigid-skin shader (one bone per vertex) is transformed on the
  CPU (`rigid_skin.h`); blended skinning stays skipped.
- **Deformed flag.** Records whose shader moves the position before the transform
  are drawn undeformed and flagged `deformed` (counted in F3 and the logs).
- **Menu.** The menu does not exercise the main-scene path, so main-scene checks
  run in gameplay, reached with in-process autoplay (`tools\drive_game.ps1`,
  `FABLE2_AUTOPLAY`).
- **Capture cost.** Draw records are built only while a composite view
  (overlay, split, native) is shown; with the view off the capture only counts
  main-scene draws. Guest-thread capture time is timed per frame (F3 and the
  `[native] capture:` log line).
