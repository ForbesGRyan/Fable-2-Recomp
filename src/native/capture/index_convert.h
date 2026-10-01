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
