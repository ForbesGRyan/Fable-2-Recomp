# Native Renderer Albedo Textures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each clay draw its albedo texture, mapped with its own UVs and drawn unlit in the existing F6 debug views, with at least 80% of drawn non-terrain draws textured.

**Architecture:** An offline table (`docs/native-renderer/ps-albedo.json` plus a `"uv"` block per vertex shader in `vs-transforms.json`) says, per pixel shader, which texture fetch is the albedo and how its UVs derive from a vertex element (up to two affine stages on each side, from shader constants). The capture copies the albedo fetch constant, resolves the UV element's stream and folds the constants into a scale/offset per draw. On the render thread a texture cache untiles guest textures on the CPU (SDK layout helpers plus a pure untiler), uploads BC1-3/RGBA8 with mips, and the clay pass samples them through a UV buffer decoded next to the positions.

**Tech Stack:** C++23 (clang-cl via CMake/Ninja), ReXGlue SDK (`thirdparty/rexglue-sdk`, branch `renderer`, exported `texture_util` helpers), nrhi D3D12 backend, HLSL (runtime-compiled), Python 3 (unittest, standard library only: `zlib`/`struct` for PNG).

**Spec:** `docs/superpowers/specs/2026-10-02-native-renderer-textures-design.md`

## Global Constraints

- Environment: put `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin` FIRST on PATH, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin` (`tests\run_native_tests.cmd` calls bare `clang++`).
- SDK build (only if an SDK file changes): `tools\build_runtime_sdk.cmd C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` (never without the argument). Game build: `& .\build.cmd -release fable_2` from PowerShell. No `fable_2.exe` may be running while building.
- Python tests: `python -m unittest discover -s tests -p "<file>.py" -v`. Native tests: `& .\tests\run_native_tests.cmd` (add new tests before its final `exit /b 0`; the "broken_ps failed ... X3004" output is an expected negative test).
- `thirdparty/rexglue-sdk/src/system/rexruntime.def`: never add `;` comment lines; new exports need MSVC-decorated names.
- Preserve each edited file's line endings (several SDK and game files are CRLF).
- Git: branch `native-renderer-textures`; stage files explicitly; never stage `fable_2_manifest.toml`; never `git add -A`; never push, stash, reset or switch branches. Every commit message ends with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Gameplay runs ONLY via `tools\drive_game.ps1` (in-process autoplay, the user's key sequence; the game window may stay in the background; world fully up at ~50 s). Never `-KeyboardInput`, never any other input method. Discovery delays in gameplay: at least 50 s.
- Unit-tested headers marked "pure" must not include SDK, Windows or GPU headers.
- New cvars (category "Fable2"): `fable2_native_textures` (bool, default true), `fable2_native_texture_upload_mb` (int32, default 8), `fable2_native_texture_budget_mb` (int32, default 512).
- Material reasons (exact strings): `textured`, `terrain`, `ps-unknown`, `no-albedo`, `uv-unsupported`, `format-unsupported`, `texture-pending`, `texture-dynamic`, `texture-bad`.
- A material failure never changes whether a draw is drawn: the draw falls back to flat clay.
- The emulated frame is never modified; with `fable2_native_view=off` nothing is drawn and records are not built (sub-project 3 behaviour, unchanged).
- Spec deviation (ruling): the spec lists `tfetch_decode.h` and `uv_trace.h` as C++ headers. Only the offline tool needs shader tracing, so tracing lives in `tools/xdk_sigmatch/shader_trace.py` and works on the SDK's shader disassembly dumps (`--dump_shaders`); the runtime needs only the texture fetch slots a pixel shader uses (`TextureFetchSlots` in `vfetch_decode.h`, Task 2).

## Review Focus

1. A fetch constant with garbage fields (zero size, pitch below width, base 0, unknown format, cube/3D): the draw stays clay with `format-unsupported` or `texture-bad`, never a crash or an out-of-range read. Pinned in Task 1 (`DecodeTextureFetch` rejects) and Task 11 (bounded reads).
2. A texture whose guest bytes change every frame (render target resolved into it): after 8 consecutive changed frames it is `texture-dynamic` and stops re-uploading. Pinned in Task 4.
3. A single texture larger than the per-frame upload budget: it still uploads (one oversize upload per frame is allowed), otherwise it would stay pending forever. Pinned in Task 4.
4. Texture memory over budget with every resident texture used this frame: nothing in use is evicted; the cache goes over budget and reports it (the geometry cache rule). Pinned in Task 4 (reuses `GeometryCacheIndex`, plus a test).
5. A table UV entry whose decoded vertex fetch does not match the format/offset recorded in the table (fetch order differs between the disassembly and `DecodeVertexFetches`): the draw is `uv-unsupported`, not drawn with garbage UVs. Pinned in Task 10 (`ResolveUvFetch` test in Task 3).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/native/capture/xenos_tiling.h` (pure) | `TiledOffset2D` shared by terrain and textures | 1 |
| `src/native/render/texture_decode.h` (pure) | Fetch constant decode, format info, level layout, untile + endian swap, sampler choice, identity hash | 1 |
| `src/native/capture/uv_decode.h` (pure) | UV vertex element layout and decode to float2 | 2 |
| `src/native/capture/vfetch_decode.h` | + `TextureFetchSlots` | 2 |
| `src/native/capture/material.h` (pure) | `MaterialStatus`, `Material`, `UvStage`, table spec structs, UV composition, fetch matching | 3 |
| `src/native/capture/draw_record.h` | + `Material` in `DrawRecord` / `DrawInputs` | 3 |
| `src/native/render/texture_residency.h` (pure) | Sparse sample hash, change streaks, upload budget | 4 |
| `tools/xdk_sigmatch/gen_albedo_table.py`, `tools/xdk_sigmatch/gen_transform_table.py` | `ps_albedo_table.inc`, `FABLE2_VS_UV` | 5 |
| `docs/native-renderer/ps-albedo.json`, `src/native/capture/ps_albedo_table.inc` (generated) | Albedo table | 5, 9 |
| `tools/xdk_sigmatch/shader_trace.py` | Disassembly parser, texture fetch list, UV tracing | 6 |
| `src/native/capture/xdk_layout.h`, `src/native/capture/capture.cpp` | PS microcode, PS hash, PS constants (D1); discovery rows and texture dumps (D3); material capture | 7, 8, 10 |
| `tools/xdk_sigmatch/texture_thumb.py`, `tools/xdk_sigmatch/albedo_finder.py` | Python untile/BC decode to PNG; candidate scoring and proposals | 8 |
| `src/native/render/texture_cache.{h,cpp}` | GPU textures, uploads, views, LRU, latch | 11 |
| `src/native/render/geometry_cache.{h,cpp}`, `clay_logic.h`, `clay_pass.{h,cpp}`, `src/native/fable2_native_shaders.h`, `src/native/fable2_native_render.cpp` | UV buffers, textured shaders, samplers, F3 line, cvars | 12 |
| `docs/native-renderer/frame-map.md` | Evidence (sections 8, 11) | 7, 9, 13 |

---

### Task 1: Texture fetch decode and untiling (pure)

**Files:**
- Create: `src/native/capture/xenos_tiling.h`, `src/native/render/texture_decode.h`, `tests/native/test_texture_decode.cpp`
- Modify: `src/native/capture/terrain_patch.h` (use the shared `TiledOffset2D`), `tests/run_native_tests.cmd`

**Interfaces:**
- Produces: `capture::TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2)`; in `fable2::native::render`: `enum class TexFormat`, `struct TexFormatInfo {block, bytes_per_block, bpb_log2}`, `TexFormatFromXenos`, `FormatInfo`, `enum class FetchError`, `struct TextureFetch`, `FetchError DecodeTextureFetch(const uint32_t fc[6], TextureFetch*)`, `struct LevelLayout`, `LevelLayout BaseLevelLinear(const TextureFetch&)`, `uint32_t LevelCount(const TextureFetch&)`, `uint32_t LevelExtent(uint32_t base, uint32_t level)`, `uint32_t UploadRowPitch(uint32_t width_blocks, uint32_t bytes_per_block)`, `void CopySwapped(uint8_t*, const uint8_t*, uint32_t, uint32_t endian)`, `bool UntileLevel(...)`, `uint32_t SamplerIndex(const TextureFetch&)`, `bool IsMirror(uint8_t clamp)`, `uint64_t TextureIdentity(const uint32_t fc[6])`.

- [ ] **Step 1: Move `TiledOffset2D` to a shared header**

Create `src/native/capture/xenos_tiling.h`:

```cpp
#pragma once

// Xenos tiled-texture addressing (pure: no SDK/GPU deps).

#include <cstdint>

namespace fable2::native::capture {

// xenia's texture_util::GetTiledOffset2D (UModel's Xbox 360 untiling). x, y and
// pitch are in blocks (texels for uncompressed formats, 4x4 blocks for DXT);
// returns the byte offset of block (x, y) from the level's base.
inline int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

}  // namespace fable2::native::capture
```

In `src/native/capture/terrain_patch.h`, delete the local `TiledOffset2D` (the comment line and the function) and add `#include "xenos_tiling.h"` after `#include "position_decode.h"`. Run `& .\tests\run_native_tests.cmd`; expected: all pass (terrain test unchanged).

- [ ] **Step 2: Write the failing test**

Create `tests/native/test_texture_decode.cpp`:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/texture_decode.h"
#include <cstring>
#include <vector>

using namespace fable2::native;
using namespace fable2::native::render;

struct FcArgs {
  uint32_t format = 18, w = 256, h = 128, pitch = 256, endian = 1, base = 0x1A000000, mip = 0x1A010000;
  uint32_t mip_max = 8, swizzle = 0x688, filter = 1, clamp_x = 0, clamp_y = 2, dim = 1, type = 2;
  bool tiled = true, packed = true;
};
static void MakeFc(const FcArgs& a, uint32_t fc[6]) {
  fc[0] = a.type | (a.clamp_x << 10) | (a.clamp_y << 13) | ((a.pitch >> 5) << 22) | (uint32_t(a.tiled) << 31);
  fc[1] = a.format | (a.endian << 6) | a.base;
  fc[2] = (a.w - 1) | ((a.h - 1) << 13);
  fc[3] = (a.swizzle << 1) | (a.filter << 19);
  fc[4] = (a.mip_max << 6);
  fc[5] = (a.dim << 9) | (uint32_t(a.packed) << 11) | a.mip;
}

int main() {
  uint32_t fc[6];
  TextureFetch t;
  // --- decode ---
  MakeFc({}, fc);
  if (DecodeTextureFetch(fc, &t) != FetchError::kNone) return 1;
  if (t.format != TexFormat::kDxt1 || t.width != 256 || t.height != 128 || t.pitch_texels != 256) return 2;
  if (t.base_phys != 0x1A000000 || t.mip_phys != 0x1A010000 || !t.tiled || t.endian != 1) return 3;
  if (t.mip_max != 8 || !t.packed_mips || t.clamp_x != 0 || t.clamp_y != 2 || !t.linear_filter) return 4;
  if (t.swizzle[0] != 0 || t.swizzle[1] != 1 || t.swizzle[2] != 2 || t.swizzle[3] != 3) return 5;
  FcArgs a;
  a.type = 0; MakeFc(a, fc);
  if (DecodeTextureFetch(fc, &t) != FetchError::kNotTexture) return 6;
  a = {}; a.dim = 3; MakeFc(a, fc);  // cube
  if (DecodeTextureFetch(fc, &t) != FetchError::kNot2D) return 7;
  a = {}; a.format = 7; MakeFc(a, fc);  // k_2_10_10_10: no decoder
  if (DecodeTextureFetch(fc, &t) != FetchError::kFormat) return 8;
  a = {}; a.pitch = 128; MakeFc(a, fc);  // pitch below width
  if (DecodeTextureFetch(fc, &t) != FetchError::kSize) return 9;
  a = {}; a.base = 0; MakeFc(a, fc);  // no base level
  if (DecodeTextureFetch(fc, &t) != FetchError::kSize) return 10;
  // The terrain heightmap (k_16) has no albedo decoder.
  const uint32_t tf16[6] = {0x84C04802, 0x1BD0C058, 0x00480240, 0x01001400, 0x00000000, 0x00000218};
  if (DecodeTextureFetch(tf16, &t) != FetchError::kFormat) return 11;

  // --- format info, levels, pitches ---
  if (FormatInfo(TexFormat::kDxt1).bytes_per_block != 8 || FormatInfo(TexFormat::kDxt45).bpb_log2 != 4) return 12;
  if (FormatInfo(TexFormat::k8888).block != 1 || FormatInfo(TexFormat::k8888).bytes_per_block != 4) return 13;
  a = {}; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (LevelCount(t) != 9) return 14;  // min(8, log2(256)) + 1
  a.mip = 0; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (LevelCount(t) != 1) return 15;  // no mip address: base only
  if (LevelExtent(256, 3) != 32 || LevelExtent(5, 4) != 1) return 16;
  if (UploadRowPitch(3, 8) != 256 || UploadRowPitch(64, 8) != 512) return 17;
  a = {}; a.tiled = false; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  const LevelLayout base = BaseLevelLinear(t);
  if (base.pitch_blocks != 64 || base.width_blocks != 64 || base.height_blocks != 32 || base.row_pitch_bytes != 512) return 18;

  // --- endian swaps ---
  const uint8_t s[4] = {1, 2, 3, 4};
  uint8_t d[4];
  CopySwapped(d, s, 4, 0); if (std::memcmp(d, "\x01\x02\x03\x04", 4)) return 19;
  CopySwapped(d, s, 4, 1); if (std::memcmp(d, "\x02\x01\x04\x03", 4)) return 20;
  CopySwapped(d, s, 4, 2); if (std::memcmp(d, "\x04\x03\x02\x01", 4)) return 21;
  CopySwapped(d, s, 4, 3); if (std::memcmp(d, "\x03\x04\x01\x02", 4)) return 22;

  // --- linear 8888 untile: 4x2 texels, guest rows padded to 256 bytes, 8in32 ---
  {
    std::vector<uint8_t> src(256 + 16, 0);
    for (uint32_t y = 0; y < 2; ++y)
      for (uint32_t x = 0; x < 4; ++x) {
        uint8_t* p = src.data() + y * 256 + x * 4;
        p[0] = 0xAA; p[1] = uint8_t(y); p[2] = uint8_t(x); p[3] = 0x55;  // big-endian dword
      }
    LevelLayout l;
    l.pitch_blocks = 32; l.width_blocks = 4; l.height_blocks = 2; l.row_pitch_bytes = 256;
    std::vector<uint8_t> dst(2 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::k8888), false, 2, dst.data(), 256)) return 23;
    const uint8_t* q = dst.data() + 1 * 256 + 3 * 4;  // texel (3, 1), swapped to little-endian
    if (q[0] != 0x55 || q[1] != 3 || q[2] != 1 || q[3] != 0xAA) return 24;
    if (UntileLevel(src.data(), 256 + 8, l, FormatInfo(TexFormat::k8888), false, 2, dst.data(), 256)) return 25;
  }
  // --- tiled 8888: 64x64 texels, pitch 64; each texel's bytes name its (x, y) ---
  {
    uint32_t extent = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) extent = std::max(extent, uint32_t(capture::TiledOffset2D(x, y, 64, 2)) + 4);
    std::vector<uint8_t> src(extent, 0);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        uint8_t* p = src.data() + capture::TiledOffset2D(x, y, 64, 2);
        p[0] = uint8_t(x); p[1] = uint8_t(y); p[2] = 7; p[3] = 9;
      }
    LevelLayout l;
    l.pitch_blocks = 64; l.width_blocks = 64; l.height_blocks = 64;
    std::vector<uint8_t> dst(64 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::k8888), true, 0, dst.data(), 256)) return 26;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const uint8_t* q = dst.data() + y * 256 + x * 4;
        if (q[0] != x || q[1] != y) return 27;
      }
  }
  // --- tiled DXT1 packed-mip origin: a 4x4-block level read at block offset (16, 8) ---
  {
    uint32_t extent = 0;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) extent = std::max(extent, uint32_t(capture::TiledOffset2D(x, y, 32, 3)) + 8);
    std::vector<uint8_t> src(extent, 0);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        uint8_t* p = src.data() + capture::TiledOffset2D(x, y, 32, 3);
        p[0] = uint8_t(x); p[1] = uint8_t(y);  // 8in16 swaps these into d[1], d[0]
      }
    LevelLayout l;
    l.pitch_blocks = 32; l.width_blocks = 4; l.height_blocks = 4; l.x_blocks = 16; l.y_blocks = 8;
    std::vector<uint8_t> dst(4 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::kDxt1), true, 1, dst.data(), 256)) return 28;
    const uint8_t* q = dst.data() + 2 * 256 + 3 * 8;  // block (3, 2) of the level = guest (19, 10)
    if (q[1] != 19 || q[0] != 10) return 29;
  }

  // --- sampler choice and identity ---
  a = {}; a.filter = 0; a.clamp_x = 0; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (SamplerIndex(t) != 0) return 30;  // point, wrap
  a.filter = 1; a.clamp_x = 2; a.clamp_y = 2; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (SamplerIndex(t) != 3) return 31;  // linear, clamp
  if (!IsMirror(1) || IsMirror(2) || IsMirror(0)) return 32;
  // Identity ignores sampler-only fields (clamps, filters) but not the address or format.
  uint32_t f1[6], f2[6];
  a = {}; MakeFc(a, f1);
  a.clamp_x = 2; a.filter = 0; MakeFc(a, f2);
  if (TextureIdentity(f1) != TextureIdentity(f2)) return 33;
  a = {}; a.base = 0x1B000000; MakeFc(a, f2);
  if (TextureIdentity(f1) == TextureIdentity(f2)) return 34;
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:

```bat
clang++ -std=c++23 "%~dp0native\test_texture_decode.cpp" -o "%OUT%\texture_decode.exe" || exit /b 1
"%OUT%\texture_decode.exe" || exit /b 1
```

- [ ] **Step 3: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile error, `texture_decode.h` not found.

- [ ] **Step 4: Write the implementation**

Create `src/native/render/texture_decode.h`:

```cpp
#pragma once

// Xenos 2D texture fetch constants -> decode parameters, and level untiling
// into host upload rows (pure: no SDK/GPU deps). Field layout:
// xenos::xe_gpu_texture_fetch_t, host-order dwords (the capture byte-swaps the
// device shadow). Level offsets for mips and packed tails come from the SDK
// (texture_cache.cpp); this header handles one level at a time.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../capture/xenos_tiling.h"

namespace fable2::native::render {

enum class TexFormat : uint8_t { kUnsupported, kDxt1, kDxt23, kDxt45, k8888 };

struct TexFormatInfo {
  uint32_t block = 1;            // texels per block edge
  uint32_t bytes_per_block = 0;
  uint32_t bpb_log2 = 0;
};

inline TexFormat TexFormatFromXenos(uint32_t xenos_format) {
  switch (xenos_format) {
    case 18: return TexFormat::kDxt1;   // k_DXT1
    case 19: return TexFormat::kDxt23;  // k_DXT2_3
    case 20: return TexFormat::kDxt45;  // k_DXT4_5
    case 6: return TexFormat::k8888;    // k_8_8_8_8
    default: return TexFormat::kUnsupported;
  }
}

inline TexFormatInfo FormatInfo(TexFormat f) {
  switch (f) {
    case TexFormat::kDxt1: return {4, 8, 3};
    case TexFormat::kDxt23:
    case TexFormat::kDxt45: return {4, 16, 4};
    case TexFormat::k8888: return {1, 4, 2};
    default: return {};
  }
}

enum class FetchError : uint8_t { kNone, kNotTexture, kNot2D, kFormat, kSize };

struct TextureFetch {
  uint32_t base_phys = 0, mip_phys = 0;  // GPU physical, 4 KB aligned
  uint32_t width = 0, height = 0;
  uint32_t pitch_texels = 0;             // fetch pitch field << 5
  uint32_t xenos_format = 0;
  TexFormat format = TexFormat::kUnsupported;
  uint32_t endian = 0;                   // xenos::Endian
  bool tiled = false;
  bool packed_mips = false;
  uint32_t mip_min = 0, mip_max = 0;
  uint8_t swizzle[4] = {0, 1, 2, 3};     // 0-3 channel, 4 = 0.0, 5 = 1.0
  uint8_t clamp_x = 0, clamp_y = 0;      // xenos::ClampMode
  bool linear_filter = false;            // mag filter linear
};

inline FetchError DecodeTextureFetch(const uint32_t fc[6], TextureFetch* out) {
  if ((fc[0] & 3) != 2) return FetchError::kNotTexture;
  if (((fc[5] >> 9) & 3) != 1) return FetchError::kNot2D;  // xenos::DataDimension::k2DOrStacked
  TextureFetch t;
  t.xenos_format = fc[1] & 0x3F;
  t.format = TexFormatFromXenos(t.xenos_format);
  if (t.format == TexFormat::kUnsupported) return FetchError::kFormat;
  t.endian = (fc[1] >> 6) & 3;
  t.base_phys = fc[1] & 0xFFFFF000u;
  t.mip_phys = fc[5] & 0xFFFFF000u;
  t.width = (fc[2] & 0x1FFF) + 1;
  t.height = ((fc[2] >> 13) & 0x1FFF) + 1;
  t.pitch_texels = ((fc[0] >> 22) & 0x1FF) << 5;
  t.tiled = (fc[0] >> 31) != 0;
  t.packed_mips = ((fc[5] >> 11) & 1) != 0;
  t.mip_min = (fc[4] >> 2) & 0xF;
  t.mip_max = (fc[4] >> 6) & 0xF;
  for (int i = 0; i < 4; ++i) {
    const uint8_t s = uint8_t((fc[3] >> (1 + 3 * i)) & 7);
    t.swizzle[i] = s <= 5 ? s : 4;
  }
  t.clamp_x = uint8_t((fc[0] >> 10) & 7);
  t.clamp_y = uint8_t((fc[0] >> 13) & 7);
  t.linear_filter = ((fc[3] >> 19) & 3) == 1;
  if (t.base_phys == 0 || t.pitch_texels < t.width) return FetchError::kSize;
  *out = t;
  return FetchError::kNone;
}

// One level's storage. Offsets and pitches are in blocks; x/y_blocks is the
// level's origin inside a packed mip tail (0 otherwise).
struct LevelLayout {
  uint32_t offset = 0;           // bytes from the level's base address
  uint32_t pitch_blocks = 0;     // tiled addressing pitch
  uint32_t row_pitch_bytes = 0;  // linear addressing
  uint32_t width_blocks = 0, height_blocks = 0;
  uint32_t x_blocks = 0, y_blocks = 0;
};

inline uint32_t LevelExtent(uint32_t base, uint32_t level) { return std::max(1u, base >> level); }

// Levels to upload: the base plus mips up to mip_max when a mip address exists.
inline uint32_t LevelCount(const TextureFetch& t) {
  if (!t.mip_phys) return 1;
  uint32_t log2 = 0;
  for (uint32_t m = std::max(t.width, t.height); m > 1; m >>= 1) ++log2;
  return std::min(t.mip_max, log2) + 1;
}

inline uint32_t UploadRowPitch(uint32_t width_blocks, uint32_t bytes_per_block) {
  return (width_blocks * bytes_per_block + 255) & ~255u;
}

// The base level of a linear texture (rows padded to 256 bytes).
inline LevelLayout BaseLevelLinear(const TextureFetch& t) {
  const TexFormatInfo fi = FormatInfo(t.format);
  LevelLayout l;
  l.pitch_blocks = t.pitch_texels / fi.block;
  l.row_pitch_bytes = (l.pitch_blocks * fi.bytes_per_block + 255) & ~255u;
  l.width_blocks = (t.width + fi.block - 1) / fi.block;
  l.height_blocks = (t.height + fi.block - 1) / fi.block;
  return l;
}

// Copies n bytes applying the GPU's endian swap (xenos::Endian): the result is
// what the GPU reads, i.e. little-endian DXT words / RGBA8 bytes.
inline void CopySwapped(uint8_t* d, const uint8_t* s, uint32_t n, uint32_t endian) {
  switch (endian) {
    case 1:  // 8in16
      for (uint32_t i = 0; i + 1 < n; i += 2) { d[i] = s[i + 1]; d[i + 1] = s[i]; }
      break;
    case 2:  // 8in32
      for (uint32_t i = 0; i + 3 < n; i += 4) {
        d[i] = s[i + 3]; d[i + 1] = s[i + 2]; d[i + 2] = s[i + 1]; d[i + 3] = s[i];
      }
      break;
    case 3:  // 16in32
      for (uint32_t i = 0; i + 3 < n; i += 4) {
        d[i] = s[i + 2]; d[i + 1] = s[i + 3]; d[i + 2] = s[i]; d[i + 3] = s[i + 1];
      }
      break;
    default:
      std::memcpy(d, s, n);
  }
}

// Writes the level's blocks as rows of dst_row_pitch bytes (one row per block
// row). False if any block lies outside src_size (nothing is read past it).
inline bool UntileLevel(const uint8_t* src, size_t src_size, const LevelLayout& l, const TexFormatInfo& fi,
                        bool tiled, uint32_t endian, uint8_t* dst, uint32_t dst_row_pitch) {
  if (!src || !dst || fi.bytes_per_block == 0) return false;
  for (uint32_t by = 0; by < l.height_blocks; ++by) {
    for (uint32_t bx = 0; bx < l.width_blocks; ++bx) {
      const uint32_t x = bx + l.x_blocks, y = by + l.y_blocks;
      uint64_t o = tiled ? uint64_t(uint32_t(capture::TiledOffset2D(int32_t(x), int32_t(y), l.pitch_blocks,
                                                                    fi.bpb_log2)))
                         : uint64_t(y) * l.row_pitch_bytes + uint64_t(x) * fi.bytes_per_block;
      o += l.offset;
      if (o + fi.bytes_per_block > src_size) return false;
      CopySwapped(dst + size_t(by) * dst_row_pitch + size_t(bx) * fi.bytes_per_block, src + o,
                  fi.bytes_per_block, endian);
    }
  }
  return true;
}

inline bool IsMirror(uint8_t clamp) { return clamp == 1 || clamp == 3 || clamp == 5 || clamp == 7; }

// Static sampler s<i>: bit 0 linear (else point), bit 1 clamp (else wrap).
// Mirror modes use wrap (counted by the caller).
inline uint32_t SamplerIndex(const TextureFetch& t) {
  const bool wrap = t.clamp_x <= 1;
  return (t.linear_filter ? 1u : 0u) | (wrap ? 0u : 2u);
}

// Hash of the fields that define the texture's contents and layout; clamp,
// filter and LOD fields (sampler state) are masked out.
inline uint64_t TextureIdentity(const uint32_t fc[6]) {
  const uint32_t masked[6] = {fc[0] & ~(0x1FFu << 10), fc[1], fc[2], fc[3] & 0x7FFFFu, fc[4] & (0xFFu << 2),
                              fc[5] & ~0x1FFu};
  uint64_t h = 1469598103934665603ull;
  for (uint32_t v : masked) h = (h ^ v) * 1099511628211ull;
  return h;
}

}  // namespace fable2::native::render
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `& .\tests\run_native_tests.cmd`
Expected: exit 0 (texture_decode passes; terrain_patch still passes).

- [ ] **Step 6: Commit**

```bash
git add src/native/capture/xenos_tiling.h src/native/capture/terrain_patch.h src/native/render/texture_decode.h tests/native/test_texture_decode.cpp tests/run_native_tests.cmd
git commit -m "Native textures: fetch constant decode and level untiling"
```

---

### Task 2: UV element decode and texture fetch slots (pure)

**Files:**
- Create: `src/native/capture/uv_decode.h`, `tests/native/test_uv_decode.cpp`
- Modify: `src/native/capture/vfetch_decode.h`, `tests/native/test_vfetch_decode.cpp`, `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `VertexFetch` (vfetch_decode.h), `HalfToFloat`, `detail::Be16/BeFloat` (position_decode.h).
- Produces: `enum class UvFormat`, `struct UvLayout {format, is_signed, normalized, exp_adjust, offset_bytes, stride_bytes, fetch_slot, comp_u, comp_v, swap16}`, `UvFormatFromXenos`, `bool UvLayoutFromFetch(const VertexFetch&, uint8_t comp_u, uint8_t comp_v, UvLayout*)`, `bool ApplyUvEndian(UvLayout*, uint32_t endian)`, `bool DecodeUvs(const uint8_t* src, size_t src_size, const UvLayout&, uint32_t first_vertex, uint32_t count, Float2* out)`, `struct Float2 {float u, v;}`, `std::vector<uint32_t> TextureFetchSlots(const uint32_t* ucode, size_t dword_count)`.

`comp_u`/`comp_v` are the GPU's source components of the fetched element (0-3, before the shader's destination swizzle); under 8in32 a 16-bit component i is memory component i ^ 1, as for positions.

- [ ] **Step 1: Write the failing tests**

Create `tests/native/test_uv_decode.cpp`:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/uv_decode.h"
#include <cmath>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
static void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
static void PutBeF(uint8_t* p, float f) { uint32_t u; std::memcpy(&u, &f, 4); p[0] = u >> 24; p[1] = u >> 16; p[2] = u >> 8; p[3] = u; }

int main() {
  if (UvFormatFromXenos(31) != UvFormat::kHalf2 || UvFormatFromXenos(37) != UvFormat::kFloat2) return 1;
  if (UvFormatFromXenos(25) != UvFormat::kShort2 || UvFormatFromXenos(26) != UvFormat::kShort4) return 2;
  if (UvFormatFromXenos(32) != UvFormat::kHalf4 || UvFormatFromXenos(57) != UvFormat::kFloat3) return 3;
  if (UvFormatFromXenos(38) != UvFormat::kFloat4 || UvFormatFromXenos(6) != UvFormat::kUnknown) return 4;

  // Shader 0xECD66A10092E6562: vfetch_mini r6.yx, Offset=3, FMT_16_16_FLOAT,
  // stride 5 dwords, endian 8in32; o0.x = r6.x = source y, o0.y = source x.
  VertexFetch f;
  f.format = 31; f.fetch_slot = 95; f.stride_dwords = 5; f.offset_dwords = 3; f.mini = true;
  UvLayout l;
  if (!UvLayoutFromFetch(f, 1, 0, &l)) return 5;
  if (l.format != UvFormat::kHalf2 || l.stride_bytes != 20 || l.offset_bytes != 12 || l.fetch_slot != 95) return 6;
  if (!ApplyUvEndian(&l, 2) || !l.swap16) return 7;
  // Memory halves m0 = 0.25 (0x3400), m1 = 0.75 (0x3A00) at vertex 1.
  std::vector<uint8_t> vb(40, 0);
  PutBe16(vb.data() + 20 + 12, 0x3400);
  PutBe16(vb.data() + 20 + 14, 0x3A00);
  Float2 uv[1];
  if (!DecodeUvs(vb.data(), vb.size(), l, 1, 1, uv)) return 8;
  // GPU source y under 8in32 is memory component 0, source x is memory 1.
  if (!Near(uv[0].u, 0.25f) || !Near(uv[0].v, 0.75f)) return 9;
  // Reading past the buffer fails.
  if (DecodeUvs(vb.data(), 30, l, 1, 1, uv)) return 10;

  // float2 under 8in32 (32-bit components are not pair-swapped).
  VertexFetch g;
  g.format = 37; g.stride_dwords = 2; g.offset_dwords = 0;
  if (!UvLayoutFromFetch(g, 0, 1, &l) || !ApplyUvEndian(&l, 2) || l.swap16) return 11;
  std::vector<uint8_t> fb(8);
  PutBeF(fb.data(), 2.5f); PutBeF(fb.data() + 4, -1.0f);
  if (!DecodeUvs(fb.data(), fb.size(), l, 0, 1, uv) || !Near(uv[0].u, 2.5f) || !Near(uv[0].v, -1.0f)) return 12;
  // 16-bit normalized signed, 8in16 (memory order), with exp adjust -1.
  VertexFetch h;
  h.format = 25; h.stride_dwords = 1; h.is_signed = true; h.normalized = true; h.exp_adjust = -1;
  if (!UvLayoutFromFetch(h, 0, 1, &l) || !ApplyUvEndian(&l, 1) || l.swap16) return 13;
  std::vector<uint8_t> sb(4);
  PutBe16(sb.data(), 32767); PutBe16(sb.data() + 2, uint16_t(-32767));
  if (!DecodeUvs(sb.data(), sb.size(), l, 0, 1, uv) || !Near(uv[0].u, 0.5f) || !Near(uv[0].v, -0.5f)) return 14;
  // Rejections: unknown format, component past the element, bad endian, zero stride.
  VertexFetch bad = g; bad.format = 6;
  if (UvLayoutFromFetch(bad, 0, 1, &l)) return 15;
  if (UvLayoutFromFetch(g, 0, 2, &l)) return 16;  // float2 has no component 2
  if (!UvLayoutFromFetch(g, 0, 1, &l) || ApplyUvEndian(&l, 1)) return 17;  // 32-bit needs 8in32
  VertexFetch zero = g; zero.stride_dwords = 0;
  if (UvLayoutFromFetch(zero, 0, 1, &l)) return 18;
  return 0;
}
```

Append to `tests/native/test_vfetch_decode.cpp`, just before its final `return 0;`:

```cpp
  // Texture fetch slots: a tfetch (opcode 1) with const_index 5 and one with 2,
  // between vertex fetches; each slot listed once, ascending.
  {
    uint32_t tu[3 * 6] = {};
    PackCf(Exec(2, 2, 4, 0b01010101), 0, tu);
    Vfetch(tu + 6, 0, 0, 1, 57, true, true, 0, false, 8, 0);
    tu[9] = 1u | (5u << 20);    // tfetch, tf5
    tu[12] = 1u | (2u << 20);   // tfetch, tf2
    tu[15] = 1u | (5u << 20);   // tf5 again
    const std::vector<uint32_t> slots = TextureFetchSlots(tu, 18);
    if (slots.size() != 2 || slots[0] != 2 || slots[1] != 5) return 40;  // pick unused return codes
    if (!TextureFetchSlots(nullptr, 0).empty()) return 41;
  }
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:

```bat
clang++ -std=c++23 "%~dp0native\test_uv_decode.cpp" -o "%OUT%\uv_decode.exe" || exit /b 1
"%OUT%\uv_decode.exe" || exit /b 1
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile errors (`TextureFetchSlots` undeclared in test_vfetch_decode.cpp; `uv_decode.h` missing).

- [ ] **Step 3: Implement `TextureFetchSlots`**

In `src/native/capture/vfetch_decode.h`, add `#include <algorithm>` and, after `DecodeVertexFetches`:

```cpp
// Texture fetch constant indices (tf<n>) used by a shader's texture fetch
// instructions (ucode.h TextureFetchInstruction: opcode bits 0-4 nonzero,
// const_index bits 20-24 of dword 0), sorted and unique.
inline std::vector<uint32_t> TextureFetchSlots(const uint32_t* ucode, size_t dword_count) {
  std::vector<uint32_t> out;
  if (!ucode || dword_count < 3) return out;
  size_t cf_end = dword_count;
  for (size_t cf = 0; cf + 3 <= cf_end; cf += 3) {
    const uint32_t* dw = ucode + cf;
    const uint64_t pair[2] = {uint64_t(dw[0]) | (uint64_t(dw[1] & 0xFFFF) << 32),
                              uint64_t(dw[1] >> 16) | (uint64_t(dw[2]) << 16)};
    bool ended = false;
    for (uint64_t ins : pair) {
      const uint32_t op = uint32_t(ins >> 44) & 0xF;
      if (!detail::IsExec(op)) continue;
      const uint32_t address = uint32_t(ins) & 0xFFF;
      const uint32_t count = uint32_t(ins >> 12) & 0x7;
      const uint32_t sequence = uint32_t(ins >> 16) & 0xFFF;
      if (size_t(address) * 3 < cf_end) cf_end = size_t(address) * 3;
      for (uint32_t i = 0; i < count; ++i) {
        if (!((sequence >> (2 * i)) & 1)) continue;  // ALU
        const size_t at = (size_t(address) + i) * 3;
        if (at + 3 > dword_count) break;
        const uint32_t d0 = ucode[at];
        const uint32_t opcode = d0 & 0x1F;
        if (opcode == 0) continue;  // vertex fetch
        out.push_back((d0 >> 20) & 0x1F);
      }
      if (detail::IsExecEnd(op)) { ended = true; break; }
    }
    if (ended) break;
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
```

Note: opcodes 1 (`tfetch`) through 0x19 include `getTextureBorderColorFrac`, `getTextureComputedLod` etc.; they also name a texture fetch constant, which is what the discovery rows need.

- [ ] **Step 4: Implement `uv_decode.h`**

Create `src/native/capture/uv_decode.h`:

```cpp
#pragma once

// UV vertex elements -> float2 (pure: no SDK/GPU deps). A table entry names
// the vertex fetch and the two GPU source components the shader routes into
// the albedo coordinates (vs-transforms.json "uv"); the scale/offset is
// applied later, per draw, from shader constants.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "position_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

enum class UvFormat : uint8_t { kUnknown, kFloat2, kFloat3, kFloat4, kHalf2, kHalf4, kShort2, kShort4 };

struct Float2 {
  float u, v;
};

struct UvLayout {
  UvFormat format = UvFormat::kUnknown;
  bool is_signed = true;      // kShort* only
  bool normalized = true;     // kShort* only
  int exp_adjust = 0;         // kShort* only
  uint32_t offset_bytes = 0;  // within one vertex
  uint32_t stride_bytes = 0;
  uint32_t fetch_slot = 0;
  uint8_t comp_u = 0, comp_v = 1;  // GPU source components
  bool swap16 = false;             // 8in32 on 16-bit components
};

inline UvFormat UvFormatFromXenos(uint32_t f) {
  switch (f) {
    case 37: return UvFormat::kFloat2;  // k_32_32_FLOAT
    case 57: return UvFormat::kFloat3;  // k_32_32_32_FLOAT
    case 38: return UvFormat::kFloat4;  // k_32_32_32_32_FLOAT
    case 31: return UvFormat::kHalf2;   // k_16_16_FLOAT
    case 32: return UvFormat::kHalf4;   // k_16_16_16_16_FLOAT
    case 25: return UvFormat::kShort2;  // k_16_16
    case 26: return UvFormat::kShort4;  // k_16_16_16_16
    default: return UvFormat::kUnknown;
  }
}

inline uint32_t UvComponents(UvFormat f) {
  switch (f) {
    case UvFormat::kFloat2: case UvFormat::kHalf2: case UvFormat::kShort2: return 2;
    case UvFormat::kFloat3: return 3;
    case UvFormat::kFloat4: case UvFormat::kHalf4: case UvFormat::kShort4: return 4;
    default: return 0;
  }
}

inline bool UvSixteen(UvFormat f) {
  return f == UvFormat::kHalf2 || f == UvFormat::kHalf4 || f == UvFormat::kShort2 || f == UvFormat::kShort4;
}

inline bool UvLayoutFromFetch(const VertexFetch& f, uint8_t comp_u, uint8_t comp_v, UvLayout* out) {
  UvLayout l;
  l.format = UvFormatFromXenos(f.format);
  const uint32_t n = UvComponents(l.format);
  if (n == 0 || f.stride_dwords == 0 || f.offset_dwords < 0 || comp_u >= n || comp_v >= n) return false;
  l.is_signed = f.is_signed;
  l.normalized = f.normalized;
  l.exp_adjust = f.exp_adjust;
  l.offset_bytes = uint32_t(f.offset_dwords) * 4;
  l.stride_bytes = f.stride_dwords * 4;
  l.fetch_slot = f.fetch_slot;
  l.comp_u = comp_u;
  l.comp_v = comp_v;
  *out = l;
  return true;
}

// Same endian rules as ApplyFetchEndian (position_decode.h).
inline bool ApplyUvEndian(UvLayout* l, uint32_t endian) {
  if (UvSixteen(l->format) && (endian == 1 || endian == 2)) {
    l->swap16 = endian == 2;
    return true;
  }
  if (!UvSixteen(l->format) && l->format != UvFormat::kUnknown && endian == 2) {
    l->swap16 = false;
    return true;
  }
  return false;
}

namespace detail {
inline float UvComponent(const uint8_t* p, const UvLayout& l, uint32_t memory_comp) {
  switch (l.format) {
    case UvFormat::kFloat2: case UvFormat::kFloat3: case UvFormat::kFloat4:
      return BeFloat(p + 4 * memory_comp);
    case UvFormat::kHalf2: case UvFormat::kHalf4:
      return HalfToFloat(Be16(p + 2 * memory_comp));
    default: {
      const uint16_t raw = Be16(p + 2 * memory_comp);
      float v;
      if (l.is_signed) {
        const int16_t s = int16_t(raw);
        v = l.normalized ? std::fmax(float(s) / 32767.0f, -1.0f) : float(s);
      } else {
        v = l.normalized ? float(raw) / 65535.0f : float(raw);
      }
      return l.exp_adjust ? std::ldexp(v, l.exp_adjust) : v;
    }
  }
}
}  // namespace detail

inline bool DecodeUvs(const uint8_t* src, size_t src_size, const UvLayout& l, uint32_t first_vertex,
                      uint32_t count, Float2* out) {
  const uint32_t n = UvComponents(l.format);
  if (!src || n == 0 || l.stride_bytes == 0) return false;
  const uint32_t bytes = n * (UvSixteen(l.format) ? 2 : 4);
  const uint32_t mu = l.swap16 && UvSixteen(l.format) ? (l.comp_u ^ 1u) : l.comp_u;
  const uint32_t mv = l.swap16 && UvSixteen(l.format) ? (l.comp_v ^ 1u) : l.comp_v;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = uint64_t(first_vertex + i) * l.stride_bytes + l.offset_bytes;
    if (at + bytes > src_size) return false;
    out[i] = {detail::UvComponent(src + at, l, mu), detail::UvComponent(src + at, l, mv)};
  }
  return true;
}

}  // namespace fable2::native::capture
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `& .\tests\run_native_tests.cmd`
Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/native/capture/uv_decode.h src/native/capture/vfetch_decode.h tests/native/test_uv_decode.cpp tests/native/test_vfetch_decode.cpp tests/run_native_tests.cmd
git commit -m "Native textures: UV element decode and texture fetch slots"
```

---

### Task 3: Material record, table specs and UV composition (pure)

**Files:**
- Create: `src/native/capture/material.h`, `tests/native/test_material.cpp`
- Modify: `src/native/capture/draw_record.h`, `tests/native/test_frame_scene.cpp` (only if it constructs `DrawRecord` aggregates that break), `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `UvLayout`, `VertexFetch`, `BufferRef` (moved: see Step 3).
- Produces (namespace `fable2::native::capture`):
  - `enum class MaterialStatus : uint8_t { kTextured, kTerrain, kPsUnknown, kNoAlbedo, kUvUnsupported, kFormatUnsupported, kTexturePending, kTextureDynamic, kTextureBad, kCount }`, `const char* MaterialStatusName(MaterialStatus)`.
  - `struct Material { MaterialStatus status; uint64_t ps_hash; uint32_t fetch[6]; BufferRef uv_vb; UvLayout uv; float uv_xform[4]; }` — `uv_xform` = (scale_u, scale_v, offset_u, offset_v).
  - `inline constexpr int32_t kNoRef = -1;` `struct UvStage { int32_t scale = kNoRef, offset = kNoRef; }` — ref = `(negate << 11) | (bank << 10) | (reg * 4 + comp)`, bank 0 vertex, 1 pixel.
  - `struct VsUvSpec { uint64_t vs_hash; uint8_t interp, comp; int8_t fetch_index; uint8_t src_comp; uint8_t xenos_format; int32_t offset_dwords; UvStage stages[2]; }`.
  - `struct AlbedoSpec { uint64_t ps_hash; int8_t slot; uint8_t u_interp, u_comp, v_interp, v_comp; UvStage u_stages[2], v_stages[2]; }` — `slot = -1` = the shader has no albedo.
  - `float RefValue(int32_t ref, float none, const float* vs_bank, const float* ps_bank)`.
  - `void ComposeAxis(const UvStage* vs, const UvStage* ps, const float* vs_bank, const float* ps_bank, float* scale, float* offset)` (two stages each; VS stages apply first).
  - `bool ResolveUvFetch(const std::vector<VertexFetch>&, const VsUvSpec& u, const VsUvSpec& v, UvLayout*)`.
  - `void ForEachRef(const UvStage* stages, int n, F&& f)` helper for the capture to know which registers to read.
  - `DrawRecord::material`, `DrawInputs::material`.

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_material.cpp`:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/draw_record.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
static int32_t Ref(int bank, int reg, int comp, bool neg = false) {
  return (int32_t(neg) << 11) | (bank << 10) | (reg * 4 + comp);
}

int main() {
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextured), "textured") != 0) return 1;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextureDynamic), "texture-dynamic") != 0) return 2;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kUvUnsupported), "uv-unsupported") != 0) return 3;

  float vs[1024] = {}, ps[1024] = {};
  vs[10 * 4 + 1] = 2.0f;   // c10.y (vertex)
  vs[10 * 4 + 3] = 0.5f;   // c10.w
  ps[8 * 4 + 1] = 3.0f;    // c8.y (pixel)
  ps[8 * 4 + 3] = -1.0f;   // c8.w
  if (!Near(RefValue(kNoRef, 1.0f, vs, ps), 1.0f) || !Near(RefValue(Ref(1, 8, 1), 0, vs, ps), 3.0f)) return 4;
  if (!Near(RefValue(Ref(0, 10, 3, true), 0, vs, ps), -0.5f)) return 5;
  // VS: x*c10.y + c10.w, then PS: x*c8.y + c8.w  ->  x*6 + (0.5*3 - 1) = x*6 + 0.5
  UvStage vst[2] = {{Ref(0, 10, 1), Ref(0, 10, 3)}, {}};
  UvStage pst[2] = {{Ref(1, 8, 1), Ref(1, 8, 3)}, {}};
  float a = 0, b = 0;
  ComposeAxis(vst, pst, vs, ps, &a, &b);
  if (!Near(a, 6.0f) || !Near(b, 0.5f)) return 6;
  UvStage none[2] = {};
  ComposeAxis(none, none, vs, ps, &a, &b);
  if (!Near(a, 1.0f) || !Near(b, 0.0f)) return 7;

  // ResolveUvFetch: both axes from fetch 1 (half2 at offset 3), sources y then x.
  std::vector<VertexFetch> f(2);
  f[0].format = 32; f[0].stride_dwords = 5; f[0].offset_dwords = 0;
  f[1].format = 31; f[1].stride_dwords = 5; f[1].offset_dwords = 3; f[1].mini = true;
  VsUvSpec u{0x1, 0, 0, 1, 1, 31, 3, {}};
  VsUvSpec v{0x1, 0, 1, 1, 0, 31, 3, {}};
  UvLayout l;
  if (!ResolveUvFetch(f, u, v, &l) || l.comp_u != 1 || l.comp_v != 0 || l.offset_bytes != 12) return 8;
  VsUvSpec wrong_fmt = u; wrong_fmt.xenos_format = 37;      // table disagrees with the decoded fetch
  if (ResolveUvFetch(f, wrong_fmt, v, &l)) return 9;
  VsUvSpec wrong_off = v; wrong_off.offset_dwords = 0;
  if (ResolveUvFetch(f, u, wrong_off, &l)) return 10;
  VsUvSpec other = v; other.fetch_index = 0; other.xenos_format = 32; other.offset_dwords = 0;
  if (ResolveUvFetch(f, u, other, &l)) return 11;           // u and v from different fetches
  VsUvSpec past = u; past.fetch_index = 5;
  if (ResolveUvFetch(f, past, v, &l)) return 12;

  // Refs visited by ForEachRef (the capture reads only these registers).
  std::vector<int32_t> seen;
  ForEachRef(vst, 2, [&](int32_t r) { seen.push_back(r); });
  if (seen.size() != 2 || seen[0] != Ref(0, 10, 1) || seen[1] != Ref(0, 10, 3)) return 13;

  // Records carry the material through AssembleRecord.
  DrawInputs in;
  in.prim = 4; in.have_shader = true; in.have_pos = true; in.have_vb = true; in.vb = {0x1000, 64};
  in.pos.format = PosFormat::kFloat3; in.pos.stride_bytes = 12;
  TransformInfo ti{0, TransformLayout::kDot, -1};
  float bank[1024] = {};
  in.transform = &ti; in.bank = bank;
  in.material.status = MaterialStatus::kTextured;
  in.material.uv_xform[0] = 4.0f;
  DrawRecord r = AssembleRecord(in, 7);
  if (r.skip != SkipReason::kNone || r.material.status != MaterialStatus::kTextured || r.material.uv_xform[0] != 4.0f) return 14;
  // Default material status is ps-unknown.
  if (Material{}.status != MaterialStatus::kPsUnknown) return 15;
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:

```bat
clang++ -std=c++23 "%~dp0native\test_material.cpp" -o "%OUT%\material.exe" || exit /b 1
"%OUT%\material.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile errors (`MaterialStatusName`, `Material` undeclared).

- [ ] **Step 3: Implement `material.h` and wire it into `draw_record.h`**

Move `struct BufferRef` from `draw_record.h` into the new `material.h` (draw_record.h includes material.h, so existing users still see it). Create `src/native/capture/material.h`:

```cpp
#pragma once

// Albedo material of a captured draw (pure: no SDK/GPU deps): the albedo
// texture's fetch constant, the UV element and a per-draw UV scale/offset
// folded from shader constants (ps-albedo.json, vs-transforms.json "uv").

#include <cstdint>
#include <vector>

#include "uv_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

struct BufferRef {
  uint32_t phys_addr = 0;
  uint32_t size = 0;
};

enum class MaterialStatus : uint8_t {
  kTextured,           // capture: albedo and UVs resolved; renderer: drawn textured
  kTerrain,
  kPsUnknown,
  kNoAlbedo,
  kUvUnsupported,
  kFormatUnsupported,  // renderer
  kTexturePending,     // renderer
  kTextureDynamic,     // renderer
  kTextureBad,         // renderer
  kCount
};

inline const char* MaterialStatusName(MaterialStatus s) {
  switch (s) {
    case MaterialStatus::kTextured: return "textured";
    case MaterialStatus::kTerrain: return "terrain";
    case MaterialStatus::kPsUnknown: return "ps-unknown";
    case MaterialStatus::kNoAlbedo: return "no-albedo";
    case MaterialStatus::kUvUnsupported: return "uv-unsupported";
    case MaterialStatus::kFormatUnsupported: return "format-unsupported";
    case MaterialStatus::kTexturePending: return "texture-pending";
    case MaterialStatus::kTextureDynamic: return "texture-dynamic";
    case MaterialStatus::kTextureBad: return "texture-bad";
    default: return "?";
  }
}

struct Material {
  MaterialStatus status = MaterialStatus::kPsUnknown;
  uint64_t ps_hash = 0;
  uint32_t fetch[6] = {};  // albedo texture fetch constant, host-order dwords
  BufferRef uv_vb;         // stream holding the UV element
  UvLayout uv;
  float uv_xform[4] = {1.0f, 1.0f, 0.0f, 0.0f};  // u*x + z, v*y + w
};

// Constant reference in a UV transform: (negate << 11) | (bank << 10) | (reg * 4 + comp),
// bank 0 = vertex constants, 1 = pixel constants; kNoRef = identity.
inline constexpr int32_t kNoRef = -1;

struct UvStage {
  int32_t scale = kNoRef;
  int32_t offset = kNoRef;
};

// vs-transforms.json "uv": interpolator `interp` component `comp` is
// vertex fetch `fetch_index` (DecodeVertexFetches order) source component
// `src_comp`, through up to two stages. xenos_format/offset_dwords are what the
// tool saw for that fetch; a mismatch at runtime rejects the entry.
struct VsUvSpec {
  uint64_t vs_hash;
  uint8_t interp, comp;
  int8_t fetch_index;
  uint8_t src_comp;
  uint8_t xenos_format;
  int32_t offset_dwords;
  UvStage stages[2];
};

// ps-albedo.json: the albedo is texture fetch constant `slot`; its u and v
// coordinates are interpolator components through up to two stages each.
// slot -1: the shader samples no albedo.
struct AlbedoSpec {
  uint64_t ps_hash;
  int8_t slot;
  uint8_t u_interp, u_comp, v_interp, v_comp;
  UvStage u_stages[2];
  UvStage v_stages[2];
};

inline float RefValue(int32_t ref, float none, const float* vs_bank, const float* ps_bank) {
  if (ref < 0) return none;
  const float* bank = ((ref >> 10) & 1) ? ps_bank : vs_bank;
  const float v = bank[ref & 0x3FF];
  return ((ref >> 11) & 1) ? -v : v;
}

template <typename F>
inline void ForEachRef(const UvStage* stages, int n, F&& f) {
  for (int i = 0; i < n; ++i) {
    if (stages[i].scale >= 0) f(stages[i].scale);
    if (stages[i].offset >= 0) f(stages[i].offset);
  }
}

// x' = x * scale + offset per stage, VS stages first: folds into one affine map.
inline void ComposeAxis(const UvStage* vs, const UvStage* ps, const float* vs_bank, const float* ps_bank,
                        float* scale, float* offset) {
  float a = 1.0f, b = 0.0f;
  auto apply = [&](const UvStage& st) {
    const float s = RefValue(st.scale, 1.0f, vs_bank, ps_bank);
    const float o = RefValue(st.offset, 0.0f, vs_bank, ps_bank);
    a *= s;
    b = b * s + o;
  };
  for (int i = 0; i < 2; ++i) apply(vs[i]);
  for (int i = 0; i < 2; ++i) apply(ps[i]);
  *scale = a;
  *offset = b;
}

// Both axes must come from the same vertex fetch, which must match the format
// and offset the tool recorded (guards against a different fetch order).
inline bool ResolveUvFetch(const std::vector<VertexFetch>& fetches, const VsUvSpec& u, const VsUvSpec& v,
                           UvLayout* out) {
  if (u.fetch_index != v.fetch_index || u.fetch_index < 0 || size_t(u.fetch_index) >= fetches.size()) return false;
  const VertexFetch& f = fetches[size_t(u.fetch_index)];
  for (const VsUvSpec* s : {&u, &v}) {
    if (f.format != s->xenos_format || f.offset_dwords != s->offset_dwords) return false;
  }
  return UvLayoutFromFetch(f, u.src_comp, v.src_comp, out);
}

}  // namespace fable2::native::capture
```

In `src/native/capture/draw_record.h`: add `#include "material.h"`, delete the moved `BufferRef`, add `Material material;` to `DrawRecord` (after `TerrainPatch terrain;`, comment: `// Albedo texture and UVs (material.h); status says why a draw stays clay.`) and to `DrawInputs` (after `TerrainPatch terrain;`), and in `AssembleRecord` set `r.material = in.material;` right after `r.vs_hash = in.vs_hash;`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `& .\tests\run_native_tests.cmd`
Expected: exit 0 (material, frame_scene, clay_logic and the rest still pass). Also `& .\build.cmd -release fable_2` exit 0 (capture.cpp still compiles).

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/material.h src/native/capture/draw_record.h tests/native/test_material.cpp tests/run_native_tests.cmd
git commit -m "Native textures: material record, table specs and UV composition"
```

---

### Task 4: Texture residency logic (pure)

**Files:**
- Create: `src/native/render/texture_residency.h`, `tests/native/test_texture_residency.cpp`
- Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces (namespace `fable2::native::render`): `inline constexpr uint32_t kSampleCount = 16, kSampleBytes = 256, kDynamicStreak = 8, kDynamicClearFrames = 60;` `uint64_t SampleHash(const uint8_t* data, uint32_t size)`; `struct TextureState { uint64_t sample_hash = 0; uint64_t last_frame = 0; uint64_t last_change = 0; uint32_t streak = 0; bool dynamic = false; }`; `bool NoteSample(TextureState&, uint64_t sample_hash, uint64_t frame)` (returns true when the contents changed since the last observation); `class UploadBudget { explicit UploadBudget(uint64_t bytes); void BeginFrame(uint64_t bytes); bool TryTake(uint64_t bytes); uint64_t used() const; }`.
- The LRU is `GeometryCacheIndex` (geometry_cache_index.h) with `GeoKey::kind = 2`.

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_texture_residency.cpp`:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/geometry_cache_index.h"
#include "../../src/native/render/texture_residency.h"
#include <vector>

using namespace fable2::native::render;

int main() {
  // --- sample hash: sensitive inside the sampled ranges, cheap on big textures ---
  std::vector<uint8_t> tex(1 << 20, 0x11);
  const uint64_t h0 = SampleHash(tex.data(), uint32_t(tex.size()));
  tex[0] ^= 1;  // first byte is always sampled
  if (SampleHash(tex.data(), uint32_t(tex.size())) == h0) return 1;
  tex[0] ^= 1;
  tex[tex.size() - 1] ^= 1;  // last byte is always sampled
  if (SampleHash(tex.data(), uint32_t(tex.size())) == h0) return 2;
  tex[tex.size() - 1] ^= 1;
  if (SampleHash(tex.data(), uint32_t(tex.size())) != h0) return 3;
  // Small textures are hashed whole.
  std::vector<uint8_t> small(1000, 3);
  const uint64_t s0 = SampleHash(small.data(), 1000);
  small[500] = 4;
  if (SampleHash(small.data(), 1000) == s0) return 4;
  if (SampleHash(nullptr, 0) != SampleHash(nullptr, 0)) return 5;

  // --- change streaks ---
  TextureState st;
  if (!NoteSample(st, 1, 1)) return 6;          // first observation counts as a change (upload)
  if (NoteSample(st, 1, 2) || st.dynamic) return 7;
  for (uint64_t f = 3; f < 3 + kDynamicStreak; ++f) NoteSample(st, 100 + f, f);  // changes every frame
  if (st.dynamic) return 8;                     // exactly kDynamicStreak changed frames: not yet
  NoteSample(st, 999, 3 + kDynamicStreak);
  if (!st.dynamic) return 9;                    // one more: dynamic
  // A gap in observations (texture not drawn) resets the streak, but dynamic
  // clears only after kDynamicClearFrames frames without a change.
  uint64_t f = 3 + kDynamicStreak + 1;
  for (uint32_t i = 0; i < kDynamicClearFrames - 1; ++i, ++f) NoteSample(st, 999, f);
  if (!st.dynamic) return 10;
  NoteSample(st, 999, f);
  if (st.dynamic) return 11;
  TextureState gap;
  NoteSample(gap, 1, 1);
  NoteSample(gap, 2, 2);
  NoteSample(gap, 3, 10);                       // not observed in frames 3-9
  if (gap.streak != 1) return 12;

  // --- upload budget ---
  UploadBudget b(100);
  b.BeginFrame(100);
  if (!b.TryTake(60) || b.used() != 60) return 13;
  if (b.TryTake(50)) return 14;                 // would exceed
  if (!b.TryTake(40) || b.used() != 100) return 15;
  b.BeginFrame(100);
  if (!b.TryTake(500)) return 16;               // one oversize upload per frame is allowed
  if (b.TryTake(1)) return 17;
  b.BeginFrame(100);
  if (!b.TryTake(10) || b.TryTake(500)) return 18;  // oversize only as the first upload

  // --- LRU via GeometryCacheIndex (kind 2): nothing used this frame is evicted ---
  GeometryCacheIndex idx(100);
  idx.BeginFrame(1);
  std::vector<uint32_t> ev;
  const GeoKey k1{0x1000, 64, 0, 0, 2}, k2{0x2000, 64, 0, 0, 2};
  idx.Insert(k1, 1, 80, &ev);
  idx.Insert(k2, 2, 80, &ev);
  if (!ev.empty() || idx.resident_bytes() != 160) return 19;  // over budget, both in use
  idx.BeginFrame(2);
  idx.Lookup(k2, 2);
  idx.Insert(GeoKey{0x3000, 64, 0, 0, 2}, 3, 10, &ev);
  if (ev.size() != 1 || idx.Lookup(k1, 1).hit) return 20;     // k1 (unused this frame) evicted
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:

```bat
clang++ -std=c++23 "%~dp0native\test_texture_residency.cpp" -o "%OUT%\texture_residency.exe" || exit /b 1
"%OUT%\texture_residency.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile error, `texture_residency.h` not found.

- [ ] **Step 3: Write the implementation**

Create `src/native/render/texture_residency.h`:

```cpp
#pragma once

// Texture cache policy (pure: no SDK/GPU deps): a sparse content hash checked
// every frame, change streaks that mark render-target-like textures dynamic,
// and the per-frame upload budget.

#include <algorithm>
#include <cstdint>

namespace fable2::native::render {

inline constexpr uint32_t kSampleCount = 16;
inline constexpr uint32_t kSampleBytes = 256;
// More than this many consecutive observed frames with a change: dynamic.
inline constexpr uint32_t kDynamicStreak = 8;
// A dynamic texture becomes static again after this many observed frames
// without a change.
inline constexpr uint32_t kDynamicClearFrames = 60;

// FNV-1a over kSampleCount ranges of kSampleBytes spread evenly over
// [0, size), always including the first and last bytes; textures of at most
// kSampleCount * kSampleBytes bytes are hashed whole.
inline uint64_t SampleHash(const uint8_t* data, uint32_t size) {
  uint64_t h = 1469598103934665603ull ^ size;
  auto mix = [&](uint32_t from, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) h = (h ^ data[from + i]) * 1099511628211ull;
  };
  if (!data || size == 0) return h;
  if (size <= kSampleCount * kSampleBytes) {
    mix(0, size);
    return h;
  }
  const uint64_t span = size - kSampleBytes;
  for (uint32_t i = 0; i < kSampleCount; ++i) {
    mix(uint32_t(span * i / (kSampleCount - 1)), kSampleBytes);
  }
  return h;
}

struct TextureState {
  uint64_t sample_hash = 0;
  uint64_t last_frame = 0;   // last observation (0 = never)
  uint64_t last_change = 0;
  uint32_t streak = 0;       // consecutive observed frames with a change
  bool dynamic = false;
};

// Records this frame's sample hash. Returns true if the contents changed
// (always on the first observation).
inline bool NoteSample(TextureState& s, uint64_t sample_hash, uint64_t frame) {
  const bool first = s.last_frame == 0;
  const bool changed = first || sample_hash != s.sample_hash;
  const bool consecutive = !first && frame == s.last_frame + 1;
  if (changed && !first) {
    s.streak = consecutive ? s.streak + 1 : 1;
    s.last_change = frame;
    if (s.streak > kDynamicStreak) s.dynamic = true;
  } else if (!changed) {
    s.streak = 0;
    if (s.dynamic && frame - s.last_change >= kDynamicClearFrames) s.dynamic = false;
  }
  if (first) s.last_change = frame;
  s.sample_hash = sample_hash;
  s.last_frame = frame;
  return changed;
}

// Guest bytes decoded per frame. The first upload of a frame may exceed the
// budget (a texture larger than the budget would otherwise never load).
class UploadBudget {
 public:
  explicit UploadBudget(uint64_t bytes) : budget_(bytes) {}
  void BeginFrame(uint64_t bytes) {
    budget_ = bytes;
    used_ = 0;
  }
  bool TryTake(uint64_t bytes) {
    if (used_ == 0 || used_ + bytes <= budget_) {
      used_ += bytes;
      return true;
    }
    return false;
  }
  uint64_t used() const { return used_; }

 private:
  uint64_t budget_;
  uint64_t used_ = 0;
};

}  // namespace fable2::native::render
```

Check against the test: in the streak test the first `NoteSample(st, 1, 1)` is the first observation (streak stays 0), frame 2 unchanged (streak 0), frames 3..10 change each frame (streak 1..8, not dynamic), frame 11 changes (streak 9 > 8, dynamic). Then frames 12..70 unchanged: `frame - last_change` reaches 60 at frame 71, so after 59 unchanged frames it is still dynamic and the 60th clears it. In the gap test frame 2 changes (streak 1), frame 10 changes non-consecutively (streak reset to 1).

- [ ] **Step 4: Run tests to verify they pass**

Run: `& .\tests\run_native_tests.cmd`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/texture_residency.h tests/native/test_texture_residency.cpp tests/run_native_tests.cmd
git commit -m "Native textures: residency policy (sample hash, dynamic streaks, upload budget)"
```

---

### Task 5: Table generators (Python)

**Files:**
- Create: `tools/xdk_sigmatch/gen_albedo_table.py`, `tools/xdk_sigmatch/uv_refs.py`, `tests/test_gen_albedo_table.py`, `docs/native-renderer/ps-albedo.json` (content `{}`), `src/native/capture/ps_albedo_table.inc` (generated, comment line only)
- Modify: `tools/xdk_sigmatch/gen_transform_table.py`, `tests/test_gen_transform_table.py`, `src/native/capture/capture.cpp` (add empty `FABLE2_VS_UV` defines to the four existing table blocks so the build keeps compiling)

**Interfaces:**
- JSON formats (both tables use the same stage notation):
  - `ps-albedo.json`: `{"0x<PS HASH>": {"slot": 0, "u": {"input": "r0.y", "stages": [{"scale": "c8.y", "offset": "c8.w"}]}, "v": {"input": "r0.x", "stages": []}, "manual": true, "evidence": "..."}}` or `{"0x<PS HASH>": {"no_albedo": "<reason>", "manual": true, "evidence": "..."}}`. Stage refs are pixel constants; `"-c8.w"` negates; `null` scale/offset = identity; at most 2 stages, applied in list order.
  - `vs-transforms.json` entry gains `"uv": {"o0.x": {"fetch": 1, "src": "y", "format": 31, "offset": 3, "stages": [...]}, "o0.y": {...}}` (vertex constant refs).
- Generated macros:
  - `FABLE2_PS_ALBEDO(hash, slot, u_interp, u_comp, u_s0, u_o0, u_s1, u_o1, v_interp, v_comp, v_s0, v_o0, v_s1, v_o1)`
  - `FABLE2_PS_NO_ALBEDO(hash)`
  - `FABLE2_VS_UV(hash, interp, comp, fetch_index, src_comp, xenos_format, offset_dwords, s0, o0, s1, o1)` (emitted into `vs_transform_table.inc` after the other extras)
- `uv_refs.encode_ref(text, bank)` and `uv_refs.stages(list, bank)` shared by both generators and by `albedo_finder.py` (Task 8).

- [ ] **Step 1: Write the failing tests**

Create `tests/test_gen_albedo_table.py`:

```python
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import gen_albedo_table  # noqa: E402
import uv_refs  # noqa: E402


class UvRefsTest(unittest.TestCase):
    def test_encode(self):
        self.assertEqual(uv_refs.encode_ref("c8.y", 1), (1 << 10) | (8 * 4 + 1))
        self.assertEqual(uv_refs.encode_ref("-c10.w", 0), (1 << 11) | (10 * 4 + 3))
        self.assertEqual(uv_refs.encode_ref(None, 0), -1)
        for bad in ("c256.x", "r1.x", "c1.xy", "c1"):
            with self.assertRaises(ValueError):
                uv_refs.encode_ref(bad, 0)

    def test_stages(self):
        self.assertEqual(uv_refs.stages([], 1), [-1, -1, -1, -1])
        self.assertEqual(uv_refs.stages([{"scale": "c8.y", "offset": None}], 1), [(1 << 10) | 33, -1, -1, -1])
        with self.assertRaises(ValueError):
            uv_refs.stages([{}, {}, {}], 0)

    def test_component(self):
        self.assertEqual(uv_refs.input_comp("r0.y"), (0, 1))
        self.assertEqual(uv_refs.input_comp("o3.w"), (3, 3))
        with self.assertRaises(ValueError):
            uv_refs.input_comp("r0.xy")


class GenAlbedoTest(unittest.TestCase):
    def test_generate(self):
        data = {
            "0x00E09D1BC5295D52": {"slot": 0, "manual": True, "evidence": "e",
                                   "u": {"input": "r0.y", "stages": [{"scale": "c8.y", "offset": "c8.w"}]},
                                   "v": {"input": "r0.x", "stages": [{"scale": "c8.x", "offset": "c8.z"}]}},
            "0x0000000000000002": {"no_albedo": "fog only", "manual": True, "evidence": "e"},
        }
        out = gen_albedo_table.generate(data).splitlines()
        self.assertTrue(out[0].startswith("// Generated"))
        self.assertEqual(out[1], "FABLE2_PS_NO_ALBEDO(0x0000000000000002ull)")
        s = (1 << 10)
        self.assertEqual(out[2], f"FABLE2_PS_ALBEDO(0x00E09D1BC5295D52ull, 0, 0, 1, {s | 33}, {s | 35}, -1, -1, "
                                 f"0, 0, {s | 32}, {s | 34}, -1, -1)")

    def test_rejects_bad_slot(self):
        with self.assertRaises(ValueError):
            gen_albedo_table.generate({"0x1": {"slot": 32, "u": {"input": "r0.x", "stages": []},
                                                "v": {"input": "r0.y", "stages": []}}})


if __name__ == "__main__":
    unittest.main()
```

Append to `tests/test_gen_transform_table.py` (inside its existing test class; adapt the class name to the file):

```python
    def test_uv_entries(self):
        data = {"0xECD66A10092E6562": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "uv": {"o0.x": {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []},
                                              "o0.y": {"fetch": 2, "src": "x", "format": 31, "offset": 3,
                                                       "stages": [{"scale": "c10.y", "offset": "-c10.w"}]}}}}
        out = gen_transform_table.generate(data).splitlines()
        self.assertIn("FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 0, 2, 1, 31, 3, -1, -1, -1, -1)", out)
        self.assertIn(f"FABLE2_VS_UV(0xECD66A10092E6562ull, 0, 1, 2, 0, 31, 3, {41}, {(1 << 11) | 43}, -1, -1)", out)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s tests -p "test_gen_albedo_table.py" -v` and `python -m unittest discover -s tests -p "test_gen_transform_table.py" -v`
Expected: ImportError (`gen_albedo_table`, `uv_refs`), and the uv test fails (no `FABLE2_VS_UV` lines).

- [ ] **Step 3: Implement**

Create `tools/xdk_sigmatch/uv_refs.py`:

```python
"""Shared notation for UV transforms in ps-albedo.json and vs-transforms.json "uv".

A constant ref "c8.y" / "-c8.y" encodes as (negate << 11) | (bank << 10) | (reg * 4 + comp)
with bank 0 = vertex constants, 1 = pixel constants; None = identity (-1).
A stage list holds at most two {"scale": ref|None, "offset": ref|None}, applied in order.
"""
import re

_COMP = "xyzw"


def encode_ref(text, bank):
    if text is None:
        return -1
    m = re.fullmatch(r"(-?)c(\d+)\.([xyzw])", text)
    if not m or int(m.group(2)) > 255:
        raise ValueError(f"bad constant ref {text!r}")
    neg = 1 if m.group(1) else 0
    return (neg << 11) | (bank << 10) | (int(m.group(2)) * 4 + _COMP.index(m.group(3)))


def stages(items, bank):
    if len(items) > 2:
        raise ValueError(f"more than two UV stages: {items!r}")
    out = []
    for s in list(items) + [{}] * (2 - len(items)):
        out += [encode_ref(s.get("scale"), bank), encode_ref(s.get("offset"), bank)]
    return out


def input_comp(text):
    """'r0.y' (pixel shader input) or 'o3.w' (vertex shader export) -> (index, component)."""
    m = re.fullmatch(r"[ro](\d+)\.([xyzw])", text)
    if not m or int(m.group(1)) > 15:
        raise ValueError(f"bad interpolator component {text!r}")
    return int(m.group(1)), _COMP.index(m.group(2))
```

Create `tools/xdk_sigmatch/gen_albedo_table.py`:

```python
"""ps-albedo.json -> src/native/capture/ps_albedo_table.inc.

  FABLE2_PS_ALBEDO(hash, slot, u_interp, u_comp, u_s0, u_o0, u_s1, u_o1,
                   v_interp, v_comp, v_s0, v_o0, v_s1, v_o1)
  FABLE2_PS_NO_ALBEDO(hash)
Stage refs use uv_refs notation with bank 1 (pixel constants).
"""
import argparse
import json
from pathlib import Path

import uv_refs


def generate(data):
    lines = ["// Generated by tools/xdk_sigmatch/gen_albedo_table.py - do not edit."]
    for ps, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        h = f"0x{int(ps, 16):016X}ull"
        if "no_albedo" in e:
            lines.append(f"FABLE2_PS_NO_ALBEDO({h})")
            continue
        slot = int(e["slot"])
        if not 0 <= slot <= 31:
            raise ValueError(f"{ps}: bad texture fetch slot {slot}")
        fields = [str(slot)]
        for axis in ("u", "v"):
            interp, comp = uv_refs.input_comp(e[axis]["input"])
            fields += [str(interp), str(comp)] + [str(r) for r in uv_refs.stages(e[axis]["stages"], 1)]
        lines.append(f"FABLE2_PS_ALBEDO({h}, {', '.join(fields)})")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    Path(a.out).write_text(generate(json.loads(Path(a.json).read_text())))


if __name__ == "__main__":
    main()
```

In `tools/xdk_sigmatch/gen_transform_table.py`: add `import uv_refs` (the test harness puts the tool folder on `sys.path`; when run as a script the folder is the script's own), document `FABLE2_VS_UV` in the docstring, and inside the per-entry loop after the `terrain` block:

```python
        for name, u in sorted(e.get("uv", {}).items()):
            interp, comp = uv_refs.input_comp(name)
            src = _COMP.index(u["src"])
            refs = ", ".join(str(r) for r in uv_refs.stages(u.get("stages", []), 0))
            extras.append(f"FABLE2_VS_UV({vs}ull, {interp}, {comp}, {int(u['fetch'])}, {src}, "
                          f"{int(u['format'])}, {int(u['offset'])}, {refs})")
```

Create `docs/native-renderer/ps-albedo.json` with `{}` and generate the empty include:

```
python tools\xdk_sigmatch\gen_albedo_table.py --json docs\native-renderer\ps-albedo.json --out src\native\capture\ps_albedo_table.inc
python tools\xdk_sigmatch\gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc
```

The second command must leave `vs_transform_table.inc` byte-identical (no entry has `"uv"` yet): check with `git diff --stat src/native/capture/vs_transform_table.inc` (expected: no change).

In `src/native/capture/capture.cpp`, each of the four table blocks (`kTransformTable`, the pos-swizzle table, `kSkinTable`, `kTerrainTable`) defines and undefines the existing macros around `#include "vs_transform_table.inc"`; add `#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)` and the matching `#undef FABLE2_VS_UV` to each block.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest discover -s tests -p "test_gen_*.py" -v` (expected: all OK) and `& .\build.cmd -release fable_2` (expected: exit 0).

- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/uv_refs.py tools/xdk_sigmatch/gen_albedo_table.py tools/xdk_sigmatch/gen_transform_table.py tests/test_gen_albedo_table.py tests/test_gen_transform_table.py docs/native-renderer/ps-albedo.json src/native/capture/ps_albedo_table.inc src/native/capture/capture.cpp
git commit -m "Native textures: albedo table and VS UV table generators"
```

---

### Task 6: Shader disassembly tracing tool (Python)

**Files:**
- Create: `tools/xdk_sigmatch/shader_trace.py`, `tests/test_shader_trace.py`

**Interfaces:**
- Input: SDK dumps from `fable_2.exe --dump_shaders=<dir>`: `shader_<HASH>.ucode.frag` / `.ucode.vert` (disassembly text, format below).
- Produces:
  - `parse(text) -> list[Instr]` where `Instr = namedtuple("Instr", "index op dest mask srcs fetch_slot raw")`; scalar co-issued lines (`+ op ...`) become their own `Instr` with `op` prefixed `"s:"`. `mask` is the set of written components (`"xy"`), `srcs` a list of `Src(neg, reg, swizzle, abs)`.
  - `texture_fetches(instrs) -> list[TFetch(index, dim, dest, mask, coord_reg, coord_swz, slot)]`.
  - `trace_component(instrs, before_index, reg, comp) -> Trace | Unsupported` — walks backward from instruction `before_index`; returns `Trace(base_reg, base_comp, stages)` where `stages` is a list (execution order) of `{"scale": ref|None, "offset": ref|None}` with constant refs in `uv_refs` text notation, or `Unsupported(reason)`.
  - `trace_ps_albedo(instrs, tfetch) -> dict | Unsupported` → `{"u": {"input": "r0.y", "stages": [...]}, "v": {...}}`.
  - `vfetches(instrs) -> list[VFetch(ordinal, dest, dest_swizzle, fmt, offset)]` (ordinal = position among vertex fetches, i.e. `DecodeVertexFetches` order).
  - `trace_vs_export(instrs, interp, comp) -> dict | Unsupported` → `{"fetch": n, "src": "y", "format": 31, "offset": 3, "stages": [...]}`.
  - CLI: `python tools\xdk_sigmatch\shader_trace.py <dump_dir> <PS_HASH> [--vs <VS_HASH>]` prints each texture fetch with its trace, and with `--vs` the VS-side trace of each traced interpolator.

Disassembly format (from real dumps, SDK `Shader::DumpUcode`):

```
/*    0.0 */       exec
/*   10   */          mad r10._yz_, r0.yyxx, c10.yyxx, c10.wwzz
              +       retain_prev r8._
/*   13   */          tfetch2D r10.x___, r0.zw, tf8
/*   14   */          tfetch2D r0.wxyz, r0.yx, tf0
/*    5   */          vfetch_full r5.yxw1, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=5, Signed=true, NumFormat=integer, PrefetchCount=5
/*    7   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true, NumFormat=integer
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
```

Rules (a reviewer checks each):
- Destination `r10._yz_` writes components y, z; `oPos`, `o<N>` are exports; `r_abs[9]` is an absolute-value source (unsupported in a trace). Source swizzles are 4 characters for ALU (`r0.yyxx`: destination component i reads source component swizzle[i]); short swizzles (`r0.zw` in a fetch) list coordinate components in order (u, v).
- Supported producers when walking backward to the most recent write of `reg.comp`: `mov`, `max a, a` with identical operands (a move), `mul` with exactly one constant operand (stage scale), `add` with exactly one constant operand (stage offset; a negated temp operand is unsupported), `mad` with operand 2 constant and operand 3 constant (stage scale + offset; constant/temp order in operands 1-2 may be either). Anything else (dependent texture fetch, `dp*`, `_sat`, `r_abs`, relative constants `c[...]`, a scalar co-issue writing the component, predication `(p0)`/`(!p0)` prefixes, `cexec`/`jmp`/`loop` control flow before the use) → `Unsupported("<what>")`.
- No write found before the use: the register is a shader input (PS: interpolator index = register number) or, in a VS, an unsupported read.
- Constants are `c<N>` (bank decided by the caller: vertex 0, pixel 1).
- `vfetch_full`/`vfetch_mini` destination swizzle characters map destination components to source components (`r6.yx__`: r6.x = source y). `DataFormat=FMT_16_16_FLOAT` maps to the Xenos format number through a table in the tool (FMT_16_16=25, FMT_16_16_16_16=26, FMT_16_16_FLOAT=31, FMT_16_16_16_16_FLOAT=32, FMT_32_32_FLOAT=37, FMT_32_32_32_32_FLOAT=38, FMT_32_32_32_FLOAT=57; others → `Unsupported`). `Offset=` is in dwords; absent on `vfetch_full` means 0.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_shader_trace.py`:

```python
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import shader_trace as st  # noqa: E402

PS = """/*    0.0 */       exec
/*   10   */          mad r10._yz_, r0.yyxx, c10.yyxx, c10.wwzz
              +       retain_prev r8._
/*   11   */          mad r0.xy__, r0.yxxx, c8.yxxx, c8.wzzz
              +       retain_prev r8._
/*   12   */          mad r0.__zw, r_abs[9].xxxy, c0.zzzw, c0.xxxy
              +       retain_prev r8._
/*   13   */          tfetch2D r10.x___, r0.zw, tf8
/*   14   */          tfetch2D r0.wxyz, r0.yx, tf0
/*   15   */          tfetch2D r9.xy__, r10.zy, tf2
/*    0.1 */       alloc colors
"""

VS = """/*    0.0 */       exec
/*    5   */          vfetch_full r5.yxw1, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT, Stride=5, Signed=true, NumFormat=integer, PrefetchCount=5
/*    6   */          vfetch_mini r7.xyz_, Offset=2, DataFormat=FMT_10_11_11, Signed=true
/*    7   */          vfetch_mini r6.yx__, Offset=3, DataFormat=FMT_16_16_FLOAT, Signed=true, NumFormat=integer
/*    1.0 */       exec
/*   10   */          dp4 oPos.x___, c0.zxyw, r5.zxyw
/*   16   */          mul r4.xyz_, c19.xyzz, c7.wwww
/*   22   */          dp3 r2.__z_, r7.zxyy, c6.zxyy
              +       log r3._y__, r_abs[3].y
/*   23   */          max o0.xy__, r6.xyyy, r6.xyyy
/*   24   */          dp4 o2.x___, c4.zxyw, r5.zxyw
/*   27   */          mul o1.xyz_, r2.xyzz, c7.wwww
"""


class ParseTest(unittest.TestCase):
    def test_texture_fetches(self):
        tf = st.texture_fetches(st.parse(PS))
        self.assertEqual([(t.slot, t.coord_reg, t.coord_swz, t.mask) for t in tf],
                         [(8, 0, "zw", "x"), (0, 0, "yx", "xyzw"), (2, 10, "zy", "xy")])
        self.assertEqual(tf[0].dim, "2D")

    def test_vfetches(self):
        vf = st.vfetches(st.parse(VS))
        self.assertEqual([(v.ordinal, v.dest, v.dest_swizzle, v.fmt, v.offset) for v in vf],
                         [(0, 5, "yxw1", 32, 0), (1, 7, "xyz_", None, 2), (2, 6, "yx__", 31, 3)])


class TraceTest(unittest.TestCase):
    def setUp(self):
        self.ps = st.parse(PS)
        self.tf = st.texture_fetches(self.ps)

    def test_albedo_affine(self):
        # tf0 at r0.yx: r0.x = r0.y*c8.y + c8.w ; r0.y = r0.x*c8.x + c8.z (instr 11)
        t = st.trace_ps_albedo(self.ps, self.tf[1])
        self.assertEqual(t["u"], {"input": "r0.x", "stages": [{"scale": "c8.x", "offset": "c8.z"}]})
        self.assertEqual(t["v"], {"input": "r0.y", "stages": [{"scale": "c8.y", "offset": "c8.w"}]})

    def test_abs_unsupported(self):
        t = st.trace_ps_albedo(self.ps, self.tf[0])  # r0.zw from r_abs[9]
        self.assertIsInstance(t, st.Unsupported)
        self.assertIn("abs", t.reason)

    def test_vs_export(self):
        vs = st.parse(VS)
        # o0.x = r6.x = source y of fetch 2 ; o0.y = r6.y = source x.
        self.assertEqual(st.trace_vs_export(vs, 0, 0),
                         {"fetch": 2, "src": "y", "format": 31, "offset": 3, "stages": []})
        self.assertEqual(st.trace_vs_export(vs, 0, 1)["src"], "x")
        # o2.x comes from a dp4: unsupported.
        self.assertIsInstance(st.trace_vs_export(vs, 2, 0), st.Unsupported)
        # Not exported at all.
        self.assertIsInstance(st.trace_vs_export(vs, 9, 0), st.Unsupported)

    def test_mul_and_add_stages(self):
        text = """/*    0.0 */       exec
/*    1   */          mul r1.xy__, r0.xyyy, c3.xyyy
/*    2   */          add r1.xy__, r1.xyyy, -c4.zwww
/*    3   */          tfetch2D r2, r1.xy, tf1
"""
        ins = st.parse(text)
        t = st.trace_ps_albedo(ins, st.texture_fetches(ins)[0])
        self.assertEqual(t["u"], {"input": "r0.x", "stages": [{"scale": "c3.x", "offset": None},
                                                              {"scale": None, "offset": "-c4.z"}]})

    def test_predicated_and_sat_unsupported(self):
        for line in ("mul_sat r1.xy__, r0.xyyy, c3.xyyy", "(p0) mul r1.xy__, r0.xyyy, c3.xyyy"):
            text = f"/*    0.0 */       exec\n/*    1   */          {line}\n/*    3   */          tfetch2D r2, r1.xy, tf1\n"
            ins = st.parse(text)
            self.assertIsInstance(st.trace_ps_albedo(ins, st.texture_fetches(ins)[0]), st.Unsupported, line)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s tests -p "test_shader_trace.py" -v`
Expected: ImportError (`shader_trace`).

- [ ] **Step 3: Implement `shader_trace.py`**

Implement the module to the interfaces and rules above. Structure:

```python
"""Trace albedo texture coordinates through SDK shader disassembly dumps.

Usage: shader_trace.py <dump_dir> <PS_HASH> [--vs <VS_HASH>]
"""
import argparse
import re
from collections import namedtuple
from pathlib import Path

Src = namedtuple("Src", "neg reg kind swizzle abs")      # kind: 'r', 'c', 'o'
Instr = namedtuple("Instr", "index op dest dest_kind mask srcs fetch_slot pred sat raw")
TFetch = namedtuple("TFetch", "index dim dest mask coord_reg coord_swz slot")
VFetch = namedtuple("VFetch", "ordinal index dest dest_swizzle fmt offset")
Trace = namedtuple("Trace", "base_reg base_comp stages")


class Unsupported:
    def __init__(self, reason):
        self.reason = reason

    def __repr__(self):
        return f"Unsupported({self.reason!r})"


_FMT = {"FMT_16_16": 25, "FMT_16_16_16_16": 26, "FMT_16_16_FLOAT": 31, "FMT_16_16_16_16_FLOAT": 32,
        "FMT_32_32_FLOAT": 37, "FMT_32_32_32_32_FLOAT": 38, "FMT_32_32_32_FLOAT": 57}
_LINE = re.compile(r"^/\*\s*(\d+)(?:\.\d+)?\s*\*/\s+(.*)$")
_CF = re.compile(r"^/\*\s*\d+\.\d+\s*\*/")
```

- `parse`: iterate lines; a control-flow line (`/*    0.0 */`) yields `Instr(op="cf:<word>")` (`exec`, `exece`, `alloc`, `cnop`, anything else such as `cexec`, `jmp`, `loop_start` is kept verbatim so tracing can refuse to cross it); an instruction line yields one `Instr`; a `+` continuation yields a scalar `Instr` with the previous index and `op="s:<op>"`. Parse the predicate prefix `(p0)`/`(!p0)` into `pred=True`, the `_sat` suffix into `sat=True`. Operands split on `, ` until the first `Name=` argument; `tf<N>`/`vf<N>` are fetch slots.
- `texture_fetches`: instructions whose op starts with `tfetch` (`tfetch1D/2D/3D/Cube`); `dim` = the suffix.
- `trace_component(instrs, before, reg, comp)`: scan `instrs` backward from the position of the instruction with index `before`; refuse (`Unsupported("control flow: <op>")`) on any `cf:` op other than `exec`, `exece`, `alloc`, `cnop`; for each instruction writing `reg` with `comp` in its mask: apply the producer rules, map `comp` through the source swizzle (`swizzle[ "xyzw".index(comp) ]`), and continue from that source; collect stages in reverse and return them in execution order. Constant refs are rendered as `"c8.y"` / `"-c8.y"` text.
- `trace_ps_albedo`: requires `dim == "2D"` and a 2-character coordinate swizzle; traces both components from the fetch's index; the base registers become `"r<N>.<comp>"` inputs.
- `vfetches`/`trace_vs_export`: find the last instruction exporting `o<interp>` with `comp` in its mask, trace its source to a register written by a vertex fetch, map through that fetch's destination swizzle to the source component; `fmt is None` for formats outside `_FMT` (then `Unsupported("format ...")` if traced to).
- CLI as in the Interfaces.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest discover -s tests -p "test_shader_trace.py" -v`
Expected: OK. Then the smoke check on a real dump: `python tools\xdk_sigmatch\shader_trace.py <dir with shader_*.ucode.frag> 00E09D1BC5295D52` prints three fetches, tf0 traced with stages through c8, tf8 unsupported (abs). If no dump directory exists yet, produce one with Task 7's discovery run and do this check there (say so in the report).

- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/shader_trace.py tests/test_shader_trace.py
git commit -m "Native textures: shader disassembly tracing for albedo UVs"
```

---

### Task 7: Discovery D1 — pixel shader microcode, PS hash and pixel constants

**Files:**
- Modify: `src/native/capture/xdk_layout.h`, `src/native/capture/capture.cpp`, `tests/native/test_xdk_layout.cpp`, `docs/native-renderer/frame-map.md` (section 8)

**Interfaces:**
- Produces (capture.cpp, anonymous namespace): `UcodeRef ReadPsUcode(uint32_t obj)` (same `UcodeRef` as the VS reader: `ok, phys, bytes, hash, dwords`), `bool ChoosePs(const DeviceSnapshot&, uint32_t* obj)` (device field `+0x3194`, else the last `GpuLoadShaders` r5 on this thread), `const PsInfo* LookupPs(uint32_t obj)` with `struct PsInfo { uint64_t hash; std::vector<uint32_t> tex_slots; const AlbedoSpec* albedo; bool no_albedo; }` (albedo lookup added in Task 10; this task fills `hash` and `tex_slots`), `std::atomic<uint32_t> g_ps_bank_ptr`, `bool ReadPsBankRegisters(uint32_t device, uint32_t reg, uint32_t count)` filling `thread_local float t_ps_bank[1024]`.
- xdk_layout.h constants with evidence: `kPsHeaderOffset`, `kPsRecordOffsetField`, `kPsUcodeBaseDword`, `kPsUcodeAddressDword`, `kPsUcodeSizeDword`, `kPsUcodeSizeShift` (or a statement that the pixel shader object uses the vertex shader constants), `kDevicePsConstantsOffset`.

Method (sub-project 3 did the same for vertex shaders, frame-map section 8 "Vertex shader microcode: three sources"):

- [ ] **Step 1: Disassemble the PS emit paths**

`python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 0x8221B300 120` (the IM_LOAD type-1 emit around `0x8221B354`, `ori r11,r11,1`) and `python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 0x82221978 200` (`GpuLoadShaders`, r5 pixel shader). Record which object dword holds the microcode base, where the record table sits, and how the size is encoded, with instruction addresses, in a new "Pixel shader microcode" subsection of frame-map section 8.

- [ ] **Step 2: Disassemble the pixel constant upload**

Find the `SetPending_AluConstants` call with `r5 = 0x4400` (pixel bank) in `DrawIndexedVertices` (`0x8221E0F0`) and `DrawVertices`: record the `r6` base (expected `device + 0x780 + 0x1000` if the banks are contiguous; confirm, do not assume) as `kDevicePsConstantsOffset`. In `UpdateState` (capture.cpp), next to the existing `g_vs_bank_ptr` store, store `ctx.r6.u32` into `g_ps_bank_ptr` when `ctx.r5.u32 == 0x4400`.

- [ ] **Step 3: Implement `ReadPsUcode`, `ChoosePs`, `LookupPs`, `ReadPsBankRegisters`**

Mirror `ReadVsUcode` / `LookupVs` / `ReadBankRegisters` (capture.cpp ~522, ~990, ~1041) with the PS constants. `LookupPs` caches per object in a `thread_local std::unordered_map<uint32_t, PsInfo>` (cleared above 4096 entries like `t_vs_cache`), re-reading when the record's phys/bytes change. `tex_slots = TextureFetchSlots(u.dwords.data(), u.dwords.size())`. Track the last `GpuLoadShaders` r5 in `OnGpuLoadShaders` (store `ps` in a `thread_local uint32_t t_gpu_ps`).

Add to `tests/native/test_xdk_layout.cpp` static checks of the new constants' values (the same way the VS constants are pinned there).

- [ ] **Step 4: Confirm the hash against the emulator**

Add `"ps_hash":"0x%016llX"` (0 when unknown) to the discovery draw rows (`WriteDrawRow`). Run a gameplay discovery capture with shader dumps:

```
.\tools\drive_game.ps1 -Total 120 -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump" -Env @{FABLE2_NATIVE_DISCOVERY="120"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="16"}
```

Acceptance: every nonzero `ps_hash` in the draw rows has a `shader_<HASH>.ucode.frag` in the dump folder (report counts: rows, distinct PS hashes, matched), and at least 95% of in-scene draw rows have a nonzero `ps_hash`. Write the counts and the capture name into the frame-map subsection. A `python` one-off for the count is fine (scratch, not committed).

- [ ] **Step 5: Build, test, commit**

`& .\tests\run_native_tests.cmd`, `python -m unittest discover -s tests -p "test_*.py"`, `& .\build.cmd -release fable_2`: all green.

```bash
git add src/native/capture/xdk_layout.h src/native/capture/capture.cpp tests/native/test_xdk_layout.cpp docs/native-renderer/frame-map.md
git commit -m "Native capture: pixel shader microcode, PS hash and pixel constants (D1)"
```

---

### Task 8: Discovery D3 capture, texture thumbnails and albedo finder

**Files:**
- Create: `tools/xdk_sigmatch/texture_thumb.py`, `tools/xdk_sigmatch/albedo_finder.py`, `tests/test_texture_thumb.py`, `tests/test_albedo_finder.py`
- Modify: `src/native/capture/capture.cpp`

**Interfaces:**
- Discovery draw rows gain `"tf":{"<slot>":["0x%08X" x6], ...}` for every slot in the draw's `PsInfo::tex_slots` (host-order dwords read from `device + 0x480 + 24*slot`, byte-swapped like the terrain read).
- Texture dumps: while discovery is armed, for each distinct `(base address, TextureIdentity)` of a 2D fetch constant in a draw row (at most 768 textures and 8 MB each per capture), write the raw guest bytes of the base level to `<exe folder>\logs\native_tex_<capture stamp>\<BASE>_<IDENTITY16>.bin` and one row `{"kind":"texture","file":"...","fc":[...6 dwords...]}` into the discovery JSONL. Extent: tiled = `texture_util::GetTiledAddressUpperBound2D(width_blocks, height_blocks, pitch_blocks, bpb_log2)` (SDK export; include `<rex/graphics/pipeline/texture/util.h>` in capture.cpp only), linear = `row_pitch * (height_blocks - 1) + width_blocks * bytes_per_block`. Unsupported formats are dumped too (the census needs them) but limited to 64 KB.
- `texture_thumb.py`: `tiled_offset_2d(x, y, pitch, bpb_log2)` (port of `TiledOffset2D`), `decode_rgba(data, fc) -> (w, h, bytes)` for DXT1/DXT2_3/DXT4_5/8888 base levels (other formats raise `ValueError("format N")`), `write_png(path, w, h, rgba, max_edge=256)` (nearest downscale; PNG via `zlib` + `struct`).
- `albedo_finder.py <capture.jsonl> --dumps <shader_dump_dir> --out <proposals.json> [--thumbs <dir>]`: for each pixel shader ordered by in-scene draw count, list texture fetches with `shader_trace` results, score candidates (2D +1, UVs traced to inputs +2, fetch writes ≥3 components +1, lowest interpolator +0.5), find the VS-side trace for the traced interpolators using the rows' `vs_hash`, write thumbnails `<thumbs>\<PS>_tf<slot>.png` from the texture dumps, and write proposals in the exact `ps-albedo.json` / `vs-transforms.json` `"uv"` shapes (Task 5) with `"proposed": true` and an `"evidence"` string naming the capture, draw count, score and thumbnail. Prints cumulative coverage (non-terrain in-scene draws) in draw-count order.

- [ ] **Step 1: Write the failing Python tests**

Create `tests/test_texture_thumb.py`:

```python
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import texture_thumb as tt  # noqa: E402


def fc_for(fmt, w, h, pitch, tiled, endian=1):
    return [2 | ((pitch >> 5) << 22) | (int(tiled) << 31), fmt | (endian << 6) | 0x1A000000,
            (w - 1) | ((h - 1) << 13), 0x688 << 1, 0, (1 << 9)]


class ThumbTest(unittest.TestCase):
    def test_tiled_offset_matches_cpp(self):
        # Values from capture::TiledOffset2D (tests/native/test_terrain_patch.cpp uses the same function).
        self.assertEqual(tt.tiled_offset_2d(0, 0, 32, 2), 0)
        self.assertEqual(tt.tiled_offset_2d(1, 0, 32, 2), 4)
        self.assertEqual(tt.tiled_offset_2d(0, 1, 32, 2), 16)
        self.assertEqual(tt.tiled_offset_2d(8, 0, 32, 2), 64)

    def test_dxt1_solid_red(self):
        # One 4x4 DXT1 block, linear, 8in16: color0 = color1 = red (0xF800), indices 0.
        block = struct.pack(">HHI", 0xF800, 0xF800, 0)  # big-endian words; 8in16 makes them LE
        data = block + bytes(256 - len(block))
        w, h, rgba = tt.decode_rgba(data, fc_for(18, 4, 4, 32, False))
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(rgba[0:4], bytes([255, 0, 0, 255]))

    def test_8888_tiled(self):
        pitch = 32
        size = max(tt.tiled_offset_2d(x, y, pitch, 2) for x in range(32) for y in range(32)) + 4
        buf = bytearray(size)
        for y in range(32):
            for x in range(32):
                o = tt.tiled_offset_2d(x, y, pitch, 2)
                buf[o:o + 4] = bytes([255, x, y, 7])  # 8in32 swaps to (7, y, x, 255)
        w, h, rgba = tt.decode_rgba(bytes(buf), fc_for(6, 32, 32, 32, True, endian=2))
        i = (5 * 32 + 3) * 4
        self.assertEqual(rgba[i:i + 4], bytes([7, 5, 3, 255]))

    def test_unsupported_format(self):
        with self.assertRaises(ValueError):
            tt.decode_rgba(bytes(64), fc_for(7, 4, 4, 32, False))

    def test_png(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.png"
            tt.write_png(p, 2, 1, bytes([1, 2, 3, 255, 4, 5, 6, 255]))
            raw = p.read_bytes()
            self.assertEqual(raw[:8], b"\x89PNG\r\n\x1a\n")
            self.assertIn(b"IDAT", raw)


if __name__ == "__main__":
    unittest.main()
```

Create `tests/test_albedo_finder.py` with a synthetic capture (two draw rows sharing one PS with a `tf` entry, one texture row pointing at a DXT1 block file written in a temp dir) and a temp dump dir holding `shader_<PS>.ucode.frag` = the `PS` text from `test_shader_trace.py` plus `shader_<VS>.ucode.vert` = the `VS` text; assert that the proposal for the PS has `"slot": 0`, `"u": {"input": "r0.x", ...}`, `"proposed": true`, that the VS proposal has `"uv"` keys `"o0.x"`/`"o0.y"`, and that the printed coverage line reports 2 of 2 draws.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s tests -p "test_texture_thumb.py" -v` and `... -p "test_albedo_finder.py" -v`
Expected: ImportError.

- [ ] **Step 3: Implement the Python tools**

DXT decode (standard): DXT1 block = color0, color1 (RGB565, little-endian after the endian swap), 32-bit index word (2 bits per texel, row-major from the LSB); `color0 > color1`: 4-color mode (c2 = (2c0+c1)/3, c3 = (c0+2c1)/3), else 3-color + transparent black. DXT2_3 = 8 bytes of explicit 4-bit alpha then a DXT1 color block (always 4-color). DXT4_5 = alpha0, alpha1, 48-bit 3-bit indices (8-alpha mode if alpha0 > alpha1, else 6-alpha + 0/255), then a DXT1 color block. Apply the fetch constant's endian swap per block before decoding (same rule as `CopySwapped`), and the tiled addressing on block coordinates with `pitch = pitch_texels // 4` for DXT.

- [ ] **Step 4: Add the capture-side `tf` fields and texture dumps** (capture.cpp `WriteDrawRow` and a new `DumpTexture(const uint32_t fc[6])` called from it; discovery only, never on the record path).

- [ ] **Step 5: Run tests and build**

`python -m unittest discover -s tests -p "test_*.py"`, `& .\tests\run_native_tests.cmd`, `& .\build.cmd -release fable_2`: green.

- [ ] **Step 6: Commit**

```bash
git add tools/xdk_sigmatch/texture_thumb.py tools/xdk_sigmatch/albedo_finder.py tests/test_texture_thumb.py tests/test_albedo_finder.py src/native/capture/capture.cpp
git commit -m "Native textures: discovery texture dumps, thumbnails and albedo finder (D3)"
```

---

### Task 9: Discovery runs, table entries and format census (D3, D4)

**Files:**
- Modify: `docs/native-renderer/ps-albedo.json`, `docs/native-renderer/vs-transforms.json`, generated `src/native/capture/ps_albedo_table.inc` and `src/native/capture/vs_transform_table.inc`, `docs/native-renderer/frame-map.md` (new section 11 "Albedo table")

- [ ] **Step 1: Bowerstone capture (autoplay)**

```
.\tools\drive_game.ps1 -Total 180 -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump" -Env @{FABLE2_NATIVE_DISCOVERY="300"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="8"}
python tools\xdk_sigmatch\albedo_finder.py out\build\win-amd64-release\logs\native_discovery_<stamp>.jsonl --dumps out\shader_dump --out out\albedo_proposals_bowerstone.json --thumbs out\albedo_thumbs
```

- [ ] **Step 2: Bower Lake capture (user)**

Autoplay loads the save where the user left it; Bower Lake needs the user to play there. Ask the user to run, from `out\build\win-amd64-release`:

```
$env:FABLE2_NATIVE_DISCOVERY="300"; $env:FABLE2_NATIVE_DISCOVERY_DELAY="5"; $env:FABLE2_NATIVE_DISCOVERY_EVERY="8"
& .\fable_2.exe --fullscreen=false --dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump
```

with the instruction: load the save, walk to Bower Lake, then press nothing special — discovery arms 5 s after start, so instead set `FABLE2_NATIVE_DISCOVERY_DELAY` to the number of seconds the user needs to reach the lake (ask them; e.g. 240). Clear the variables afterwards. Run `albedo_finder.py` on that capture too (`--out out\albedo_proposals_lake.json`).

- [ ] **Step 3: Confirm entries with thumbnails**

For pixel shaders in draw-count order until the cumulative coverage reaches 80% of non-terrain in-scene draws in each capture: open the candidate thumbnails (the Read tool shows PNGs), confirm the albedo (photographic colour; normal maps are blue-violet, masks greyscale, lookup ramps 1-pixel tall), and copy the proposal into `ps-albedo.json` / the VS entry's `"uv"` in `vs-transforms.json` with `"manual": true`, removing `"proposed"`, and an evidence string (capture, draws, score, thumbnail path, what the thumbnail shows). Shaders whose candidates are all unsupported or non-colour get `"no_albedo"` with the reason. Never add an entry without a thumbnail or a disassembly reason.

- [ ] **Step 4: Format census (D4)**

From both captures' texture rows, tabulate the formats of the confirmed albedo fetch constants (count of textures and draws per format) and the UV element formats of the confirmed VS entries. Write both tables into frame-map section 11. If a format outside DXT1/DXT2_3/DXT4_5/8888 carries more than 5% of textured draws, stop and report it (a decoder needs adding: a plan amendment); otherwise those draws will report `format-unsupported`.

- [ ] **Step 5: Generate tables, build, commit**

```
python tools\xdk_sigmatch\gen_albedo_table.py --json docs\native-renderer\ps-albedo.json --out src\native\capture\ps_albedo_table.inc
python tools\xdk_sigmatch\gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc
```

`python -m unittest discover -s tests -p "test_*.py"` and `& .\build.cmd -release fable_2`: green.

```bash
git add docs/native-renderer/ps-albedo.json docs/native-renderer/vs-transforms.json src/native/capture/ps_albedo_table.inc src/native/capture/vs_transform_table.inc docs/native-renderer/frame-map.md
git commit -m "Native textures: albedo table from gameplay discovery (D3, D4)"
```

---

### Task 10: Capture integration — materials in draw records

**Files:**
- Modify: `src/native/capture/capture.cpp`, `src/native/render/frame_scene.h` (material tallies), `tests/native/test_frame_scene.cpp`

**Interfaces:**
- Consumes: `AlbedoSpec`, `VsUvSpec`, `ComposeAxis`, `ResolveUvFetch`, `ForEachRef` (Task 3); `LookupPs`, `ReadPsBankRegisters` (Task 7); `ApplyUvEndian` (Task 2); the generated tables (Tasks 5, 9).
- Produces: `DrawRecord::material` filled for every recorded draw; `FrameScene` gains `uint32_t material[size_t(capture::MaterialStatus::kCount)]` (capture-side statuses of drawable records) and `std::vector<std::pair<uint64_t, uint32_t>> untextured_ps` (top 8 PS hashes with `ps-unknown`/`no-albedo`/`uv-unsupported`, by count); the `[native] capture:` log line gains `untextured by reason {...}` and a second line `untextured by ps {0x...: n, ...}` every 300 frames.

- [ ] **Step 1: Write the failing test** (frame_scene tallies)

In `tests/native/test_frame_scene.cpp` add: a `FrameBuilder` given three drawable records with material statuses `kTextured`, `kPsUnknown` (ps_hash 0xA), `kPsUnknown` (ps_hash 0xA) and one skipped record (`kNoTransform`, status `kPsUnknown`, ps 0xB) publishes `material[kTextured] == 1`, `material[kPsUnknown] == 2` (skipped records are not counted) and `untextured_ps == {{0xA, 2}}`. Use the builder API exactly as the existing tests in that file do.

- [ ] **Step 2: Run to verify it fails**, then implement the tallies in `FrameBuilder` (counted when a record with `skip == kNone` is added).

- [ ] **Step 3: Tables and lookups in capture.cpp**

```cpp
const std::vector<AlbedoSpec> kAlbedoTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1) \
  {H, int8_t(SL), UI, UC, VI, VC, {{US0, UO0}, {US1, UO1}}, {{VS0, VO0}, {VS1, VO1}}},
#define FABLE2_PS_NO_ALBEDO(H) {H, int8_t(-1), 0, 0, 0, 0, {}, {}},
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
};
const std::vector<VsUvSpec> kVsUvTable = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, C, R0, R1, R2)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1) \
  {H, I, C, int8_t(F), S, FM, O, {{S0, O0}, {S1, O1}}},
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
};
```

(Both tables may be empty, hence `std::vector` rather than C arrays.) `FindAlbedo(uint64_t ps_hash)` and `FindVsUv(uint64_t vs_hash, uint8_t interp, uint8_t comp)` are linear scans, cached in `PsInfo::albedo` / per-VS (`VsInfo` gains `const VsUvSpec* uv[16][4]` filled in `FillShader`, and `std::vector<VertexFetch> fetches` kept for `ResolveUvFetch`).

- [ ] **Step 4: `FillMaterial`**

Called from `RecordDraw` only when records are built for a consumer (the existing `g_records_frame` gate), after `FillDrawInputs` / `FillTerrainInputs`:

```cpp
// Albedo texture and UVs for a recorded draw (material.h). Never changes the
// draw's skip reason.
void FillMaterial(uint32_t device, const DeviceSnapshot& dev, const VsInfo* vs, DrawInputs& in) {
  Material& m = in.material;
  if (in.terrain_shader) { m.status = MaterialStatus::kTerrain; return; }
  uint32_t ps_obj = 0;
  const PsInfo* ps = ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  if (!ps || !ps->albedo) { m.status = MaterialStatus::kPsUnknown; if (ps) m.ps_hash = ps->hash; return; }
  m.ps_hash = ps->hash;
  const AlbedoSpec& a = *ps->albedo;
  if (a.slot < 0) { m.status = MaterialStatus::kNoAlbedo; return; }
  const VsUvSpec* u = vs ? vs->uv[a.u_interp][a.u_comp] : nullptr;
  const VsUvSpec* v = vs ? vs->uv[a.v_interp][a.v_comp] : nullptr;
  UvLayout uv;
  StreamView sv;
  if (!u || !v || !ResolveUvFetch(vs->fetches, *u, *v, &uv) || ResolveStream(dev, uv.fetch_slot, &sv) ||
      !sv.fc_match || !ApplyUvEndian(&uv, sv.fc1 & 3)) {
    m.status = MaterialStatus::kUvUnsupported;
    return;
  }
  // Constants named by the four stage lists, then the albedo fetch constant.
  bool ok = true;
  auto need = [&](int32_t ref) {
    const uint32_t reg = uint32_t(ref & 0x3FF) / 4;
    ok = ok && (((ref >> 10) & 1) ? ReadPsBankRegisters(device, reg, 1) : ReadBankRegisters(device, reg, 1));
  };
  ForEachRef(u->stages, 2, need);
  ForEachRef(v->stages, 2, need);
  ForEachRef(a.u_stages, 2, need);
  ForEachRef(a.v_stages, 2, need);
  const uint8_t* fc = ReadVirtual(device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * uint32_t(a.slot), 24);
  if (!ok || !fc) { m.status = MaterialStatus::kUvUnsupported; return; }
  for (int i = 0; i < 6; ++i) m.fetch[i] = LoadBe32(fc + 4 * i);
  ComposeAxis(u->stages, a.u_stages, t_bank, t_ps_bank, &m.uv_xform[0], &m.uv_xform[2]);
  ComposeAxis(v->stages, a.v_stages, t_bank, t_ps_bank, &m.uv_xform[1], &m.uv_xform[3]);
  m.uv = uv;
  m.uv_vb = {sv.base + sv.offset, sv.size - sv.offset};
  m.status = MaterialStatus::kTextured;
}
```

Note: `ReadBankRegisters` converts into `t_bank` at the register's slot without clearing the transform rows already read (it writes only `[reg, reg+count)`), so reading the transform first and the UV constants after is safe; `in.bank` keeps pointing at `t_bank`. `FillDrawInputs` must expose the `DeviceSnapshot` and `VsInfo*` it used (return them through two out-parameters) so `FillMaterial` does not read the device twice.

- [ ] **Step 5: Log lines**

In the 300-frame capture log (`EndMainSceneFrame` / `LogShaderBreakdown`), print `untextured by reason {ps-unknown: n, ...}` (all non-`textured` capture statuses) and `untextured by ps {0x...: n, ...}`.

- [ ] **Step 6: Verify**

`& .\tests\run_native_tests.cmd`, `& .\build.cmd -release fable_2`, then a gameplay run:

```
.\tools\drive_game.ps1 -Total 120 -GameArgs "--fable2_native_render=true","--fable2_native_view=split"
```

Expected in the game log: `untextured by reason` lines; with the Task 9 table, `textured` (capture side) at least 80% of drawable non-terrain records in Bowerstone. Record the numbers in the report. Also check the view-off cost is unchanged: a 120 s run with `--fable2_native_view=off` reports `capture` median under 0.5 ms (records are not built with the view off, so `FillMaterial` must not run).

- [ ] **Step 7: Commit**

```bash
git add src/native/capture/capture.cpp src/native/render/frame_scene.h tests/native/test_frame_scene.cpp
git commit -m "Native capture: albedo materials in draw records"
```

---

### Task 11: Texture cache (GPU)

**Files:**
- Create: `src/native/render/texture_cache.h`, `src/native/render/texture_cache.cpp`

**Interfaces:**
- Consumes: `TextureFetch`, `DecodeTextureFetch`, `UntileLevel`, `LevelLayout`, `LevelCount`, `LevelExtent`, `UploadRowPitch`, `SamplerIndex`, `IsMirror`, `TextureIdentity` (Task 1); `SampleHash`, `TextureState`, `NoteSample`, `UploadBudget` (Task 4); `GeometryCacheIndex`, `GeoKey`, `RetirePool` (geometry_cache_index.h); SDK `rex::graphics::texture_util::GetGuestTextureLayout`, `GetPackedMipOffset`, `GetTiledAddressUpperBound2D`; `capture::ReadPhysical`.
- Produces:

```cpp
struct TextureStats {
  uint32_t textured = 0, resident = 0, uploads = 0, mirror = 0, base_only = 0;
  uint64_t resident_bytes = 0, upload_bytes = 0;
  double decode_ms = 0;
  uint32_t status[size_t(capture::MaterialStatus::kCount)] = {};  // final statuses of drawn records
};

class TextureCache {
 public:
  explicit TextureCache(uint64_t budget_bytes);
  void BeginFrame(uint64_t frame, uint64_t budget_bytes, uint64_t upload_bytes_per_frame);
  // Resolves (uploading if needed and allowed) the albedo of `m`. Returns the
  // view and sampler index, or nullptr with *status set (format-unsupported,
  // texture-pending, texture-dynamic, texture-bad). Records copy commands and
  // barriers on `cmd`: call before render targets are bound.
  nrhi::TextureView* Resolve(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::Material& m,
                             uint32_t* sampler, float* uv_fix, capture::MaterialStatus* status, TextureStats& st);
  void Release(nrhi::Device* dev);
  bool latched() const { return latched_; }
};
```

`uv_fix` returns (w / host_w, h / host_h) for BC textures whose base size was rounded up to a multiple of 4 (the renderer multiplies the UV scale and offset by it).

- [ ] **Step 1: Implement `Resolve`**

Order of checks and actions (each failure sets the status and returns nullptr):
1. `latched_` → `kTextureBad`.
2. `DecodeTextureFetch(m.fetch, &t)`: `kFormat`/`kNot2D` → `kFormatUnsupported`; other errors → `kTextureBad`.
3. Layout: `texture_util::GetGuestTextureLayout(xenos::DataDimension::k2DOrStacked, t.pitch_texels >> 5, t.width, t.height, 1, t.tiled, xenos::TextureFormat(t.xenos_format), t.packed_mips, true, LevelCount(t) - 1)`. Level `l` source: `l == 0` → base address, `layout.base`; `l >= 1` → mip address + `layout.mip_offsets_bytes[l]`, `layout.mips[l]`. For `l >= layout.packed_level` (when not `UINT32_MAX`): the level lives in the packed tail at the packed level's storage, at block offset `GetPackedMipOffset(t.width, t.height, 1, format, l, x, y, z)`; use `layout.mips[packed]`/`mip_offsets_bytes[packed]` (or `layout.base` when the packed level is 0). Pitch in blocks for tiled addressing = `row_pitch_bytes >> bpb_log2`. Guest extent per level: tiled `GetTiledAddressUpperBound2D(x_extent_blocks, y_extent_blocks, pitch_blocks, bpb_log2)`, linear `level_data_extent_bytes`. If any level's layout fails (extent 0), upload only the base level (`st.base_only++`).
4. Key `GeoKey{t.base_phys, extent0, t.xenos_format | (t.width << 8), uint32_t(TextureIdentity(m.fetch)), 2}`. Read guest bytes with `capture::ReadPhysical(addr, extent)`; null → `kTextureBad`.
5. Sample hash + `NoteSample` on the per-key `TextureState` (`std::unordered_map<GeoKey, TextureState, GeoKeyHash>`): if `dynamic` → `kTextureDynamic`. If resident and unchanged → `index_.Lookup` hit, return the view.
6. Changed or new: `upload_.TryTake(sum of guest extents)` false → `kTexturePending`. Decode: for each level, host size = `LevelExtent(host_base, l)` where `host_base` = width/height rounded up to the block size; `width_blocks = min(guest blocks, host blocks)`; untile into a zeroed staging buffer (`HeapKind::kUpload`, `BufferBindClass::kCopySrc`) at 512-byte aligned offsets with `UploadRowPitch(host blocks, bytes_per_block)` rows. `UntileLevel` false → `kTextureBad`.
7. Create (or reuse: same size/format/mips, retired and completed — `RetirePool<nrhi::Texture*>`) the texture `{k2D, host_w, host_h, mip_levels = levels, format, initial_state = kCopyDest}`; on a reused texture, barrier `kPixelShaderResource -> kCopyDest`. `cmd->CopyBufferToTexture(tex, l, 0, staging, offset_l, row_pitch_l, host_blocks_w * block, host_blocks_h * block, 1)` per level; barrier `kCopyDest -> kPixelShaderResource`; `dev->DestroyDeferred(staging)`.
8. View: `CreateTextureView(tex, {k2D, kUnknown, 0, ~0u, swizzle mapped from t.swizzle (0-3 -> kX..kW, 4 -> kZero, 5 -> kOne)})`.
9. `index_.Insert` (evicted ids: destroy their view and retire their texture with `dev->CurrentSubmission()`), store `Entry{tex, view, bytes, host dims}`.
10. Format map: `kDxt1 -> kBC1_UNORM`, `kDxt23 -> kBC2_UNORM`, `kDxt45 -> kBC3_UNORM`, `k8888 -> kR8G8B8A8_UNORM`.
11. Any RHI creation failure (texture, buffer, view, map) → log once `[native] textures: <what> failed, texture path off`, set `latched_`, return `kTextureBad`.
12. `*sampler = SamplerIndex(t)`; `if (IsMirror(t.clamp_x) || IsMirror(t.clamp_y)) ++st.mirror`.

- [ ] **Step 2: Build**

`& .\build.cmd -release fable_2` exit 0 (the class is not used yet; the file compiles through the recursive glob).

- [ ] **Step 3: Commit**

```bash
git add src/native/render/texture_cache.h src/native/render/texture_cache.cpp
git commit -m "Native textures: GPU texture cache (untile, upload, LRU, latch)"
```

---

### Task 12: Textured clay pass, UV buffers, F3 line and cvars

**Files:**
- Modify: `src/native/render/geometry_cache.{h,cpp}`, `src/native/render/clay_logic.h`, `src/native/render/clay_pass.{h,cpp}`, `src/native/fable2_native_shaders.h`, `src/native/fable2_native_render.cpp`, `tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`, `README.md` (native renderer settings list)

**Interfaces:**
- `ClayConstants` grows to 28 dwords: after `color` add `float uv[4]; uint32_t textured; uint32_t sampler; uint32_t pad[2];` (`static_assert(sizeof(ClayConstants) == 112)`); `MakeClayConstants(r, vertex_count, color, const float uv[4], bool textured, uint32_t sampler)`.
- `GeoKey UvKey(const capture::DrawRecord&)` (kind 3: `addr = material.uv_vb.phys_addr`, `size`, `stride = uv.stride_bytes`, `extra = HashCombine32({offset, format, comp_u, comp_v, swap16, normalized, signed, exp_adjust})`).
- `GeometryCache::Uvs(dev, r, vertex_count, ClayStats&) -> nrhi::Buffer*` (float2 buffer, decoded with `DecodeUvs` over the same vertex range as positions; content hash = the frame hash of the UV range).
- `FormatTextureText(const TextureStats&, uint32_t drawn) -> std::string`: `Textures: textured T of D drawn (S%), resident N (M MB), uploads U (X MB), decode Y ms | top untextured: a n, b n, c n` (top three final non-`textured` statuses).
- Cvars as in Global Constraints.

- [ ] **Step 1: Write the failing tests**

In `tests/native/test_clay_logic.cpp` add: `sizeof(ClayConstants) == 112`; `MakeClayConstants` copies `uv` and sets `textured`/`sampler`; `UvKey` differs when `comp_u` or `swap16` differs and equals for identical layouts; `FormatTextureText` with `textured 80, drawn 100, status[kPsUnknown] = 15, status[kTexturePending] = 5` contains `"textured 80 of 100 drawn (80%)"` and `"top untextured: ps-unknown 15, texture-pending 5"`.

In `tests/native/test_native_shaders.cpp` (it compiles the HLSL with the test's existing method) add the new `kClayTexturedPs` and updated `kClayVs` to the compiled set.

- [ ] **Step 2: Run to verify they fail**, then implement.

- [ ] **Step 3: Shaders**

`kClayVs` cbuffer gains `float4 uv_xform; uint textured; uint sampler_index; uint2 pad;`, adds `StructuredBuffer<float2> uvs : register(t2);`, and outputs `float2 uv : TEXCOORD1 = textured ? uvs[v] * uv_xform.xy + uv_xform.zw : 0` (guard `v < vertex_count` as for positions). `kClayPs` keeps the flat path; new `kClayTexturedPs`:

```hlsl
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured; uint sampler_index; uint2 pad; };
Texture2D albedo : register(t3);
SamplerState s_point_wrap : register(s0);
SamplerState s_linear_wrap : register(s1);
SamplerState s_point_clamp : register(s2);
SamplerState s_linear_clamp : register(s3);
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float facet = saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base;
  if (textured != 0) {
    float4 t;
    if (sampler_index == 0) t = albedo.Sample(s_point_wrap, uv);
    else if (sampler_index == 1) t = albedo.Sample(s_linear_wrap, uv);
    else if (sampler_index == 2) t = albedo.Sample(s_point_clamp, uv);
    else t = albedo.Sample(s_linear_clamp, uv);
    return float4(t.rgb * (0.75 + 0.25 * facet), 1.0);
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * (0.35 + 0.65 * facet), 1.0);
}
```

(Sampler register order matches `SamplerIndex`: bit 0 linear, bit 1 clamp.)

- [ ] **Step 4: Clay pass**

Binding layout: params `[0]` constants b0 count 28, `[1]` SRV t0 (positions), `[2]` SRV t1 (indices), `[3]` SRV t2 (uvs), `[4]` texture table t3 count 1; four static samplers s0-s3 (`kPoint/kWrap`, `kLinear/kWrap`, `kPoint/kClamp`, `kLinear/kClamp`). A 1x1 white RGBA8 texture + view created in `Ensure` is bound for untextured draws; the positions buffer is bound to t2 for untextured draws. The pixel shader is `kClayTexturedPs` when `fable2_native_textures` is true at `Ensure`, else `kClayPs` (rebuild the pipeline when the cvar changes; read it once per frame in `PollFrame`/the render callback and pass it to `Render`).

`Render`: before binding render targets, loop the scene's draws and call `textures_.Resolve` for drawn records whose capture status is `kTextured` (store view, sampler, uv fix per draw in a reused vector); count final statuses into `TextureStats::status` (capture statuses pass through for non-textured records). Then the existing draw loop, with `Uvs(...)` for textured draws (a failed UV decode makes the draw untextured with `kUvUnsupported`), constants with `uv_xform` multiplied by the uv fix (scale and offset components), `SetTexture(4, view)`.

`TextureCache` lives in `ClayPass` next to `GeometryCache`; `BeginFrame` is called with the cvars' budgets (MB to bytes) where the geometry cache's is.

- [ ] **Step 5: F3 and log**

`FormatStatusText` output gains a fourth line from `FormatTextureText`; the `[native] clay:` 300-frame log line gains `textured T of D`.

- [ ] **Step 6: Verify**

`& .\tests\run_native_tests.cmd`, `python -m unittest discover -s tests -p "test_*.py"`, `& .\build.cmd -release fable_2`: green. Gameplay:

```
.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"
.\tools\drive_game.ps1 -Total 120 -Shots "95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split","--fable2_native_textures=false"
```

Expected: shot 95 of the first run shows textured buildings/characters in the right (clay) half matching the left; the second run's clay half is flat clay as in sub-project 3; 30 fps in both (`[frame] guest` lines); F3/log `textured` share at least 80% of drawn non-terrain draws. Describe both screenshots in the report. Review Focus 3 check: the log shows uploads spread over several frames after loading, no frame stuck pending beyond 5 s.

- [ ] **Step 7: Commit**

```bash
git add src/native/render/geometry_cache.h src/native/render/geometry_cache.cpp src/native/render/clay_logic.h src/native/render/clay_pass.h src/native/render/clay_pass.cpp src/native/fable2_native_shaders.h src/native/fable2_native_render.cpp tests/native/test_clay_logic.cpp tests/native/test_native_shaders.cpp README.md
git commit -m "Native textures: textured clay pass, UV buffers, F3 line and cvars"
```

---

### Task 13: Validation and documentation

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (section 11 validation), `docs/superpowers/specs/2026-10-02-native-renderer-textures-design.md` (status line and, if any, an "Implementation notes (deviations)" section)

- [ ] **Step 1: Cost A/B (agent, autoplay)**

Three 120 s runs: `--fable2_native_render=false`; `--fable2_native_render=true --fable2_native_view=off`; `--fable2_native_render=true --fable2_native_view=split`. Report guest work medians and the `capture` medians; criterion: view-off capture under 0.5 ms.

- [ ] **Step 2: User checks** (ask the user; from `out\build\win-amd64-release`)

1. `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=split`, F6 through overlay/native at town, an open field and an interior: textures match the emulated image (same image, orientation, scale; no tiling garbage).
2. F3 `Textures:` line at Bowerstone and at Bower Lake: textured share at least 80% of drawn non-terrain draws.
3. 10 minutes including an area transition: no crash, no `[native] textures: ... texture path off` latch in the log, resident MB within the budget.

- [ ] **Step 3: Record and commit**

Write the results (numbers, logs, screenshots described, remaining untextured reasons by count and next action) into frame-map section 11; set the spec status line to `Status: implemented (sub-project 4), validation results in docs/native-renderer/frame-map.md section 11.`; list deviations (at least: tracing in Python over disassembly dumps).

```bash
git add docs/native-renderer/frame-map.md docs/superpowers/specs/2026-10-02-native-renderer-textures-design.md
git commit -m "Albedo textures validation results"
```
