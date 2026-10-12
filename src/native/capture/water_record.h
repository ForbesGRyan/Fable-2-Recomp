#pragma once

// The frame's water records (pure): the lake's surface (frame-map section 20, "Water records";
// docs/native-renderer/lighting-formulas.md, "Water"). The game draws each mesh of a water body twice
// with vertex shader BEAD84BD72072E0E: once with pixel shader F6D98C7B4D98438B, the opaque twin,
// which kills every pixel in every captured row (the "dead draw"), and late in the frame, blended,
// with A17D8AEC3A817D45, the surface. A record holds what the native pass needs beyond the draw
// record of the same draw: the registers the formulas read, the four fetch constants, the bases of
// the fog's two tables and the pixel bool word as its last writer left it.
// They are a bounded block of their own beside the frame's draw records (FrameScene::water), linked
// to them by sequence number (WaterRecord::place = DrawRecord::seq): no DrawRecord is made otherwise
// or changed for these draws, which keep the no-albedo record they had (spec ruling 10; the draw
// record stays 616 bytes). The state words, the alpha test's reference among them, are the draw
// record's own. At most kMaxWaterRecords of both pixel shaders a frame, and THE FRAME'S WATER IS
// WHOLE OR EMPTY (spec ruling 11): one refused record of either kind and the scene holds none
// (FrameBuilder::Finish), so that the dead draw is never left out without the surface that
// replaces it.
//
// The numbers are those of 164 water rows of five discovery captures of 2026-10-11 (plan Task 4;
// out/native-evidence/water/t4_*.txt): 82 rows of each pixel shader; the pixel bool word is 0x110
// as its last writer left it, with b136 (bit 8) set and b137 (bit 9) clear; one vertex stream of 5
// dwords, the position (32_32_32_FLOAT) at 0 and the mask's pair (32_32_FLOAT) at 3.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../render/texture_decode.h"
#include "fog_block.h"
#include "render_state.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

inline constexpr uint32_t kMaxWaterRecords = 64;                                   // spec ruling 10
inline constexpr uint32_t kWaterVsRegisters = 4, kWaterPsRegisters = 24;           // As fixed by Task 4: four and twenty-four
// The records' registers by name, in the table's order.
enum : uint32_t { kWatV115, kWatV116, kWatV117, kWatV118 };
enum : uint32_t { kWatP0, kWatP19, kWatP20, kWatP28, kWatP31, kWatP66, kWatP67, kWatP68, kWatP116, kWatP117, kWatP126, kWatP127,
                  kWatP128, kWatP129, kWatP130, kWatP131, kWatP136, kWatP137, kWatP138, kWatP139, kWatP140, kWatP141, kWatP142,
                  kWatP143 };
enum : uint32_t { kWatMask, kWatNormal, kWatPicture, kWatNear };                   // WaterRecord::fetch: slots 1, 2, 13, 14

// water_table.inc (tools/xdk_sigmatch/gen_water_table.py, from light_formulas.WATER_SHADER), read in one
// pass. A count runs on past its array, so that a table with too many lines fails the static assert below
// instead of being cut short.
struct WaterTable {
  uint64_t vs = 0, ps = 0, ps_opaque = 0;
  uint32_t shader_lines = 0;
  uint32_t vs_count = 0, ps_count = 0;
  uint8_t vs_reg[kWaterVsRegisters] = {}, vs_read[kWaterVsRegisters] = {};
  uint8_t ps_reg[kWaterPsRegisters] = {}, ps_read[kWaterPsRegisters] = {}, ps_kind[kWaterPsRegisters] = {};
  uint32_t literal_count = 0;
  struct Literal { uint8_t reg; uint32_t bits[4]; } literal[2] = {};               // the pixel shaders' own c254 and c255
  uint32_t bool_lines = 0, b136 = 0, b137 = 0;
  uint32_t slot_lines = 0, slot_unknown = 0;
  uint8_t slot_seen[4] = {}, slot[4] = {};
};
constexpr WaterTable ReadWaterTable() {
  WaterTable t;
#define FABLE2_WATER_SHADERS(vs_hash, ps_hash, opaque_hash) \
  t.vs = vs_hash;                                           \
  t.ps = ps_hash;                                           \
  t.ps_opaque = opaque_hash;                                \
  ++t.shader_lines;
#define FABLE2_WATER_VS_REGISTER(r, mask)   \
  if (t.vs_count < kWaterVsRegisters) {     \
    t.vs_reg[t.vs_count] = uint8_t(r);      \
    t.vs_read[t.vs_count] = uint8_t(mask);  \
  }                                         \
  ++t.vs_count;
#define FABLE2_WATER_PS_REGISTER(r, mask, kind) \
  if (t.ps_count < kWaterPsRegisters) {         \
    t.ps_reg[t.ps_count] = uint8_t(r);          \
    t.ps_read[t.ps_count] = uint8_t(mask);      \
    t.ps_kind[t.ps_count] = uint8_t(kind);      \
  }                                             \
  ++t.ps_count;
#define FABLE2_WATER_LITERAL(r, x, y, z, w)                                                        \
  if (t.literal_count < 2) t.literal[t.literal_count] = WaterTable::Literal{uint8_t(r), {x, y, z, w}}; \
  ++t.literal_count;
#define FABLE2_WATER_BOOL(name, mask) \
  t.name = mask;                      \
  ++t.bool_lines;
#define FABLE2_WATER_FETCH_SLOT(index, s) \
  if (index < 4) {                        \
    ++t.slot_seen[index];                 \
    t.slot[index] = uint8_t(s);           \
  } else {                                \
    ++t.slot_unknown;                     \
  }                                       \
  ++t.slot_lines;
#include "water_table.inc"
#undef FABLE2_WATER_SHADERS
#undef FABLE2_WATER_VS_REGISTER
#undef FABLE2_WATER_PS_REGISTER
#undef FABLE2_WATER_LITERAL
#undef FABLE2_WATER_BOOL
#undef FABLE2_WATER_FETCH_SLOT
  return t;
}
inline constexpr WaterTable kWaterTable = ReadWaterTable();

// What a reader relies on: the three hashes, once, all different; four and twenty-four registers, ascending
// (so none is named twice) and below the literals (c248 on); a read mask of 1 to 15 for each; a kind 0 to 3;
// the two literal registers c254 and c255, each once; each bool one bit, the two different; the four fetch
// slots, each once and below 32.
constexpr bool WaterTableIsWellFormed(const WaterTable& t) {
  if (t.shader_lines != 1 || t.vs == 0 || t.ps == 0 || t.ps_opaque == 0 || t.vs == t.ps || t.vs == t.ps_opaque || t.ps == t.ps_opaque) return false;
  if (t.vs_count != kWaterVsRegisters || t.ps_count != kWaterPsRegisters) return false;
  for (uint32_t i = 0; i < kWaterVsRegisters; ++i) {
    if (t.vs_reg[i] >= 248 || (i > 0 && t.vs_reg[i] <= t.vs_reg[i - 1]) || t.vs_read[i] == 0 || t.vs_read[i] > 15) return false;
  }
  for (uint32_t i = 0; i < kWaterPsRegisters; ++i) {
    if (t.ps_reg[i] >= 248 || (i > 0 && t.ps_reg[i] <= t.ps_reg[i - 1]) || t.ps_read[i] == 0 || t.ps_read[i] > 15 || t.ps_kind[i] > 3) return false;
  }
  if (t.literal_count != 2) return false;
  uint32_t c254 = 0, c255 = 0;
  for (const WaterTable::Literal& l : t.literal) {
    c254 += l.reg == 254 ? 1 : 0;
    c255 += l.reg == 255 ? 1 : 0;
  }
  if (c254 != 1 || c255 != 1) return false;
  if (t.bool_lines != 2 || t.b136 == 0 || t.b137 == 0 || (t.b136 & (t.b136 - 1)) != 0 || (t.b137 & (t.b137 - 1)) != 0 || t.b136 == t.b137) return false;
  if (t.slot_lines != 4 || t.slot_unknown != 0) return false;
  for (uint32_t k = 0; k < 4; ++k) {
    if (t.slot_seen[k] != 1 || t.slot[k] >= 32) return false;
  }
  return true;
}
static_assert(WaterTableIsWellFormed(kWaterTable), "water_table.inc is not what water_record.h reads (regenerate it: gen_water_table.py)");

inline constexpr uint64_t kWaterVs = kWaterTable.vs, kWaterPs = kWaterTable.ps, kWaterOpaquePs = kWaterTable.ps_opaque;
static_assert(kWaterVs == render::kWaterVsHash, "the table's vertex shader is not the discovery rows' (texture_decode.h)");
inline constexpr uint8_t kWaterVsRegister[kWaterVsRegisters] = {kWaterTable.vs_reg[0], kWaterTable.vs_reg[1], kWaterTable.vs_reg[2],
                                                                kWaterTable.vs_reg[3]};
// (bit k: component k is read by a formula)
inline constexpr uint8_t kWaterVsRead[kWaterVsRegisters] = {kWaterTable.vs_read[0], kWaterTable.vs_read[1], kWaterTable.vs_read[2],
                                                            kWaterTable.vs_read[3]};
namespace water_detail {
struct PsColumns {
  uint8_t reg[kWaterPsRegisters], read[kWaterPsRegisters], kind[kWaterPsRegisters];
};
constexpr PsColumns Columns() {
  PsColumns c{};
  for (uint32_t i = 0; i < kWaterPsRegisters; ++i) {
    c.reg[i] = kWaterTable.ps_reg[i];
    c.read[i] = kWaterTable.ps_read[i];
    c.kind[i] = kWaterTable.ps_kind[i];
  }
  return c;
}
inline constexpr PsColumns kPs = Columns();
}  // namespace water_detail
// The pixel registers, in the record's order; which components a formula reads; and the bound's kind: 0 a place
// within kWaterPlaceMax, 1 a matrix term within kWaterMatrixMax, 2 a factor or direction within kWaterFactorMax,
// 3 a factor that must be above 0, in [kWaterFactorMin, kWaterFactorMax] (ledger ruling 34).
inline constexpr const uint8_t* kWaterPsRegister = water_detail::kPs.reg;
inline constexpr const uint8_t* kWaterPsRead = water_detail::kPs.read;
inline constexpr const uint8_t* kWaterPsKind = water_detail::kPs.kind;
// The enumerators above are the table's order (a reader indexes a record's registers by them).
static_assert(kWaterVsRegister[kWatV115] == 115 && kWaterVsRegister[kWatV116] == 116 && kWaterVsRegister[kWatV117] == 117 &&
              kWaterVsRegister[kWatV118] == 118,
              "the vertex registers' names are not the table's order");
static_assert(water_detail::kPs.reg[kWatP0] == 0 && water_detail::kPs.reg[kWatP19] == 19 && water_detail::kPs.reg[kWatP20] == 20 &&
              water_detail::kPs.reg[kWatP28] == 28 && water_detail::kPs.reg[kWatP31] == 31 && water_detail::kPs.reg[kWatP66] == 66 &&
              water_detail::kPs.reg[kWatP67] == 67 && water_detail::kPs.reg[kWatP68] == 68 && water_detail::kPs.reg[kWatP116] == 116 &&
              water_detail::kPs.reg[kWatP117] == 117 && water_detail::kPs.reg[kWatP126] == 126 && water_detail::kPs.reg[kWatP127] == 127 &&
              water_detail::kPs.reg[kWatP128] == 128 && water_detail::kPs.reg[kWatP129] == 129 && water_detail::kPs.reg[kWatP130] == 130 &&
              water_detail::kPs.reg[kWatP131] == 131 && water_detail::kPs.reg[kWatP136] == 136 && water_detail::kPs.reg[kWatP137] == 137 &&
              water_detail::kPs.reg[kWatP138] == 138 && water_detail::kPs.reg[kWatP139] == 139 && water_detail::kPs.reg[kWatP140] == 140 &&
              water_detail::kPs.reg[kWatP141] == 141 && water_detail::kPs.reg[kWatP142] == 142 && water_detail::kPs.reg[kWatP143] == 143,
              "the pixel registers' names are not the table's order");
// What the rules below name by component is what the table says a formula reads there.
static_assert(water_detail::kPs.read[kWatP0] == 0xF && water_detail::kPs.read[kWatP20] == 0x8 && water_detail::kPs.read[kWatP31] == 0x3 &&
              water_detail::kPs.read[kWatP131] == 0x1 && water_detail::kPs.read[kWatP141] == 0xB && water_detail::kPs.read[kWatP28] == 0x3 &&
              water_detail::kPs.read[kWatP66] == 0x7 && water_detail::kPs.read[kWatP67] == 0xD && water_detail::kPs.read[kWatP68] == 0x7 &&
              water_detail::kPs.kind[kWatP128] == 3 && water_detail::kPs.kind[kWatP136] == 3 && water_detail::kPs.kind[kWatP138] == 3,
              "the named bounds and the fog rule are not on the components the table reads");
inline constexpr uint32_t kWaterB136 = kWaterTable.b136;   // the sun's glint: c136, c137, c138, c142, c143 are read under it alone
inline constexpr uint32_t kWaterB137 = kWaterTable.b137;   // the screen shadow mask on the reflection: c139 is read under it alone
inline constexpr uint32_t kWaterFetchSlot[4] = {kWaterTable.slot[0], kWaterTable.slot[1], kWaterTable.slot[2], kWaterTable.slot[3]};
static_assert(kWaterFetchSlot[kWatMask] == 1 && kWaterFetchSlot[kWatNormal] == 2 && kWaterFetchSlot[kWatPicture] == 13 &&
              kWaterFetchSlot[kWatNear] == 14);
// The vertex stream: position 32_32_32_FLOAT (Xenos vertex format 57) at dword 0, the mask's pair 32_32_FLOAT
// (37) at dword 3, of a 5-dword vertex.
inline constexpr uint32_t kWaterStrideDwords = 5, kWaterPairOffsetDwords = 3;
inline constexpr uint32_t kWaterPositionFormat = 57, kWaterPairFormat = 37;
// The scene's size, whose reciprocal a draw's c0.zw must be (the `register` rule): the game's main scene and the
// clay target (render::ClayPass::kWidth, kHeight: the pass holds the two equal where it reads the records).
inline constexpr uint32_t kWaterSceneWidth = 1120, kWaterSceneHeight = 720;
// The bounds by kind: sanity bounds that keep garbage out, fitted to no capture (sub-project 12's ledger ruling
// 54; its final review, I2). What holds a value to its truth is another rule where there is one: the fog's
// registers by WaterFogMatches, c0.zw by the scene's size, c20.w by the frame's output scale (the pass).
inline constexpr float kWaterPlaceMax = 1e6f, kWaterMatrixMax = 1e7f, kWaterFactorMax = 65536.0f;
inline constexpr float kWaterFactorMin = 1.0f / 65536.0f;                          // kind 3: strictly above 0 (ledger ruling 34)
inline constexpr uint32_t kWaterVsKind = 2;                                        // the vertex registers' kind (the table has no column for it)
// The named bounds, each with a meaning of its own: c31.xy in (0, kWaterViewFactorMax] (the mirrored camera's two
// factors); c141.w in [0, 1] (a threshold on an alpha); c0.zw the reciprocal
// of the scene's size within kWaterTexelTolerance, relative; c20.w in (0, kWaterScaleMax].
// (Ledger ruling 42 of sub-project 13, after the review of Tasks 4 to 15, I3: c131.x, c128.x and c136.x have
// their KIND's bound alone. The spec's "c131.x in [0, 64]" and "c128.x, c136.x at most 64" refused values the
// formula takes: it saturates c131.x, so a negative one is valid and means no reflection; and 64 was a multiple of
// one region's values. A bound may refuse only garbage: one refused record costs the frame its whole lake.)
inline constexpr float kWaterViewFactorMax = 4.0f, kWaterScaleMax = 64.0f, kWaterTexelTolerance = 1e-6f;

enum class WaterKind : uint8_t { kNone, kSurface, kOpaque };                       // A17D; F6D9
inline const char* WaterKindName(WaterKind k) { return k == WaterKind::kSurface ? "surface" : k == WaterKind::kOpaque ? "opaque" : "none"; }
inline constexpr const char* kWaterRecordReason = "water-record";
// Why a draw of the pair gave no record, or the frame's records were taken out; kNone: neither.
enum class WaterReason : uint8_t {
  kNone,
  kUnread,      // the device, a shader, a register, a fetch constant, the state words or the bool word's last writer could
                // not be read; a draw of the pair whose draw record is itself skipped; a draw seen that was never answered
  kShader,      // the vertex shader is not the one the pixel shader's formula is checked with
  kLiteral,     // the pixel shader object's own literal table is not the table's
  kFetchList,   // the vertex shader's fetches are not one stream of 5 dwords with the position at 0 and the pair at 3
  kRegister,    // a component a formula reads is no number or outside its bound (garbage alone), or a named bound fails
  kScale,       // c20.w outside (0, 64]
  kState,       // the state words are not the kind's
  kFetch,       // slot 1, 2 or 13 is not the mask's, the normal map's or the picture's fetch
  kShadowTerm,  // bool b137 set: the screen shadow mask on the reflection, which no capture exercises (spec ruling 21)
  kFog,         // the record's fog is not the frame's validated block (FrameBuilder::Finish, WaterFogMatches)
  kOverLimit,   // the 65th record of a frame
  kCount
};
inline const char* WaterReasonName(WaterReason r) {
  static constexpr const char* kNames[] = {"none", "unread", "shader", "literal", "fetch-list", "register", "scale", "state",
                                           "fetch", "shadow-term", "fog", "over-limit"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(WaterReason::kCount));
  return r < WaterReason::kCount ? kNames[size_t(r)] : "?";
}

// One water draw of the frame. An invalid record is all zero (ps_hash 0: WaterRecordValid).
struct WaterRecord {
  uint64_t ps_hash, vs_hash;
  uint32_t place;                            // DrawRecord::seq of its draw (FrameBuilder::NextSeq)
  uint32_t kind;                             // WaterKind
  uint32_t pbool;                            // the pixel bool word as its last writer left it on the GPU
  uint32_t stride_dwords;                    // the vertex stream's stride (kWaterStrideDwords)
  float vs[kWaterVsRegisters][4];            // c115..c118
  float ps[kWaterPsRegisters][4];            // the table's pixel registers, in its order; unread components as the game left them
  uint32_t fetch[4][6];                      // slots 1, 2, 13, 14
  uint32_t fog_base[2];                      // the base addresses of slots 4 and 5 at the draw
};
// 16 + 16 + 64 + 384 + 96 + 8 (recomputed by plan Task 9 from what is carried: the plan's 584 stands). The state
// words and the alpha test's reference are not here: they are the draw record's (DrawRecord::state_words), which
// `place` names. So a record's two thresholds are two numbers in two places: ps[kWatP141][3] (shaded against
// colour 0, inside the shader) and the draw's alpha reference (drawn against not).
static_assert(sizeof(WaterRecord) == 584, "WaterRecord grew or shrank: the documents state its size");

inline bool WaterRecordValid(const WaterRecord& r) { return r.ps_hash != 0; }

// What was read at the draw (capture.cpp fills it on the no-albedo branch).
struct WaterDraw {
  bool read = false;                         // the device, both shaders, every register, constant and fetch were read
  bool pbool_known = false;                  // the last writer's bool word could be said (capture.cpp, ImpostorBoolWord's rule)
  uint64_t ps_hash = 0, vs_hash = 0;
  bool literals_match = false;               // the pixel shader object's own literal table holds the table's c254 and c255
  uint32_t streams = 0, stride_dwords = 0;   // the vertex shader's fetch list
  uint32_t position_format = 0, position_offset = 0, pair_format = 0, pair_offset = 0;   // 57 at 0, 37 at 3
  float vs[kWaterVsRegisters][4] = {};
  float ps[kWaterPsRegisters][4] = {};
  uint32_t pbool = 0, fetch[4][6] = {}, fog_base[2] = {};
};

// kNone unless the pair is BEAD with A17D (the surface) or F6D9 (the opaque twin).
constexpr WaterKind WaterKindOf(uint64_t vs_hash, uint64_t ps_hash) {
  if (vs_hash != kWaterVs) return WaterKind::kNone;
  return ps_hash == kWaterPs ? WaterKind::kSurface : ps_hash == kWaterOpaquePs ? WaterKind::kOpaque : WaterKind::kNone;
}

// Which of the pair's two pixel shaders `ps_hash` is, whatever the vertex shader: what tells a draw of the water
// from every other no-albedo draw before anything of it is read (capture.cpp, ObserveWaterDraw).
constexpr WaterKind WaterKindOfPs(uint64_t ps_hash) {
  return ps_hash == kWaterPs ? WaterKind::kSurface : ps_hash == kWaterOpaquePs ? WaterKind::kOpaque : WaterKind::kNone;
}

// The vertex shader's fetch list as the `fetch-list` rule reads it, from its decoded vertex fetches
// (vfetch_decode.h, DecodeVertexFetches, in the shader's order): the pair's shader has two, the position and the
// mask's pair, both of one stream. `streams`: the distinct fetch slots among them; the stride is the first
// fetch's. Any other number of fetches leaves the list empty (streams 0: not the shape).
inline void WaterFetchList(const VertexFetch* fetches, size_t count, WaterDraw* d) {
  d->streams = d->stride_dwords = d->position_format = d->position_offset = d->pair_format = d->pair_offset = 0;
  if (!fetches || count != 2) return;
  d->streams = fetches[0].fetch_slot == fetches[1].fetch_slot ? 1 : 2;
  d->stride_dwords = fetches[0].stride_dwords;
  d->position_format = fetches[0].format;
  d->position_offset = uint32_t(fetches[0].offset_dwords);
  d->pair_format = fetches[1].format;
  d->pair_offset = uint32_t(fetches[1].offset_dwords);
}

// Whether a pixel shader object's own literal table is the table's: `bits[r][c]`: component c of the shader's own
// register 248 + r as its table holds it; bit r * 4 + c of `have`: the table holds that dword at all
// (wind_inputs.h, AddWindLiteralRun, fills both). Every component of c254 and c255 must be held and bit-equal,
// the two no formula reads too (c254.z, .w): they are the same in every captured table, and a table that differs
// in them is not the shader the formulas were checked with.
inline bool WaterLiteralsMatch(const uint32_t (*bits)[4], uint32_t have) {
  if (!bits) return false;
  for (const WaterTable::Literal& l : kWaterTable.literal) {
    if (l.reg < 248) return false;
    const uint32_t r = uint32_t(l.reg) - 248;
    for (uint32_t c = 0; c < 4; ++c) {
      if (!((have >> (r * 4 + c)) & 1u) || bits[r][c] != l.bits[c]) return false;
    }
  }
  return true;
}

namespace water_detail {
// A value inside its kind's bound (false for no number).
inline bool Bounded(uint32_t kind, float v) {
  switch (kind) {
    case 0: return std::fabs(v) <= kWaterPlaceMax;
    case 1: return std::fabs(v) <= kWaterMatrixMax;
    case 2: return std::fabs(v) <= kWaterFactorMax;
    default: return v >= kWaterFactorMin && v <= kWaterFactorMax;
  }
}
// Whether a record's pixel formula reads register `index` at all with the bool word `pbool`: c136, c137, c138,
// c142 and c143 only under b136; c139 only under b137 (light_formulas.WATER_SHADER["ps_read_under"]), and a draw
// with b137 set gives no record whatever c139 holds (`shadow-term`), so c139 is read by no record. A register that
// is not read is carried as it is and not judged, as an unread component is.
constexpr bool PsRegisterRead(uint32_t index, uint32_t pbool) {
  if (index == kWatP136 || index == kWatP137 || index == kWatP138 || index == kWatP142 || index == kWatP143) return (pbool & kWaterB136) != 0;
  return index != kWatP139;
}
// A 2D k_8_8_8_8 picture of 256 x 256 with a base, its three colour components delivered as unsigned fractions
// (no sign field, gamma among them, on the channel each colour's swizzle names: render::FetchComponentsUnsigned,
// ledger ruling 41; the fraction number format; no exponent adjust).
inline bool PictureFetch(const uint32_t fc[6]) {
  return render::WaterPictureFetch(fc) && (fc[1] & 0xFFFFF000u) != 0 && render::FetchComponentsUnsigned(fc, 3) && (fc[3] & 1u) == 0 &&
         ((fc[3] >> 13) & 0x3Fu) == 0;
}
}  // namespace water_detail

// The `register` rule's bounds on a draw's registers, but for the two that need more than the registers (c0.zw
// against the scene's size; c20.w, which is `scale`): every component A FORMULA READS with the bool word `pbool`
// a number inside its kind's bound (c131.x within 65,536; c128.x and, under b136, c136.x in [1 / 65,536,
// 65,536]), and the named bounds (c31.xy in (0, 4]; c141.w in [0, 1]). Nothing else is looked at. One function, so that the pass judges a
// record again by the very rule the capture took it by (render::MakeClayWaterConstants).
inline bool WaterRegistersBounded(const float vs[kWaterVsRegisters][4], const float ps[kWaterPsRegisters][4], uint32_t pbool) {
  for (uint32_t i = 0; i < kWaterVsRegisters; ++i) {
    for (uint32_t k = 0; k < 4; ++k) {
      // (a scrolling offset or a scale: a factor, as gen_water_table.VS_KIND has every vertex register of the table)
      if (((kWaterVsRead[i] >> k) & 1u) && !water_detail::Bounded(kWaterVsKind, vs[i][k])) return false;
    }
  }
  for (uint32_t i = 0; i < kWaterPsRegisters; ++i) {
    if (!water_detail::PsRegisterRead(i, pbool)) continue;
    for (uint32_t k = 0; k < 4; ++k) {
      if (i == kWatP20 && k == 3) continue;   // (the scale: WaterScaleBounded)
      if (((kWaterPsRead[i] >> k) & 1u) && !water_detail::Bounded(kWaterPsKind[i], ps[i][k])) return false;
    }
  }
  const float* c31 = ps[kWatP31];
  if (!(c31[0] > 0.0f && c31[0] <= kWaterViewFactorMax && c31[1] > 0.0f && c31[1] <= kWaterViewFactorMax)) return false;
  return ps[kWatP141][3] >= 0.0f && ps[kWatP141][3] <= 1.0f;
}
// c20.w, the output scale: in (0, 64] (false for no number).
constexpr bool WaterScaleBounded(float c20w) { return c20w > 0.0f && c20w <= kWaterScaleMax; }

// kNone, or the first rule that fails, in this order:
//   - the draw and its state words were read and the bool word's last writer is known (kUnread);
//   - the pair (kShader);
//   - the pixel shader's own literal table (kLiteral);
//   - one stream of 5 dwords with the position at 0 and the pair at 3 in their formats (kFetchList);
//   - every component A FORMULA READS a number inside its kind's bound; c31.xy in (0, 4]; c141.w in [0, 1]; c0.zw the reciprocal of the scene's size within 1e-6
//     relative (kRegister). A component no formula reads is never looked at, not even for being a number, and
//     neither is a register that is read only under a bool the draw has clear (c139 never: below). c20.w is
//     judged apart: in (0, 64] (kScale);
//   - the state, by its decoded fields (render_state.h; not whole words: bits whose meaning is not established
//     are not compared): valid; the depth test and write on under the reversed range; four colour channels; the
//     alpha test on with the function "greater" and a reference in [0, 1]; no alpha-to-mask; and for the surface
//     the depth function 4 ("greater") and the colour blend source alpha, inverse source alpha, add; for the
//     opaque twin the depth function 6 ("greater or equal") and no blend: the fields of the measured words
//     0x00708746 / 0x07060706 and 0x00708766 / 0x00010001, both with colour control 0x0000000C (kState);
//   - slot 1 a mask (render::WaterTextureFetch, kK8: a 2D k_8 texture delivered as an unsigned fraction, ledger
//     rulings 33 and 41; any size a 2D constant can name, ruling 42); slot 2 a normal map (kDxn: likewise); slot 13 a
//     2D k_8_8_8_8 picture of 256 x 256 with a base and without a sign, gamma among them. Slot 14 is no texture
//     of the native draw (spec ruling 21) and is not judged (kFetch);
//   - bool b137 clear (kShadowTerm).
// Both kinds are judged alike but for the state: the opaque twin is the same formula with another kill, and its
// rows hold the same registers and fetches (164 of 164 captured rows pass these rules: plan Task 9's report).
inline WaterReason CheckWaterDraw(const WaterDraw& d, const StateWords& state, uint32_t scene_width, uint32_t scene_height) {
  if (!d.read || !state.read || !d.pbool_known) return WaterReason::kUnread;
  const WaterKind kind = WaterKindOf(d.vs_hash, d.ps_hash);
  if (kind == WaterKind::kNone) return WaterReason::kShader;
  if (!d.literals_match) return WaterReason::kLiteral;
  if (d.streams != 1 || d.stride_dwords != kWaterStrideDwords || d.position_format != kWaterPositionFormat || d.position_offset != 0 ||
      d.pair_format != kWaterPairFormat || d.pair_offset != kWaterPairOffsetDwords) {
    return WaterReason::kFetchList;
  }
  if (!WaterRegistersBounded(d.vs, d.ps, d.pbool)) return WaterReason::kRegister;
  if (scene_width == 0 || scene_height == 0) return WaterReason::kRegister;
  {
    const float du = 1.0f / float(scene_width), dv = 1.0f / float(scene_height);
    if (!(std::fabs(d.ps[kWatP0][2] - du) <= kWaterTexelTolerance * du) || !(std::fabs(d.ps[kWatP0][3] - dv) <= kWaterTexelTolerance * dv)) {
      return WaterReason::kRegister;
    }
  }
  if (!WaterScaleBounded(d.ps[kWatP20][3])) return WaterReason::kScale;
  const RenderState s = DecodeRenderState(state);
  if (!s.valid || !s.depth_test || !s.depth_write || !s.depth_reversed || s.color_mask != 0xF || !s.alpha_test ||
      s.alpha_func != Compare::kGreater || !(s.alpha_ref >= 0.0f && s.alpha_ref <= 1.0f) || s.alpha_to_mask) {
    return WaterReason::kState;
  }
  if (kind == WaterKind::kSurface) {
    if (s.depth_func != Compare::kGreater || !s.blend || s.src != Factor::kSrcAlpha || s.dst != Factor::kInvSrcAlpha || s.op != BlendOp::kAdd) {
      return WaterReason::kState;
    }
  } else if (s.depth_func != Compare::kGreaterEqual || s.blend) {
    return WaterReason::kState;
  }
  render::TextureFetch unused;
  if (render::WaterTextureFetch(d.fetch[kWatMask], render::WaterDecode::kK8, &unused) != render::FetchError::kNone ||
      render::WaterTextureFetch(d.fetch[kWatNormal], render::WaterDecode::kDxn, &unused) != render::FetchError::kNone ||
      !water_detail::PictureFetch(d.fetch[kWatPicture])) {
    return WaterReason::kFetch;
  }
  if (d.pbool & kWaterB137) return WaterReason::kShadowTerm;
  return WaterReason::kNone;
}

// What a refused draw was refused FOR, as text for the log's one line a reason (ledger ruling 42: a reason that
// empties a frame's water is said at once, with the draw's shaders and the value, not only counted): `why` is
// CheckWaterDraw's answer for this draw. The shaders' hashes and the bool word always; then by reason the first
// register component outside its bound with its value and bits ("register"), c20.w ("scale"), the state words
// ("state"), the first refused slot's constant ("fetch"), the stream's shape ("fetch-list"). Nothing is judged
// here that CheckWaterDraw does not judge: the same rules are asked again, in its order.
inline std::string DescribeWaterRefusal(const WaterDraw& d, const StateWords& state, WaterReason why, uint32_t scene_width, uint32_t scene_height) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "vs %016llX ps %016llX pbool %08X", static_cast<unsigned long long>(d.vs_hash),
                static_cast<unsigned long long>(d.ps_hash), d.pbool);
  std::string text = buf;
  auto component = [&](const char* bank, uint32_t reg, uint32_t k, float v, const char* rule) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    std::snprintf(buf, sizeof(buf), ": %s c%u.%c = %g (%08X), %s", bank, reg, "xyzw"[k & 3], double(v), bits, rule);
    text += buf;
  };
  switch (why) {
    case WaterReason::kRegister: {
      for (uint32_t i = 0; i < kWaterVsRegisters; ++i) {
        for (uint32_t k = 0; k < 4; ++k) {
          if (((kWaterVsRead[i] >> k) & 1u) && !water_detail::Bounded(kWaterVsKind, d.vs[i][k])) {
            component("vertex", kWaterVsRegister[i], k, d.vs[i][k], "outside its kind's bound");
            return text;
          }
        }
      }
      for (uint32_t i = 0; i < kWaterPsRegisters; ++i) {
        if (!water_detail::PsRegisterRead(i, d.pbool)) continue;
        for (uint32_t k = 0; k < 4; ++k) {
          if (i == kWatP20 && k == 3) continue;
          if (((kWaterPsRead[i] >> k) & 1u) && !water_detail::Bounded(kWaterPsKind[i], d.ps[i][k])) {
            component("pixel", kWaterPsRegister[i], k, d.ps[i][k], "outside its kind's bound");
            return text;
          }
        }
      }
      for (uint32_t k = 0; k < 2; ++k) {
        if (!(d.ps[kWatP31][k] > 0.0f && d.ps[kWatP31][k] <= kWaterViewFactorMax)) {
          component("pixel", 31, k, d.ps[kWatP31][k], "not in (0, 4]");
          return text;
        }
      }
      if (!(d.ps[kWatP141][3] >= 0.0f && d.ps[kWatP141][3] <= 1.0f)) {
        component("pixel", 141, 3, d.ps[kWatP141][3], "not in [0, 1]");
        return text;
      }
      std::snprintf(buf, sizeof(buf), ": pixel c0.zw = (%g, %g), not the reciprocal of the scene's size %u x %u", double(d.ps[kWatP0][2]),
                    double(d.ps[kWatP0][3]), scene_width, scene_height);
      return text + buf;
    }
    case WaterReason::kScale:
      component("pixel", 20, 3, d.ps[kWatP20][3], "not in (0, 64]");
      return text;
    case WaterReason::kState:
      std::snprintf(buf, sizeof(buf), ": state words depth %08X blend %08X colour control %08X alpha ref %08X colour mask %08X cull %08X z scale %08X%s",
                    state.depth_control, state.blend_control, state.color_control, state.alpha_ref, state.color_mask, state.su_sc_mode,
                    state.vport_z_scale, state.read ? "" : " (not read)");
      return text + buf;
    case WaterReason::kFetch: {
      render::TextureFetch unused;
      const bool mask = render::WaterTextureFetch(d.fetch[kWatMask], render::WaterDecode::kK8, &unused) == render::FetchError::kNone;
      const bool normal = render::WaterTextureFetch(d.fetch[kWatNormal], render::WaterDecode::kDxn, &unused) == render::FetchError::kNone;
      const uint32_t at = !mask ? kWatMask : !normal ? kWatNormal : kWatPicture;
      const uint32_t* fc = d.fetch[at];
      const char* rule = at == kWatPicture ? "not a 256 x 256 k_8_8_8_8 picture delivered unsigned"
                                           : render::FetchErrorName(render::WaterTextureFetch(fc, at == kWatMask ? render::WaterDecode::kK8 : render::WaterDecode::kDxn, &unused));
      std::snprintf(buf, sizeof(buf), ": slot %u = %08X %08X %08X %08X %08X %08X, %s", kWaterFetchSlot[at], fc[0], fc[1], fc[2], fc[3], fc[4], fc[5], rule);
      return text + buf;
    }
    case WaterReason::kFetchList:
      std::snprintf(buf, sizeof(buf), ": %u streams, stride %u dwords, position format %u at %u, pair format %u at %u", d.streams, d.stride_dwords,
                    d.position_format, d.position_offset, d.pair_format, d.pair_offset);
      return text + buf;
    case WaterReason::kUnread:
      std::snprintf(buf, sizeof(buf), ": %s", !d.read ? "the draw could not be read whole" : !state.read ? "the state words could not be read"
                                                : !d.pbool_known ? "the bool word's last writer is not known" : "its draw record was skipped");
      return text + buf;
    case WaterReason::kLiteral: return text + ": the pixel shader's own literal table is not the table's";
    case WaterReason::kShader: return text + ": not the pair's vertex shader";
    case WaterReason::kShadowTerm: return text + ": bool b137 is set";
    default: return text;
  }
}

// The record of a draw: CheckWaterDraw, then the copy (whole registers, unread components as they are). No
// record (all zero) on any failure. `place` is 0: the caller sets it (the draw's seq).
inline WaterRecord MakeWaterRecord(const WaterDraw& d, const StateWords& state, uint32_t scene_width, uint32_t scene_height, WaterReason* why) {
  const WaterReason reason = CheckWaterDraw(d, state, scene_width, scene_height);
  if (why) *why = reason;
  if (reason != WaterReason::kNone) return WaterRecord{};
  WaterRecord r{};
  r.ps_hash = d.ps_hash;
  r.vs_hash = d.vs_hash;
  r.kind = uint32_t(WaterKindOf(d.vs_hash, d.ps_hash));
  r.pbool = d.pbool;
  r.stride_dwords = d.stride_dwords;
  std::memcpy(r.vs, d.vs, sizeof(r.vs));
  std::memcpy(r.ps, d.ps, sizeof(r.ps));
  std::memcpy(r.fetch, d.fetch, sizeof(r.fetch));
  std::memcpy(r.fog_base, d.fog_base, sizeof(r.fog_base));
  return r;
}

// Whether a record's fog is the frame's validated block (the `fog` rule, taken at FrameBuilder::Finish where the
// frame's block and its tables' addresses are both in hand): its slots 4 and 5 at the frame's two tables by base
// address (`table_base`: FogSource::base, [0] tf4's and [1] tf5's; an address of 0 is no table), and c28.xy,
// c66.xyz, c67.x, .z, .w and c68.xyz bit-equal to the block's amount, axis, range and near colour (c67.y is the
// sky's path height, which the water's fog does not read). False for an invalid block. This rule, not the
// table's bound, is what holds the fog's registers to their truth: the native surface is fogged from the FRAME's
// block (cbuffer Frame), so a record whose fog is another's must not be drawn with it.
inline bool WaterFogMatches(const WaterRecord& r, const FogBlock& block, const uint32_t table_base[2]) {
  if (!block.valid || !table_base || table_base[0] == 0 || table_base[1] == 0) return false;
  if (r.fog_base[0] != table_base[0] || r.fog_base[1] != table_base[1]) return false;
  if (std::memcmp(r.ps[kWatP28], block.amount, 2 * sizeof(float)) != 0) return false;
  if (std::memcmp(r.ps[kWatP66], block.axis, 3 * sizeof(float)) != 0) return false;
  const float range[3] = {r.ps[kWatP67][0], r.ps[kWatP67][2], r.ps[kWatP67][3]};
  const float want[3] = {block.range[0], block.range[2], block.range[3]};
  if (std::memcmp(range, want, sizeof(range)) != 0) return false;
  return std::memcmp(r.ps[kWatP68], block.near_colour, 3 * sizeof(float)) == 0;
}

}  // namespace fable2::native::capture
