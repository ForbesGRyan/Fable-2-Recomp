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
// The head of an "indx" row, the decoded row of a draw of the indexed immediate builder
// 0x82217EE8 (xdk_layout.h, "BeginIndexedVertices"; `args` are r4..r10: the primitive type in
// its low six bits, the base vertex, the vertex count, the index count, the index format with
// bit 2 set for 32-bit indices, the vertex size in bytes, where the index storage is returned):
// open, to be continued with the members the other decoded rows have.
inline std::string IndxRowHead(int frame, const char* func, const uint32_t args[7], uint32_t device,
                               bool in_scene) {
  char buf[448];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"indx\",\"frame\":%d,\"func\":\"%s\",\"prim\":%u,\"base\":%d,\"vertices\":%u,"
                "\"indices\":%u,\"index32\":%s,\"stride\":%u,\"args\":[\"0x%08X\",\"0x%08X\",\"0x%08X\","
                "\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"],\"device\":\"0x%08X\",\"in_scene\":%s",
                frame, func, args[0] & 0x3F, int32_t(args[1]), args[2], args[3], (args[4] & 4) ? "true" : "false",
                args[5], args[0], args[1], args[2], args[3], args[4], args[5], args[6], device,
                in_scene ? "true" : "false");
  return buf;
}
// The dwords of vertex and of index storage that builder reserves (0x82217EF8 `mullw r11,r6,r9`
// and `rlwinm r19,r11,30,2,31`; 0x82217F24 `addi r11,r23,1`, `rlwinm r18,r11,31,1,31` for 16-bit
// indices, the count itself for 32-bit ones), with the builder's own 32-bit arithmetic.
inline uint32_t IndxVertexDwords(uint32_t vertices, uint32_t stride_bytes) {
  return uint32_t(vertices * stride_bytes) >> 2;
}
inline uint32_t IndxIndexDwords(uint32_t indices, bool index32) {
  return index32 ? indices : uint32_t(indices + 1) >> 1;
}
// Bytes as hex, two upper-case digits each, in memory order ("" for none).
inline std::string HexBytes(const uint8_t* p, size_t n) {
  static const char kDigits[] = "0123456789ABCDEF";
  std::string s;
  if (!p) return s;
  s.reserve(2 * n);
  for (size_t i = 0; i < n; ++i) {
    s += kDigits[p[i] >> 4];
    s += kDigits[p[i] & 15];
  }
  return s;
}
// Post discovery (frame-map section 17). Whether `count` command dwords at `be` (as the guest
// stores them, big-endian) hold a type-0 packet header whose registers include one of the copy
// registers 0x2318..0x231B (RB_COPY_CONTROL, RB_COPY_DEST_BASE, RB_COPY_DEST_PITCH,
// RB_COPY_DEST_INFO): the registers a resolve is issued with. A type-0 header is index in bits
// 0-14, bit 15 "every value to the base register", count - 1 in bits 16-29, bits 30-31 zero (the
// SDK's ExecutePacketType0). Every dword is looked at as a header, so a data dword that looks
// like one counts too: this only decides whether a capture keeps the dwords; the reader
// (tools/xdk_sigmatch/post_tables.py) walks the packets.
inline bool NamesCopyRegisters(const uint8_t* be, size_t count) {
  if (!be) return false;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t d = uint32_t(be[4 * i]) << 24 | uint32_t(be[4 * i + 1]) << 16 | uint32_t(be[4 * i + 2]) << 8 |
                       uint32_t(be[4 * i + 3]);
    if ((d >> 30) != 0) continue;
    const uint32_t first = d & 0x7FFF;
    const uint32_t last = (d & 0x8000) ? first : first + ((d >> 16) & 0x3FFF);
    if (first <= 0x231B && last >= 0x2318) return true;
  }
  return false;
}
// Post discovery. The stretch (after, last] of the guest's command buffer between two marks of
// the device's write pointer (each the address of a dword): kDwords when it is 1 to `max_dwords`
// whole dwords (`*first` the first dword's address, `*count` their number); kEmpty when the
// pointer did not move (nothing was written: no drop); otherwise why it is not looked at: kBack
// (the pointer went back: another segment of the command buffer, or its wrap), kUnaligned (no
// whole dwords), kLong (more than the cap; `*count` the distance in dwords). Only the arithmetic:
// the caller reads the dwords, and a stretch it cannot read is skipped as "unreadable".
enum class StreamSpan : uint8_t { kDwords, kEmpty, kBack, kUnaligned, kLong };
inline StreamSpan ClassifyStream(uint32_t after, uint32_t last, uint32_t max_dwords, uint32_t* first, uint32_t* count) {
  *first = 0;
  *count = 0;
  if (last == after) return StreamSpan::kEmpty;
  if (last < after) return StreamSpan::kBack;
  if (((last - after) & 3) != 0) return StreamSpan::kUnaligned;
  *count = (last - after) / 4;
  if (*count > max_dwords) return StreamSpan::kLong;
  *first = after + 4;  // no wrap: last >= after + 4
  return StreamSpan::kDwords;
}
// The "skipped" member's word for a stretch that is not looked at; nullptr for one that is taken
// or empty.
inline const char* StreamSkipName(StreamSpan span) {
  switch (span) {
    case StreamSpan::kBack: return "back";
    case StreamSpan::kUnaligned: return "unaligned";
    case StreamSpan::kLong: return "long";
    default: return nullptr;
  }
}
// The "cmdgap" row of a stretch that was not looked at (no "cmd"): `why` is StreamSkipName's word
// or "unreadable"; "from" the address after the earlier mark, "to" the later mark, "dwords" the
// distance where it is one (0 for "back" and "unaligned"). A resolve issued inside such a stretch
// is in no row: the reader counts these rows and says so.
inline std::string SkippedGapRow(int frame, uint32_t n, const char* func, const char* why, uint32_t after, uint32_t last,
                                 uint32_t dwords) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"cmdgap\",\"frame\":%d,\"n\":%u,\"func\":\"%s\",\"skipped\":\"%s\",\"from\":\"0x%08X\","
                "\"to\":\"0x%08X\",\"dwords\":%u}\n",
                frame, n, func, why, uint32_t(after + 4), last, dwords);
  return buf;
}
// Foliage discovery (frame-map section 18). The bytes of a draw's index buffer a discovery row
// keeps as a file, as an offset into the buffer and a length: the buffer whole when it is at most
// `cap` bytes; else the draw's own range [start, start + count) when that is; else none (`bytes`
// 0). A draw without indices, a buffer without bytes and a range that does not lie inside the
// buffer give none, whatever the buffer's size: a row names only bytes its draw could read. The
// arithmetic is 64-bit (the guest's start and count are any 32-bit words).
struct IndexDump {
  uint32_t offset_bytes = 0, bytes = 0;
  bool whole = false;
};
inline IndexDump IndexDumpRange(uint32_t ib_size, uint32_t start, uint32_t count, bool index32, uint32_t cap) {
  IndexDump none;
  if (ib_size == 0 || count == 0) return none;
  const uint64_t isize = index32 ? 4 : 2;
  const uint64_t first = uint64_t(start) * isize, bytes = uint64_t(count) * isize;
  if (first + bytes > ib_size) return none;
  if (ib_size <= cap) return IndexDump{0, ib_size, true};
  if (bytes > cap) return none;
  return IndexDump{uint32_t(first), uint32_t(bytes), false};
}
}  // namespace fable2::native::capture
