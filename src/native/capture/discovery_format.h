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
// Constant registers by their bits, as discovery rows write them ("pfull"): 32
// hex digits per register, the dwords of x, y, z, w from `bits` (four per
// register); 32 dashes for a register that was not read (`have[r]` false, or no
// `bits`; a null `have` means all were read).
inline std::string RegistersHex(const uint32_t* bits, const bool* have, size_t registers) {
  std::string s;
  s.reserve(registers * 32);
  char buf[40];
  for (size_t r = 0; r < registers; ++r) {
    if (!bits || (have && !have[r])) {
      s.append(32, '-');
      continue;
    }
    std::snprintf(buf, sizeof(buf), "%08X%08X%08X%08X", bits[4 * r], bits[4 * r + 1], bits[4 * r + 2],
                  bits[4 * r + 3]);
    s += buf;
  }
  return s;
}
// The members of a row's "rs" object for `n` registers: "<REG>":"0x<word>" (four and eight hex
// digits), comma-separated, in the order of `regs`; `read(reg, &word)` gives a register's word
// or false, and a register it does not give is left out.
template <typename Read>
inline std::string RegisterWords(const uint32_t* regs, size_t n, Read read) {
  std::string s;
  char buf[32];
  for (size_t i = 0; i < n; ++i) {
    uint32_t word = 0;
    if (!read(regs[i], &word)) continue;
    std::snprintf(buf, sizeof(buf), "%s\"%04X\":\"0x%08X\"", s.empty() ? "" : ",", regs[i], word);
    s += buf;
  }
  return s;
}
// The head of a "verts" row, the decoded row of a BeginVertices draw (0x822060A0: r4 the
// primitive type in its low six bits, r5 the vertex count, r6 the vertex size in bytes; `args`
// are r4..r7): open, to be continued with the members the other decoded rows have.
inline std::string VertsRowHead(int frame, const char* func, const uint32_t args[4], uint32_t device,
                                bool in_scene) {
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"verts\",\"frame\":%d,\"func\":\"%s\",\"prim\":%u,\"count\":%u,"
                "\"args\":[\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"],\"device\":\"0x%08X\",\"in_scene\":%s",
                frame, func, args[0] & 0x3F, args[1], args[0], args[1], args[2], args[3], device,
                in_scene ? "true" : "false");
  return buf;
}
}  // namespace fable2::native::capture
