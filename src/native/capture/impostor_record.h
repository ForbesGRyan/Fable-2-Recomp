#pragma once

// The frame's impostor records (pure): the far woods (frame-map section 19, "Impostor records";
// docs/native-renderer/lighting-formulas.md, "The impostors"). The game draws each tree model's far
// trees as one quad list through D3DDevice_BeginVertices with vertex shader 129F7F2793DA9084 and
// pixel shader 4B1D3E6E8E58D3AC: one half4 a quad (x, y, z, s: the tree's place and half its
// height), four vertices a quad, the corners made by the shader. A record holds what the native
// pass needs to draw such a call again: the registers the two formulas read, the fetch constants,
// the state words and the alpha test's reference; the quads' words go to an array of the frame's
// scene (FrameScene::impostor_words), four floats a quad.
// They are a bounded block of their own beside the frame's draw records (FrameScene::impostors):
// no DrawRecord is made or changed for these calls, which stay counted "unsupported-prim" as
// before (spec ruling 10). At most kMaxImpostorRecords draws and kMaxImpostorQuads quads a frame;
// a draw is whole or missing, and the frame's set is not all-or-none (spec ruling 9): a missing
// draw is one tree model missing, which is the picture of before for that model.
//
// The numbers are those of the five discovery captures of 2026-10-11 (plan Task 3; frame-map
// section 19, "Discovery captures"; 116 of 116 rows): the builder's own command packet names the
// storage (the device's shadow of fetch slot 31 does not); a quad's eight bytes are four
// big-endian half floats x, y, z, s (the first 8 * quads bytes of the 16 * quads the call
// reserves); the alpha test's reference is 128 / 255; the pixel bool word as its last writer left
// it is 0x110, with b129 (bit 1) clear; tf0's swizzle field is 0x60A; the vertex texture slots 16
// and 17 hold the frame's fog tables tf5 and tf4.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "fog_block.h"
#include "position_decode.h"
#include "render_state.h"
#include "xdk_layout.h"

namespace fable2::native::capture {

// --fable2_native_impostors (spec ruling 5). kOff: nothing of this file is reached. kFlat: the
// records are captured and drawn with one flat colour. kOn: the pictures by read-back, which this
// build does not have (ledger ruling 31: phase 12c is not entered): the records are captured as
// in kFlat and nothing is drawn, each record counted "picture: unread" (ledger ruling 23).
enum class ImpostorMode : uint8_t { kOff, kFlat, kOn };
inline constexpr const char* kImpostorSwitchWords = "off, false, 0, flat, on, true, 1";
// The switch's word. Anything else: kOff, with `*recognized` false (the caller warns once).
inline ImpostorMode ParseImpostorMode(std::string_view text, bool* recognized) {
  if (recognized) *recognized = true;
  if (text == "off" || text == "false" || text == "0") return ImpostorMode::kOff;
  if (text == "flat") return ImpostorMode::kFlat;
  if (text == "on" || text == "true" || text == "1") return ImpostorMode::kOn;
  if (recognized) *recognized = false;
  return ImpostorMode::kOff;
}
inline const char* ImpostorModeName(ImpostorMode m) {
  return m == ImpostorMode::kFlat ? "flat" : m == ImpostorMode::kOn ? "on" : "off";
}

inline constexpr uint32_t kMaxImpostorRecords = 32, kMaxImpostorQuads = 4096;      // spec ruling 9
inline constexpr uint32_t kImpostorPrimitive = 13, kImpostorStride = 4;            // a quad list; BeginVertices' r6
inline constexpr uint32_t kImpostorVsRegisters = 14, kImpostorPsRegisters = 5;     // As fixed by Task 6: fourteen and five
// The records' registers by name, in the table's order.
enum : uint32_t { kImpC8, kImpC46, kImpC47, kImpC72, kImpC73, kImpC74, kImpC75, kImpC110, kImpC111,
                  kImpC113, kImpC114, kImpC115, kImpC116, kImpC117 };
enum : uint32_t { kImpP12, kImpP19, kImpP20, kImpP29, kImpP31 };

// impostor_table.inc (tools/xdk_sigmatch/gen_impostor_table.py, from light_formulas.IMPOSTOR_SHADER
// and the captures' bounds), read in one pass. A count runs on past its array, so that a table with
// too many lines fails the static assert below instead of being cut short.
struct ImpostorTable {
  uint64_t vs = 0, ps = 0;
  uint32_t shader_lines = 0;
  uint32_t vs_count = 0, ps_count = 0;
  uint8_t vs_reg[kImpostorVsRegisters] = {}, ps_reg[kImpostorPsRegisters] = {};
  uint32_t literal_count = 0;
  struct Literal { uint8_t kind, reg; uint32_t bits[4]; } literal[3] = {};       // kind 0: the vertex shader's, 1: the pixel shader's
  uint32_t word_lines = 0;
  uint8_t word_seen[4] = {}, word_source[4] = {};
  uint32_t endian_lines = 0, endian = 0;
  uint32_t bound_lines = 0, bound_unknown = 0;
  uint8_t bound_seen[2][kImpostorVsRegisters][4] = {};
  float bound_low[2][kImpostorVsRegisters][4] = {}, bound_high[2][kImpostorVsRegisters][4] = {};
  uint32_t size_lines = 0, b129_lines = 0, b129 = 0;
  float size_max = 0.0f;
};
constexpr ImpostorTable ReadImpostorTable() {
  ImpostorTable t;
#define FABLE2_IMPOSTOR_SHADERS(vs_hash, ps_hash) \
  t.vs = vs_hash;                                 \
  t.ps = ps_hash;                                 \
  ++t.shader_lines;
#define FABLE2_IMPOSTOR_VS_REGISTER(r)                              \
  if (t.vs_count < kImpostorVsRegisters) t.vs_reg[t.vs_count] = r; \
  ++t.vs_count;
#define FABLE2_IMPOSTOR_PS_REGISTER(r)                              \
  if (t.ps_count < kImpostorPsRegisters) t.ps_reg[t.ps_count] = r; \
  ++t.ps_count;
#define FABLE2_IMPOSTOR_LITERAL(kind, r, x, y, z, w)                                          \
  if (t.literal_count < 3) t.literal[t.literal_count] = ImpostorTable::Literal{kind, r, {x, y, z, w}}; \
  ++t.literal_count;
#define FABLE2_IMPOSTOR_WORD(k, source)  \
  if (k < 4) {                           \
    ++t.word_seen[k];                    \
    t.word_source[k] = uint8_t(source);  \
  }                                      \
  ++t.word_lines;
#define FABLE2_IMPOSTOR_ENDIAN(e) \
  t.endian = e;                   \
  ++t.endian_lines;
#define FABLE2_IMPOSTOR_BOUND(kind, r, comp, low, high)                                   \
  {                                                                                       \
    bool found = false;                                                                   \
    const uint32_t n = kind == 0 ? kImpostorVsRegisters : kImpostorPsRegisters;           \
    for (uint32_t i = 0; kind < 2 && comp < 4 && i < n && i < (kind == 0 ? t.vs_count : t.ps_count); ++i) { \
      if ((kind == 0 ? t.vs_reg[i] : t.ps_reg[i]) != r) continue;                         \
      ++t.bound_seen[kind][i][comp];                                                      \
      t.bound_low[kind][i][comp] = std::bit_cast<float>(uint32_t(low));                   \
      t.bound_high[kind][i][comp] = std::bit_cast<float>(uint32_t(high));                 \
      found = true;                                                                       \
    }                                                                                     \
    if (!found) ++t.bound_unknown;                                                        \
    ++t.bound_lines;                                                                      \
  }
#define FABLE2_IMPOSTOR_SIZE_MAX(bits)          \
  t.size_max = std::bit_cast<float>(uint32_t(bits)); \
  ++t.size_lines;
#define FABLE2_IMPOSTOR_B129(mask) \
  t.b129 = mask;                   \
  ++t.b129_lines;
#include "impostor_table.inc"
#undef FABLE2_IMPOSTOR_SHADERS
#undef FABLE2_IMPOSTOR_VS_REGISTER
#undef FABLE2_IMPOSTOR_PS_REGISTER
#undef FABLE2_IMPOSTOR_LITERAL
#undef FABLE2_IMPOSTOR_WORD
#undef FABLE2_IMPOSTOR_ENDIAN
#undef FABLE2_IMPOSTOR_BOUND
#undef FABLE2_IMPOSTOR_SIZE_MAX
#undef FABLE2_IMPOSTOR_B129
  return t;
}
inline constexpr ImpostorTable kImpostorTable = ReadImpostorTable();

// What a reader relies on: both hashes, once; fourteen and five registers, ascending (so none is
// named twice) and below the literals (c248 on); the three literal registers the formulas read
// (the vertex shader's c254 and c255, the pixel shader's c255), each once; every component of the
// word from one source 0 to 3, each source once; one endian 0 to 3; at most one bound a component
// (a component WITHOUT a bound is one no formula reads: carried and never judged, ledger ruling 34)
// and at least one a register, each finite with its low at most its high; one size bound above 0;
// b129 one bit.
constexpr bool ImpostorTableIsWellFormed(const ImpostorTable& t) {
  if (t.shader_lines != 1 || t.vs == 0 || t.ps == 0 || t.vs == t.ps) return false;
  if (t.vs_count != kImpostorVsRegisters || t.ps_count != kImpostorPsRegisters) return false;
  for (uint32_t i = 0; i < kImpostorVsRegisters; ++i) {
    if (t.vs_reg[i] >= 248 || (i > 0 && t.vs_reg[i] <= t.vs_reg[i - 1])) return false;
  }
  for (uint32_t i = 0; i < kImpostorPsRegisters; ++i) {
    if (t.ps_reg[i] >= 248 || (i > 0 && t.ps_reg[i] <= t.ps_reg[i - 1])) return false;
  }
  if (t.literal_count != 3) return false;
  uint32_t vs254 = 0, vs255 = 0, ps255 = 0;
  for (const ImpostorTable::Literal& l : t.literal) {
    vs254 += l.kind == 0 && l.reg == 254 ? 1 : 0;
    vs255 += l.kind == 0 && l.reg == 255 ? 1 : 0;
    ps255 += l.kind == 1 && l.reg == 255 ? 1 : 0;
  }
  if (vs254 != 1 || vs255 != 1 || ps255 != 1) return false;
  if (t.word_lines != 4) return false;
  uint32_t sources = 0;
  for (uint32_t k = 0; k < 4; ++k) {
    if (t.word_seen[k] != 1 || t.word_source[k] > 3) return false;
    sources |= 1u << t.word_source[k];
  }
  if (sources != 0xF || t.endian_lines != 1 || t.endian > 3) return false;
  if (t.bound_unknown != 0 || t.bound_lines == 0) return false;
  for (uint32_t kind = 0; kind < 2; ++kind) {
    for (uint32_t i = 0; i < (kind == 0 ? kImpostorVsRegisters : kImpostorPsRegisters); ++i) {
      uint32_t bounded = 0;
      for (uint32_t c = 0; c < 4; ++c) {
        if (t.bound_seen[kind][i][c] == 0) continue;
        const float low = t.bound_low[kind][i][c], high = t.bound_high[kind][i][c];
        // (written so that a bound that is no number fails; 3.0e38: any finite bound of the table)
        if (t.bound_seen[kind][i][c] != 1 || !(low <= high) || !(low >= -3.0e38f) || !(high <= 3.0e38f)) return false;
        ++bounded;
      }
      if (bounded == 0) return false;
    }
  }
  if (t.size_lines != 1 || !(t.size_max > 0.0f) || !(t.size_max <= 65504.0f)) return false;   // (a half holds no more)
  return t.b129_lines == 1 && t.b129 != 0 && (t.b129 & (t.b129 - 1)) == 0;
}
static_assert(ImpostorTableIsWellFormed(kImpostorTable),
              "impostor_table.inc is not what impostor_record.h reads (regenerate it: gen_impostor_table.py)");

inline constexpr uint64_t kImpostorVs = kImpostorTable.vs, kImpostorPs = kImpostorTable.ps;
inline constexpr uint8_t kImpostorVsRegister[kImpostorVsRegisters] = {
    kImpostorTable.vs_reg[0], kImpostorTable.vs_reg[1], kImpostorTable.vs_reg[2],  kImpostorTable.vs_reg[3],  kImpostorTable.vs_reg[4],
    kImpostorTable.vs_reg[5], kImpostorTable.vs_reg[6], kImpostorTable.vs_reg[7],  kImpostorTable.vs_reg[8],  kImpostorTable.vs_reg[9],
    kImpostorTable.vs_reg[10], kImpostorTable.vs_reg[11], kImpostorTable.vs_reg[12], kImpostorTable.vs_reg[13]};
inline constexpr uint8_t kImpostorPsRegister[kImpostorPsRegisters] = {kImpostorTable.ps_reg[0], kImpostorTable.ps_reg[1], kImpostorTable.ps_reg[2],
                                                                      kImpostorTable.ps_reg[3], kImpostorTable.ps_reg[4]};
// The enumerators above are the table's order (a reader indexes a record's registers by them).
static_assert(kImpostorVsRegister[kImpC8] == 8 && kImpostorVsRegister[kImpC46] == 46 && kImpostorVsRegister[kImpC47] == 47 &&
              kImpostorVsRegister[kImpC72] == 72 && kImpostorVsRegister[kImpC73] == 73 && kImpostorVsRegister[kImpC74] == 74 &&
              kImpostorVsRegister[kImpC75] == 75 && kImpostorVsRegister[kImpC110] == 110 && kImpostorVsRegister[kImpC111] == 111 &&
              kImpostorVsRegister[kImpC113] == 113 && kImpostorVsRegister[kImpC114] == 114 && kImpostorVsRegister[kImpC115] == 115 &&
              kImpostorVsRegister[kImpC116] == 116 && kImpostorVsRegister[kImpC117] == 117,
              "the vertex registers' names are not the table's order");
static_assert(kImpostorPsRegister[kImpP12] == 12 && kImpostorPsRegister[kImpP19] == 19 && kImpostorPsRegister[kImpP20] == 20 &&
              kImpostorPsRegister[kImpP29] == 29 && kImpostorPsRegister[kImpP31] == 31,
              "the pixel registers' names are not the table's order");
// kImpostorWordSource[k]: which of the element's four halves, in the order the GPU reads them after
// its endian swap (render::CopySwapped by kImpostorWordEndian), is component k of (x, y, z, s).
// With (1, 0, 3, 2) and endian 2 (8-in-32): four big-endian halves x, y, z, s in the guest's bytes.
inline constexpr uint8_t kImpostorWordSource[4] = {kImpostorTable.word_source[0], kImpostorTable.word_source[1],
                                                   kImpostorTable.word_source[2], kImpostorTable.word_source[3]};
inline constexpr uint32_t kImpostorWordEndian = kImpostorTable.endian;
inline constexpr float kImpostorSizeMax = kImpostorTable.size_max;      // a quad's s: four times the captures' largest
inline constexpr uint32_t kImpostorB129 = kImpostorTable.b129;          // the ground-fog bool's bit of the pixel bool word
inline constexpr float kImpostorPlaceMax = 1e6f;                        // a quad's place
// tf0's dword 0 in every captured row: a tiled 2D fetch, pitch 256, clamp 6 / 6, no sign field.
inline constexpr uint32_t kImpostorFetch0 = 0x8200D802u;
// tf0's dword 3 bits 1 to 12 in every captured row: x <- 2, y <- 1, z <- 0, w <- 3 (As fixed by Task 3; not 0).
inline constexpr uint32_t kImpostorSwizzle = 0x60Au;
// The scale's bound, of the table (c20.w in (0, 64]: its low is the smallest number above 0).
inline constexpr float kImpostorScaleMax = kImpostorTable.bound_high[1][kImpP20][3];
static_assert(kImpostorTable.bound_seen[1][kImpP20][3] == 1 && kImpostorScaleMax == 64.0f && kImpostorTable.bound_low[1][kImpP20][3] > 0.0f);

inline constexpr const char* kImpostorRecordReason = "impostor-record";
// Why a call of the pair gave no record, or a record was taken out of its frame; kNone: neither.
enum class ImpostorReason : uint8_t {
  kNone,
  kUnread,     // the device, a shader, a register, a fetch constant, the builder's packet, a state word, the bool
               // word's last writer or the storage could not be read; also a call whose pixel shader could not be told
  kShader,     // the vertex shader is not the one the pixel shader's formula is checked with
  kLiteral,    // a shader object's own literal table is not the table's, or loads a register the formulas read from the bank
  kStorage,    // the builder's arguments, the device's storage fields or the packet's fetch constant are not a quad list's
  kQuad,       // a quad's word is no number, its place beyond kImpostorPlaceMax or its size outside (0, kImpostorSizeMax]
  kRegister,   // a register's component is no number or outside the table's bound
  kScale,      // c20.w outside (0, 64]
  kState,      // the state words are not the captured draw's
  kFetch,      // tf0 is not the picture's fetch
  kGroundFog,  // bool b129 set: the ground-fog block, which no capture exercises (spec ruling 20)
  kFogTable,   // the record's fog is not the frame's (FrameBuilder::Finish, ImpostorFogMatches)
  kOverLimit,  // the 33rd draw of a frame, or a draw whose quads do not fit below kMaxImpostorQuads: that draw whole
  kPending,    // the call's storage was not read before its frame ended
  kCount
};
inline const char* ImpostorReasonName(ImpostorReason r) {
  static constexpr const char* kNames[] = {"none", "unread", "shader", "literal", "storage", "quad", "register",
                                           "scale", "state", "fetch", "ground-fog", "fog-table", "over-limit", "pending"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(ImpostorReason::kCount));
  return r < ImpostorReason::kCount ? kNames[size_t(r)] : "?";
}

// One draw of the frame. An invalid record is all zero (ps_hash 0: ImpostorRecordValid).
struct ImpostorRecord {
  uint64_t ps_hash, vs_hash;
  uint32_t place;                            // DrawRecord::seq its own call was counted under (FrameBuilder::NextSeq)
  uint32_t quads, first_quad;                // its words: FrameScene::impostor_words[4 * first_quad ...), 4 floats a quad
  uint32_t pbool;                            // the pixel bool word as its last writer left it on the GPU
  float vs[kImpostorVsRegisters][4];         // the table's vertex registers, in its order
  float ps[kImpostorPsRegisters][4];         // c12, c19, c20, c29, c31
  uint32_t fetch[6];                         // tf0
  uint32_t fog_fetch[2][6];                  // vertex tf16, tf17
  uint32_t vfetch[2];                        // the storage's vertex fetch constant, as the builder's packet has it
  uint32_t depth, blend, colour_control, colour_mask, surface_info;
  float alpha_ref;                           // RB_ALPHA_REF, in [0, 1]
};
// 16 + 16 + 224 + 80 + 24 + 48 + 8 + 20 + 4 (recomputed by plan Task 8 from what is carried: the plan's 440 stands).
static_assert(sizeof(ImpostorRecord) == 440, "ImpostorRecord grew or shrank: the documents state its size");

inline bool ImpostorRecordValid(const ImpostorRecord& r) { return r.ps_hash != 0; }

// What was read at the call's return (capture.cpp fills it; every count and size is the arguments').
struct ImpostorCall {
  // The device, both shaders, every register, fetch constant and state word, the builder's packet and the bool
  // word's last writer were read.
  bool read = false;
  uint64_t ps_hash = 0, vs_hash = 0;
  bool literals_match = false;               // both shader objects' own literal tables hold the table's literals (asked once an object and frame)
  uint32_t prim = 0, count = 0, stride = 0;  // r4 & 0x3F, r5, r6
  uint32_t storage = 0, storage_dwords = 0;  // device + 0x3488, + 0x3490
  float vs[kImpostorVsRegisters][4] = {};
  float ps[kImpostorPsRegisters][4] = {};
  uint32_t pbool = 0, fetch[6] = {}, fog_fetch[2][6] = {};
  // The last pair of the six dwords the builder wrote for fetch slot 31 (vertex fetch constant 95): its command
  // packet's, not the device's shadow, which names the storage in no captured row.
  uint32_t vfetch[2] = {};
  StateWords state;
};

// The quads of a call of `count` vertices: count / 4 for a multiple of 4 in [4, 4 * kMaxImpostorQuads]; else 0.
constexpr uint32_t ImpostorQuads(uint32_t count) {
  return count >= 4 && count % 4 == 0 && count / 4 <= kMaxImpostorQuads ? count / 4 : 0;
}

// Whether a shader object's own literal table is the table's: kind 0 the vertex shader (c254, c255), 1 the pixel
// shader (c255). `bits[r][c]`: component c of the shader's own register 248 + r as its table holds it; bit r * 4 + c
// of `have`: the table holds that dword at all (wind_inputs.h, AddWindLiteralRun, fills both). Every component of a
// literal register must be held and bit-equal, the ones no formula reads too (the vertex shader's c254.w, the pixel
// shader's c255.x and .w, read only under b129): they are equal in every captured table, and a table that differs
// in them is not the shader the formulas were checked with.
inline bool ImpostorLiteralsMatch(uint32_t kind, const uint32_t (*bits)[4], uint32_t have) {
  if (!bits || kind > 1) return false;
  for (const ImpostorTable::Literal& l : kImpostorTable.literal) {
    if (l.kind != kind) continue;
    if (l.reg < 248) return false;
    const uint32_t r = uint32_t(l.reg) - 248;
    for (uint32_t c = 0; c < 4; ++c) {
      if (!((have >> (r * 4 + c)) & 1u) || bits[r][c] != l.bits[c]) return false;
    }
  }
  return true;
}

namespace detail {
// A register's component inside the table's bound (false for no number). A component the table has no bound
// for is one no formula reads: whatever it holds, it is not judged (ledger ruling 34).
inline bool ImpostorBounded(uint32_t kind, uint32_t index, uint32_t comp, float v) {
  if (!kImpostorTable.bound_seen[kind][index][comp]) return true;
  return v >= kImpostorTable.bound_low[kind][index][comp] && v <= kImpostorTable.bound_high[kind][index][comp];
}
// Whether a formula reads component `comp` of the record's register `index` of `kind` (0 vertex, 1 pixel).
constexpr bool ImpostorComponentRead(uint32_t kind, uint32_t index, uint32_t comp) {
  return kind < 2 && index < kImpostorVsRegisters && comp < 4 && kImpostorTable.bound_seen[kind][index][comp] != 0;
}
}  // namespace detail

// Every rule of a call but the words': kNone, or the first that fails, in this order:
//   - everything was read (kUnread);
//   - both hashes are the table's (kShader);
//   - the shaders' own literal tables are the table's (kLiteral);
//   - a quad list (primitive 13) at a stride of 4, a count that ImpostorQuads takes, a storage pointer, the device's
//     dwords equal to the count, and the packet's fetch constant naming that storage, field by field as the GPU
//     reads a vertex fetch constant (the SDK's xenos.h, xe_gpu_vertex_fetch_t): dword 0 = type (bits 0 to 1) 3 and
//     the address (bits 2 to 31) xdk::GpuAddress(storage); dword 1 = the endian (bits 0 to 1) the table's and the
//     size in words (bits 2 to 25) the dwords. Dword 1's bits 26 to 31 are padding to the GPU and are NOT compared:
//     the game's packets have 0x10000000 there (116 of 116 captured rows: (dwords << 2) | 0x10000002, the first row
//     0x10000382 for 224 dwords), a bit whose meaning is not established and which no fetch depends on (kStorage);
//   - every component of every register that a formula reads a number inside the table's bound (kRegister), but for
//     c20.w, which is in (0, kImpostorScaleMax] (kScale); a component no formula reads is carried as it is and not
//     judged, not even for being a number (ledger ruling 34);
//   - the state: DecodeRenderState valid; the depth test and write on with the function "greater or equal" under the
//     reversed range; no blend; all four colour channels; the alpha test on with the function "greater" and a
//     reference in [0, 1]; alpha-to-mask with offsets 0xAA on a two-sample surface (kState);
//   - tf0: dword 0 kImpostorFetch0, format 3, endian 1, a base address, 256 x 256, the swizzle kImpostorSwizzle,
//     highest level 3, a 2D texture (kFetch);
//   - b129 clear in the pixel bool word (kGroundFog).
inline ImpostorReason CheckImpostorCall(const ImpostorCall& c) {
  if (!c.read) return ImpostorReason::kUnread;
  if (c.ps_hash != kImpostorPs || c.vs_hash != kImpostorVs) return ImpostorReason::kShader;
  if (!c.literals_match) return ImpostorReason::kLiteral;
  if (c.prim != kImpostorPrimitive || c.stride != kImpostorStride || ImpostorQuads(c.count) == 0 || c.storage == 0 ||
      c.storage_dwords != c.count || (c.vfetch[0] & 3u) != 3u || (c.vfetch[0] & ~3u) != xdk::GpuAddress(c.storage) ||
      (c.vfetch[1] & 3u) != kImpostorWordEndian || ((c.vfetch[1] >> 2) & 0xFFFFFFu) != c.storage_dwords) {
    return ImpostorReason::kStorage;
  }
  for (uint32_t i = 0; i < kImpostorVsRegisters; ++i) {
    for (uint32_t k = 0; k < 4; ++k) {
      if (!detail::ImpostorBounded(0, i, k, c.vs[i][k])) return ImpostorReason::kRegister;
    }
  }
  for (uint32_t i = 0; i < kImpostorPsRegisters; ++i) {
    for (uint32_t k = 0; k < 4; ++k) {
      if (i == kImpP20 && k == 3) continue;
      if (!detail::ImpostorBounded(1, i, k, c.ps[i][k])) return ImpostorReason::kRegister;
    }
  }
  if (!detail::ImpostorBounded(1, kImpP20, 3, c.ps[kImpP20][3])) return ImpostorReason::kScale;
  const RenderState s = DecodeRenderState(c.state);
  if (!s.valid || !s.depth_test || !s.depth_write || s.depth_func != Compare::kGreaterEqual || !s.depth_reversed || s.blend ||
      s.color_mask != 0xF || !s.alpha_test || s.alpha_func != Compare::kGreater || !(s.alpha_ref >= 0.0f && s.alpha_ref <= 1.0f) ||
      !s.alpha_to_mask || s.alpha_to_mask_offsets != 0xAA || s.alpha_to_mask_samples != 1) {
    return ImpostorReason::kState;
  }
  const uint32_t* f = c.fetch;
  if (f[0] != kImpostorFetch0 || (f[1] & 0x3Fu) != 3u || ((f[1] >> 6) & 3u) != 1u || (f[1] & 0xFFFFF000u) == 0 ||
      (f[2] & 0x1FFFu) != 255u || ((f[2] >> 13) & 0x1FFFu) != 255u || ((f[3] >> 1) & 0xFFFu) != kImpostorSwizzle ||
      ((f[4] >> 6) & 0xFu) != 3u || ((f[5] >> 9) & 3u) != 1u) {
    return ImpostorReason::kFetch;
  }
  if (c.pbool & kImpostorB129) return ImpostorReason::kGroundFog;
  return ImpostorReason::kNone;
}

// The record of a call and its words (4 * ImpostorQuads(c.count) floats into `words`): CheckImpostorCall again,
// then `bytes` (the storage's first 8 * quads bytes, guest order) whole (`byte_count` exactly that, and both
// pointers there: kUnread otherwise), each quad's four halves by kImpostorWordSource and kImpostorWordEndian,
// every value a number, a place within kImpostorPlaceMax, a size in (0, kImpostorSizeMax] (kQuad). Every quad is
// judged before one word is written: no record (all zero) and `words` untouched on any failure. `place` and
// `first_quad` are 0: the caller sets the place (the call's seq), the frame the first quad.
inline ImpostorRecord MakeImpostorRecord(const ImpostorCall& c, const uint8_t* bytes, size_t byte_count, float* words,
                                         ImpostorReason* why) {
  auto fail = [&](ImpostorReason r) {
    if (why) *why = r;
    return ImpostorRecord{};
  };
  const ImpostorReason call = CheckImpostorCall(c);
  if (call != ImpostorReason::kNone) return fail(call);
  const uint32_t quads = ImpostorQuads(c.count);
  if (!bytes || !words || byte_count != size_t(8) * quads) return fail(ImpostorReason::kUnread);
  // The GPU's endian swap of one 32-bit word, as an index into the guest's four bytes (render::CopySwapped).
  constexpr uint32_t flip = kImpostorWordEndian == 1 ? 1u : kImpostorWordEndian == 2 ? 3u : kImpostorWordEndian == 3 ? 2u : 0u;
  auto word = [&](uint32_t q, float out[4]) {
    const uint8_t* p = bytes + size_t(8) * q;
    for (uint32_t k = 0; k < 4; ++k) {
      const uint32_t at = 2u * kImpostorWordSource[k];   // the half's low byte among the eight the GPU reads
      out[k] = HalfToFloat(uint16_t(uint32_t(p[at ^ flip]) | uint32_t(p[(at + 1) ^ flip]) << 8));
    }
  };
  for (uint32_t q = 0; q < quads; ++q) {
    float v[4];
    word(q, v);
    for (uint32_t k = 0; k < 3; ++k) {
      if (!(std::fabs(v[k]) <= kImpostorPlaceMax)) return fail(ImpostorReason::kQuad);   // (false for no number)
    }
    if (!(v[3] > 0.0f && v[3] <= kImpostorSizeMax)) return fail(ImpostorReason::kQuad);
  }
  for (uint32_t q = 0; q < quads; ++q) word(q, words + size_t(4) * q);
  ImpostorRecord r{};
  r.ps_hash = c.ps_hash;
  r.vs_hash = c.vs_hash;
  r.quads = quads;
  r.pbool = c.pbool;
  std::memcpy(r.vs, c.vs, sizeof(r.vs));
  std::memcpy(r.ps, c.ps, sizeof(r.ps));
  std::memcpy(r.fetch, c.fetch, sizeof(r.fetch));
  std::memcpy(r.fog_fetch, c.fog_fetch, sizeof(r.fog_fetch));
  std::memcpy(r.vfetch, c.vfetch, sizeof(r.vfetch));
  r.depth = c.state.depth_control;
  r.blend = c.state.blend_control;
  r.colour_control = c.state.color_control;
  r.colour_mask = c.state.color_mask & 0xFu;
  r.surface_info = c.state.surface_info;
  std::memcpy(&r.alpha_ref, &c.state.alpha_ref, sizeof(r.alpha_ref));
  if (why) *why = ImpostorReason::kNone;
  return r;
}

// Whether a record's fog is the frame's (the `fog-table` rule, taken at FrameBuilder::Finish where the frame's
// block and its tables' addresses are both in hand: plan contradiction 5): its slot 16 at the frame's tf5 and its
// slot 17 at the frame's tf4 by base address (`table_base`: FogSource::base, [0] tf4's and [1] tf5's; an address
// of 0 is no table), c110.xyz bit-equal to the block's axis, c111.x, .z, .w bit-equal to the block's range (c111.y
// is the sky's path height, which the impostor's fog does not read). False for an invalid block.
inline bool ImpostorFogMatches(const ImpostorRecord& r, const FogBlock& block, const uint32_t table_base[2]) {
  if (!block.valid || !table_base || table_base[0] == 0 || table_base[1] == 0) return false;
  if ((r.fog_fetch[0][1] & 0xFFFFF000u) != table_base[1] || (r.fog_fetch[1][1] & 0xFFFFF000u) != table_base[0]) return false;
  if (std::memcmp(r.vs[kImpC110], block.axis, 3 * sizeof(float)) != 0) return false;
  const float range[3] = {r.vs[kImpC111][0], r.vs[kImpC111][2], r.vs[kImpC111][3]};
  const float want[3] = {block.range[0], block.range[2], block.range[3]};
  return std::memcmp(range, want, sizeof(range)) == 0;
}

}  // namespace fable2::native::capture
