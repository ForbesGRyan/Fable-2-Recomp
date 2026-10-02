# Native Renderer Clay Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture Fable 2's main-scene draw calls once per guest call and render them natively as untextured clay geometry in a debug view that lines up with the emulated frame.

**Architecture:** Strong overrides of the 14 XDK draw-packet builders and the binding functions feed a game-thread `FrameBuilder` (one `DrawRecord` per guest call inside the main-scene bracket); an immutable `FrameScene` is published at the XDK Swap hook. On the GPU thread, the existing post-processor callback decodes guest vertex/index data into a geometry cache, draws a 1120x720 clay pass with vertex pulling, and composites it over the emulated output in a debug view. Four discovery tasks find the guest object layouts, the main-scene flag and the transform constants first.

**Tech Stack:** C++23 (clang-cl via CMake/Ninja), ReXGlue SDK (`thirdparty/rexglue-sdk`, branch `renderer`), nrhi D3D12 backend, HLSL (runtime-compiled by the RHI), Python 3 (unittest) for tools.

**Spec:** `docs/superpowers/specs/2026-10-01-native-renderer-clay-pass-design.md`

## Global Constraints

- Environment: put `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin` FIRST on PATH, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin`.
- SDK build: always `tools\build_runtime_sdk.cmd C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` (never without the argument). Game build: `build.cmd -release fable_2`. Run `.cmd` scripts from PowerShell (`& .\build.cmd ...`). No `fable_2.exe` may be running while building.
- Python tests: `python -m unittest discover -s tests -p "<file>.py" -v` (never `python -m unittest tests.<mod>`). Native tests: `& .\tests\run_native_tests.cmd` (add new tests before its final `exit /b 0`).
- `thirdparty/rexglue-sdk/src/system/rexruntime.def`: never add `;` comment lines. New exports need MSVC-decorated names.
- Files in the SDK and several game files use CRLF line endings in the working copy; preserve the existing line endings of any file you edit.
- Git: stage files explicitly; never stage `fable_2_manifest.toml`; never `git add -A` in the parent repo; never push; never stash/reset/checkout other branches. Every commit message ends with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. SDK commits go to the submodule (branch `renderer`) first, then the parent commit bumps the submodule.
- Main scene: pitch 1120, 1120x720 (frame-map section 3b). Native target is exactly 1120x720; it is stretched to the guest output size in the composite.
- RHI limits (`native_rhi.h`): index buffers are 16-bit only and there is no buffer-to-buffer copy. Ruling: the clay pass uses vertex pulling from `StructuredBuffer`s in upload-heap buffers, and all primitive types are converted to uint32 triangle lists on the CPU (this replaces the spec's "triangle list and strip map directly (strip cut enabled)"; rect lists are a skip reason).
- New cvars (category "Fable2"): `fable2_native_view` (string: off|overlay|split|native|pattern, default "off"), `fable2_native_clay_color` (string: clay|draw|shader, default "clay"), `fable2_native_geometry_budget_mb` (int32, default 256). `fable2_native_render_mode` and `fable2_native_render_active` are removed; `fable2_native_render` (startup enable) stays.
- Discovery env vars: `FABLE2_NATIVE_DISCOVERY=<frames>`, `FABLE2_NATIVE_DISCOVERY_EVERY=<n>` (default 64); output `<exe folder>\logs\native_discovery_<YYYYMMDD_HHMMSS>.jsonl`.
- The emulated frame is never modified by the native path in any view mode; with `fable2_native_view=off` nothing is drawn.
- Unit-tested headers marked "pure" must not include SDK, Windows or GPU headers.

## Review Focus

1. Garbage or unmapped guest addresses in buffer/shader objects: decoders and converters must reject out-of-range sizes without reading past `src_size`, and the renderer must skip the draw (`kBadMemory`), never crash. Pinned by tests in Tasks 1, 2 and 4.
2. Indices that reference past the end of the vertex buffer: the draw is skipped with `kBadIndex`. Pinned in Task 2 (max index reported) and Task 4 (assembler rejects).
3. A vertex shader with no vertex fetch, or whose first fetch is a mini fetch or has zero stride: no position element is reported and the draw is skipped (`kUnknownPosFormat`). Pinned in Task 3.
4. A geometry budget smaller than one frame's working set: entries used in the current frame are never evicted; the cache goes over budget and reports it. Pinned in Task 5.
5. An unknown `fable2_native_view` string: treated as `off` with one warning. Pinned in Task 13.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/native/capture/position_decode.h` (pure) | Big-endian position decoding | 1 |
| `src/native/capture/index_convert.h` (pure) | Index swap + primitive -> triangle list | 2 |
| `src/native/capture/vfetch_decode.h` (pure) | Xenos microcode vertex-fetch decoding | 3 |
| `src/native/capture/draw_record.h` (pure) | `DrawRecord`, `SkipReason`, `TransformLayout`, `AssembleRecord` | 4 |
| `src/native/render/frame_scene.h` (pure) | `FrameScene`, `FrameBuilder`, `ScenePublisher` | 4 |
| `src/native/render/geometry_cache_index.h` (pure) | LRU/budget bookkeeping for the geometry cache | 5 |
| `tools/xdk_sigmatch/matrix_finder.py`, `tools/xdk_sigmatch/gen_transform_table.py` | Transform discovery + table generation | 6 |
| `src/native/capture/xdk_dispatch.h`, `src/native/capture/xdk_hooks.inc` (generated) | Shared strong overrides: capture + census observers | 7 |
| `src/native/capture/guest_read.h` | Bounds-checked guest reads | 8 |
| `src/native/capture/capture.{h,cpp}` | DrawState, hook handlers, discovery writer, swap publish | 8, 9, 10, 11 |
| `src/native/capture/xdk_layout.h` | Discovered guest offsets (D1-D3) | 9, 10 |
| `src/native/capture/vs_transform_table.inc` (generated) | D4 result | 11 |
| `src/native/render/geometry_cache.{h,cpp}` | GPU buffers for decoded geometry | 12 |
| `src/native/render/clay_pass.{h,cpp}` | Clay targets, pipeline, draw loop | 12 |
| `src/native/render/composite.{h,cpp}` | Debug view composition | 13 |
| `src/native/native_render_state.h` | `View` parsing/cycling (replaces `Mode`) | 13 |
| `src/native/fable2_native_shaders.h` | Clay + composite HLSL | 12, 13 |
| `src/native/fable2_native_render.{h,cpp}` | Install, F6, callbacks | 13 |

CMake: `CMakeLists.txt:76` globs only `src/native/*.cpp`. Task 8 changes it to `file(GLOB_RECURSE FABLE2_NATIVE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/native/*.cpp)` and adds `${CMAKE_CURRENT_SOURCE_DIR}/src/native/capture` and `${CMAKE_CURRENT_SOURCE_DIR}/src/native/render` to the include directories next to `src/native` (line ~100).

---

### Task 1: Position decoding (pure)

**Files:**
- Create: `src/native/capture/position_decode.h`
- Test: `tests/native/test_position_decode.cpp`; Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces:
  ```cpp
  namespace fable2::native::capture {
  enum class PosFormat : uint8_t { kUnknown, kFloat3, kFloat4, kHalf4, kShort4 };
  struct PosLayout {
    PosFormat format = PosFormat::kUnknown;
    bool is_signed = true;       // kShort4 only
    bool normalized = true;      // kShort4 only
    int exp_adjust = 0;          // value *= 2^exp_adjust (kShort4 only)
    uint32_t offset_bytes = 0;   // within one vertex
    uint32_t stride_bytes = 0;
    uint32_t fetch_slot = 0;     // vertex fetch constant index [0,95]
  };
  struct Float4 { float x, y, z, w; };
  PosFormat PosFormatFromXenos(uint32_t xenos_vertex_format);
  float HalfToFloat(uint16_t h);
  uint32_t PositionBytes(PosFormat f);  // bytes read per vertex, 0 for unknown
  bool DecodePositions(const uint8_t* src, size_t src_size, const PosLayout& layout,
                       uint32_t first_vertex, uint32_t count, Float4* out);
  }
  ```

- [ ] **Step 1: Write the failing test**

`tests/native/test_position_decode.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/position_decode.h"
#include <cmath>
#include <cstring>
#include <iostream>

using namespace fable2::native::capture;

static void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = uint8_t(v); }
static void PutBe16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = uint8_t(v); }
static uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main() {
  if (PosFormatFromXenos(57) != PosFormat::kFloat3) return 1;
  if (PosFormatFromXenos(38) != PosFormat::kFloat4) return 2;
  if (PosFormatFromXenos(32) != PosFormat::kHalf4) return 3;
  if (PosFormatFromXenos(26) != PosFormat::kShort4) return 4;
  if (PosFormatFromXenos(6) != PosFormat::kUnknown) return 5;

  if (HalfToFloat(0x3C00) != 1.0f || HalfToFloat(0xC000) != -2.0f) return 6;
  if (HalfToFloat(0x0001) <= 0.0f || HalfToFloat(0x0001) > 1e-7f) return 7;  // denormal
  if (!std::isinf(HalfToFloat(0x7C00))) return 8;

  // float3, stride 16, offset 4: vertex1 = (1,2,3)
  uint8_t vb[64] = {};
  PutBe32(vb + 16 + 4, Bits(1.0f)); PutBe32(vb + 16 + 8, Bits(2.0f)); PutBe32(vb + 16 + 12, Bits(3.0f));
  PosLayout f3; f3.format = PosFormat::kFloat3; f3.offset_bytes = 4; f3.stride_bytes = 16;
  Float4 out[2];
  if (!DecodePositions(vb, sizeof(vb), f3, 1, 1, out)) return 9;
  if (out[0].x != 1.0f || out[0].y != 2.0f || out[0].z != 3.0f || out[0].w != 1.0f) return 10;
  // Reading past src_size fails: vertex 3 needs bytes 52..64 (ok), vertex 4 needs 68 (fail).
  if (!DecodePositions(vb, sizeof(vb), f3, 3, 1, out)) return 11;
  if (DecodePositions(vb, sizeof(vb), f3, 3, 2, out)) return 12;
  // NaN passes through unchanged (renderer culls).
  PutBe32(vb + 4, 0x7FC00000);
  if (!DecodePositions(vb, sizeof(vb), f3, 0, 1, out) || !std::isnan(out[0].x)) return 13;

  // short4 signed normalized: -32768 clamps to -1, 32767 -> 1, exp_adjust 1 doubles.
  uint8_t sb[8];
  PutBe16(sb + 0, 0x8000); PutBe16(sb + 2, 0x7FFF); PutBe16(sb + 4, 0); PutBe16(sb + 6, 0x7FFF);
  PosLayout s4; s4.format = PosFormat::kShort4; s4.stride_bytes = 8;
  if (!DecodePositions(sb, sizeof(sb), s4, 0, 1, out)) return 14;
  if (out[0].x != -1.0f || out[0].y != 1.0f || out[0].z != 0.0f || out[0].w != 1.0f) return 15;
  s4.normalized = false; s4.exp_adjust = 1;
  if (!DecodePositions(sb, sizeof(sb), s4, 0, 1, out)) return 16;
  if (out[0].x != -65536.0f || out[0].y != 65534.0f) return 17;

  // half4
  uint8_t hb[8];
  PutBe16(hb + 0, 0x3C00); PutBe16(hb + 2, 0x4000); PutBe16(hb + 4, 0xC000); PutBe16(hb + 6, 0x3C00);
  PosLayout h4; h4.format = PosFormat::kHalf4; h4.stride_bytes = 8;
  if (!DecodePositions(hb, sizeof(hb), h4, 0, 1, out)) return 18;
  if (out[0].x != 1.0f || out[0].y != 2.0f || out[0].z != -2.0f || out[0].w != 1.0f) return 19;

  // Unknown format and zero stride are rejected.
  PosLayout bad; bad.stride_bytes = 16;
  if (DecodePositions(vb, sizeof(vb), bad, 0, 1, out)) return 20;
  f3.stride_bytes = 0;
  if (DecodePositions(vb, sizeof(vb), f3, 0, 1, out)) return 21;
  std::cout << "PASS: position decode\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```bat
clang++ -std=c++23 "%~dp0native\test_position_decode.cpp" -o "%OUT%\position_decode.exe" || exit /b 1
"%OUT%\position_decode.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: FAIL compiling `test_position_decode.cpp` ("file not found" for `position_decode.h`).

- [ ] **Step 3: Write the implementation**

`src/native/capture/position_decode.h`:
```cpp
#pragma once

// Big-endian vertex position decoding for the clay pass (pure: no SDK/GPU deps).
// Xenos vertex formats: xenos::VertexFormat in thirdparty/rexglue-sdk/include/rex/graphics/xenos.h.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace fable2::native::capture {

enum class PosFormat : uint8_t { kUnknown, kFloat3, kFloat4, kHalf4, kShort4 };

struct PosLayout {
  PosFormat format = PosFormat::kUnknown;
  bool is_signed = true;       // kShort4 only
  bool normalized = true;      // kShort4 only
  int exp_adjust = 0;          // value *= 2^exp_adjust (kShort4 only)
  uint32_t offset_bytes = 0;   // within one vertex
  uint32_t stride_bytes = 0;
  uint32_t fetch_slot = 0;     // vertex fetch constant index [0,95]
};

struct Float4 {
  float x, y, z, w;
};

inline PosFormat PosFormatFromXenos(uint32_t xenos_vertex_format) {
  switch (xenos_vertex_format) {
    case 57: return PosFormat::kFloat3;  // k_32_32_32_FLOAT
    case 38: return PosFormat::kFloat4;  // k_32_32_32_32_FLOAT
    case 32: return PosFormat::kHalf4;   // k_16_16_16_16_FLOAT
    case 26: return PosFormat::kShort4;  // k_16_16_16_16
    default: return PosFormat::kUnknown;
  }
}

inline uint32_t PositionBytes(PosFormat f) {
  switch (f) {
    case PosFormat::kFloat3: return 12;
    case PosFormat::kFloat4: return 16;
    case PosFormat::kHalf4:
    case PosFormat::kShort4: return 8;
    default: return 0;
  }
}

inline float HalfToFloat(uint16_t h) {
  const uint32_t sign = uint32_t(h >> 15) << 31;
  const uint32_t exp = (h >> 10) & 0x1F;
  const uint32_t man = h & 0x3FF;
  float f;
  if (exp == 0) {
    f = std::ldexp(float(man), -24);  // zero / denormal
    return sign ? -f : f;
  }
  if (exp == 31) {
    const uint32_t bits = sign | 0x7F800000u | (man << 13);
    std::memcpy(&f, &bits, 4);
    return f;
  }
  const uint32_t bits = sign | ((exp + 112) << 23) | (man << 13);
  std::memcpy(&f, &bits, 4);
  return f;
}

namespace detail {
inline uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint16_t Be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline float BeFloat(const uint8_t* p) {
  const uint32_t u = Be32(p);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
inline float Short(const uint8_t* p, const PosLayout& l) {
  const uint16_t raw = Be16(p);
  float v;
  if (l.is_signed) {
    const int16_t s = int16_t(raw);
    v = l.normalized ? std::fmax(float(s) / 32767.0f, -1.0f) : float(s);
  } else {
    v = l.normalized ? float(raw) / 65535.0f : float(raw);
  }
  return l.exp_adjust ? std::ldexp(v, l.exp_adjust) : v;
}
}  // namespace detail

// Decodes `count` positions starting at vertex `first_vertex`. Fails (and
// writes nothing further) if the format is unknown, the stride is zero, or
// any read would pass src_size.
inline bool DecodePositions(const uint8_t* src, size_t src_size, const PosLayout& layout,
                            uint32_t first_vertex, uint32_t count, Float4* out) {
  const uint32_t bytes = PositionBytes(layout.format);
  if (!src || bytes == 0 || layout.stride_bytes == 0) return false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = uint64_t(first_vertex + i) * layout.stride_bytes + layout.offset_bytes;
    if (at + bytes > src_size) return false;
    const uint8_t* p = src + at;
    Float4& o = out[i];
    switch (layout.format) {
      case PosFormat::kFloat3:
        o = {detail::BeFloat(p), detail::BeFloat(p + 4), detail::BeFloat(p + 8), 1.0f};
        break;
      case PosFormat::kFloat4:
        o = {detail::BeFloat(p), detail::BeFloat(p + 4), detail::BeFloat(p + 8), detail::BeFloat(p + 12)};
        break;
      case PosFormat::kHalf4:
        o = {HalfToFloat(detail::Be16(p)), HalfToFloat(detail::Be16(p + 2)),
             HalfToFloat(detail::Be16(p + 4)), HalfToFloat(detail::Be16(p + 6))};
        break;
      case PosFormat::kShort4:
        o = {detail::Short(p, layout), detail::Short(p + 2, layout), detail::Short(p + 4, layout),
             detail::Short(p + 6, layout)};
        break;
      default:
        return false;
    }
  }
  return true;
}

}  // namespace fable2::native::capture
```

Note: the test expects `w == 1.0f` for short4 input `0x7FFF` (normalized) and for half4 input `0x3C00`; the decoded fourth component is used as-is (no forced w=1) except for kFloat3.

- [ ] **Step 4: Run test to verify it passes**

Run: `& .\tests\run_native_tests.cmd`
Expected: all prior PASS lines plus `PASS: position decode`.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/position_decode.h tests/native/test_position_decode.cpp tests/run_native_tests.cmd
git commit -m "Native capture: big-endian position decoding"
```

---

### Task 2: Index conversion (pure)

**Files:**
- Create: `src/native/capture/index_convert.h`
- Test: `tests/native/test_index_convert.cpp`; Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces:
  ```cpp
  namespace fable2::native::capture {
  // Xenos primitive types (xenos::PrimitiveType).
  enum : uint32_t { kPrimTriangleList = 4, kPrimTriangleFan = 5, kPrimTriangleStrip = 6,
                    kPrimRectangleList = 8, kPrimQuadList = 13 };
  struct IndexSource { const uint8_t* data; size_t size; bool is32; };  // big-endian
  // Appends a uint32 triangle list for `count` indices starting at `start`.
  // data == nullptr means sequential indices start..start+count-1.
  // Returns false for unsupported primitive types or out-of-range reads.
  // *max_index receives the largest emitted index (0 if none).
  bool BuildTriangleList(const IndexSource& src, uint32_t start, uint32_t count, uint32_t prim,
                         std::vector<uint32_t>& out, uint32_t* max_index);
  bool IsSupportedPrim(uint32_t prim);
  }
  ```

- [ ] **Step 1: Write the failing test**

`tests/native/test_index_convert.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/index_convert.h"
#include <iostream>

using namespace fable2::native::capture;

static std::vector<uint8_t> Be16s(std::initializer_list<uint32_t> v) {
  std::vector<uint8_t> b;
  for (uint32_t x : v) { b.push_back(uint8_t(x >> 8)); b.push_back(uint8_t(x)); }
  return b;
}
static std::vector<uint8_t> Be32s(std::initializer_list<uint32_t> v) {
  std::vector<uint8_t> b;
  for (uint32_t x : v) { b.push_back(x >> 24); b.push_back(x >> 16); b.push_back(x >> 8); b.push_back(uint8_t(x)); }
  return b;
}
static bool Eq(const std::vector<uint32_t>& a, std::initializer_list<uint32_t> b) {
  return a == std::vector<uint32_t>(b);
}

int main() {
  std::vector<uint32_t> out;
  uint32_t mx = 0;
  // List, 16-bit, start offset 1.
  auto l16 = Be16s({9, 0, 1, 2, 2, 1, 3});
  if (!BuildTriangleList({l16.data(), l16.size(), false}, 1, 6, kPrimTriangleList, out, &mx)) return 1;
  if (!Eq(out, {0, 1, 2, 2, 1, 3}) || mx != 3) return 2;
  // Strip with odd-triangle winding swap and a 0xFFFF cut.
  out.clear();
  auto s16 = Be16s({0, 1, 2, 3, 0xFFFF, 4, 5, 6});
  if (!BuildTriangleList({s16.data(), s16.size(), false}, 0, 8, kPrimTriangleStrip, out, &mx)) return 3;
  if (!Eq(out, {0, 1, 2, 2, 1, 3, 4, 5, 6}) || mx != 6) return 4;
  // 32-bit strip with 0xFFFFFFFF cut.
  out.clear();
  auto s32 = Be32s({10, 11, 12, 0xFFFFFFFF, 20, 21, 22});
  if (!BuildTriangleList({s32.data(), s32.size(), true}, 0, 7, kPrimTriangleStrip, out, &mx)) return 5;
  if (!Eq(out, {10, 11, 12, 20, 21, 22}) || mx != 22) return 6;
  // Fan, sequential.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 5, 5, kPrimTriangleFan, out, &mx)) return 7;
  if (!Eq(out, {5, 6, 7, 5, 7, 8, 5, 8, 9}) || mx != 9) return 8;
  // Quad list.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 0, 4, kPrimQuadList, out, &mx)) return 9;
  if (!Eq(out, {0, 1, 2, 0, 2, 3})) return 10;
  // Rect list and line types are unsupported.
  if (BuildTriangleList({nullptr, 0, false}, 0, 3, kPrimRectangleList, out, &mx)) return 11;
  if (BuildTriangleList({nullptr, 0, false}, 0, 2, 2, out, &mx)) return 12;
  // Reading past the buffer fails.
  if (BuildTriangleList({l16.data(), l16.size(), false}, 2, 6, kPrimTriangleList, out, &mx)) return 13;
  // Partial trailing triangle in a list is dropped.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 0, 5, kPrimTriangleList, out, &mx)) return 14;
  if (!Eq(out, {0, 1, 2})) return 15;
  std::cout << "PASS: index convert\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```bat
clang++ -std=c++23 "%~dp0native\test_index_convert.cpp" -o "%OUT%\index_convert.exe" || exit /b 1
"%OUT%\index_convert.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: FAIL compiling (`index_convert.h` not found).

- [ ] **Step 3: Write the implementation**

`src/native/capture/index_convert.h`:
```cpp
#pragma once

// Guest index data -> uint32 triangle list (pure: no SDK/GPU deps). The RHI
// only takes 16-bit index buffers, so the clay pass pulls vertices by index
// from a StructuredBuffer and every primitive type is flattened here.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable2::native::capture {

enum : uint32_t {
  kPrimTriangleList = 4,
  kPrimTriangleFan = 5,
  kPrimTriangleStrip = 6,
  kPrimRectangleList = 8,
  kPrimQuadList = 13,
};

struct IndexSource {
  const uint8_t* data;  // big-endian; nullptr = sequential
  size_t size;
  bool is32;
};

inline bool IsSupportedPrim(uint32_t prim) {
  return prim == kPrimTriangleList || prim == kPrimTriangleFan || prim == kPrimTriangleStrip ||
         prim == kPrimQuadList;
}

inline bool BuildTriangleList(const IndexSource& src, uint32_t start, uint32_t count, uint32_t prim,
                              std::vector<uint32_t>& out, uint32_t* max_index) {
  if (!IsSupportedPrim(prim)) return false;
  const uint32_t width = src.is32 ? 4 : 2;
  if (src.data && (uint64_t(start) + count) * width > src.size) return false;
  const uint32_t cut = src.is32 ? 0xFFFFFFFFu : 0xFFFFu;
  auto at = [&](uint32_t i) -> uint32_t {
    if (!src.data) return start + i;
    const uint8_t* p = src.data + uint64_t(start + i) * width;
    return src.is32 ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]
                    : (uint32_t(p[0]) << 8) | p[1];
  };
  uint32_t mx = 0;
  auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
    out.push_back(a); out.push_back(b); out.push_back(c);
    mx = std::max(mx, std::max(a, std::max(b, c)));
  };
  if (prim == kPrimTriangleList) {
    for (uint32_t i = 0; i + 2 < count; i += 3) emit(at(i), at(i + 1), at(i + 2));
  } else if (prim == kPrimQuadList) {
    for (uint32_t i = 0; i + 3 < count; i += 4) {
      emit(at(i), at(i + 1), at(i + 2));
      emit(at(i), at(i + 2), at(i + 3));
    }
  } else {
    // Strips and fans restart after a cut index.
    uint32_t run = 0, first = 0, prev2 = 0, prev1 = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t v = at(i);
      if (src.data && v == cut) { run = 0; continue; }
      if (run == 0) first = v;
      if (run >= 2) {
        if (prim == kPrimTriangleFan) emit(first, prev1, v);
        else if ((run & 1) == 0) emit(prev2, prev1, v);
        else emit(prev1, prev2, v);
      }
      prev2 = prev1;
      prev1 = v;
      ++run;
    }
  }
  if (max_index) *max_index = mx;
  return true;
}

}  // namespace fable2::native::capture
```
Check the strip expectation against the code: run indices 0,1,2,3 -> triangles (0,1,2) at run 2 (even) and (2,1,3) at run 3 (odd: prev1=2, prev2=1). Matches the test.

- [ ] **Step 4: Run test to verify it passes**

Run: `& .\tests\run_native_tests.cmd` — Expected: `PASS: index convert`.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/index_convert.h tests/native/test_index_convert.cpp tests/run_native_tests.cmd
git commit -m "Native capture: guest index conversion to triangle lists"
```

---

### Task 3: Vertex-fetch microcode decoding (pure)

**Files:**
- Create: `src/native/capture/vfetch_decode.h`
- Test: `tests/native/test_vfetch_decode.cpp`; Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `PosLayout`, `PosFormatFromXenos` (Task 1).
- Produces:
  ```cpp
  namespace fable2::native::capture {
  struct VertexFetch {
    uint32_t instr_index;      // order in the program (0-based across exec clauses)
    uint32_t fetch_slot;       // const_index * 3 + const_index_sel (full fetch) or inherited (mini)
    uint32_t dst_reg;
    uint32_t dst_swizzle;      // 12 bits, 3 per component
    uint32_t format;           // xenos::VertexFormat value
    bool is_signed, normalized, mini;
    int exp_adjust;
    uint32_t stride_dwords;    // full fetch value or inherited (mini)
    int32_t offset_dwords;
  };
  // ucode: host-endian dwords (caller byte-swaps the guest big-endian words).
  std::vector<VertexFetch> DecodeVertexFetches(const uint32_t* ucode, size_t dword_count);
  // Position element: fetches[override_index] if override_index >= 0, else the
  // first full (non-mini) fetch. Fails for unknown format or zero stride.
  bool SelectPosition(const std::vector<VertexFetch>& fetches, int override_index, PosLayout* out);
  }
  ```

Bit layouts (verify against `thirdparty/rexglue-sdk/include/rex/graphics/format/ucode.h` before coding: `ControlFlowOpcode` enum near line 95, `ControlFlowExecInstruction` at line 218, `ControlFlowCondExecInstruction` at line 260, `UnpackControlFlowInstructions` at line 515, `VertexFetchInstruction::Data` at line ~740):
- Control flow: the program starts with control-flow instructions, two 48-bit instructions per 3 dwords: `a = dw0 | (uint64(dw1 & 0xFFFF) << 32)`, `b = (dw1 >> 16) | (uint64(dw2) << 16)`.
- Every exec-type control-flow instruction (opcodes 1 kExec, 2 kExecEnd, 3 kCondExec, 4 kCondExecEnd, 5 kCondExecPred, 6 kCondExecPredEnd, 13 kCondExecPredClean, 14 kCondExecPredCleanEnd) has `address = bits[0:12)`, `count = bits[12:15)`, `sequence = bits[16:28)` and `opcode = bits[44:48)`. End variants are 2, 4, 6, 14.
- Instruction `address` is in units of 3 dwords from the program start; instruction `i` of the clause is a fetch when `(sequence >> (2*i)) & 1`.
- Fetch instruction (3 dwords): dw0 `opcode bits[0:5)` (0 = vertex fetch), `dst_reg [12:18)`, `const_index [20:25)`, `const_index_sel [25:27)`; dw1 `dst_swiz [0:12)`, `fomat_comp_all [12]` (signed), `num_format_all [13]` (0 = normalized), `format [16:22)`, `exp_adjust [24:30)` signed 6-bit, `is_mini_fetch [30]`; dw2 `stride [0:8)`, `offset [8:31)` signed 23-bit.
- Walking stops after an End exec, or when the next control-flow pair would start at or after the smallest exec address seen (the instruction area).

- [ ] **Step 1: Write the failing test**

`tests/native/test_vfetch_decode.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/vfetch_decode.h"
#include <iostream>

using namespace fable2::native::capture;

// 48-bit exec CF instruction.
static uint64_t Exec(uint32_t opcode, uint32_t address, uint32_t count, uint32_t sequence) {
  return uint64_t(address & 0xFFF) | (uint64_t(count & 7) << 12) | (uint64_t(sequence & 0xFFF) << 16) |
         (uint64_t(opcode & 0xF) << 44);
}
static void PackCf(uint64_t a, uint64_t b, uint32_t* dw) {
  dw[0] = uint32_t(a);
  dw[1] = uint32_t((a >> 32) & 0xFFFF) | (uint32_t(b & 0xFFFF) << 16);
  dw[2] = uint32_t(b >> 16);
}
static void Vfetch(uint32_t* dw, uint32_t dst, uint32_t ci, uint32_t sel, uint32_t fmt, bool sgn, bool norm,
                   int exp, bool mini, uint32_t stride, int32_t offset) {
  dw[0] = 0u | (dst << 12) | (1u << 19) | (ci << 20) | (sel << 25);
  dw[1] = 0x688u | (uint32_t(sgn) << 12) | (uint32_t(!norm) << 13) | (fmt << 16) |
          ((uint32_t(exp) & 0x3F) << 24) | (uint32_t(mini) << 30);
  dw[2] = stride | ((uint32_t(offset) & 0x7FFFFF) << 8);
}

int main() {
  // CF pair 0: exec(addr=2, count=3, seq: fetch, fetch, alu) + exec_end(addr=5, count=1, fetch).
  uint32_t ucode[3 * 6] = {};
  PackCf(Exec(1, 2, 3, 0b000101), Exec(2, 5, 1, 0b01), ucode);
  // Instructions start at address 2 (dword 6).
  Vfetch(ucode + 6, 0, 0, 1, 57, true, true, 0, false, 8, 0);   // float3 pos, slot 1, stride 8 dw
  Vfetch(ucode + 9, 1, 0, 1, 26, true, true, 0, true, 0, 3);    // mini: inherits slot/stride
  // ucode + 12 is an ALU instruction (sequence bit 0) - left zero.
  Vfetch(ucode + 15, 2, 1, 0, 32, false, true, -2, false, 4, 1); // half4, slot 3, stride 4
  auto f = DecodeVertexFetches(ucode, 18);
  if (f.size() != 3) return 1;
  if (f[0].fetch_slot != 1 || f[0].format != 57 || f[0].stride_dwords != 8 || f[0].mini) return 2;
  if (!f[1].mini || f[1].fetch_slot != 1 || f[1].stride_dwords != 8 || f[1].offset_dwords != 3) return 3;
  if (f[2].fetch_slot != 3 || f[2].exp_adjust != -2 || f[2].offset_dwords != 1 || f[2].instr_index != 3) return 4;
  PosLayout pos;
  if (!SelectPosition(f, -1, &pos)) return 5;
  if (pos.format != PosFormat::kFloat3 || pos.stride_bytes != 32 || pos.offset_bytes != 0 || pos.fetch_slot != 1) return 6;
  if (!SelectPosition(f, 2, &pos) || pos.format != PosFormat::kHalf4 || pos.offset_bytes != 4) return 7;
  if (SelectPosition(f, 9, &pos)) return 8;
  // A program whose first fetch is a mini fetch, or with no fetches, has no position.
  std::vector<VertexFetch> only_mini = {f[1]};
  only_mini[0].fetch_slot = 0;
  if (SelectPosition(only_mini, -1, &pos)) return 9;
  if (SelectPosition({}, -1, &pos)) return 10;
  // Zero stride full fetch is rejected.
  std::vector<VertexFetch> zero = {f[0]};
  zero[0].stride_dwords = 0;
  if (SelectPosition(zero, -1, &pos)) return 11;
  // Truncated input never reads out of bounds.
  if (!DecodeVertexFetches(ucode, 2).empty()) return 12;
  std::cout << "PASS: vfetch decode\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```bat
clang++ -std=c++23 "%~dp0native\test_vfetch_decode.cpp" -o "%OUT%\vfetch_decode.exe" || exit /b 1
"%OUT%\vfetch_decode.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd` — Expected: FAIL compiling (`vfetch_decode.h` not found).

- [ ] **Step 3: Write the implementation**

`src/native/capture/vfetch_decode.h`:
```cpp
#pragma once

// Xenos shader microcode -> vertex fetch list (pure: no SDK/GPU deps). Bit
// layouts mirror rex/graphics/format/ucode.h (ControlFlowExecInstruction,
// VertexFetchInstruction); see the plan's Task 3 for the field list.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "position_decode.h"

namespace fable2::native::capture {

struct VertexFetch {
  uint32_t instr_index = 0;
  uint32_t fetch_slot = 0;
  uint32_t dst_reg = 0;
  uint32_t dst_swizzle = 0;
  uint32_t format = 0;
  bool is_signed = false;
  bool normalized = true;
  bool mini = false;
  int exp_adjust = 0;
  uint32_t stride_dwords = 0;
  int32_t offset_dwords = 0;
};

namespace detail {
inline bool IsExec(uint32_t op) { return (op >= 1 && op <= 6) || op == 13 || op == 14; }
inline bool IsExecEnd(uint32_t op) { return op == 2 || op == 4 || op == 6 || op == 14; }
inline int32_t SignExtend(uint32_t v, int bits) {
  const uint32_t m = 1u << (bits - 1);
  return int32_t((v ^ m) - m);
}
}  // namespace detail

inline std::vector<VertexFetch> DecodeVertexFetches(const uint32_t* ucode, size_t dword_count) {
  std::vector<VertexFetch> out;
  if (!ucode || dword_count < 3) return out;
  uint32_t instr_index = 0, cur_slot = 0, cur_stride = 0;
  size_t cf_end = dword_count;  // shrinks to the first exec's instruction area
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
      for (uint32_t i = 0; i < count; ++i, ++instr_index) {
        if (!((sequence >> (2 * i)) & 1)) continue;
        const size_t at = (size_t(address) + i) * 3;
        if (at + 3 > dword_count) return out;
        const uint32_t d0 = ucode[at], d1 = ucode[at + 1], d2 = ucode[at + 2];
        if ((d0 & 0x1F) != 0) continue;  // texture fetch
        VertexFetch f;
        f.instr_index = instr_index;
        f.dst_reg = (d0 >> 12) & 0x3F;
        f.dst_swizzle = d1 & 0xFFF;
        f.is_signed = (d1 >> 12) & 1;
        f.normalized = ((d1 >> 13) & 1) == 0;
        f.format = (d1 >> 16) & 0x3F;
        f.exp_adjust = detail::SignExtend((d1 >> 24) & 0x3F, 6);
        f.mini = (d1 >> 30) & 1;
        if (!f.mini) {
          cur_slot = ((d0 >> 20) & 0x1F) * 3 + ((d0 >> 25) & 0x3);
          cur_stride = d2 & 0xFF;
        }
        f.fetch_slot = cur_slot;
        f.stride_dwords = cur_stride;
        f.offset_dwords = detail::SignExtend((d2 >> 8) & 0x7FFFFF, 23);
        out.push_back(f);
      }
      if (detail::IsExecEnd(op)) { ended = true; break; }
    }
    if (ended) break;
  }
  return out;
}

inline bool SelectPosition(const std::vector<VertexFetch>& fetches, int override_index, PosLayout* out) {
  const VertexFetch* f = nullptr;
  if (override_index >= 0) {
    if (size_t(override_index) >= fetches.size()) return false;
    f = &fetches[size_t(override_index)];
  } else {
    for (const auto& c : fetches) {
      if (!c.mini) { f = &c; break; }
    }
  }
  if (!f || f->stride_dwords == 0) return false;
  PosLayout l;
  l.format = PosFormatFromXenos(f->format);
  if (l.format == PosFormat::kUnknown || f->offset_dwords < 0) return false;
  l.is_signed = f->is_signed;
  l.normalized = f->normalized;
  l.exp_adjust = f->exp_adjust;
  l.offset_bytes = uint32_t(f->offset_dwords) * 4;
  l.stride_bytes = f->stride_dwords * 4;
  l.fetch_slot = f->fetch_slot;
  *out = l;
  return true;
}

}  // namespace fable2::native::capture
```

Note on the test's `only_mini` case: the vector contains only a mini fetch, so the first-full-fetch rule finds nothing and returns false.

- [ ] **Step 4: Run test to verify it passes**

Run: `& .\tests\run_native_tests.cmd` — Expected: `PASS: vfetch decode`.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/vfetch_decode.h tests/native/test_vfetch_decode.cpp tests/run_native_tests.cmd
git commit -m "Native capture: decode vertex fetches from Xenos microcode"
```

---

### Task 4: Draw records, frame builder and scene publishing (pure)

**Files:**
- Create: `src/native/capture/draw_record.h`, `src/native/render/frame_scene.h`
- Test: `tests/native/test_frame_scene.cpp`; Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `PosLayout` (Task 1), `IsSupportedPrim` (Task 2).
- Produces:
  ```cpp
  namespace fable2::native::capture {
  enum class SkipReason : uint8_t { kNone, kNoTransform, kUnknownShader, kUnknownPosFormat,
    kUnsupportedPrim, kBadMemory, kBadIndex, kNoStream, kCount };
  const char* SkipReasonName(SkipReason r);
  // kDot: clip[i] = dot(row[i], p).  kCombine: clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3.
  enum class TransformLayout : uint8_t { kDot = 0, kCombine = 1 };
  struct BufferRef { uint32_t phys_addr = 0; uint32_t size = 0; };
  struct DrawRecord {
    uint32_t seq = 0;            // capture order within the frame
    uint32_t func_id = 0;        // hook id (xdk_hooks.inc)
    uint32_t prim = 0;
    int32_t base_vertex = 0;
    uint32_t start = 0;
    uint32_t count = 0;
    bool indexed = false;
    bool index32 = false;
    BufferRef ib;
    BufferRef vb;                // the stream that holds the position element
    PosLayout pos;
    uint64_t vs_hash = 0;
    float rows[16] = {};         // 4 constant registers, row-major as read
    TransformLayout layout = TransformLayout::kDot;
    SkipReason skip = SkipReason::kNone;
  };
  struct TransformInfo { uint32_t base_reg; TransformLayout layout; int pos_fetch; };
  // Everything the hook path learned for one draw; AssembleRecord decides the skip reason.
  struct DrawInputs {
    uint32_t func_id, prim, start, count; int32_t base_vertex; bool indexed;
    bool have_shader; uint64_t vs_hash; bool have_pos; PosLayout pos;
    bool have_vb; BufferRef vb; bool have_ib; BufferRef ib; bool index32;
    const TransformInfo* transform;     // nullptr = shader not in the table
    const float* bank;                  // 256*4 floats (host order) or nullptr
  };
  DrawRecord AssembleRecord(const DrawInputs& in, uint32_t seq);
  }
  namespace fable2::native::render {
  struct FrameScene {
    uint64_t frame = 0;
    std::vector<capture::DrawRecord> draws;   // drawable records only (skip == kNone)
    uint32_t captured = 0;                    // all main-scene guest draw calls
    uint32_t skipped[size_t(capture::SkipReason::kCount)] = {};
  };
  class FrameBuilder {
   public:
    void Open();            // second Open while open is ignored
    void Close();           // Close while closed is ignored
    bool InMainScene() const;
    void Add(const capture::DrawRecord& r);  // ignored outside the bracket
    uint32_t NextSeq() const;
    std::shared_ptr<const FrameScene> Finish(uint64_t frame);  // closes, resets, keeps capacity
  };
  class ScenePublisher {
   public:
    void Publish(std::shared_ptr<const FrameScene> s);
    std::shared_ptr<const FrameScene> Latest() const;
  };
  }
  ```

- [ ] **Step 1: Write the failing test**

`tests/native/test_frame_scene.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/frame_scene.h"
#include <iostream>
#include <string>

using namespace fable2::native;
using capture::SkipReason;

static capture::DrawInputs Good() {
  static float bank[256 * 4] = {};
  static const capture::TransformInfo t{4, capture::TransformLayout::kCombine, -1};
  for (int i = 0; i < 16; ++i) bank[4 * 4 + i] = float(i + 1);
  capture::DrawInputs in{};
  in.func_id = 2; in.prim = 6; in.start = 0; in.count = 30; in.indexed = true;
  in.have_shader = true; in.vs_hash = 0xABCD; in.have_pos = true;
  in.pos.format = capture::PosFormat::kFloat3; in.pos.stride_bytes = 12;
  in.have_vb = true; in.vb = {0x1000, 1200}; in.have_ib = true; in.ib = {0x2000, 60};
  in.transform = &t; in.bank = bank;
  return in;
}

int main() {
  auto r = capture::AssembleRecord(Good(), 7);
  if (r.skip != SkipReason::kNone || r.seq != 7 || r.rows[0] != 1.0f || r.rows[15] != 16.0f) return 1;
  if (r.layout != capture::TransformLayout::kCombine || r.vb.size != 1200) return 2;
  auto in = Good(); in.have_shader = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnknownShader) return 3;
  in = Good(); in.have_pos = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnknownPosFormat) return 4;
  in = Good(); in.transform = nullptr;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoTransform) return 5;
  in = Good(); in.bank = nullptr;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoTransform) return 6;
  in = Good(); in.prim = 8;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnsupportedPrim) return 7;
  in = Good(); in.have_vb = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 8;
  in = Good(); in.have_ib = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 9;
  in = Good(); in.vb.size = 0;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kBadMemory) return 10;
  in = Good(); in.ib.size = 30;  // 30 indices * 2 bytes = 60 > 30
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kBadMemory) return 11;
  if (std::string(capture::SkipReasonName(SkipReason::kBadIndex)) != "bad-index") return 12;

  render::FrameBuilder b;
  b.Add(r);                         // outside bracket: ignored
  b.Close();                        // close while closed: ignored
  b.Open(); b.Open();               // nested open: ignored
  if (!b.InMainScene()) return 13;
  b.Add(r);
  auto skipped = r; skipped.skip = SkipReason::kNoTransform;
  b.Add(skipped);
  b.Close();
  b.Add(r);                         // after close: ignored
  b.Open();                         // left open at Finish
  b.Add(r);
  auto s = b.Finish(42);
  if (s->frame != 42 || s->captured != 3 || s->draws.size() != 2) return 14;
  if (s->skipped[size_t(SkipReason::kNoTransform)] != 1) return 15;
  if (b.InMainScene()) return 16;   // Finish closes
  auto s2 = b.Finish(43);
  if (s2->captured != 0 || !s2->draws.empty()) return 17;

  render::ScenePublisher pub;
  if (pub.Latest()) return 18;
  pub.Publish(s);
  pub.Publish(s2);
  if (pub.Latest()->frame != 43) return 19;
  std::cout << "PASS: draw record, frame builder, publisher\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```bat
clang++ -std=c++23 "%~dp0native\test_frame_scene.cpp" -o "%OUT%\frame_scene.exe" || exit /b 1
"%OUT%\frame_scene.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd` — Expected: FAIL compiling (`frame_scene.h` not found).

- [ ] **Step 3: Write the implementation**

`src/native/capture/draw_record.h`:
```cpp
#pragma once

// One captured guest draw call (pure: no SDK/GPU deps).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "index_convert.h"
#include "position_decode.h"

namespace fable2::native::capture {

enum class SkipReason : uint8_t {
  kNone,
  kNoTransform,
  kUnknownShader,
  kUnknownPosFormat,
  kUnsupportedPrim,
  kBadMemory,
  kBadIndex,
  kNoStream,
  kCount
};

inline const char* SkipReasonName(SkipReason r) {
  switch (r) {
    case SkipReason::kNone: return "none";
    case SkipReason::kNoTransform: return "no-transform";
    case SkipReason::kUnknownShader: return "unknown-shader";
    case SkipReason::kUnknownPosFormat: return "unknown-pos-format";
    case SkipReason::kUnsupportedPrim: return "unsupported-prim";
    case SkipReason::kBadMemory: return "bad-memory";
    case SkipReason::kBadIndex: return "bad-index";
    case SkipReason::kNoStream: return "no-stream";
    default: return "?";
  }
}

// kDot: clip[i] = dot(row[i], p).  kCombine: clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3.
enum class TransformLayout : uint8_t { kDot = 0, kCombine = 1 };

struct BufferRef {
  uint32_t phys_addr = 0;
  uint32_t size = 0;
};

struct DrawRecord {
  uint32_t seq = 0;
  uint32_t func_id = 0;
  uint32_t prim = 0;
  int32_t base_vertex = 0;
  uint32_t start = 0;
  uint32_t count = 0;
  bool indexed = false;
  bool index32 = false;
  BufferRef ib;
  BufferRef vb;
  PosLayout pos;
  uint64_t vs_hash = 0;
  float rows[16] = {};
  TransformLayout layout = TransformLayout::kDot;
  SkipReason skip = SkipReason::kNone;
};

struct TransformInfo {
  uint32_t base_reg;
  TransformLayout layout;
  int pos_fetch;  // -1 = first full vertex fetch
};

struct DrawInputs {
  uint32_t func_id = 0, prim = 0, start = 0, count = 0;
  int32_t base_vertex = 0;
  bool indexed = false;
  bool have_shader = false;
  uint64_t vs_hash = 0;
  bool have_pos = false;
  PosLayout pos;
  bool have_vb = false;
  BufferRef vb;
  bool have_ib = false;
  BufferRef ib;
  bool index32 = false;
  const TransformInfo* transform = nullptr;
  const float* bank = nullptr;  // 256 registers * 4 floats, host order
};

inline DrawRecord AssembleRecord(const DrawInputs& in, uint32_t seq) {
  DrawRecord r;
  r.seq = seq;
  r.func_id = in.func_id;
  r.prim = in.prim;
  r.base_vertex = in.base_vertex;
  r.start = in.start;
  r.count = in.count;
  r.indexed = in.indexed;
  r.index32 = in.index32;
  r.ib = in.ib;
  r.vb = in.vb;
  r.pos = in.pos;
  r.vs_hash = in.vs_hash;
  auto skip = [&](SkipReason why) { r.skip = why; return r; };
  if (!IsSupportedPrim(in.prim)) return skip(SkipReason::kUnsupportedPrim);
  if (!in.have_shader) return skip(SkipReason::kUnknownShader);
  if (!in.have_pos) return skip(SkipReason::kUnknownPosFormat);
  if (!in.have_vb || (in.indexed && !in.have_ib)) return skip(SkipReason::kNoStream);
  if (in.vb.size == 0) return skip(SkipReason::kBadMemory);
  if (in.indexed && (uint64_t(in.start) + in.count) * (in.index32 ? 4 : 2) > in.ib.size)
    return skip(SkipReason::kBadMemory);
  if (!in.transform || !in.bank || in.transform->base_reg > 252) return skip(SkipReason::kNoTransform);
  std::memcpy(r.rows, in.bank + size_t(in.transform->base_reg) * 4, sizeof(r.rows));
  r.layout = in.transform->layout;
  return r;
}

}  // namespace fable2::native::capture
```

`src/native/render/frame_scene.h`:
```cpp
#pragma once

// Per-frame scene snapshot built on the game thread and consumed on the GPU
// thread (pure: no SDK/GPU deps). Pattern: skate3recomp's FrameScene.

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

#include "../capture/draw_record.h"

namespace fable2::native::render {

struct FrameScene {
  uint64_t frame = 0;
  std::vector<capture::DrawRecord> draws;
  uint32_t captured = 0;
  uint32_t skipped[size_t(capture::SkipReason::kCount)] = {};
};

class FrameBuilder {
 public:
  void Open() { open_ = true; }
  void Close() { open_ = false; }
  bool InMainScene() const { return open_; }
  uint32_t NextSeq() const { return captured_; }

  void Add(const capture::DrawRecord& r) {
    if (!open_) return;
    ++captured_;
    if (r.skip == capture::SkipReason::kNone) {
      draws_.push_back(r);
    } else {
      ++skipped_[size_t(r.skip)];
    }
  }

  std::shared_ptr<const FrameScene> Finish(uint64_t frame) {
    auto s = std::make_shared<FrameScene>();
    s->frame = frame;
    s->captured = captured_;
    std::copy(std::begin(skipped_), std::end(skipped_), std::begin(s->skipped));
    const size_t reserve = draws_.size();
    s->draws = std::move(draws_);
    draws_ = {};
    draws_.reserve(reserve);
    captured_ = 0;
    std::fill(std::begin(skipped_), std::end(skipped_), 0u);
    open_ = false;
    return s;
  }

 private:
  bool open_ = false;
  uint32_t captured_ = 0;
  uint32_t skipped_[size_t(capture::SkipReason::kCount)] = {};
  std::vector<capture::DrawRecord> draws_;
};

class ScenePublisher {
 public:
  void Publish(std::shared_ptr<const FrameScene> s) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = std::move(s);
  }
  std::shared_ptr<const FrameScene> Latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
  }

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const FrameScene> latest_;
};

}  // namespace fable2::native::render
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& .\tests\run_native_tests.cmd` — Expected: `PASS: draw record, frame builder, publisher`.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/draw_record.h src/native/render/frame_scene.h tests/native/test_frame_scene.cpp tests/run_native_tests.cmd
git commit -m "Native capture: draw records, frame builder, scene publisher"
```

---

### Task 5: Geometry cache bookkeeping (pure)

**Files:**
- Create: `src/native/render/geometry_cache_index.h`
- Test: `tests/native/test_geometry_cache_index.cpp`; Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces:
  ```cpp
  namespace fable2::native::render {
  struct GeoKey {
    uint32_t addr = 0, size = 0, stride = 0, extra = 0;  // extra: format / prim / start / count digest
    uint8_t kind = 0;                                    // 0 = positions, 1 = indices
    bool operator==(const GeoKey&) const = default;
  };
  struct GeoKeyHash { size_t operator()(const GeoKey& k) const; };
  struct LookupResult { bool hit; uint32_t id; };
  class GeometryCacheIndex {
   public:
    explicit GeometryCacheIndex(uint64_t budget_bytes);
    void BeginFrame(uint64_t frame);
    // Hit if key present with the same content hash (marks it used this frame).
    LookupResult Lookup(const GeoKey& key, uint64_t content_hash);
    // Inserts or replaces; returns ids evicted to fit the budget (LRU, never
    // entries used in the current frame; ties evict the lowest id first). The
    // replaced id (same key) is also returned.
    uint32_t Insert(const GeoKey& key, uint64_t content_hash, uint64_t bytes, std::vector<uint32_t>* evicted);
    uint64_t resident_bytes() const;
    uint64_t budget_bytes() const;
    void set_budget_bytes(uint64_t b);
    size_t size() const;
  };
  }
  ```

- [ ] **Step 1: Write the failing test**

`tests/native/test_geometry_cache_index.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/geometry_cache_index.h"
#include <algorithm>
#include <iostream>

using namespace fable2::native::render;

int main() {
  GeometryCacheIndex c(100);
  std::vector<uint32_t> ev;
  c.BeginFrame(1);
  GeoKey a{0x1000, 40, 12, 0, 0}, b{0x2000, 40, 12, 0, 0}, d{0x3000, 40, 12, 0, 0};
  if (c.Lookup(a, 7).hit) return 1;
  const uint32_t ida = c.Insert(a, 7, 40, &ev);
  if (!ev.empty() || !c.Lookup(a, 7).hit || c.Lookup(a, 7).id != ida) return 2;
  if (c.Lookup(a, 8).hit) return 3;                    // content changed -> miss
  const uint32_t ida2 = c.Insert(a, 8, 40, &ev);       // replace: old id evicted
  if (ev.size() != 1 || ev[0] != ida || ida2 == ida || c.resident_bytes() != 40) return 4;
  ev.clear();
  c.BeginFrame(2);
  const uint32_t idb = c.Insert(b, 1, 40, &ev);
  c.BeginFrame(3);
  c.Lookup(b, 1);                                      // b used in frame 3, a last used frame 1
  const uint32_t idd = c.Insert(d, 1, 40, &ev);        // 120 > 100: evict LRU (a)
  if (ev.size() != 1 || ev[0] != ida2 || c.resident_bytes() != 80) return 5;
  ev.clear();
  // Everything resident is used this frame: over budget is allowed, nothing evicted.
  GeoKey e{0x4000, 40, 12, 0, 0};
  c.Lookup(d, 1);
  c.Insert(e, 1, 40, &ev);
  if (!ev.empty() || c.resident_bytes() != 120 || c.size() != 3) return 6;
  // Lower budget: next insert in a new frame evicts down to budget.
  c.set_budget_bytes(50);
  c.BeginFrame(4);
  GeoKey f{0x5000, 10, 12, 0, 0};
  c.Insert(f, 1, 10, &ev);
  if (c.resident_bytes() > 50 || std::find(ev.begin(), ev.end(), idb) == ev.end()) return 7;
  (void)idd;
  // Different kind with same address is a different key.
  GeoKey fi = f; fi.kind = 1;
  if (c.Lookup(fi, 1).hit) return 8;
  std::cout << "PASS: geometry cache index\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```bat
clang++ -std=c++23 "%~dp0native\test_geometry_cache_index.cpp" -o "%OUT%\geometry_cache_index.exe" || exit /b 1
"%OUT%\geometry_cache_index.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd` — Expected: FAIL compiling.

- [ ] **Step 3: Write the implementation**

`src/native/render/geometry_cache_index.h`:
```cpp
#pragma once

// Bookkeeping for decoded guest geometry on the GPU (pure: no SDK/GPU deps):
// key + content hash -> id, byte budget, LRU eviction that never evicts an
// entry used in the current frame.

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fable2::native::render {

struct GeoKey {
  uint32_t addr = 0, size = 0, stride = 0, extra = 0;
  uint8_t kind = 0;  // 0 = positions, 1 = indices
  bool operator==(const GeoKey&) const = default;
};

struct GeoKeyHash {
  size_t operator()(const GeoKey& k) const {
    uint64_t h = 1469598103934665603ull;
    for (uint64_t v : {uint64_t(k.addr), uint64_t(k.size), uint64_t(k.stride), uint64_t(k.extra),
                       uint64_t(k.kind)}) {
      h = (h ^ v) * 1099511628211ull;
    }
    return size_t(h);
  }
};

struct LookupResult {
  bool hit;
  uint32_t id;
};

class GeometryCacheIndex {
 public:
  explicit GeometryCacheIndex(uint64_t budget_bytes) : budget_(budget_bytes) {}

  void BeginFrame(uint64_t frame) { frame_ = frame; }

  LookupResult Lookup(const GeoKey& key, uint64_t content_hash) {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.hash != content_hash) return {false, 0};
    it->second.last_used = frame_;
    return {true, it->second.id};
  }

  uint32_t Insert(const GeoKey& key, uint64_t content_hash, uint64_t bytes,
                  std::vector<uint32_t>* evicted) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      resident_ -= it->second.bytes;
      if (evicted) evicted->push_back(it->second.id);
      entries_.erase(it);
    }
    while (resident_ + bytes > budget_) {
      auto victim = entries_.end();
      for (auto e = entries_.begin(); e != entries_.end(); ++e) {
        if (e->second.last_used == frame_) continue;
        if (victim == entries_.end() || e->second.last_used < victim->second.last_used ||
            (e->second.last_used == victim->second.last_used && e->second.id < victim->second.id)) {
          victim = e;
        }
      }
      if (victim == entries_.end()) break;  // all in use this frame: go over budget
      resident_ -= victim->second.bytes;
      if (evicted) evicted->push_back(victim->second.id);
      entries_.erase(victim);
    }
    const uint32_t id = next_id_++;
    entries_[key] = {content_hash, bytes, frame_, id};
    resident_ += bytes;
    return id;
  }

  uint64_t resident_bytes() const { return resident_; }
  uint64_t budget_bytes() const { return budget_; }
  void set_budget_bytes(uint64_t b) { budget_ = b; }
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    uint64_t hash;
    uint64_t bytes;
    uint64_t last_used;
    uint32_t id;
  };
  std::unordered_map<GeoKey, Entry, GeoKeyHash> entries_;
  uint64_t budget_;
  uint64_t resident_ = 0;
  uint64_t frame_ = 0;
  uint32_t next_id_ = 1;
};

}  // namespace fable2::native::render
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& .\tests\run_native_tests.cmd` — Expected: `PASS: geometry cache index`.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/geometry_cache_index.h tests/native/test_geometry_cache_index.cpp tests/run_native_tests.cmd
git commit -m "Native render: geometry cache bookkeeping with LRU budget"
```

---

### Task 6: Matrix finder and transform-table generator (Python)

**Files:**
- Create: `tools/xdk_sigmatch/matrix_finder.py`, `tools/xdk_sigmatch/gen_transform_table.py`
- Test: `tests/test_matrix_finder.py`

**Interfaces:**
- Consumes: discovery rows (written by Task 10), one JSON object per line. Rows with `"kind": "draw"` carry: `vs_hash` (string `"0x..."`), `bank` (list of 1024 floats, host order, register-major: reg r component c at `4*r+c`), `positions` (list of `[x, y, z, w]` decoded with the shader's position layout), `viewport` (`[1120, 720]`).
- Produces:
  - `matrix_finder.find_transform(samples, products=False) -> dict | None` where `samples` is a list of `(bank, positions)`; result `{"base": int, "layout": "dot"|"combine", "score": float}` or, with products, `{"base": a, "base2": b, "layout": ..., "score": ...}`.
  - CLI: `python tools\xdk_sigmatch\matrix_finder.py <discovery.jsonl> --out docs\native-renderer\vs-transforms.json [--products] [--min-score 0.9]` writes `{ "0x<hash>": {"base", "layout", "score", "samples", "pos_fetch": -1} }`, merging with an existing file (existing entries with `"manual": true` are kept).
  - `gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc` writes lines `FABLE2_VS_TRANSFORM(0x<hash>ull, <base>, <0 dot|1 combine>, <pos_fetch>)`, sorted by hash, skipping entries with `base2`.

Scoring (both modes): for one sample and a candidate matrix, each vertex maps to clip `c`; a vertex is "inside" when `c.w > 1e-6`, `|c.x/c.w| <= 1.05`, `|c.y/c.w| <= 1.05`, `-0.05 <= c.z/c.w <= 1.05`. Sample score = fraction inside. The candidate is valid for the sample if the sample score >= 0.5 and the NDC spread (max - min of `x/w` over inside vertices) is >= 0.002. Candidate score = mean sample score over all samples of that shader, but 0 if any sample is invalid. Best = highest score; ties broken by lowest base, then `dot` before `combine`. Result rejected below `--min-score` (default 0.9). Registers with all-zero rows are skipped as candidates.

- [ ] **Step 1: Write the failing test**

`tests/test_matrix_finder.py`:
```python
import importlib.util
import json
import random
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("mf", ROOT / "tools" / "xdk_sigmatch" / "matrix_finder.py")
mf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mf)
SPEC2 = importlib.util.spec_from_file_location("gtt", ROOT / "tools" / "xdk_sigmatch" / "gen_transform_table.py")
gtt = importlib.util.module_from_spec(SPEC2)
SPEC2.loader.exec_module(gtt)

# A perspective-ish matrix mapping the unit cube in front of the camera into clip space.
M = [[1.2, 0.0, 0.0, 0.0],
     [0.0, 1.6, 0.0, 0.0],
     [0.0, 0.0, 1.0, -0.1],
     [0.0, 0.0, 1.0, 0.0]]


def bank_with(rows, base, layout, seed=1):
    rnd = random.Random(seed)
    bank = [rnd.uniform(-50, 50) for _ in range(1024)]
    for r in range(4):
        for c in range(4):
            v = rows[r][c] if layout == "dot" else rows[c][r]
            bank[4 * (base + r) + c] = v
    return bank


def verts(seed=2, n=40):
    rnd = random.Random(seed)
    return [[rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(2, 10), 1.0] for _ in range(n)]


class FinderTests(unittest.TestCase):
    def test_finds_dot_layout(self):
        samples = [(bank_with(M, 12, "dot", s), verts(s)) for s in range(3)]
        r = mf.find_transform(samples)
        self.assertEqual((r["base"], r["layout"]), (12, "dot"))
        self.assertGreaterEqual(r["score"], 0.9)

    def test_finds_combine_layout(self):
        samples = [(bank_with(M, 40, "combine", s), verts(s)) for s in range(3)]
        r = mf.find_transform(samples)
        self.assertEqual((r["base"], r["layout"]), (40, "combine"))

    def test_rejects_bank_without_matrix(self):
        rnd = random.Random(9)
        samples = [([rnd.uniform(-50, 50) for _ in range(1024)], verts(s)) for s in range(5)]
        self.assertIsNone(mf.find_transform(samples))

    def test_product_of_two_windows(self):
        # Vertices behind the camera; "world" flips z so only view-projection x world fits.
        # Sparse bank keeps the O(windows^2) product search fast.
        world = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, -1, 0], [0, 0, 0, 1]]
        bank = [0.0] * 1024
        for r in range(4):
            for c in range(4):
                bank[4 * (8 + r) + c] = M[r][c]
                bank[4 * (20 + r) + c] = world[r][c]
        rnd = random.Random(4)
        behind = [[rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-10, -2), 1.0] for _ in range(40)]
        samples = [(bank, behind)]
        self.assertIsNone(mf.find_transform(samples, min_score=0.99))  # neither window alone fits
        r = mf.find_transform(samples, products=True, min_score=0.99)
        self.assertEqual((r.get("base"), r.get("base2")), (8, 20))

    def test_cli_merges_and_keeps_manual(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            log = d / "disc.jsonl"
            rows = [{"kind": "draw", "vs_hash": "0x1", "bank": bank_with(M, 12, "dot", s), "positions": verts(s),
                     "viewport": [1120, 720]} for s in range(2)]
            rows.append({"kind": "meta"})
            log.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
            out = d / "vs.json"
            out.write_text(json.dumps({"0x2": {"base": 4, "layout": "combine", "manual": True, "pos_fetch": 1}}))
            mf.main([str(log), "--out", str(out)])
            data = json.loads(out.read_text())
            self.assertEqual(data["0x1"]["base"], 12)
            self.assertEqual(data["0x1"]["samples"], 2)
            self.assertTrue(data["0x2"]["manual"])
            inc = d / "t.inc"
            gtt.main(["--json", str(out), "--out", str(inc)])
            text = inc.read_text()
            self.assertIn("FABLE2_VS_TRANSFORM(0x1ull, 12, 0, -1)", text)
            self.assertIn("FABLE2_VS_TRANSFORM(0x2ull, 4, 1, 1)", text)
            self.assertLess(text.index("0x1ull"), text.index("0x2ull"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest discover -s tests -p "test_matrix_finder.py" -v`
Expected: FAIL (`FileNotFoundError` for `matrix_finder.py`).

- [ ] **Step 3: Write the implementation**

`tools/xdk_sigmatch/matrix_finder.py`:
```python
"""Find the world-view-projection constant window per vertex shader.

Reads native discovery rows (FABLE2_NATIVE_DISCOVERY) and, for each vertex
shader hash, tries every 4-register window of the vertex constant bank in two
layouts: "dot" (clip[i] = dot(row[i], p)) and "combine" (clip = sum p[j] *
row[j]). With --products it also tries row-window products (second window
applied first, e.g. view-projection x world). Writes vs-transforms.json.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def _rows(bank, base):
    return [bank[4 * (base + r): 4 * (base + r) + 4] for r in range(4)]


def _apply(rows, layout, p):
    if layout == "dot":
        return [sum(rows[i][k] * p[k] for k in range(4)) for i in range(4)]
    return [sum(p[k] * rows[k][i] for k in range(4)) for i in range(4)]


def _matmul_dot(a, b):
    # Combined "dot" rows for applying b first, then a: clip = A (B p).
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def _to_dot(rows, layout):
    return rows if layout == "dot" else [[rows[k][i] for k in range(4)] for i in range(4)]


def _sample_score(rows, layout, positions):
    inside = 0
    xs = []
    for p in positions:
        c = _apply(rows, layout, p)
        w = c[3]
        if w <= 1e-6:
            continue
        x, y, z = c[0] / w, c[1] / w, c[2] / w
        if abs(x) <= 1.05 and abs(y) <= 1.05 and -0.05 <= z <= 1.05:
            inside += 1
            xs.append(x)
    if not positions:
        return 0.0, False
    score = inside / len(positions)
    spread = (max(xs) - min(xs)) if xs else 0.0
    return score, score >= 0.5 and spread >= 0.002


def _candidate_score(get_rows, layout, samples):
    total = 0.0
    for bank, positions in samples:
        rows = get_rows(bank)
        if rows is None:
            return 0.0
        s, ok = _sample_score(rows, layout, positions)
        if not ok:
            return 0.0
        total += s
    return total / len(samples)


def _nonzero(bank, base):
    return any(v != 0.0 for v in bank[4 * base: 4 * base + 16])


def find_transform(samples, products=False, min_score=0.9):
    """samples: list of (bank, positions). Returns the best window or None."""
    if not samples:
        return None
    best = None
    for base in range(0, 253):
        if not all(_nonzero(b, base) for b, _ in samples):
            continue
        for layout in ("dot", "combine"):
            s = _candidate_score(lambda bank, b=base: _rows(bank, b), layout, samples)
            if s > (best["score"] if best else 0.0):
                best = {"base": base, "layout": layout, "score": s}
    if best and best["score"] >= min_score:
        return best
    if not products:
        return None
    best = None
    bases = [b for b in range(0, 253) if all(_nonzero(bank, b) for bank, _ in samples)]
    for a in bases:
        for b in bases:
            if a == b:
                continue
            for layout in ("dot", "combine"):
                def rows_ab(bank, a=a, b=b, layout=layout):
                    return _matmul_dot(_to_dot(_rows(bank, a), layout), _to_dot(_rows(bank, b), layout))
                s = _candidate_score(rows_ab, "dot", samples)
                if s > (best["score"] if best else 0.0):
                    best = {"base": a, "base2": b, "layout": layout, "score": s}
    return best if best and best["score"] >= min_score else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log")
    ap.add_argument("--out", required=True)
    ap.add_argument("--products", action="store_true")
    ap.add_argument("--min-score", type=float, default=0.9)
    a = ap.parse_args(argv)
    by_shader = defaultdict(list)
    with open(a.log) as fh:
        for line in fh:
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("kind") != "draw" or "bank" not in row or not row.get("positions"):
                continue
            by_shader[row["vs_hash"]].append((row["bank"], row["positions"]))
    out = Path(a.out)
    data = json.loads(out.read_text()) if out.exists() else {}
    for vs, samples in sorted(by_shader.items()):
        if data.get(vs, {}).get("manual"):
            continue
        r = find_transform(samples, a.products, a.min_score)
        if r is None:
            continue
        r["score"] = round(r["score"], 4)
        r["samples"] = len(samples)
        r.setdefault("pos_fetch", data.get(vs, {}).get("pos_fetch", -1))
        data[vs] = r
    out.write_text(json.dumps(dict(sorted(data.items())), indent=2) + "\n")
    print(f"{sum(1 for v in data.values() if 'base' in v)} shaders with transforms "
          f"({len(by_shader)} shaders sampled)")


if __name__ == "__main__":
    main()
```

`tools/xdk_sigmatch/gen_transform_table.py`:
```python
"""vs-transforms.json -> src/native/capture/vs_transform_table.inc."""
import argparse
import json
from pathlib import Path


def generate(data):
    lines = ["// Generated by tools/xdk_sigmatch/gen_transform_table.py - do not edit."]
    for vs, e in sorted(data.items(), key=lambda kv: int(kv[0], 16)):
        if "base" not in e or "base2" in e:
            continue
        layout = 0 if e["layout"] == "dot" else 1
        lines.append(f"FABLE2_VS_TRANSFORM({vs}ull, {e['base']}, {layout}, {e.get('pos_fetch', -1)})")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    Path(a.out).write_text(generate(json.loads(Path(a.json).read_text())))


if __name__ == "__main__":
    main()
```

Note: in `test_product_of_two_windows` the expected answer is `base=8` (view-projection, applied second) and `base2=20` (world, applied first). `rows_ab` computes `A(B p)` with `A = window a`, `B = window b`, so the loop finds `(8, 20)`. Product mode is O(windows^2) in pure Python: on dense real banks it takes minutes per shader, so run it only for shaders the single-window pass rejects (Task 11).

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest discover -s tests -p "test_matrix_finder.py" -v` — Expected: 5 tests OK. Then the full suite: `python -m unittest discover -s tests -p "test_*.py"` — Expected: OK.

- [ ] **Step 5: Commit**

```bash
git add tools/xdk_sigmatch/matrix_finder.py tools/xdk_sigmatch/gen_transform_table.py tests/test_matrix_finder.py
git commit -m "Tools: vertex-shader transform finder and table generator"
```

---

### Task 7: Shared XDK dispatch (census becomes an observer)

**Files:**
- Modify: `tools/xdk_sigmatch/gen_census_hooks.py` (macro name + output path), `tests/test_gen_census_hooks.py`
- Create: `src/native/capture/xdk_dispatch.h`, `src/native/capture/xdk_hooks.inc` (generated), `src/native/capture/capture.h` (observer stub)
- Delete: `src/diagnostics/fable2_d3d_census_hooks.inc`
- Modify: `src/diagnostics/fable2_d3d_census.h` (remove the macro and the `.inc` include; keep `OnCall`), `src/main.cpp` (include `xdk_dispatch.h` after `fable2_d3d_census.h`), `CMakeLists.txt` (recursive native glob + include dirs, see File Structure), `docs/native-renderer/frame-map.md` (regeneration command)

**Interfaces:**
- Consumes: `fable2::d3dcensus::OnCall(uint32_t id, const char* name, const PPCContext& ctx)` (existing).
- Produces:
  - Generated lines `FABLE2_XDK_HOOK(<id>, "<name>", <symbol>)` (same ids/names as today's census hooks; skip comments unchanged).
  - `src/native/capture/capture.h`:
    ```cpp
    namespace fable2::native::capture {
    // Called by every XDK dispatch override before the original runs.
    void OnXdkCall(uint32_t id, PPCContext& ctx, uint8_t* base);
    // Called after the original returns (draw functions record here).
    void OnXdkReturn(uint32_t id, PPCContext& ctx, uint8_t* base);
    }
    ```
    In this task both are empty inline functions in `capture.h`; Task 8 replaces them with declarations implemented in `capture.cpp`.
  - Hook ids are stable across tasks: map ids 0-35 as in `docs/native-renderer/xdk-map.json` order, extras 100-114 in `fable2_extra_hooks.json` order. `src/native/capture/xdk_hook_ids.h` (generated by the same tool in this task) defines `constexpr uint32_t kHook_<SanitizedName> = <id>;` for every emitted hook, sanitized as: `?` and `.` removed, `:` -> `_`, e.g. `kHook_D3DDevice_DrawIndexedVertices`, `kHook_DrawIndx_8221C9C8`, `kHook_TileReplay_82B9ED28`.

- [ ] **Step 1: Update the generator tests (failing)**

In `tests/test_gen_census_hooks.py`, replace every `FABLE2_D3D_CENSUS_HOOK(` in expected strings with `FABLE2_XDK_HOOK(` and add:
```python
    def test_hook_ids_header(self):
        d = Path(self.tmp.name)
        extra = d / "extra.json"
        extra.write_text(json.dumps([{"name": "TileReplay?:82BA1000", "address": "0x82BA1000"}]))
        empty = d / "empty.json"
        empty.write_text("[]")
        ids = gen.generate_ids(empty, self.init, self.src, extra)
        self.assertIn(f"constexpr uint32_t kHook_TileReplay_82BA1000 = {gen.EXTRA_ID_BASE};", ids)
        out = gen.generate_ids(self.map, self.init, self.src)
        self.assertIn("constexpr uint32_t kHook_D3DDevice_DrawIndexedVertices = 0;", out)
        self.assertIn("constexpr uint32_t kHook_D3DDevice_DrawVertices = 3;", out)  # near-miss '?' stripped
```

Run: `python -m unittest discover -s tests -p "test_gen_census_hooks.py" -v` — Expected: FAIL (old macro name; `generate_ids` missing).

- [ ] **Step 2: Update the generator**

In `tools/xdk_sigmatch/gen_census_hooks.py`: change the emitted macro to `FABLE2_XDK_HOOK`, update the header comment line to `// Generated by tools/xdk_sigmatch/gen_census_hooks.py - do not edit.` (unchanged), and add:
```python
def _sanitize(name):
    return name.replace("?", "").replace(".", "").replace(":", "_")


def generate_ids(map_path, init_path, src_dir, extra_path=None):
    """C++ header with one constexpr id per emitted hook."""
    body = generate(map_path, init_path, src_dir, extra_path)
    lines = ["// Generated by tools/xdk_sigmatch/gen_census_hooks.py - do not edit.", "#pragma once",
             "#include <cstdint>", "namespace fable2::native::capture {"]
    for m in re.finditer(r'FABLE2_XDK_HOOK\((\d+), "([^"]+)", \w+\)', body):
        lines.append(f"constexpr uint32_t kHook_{_sanitize(m.group(2))} = {m.group(1)};")
    lines.append("}  // namespace fable2::native::capture")
    return "\n".join(lines) + "\n"
```
and a `--ids-out` CLI option that writes `generate_ids(...)` when given.

Run the generator test again — Expected: OK.

- [ ] **Step 3: Regenerate the hook files**

```bash
python tools/xdk_sigmatch/gen_census_hooks.py --map docs/native-renderer/xdk-map.json --init generated/default/fable_2_init.cpp --src src --out src/native/capture/xdk_hooks.inc --ids-out src/native/capture/xdk_hook_ids.h --extra tools/xdk_sigmatch/fable2_extra_hooks.json
git rm src/diagnostics/fable2_d3d_census_hooks.inc
```
Check: `xdk_hooks.inc` has the same 45 hook lines and the same skip comments as the deleted file, with the new macro name. (The generator scans `src/` for `__imp__<symbol>` to detect existing overrides; `xdk_dispatch.h` itself contains no literal `__imp__<symbol>` strings, only the token-pasted macro, so it does not hide hooks.)

- [ ] **Step 4: Write the dispatch header and observer stub**

`src/native/capture/capture.h`:
```cpp
#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

namespace fable2::native::capture {
// Called by every XDK dispatch override before / after the original.
inline void OnXdkCall(uint32_t, PPCContext&, uint8_t*) {}
inline void OnXdkReturn(uint32_t, PPCContext&, uint8_t*) {}
}  // namespace fable2::native::capture
```

`src/native/capture/xdk_dispatch.h`:
```cpp
#pragma once

// One strong override per hooked XDK D3D function (list generated by
// tools/xdk_sigmatch/gen_census_hooks.py into xdk_hooks.inc). Observers: the
// native capture layer, then the D3D census; then the original function, then
// the capture layer's after-call hook.

#include "capture.h"
#include "fable2_d3d_census.h"

#define FABLE2_XDK_HOOK(ID, NAME, SYM)                                \
  extern "C" void __imp__##SYM(PPCContext& ctx, uint8_t* base);      \
  extern "C" void SYM(PPCContext& __restrict ctx, uint8_t* base) {   \
    fable2::native::capture::OnXdkCall(ID, ctx, base);               \
    fable2::d3dcensus::OnCall(ID, NAME, ctx);                        \
    __imp__##SYM(ctx, base);                                         \
    fable2::native::capture::OnXdkReturn(ID, ctx, base);             \
  }
#include "xdk_hooks.inc"
#undef FABLE2_XDK_HOOK
```

In `src/diagnostics/fable2_d3d_census.h` delete the `#define FABLE2_D3D_CENSUS_HOOK ...`, the `#include "fable2_d3d_census_hooks.inc"` and the `#undef` at the end of the file. In `src/main.cpp` add `#include "xdk_dispatch.h"` on the line after `#include "fable2_d3d_census.h"`. Apply the CMake changes from the File Structure section.

Note: `OnXdkReturn` runs after `__imp__`, so `ctx.r3..r10` may already be clobbered; Task 8 copies the argument registers it needs inside `OnXdkCall`.

- [ ] **Step 5: Build and verify census behaviour is unchanged**

Run: `& .\build.cmd -release fable_2` — Expected: exit 0.
Smoke (PowerShell, from `out\build\win-amd64-release`): set `$env:FABLE2_D3D_CENSUS="60"; $env:FABLE2_D3D_CENSUS_DELAY="25"`, start `.\fable_2.exe --fullscreen=false`, wait 55 s, stop it, clear both variables. Expected: the newest `logs\d3d_census_*.jsonl` has 60 rows and its `funcs` contain `DrawIndx:82217EE8` and `D3DDevice_SetStreamSource` (same names as before); the newest `logs\fable_2_*.log` has `[census] complete` and no `access violation`.

Update the regeneration command in `docs/native-renderer/frame-map.md` (Pending item 2) to the Step 3 command.

- [ ] **Step 6: Commit**

```bash
git add tools/xdk_sigmatch/gen_census_hooks.py tests/test_gen_census_hooks.py src/native/capture/capture.h src/native/capture/xdk_dispatch.h src/native/capture/xdk_hooks.inc src/native/capture/xdk_hook_ids.h src/diagnostics/fable2_d3d_census.h src/main.cpp CMakeLists.txt docs/native-renderer/frame-map.md
git commit -m "Shared XDK dispatch: capture and census observe the same overrides"
```
(`git rm` in Step 3 already staged the deleted `.inc`.)

---

### Task 8: Capture plumbing and raw discovery dumps

**Files:**
- Create: `src/native/capture/guest_read.h`, `src/native/capture/capture.cpp`
- Modify: `src/native/capture/capture.h` (declarations), `src/native/fable2_native_render.{h,cpp}` (`Install(rex::memory::Memory*)`), `src/core/fable_2_app.h:284` (pass `runtime()->memory()`), `src/diagnostics/fps_meter.h` (call `capture::OnSwap()` before `fable2::d3dcensus::OnFrame()`)
- SDK: `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp` (vertex-binding log)

**Interfaces:**
- Consumes: hook ids from `xdk_hook_ids.h` (Task 7).
- Produces:
  ```cpp
  namespace fable2::native::capture {
  void SetMemory(rex::memory::Memory* memory);      // from Install
  void OnXdkCall(uint32_t id, PPCContext& ctx, uint8_t* base);
  void OnXdkReturn(uint32_t id, PPCContext& ctx, uint8_t* base);
  void OnSwap();                                    // XDK Swap override, once per guest frame
  }
  // guest_read.h
  namespace fable2::native::capture {
  // Host pointer for [guest_virtual, +size) or nullptr if any page is not committed+readable.
  const uint8_t* ReadVirtual(uint32_t guest_virtual, uint32_t size);
  const uint8_t* ReadPhysical(uint32_t guest_physical, uint32_t size);
  uint32_t LoadBe32(const uint8_t* p);
  }
  ```
  - SDK cvar `native_render_log_vertex_bindings` (bool, default false, category "GPU"): when true, `D3D12CommandProcessor::IssueDraw` logs once per new vertex-shader `ucode_data_hash()`: `[vbind] vs=0x<hash> dwords=<n>` followed by one line per vertex binding `[vbind]   fetch=<fetch_constant> stride_dw=<stride_words> attrs=<offset_words>:<format>:<signed>:<normalized>,...` and the fetch-constant dwords for that binding `[vbind]   fc=<dword0 hex> <dword1 hex>` (`regs.GetVertexFetch(index)`).

**Behaviour (this task, raw stage only):**
- `OnXdkCall` copies `ctx.r3..ctx.r10` (u32) into a per-thread `LastArgs` for the id.
- Binding ids (`kHook_D3DDevice_SetStreamSource`, `kHook_D3DDevice_SetIndices`, `kHook_D3DDevice_SetVertexShader`, `kHook_D3DDevice_SetPixelShader`, `kHook_D3DDevice_SetPending_AluConstants`) update a `DrawState { uint32_t device; uint32_t stream_obj[4]; uint32_t stream_offset[4]; uint32_t stream_args[4][2]; uint32_t ib_obj; uint32_t vs_obj; uint32_t vs_bank_ptr; }` from those registers (SetStreamSource: r4 index (ignore >= 4), r5 object, r6 offset, r7/r8 raw; SetIndices: r4; SetVertexShader?: r4; SetPending_AluConstants?: when r5 == 0x4000 store r6).
- Draw ids (the 14 `DrawIndx*`/`D3DDevice_Draw*`/`BeginVertices` hooks) in `OnXdkReturn`: when discovery is active and this is the Nth draw (`FABLE2_NATIVE_DISCOVERY_EVERY`, default 64), write one row `{"kind":"raw","frame":F,"func":"<name>","args":[r3..r10 hex],"device":"0x..","streams":[{"index":i,"obj":"0x..","offset":n,"raw":[r7,r8],"obj_dwords":[16 hex dwords or null]}...],"ib":{"obj":"0x..","obj_dwords":[...]},"vs":{"obj":"0x..","obj_dwords":[32 hex dwords]},"vs_bank_ptr":"0x..","device_dwords":{"0x480":[24 hex dwords]}}`. Object dwords are read with `ReadVirtual` (objects live in virtual memory, e.g. `0x407F1968`); unreadable -> `null`.
- `OnSwap` increments the frame counter, arms discovery after `FABLE2_D3D_CENSUS_DELAY`-style delay `FABLE2_NATIVE_DISCOVERY_DELAY` (seconds, default 0), and stops after the requested frame count, logging `[native-discovery] writing <N> frames to <path>` / `[native-discovery] complete: ...` like the census.
- The capture layer does nothing else unless `fable2_native_render` is true or discovery is active (one relaxed atomic check per call).

`guest_read.h` readability: query `VirtualQuery` on the host range, cache the last 64 page results per thread (page base -> readable), require `MEM_COMMIT` and a readable protection; physical addresses use `memory->TranslatePhysical(addr & 0x1FFFFFFF)`, virtual `memory->TranslateVirtual(addr)`.

- [ ] **Step 1: Write a failing test for `LoadBe32` and the raw-row formatter**

Put the JSON formatting in a pure helper `src/native/capture/discovery_format.h`:
```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
namespace fable2::native::capture {
inline std::string HexDwords(const uint32_t* v, size_t n) {  // ["0x...",...] or null
  if (!v) return "null";
  std::string s = "[";
  char buf[16];
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(buf, sizeof(buf), "%s\"0x%08X\"", i ? "," : "", v[i]);
    s += buf;
  }
  return s + "]";
}
}  // namespace fable2::native::capture
```
Test `tests/native/test_discovery_format.cpp`:
```cpp
#include "../../src/native/capture/discovery_format.h"
#include <iostream>
int main() {
  const uint32_t v[2] = {0x1, 0xDEADBEEF};
  if (fable2::native::capture::HexDwords(v, 2) != "[\"0x00000001\",\"0xDEADBEEF\"]") return 1;
  if (fable2::native::capture::HexDwords(nullptr, 2) != "null") return 2;
  std::cout << "PASS: discovery format\n";
  return 0;
}
```
Add to `tests/run_native_tests.cmd` (same two-line pattern, exe name `discovery_format.exe`). Run — Expected: FAIL compiling, then write the header, run — Expected: PASS.

- [ ] **Step 2: Implement `guest_read.h`, `capture.cpp`, the declarations in `capture.h`, the Install/OnSwap wiring**

Replace the inline stubs in `capture.h` with declarations of `SetMemory`, `OnXdkCall`, `OnXdkReturn`, `OnSwap` (signatures above). `capture.cpp` includes `capture.h`, `guest_read.h`, `discovery_format.h`, `xdk_hook_ids.h`, `<rex/filesystem.h>`, `<rex/logging/macros.h>`, and holds the `DrawState`, per-thread `LastArgs`, discovery file (opened like `fable2_d3d_census.h::Log()` under `GetExecutableFolder() / "logs"`), and the counters. `fable2::native::Install` becomes `Install(rex::memory::Memory* memory)`, calls `capture::SetMemory(memory)` first (always, even when the native renderer is disabled, so discovery works), and `fable_2_app.h` passes `runtime()->memory()`. `fps_meter.h` calls `fable2::native::capture::OnSwap();` immediately before `fable2::d3dcensus::OnFrame();` (include `capture.h` there).

- [ ] **Step 3: SDK vertex-binding log**

In `thirdparty/rexglue-sdk/src/graphics/d3d12/command_processor.cpp`, define `REXCVAR_DEFINE_BOOL(native_render_log_vertex_bindings, false, "GPU", "Log each new vertex shader's vertex bindings and fetch constants once (native-renderer discovery).");` near the other cvars at the top of the file, and in `IssueDraw`, after the active vertex shader is known (`Shader* vertex_shader = active_vertex_shader();` and its analysis has run — place it right after the existing `if (!vertex_shader) { ... return false; }` check), add:
```cpp
  if (REXCVAR_GET(native_render_log_vertex_bindings)) {
    static std::mutex logged_mutex;
    static std::unordered_set<uint64_t> logged;
    std::lock_guard<std::mutex> lock(logged_mutex);
    if (logged.insert(vertex_shader->ucode_data_hash()).second) {
      REXGPU_INFO("[vbind] vs=0x{:016X} dwords={}", vertex_shader->ucode_data_hash(),
                  vertex_shader->ucode_dword_count());
      for (const auto& binding : vertex_shader->vertex_bindings()) {
        std::string attrs;
        for (const auto& a : binding.attributes) {
          attrs += fmt::format("{}{}:{}:{}:{}", attrs.empty() ? "" : ",",
                               a.fetch_instr.attributes.offset,
                               uint32_t(a.fetch_instr.attributes.data_format),
                               a.fetch_instr.attributes.is_signed ? 1 : 0,
                               a.fetch_instr.attributes.is_integer ? 0 : 1);
        }
        const auto fc = regs.GetVertexFetch(binding.fetch_constant);
        REXGPU_INFO("[vbind]   fetch={} stride_dw={} attrs={}", binding.fetch_constant,
                    binding.stride_words, attrs);
        REXGPU_INFO("[vbind]   fc=0x{:08X} 0x{:08X}", fc.dword_0, fc.dword_1);
      }
    }
  }
```
Adapt field names to `ParsedVertexFetchInstruction::Attributes` and `xe_gpu_vertex_fetch_t` as declared in `include/rex/graphics/pipeline/shader/shader.h` and `xenos.h` (read them first; record any rename in the task report). Add `#include <mutex>`, `#include <unordered_set>` if missing. This is logging only; no export changes.

- [ ] **Step 4: Build SDK and game, run an automated menu discovery capture**

Run: `& .\tools\build_runtime_sdk.cmd C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64` then `& .\build.cmd -release fable_2` — Expected: both exit 0.
From `out\build\win-amd64-release`: `$env:FABLE2_NATIVE_DISCOVERY="60"; $env:FABLE2_NATIVE_DISCOVERY_DELAY="25"; $env:FABLE2_NATIVE_DISCOVERY_EVERY="8"`, start `.\fable_2.exe --fullscreen=false --native_render_log_vertex_bindings=true`, wait 60 s, stop, clear the variables.
Expected: `logs\native_discovery_*.jsonl` with `"kind":"raw"` rows whose `streams[].obj_dwords` and `vs.obj_dwords` are mostly non-null; the game log has `[native-discovery] complete` and `[vbind] vs=` lines; no `access violation`.

- [ ] **Step 5: Commit**

SDK first:
```bash
git -C thirdparty/rexglue-sdk add src/graphics/d3d12/command_processor.cpp
git -C thirdparty/rexglue-sdk commit -m "Discovery log: per-shader vertex bindings and fetch constants"
```
Then parent:
```bash
git add src/native/capture/guest_read.h src/native/capture/capture.h src/native/capture/capture.cpp src/native/capture/discovery_format.h tests/native/test_discovery_format.cpp tests/run_native_tests.cmd src/native/fable2_native_render.h src/native/fable2_native_render.cpp src/core/fable_2_app.h src/diagnostics/fps_meter.h thirdparty/rexglue-sdk
git commit -m "Native capture: binding state, guest reads, raw discovery dumps"
```

---

### Task 9: Discovery D2 + D3 — buffer and shader object layouts

**Files:**
- Create: `src/native/capture/xdk_layout.h`, `tools/xdk_sigmatch/check_discovery.py`, `tests/test_check_discovery.py`
- Modify: `src/native/capture/capture.cpp`, `docs/native-renderer/frame-map.md` (new section "8. Guest object layouts")

**Goal:** From the Task 8 raw dumps and `[vbind]` log lines, determine and encode:
- Vertex-buffer object: which dword holds the vertex fetch constant (dword 0: type in bits 0-1 = 3, base address in bits 2-31 as physical dword address; dword 1: endian bits 0-1, size in dwords bits 2-25). The XDK stores the fetch constant inside the resource object; find the dword offset `kVbFetchDword` whose value matches the `[vbind] fc=` values for streams used by the same shader. Base physical address = `dword0 & 0xFFFFFFFC`, size bytes = `((dword1 >> 2) & 0xFFFFFF) * 4`.
- Index-buffer object: dword offsets of the physical address (`kIbAddressDword`), size in bytes (`kIbSizeDword`) and the 32-bit flag (`kIbFormatDword`, `kIbFormatMask`). Cross-check: `start + count` of sampled indexed draws fits in `size`, and the address is 4-byte aligned in physical memory below 0x20000000.
- Vertex-shader object: dword offsets of the microcode physical address (`kVsUcodeAddressDword`) and size (`kVsUcodeSizeDword`, with its unit), verified by computing `XXH3_64bits` over the guest bytes and matching a `[vbind] vs=` hash. If the `SetVertexShader?` hook's `r4` is not the shader object (it is sometimes 0 in the census), find the real binding call (look at `r5`/`r6` of the same hook, or at the vertex-shader pointer cached in the device: compare `device_dwords` across draws for a field equal to a known object). Record which register/field was used as `kVsSource` in `xdk_layout.h`.
- Device fetch-constant shadow offset (fallback path): `kDeviceVertexFetchOffset` if found.

`xdk_layout.h` shape (values are the discovered ones; every constant has a comment naming the evidence row/log line):
```cpp
#pragma once
#include <cstdint>
namespace fable2::native::capture::xdk {
// Vertex buffer object (SetStreamSource r5): vertex fetch constant at these dwords.
inline constexpr uint32_t kVbFetchDword = /* discovered */;
// Index buffer object (SetIndices r4).
inline constexpr uint32_t kIbAddressDword = /* discovered */;
inline constexpr uint32_t kIbSizeDword = /* discovered */;
inline constexpr uint32_t kIbFormatDword = /* discovered */;
inline constexpr uint32_t kIbFormatMask = /* discovered */;   // set = 32-bit indices
// Vertex shader object.
enum class VsSource : uint8_t { kHookR4, kHookR5, kHookR6, kDeviceField };
inline constexpr VsSource kVsSource = /* discovered */;
inline constexpr uint32_t kVsDeviceFieldOffset = /* discovered or 0 */;
inline constexpr uint32_t kVsUcodeAddressDword = /* discovered */;
inline constexpr uint32_t kVsUcodeSizeDword = /* discovered */;
inline constexpr uint32_t kVsUcodeSizeShift = /* 0 if bytes, 2 if dwords */;
}
```
A layout constant that cannot be confirmed is a BLOCKED report with the evidence gathered, not a guess.

**`check_discovery.py`** (the cross-check tool, TDD): `python tools\xdk_sigmatch\check_discovery.py <discovery.jsonl> <game.log>` parses `[vbind]` blocks into `{vs_hash: [{"fetch", "stride_dw", "attrs": [(offset, format, signed, normalized)], "fc": (d0, d1)}]}` and `"kind":"draw"` rows (written after this task, see Step 4) with `vs_hash`, `pos` (`fetch_slot`, `offset_bytes`, `stride_bytes`, `format`) and `vb` (`phys_addr`, `size`); it reports, per sampled draw, whether the decoded position element matches an attribute of the same shader's binding with the same fetch slot (`offset_bytes == 4*offset`, `stride_bytes == 4*stride_dw`, same format) and whether `vb.phys_addr == fc.d0 & ~3`. Exit code 0 when at least 20 sampled draws were checked and all matched.

- [ ] **Step 1: Failing tests for `check_discovery.py`**

`tests/test_check_discovery.py`:
```python
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cd", ROOT / "tools" / "xdk_sigmatch" / "check_discovery.py")
cd = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cd)

LOG = """[2026-10-01 10:00:00.000] [info] [gpu] [vbind] vs=0x00000000000000AB dwords=120
[2026-10-01 10:00:00.000] [info] [gpu] [vbind]   fetch=1 stride_dw=8 attrs=0:57:1:1,3:6:0:1
[2026-10-01 10:00:00.000] [info] [gpu] [vbind]   fc=0x10002003 0x00000802
"""


def draw(ok=True):
    return {"kind": "draw", "vs_hash": "0x00000000000000AB",
            "pos": {"fetch_slot": 1, "offset_bytes": 0, "stride_bytes": 32, "format": 57},
            "vb": {"phys_addr": 0x10002000 if ok else 0x10003000, "size": 2048}}


class CheckDiscoveryTests(unittest.TestCase):
    def test_parse_vbind(self):
        b = cd.parse_vbind(LOG.splitlines())
        self.assertEqual(b["0x00000000000000AB"][0]["stride_dw"], 8)
        self.assertEqual(b["0x00000000000000AB"][0]["attrs"][0], (0, 57, 1, 1))
        self.assertEqual(b["0x00000000000000AB"][0]["fc"], (0x10002003, 0x00000802))

    def test_matches(self):
        b = cd.parse_vbind(LOG.splitlines())
        self.assertEqual(cd.check_rows([draw()] * 20, b), (20, 20))
        self.assertEqual(cd.check_rows([draw(), draw(ok=False)], b), (2, 1))

    def test_cli_exit_code(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "g.log").write_text(LOG)
            (d / "x.jsonl").write_text("\n".join(json.dumps(draw()) for _ in range(20)) + "\n")
            self.assertEqual(cd.main([str(d / "x.jsonl"), str(d / "g.log")]), 0)
            (d / "y.jsonl").write_text("\n".join(json.dumps(draw()) for _ in range(5)) + "\n")
            self.assertEqual(cd.main([str(d / "y.jsonl"), str(d / "g.log")]), 1)


if __name__ == "__main__":
    unittest.main()
```
Run: `python -m unittest discover -s tests -p "test_check_discovery.py" -v` — Expected: FAIL (file missing).

- [ ] **Step 2: Implement `check_discovery.py`**

```python
"""Cross-check native discovery rows against the SDK's [vbind] log."""
import argparse
import json
import re

VS = re.compile(r"\[vbind\] vs=(0x[0-9A-Fa-f]+) dwords=(\d+)")
BIND = re.compile(r"\[vbind\]\s+fetch=(\d+) stride_dw=(\d+) attrs=(\S*)")
FC = re.compile(r"\[vbind\]\s+fc=(0x[0-9A-Fa-f]+) (0x[0-9A-Fa-f]+)")


def parse_vbind(lines):
    out, cur = {}, None
    for line in lines:
        if m := VS.search(line):
            cur = out.setdefault(m.group(1), [])
        elif (m := BIND.search(line)) and cur is not None:
            attrs = [tuple(int(x) for x in a.split(":")) for a in m.group(3).split(",") if a]
            cur.append({"fetch": int(m.group(1)), "stride_dw": int(m.group(2)), "attrs": attrs, "fc": None})
        elif (m := FC.search(line)) and cur:
            cur[-1]["fc"] = (int(m.group(1), 16), int(m.group(2), 16))
    return out


def _match(row, bindings):
    p, vb = row["pos"], row["vb"]
    for b in bindings:
        if b["fetch"] != p["fetch_slot"] or 4 * b["stride_dw"] != p["stride_bytes"]:
            continue
        if not any(4 * off == p["offset_bytes"] and fmt == p["format"] for off, fmt, *_ in b["attrs"]):
            continue
        if b["fc"] is None or (b["fc"][0] & ~3) == vb["phys_addr"]:
            return True
    return False


def check_rows(rows, vbind):
    checked = matched = 0
    for r in rows:
        if r.get("kind") != "draw" or "pos" not in r or r["vs_hash"] not in vbind:
            continue
        checked += 1
        matched += _match(r, vbind[r["vs_hash"]])
    return checked, matched


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("discovery")
    ap.add_argument("game_log")
    a = ap.parse_args(argv)
    with open(a.game_log, errors="ignore") as fh:
        vbind = parse_vbind(fh)
    with open(a.discovery) as fh:
        rows = [json.loads(l) for l in fh if l.strip()]
    checked, matched = check_rows(rows, vbind)
    print(f"checked {checked} sampled draws, {matched} matched")
    return 0 if checked >= 20 and matched == checked else 1


if __name__ == "__main__":
    raise SystemExit(main())
```
Note on the vbind `fc` match: `vb.phys_addr` is the fetch-constant base; the XDK may add the `SetStreamSource` offset separately — compare the base only (the offset stays in `DrawState::stream_offset` and is added when reading). Run the test — Expected: OK.

- [ ] **Step 3: Determine the layouts**

Analyse the Task 8 dump and `[vbind]` lines (write throwaway scripts in the scratchpad, not in the repo). Fill `xdk_layout.h` with the confirmed values and evidence comments. Write frame-map section 8 with a table: constant, value, evidence.

- [ ] **Step 4: Emit decoded "draw" rows**

In `capture.cpp`, for each sampled discovery draw, also write a `"kind":"draw"` row: `vs_hash` (`"0x%016llX"` of `XXH3_64bits` over the guest ucode bytes, exactly as read from guest memory, not byte-swapped — the SDK hashes the same bytes), `pos` (the `SelectPosition(DecodeVertexFetches(...), -1, ...)` result: `fetch_slot`, `offset_bytes`, `stride_bytes`, `format` as the xenos value), `vb` (`phys_addr`, `size` from the stream whose fetch constant slot matches `pos.fetch_slot` — the stream index for slot `s` is resolved by matching the object's fetch constant to the stream bindings; record `"stream": i`), `bank` (256x4 floats read from `vs_bank_ptr`, byte-swapped to host floats), `positions` (the first 64 vertices decoded with `DecodePositions` from `ReadPhysical(vb.phys_addr + stream_offset, vb.size)`), and `viewport: [1120, 720]`. Include `<xxhash.h>` (available from the SDK include directory).

- [ ] **Step 5: Build, capture, cross-check**

Build the game, run the Task 8 capture command again (menu, 60 frames, every 8th draw, with `--native_render_log_vertex_bindings=true`), then:
`python tools\xdk_sigmatch\check_discovery.py <newest native_discovery_*.jsonl> <newest fable_2_*.log>` — Expected: exit 0, `checked N sampled draws, N matched` with N >= 20.

- [ ] **Step 6: Commit**

```bash
git add src/native/capture/xdk_layout.h src/native/capture/capture.cpp tools/xdk_sigmatch/check_discovery.py tests/test_check_discovery.py docs/native-renderer/frame-map.md
git commit -m "Native discovery D2/D3: buffer and vertex-shader object layouts"
```

---

### Task 10: Discovery D1 — main-scene bracket

**Files:**
- Modify: `src/native/capture/xdk_layout.h`, `src/native/capture/capture.cpp`, `docs/native-renderer/frame-map.md` (section 8), possibly `tools/xdk_sigmatch/fable2_extra_hooks.json` + regenerated `xdk_hooks.inc`/`xdk_hook_ids.h` (if a new hook is needed)

**Goal:** Decide, for every guest draw call, whether it belongs to the tiled main-scene pass, and drive `FrameBuilder::Open/Close` from it.

Method (in order; stop at the first that meets the acceptance check):
1. **Device tiling flag.** Six of the seven `DRAW_INDX` builders emit a `SET_BIN_MASK_LO` header only while tiling is active (frame-map section 6b), so they test a device field first. Disassemble `0x8221E0F0` around its `SET_BIN_MASK_LO` site (`python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 <addr> 40`, the site address is printed by `pm4_emitters.py`) and find the `lwz`/`rlwinm`/`cmp` on a `r3`-relative (device) field that guards it. Encode `kDeviceTilingFlagOffset` and `kDeviceTilingFlagMask` in `xdk_layout.h`; in `OnXdkReturn` for draw ids read the field (`ReadVirtual(device + offset, 4)`) and `Open()`/`Close()` on transitions before recording.
2. **Begin-tiling hook.** If the field is not found, locate the XDK BeginTiling function: a static caller of `0x822A6318`/`0x82B9E848` neighbours called once per frame before the main-scene draws (the census `TileReplay?:82B9ED28` is the end). Add it to `fable2_extra_hooks.json` as `TileBegin:<addr>`, regenerate the hook files (Task 7 Step 3 command), `Open()` on it and `Close()` on `TileReplay?:82B9ED28`.

Acceptance check (menu capture, automated; record numbers in frame-map section 8): with discovery active, add `"main_scene"` to the meta row per frame (`{"kind":"frame","frame":F,"captured":N,"in_bracket":M}`); over 60 menu frames `in_bracket` is non-zero every frame and the bracket opens and closes at least once per frame; the census of the same run (`FABLE2_D3D_CENSUS=60`) shows pitch 1280 as the menu's tiled target (frame-map section 2a) — `in_bracket` should be within 20% of the census `pred_draws / 2.4` estimate for that run. Gameplay confirmation is part of Task 14.

- [ ] **Step 1:** Run the disassembly and identify the guard; write the evidence (instruction addresses) into frame-map section 8.
- [ ] **Step 2:** Encode the constants (or the new hook) and the Open/Close logic; build.
- [ ] **Step 3:** Run the menu capture with both `FABLE2_NATIVE_DISCOVERY=60` and `FABLE2_D3D_CENSUS=60` (same delay 25 s); check the acceptance numbers.
- [ ] **Step 4: Commit**

```bash
git add src/native/capture/xdk_layout.h src/native/capture/capture.cpp docs/native-renderer/frame-map.md
git commit -m "Native discovery D1: main-scene bracket"
```
(Add `tools/xdk_sigmatch/fable2_extra_hooks.json src/native/capture/xdk_hooks.inc src/native/capture/xdk_hook_ids.h` if method 2 was used.)

---

### Task 11: Discovery D4 — transforms, and full draw records

**Files:**
- Create: `docs/native-renderer/vs-transforms.json`, `src/native/capture/vs_transform_table.inc` (generated)
- Modify: `src/native/capture/capture.cpp` (records), `src/native/capture/capture.h` (`Publisher()` accessor), `docs/native-renderer/frame-map.md` (section 9 "Transforms")

**Interfaces:**
- Consumes: `AssembleRecord`, `FrameBuilder`, `ScenePublisher` (Task 4); `DecodeVertexFetches`/`SelectPosition` (Task 3); layouts (Tasks 9, 10).
- Produces:
  ```cpp
  namespace fable2::native::capture {
  render::ScenePublisher& Publisher();   // FrameScene published at each OnSwap
  }
  ```
  `vs_transform_table.inc` is included in `capture.cpp` as:
  ```cpp
  struct TableEntry { uint64_t hash; uint32_t base; uint8_t layout; int pos_fetch; };
  static constexpr TableEntry kTransformTable[] = {
  #define FABLE2_VS_TRANSFORM(H, B, L, P) {H, B, L, P},
  #include "vs_transform_table.inc"
  #undef FABLE2_VS_TRANSFORM
  };
  ```
  looked up through an `std::unordered_map<uint64_t, TransformInfo>` built once.

- [ ] **Step 1: Run the matrix finder on a menu capture**

Capture as in Task 9 Step 5 (60 frames, every 8th draw). Run:
`python tools\xdk_sigmatch\matrix_finder.py <newest native_discovery_*.jsonl> --out docs\native-renderer\vs-transforms.json`
If fewer than half of the sampled shaders resolve, rerun with `--products` and record which shaders needed it.
Then: `python tools\xdk_sigmatch\gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc`.

- [ ] **Step 2: Build full records**

In `capture.cpp` `OnXdkReturn` for draw ids, while the bracket is open (and only when `fable2_native_render` is true): fill `DrawInputs` (shader from `kVsSource`, ucode via `ReadPhysical`, hash with `XXH3_64bits`, position via `SelectPosition` with the table's `pos_fetch`, stream resolved by fetch slot, VB/IB from the `xdk_layout.h` dwords, `transform` from the table, `bank` = 1024 host floats read from `vs_bank_ptr` into a per-thread buffer), call `AssembleRecord(in, builder.NextSeq())` and `builder.Add(...)`. Cache per vertex-shader object address: `{hash, fetches, have_pos, pos}` (cleared when the object's ucode address changes) so steady-state draws do not re-hash microcode. `OnSwap` calls `Publisher().Publish(builder.Finish(frame))`. Draw-argument mapping per function family: `DrawIndexedVertices` (`r4` prim, `r5` base vertex, `r6` start index, `r7` index count, indexed); `DrawVertices` (`r4` prim, `r5` start vertex, `r6` vertex count, not indexed — verify against the census `args` samples for `D3DDevice_DrawVertices?` before relying on it, and record the confirmed mapping in frame-map section 8); the other `DrawIndx*` hooks: record with `skip = kUnsupportedPrim` unless their argument mapping was confirmed during Task 9 (record what was confirmed in frame-map section 8).

- [ ] **Step 3: Coverage check (automated, menu)**

Add a per-frame log line every 300 frames when the native renderer is enabled: `[native] capture: frame F captured C drawable D skipped {reason: n, ...}`. Build, run the menu for 60 s with `--fable2_native_render=true` (view off). Expected: `D / C >= 0.9` in the menu's 3D scene lines, or the shortfall explained by reason counts in frame-map section 9 with the next action (more samples, products, manual entries).

- [ ] **Step 4: Commit**

```bash
git add docs/native-renderer/vs-transforms.json src/native/capture/vs_transform_table.inc src/native/capture/capture.cpp src/native/capture/capture.h docs/native-renderer/frame-map.md
git commit -m "Native capture D4: transform table and full draw records"
```

---

### Task 12: Geometry cache (GPU) and clay pass

**Files:**
- Create: `src/native/render/geometry_cache.{h,cpp}`, `src/native/render/clay_pass.{h,cpp}`
- Modify: `src/native/fable2_native_shaders.h` (clay HLSL), `src/native/fable2_native_render.cpp` (cvars + call the pass)
- SDK: `include/rex/ui/overlay/debug_overlay.h` (`FrameStats::extra_text`), `src/ui/overlay/debug_overlay.cpp` (render it)

**Interfaces:**
- Consumes: `FrameScene` (Task 4), `GeometryCacheIndex` (Task 5), decoders (Tasks 1, 2), `ReadPhysical` (Task 8), `capture::Publisher()` (Task 11).
- Produces:
  ```cpp
  namespace fable2::native::render {
  struct ClayStats { uint32_t drawn = 0, skipped_bad_index = 0, uploads = 0, hits = 0;
                     uint64_t resident_bytes = 0; double hash_ms = 0, decode_ms = 0, record_ms = 0; };
  enum class ClayColor : uint8_t { kClay, kDraw, kShader };
  class GeometryCache {
   public:
    // Returns the GPU buffer holding float4 positions / uint32 indices, or nullptr (draw skipped).
    nrhi::Buffer* Positions(nrhi::Device* dev, const capture::DrawRecord& r, ClayStats& st);
    nrhi::Buffer* Indices(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t vertex_count,
                          uint32_t* index_count, ClayStats& st);
    void BeginFrame(uint64_t frame, uint64_t budget_bytes);
    void Release(nrhi::Device* dev);  // DestroyDeferred all
  };
  class ClayPass {
   public:
    bool Ensure(nrhi::Device* dev);   // targets 1120x720 (R8G8B8A8 color + D32 depth), layout, shaders, pipeline
    void Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color, ClayStats& st);
    nrhi::Texture* color() const;     // left in kPixelShaderResource after Render
  };
  }
  ```
  - SDK: `rex::ui::FrameStats` gains `std::string extra_text;` rendered with `ImGui::TextUnformatted` (each `\n`-separated line) after the Verdict line when non-empty.

Clay HLSL (`kClayVs`, `kClayPs` in `fable2_native_shaders.h`):
```hlsl
cbuffer Draw : register(b0) {
  float4 r0; float4 r1; float4 r2; float4 r3;   // captured rows
  uint layout;        // 0 dot, 1 combine
  int base_vertex;
  uint vertex_count;  // positions in the buffer
  uint color;         // 0xRRGGBB
};
StructuredBuffer<float4> positions : register(t0);
StructuredBuffer<uint> indices : register(t1);
struct VsOut { float4 pos : SV_Position; float3 ndc : TEXCOORD0; };
VsOut main(uint vid : SV_VertexID) {
  VsOut o;
  int v = int(indices[vid]) + base_vertex;
  float4 p = (v >= 0 && uint(v) < vertex_count) ? positions[v] : float4(0, 0, 0, 0);
  float4 c = layout == 0 ? float4(dot(r0, p), dot(r1, p), dot(r2, p), dot(r3, p))
                         : p.x * r0 + p.y * r1 + p.z * r2 + p.w * r3;
  o.pos = c;
  o.ndc = float3(c.xy / max(abs(c.w), 1e-6), c.w * 0.01);
  return o;
}
```
```hlsl
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex; uint vertex_count; uint color; };
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float shade = 0.35 + 0.65 * saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * shade, 1.0);
}
```
Binding layout: param 0 `kConstants` b0 count 20 visibility all; param 1 `kBufferSrv` t0; param 2 `kBufferSrv` t1; `allow_input_layout = false`. Pipeline: `rtv_format = kR8G8B8A8_UNORM`, `dsv_format = kD32_FLOAT`, depth test+write `kLess`, cull none, topology triangle list, `Draw(index_count, 0)`.

Per draw (`Render`): skip if `Positions` or `Indices` is null; root constants = rows, layout, base_vertex, vertex_count, color (`kClay` 0xB8B0A0; `kDraw` hash of `seq`; `kShader` hash of `vs_hash`, both `0x404040 | (h & 0xBFBFBF)`); `SetBufferSrv(1, positions, 0)`, `SetBufferSrv(2, indices, 0)`. Targets: clear color 0.08/0.08/0.1/1, depth 1.0; viewport 1120x720; transitions `kPixelShaderResource -> kRenderTarget` (color) and `kDepthWrite` (depth) at the start, back to `kPixelShaderResource` at the end, with `FlushBarriers()` after each group.

`GeometryCache`:
- Positions key `{vb.phys_addr, vb.size, pos.stride_bytes, extra = (offset_bytes << 8) | format | (exp_adjust & 0xFF) << 16, kind 0}`; content hash `XXH3_64bits(ReadPhysical(addr, size), size)`; on miss decode all `size / stride` vertices with `DecodePositions` into a `std::vector<Float4>`, create an upload buffer (`HeapKind::kUpload`, `BufferBindClass::kFull`, size = 16 * vertex_count rounded up to 256), `Map`, copy, keep mapped.
- Indices key `{ib.phys_addr or 0, ib.size or 0, stride = index32, extra = hash32(prim, start, count, indexed), kind 1}`; content hash over the referenced index bytes (`start*width .. (start+count)*width`), or 0 for non-indexed; on miss `BuildTriangleList`, then reject (stats `skipped_bad_index`) if `max_index + base_vertex >= vertex_count` or `base_vertex + max_index < 0`; upload as uint32.
- Evicted ids -> `DestroyDeferred` on their buffers. Budget from `fable2_native_geometry_budget_mb`.
- Timings: `hash_ms`, `decode_ms` with `steady_clock`.

- [ ] **Step 1: SDK `extra_text` (TDD not applicable: ImGui rendering); implement, build SDK.**
- [ ] **Step 2: Implement `GeometryCache`, `ClayPass`, shaders.** `ClayPass::Ensure` failure (shader compile, pipeline, target creation) calls the existing `Fail("clay pass")` and the callback returns without drawing. Until Task 13 lands, the old overlay grid still draws when the post-processor runs; that is expected. In `fable2_native_render.cpp` add the `fable2_native_clay_color` and `fable2_native_geometry_budget_mb` cvars, and in `OverlayCallback` (post-processor) when the native renderer is enabled and a scene exists: `Ensure`, `Render` (the composite is added in Task 13; for this task, call `Render` only and leave the guest output untouched). Fill `FrameStats::extra_text` from the latest `ClayStats` + `FrameScene` counts: `Native: captured C, drawn D, skipped S (top: reason n, reason n, reason n)` and `Geometry: U uploads, H hits, R MB resident | hash X ms, decode Y ms, record Z ms` (provider in `fable_2_app.h` reads an atomically published copy).
- [ ] **Step 3: Build and smoke.** SDK build, game build, run the menu 60 s with `--fable2_native_render=true --fable2_native_view=off` (the view cvar arrives in Task 13; until then the pass runs whenever the renderer is enabled). Expected: no crash or failure-latch log line; the `[native] capture:` lines continue; F3 extra lines present (check visually only if a human is available; otherwise check the log line `[native] clay: drawn D` added every 300 frames).
- [ ] **Step 4: Commit** (SDK first, then parent with the submodule bump):

```bash
git -C thirdparty/rexglue-sdk add include/rex/ui/overlay/debug_overlay.h src/ui/overlay/debug_overlay.cpp
git -C thirdparty/rexglue-sdk commit -m "F3: optional extra text lines from the app"
git add src/native/render/geometry_cache.h src/native/render/geometry_cache.cpp src/native/render/clay_pass.h src/native/render/clay_pass.cpp src/native/fable2_native_shaders.h src/native/fable2_native_render.cpp src/core/fable_2_app.h thirdparty/rexglue-sdk
git commit -m "Native render: geometry cache and clay pass"
```

---

### Task 13: Debug views, F6 cycling and controls

**Files:**
- Create: `src/native/render/composite.{h,cpp}`
- Modify: `src/native/native_render_state.h` (`View`), `tests/native/test_native_render_state.cpp`, `src/native/fable2_native_shaders.h` (composite PS), `src/native/fable2_native_render.cpp`, `README.md` (Experimental native renderer section), `src/core/fable2_config.*` only if it references the removed cvars

**Interfaces:**
- Produces (in `native_render_state.h`, replacing `Mode`/`ParseMode`):
  ```cpp
  enum class View { kOff, kOverlay, kSplit, kNative, kPattern };
  struct ParsedView { View view; bool recognized; };
  ParsedView ParseView(std::string_view text);   // case/space-insensitive; unknown -> kOff, recognized=false
  View NextView(View v);                          // off -> overlay -> split -> native -> pattern -> off
  const char* ViewName(View v);
  ```
  `composite.h`:
  ```cpp
  namespace fable2::native::render {
  class Composite {
   public:
    bool Ensure(nrhi::Device* dev, nrhi::Format out_format);
    // Draws the clay image over the guest output (which stays the emulated frame underneath).
    void Draw(nrhi::Cmd* cmd, const NativeGuestOutputRenderContext& ctx, nrhi::Texture* clay, View view);
  };
  }
  ```

- [ ] **Step 1: Failing test for `View`**

Replace the `Mode` cases in `tests/native/test_native_render_state.cpp` with:
```cpp
  using fable2::native::View;
  auto pv = fable2::native::ParseView("  Split ");
  if (pv.view != View::kSplit || !pv.recognized) return 1;
  pv = fable2::native::ParseView("bogus");
  if (pv.view != View::kOff || pv.recognized) return 2;
  if (fable2::native::ParseView("pattern").view != View::kPattern) return 3;
  View v = View::kOff;
  const View expect[] = {View::kOverlay, View::kSplit, View::kNative, View::kPattern, View::kOff};
  for (View e : expect) { v = fable2::native::NextView(v); if (v != e) return 4; }
  if (std::string(fable2::native::ViewName(View::kNative)) != "native") return 5;
```
(keep the existing `EdgeDetector` and `FailureLatch` cases). Run `& .\tests\run_native_tests.cmd` — Expected: FAIL compiling (`View` missing).

- [ ] **Step 2: Implement `View` helpers** (same parsing style as the existing `ParseMode`; remove `Mode`, `ParsedMode`, `ParseMode`). Run the tests — Expected: PASS.

- [ ] **Step 3: Composite pass and controls**

Composite PS (`kCompositePs`), full-screen triangle with the existing `kFullscreenVs`; binding layout: param 0 `kConstants` b0 count 4 (pixel), param 1 `kTextureTable` t0 count 1 (pixel), static sampler s0 linear clamp:
```hlsl
cbuffer C : register(b0) { float out_w; float out_h; uint mode; float alpha; };
Texture2D clay : register(t0);
SamplerState s : register(s0);
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 uv = pos.xy / float2(out_w, out_h);
  if (mode == 2 && uv.x < 0.5) discard;   // split: left half stays emulated
  return float4(clay.Sample(s, uv).rgb, alpha);
}
```
Blend: src alpha / inv src alpha. `mode`: 1 overlay (alpha 0.5), 2 split (alpha 1), 3 native (alpha 1). The clay color texture is sampled in `kPixelShaderResource`; the guest output follows the existing `kGuestOutput -> kRenderTarget -> kGuestOutput` transitions with `FlushBarriers()` (as `DrawFullscreen` does today).

`fable2_native_render.cpp`:
- Remove `fable2_native_render_mode` and `fable2_native_render_active`; add `REXCVAR_DEFINE_STRING(fable2_native_view, "off", "Fable2", "Native debug view: off, overlay, split, native, pattern (F6 cycles).")`.
- `RenderCallback` returns true only for `View::kPattern` (existing test pattern replaces the frame).
- `OverlayCallback` (post-processor): for overlay/split/native, if a scene exists, `ClayPass::Render` then `Composite::Draw`; for off, return without recording anything.
- `PollFrame`: F6 edge -> if the latch failed, clear it and keep the current view (log `[native] F6: retrying native renderer`); else set `fable2_native_view` to `ViewName(NextView(current))` and log `[native] F6: view <name>`. `RequestNativeGuestOutputPostProcess(view is overlay/split/native && !failed)`.
- Unknown view string: one `REXLOG_WARN("[native] unknown fable2_native_view '{}', using off", ...)`.
- `Install` log line: `[native] native renderer installed (view=<name>, F6 cycles views)`.

README: replace the native renderer paragraph's mode text with: start with `--fable2_native_render=true` (restart required), choose a view with `--fable2_native_view=overlay|split|native|pattern` or cycle with F6; clay geometry is a debug view and the emulated frame keeps rendering.

- [ ] **Step 4: Build and smoke**

Game build; run the menu 60 s with `--fable2_native_render=true --fable2_native_view=split`. Expected: no crash, no latch failure in the log, `[native] clay: drawn D` lines with D > 0 once the 3D menu scene shows. Run 30 s with `--fable2_native_view=off` and check the log has no `[native] clay:` lines (nothing drawn).

- [ ] **Step 5: Commit**

```bash
git add src/native/render/composite.h src/native/render/composite.cpp src/native/native_render_state.h tests/native/test_native_render_state.cpp src/native/fable2_native_shaders.h src/native/fable2_native_render.cpp README.md
git commit -m "Native render: debug views, composite, F6 view cycling"
```

---

### Task 14: Validation and documentation

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (section 10 "Clay pass validation"), `docs/superpowers/specs/2026-10-01-native-renderer-clay-pass-design.md` (status line only)

- [ ] **Step 1: Automated checks** (agent): full native and Python test suites green; game build exit 0; 10-minute menu run with `--fable2_native_render=true --fable2_native_view=overlay` without crash or latch failure (record the last `[native] capture:` and `[native] clay:` lines); A/B capture overhead in gameplay with the guest-thread capture timer: three runs of about 120 s each via `.\tools\drive_game.ps1 -Total 120` (in-process autoplay; world fully up at about 50 s) with `--fable2_native_render=false` (plus `-Env @{FABLE2_GUEST_WORK_LOG="1"}`), `--fable2_native_render=true --fable2_native_view=off`, and `--fable2_native_render=true --fable2_native_view=split`; compare the `[frame] guest` work medians and the `[native] capture:` `capture` median — expected < 0.5 ms per frame with the view off.
- [ ] **Step 2: User checks** (hand to the user as a checklist; the task completes when the user reports back):
  1. `--fable2_native_render=true --fable2_native_view=split`, then F6 through overlay/native at three spots (town, open field, interior): clay lines up with the emulated image (same camera, shapes in place within a couple of pixels).
  2. F3 in the world: `Native: captured C, drawn D` with D/C >= 0.9; note the top skip reasons.
  3. 10 minutes of play with the view on: no crash, no latch failure; include at least one area transition (walk through a loading boundary).
  4. With the view off: capture overhead visible in F3 guest work time vs a run with `--fable2_native_render=false` (< 0.5 ms difference).
  5. With the view off: the image looks exactly like a normal run; the suppression and census flags still work.
  A gameplay discovery capture (`FABLE2_NATIVE_DISCOVERY=300`, `FABLE2_NATIVE_DISCOVERY_DELAY=90`) plus a rerun of `matrix_finder.py` and `gen_transform_table.py` is the follow-up if coverage is below 90% in the world.
- [ ] **Step 3:** Record results in frame-map section 10 and set the spec status line to `Status: implemented (sub-project 3), validation results in docs/native-renderer/frame-map.md section 10.`
- [ ] **Step 4: Commit**

```bash
git add docs/native-renderer/frame-map.md docs/superpowers/specs/2026-10-01-native-renderer-clay-pass-design.md
git commit -m "Clay pass validation results"
```
