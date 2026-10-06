#pragma once

// UV vertex elements -> float2 (pure: no SDK/GPU deps). A table entry names
// the vertex fetch and the two GPU source components the shader routes into
// the albedo coordinates (vs-transforms.json "uv"); the scale/offset is
// applied later, per draw, from shader constants. u and v usually share one
// element; the instancing shaders (frame-map section 12) take them from two
// elements of the same vertex.

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
  uint32_t offset_bytes = 0;  // of the element holding u, within one vertex
  // Bytes from that element to the one holding v (same format); 0: one element.
  int32_t v_element_delta = 0;
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

// u from `fu` and v from `fv`: one fetch, or two elements of one vertex, which
// must share the stream, the stride and the element format.
inline bool UvLayoutFromFetches(const VertexFetch& fu, const VertexFetch& fv, uint8_t comp_u,
                                uint8_t comp_v, UvLayout* out) {
  UvLayout l, lv;
  if (!UvLayoutFromFetch(fu, comp_u, 0, &l) || !UvLayoutFromFetch(fv, 0, comp_v, &lv)) return false;
  if (lv.fetch_slot != l.fetch_slot || lv.stride_bytes != l.stride_bytes || lv.format != l.format ||
      lv.is_signed != l.is_signed || lv.normalized != l.normalized || lv.exp_adjust != l.exp_adjust) {
    return false;
  }
  l.comp_v = comp_v;
  l.v_element_delta = int32_t(lv.offset_bytes) - int32_t(l.offset_bytes);
  *out = l;
  return true;
}

// Bytes from the start of a vertex to the end of its UV element (the later
// one when u and v sit in two elements); 0 for a layout that cannot be read.
inline uint32_t UvElementEnd(const UvLayout& l) {
  const uint32_t n = UvComponents(l.format);
  const int64_t u_offset = l.offset_bytes;
  const int64_t v_offset = u_offset + l.v_element_delta;
  if (n == 0 || v_offset < 0 || v_offset > 0x7FFFFFFF || u_offset > 0x7FFFFFFF) return 0;
  return uint32_t(v_offset > u_offset ? v_offset : u_offset) + n * (UvSixteen(l.format) ? 2u : 4u);
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
  const uint32_t end = UvElementEnd(l);  // 0: the v element would start before the vertex
  if (!src || n == 0 || l.stride_bytes == 0 || end == 0) return false;
  const uint32_t mu = l.swap16 && UvSixteen(l.format) ? (l.comp_u ^ 1u) : l.comp_u;
  const uint32_t mv = l.swap16 && UvSixteen(l.format) ? (l.comp_v ^ 1u) : l.comp_v;
  const uint32_t v_offset = uint32_t(int64_t(l.offset_bytes) + l.v_element_delta);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t vertex = uint64_t(first_vertex + i) * l.stride_bytes;
    if (vertex + end > src_size) return false;
    out[i] = {detail::UvComponent(src + vertex + l.offset_bytes, l, mu),
              detail::UvComponent(src + vertex + v_offset, l, mv)};
  }
  return true;
}

}  // namespace fable2::native::capture
