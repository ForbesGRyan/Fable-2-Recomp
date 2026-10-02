# Native renderer sub-project 4: albedo textures

Status: approved design (brainstorming, 2026-10-02). Implements sub-project 4 of
`docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md`, building on
sub-project 3 (`docs/superpowers/specs/2026-10-01-native-renderer-clay-pass-design.md`,
validation in `docs/native-renderer/frame-map.md` section 10).

## Goal

Give each clay draw its albedo (base colour) texture, mapped with its own UVs and drawn
unlit in the existing F6 debug views. This proves that the native renderer picks the right
texture per draw and decodes it correctly: formats, tiling, endian, mips and UVs. It does
not port materials, add lighting or replace frames.

## Context

- Sub-project 3 captures main-scene draws into `DrawRecord`s (vertex shader hash,
  transform, positions, indices), publishes a `FrameScene` at the guest swap and draws it
  as flat clay. Gameplay coverage is 0.949 of main-scene draws; terrain patches are
  rebuilt from their heightmap.
- No texture data is captured today. The only texture the capture reads is the terrain
  heightmap (`terrain_patch.h`, `DecodeHeightMap`, 2D `k_16` only).
- Texture fetch constants live in the device shadow at `device + 0x480 + 24*t`, shared
  with the vertex fetch constants (`xdk_layout.h`, `kDeviceTextureFetchStride`; checked
  against the GPU register file for t = 16..19). The ALU constant shadow is at
  `device + 0x780`, vertex bank `0x4000`, pixel bank `0x4400`.
- The pixel shader object is known but never read: the hook named `SetVertexShader?`
  (`0x82208CE8`) is really SetPixelShader and stores it at `device + 0x3194`, and
  `GpuLoadShaders` passes it in `r5`. Its microcode layout is undocumented; the vertex
  shader's is (`xdk_layout.h`, header `+0x368`, record table, ucode base dword 8).
- `vfetch_decode.h` lists all vertex fetches but skips texture fetches; only positions
  are decoded.
- SDK (`thirdparty/rexglue-sdk`, branch `renderer`): `xenos::xe_gpu_texture_fetch_t`,
  `TextureFormat`, and exported helpers `GetGuestTextureLayout`,
  `GetSubresourcesFromFetchConstant`, `GetPackedMipOffset`, `GetTiledOffset2D`,
  `GetTiledAddressUpperBound2D`, `FormatInfo::Get`, `SwizzleSigns`. `TextureInfo::Prepare`,
  `Untile` and the emulator texture cache are not exported.
- Native RHI: BC1-BC5, RGBA8, R8, RG8 and other formats; 2D/cube/3D textures with mips;
  texture views with a per-channel swizzle; texture tables of 1-8 SRVs; at most 4 static
  samplers (point/linear/aniso, wrap/clamp); `CopyBufferToTexture`. No mirror address
  mode, no border colour.

## Decisions

- **Scope: albedo debug view.** One texture per draw, unlit, opaque. Alpha, normal and
  specular maps, lighting and material math are out of scope.
- **Terrain stays clay**, reported as `terrain` in F3. Its colour is a blend of atlas
  layers (and its atlas shares the `k_DXT5A` hole mask), which is a material port.
- **Approach A: an offline table.** Discovery plus a tool decides, per pixel shader,
  which texture fetch is the albedo and where its UVs come from; entries are confirmed
  with decoded thumbnails and recorded with evidence, as `vs-transforms.json` was in
  sub-project 3. The runtime only looks the table up. Rejected: runtime shader analysis
  (silent wrong picks, load-time cost) and borrowing the emulator's texture cache (couples
  the native renderer to emulator internals and breaks once emulated passes are
  replaced).
- **CPU decode** with the SDK's exported tiling helpers. DXT formats are uploaded as BC
  blocks after the endian swap.
- **Coverage target 80%** of drawn non-terrain draws textured.

## Architecture

```
guest draw -> capture (guest thread)
   + PS hash        (PS microcode read at load, like the VS)
   + albedo lookup  (ps_albedo table: fetch slot, UV source)
   + fetch snapshot (6 dwords of the albedo texture fetch constant)
   + UV source      (vertex fetch slot, offset, format, stride, scale/offset)
   -> DrawRecord.material {status, fetch[6], uv_src, uv_xform}
FrameScene publish at swap (unchanged)
render thread:
   geometry cache   + float2 UV buffer per mesh (decoded on the CPU like positions)
   texture cache    key = fetch layout + content hash -> RHI texture + SRV
                    CPU untile with SDK helpers, BC1-3 uploaded as is
                    per-frame upload budget, LRU memory budget
   clay pass        textured PS: sample(albedo, uv); other draws keep flat clay
composite / F6 views unchanged
```

### Components and files

| Unit | Purpose |
|---|---|
| `src/native/capture/tfetch_decode.h` (pure) | Parse texture fetch instructions from shader microcode: fetch slot, dimension, source register and swizzle. |
| `src/native/capture/uv_trace.h` (pure) | Trace a texture fetch's coordinate register back through `mov` and `mul`/`mad` by constants to a PS input, and a VS export back to a vertex fetch element. |
| `src/native/capture/uv_decode.h` (pure) | Decode a UV vertex element (`32_32_FLOAT`, `16_16_FLOAT`, normalized/integer `16_16`, and what the D4 census adds) to `float2`. |
| `docs/native-renderer/ps-albedo.json`, `src/native/capture/ps_albedo_table.inc` | Per-PS albedo slot, input index and swizzle, PS-side UV transform constants, evidence. Generated by `tools/xdk_sigmatch/gen_albedo_table.py`. |
| `docs/native-renderer/vs-transforms.json` (extended) | Per-VS `"uv": {input index: {fetch, swizzle, transform}}`, emitted into `vs_transform_table.inc` with a new macro. |
| `tools/xdk_sigmatch/albedo_finder.py` | Read discovery rows, list candidate fetches per PS, score them, decode thumbnails to PNG, propose table entries. |
| `src/native/render/texture_decode.h` (pure) | Fetch constant to layout (size, pitch, tiling, endian, mips, format), untile and endian swap into upload rows, format mapping to RHI formats and view swizzle. |
| `src/native/render/texture_cache_index.h` (pure), `texture_cache.{h,cpp}` | Residency, change detection, upload budget, LRU eviction gated on GPU completion. |
| `src/native/render/clay_pass.cpp`, `fable2_native_shaders.h` | UV buffer, textured shader variant, samplers, per-draw texture binding. |

New cvars: `fable2_native_textures` (default true; false gives sub-project 3's clay pass
exactly), `fable2_native_texture_upload_mb` (default 8, guest bytes decoded per frame),
`fable2_native_texture_budget_mb` (default 512).

## Discovery tasks

- **D1. PS microcode access.** Find the microcode inside the PS object: disassemble the
  IM_LOAD type-1 emitter at `0x8221B354` and compare with the GPU's loaded pixel shader
  (SDK shader dump). Deliver `ReadPsUcode` and a PS hash, with the hash checked against
  the emulator's hash of the same shader. Record the layout in frame-map section 8.
- **D2. Texture fetch decode and UV tracing.** Implement `tfetch_decode.h` and
  `uv_trace.h`. Supported coordinate sources: a PS input directly, or a PS input through
  `mov`/`mul`/`mad` with constants; on the VS side, an export written from a vertex fetch
  element directly or through `mul`/`mad` with constants. Anything else (dependent reads,
  computed coordinates) is `uv-unsupported`.
- **D3. Discovery capture and albedo choice.** Discovery rows gain the PS hash, the six
  fetch-constant dwords for each texture fetch the PS uses, and the constants the UV
  traces name. `albedo_finder.py` scores candidates (2D fetch, coordinates from a vertex
  element, result reaching the colour output), decodes each candidate texture to a PNG
  thumbnail, and proposes an entry. A human or agent confirms the albedo from the
  thumbnails (normal maps are bluish, masks greyscale). Pixel shaders are handled in
  order of draw count until 80% of non-terrain draws are covered. Entries carry
  `"manual": true` and evidence.
- **D4. Format census.** List the texture formats and UV element formats used by the
  table's albedo fetches. Only formats that occur get a decoder; expected DXT1, DXT2_3,
  DXT4_5 and 8_8_8_8, possibly CTX1 or DXN.

## Capture rules (guest thread)

- When a draw record is built (only while a clay view is on, as in sub-project 3), look up
  the current PS hash in `ps_albedo_table`.
  - Hit: copy the six fetch-constant dwords from `device + 0x480 + 24*slot`, the UV
    transform constants the table names (VS and PS banks of the ALU constant shadow), and
    the UV source from the VS table's `uv` entry.
  - Miss: status `ps-unknown`.
  - No VS `uv` entry for the PS input: status `uv-unsupported`.
- Terrain records get status `terrain` without a lookup.
- `DrawRecord` gains `material { status, fetch[6], uv_src, uv_xform[2] (float4) }`.
- A material failure never changes whether a draw is drawable; the draw falls back to
  flat clay.

## Texture decode

- Read from the fetch constant: base address, format, width, height, pitch, tiled flag,
  endian, mip address, mip count, swizzle, clamp modes, filter.
- Layout from `GetGuestTextureLayout`; tiled addressing from `GetTiledOffset2D`.
- 2D only. Cube and 3D textures: `format-unsupported`.
- DXT1, DXT2_3 and DXT4_5 upload as BC1, BC2 and BC3 after the endian swap. 8_8_8_8
  uploads as RGBA8 with the fetch constant's swizzle applied through the texture view.
  Other formats from the D4 census get a converter or stay `format-unsupported`.
- Mips: the guest mip chain including the packed mip tail. If the mip layout fails,
  upload mip 0 only and count it.
- Gamma and signed formats are treated as plain unsigned colour (debug view; listed as a
  limitation).

## Texture cache (render thread)

- Key: the layout fields plus an XXH3 hash of the guest bytes, taken when the texture is
  first seen.
- Change detection: each frame, a sparse sample of about 4 KB spread across the texture
  is re-hashed; a change triggers a re-decode. A texture that changes for more than 8
  consecutive frames is marked `texture-dynamic` and its draws stay clay.
- Upload budget: at most `fable2_native_texture_upload_mb` of guest bytes decoded per
  frame. Draws whose texture is over budget stay clay this frame as `texture-pending`.
- Memory: LRU under `fable2_native_texture_budget_mb`; evicted textures are destroyed only
  after the GPU has completed the submissions that used them.
- Guest reads go through the heap page tables, bounds-checked; a layout that does not
  fit its heap range is `texture-bad`.

## Renderer

- Geometry cache: for each textured mesh, decode the UV element into a `float2` buffer
  next to the positions. The UV source is part of the cache key. Skinned and rigid-skin
  meshes take UVs from the same vertices. UVs are pulled through the same uint32 index
  list as positions.
- Clay VS: adds `StructuredBuffer<float2> uvs` (t2) and `uv_scale_offset` in b0, outputs
  the UV.
- Clay PS (textured variant): `Texture2D albedo` from a texture table plus a sampler;
  output `albedo.rgb * shade`, where `shade` is sub-project 3's facet shading remapped to
  0.75-1.0. Alpha ignored; all draws opaque.
- Samplers: four static samplers (linear wrap, linear clamp, point wrap, point clamp),
  chosen from the fetch constant's filter and clamp fields. Mirror modes use wrap
  (counted as an approximation); no border colour.
- Draws without a usable texture keep the flat clay colour in the same pass.
- Draw order, depth state, composite views and F6 are unchanged.

## Stats (F3 and log)

- F3: `Textures: textured T of D drawn (S%), resident N (M MB), uploads U (X MB),
  decode Y ms | top untextured: ...`.
- `[native] capture:` log gains `untextured by reason {...}` and `untextured by ps
  {hash: n}`.

## Failure handling

- Per-draw failures degrade that draw to flat clay with a reason: `terrain`,
  `ps-unknown`, `no-albedo`, `uv-unsupported`, `format-unsupported`, `texture-pending`,
  `texture-dynamic`, `texture-bad`.
- An RHI failure (texture create, view create, upload) latches the texture path off for
  the session: the pass draws plain clay and the failure is logged once.

## Testing

- Standalone clang tests (TDD, RED first) for every pure unit: texture fetch parsing, UV
  tracing, fetch-constant decoding (checked against the SDK's
  `xe_gpu_texture_fetch_t` field layout), untile and endian swap (checked against known
  tiled offsets), UV decoding per format, texture cache index and LRU.
- Python tests for `gen_albedo_table.py`, the extended `gen_transform_table.py` and the
  albedo candidate scoring.
- Gameplay runs only through `tools\drive_game.ps1` (in-process autoplay); discovery
  delay at least 50 s. A/B runs with `fable2_native_textures` on and off.

## Success criteria

1. In split and overlay views, clay textures match the emulated image (same image,
   orientation and scale, no tiling garbage) in town, an open field and an interior
   (user check).
2. At least 80% of drawn non-terrain draws are textured in Bowerstone and at Bower Lake;
   the rest have reasons shown in F3.
3. 30 fps holds with a clay view on; with `fable2_native_view=off`, capture overhead stays
   under 0.5 ms of guest time per frame.
4. Texture memory stays within its budget; a 10-minute run with an area transition has no
   crash or latch.

## Non-goals

Alpha test and blending, normal/specular/other maps, lighting, terrain colour, decals,
render-target textures (`texture-dynamic`), gamma-correct colour, cube and 3D textures,
animated characters (sub-project 5), frame replacement, performance gains.

## Risks

- **Albedo behind material math** (blended layers, computed UVs): those pixel shaders
  stay clay. If they exceed 20% of non-terrain draws the coverage target is missed; the
  fallback is porting the top shaders individually, decided after D3.
- **PS object layout differs from the VS layout:** D1 settles it before anything else
  depends on it.
- **Sparse change detection misses an edit** to a texture region outside the samples:
  shows as a stale texture; acceptable for a debug view, and the full hash on re-decode
  keeps the cache consistent once a change is seen.
- **Upload hitch on area loads:** bounded by the per-frame upload budget; draws show as
  clay until their textures arrive.
