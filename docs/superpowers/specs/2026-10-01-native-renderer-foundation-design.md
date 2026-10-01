# Native Renderer Foundation — Design

Date: 2026-10-01
Status: Draft for review
Scope: sub-projects 1 (SDK native-renderer hook layer) and 2 (discovery tooling + frame map)

## Background and goal

Fable 2 currently renders by emulating the Xbox 360 Xenos GPU (ReXGlue SDK GPU
plugin `rexgpu-xenos`, D3D12 default, Vulkan optional). The long-term goal is a
**native renderer**, modelled on skate3recomp v2: the game's draw data is
captured from guest functions/memory each frame and the scene is rendered
directly with D3D12 (later Vulkan), with the emulator kept as an always-available
fallback.

Motivations (all three, equally weighted): performance, visual upgrades
(ultrawide, AA, render scale, better shadows/AO), and eliminating emulation
bugs (e.g. the hero/dog resolve-readback class of issues).

### How skate3recomp does it (reference)

- **SDK layer (~5k lines, game-agnostic):** at guest swap, inside
  `IssueSwap` -> `RefreshGuestOutput`, the command processor calls a registered
  game callback with an RHI command list and the guest-output texture. Returning
  `true` replaces the frame; `false` falls through to the emulated blit. While
  native is active, emulated draws/resolves are suppressed per render-target
  pitch (PM4 parsing, fences, queries, memexport still run) and occlusion queries
  report "visible". A backend-agnostic RHI (`native_rhi.h`) has D3D12 and Vulkan
  implementations.
- **Game layer (~40k lines, Skate-3-specific):** ~50 guest-function overrides at
  two levels — engine (scene draw lists, RenderMesh, skinning palettes, entity
  constants, camera) and **statically linked XDK D3D device functions**
  (SetStreamSource, SetIndices, DrawIndexedVertices, Set VS/PS, render state,
  shader-constant banks). An immutable `FrameScene` snapshot is published at
  swap; the renderer decodes vertices/indices/textures from guest memory
  (SDK untiling helpers) and uses hand-ported HLSL materials verified against
  the game's ucode. HUD is replayed natively. Built incrementally: clay
  geometry -> albedo/lightmaps -> materials -> decals -> skinning -> shadows/AO/
  post -> 2D/menus/FMV.

### Where Fable 2 starts

- Strong RE tooling (function trace, timed ablation, heap scan, register/memory
  dumps, Lua runner, strong-symbol overrides).
- No RTTI or symbols; the 3D renderer is unmapped. Only the UI text path is
  documented. The XDK D3D runtime is roughly located near `VdSwap`
  (`0x82B9xxxx`–`0x82BAxxxx`; `sub_82B9CD68` calls `VdSwap`).
- No data yet on guest-CPU vs GPU-emulation frame-time split.

### Key insight

Fable 2 and Skate 3 statically link the same Microsoft XDK D3D library. Skate 3
title update 3 (`default.xex` + `default.xexp`, SHA-256
`eb9ef910…1f4c`) plus skate3recomp's named addresses let us identify Fable 2's
D3D device functions by masked instruction matching, instead of reverse
engineering Lionhead's engine first.

## Decomposition

| # | Sub-project | Depends on |
|---|---|---|
| **1** | **SDK native-renderer hook layer + game plumbing (this spec)** | — |
| **2** | **Discovery tooling + frame map (this spec)** | — |
| 3 | Capture layer + first native pass (untextured world geometry) | 1, 2 |
| 4 | Textures + materials | 3 |
| 5 | Skinned characters | 4 |
| 6 | Lighting + shadows | 4 |
| 7 | Post-processing + 2D/UI | 4 |
| 8 | Vulkan RHI backend | 1 |

Sub-projects 3–8 get their own specs after sub-project 2's frame map exists.

## Decisions

- Backend strategy: **D3D12 first behind the backend-agnostic RHI**; Vulkan is
  sub-project 8. The Vulkan command processor does not call the native hook
  until then (always emulated).
- SDK layer: **port skate3's SDK layer near-verbatim** (from
  `mchughalex/rexglue-skate3`, branch `skate3-sdk-clean`), adapted to SDK
  `babc769`.
- Discovery: **XDK signature matching against Skate 3 TU3**, confirmed and
  gap-filled by **per-frame call-count correlation**.
- Toggle hotkey: **F6** (F5 is the Lua runner).

## Architecture

```
SDK fork (ForbesGRyan/rexglue-sdk, branch renderer)   Fable 2 repo
──────────────────────────────────────────────────    ─────────────────────────────
d3d12/command_processor                               src/native/
  IssueSwap ─► TryRenderNativeGuestOutput ──────────►   fable2_native_render.{h,cpp}
  IssueDraw / IssueCopy ◄── ShouldSuppress*              (callback, post-process overlay,
  occlusion queries ◄── fake "visible"                    F6 toggle, sticky fallback)
native_guest_renderer.{h,cpp}  [rexruntime.dll]       src/diagnostics/
native_rhi.h  [interface]                               fable2_d3d_census.h
d3d12/native_rhi_d3d12.cpp  [rexgpu-xenos.dll]        tools/xdk_sigmatch/
vulkan: no hook (emulated only)                       docs/native-renderer/
                                                        frame-map.md, xdk-map.json
```

## Sub-project 1: SDK hook layer

### SDK files (renderer branch, normal commits)

| File | Module | Notes |
|---|---|---|
| `include/rex/graphics/native_guest_renderer.h` | header | Callback API (`SetNativeGuestOutputRenderer`, `TryRenderNativeGuestOutput`, `Has/IsNativeGuestOutputActive`), suppression queries, wide-aspect API, post-processor API (`Set/Has/InvokeNativeGuestOutputPostProcessor`, `Request/IsRequested`). |
| `src/graphics/native_guest_renderer.cpp` | **`rexruntime.dll`** | Owns the registry, active flag and `native_render_*` cvars, so `fable_2.exe` and the GPU plugin share one instance. Every public function is added to `src/system/rexruntime.def`. |
| `include/rex/graphics/native_rhi.h` | header | Pure-virtual RHI (Device, Cmd, Buffer, Texture, TextureView, BindingLayout, Shader, Pipeline). Only `Set/GetShaderBytecodeCacheDirectory` need `.def` exports. |
| `src/graphics/d3d12/native_rhi_d3d12.cpp` | `rexgpu-xenos.dll` | D3D12 backend; runtime HLSL via D3DCompile (no offline toolchain). |

Rationale for the module split: ReXGlue at `babc769` builds graphics as a
separate plugin DLL and `rexruntime.dll` uses a curated `.def` export list
(unlike skate3's monolithic SDK).

### Command-processor edits (D3D12 only, ~100 lines)

1. `IssueSwap` -> `RefreshGuestOutput` lambda: if a renderer is registered,
   lazily create the RHI device, begin a frame on the guest-output resource,
   call `TryRenderNativeGuestOutput`; on `true` end submission and return; on
   `false` continue to the existing gamma/FXAA blit (fallback). Invoke the
   post-processor after the emulated blit when requested.
2. `IssueDraw` / `IssueCopy`: early-out when `ShouldSuppressEmulatedDraws()` and
   `ShouldSuppressPassAtPitch(pitch)`; never suppress memexport draws. Draws and
   resolves use the same predicate so suppressed EDRAM content is never
   resolved.
3. Occlusion queries: report a positive sample count while suppression is
   active.
4. Suppression census: one-time log of pitches that still execute.

### Suppression policy (generic, replaces skate3 modes 0–3)

- `native_render_suppress_emulated_draws` (bool, default **false**).
- `native_render_suppress_pitches` (list; default empty = suppress nothing) and
  `native_render_keep_pitches` (list). A pass is suppressed when suppression is
  on, the last frame was native, its pitch is in the suppress list (or the list
  is `*`), and not in the keep list.
- Values are filled in from the sub-project 2 frame map.

### Build integration

- Bump `SDK_PIN` in `tools/prepare_runtime_sdk.py` to the renderer-branch
  commit; verify `rexglue-sdk-runtime-fixes.patch` and
  `rexglue-sdk-debug-exports.patch` still apply (fold conflicting hunks into the
  branch as commits).
- Add a minimal native-RHI smoke check (create device, compile a shader, clear,
  draw a triangle into an offscreen texture) runnable without the game.

## Sub-project 1: game plumbing (`src/native/`)

### `fable2_native_render.{h,cpp}`

- Cvar `fable2_native_render` (bool, default **false**), seeded from
  `fable2_config.toml` `[native] enabled`. When false nothing is registered and
  behaviour is identical to today.
- Cvar `fable2_native_render_active` (hot), toggled by **F6** via a per-frame
  `GetAsyncKeyState` edge check in the existing MainRenderLoop override,
  focus-gated (same pattern as F5/Lua).
- Cvar `fable2_native_render_mode`: `overlay` (default) or `replace`.
- Registration in `Fable2App::OnPostSetup` (after the GPU plugin loads):
  `SetNativeGuestOutputRenderer` and `SetNativeGuestOutputPostProcessor`.

### Test modes

- `replace`: callback draws a fullscreen test pattern (checkerboard + moving
  bar) via the RHI and returns `true`. Proves device, runtime HLSL compile,
  pipeline, draw, and hand-off to the presenter.
- `overlay`: callback returns `false` (emulated frame renders), and the
  post-processor draws a translucent grid on top of the emulated image. This is
  the permanent composition path used by later sub-projects for parity overlays
  (e.g. native wireframe over the emulated frame).

### Error handling

- Any RHI failure (device, compile, pipeline) sets a sticky `failed` flag,
  logged once with the error; callback then returns `false` (emulated output).
- F6 clears the latch and retries.
- The callback never throws; future guest-memory reads use SEH-guarded copies.

### Threading

- Callback runs on the command-processor thread at swap. No shared game-thread
  data in sub-project 1; the structure anticipates a
  `shared_ptr<const FrameSnapshot>` published under a mutex (skate3 pattern) in
  sub-project 3.

### Build

- `src/native/*.cpp` glob in `CMakeLists.txt`.
- HLSL as a raw string in the `.cpp` for now; skate3's `EmbedShaders.cmake`
  arrives with sub-project 3.

## Sub-project 2: discovery

### A. XDK signature matcher (`tools/xdk_sigmatch/`)

1. **Image dump:** a small C++ CLI linked against the SDK runtime that loads a
   XEX via the SDK XEX loader, optionally applies a `.xexp` delta, and writes
   the decrypted/decompressed image plus section table. Inputs are passed by
   path; game files never enter the repo.
2. **Reference set:** Skate 3 TU3 D3D-level functions hooked by skate3recomp
   (SetIndices `82B79190`, SetStreamSource `82B78FF0`, DrawIndexedVertices
   `82B7AD68`, DrawVertices `82B7A970`, BeginVertices `82B79FC0`, Set PS/VS
   `82B7F408`/`82B7F150`, render state `82B83C48`, viewport/scissor
   `82B74310`/`82B769C0`, SetPending_AluConstants `82B83FE0`, Swap `82B82E08`,
   RegisterTexture `82C9A618`). Function boundaries come from running ReXGlue
   function analysis (`rexglue codegen` analysis phase) on the Skate 3 TU3 image,
   cross-checked against any entries in skate3recomp's manifest.
3. **Matching:** instruction words with build-variant fields masked (branch/call
   displacements, `lis`/`addi`/`ori` address pairs, r2/r13-relative
   displacements); scan Fable 2 text at 4-byte alignment. Unique hits are
   anchors; propagate along the call graph (callees at matching call-site
   positions) to name XDK helpers.
4. **Output:** `docs/native-renderer/xdk-map.json` + doc table: name, Skate 3
   address, Fable 2 address, confidence (`exact` | `masked` | `callgraph`),
   notes. Only addresses/names are stored, no game code bytes.

### B. In-game census (`src/diagnostics/fable2_d3d_census.h`)

- Enabled only by `FABLE2_D3D_CENSUS=<frames>`; otherwise compiled in but inert.
- Strong overrides on matched D3D functions (call through to `__imp__`).
  Unmatched functions: fall back to per-frame call-count correlation of the
  `0x82B9xxxx`–`0x82BAxxxx` range against SDK `IssueDraw` counts using the
  existing function tracer.
- Per draw: primitive type/counts, VB/IB guest addresses + strides, current
  VS/PS hash, render-state words, **caller address** (LR) to locate engine
  submission sites.
- Per frame: draws per render target (pitch from the SDK census), ordering of
  RT changes and resolves.
- Timing: guest frame time (`guest_frame_rate.h`), GPU-thread `IssueSwap`
  interval, time inside `IssueDraw` (small SDK perf-counter addition) to settle
  CPU- vs GPU-emulation-bound.
- Output: `logs/d3d_census_<timestamp>.jsonl`.

### C. Deliverable: `docs/native-renderer/frame-map.md`

- D3D function table (from A, confirmed by B).
- Ordered pass list for one gameplay frame and one menu frame: RT pitch/size,
  draw count, distinct shaders, likely purpose.
- Top engine call sites issuing draws.
- CPU vs GPU timing split.
- Recommendation for sub-project 3: first pass to replace and its
  suppress/keep pitch lists.

## Testing and success criteria

Sub-project 1:
- RHI smoke check passes without the game.
- With `fable2_native_render=false`: no registration; frame output and FPS
  unchanged versus the current build.
- `replace` mode: test pattern presented; F6 toggles back to emulated output
  live; forcing a shader compile error falls back to emulated with one log line.
- `overlay` mode: grid drawn over the live emulated frame in menus and gameplay.
- Suppression with an explicit pitch list removes exactly those passes; no
  stale-EDRAM artifacts after toggling.

Sub-project 2:
- Matcher maps all reference functions it reports as `exact`/`masked`; at least
  5 mappings verified by manual disassembly comparison.
- Per frame, hooked DrawIndexedVertices + DrawVertices calls equal SDK
  `IssueDraw` calls (excluding clears/memexport).
- Suppressing a single pitch removes the visual element the frame map says it
  should.
- `frame-map.md` complete with a concrete sub-project 3 recommendation.

## Non-goals

- Any native rendering of actual game content (sub-project 3+).
- Vulkan RHI backend (sub-project 8).
- Settings UI; config file + cvars + F6 only.
- Upstreaming to himdo/rexglue-sdk (may happen later, not required).

## Risks

- **XDK version drift:** Fable 2 (2008) and Skate 3 (2010) use different XDK
  versions; some D3D functions may differ. Mitigation: masked matching +
  call-graph propagation + call-count correlation fallback.
- **SDK fork divergence:** skate3's command-processor hunks target a different
  SDK base; manual merge into `babc769`. Mitigation: keep hunks small and
  isolated; smoke check.
- **Patch-file interplay:** the repo's SDK patches may conflict with new
  commits. Mitigation: fold conflicting hunks into the branch, bump `SDK_PIN`.
- **Official prebuilt SDK + antivirus:** the current build flow downloads the
  official SDK (flagged once by local AV). Out of scope here, but may block
  builds; tracked separately.
- **Fake occlusion results** could change game culling behaviour while
  suppression is on. Mitigation: only active with suppression, which is off by
  default in this spec.
