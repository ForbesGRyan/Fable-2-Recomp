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
