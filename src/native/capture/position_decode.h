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
