#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
// A float as discovery rows write it: nine significant digits; NaN and the
// infinities as bare tokens (not JSON, but Python's json reads them).
inline void AppendFloat(std::string& s, float f) {
  char buf[32];
  if (std::isnan(f)) {
    s += "NaN";
  } else if (std::isinf(f)) {
    s += f > 0 ? "Infinity" : "-Infinity";
  } else {
    std::snprintf(buf, sizeof(buf), "%.9g", double(f));
    s += buf;
  }
}
// [f,...] for `n` big-endian float32 at `be` (a guest constant bank), or null.
inline std::string BeFloats(const uint8_t* be, size_t n) {
  if (!be) return "null";
  std::string s = "[";
  for (size_t i = 0; i < n; ++i) {
    const uint32_t bits = uint32_t(be[4 * i]) << 24 | uint32_t(be[4 * i + 1]) << 16 |
                          uint32_t(be[4 * i + 2]) << 8 | uint32_t(be[4 * i + 3]);
    float f;
    std::memcpy(&f, &bits, 4);
    if (i) s += ",";
    AppendFloat(s, f);
  }
  return s + "]";
}
}  // namespace fable2::native::capture
