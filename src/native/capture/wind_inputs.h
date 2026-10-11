#pragma once

// The wind inputs of one recorded draw (pure: no SDK/GPU deps). Foliage sub-project, phase 11a:
// spec Design 5, rulings 7, 9, 10, 12; the ledger's rulings 26, 28, 30, 31; frame-map section 18,
// "Wind inputs"; the formulas are docs/native-renderer/lighting-formulas.md, "Wind".
//
// The game sways its trees and plants in twelve vertex shaders of three families (trunk, leaf,
// instanced plant; three of the twelve are the shadow casters' own). What a family's formula reads
// per draw is a handful of vertex registers (one of them the clock), a set of numbers the shader
// loads itself (its literal table), and for a tree one to three vertex elements. wind_table.inc
// (generated from the "wind" members of docs/native-renderer/vs-transforms.json) names all of it
// per shader; MakeWindInputs turns what the capture read at a draw into WindInputs or a reason.
// Nothing here moves a vertex: the pass does (plan Task 10), from these inputs and the per-vertex
// words of the geometry cache (render/wind_decode.h).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "material.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

// wind_table.inc's "family". A tree is a trunk or a leaf: the two read the same registers.
enum class WindFamily : uint8_t { kNone = 0, kTrunk = 1, kLeaf = 2, kPlant = 3, kCount };

inline const char* WindFamilyName(WindFamily f) {
  switch (f) {
    case WindFamily::kNone: return "none";
    case WindFamily::kTrunk: return "trunk";
    case WindFamily::kLeaf: return "leaf";
    case WindFamily::kPlant: return "plant";
    default: return "?";
  }
}

// Why a draw has no wind inputs and is drawn at rest (kNone: it has them).
enum class WindReason : uint8_t {
  kNone,
  kOff,                // the wind was not wanted this frame: nothing was looked up or read
  kNoEntry,            // the vertex shader has no row in the table (every draw that does not sway)
  kRegisterUnread,     // a register the family reads could not be read from the device's bank
  kRegisterNotFinite,  // ... holds a value that is no number
  kRegisterRange,      // ... lies outside its bound (WindRegisterInBounds)
  kLiteral,            // the shader object's own literal table is not the row's, literal by literal
  kElement,            // an element is not the fetch the row names, or the shader does not receive it
  kStream,             // an element's stream is not resolved or does not hold the draw's largest vertex
  kInstance,           // a plant without its instance set, or a tree with one
  kOverLimit,          // the frame's side array is full (frame_scene.h)
  kCount
};

inline const char* WindReasonName(WindReason r) {
  switch (r) {
    case WindReason::kNone: return "none";
    case WindReason::kOff: return "off";
    case WindReason::kNoEntry: return "no-entry";
    case WindReason::kRegisterUnread: return "register-unread";
    case WindReason::kRegisterNotFinite: return "register-not-finite";
    case WindReason::kRegisterRange: return "register-range";
    case WindReason::kLiteral: return "literal";
    case WindReason::kElement: return "element";
    case WindReason::kStream: return "stream";
    case WindReason::kInstance: return "instance";
    case WindReason::kOverLimit: return "over-limit";
    default: return "?";
  }
}

// The most registers a family reads (the plants' five), the most literals (a leaf's 28: seven
// registers of four; a trunk 18, a plant 7), the most vertex elements (a leaf's three).
inline constexpr uint32_t kWindRegisters = 5, kWindLiterals = 28, kWindElements = 3;
inline constexpr uint8_t kWindNoRegister = 0xFF;
enum : uint32_t { kWindPivot = 0, kWindWeights = 1, kWindFlutter = 2 };  // WindSpec::element, WindInputs::element

// The literals a family's formula reads (light_formulas.WIND_LITERAL_ORDER: a slot is a place in
// that order, and the order of the native constants).
constexpr uint32_t WindLiteralCount(WindFamily f) {
  return f == WindFamily::kTrunk ? 18u : f == WindFamily::kLeaf ? 28u : f == WindFamily::kPlant ? 7u : 0u;
}

// The vertex registers a shader's literal table may name for a wind literal: c248..c255 (the
// twelve shaders' tables begin at c248 or c252). The capture reads a shader object's table over
// exactly this range (capture.cpp, ReadWindLiterals).
inline constexpr uint32_t kWindLiteralFirst = 248, kWindLiteralRegisters = 8;

// One row of wind_table.inc with its literal lines.
struct WindSpec {
  uint64_t vs_hash;
  WindFamily family;
  bool caster;                    // a shadow caster's shader (the leaves' is the tree occluder's too)
  uint8_t reg[kWindRegisters];    // trees: 15, 16, 17, none, none; plants: 10, 21, 22, 25, 26
  uint8_t lit_count;              // WindLiteralCount(family)
  // Literal `slot` of the family's order: the register and component this shader's own table holds
  // it at, and its bits. A slot past lit_count: kWindNoRegister, 0, 0.
  uint8_t lit_reg[kWindLiterals];
  uint8_t lit_comp[kWindLiterals];
  uint32_t lit_bits[kWindLiterals];
  LightFetchSpec element[kWindElements];   // fetch -1: the family has none there
  uint8_t source_count[kWindElements];     // 3 (the pivot), 4 (the weights), 3 (the flutter); 0: none
};

namespace wind_detail {

struct Row {
  uint64_t hash;
  uint8_t family, caster;
  uint8_t reg[kWindRegisters];
  LightFetchSpec element[kWindElements];
};
struct LiteralRow {
  uint64_t hash;
  uint8_t slot, reg, comp;
  uint32_t bits;
};

inline constexpr Row kRows[] = {
#define FABLE2_WIND(hash, family, caster, reg0, reg1, reg2, reg3, reg4, pivot_fetch, pivot_format, pivot_offset, \
                    pivot_source, weights_fetch, weights_format, weights_offset, weights_source, flutter_fetch,   \
                    flutter_format, flutter_offset, flutter_source)                                               \
  {hash, family, caster, {reg0, reg1, reg2, reg3, reg4},                                                          \
   {{pivot_fetch, pivot_format, pivot_offset, pivot_source},                                                      \
    {weights_fetch, weights_format, weights_offset, weights_source},                                              \
    {flutter_fetch, flutter_format, flutter_offset, flutter_source}}},
#define FABLE2_WIND_LITERAL(hash, slot, reg, comp, bits)
#include "wind_table.inc"
#undef FABLE2_WIND
#undef FABLE2_WIND_LITERAL
};

inline constexpr LiteralRow kLiteralRows[] = {
#define FABLE2_WIND(...)
#define FABLE2_WIND_LITERAL(hash, slot, reg, comp, bits) {hash, slot, reg, comp, bits},
#include "wind_table.inc"
#undef FABLE2_WIND
#undef FABLE2_WIND_LITERAL
};

inline constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

constexpr std::array<WindSpec, kRowCount> MakeSpecs() {
  std::array<WindSpec, kRowCount> out{};
  for (size_t i = 0; i < kRowCount; ++i) {
    WindSpec& s = out[i];
    const Row& r = kRows[i];
    s.vs_hash = r.hash;
    s.family = WindFamily(r.family);
    s.caster = r.caster != 0;
    for (uint32_t k = 0; k < kWindRegisters; ++k) s.reg[k] = r.reg[k];
    for (uint32_t e = 0; e < kWindElements; ++e) {
      s.element[e] = r.element[e];
      s.source_count[e] = r.element[e].fetch < 0 ? uint8_t(0) : uint8_t(r.element[e].source >> 8);
    }
    for (uint32_t k = 0; k < kWindLiterals; ++k) s.lit_reg[k] = kWindNoRegister;
    for (const LiteralRow& l : kLiteralRows) {
      if (l.hash != r.hash || l.slot >= kWindLiterals) continue;
      s.lit_reg[l.slot] = l.reg;
      s.lit_comp[l.slot] = l.comp;
      s.lit_bits[l.slot] = l.bits;
      if (s.lit_count < l.slot + 1) s.lit_count = uint8_t(l.slot + 1);
    }
  }
  return out;
}

}  // namespace wind_detail

inline constexpr size_t kWindSpecCount = wind_detail::kRowCount;
inline constexpr std::array<WindSpec, kWindSpecCount> kWindSpecs = wind_detail::MakeSpecs();

// What the generated table must be for the reader above and for everything that indexes it: no hash
// twice; a family; registers that are not literal registers; each literal slot of the family named
// exactly once, by a register inside [kWindLiteralFirst, 255] and a component; no literal line for
// a shader without a row or for a slot past the family's; a plant without elements, a trunk with
// the weights alone, a leaf with all three, each element's count of source components what the
// family reads. An index fits WindInputs::spec.
constexpr bool WindTableIsWellFormed() {
  if (kWindSpecCount == 0 || kWindSpecCount >= 0xFF) return false;
  for (size_t i = 0; i < kWindSpecCount; ++i) {
    const WindSpec& s = kWindSpecs[i];
    if (s.vs_hash == 0 || s.family == WindFamily::kNone || s.family >= WindFamily::kCount) return false;
    for (size_t j = 0; j < i; ++j) {
      if (kWindSpecs[j].vs_hash == s.vs_hash) return false;
    }
    const bool plant = s.family == WindFamily::kPlant;
    for (uint32_t k = 0; k < kWindRegisters; ++k) {
      const bool wanted = plant || k < 3;
      if (wanted != (s.reg[k] != kWindNoRegister) || (wanted && s.reg[k] >= kWindLiteralFirst)) return false;
    }
    if (s.lit_count != WindLiteralCount(s.family)) return false;
    for (uint32_t k = 0; k < s.lit_count; ++k) {
      if (s.lit_reg[k] < kWindLiteralFirst || s.lit_comp[k] > 3) return false;
      uint32_t lines = 0;
      for (const wind_detail::LiteralRow& l : wind_detail::kLiteralRows) lines += l.hash == s.vs_hash && l.slot == k ? 1 : 0;
      if (lines != 1) return false;
    }
    const uint32_t count[kWindElements] = {3, 4, 3};
    const bool has[kWindElements] = {s.family == WindFamily::kLeaf, !plant, s.family == WindFamily::kLeaf};
    for (uint32_t e = 0; e < kWindElements; ++e) {
      if (has[e] != (s.element[e].fetch >= 0) || s.source_count[e] != (has[e] ? count[e] : 0u)) return false;
    }
  }
  for (const wind_detail::LiteralRow& l : wind_detail::kLiteralRows) {
    bool found = false;
    for (const WindSpec& s : kWindSpecs) found = found || (s.vs_hash == l.hash && l.slot < s.lit_count);
    if (!found) return false;
  }
  return true;
}
static_assert(WindTableIsWellFormed(), "wind_table.inc is not what wind_inputs.h reads (regenerate it: gen_transform_table.py --wind-out)");

// The row of a vertex shader, or null (a shader that does not sway, or whose formula is not
// entered: 36B5 pushes only, 6AD4 moves nothing).
inline const WindSpec* FindWind(uint64_t vs_hash) {
  for (const WindSpec& s : kWindSpecs) {
    if (s.vs_hash == vs_hash) return &s;
  }
  return nullptr;
}

// A row's index in kWindSpecs (WindInputs::spec), 0xFF for a pointer that is no row.
inline uint8_t WindSpecIndex(const WindSpec* spec) {
  if (!spec || spec < kWindSpecs.data() || spec >= kWindSpecs.data() + kWindSpecCount) return 0xFF;
  return uint8_t(spec - kWindSpecs.data());
}

// Whether a shader object's own literal table is the row's (spec ruling 9), each literal alone
// (ledger ruling 31): `bits[r][c]` is component c of register kWindLiteralFirst + r as the object's
// table holds it, and bit r * 4 + c of `have` says the table holds that dword at all. Every literal
// of the row must be held, at the row's register and component, bit for bit. What else the table
// holds is not asked (the plants' c255.xyz are no numbers and no formula reads them).
inline bool WindLiteralsMatch(const WindSpec& spec, const uint32_t (*bits)[4], uint32_t have) {
  if (!bits || spec.lit_count == 0 || spec.lit_count > kWindLiterals) return false;
  for (uint32_t k = 0; k < spec.lit_count; ++k) {
    const uint32_t reg = spec.lit_reg[k], comp = spec.lit_comp[k];
    if (reg < kWindLiteralFirst || reg - kWindLiteralFirst >= kWindLiteralRegisters || comp > 3) return false;
    const uint32_t r = reg - kWindLiteralFirst;
    if (!((have >> (r * 4 + comp)) & 1u) || bits[r][comp] != spec.lit_bits[k]) return false;
  }
  return true;
}

// A shader's literal table is a list of runs: `dwords` big-endian values from register
// `first_register`, component x, on (xdk_layout.h, LiteralRun). How many of a run's dwords a reader
// of the window needs: those up to c255.w; 0 for a run that ends before the window or begins past
// register 255.
constexpr uint32_t WindLiteralRunDwords(uint32_t first_register, uint32_t dwords) {
  const uint32_t window_end = (kWindLiteralFirst + kWindLiteralRegisters) * 4;
  if (first_register >= kWindLiteralFirst + kWindLiteralRegisters) return 0;
  const uint32_t begin = first_register * 4;
  const uint32_t end = dwords < window_end - begin ? begin + dwords : window_end;
  return end > kWindLiteralFirst * 4 ? end - begin : 0;
}

// One run into the window: `values` are the run's first `dwords` values (big-endian bytes, at
// least WindLiteralRunDwords of them). A dword two runs name holds the later run's value, as the
// game loads them in order.
inline void AddWindLiteralRun(uint32_t first_register, uint32_t dwords, const uint8_t* values,
                              uint32_t bits[kWindLiteralRegisters][4], uint32_t* have) {
  if (!values || !bits || !have) return;
  const uint32_t n = WindLiteralRunDwords(first_register, dwords);
  for (uint32_t d = 0; d < n; ++d) {
    const uint32_t at = first_register * 4 + d;
    if (at < kWindLiteralFirst * 4) continue;
    const uint32_t k = at - kWindLiteralFirst * 4;
    const uint8_t* p = values + 4 * size_t(d);
    bits[k / 4][k % 4] = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
    *have |= 1u << k;
  }
}

// The wind inputs of one recorded draw (an entry of FrameScene::wind or ::shadow_wind). What the
// formula reads of the shader's literals is not here: it is the row's (kWindSpecs[spec].lit_bits),
// which the capture has held against the shader object's own table.
struct WindInputs {
  WindFamily family = WindFamily::kNone;
  uint8_t spec = 0xFF;                  // the row's index in kWindSpecs
  uint16_t pad = 0;
  // The row's registers as the device's vertex bank held them at the draw, all four components, bit
  // for bit (reg[i] is register WindSpec::reg[i]; zero where the family has none). reg[0][0] is the
  // clock (trees c15.x, plants c10.x; a tree's second clock is c17.w): seconds since the process
  // started, as the game's own single-precision register holds it. Nothing here rounds it.
  float reg[kWindRegisters][4] = {};
  // A tree's vertex elements: [kWindPivot] a leaf's pivot, [kWindWeights] the four weights,
  // [kWindFlutter] a leaf's three flutter values. Default (stride 0) where the family has none.
  LightElement element[kWindElements];
};
// Measured, so that a field added here is noticed: one entry a wind draw is copied into the frame on
// the guest thread (132 to 228 scene draws and 110 to 228 caster draws a frame in the captures).
static_assert(sizeof(WindInputs) == 132, "WindInputs grew or shrank: one entry a wind draw is copied on the guest thread");

// What the capture found for one draw.
struct WindDraw {
  const WindSpec* spec = nullptr;       // the vertex shader's row, null without one
  bool literals_match = false;          // WindLiteralsMatch on the shader object's own literal table
  RegisterView vs_regs;                 // the row's registers, as read
  const std::vector<VertexFetch>* fetches = nullptr;  // the vertex shader's fetches
  LightStream stream[kWindElements];    // the stream behind each element's fetch
  int64_t max_vertex = -1;              // DrawShaders::max_vertex: the largest vertex index the draw reads
  bool instanced = false;               // the draw has its instance set (DrawInputs::instances.active)
};

// The bounds of the registers (spec Design 5 and ruling 12: about four times the largest value
// captured; the ledger's ruling 26 admits c25.x = 0, a plant without sway, and ruling 28 widens
// |c17.xy| to 16: 12.5 occurs in the world's first frame after loading). One line a bounded
// component; a component without a line is not bounded (it is held to be a number all the same:
// MakeWindInputs). tests/test_wind_inputs.py holds these lines against wind_check.REGISTER_BOUNDS.
struct WindBound {
  uint8_t tree;     // 1: a trunk or a leaf; 0: a plant
  uint8_t slot;     // the register's place in WindSpec::reg
  uint8_t reg;      // the register itself (for the reader; the slot decides)
  uint8_t comp;
  float lo, hi;
};
inline constexpr WindBound kWindBounds[] = {
    {1, 0, 15, 0, -1e7f, 1e7f},
    {1, 0, 15, 1, 0.0f, 4.0f},
    {1, 0, 15, 2, 0.0f, 4.0f},
    {1, 0, 15, 3, 0.0f, 4.0f},
    {1, 1, 16, 0, -4.0f, 4.0f},
    {1, 1, 16, 1, -4.0f, 4.0f},
    {1, 1, 16, 2, -4.0f, 4.0f},
    {1, 1, 16, 3, -4.0f, 4.0f},
    {1, 2, 17, 0, -16.0f, 16.0f},
    {1, 2, 17, 1, -16.0f, 16.0f},
    {1, 2, 17, 2, 0.0f, 16.0f},
    {1, 2, 17, 3, -2e7f, 2e7f},
    {0, 0, 10, 0, -1e7f, 1e7f},
    {0, 1, 21, 0, -4.0f, 4.0f},
    {0, 1, 21, 1, -4.0f, 4.0f},
    {0, 1, 21, 2, -4.0f, 4.0f},
    {0, 2, 22, 0, -8.0f, 8.0f},
    {0, 2, 22, 1, -8.0f, 8.0f},
    {0, 2, 22, 2, -8.0f, 8.0f},
    {0, 3, 25, 0, 0.0f, 64.0f},
    {0, 4, 26, 0, 0.0f, 4.0f},
    {0, 4, 26, 1, 0.0f, 4.0f},
};

// Whether the four components `v` of the register at `slot` of a family's row lie inside their
// bounds (both edges inside; a value that is no number is outside). False for a slot the family has
// no register at, for no family, and without values.
inline bool WindRegisterInBounds(WindFamily family, uint32_t slot, const float v[4]) {
  if (!v || family == WindFamily::kNone || family >= WindFamily::kCount) return false;
  const bool tree = family != WindFamily::kPlant;
  if (slot >= (tree ? 3u : kWindRegisters)) return false;
  for (const WindBound& b : kWindBounds) {
    if ((b.tree != 0) != tree || b.slot != slot) continue;
    if (!(v[b.comp] >= b.lo && v[b.comp] <= b.hi)) return false;
  }
  return true;
}

// The wind inputs of a draw. On kNone `*out` is complete; otherwise `*out` is left default (no
// family) and the draw is drawn at rest. Fails closed, the first rule that fails naming the reason:
//   a row (kNoEntry);
//   the shader object's literals are the row's (kLiteral);
//   every register of the row read (kRegisterUnread), a number in all four components
//   (kRegisterNotFinite) and inside its bounds (kRegisterRange);
//   a plant: its instance set (kInstance), and no element;
//   a tree: each element the row names is that fetch (ordinal, format, offset), in a format a reader
//   decodes, inside one vertex, read through source components the shader receives (kElement); its
//   stream resolved and holding the draw's largest vertex (kStream); and no instance set (kInstance).
// A plant with c25.x = 0 and c26 = 0 has inputs: its formula moves nothing (ledger ruling 26).
inline WindReason MakeWindInputs(const WindDraw& d, WindInputs* out) {
  if (!out) return WindReason::kNoEntry;
  *out = WindInputs{};
  const uint8_t index = WindSpecIndex(d.spec);
  if (index == 0xFF) return WindReason::kNoEntry;
  const WindSpec& spec = *d.spec;
  if (!d.literals_match) return WindReason::kLiteral;
  WindInputs w;
  for (uint32_t i = 0; i < kWindRegisters; ++i) {
    const uint8_t reg = spec.reg[i];
    if (reg == kWindNoRegister) continue;
    if (!d.vs_regs.Has(reg)) return WindReason::kRegisterUnread;
    const float* v = d.vs_regs.bank + size_t(reg) * 4;
    for (int c = 0; c < 4; ++c) {
      if (!std::isfinite(v[c])) return WindReason::kRegisterNotFinite;
    }
    if (!WindRegisterInBounds(spec.family, i, v)) return WindReason::kRegisterRange;
    for (int c = 0; c < 4; ++c) w.reg[i][c] = v[c];
  }
  if (spec.family == WindFamily::kPlant) {
    if (!d.instanced) return WindReason::kInstance;
  } else {
    for (uint32_t e = 0; e < kWindElements; ++e) {
      if (spec.element[e].fetch < 0) continue;
      const VertexFetch* f = d.fetches ? LightFetch(*d.fetches, spec.element[e]) : nullptr;
      if (!f) return WindReason::kElement;
      const uint32_t bytes = LightElementBytes(f->format);
      if (bytes == 0 || f->offset_dwords < 0 || f->stride_dwords == 0 || f->stride_dwords > 255 ||
          uint32_t(f->offset_dwords) * 4 + bytes > f->stride_dwords * 4) {
        return WindReason::kElement;
      }
      uint8_t source = 0;
      if (!LightSource(spec.element[e].source, spec.source_count[e], *f, &source)) return WindReason::kElement;
      if (!MakeLightElement(*f, source, spec.source_count[e], d.stream[e], &w.element[e])) return WindReason::kStream;
      // The stream holds the draw's largest vertex whole (the position's range check is the mesh
      // stream's too, but nothing here leans on another check's stream being this one).
      if (d.max_vertex < 0 || d.max_vertex >= int64_t(0xFFFFFFFFll) ||
          (uint64_t(d.max_vertex) + 1) * w.element[e].stride_bytes > w.element[e].vb.size) {
        return WindReason::kStream;
      }
    }
    if (d.instanced) return WindReason::kInstance;
  }
  w.family = spec.family;
  w.spec = index;
  *out = w;
  return WindReason::kNone;
}

}  // namespace fable2::native::capture
