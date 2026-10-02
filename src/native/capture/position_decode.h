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
  // Fetch destination swizzle, 3 bits per output component (x in bits 0-2):
  // 0-3 = source component, 4 = 0.0, 5 = 1.0, 7 = not written (taken as 0.0
  // for x/y/z and 1.0 for w). Default xyzw.
  uint32_t swizzle = 0x688;
  // 16-bit components are pair-swapped within each dword (fetch constant
  // endian 8in32): the GPU's source components are (m1, m0, m3, m2) of the
  // big-endian memory order m. Ignored for 32-bit components.
  bool swap16 = false;
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

// Sets swap16 from the stream's fetch constant endian (dword 1 bits 0-1:
// 0 none, 1 8in16, 2 8in32, 3 16in32). The decoder reads big-endian
// components, which is what the GPU sees for 16-bit components under 8in16
// (memory order) or 8in32 (pairs swapped) and for 32-bit components under
// 8in32. Returns false for any other combination.
inline bool ApplyFetchEndian(PosLayout* l, uint32_t endian) {
  const bool sixteen = l->format == PosFormat::kHalf4 || l->format == PosFormat::kShort4;
  if (sixteen && (endian == 1 || endian == 2)) {
    l->swap16 = endian == 2;
    return true;
  }
  if (!sixteen && l->format != PosFormat::kUnknown && endian == 2) {
    l->swap16 = false;
    return true;
  }
  return false;
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
    float m[4];  // memory order
    switch (layout.format) {
      case PosFormat::kFloat3:
        m[0] = detail::BeFloat(p); m[1] = detail::BeFloat(p + 4); m[2] = detail::BeFloat(p + 8);
        m[3] = 1.0f;
        break;
      case PosFormat::kFloat4:
        for (int c = 0; c < 4; ++c) m[c] = detail::BeFloat(p + 4 * c);
        break;
      case PosFormat::kHalf4:
        for (int c = 0; c < 4; ++c) m[c] = HalfToFloat(detail::Be16(p + 2 * c));
        break;
      case PosFormat::kShort4:
        for (int c = 0; c < 4; ++c) m[c] = detail::Short(p + 2 * c, layout);
        break;
      default:
        return false;
    }
    const bool pairs = layout.swap16 && (layout.format == PosFormat::kHalf4 ||
                                         layout.format == PosFormat::kShort4);
    float v[4];
    for (int c = 0; c < 4; ++c) {
      const uint32_t sel = (layout.swizzle >> (3 * c)) & 7;
      if (sel < 4) {
        v[c] = m[pairs ? (sel ^ 1) : sel];
      } else if (sel == 4) {
        v[c] = 0.0f;
      } else if (sel == 5) {
        v[c] = 1.0f;
      } else {
        v[c] = c == 3 ? 1.0f : 0.0f;
      }
    }
    out[i] = {v[0], v[1], v[2], v[3]};
  }
  return true;
}

}  // namespace fable2::native::capture
