// Standalone: clang++ -std=c++23 test_wind_inputs.cpp
#include "../../src/native/capture/wind_inputs.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using namespace fable2::native::capture;

// A draw that passes every rule for `spec`, built from the row itself: the registers at the middle
// of their bounds, a stream of 100 vertices behind each element, every fetch delivering its four
// source components in order.
struct Made {
  float bank[256 * 4] = {};
  std::vector<VertexFetch> fetches;
  WindDraw d;
};
static void Fill(Made& m, const WindSpec& spec) {
  m.d = WindDraw{};
  m.d.spec = &spec;
  m.d.literals_match = true;
  m.d.vs_regs = RegisterView{};
  m.d.vs_regs.bank = m.bank;
  for (uint32_t i = 0; i < kWindRegisters; ++i) {
    if (spec.reg[i] == kWindNoRegister) continue;
    for (int c = 0; c < 4; ++c) m.bank[spec.reg[i] * 4 + c] = 0.5f;
    m.d.vs_regs.Mark(spec.reg[i], 1);
  }
  m.fetches.assign(8, VertexFetch{});
  for (uint32_t e = 0; e < kWindElements; ++e) {
    if (spec.element[e].fetch < 0) continue;
    VertexFetch& f = m.fetches[size_t(spec.element[e].fetch)];
    f.format = spec.element[e].format;
    f.offset_dwords = int32_t(spec.element[e].offset_dwords);
    f.stride_dwords = 9;
    f.dst_swizzle = 0 | (1u << 3) | (2u << 6) | (3u << 9);
    m.d.stream[e].resolved = true;
    m.d.stream[e].vb = {0x1000000u + 0x10000u * e, 100u * 36u};
    m.d.stream[e].endian = 2;
  }
  m.d.fetches = &m.fetches;
  m.d.max_vertex = 99;
  m.d.instanced = spec.family == WindFamily::kPlant;
}

// A shader object's literal table that holds what `spec` names, at the places it names.
struct Literals {
  uint32_t bits[kWindLiteralRegisters][4] = {};
  uint32_t have = 0;
};
static Literals TableOf(const WindSpec& spec) {
  Literals l;
  for (uint32_t i = 0; i < spec.lit_count; ++i) {
    const uint32_t reg = spec.lit_reg[i] - kWindLiteralFirst, comp = spec.lit_comp[i];
    l.bits[reg][comp] = spec.lit_bits[i];
    l.have |= 1u << (reg * 4 + comp);
  }
  return l;
}

int main() {
  const WindSpec* trunk = FindWind(0x475EC9F795E5EDBBull);
  const WindSpec* leaf = FindWind(0xA5846836C90E1192ull);
  const WindSpec* grass = FindWind(0x8123C16DBF583F92ull);
  if (!trunk || !leaf || !grass) return 1;
  if (trunk->family != WindFamily::kTrunk || leaf->family != WindFamily::kLeaf || grass->family != WindFamily::kPlant) return 2;
  if (FindWind(0x36B543CECAE5C781ull) || FindWind(0x6AD4108C3FF07966ull) || FindWind(0)) return 3;   // push only; still; none
  const WindSpec* caster = FindWind(0x432563420047C96Cull);
  if (!caster || !caster->caster || caster->family != WindFamily::kLeaf || leaf->caster) return 4;

  // The table itself: twelve rows, each found by its hash at its own index; the registers and the
  // elements of each family; the literals' number by family (ledger ruling 31: 18, 28, 7) and every
  // literal register inside the range the capture reads (c248..c255).
  if (kWindSpecCount != 12 || kWindLiterals != 28 || kWindRegisters != 5 || kWindElements != 3) return 5;
  uint32_t by_family[size_t(WindFamily::kCount)] = {}, casters = 0;
  for (uint32_t i = 0; i < kWindSpecCount; ++i) {
    const WindSpec& s = kWindSpecs[i];
    if (FindWind(s.vs_hash) != &s || WindSpecIndex(&s) != i) return 6;
    if (s.family == WindFamily::kNone || s.family >= WindFamily::kCount) return 6;
    ++by_family[size_t(s.family)];
    casters += s.caster ? 1 : 0;
    if (s.lit_count != WindLiteralCount(s.family)) return 7;
    for (uint32_t k = 0; k < kWindLiterals; ++k) {
      if (k < s.lit_count ? (s.lit_reg[k] < kWindLiteralFirst || s.lit_comp[k] > 3) : s.lit_reg[k] != kWindNoRegister) return 7;
    }
    const bool plant = s.family == WindFamily::kPlant;
    const uint8_t tree_reg[5] = {15, 16, 17, kWindNoRegister, kWindNoRegister}, plant_reg[5] = {10, 21, 22, 25, 26};
    if (std::memcmp(s.reg, plant ? plant_reg : tree_reg, 5) != 0) return 8;
    // a plant: no element. a trunk: the weights. a leaf: the pivot (yxw of a half4 at dword 5), the
    // weights (xyzw of the 8_8_8_8 at dword 7), the flutter (xyz of the 8_8_8_8 at dword 8: ruling 30).
    const bool has[3] = {s.family == WindFamily::kLeaf, !plant, s.family == WindFamily::kLeaf};
    const uint8_t format[3] = {32, 6, 6}, offset[3] = {5, 7, 8}, count[3] = {3, 4, 3};
    const uint16_t source[3] = {0x331, 0x4E4, 0x324};
    for (uint32_t e = 0; e < kWindElements; ++e) {
      const LightFetchSpec& f = s.element[e];
      if (!has[e]) {
        if (f.fetch != -1 || s.source_count[e] != 0) return 9;
        continue;
      }
      if (f.fetch < 0 || f.format != format[e] || f.offset_dwords != offset[e] || f.source != source[e] ||
          s.source_count[e] != count[e]) {
        return 9;
      }
    }
  }
  if (by_family[size_t(WindFamily::kTrunk)] != 2 || by_family[size_t(WindFamily::kLeaf)] != 4 ||
      by_family[size_t(WindFamily::kPlant)] != 6 || casters != 3) {
    return 9;
  }
  if (WindSpecIndex(nullptr) != 0xFF || WindLiteralCount(WindFamily::kNone) != 0) return 9;
  if (WindLiteralCount(WindFamily::kTrunk) != 18 || WindLiteralCount(WindFamily::kLeaf) != 28 ||
      WindLiteralCount(WindFamily::kPlant) != 7) {
    return 9;
  }
  if (std::strcmp(WindFamilyName(WindFamily::kLeaf), "leaf") != 0 || std::strcmp(WindFamilyName(WindFamily::kNone), "none") != 0) return 9;
  const char* names[] = {"none", "off", "no-entry", "register-unread", "register-not-finite", "register-range",
                         "literal", "element", "stream", "instance", "over-limit"};
  if (size_t(WindReason::kCount) != 11) return 9;
  for (size_t i = 0; i < size_t(WindReason::kCount); ++i) {
    if (std::strcmp(WindReasonName(WindReason(i)), names[i]) != 0) return 9;
  }

  const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
  for (const WindSpec* spec : {trunk, leaf, grass, caster}) {
    Made m;
    WindInputs out;
    Fill(m, *spec);
    if (MakeWindInputs(m.d, &out) != WindReason::kNone || out.family != spec->family) return 10;
    if (out.reg[0][0] != 0.5f || out.spec == 0xFF || &kWindSpecs[out.spec] != spec) return 21;
    if (out.element[kWindWeights].stride_bytes != (spec->family == WindFamily::kPlant ? 0 : 36)) return 22;
    // The registers are the bank's, bit for bit, each at its slot; a slot the family has none for is zero.
    for (uint32_t i = 0; i < kWindRegisters; ++i) {
      const uint8_t reg = spec->reg[i];
      for (int c = 0; c < 4; ++c) m.bank[(reg == kWindNoRegister ? 200 : reg) * 4 + c] = 1.0f + 0.01f * float(4 * i + c);
    }
    {
      // (a signalling pattern's bits survive: the clock is carried as the register holds it)
      const uint32_t odd = 0x42F6E979u;  // 123.456001...
      std::memcpy(&m.bank[spec->reg[0] * 4], &odd, 4);
      WindInputs o;
      if (MakeWindInputs(m.d, &o) != WindReason::kNone) return 23;
      for (uint32_t i = 0; i < kWindRegisters; ++i) {
        const uint8_t reg = spec->reg[i];
        static const float zero[4] = {};
        if (std::memcmp(o.reg[i], reg == kWindNoRegister ? zero : &m.bank[reg * 4], 16) != 0) return 23;
      }
    }
    if (spec->family == WindFamily::kLeaf) {
      const LightElement& p = out.element[kWindPivot];
      const LightElement& w = out.element[kWindWeights];
      const LightElement& v = out.element[kWindFlutter];
      if (p.offset_bytes != 20 || p.format != 32 || p.source != 0x31 || p.source_count != 3 || p.vb.size != 3600) return 24;
      if (w.offset_bytes != 28 || w.format != 6 || w.source != 0xE4 || w.source_count != 4) return 24;
      if (v.offset_bytes != 32 || v.format != 6 || v.source != 0x24 || v.source_count != 3) return 24;
      if (((w.flags >> kLightElementEndianShift) & 3) != 2 || !(w.flags & kLightElementNormalized)) return 24;
    } else if (out.element[kWindPivot].stride_bytes != 0 || out.element[kWindFlutter].stride_bytes != 0) {
      return 24;
    }
    // every rule's rejection leaves default inputs
    auto refused = [&](WindReason want) {
      WindInputs o;
      o.family = WindFamily::kPlant;
      o.reg[2][1] = 7.0f;
      static const WindInputs none;
      return MakeWindInputs(m.d, &o) == want && o.family == WindFamily::kNone && o.spec == 0xFF &&
             std::memcmp(&o, &none, sizeof(o)) == 0;
    };
    Fill(m, *spec); m.d.spec = nullptr;                              if (!refused(WindReason::kNoEntry)) return 11;
    Fill(m, *spec); m.d.literals_match = false;                      if (!refused(WindReason::kLiteral)) return 12;
    Fill(m, *spec); m.d.vs_regs = RegisterView{}; m.d.vs_regs.bank = m.bank;
                                                                     if (!refused(WindReason::kRegisterUnread)) return 13;
    // (each register alone)
    for (uint32_t i = 0; i < kWindRegisters; ++i) {
      if (spec->reg[i] == kWindNoRegister) continue;
      Fill(m, *spec);
      m.d.vs_regs = RegisterView{};
      m.d.vs_regs.bank = m.bank;
      for (uint32_t k = 0; k < kWindRegisters; ++k) {
        if (k != i && spec->reg[k] != kWindNoRegister) m.d.vs_regs.Mark(spec->reg[k], 1);
      }
      if (!refused(WindReason::kRegisterUnread)) return 13;
    }
    for (float bad : {kNaN, kInf, -kInf}) {
      Fill(m, *spec); m.bank[spec->reg[1] * 4 + 1] = bad;            if (!refused(WindReason::kRegisterNotFinite)) return 14;
      // (a component the formula does not read is held to be a number all the same: nothing that is
      // none is carried)
      Fill(m, *spec); m.bank[spec->reg[0] * 4 + 1] = bad;            if (!refused(WindReason::kRegisterNotFinite)) return 14;
    }
    Fill(m, *spec); m.bank[spec->reg[0] * 4] = 2e7f;                 if (!refused(WindReason::kRegisterRange)) return 15;   // the clock past its bound
    Fill(m, *spec); m.d.instanced = !m.d.instanced;                  if (!refused(WindReason::kInstance)) return 16;
    if (spec->family != WindFamily::kPlant) {
      Fill(m, *spec); m.fetches[size_t(spec->element[kWindWeights].fetch)].offset_dwords += 1;
                                                                     if (!refused(WindReason::kElement)) return 17;
      Fill(m, *spec); m.fetches[size_t(spec->element[kWindWeights].fetch)].format = 7;
                                                                     if (!refused(WindReason::kElement)) return 17;
      // the shader does not receive a component the table reads (the fetch writes x alone)
      Fill(m, *spec); m.fetches[size_t(spec->element[kWindWeights].fetch)].dst_swizzle = 0 | (7u << 3) | (7u << 6) | (7u << 9);
                                                                     if (!refused(WindReason::kElement)) return 17;
      // the element does not lie inside one vertex
      Fill(m, *spec); m.fetches[size_t(spec->element[kWindWeights].fetch)].stride_dwords = 7;
                                                                     if (!refused(WindReason::kElement)) return 17;
      Fill(m, *spec); m.d.fetches = nullptr;                         if (!refused(WindReason::kElement)) return 17;
      Fill(m, *spec); m.d.stream[kWindWeights].resolved = false;     if (!refused(WindReason::kStream)) return 18;
      Fill(m, *spec); m.d.stream[kWindWeights].endian = 4;           if (!refused(WindReason::kStream)) return 18;
      Fill(m, *spec); m.d.max_vertex = 100;                          if (!refused(WindReason::kStream)) return 19;   // one past the stream
      Fill(m, *spec); m.d.max_vertex = 99; m.d.stream[kWindWeights].vb.size -= 1;
                                                                     if (!refused(WindReason::kStream)) return 19;   // the last vertex cut short
      Fill(m, *spec); m.d.max_vertex = -1;                           if (!refused(WindReason::kStream)) return 20;
      Fill(m, *spec); m.d.max_vertex = int64_t(1) << 40;             if (!refused(WindReason::kStream)) return 20;
      // (an index whose byte count wraps 64 bits: 2^62 vertices of 36 bytes are 9 * 2^64)
      Fill(m, *spec); m.d.max_vertex = (int64_t(1) << 62) - 1;       if (!refused(WindReason::kStream)) return 20;
    }
    if (spec->family == WindFamily::kLeaf) {
      for (uint32_t e : {uint32_t(kWindPivot), uint32_t(kWindFlutter)}) {
        Fill(m, *spec); m.fetches[size_t(spec->element[e].fetch)].offset_dwords += 1;
                                                                     if (!refused(WindReason::kElement)) return 25;
        Fill(m, *spec); m.d.stream[e].resolved = false;              if (!refused(WindReason::kStream)) return 25;
      }
    }
    // The order of the rules: the literal before the registers, the registers before the elements.
    Fill(m, *spec); m.d.literals_match = false; m.d.vs_regs = RegisterView{};
                                                                     if (!refused(WindReason::kLiteral)) return 26;
    Fill(m, *spec); m.bank[spec->reg[0] * 4] = kNaN; m.d.instanced = !m.d.instanced;
                                                                     if (!refused(WindReason::kRegisterNotFinite)) return 26;
    Fill(m, *spec);
    if (MakeWindInputs(m.d, nullptr) != WindReason::kNoEntry) return 27;
  }

  // The bounds themselves (spec Design 5 as the ledger's rulings 26 and 28 amended it): the captured
  // values inside; each edge inside and the next number outside.
  const float c15[4] = {63.1964f, 0.63f, 0.72f, 0.32f}, c17[4] = {0.4816f, -0.4636f, 2.0f, 126.637f};
  if (!WindRegisterInBounds(WindFamily::kTrunk, 0, c15) || !WindRegisterInBounds(WindFamily::kLeaf, 2, c17)) return 30;
  const float first_frame[4] = {12.5f, -12.5f, 2.0f, 126.0f};      // capture 191202 (ruling 28)
  if (!WindRegisterInBounds(WindFamily::kTrunk, 2, first_frame)) return 31;
  const float gust[4] = {16.5f, 0.0f, 2.0f, 126.0f};
  if (WindRegisterInBounds(WindFamily::kTrunk, 2, gust) || WindRegisterInBounds(WindFamily::kLeaf, 2, gust)) return 31;
  const float c25_zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};              // a plant without sway (ruling 26)
  if (!WindRegisterInBounds(WindFamily::kPlant, 3, c25_zero)) return 32;
  struct Edge {
    WindFamily family;
    uint32_t slot, comp;
    float lo, hi;
  };
  const Edge edges[] = {
      {WindFamily::kTrunk, 0, 0, -1e7f, 1e7f}, {WindFamily::kTrunk, 0, 1, 0.0f, 4.0f},  {WindFamily::kTrunk, 0, 2, 0.0f, 4.0f},
      {WindFamily::kTrunk, 0, 3, 0.0f, 4.0f},  {WindFamily::kTrunk, 1, 0, -4.0f, 4.0f}, {WindFamily::kTrunk, 1, 1, -4.0f, 4.0f},
      {WindFamily::kTrunk, 1, 2, -4.0f, 4.0f}, {WindFamily::kTrunk, 1, 3, -4.0f, 4.0f}, {WindFamily::kTrunk, 2, 0, -16.0f, 16.0f},
      {WindFamily::kTrunk, 2, 1, -16.0f, 16.0f}, {WindFamily::kTrunk, 2, 2, 0.0f, 16.0f}, {WindFamily::kTrunk, 2, 3, -2e7f, 2e7f},
      {WindFamily::kPlant, 0, 0, -1e7f, 1e7f}, {WindFamily::kPlant, 1, 0, -4.0f, 4.0f}, {WindFamily::kPlant, 1, 1, -4.0f, 4.0f},
      {WindFamily::kPlant, 1, 2, -4.0f, 4.0f}, {WindFamily::kPlant, 2, 0, -8.0f, 8.0f}, {WindFamily::kPlant, 2, 1, -8.0f, 8.0f},
      {WindFamily::kPlant, 2, 2, -8.0f, 8.0f}, {WindFamily::kPlant, 3, 0, 0.0f, 64.0f}, {WindFamily::kPlant, 4, 0, 0.0f, 4.0f},
      {WindFamily::kPlant, 4, 1, 0.0f, 4.0f}};
  for (const Edge& e : edges) {
    for (WindFamily family : {e.family, e.family == WindFamily::kTrunk ? WindFamily::kLeaf : e.family}) {
      float v[4] = {0.5f, 0.5f, 0.5f, 0.5f};
      v[e.comp] = e.lo;
      if (!WindRegisterInBounds(family, e.slot, v)) return 33;
      v[e.comp] = e.hi;
      if (!WindRegisterInBounds(family, e.slot, v)) return 33;
      v[e.comp] = std::nextafter(e.lo, -kInf);
      if (WindRegisterInBounds(family, e.slot, v)) return 34;
      v[e.comp] = std::nextafter(e.hi, kInf);
      if (WindRegisterInBounds(family, e.slot, v)) return 34;
      v[e.comp] = kNaN;
      if (WindRegisterInBounds(family, e.slot, v)) return 35;
    }
  }
  // A component no bound names is not bounded (a plant's c10.yzw repeat the clock; c21.w, c22.w,
  // c25.yzw, c26.zw are not read), a slot the family has no register for is never in bounds, and no
  // family: nothing.
  {
    const float wide[4] = {0.5f, 1e9f, -1e9f, 1e9f};
    if (!WindRegisterInBounds(WindFamily::kPlant, 0, wide) || !WindRegisterInBounds(WindFamily::kPlant, 3, wide)) return 36;
    const float mid[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    if (WindRegisterInBounds(WindFamily::kTrunk, 3, mid) || WindRegisterInBounds(WindFamily::kLeaf, 4, mid) ||
        WindRegisterInBounds(WindFamily::kPlant, 5, mid) || WindRegisterInBounds(WindFamily::kNone, 0, mid) ||
        WindRegisterInBounds(WindFamily::kCount, 0, mid) || WindRegisterInBounds(WindFamily::kPlant, 0, nullptr)) {
      return 36;
    }
  }

  // The literals, each alone (spec ruling 9; ledger ruling 31): a shader object whose table holds
  // every literal of the row at the row's place matches; one bit of one literal, or one literal the
  // table does not hold, does not.
  for (uint32_t i = 0; i < kWindSpecCount; ++i) {
    const WindSpec& s = kWindSpecs[i];
    Literals l = TableOf(s);
    if (!WindLiteralsMatch(s, l.bits, l.have)) return 40;
    for (uint32_t k = 0; k < s.lit_count; ++k) {
      const uint32_t reg = s.lit_reg[k] - kWindLiteralFirst, comp = s.lit_comp[k];
      Literals bad = l;
      bad.bits[reg][comp] ^= 1u;
      if (WindLiteralsMatch(s, bad.bits, bad.have)) return 41;
      bad = l;
      bad.have &= ~(1u << (reg * 4 + comp));
      if (WindLiteralsMatch(s, bad.bits, bad.have)) return 42;
    }
    // (what the table holds besides the row's literals is not asked: the plants' c255.xyz are no numbers)
    Literals more = l;
    for (uint32_t d = 0; d < kWindLiteralRegisters * 4; ++d) {
      if (!((more.have >> d) & 1u)) {
        more.bits[d / 4][d % 4] = 0x7FC00000u;
        more.have |= 1u << d;
      }
    }
    if (!WindLiteralsMatch(s, more.bits, more.have)) return 43;
    if (WindLiteralsMatch(s, nullptr, ~0u)) return 44;
  }
  // The literals as the native constants take them: by slot, the same numbers for every shader of a
  // family whatever register and component its own table holds them at.
  for (uint32_t i = 0; i < kWindSpecCount; ++i) {
    const WindSpec& s = kWindSpecs[i];
    const WindSpec& first = s.family == WindFamily::kTrunk ? *trunk : s.family == WindFamily::kLeaf ? *leaf : *grass;
    if (std::memcmp(s.lit_bits, first.lit_bits, sizeof(s.lit_bits)) != 0) return 45;
  }
  // (the same number at another place: A584 holds the leaves' first literal at c253.z, D7DD at c252.z)
  {
    const WindSpec* other = FindWind(0xD7DD31B9BF194DE8ull);
    if (!other || leaf->lit_reg[0] != 253 || leaf->lit_comp[0] != 2 || other->lit_reg[0] != 252 || other->lit_comp[0] != 2 ||
        leaf->lit_bits[0] != 0x3F000000u) {
      return 46;
    }
    const Literals l = TableOf(*leaf);
    if (WindLiteralsMatch(*other, l.bits, l.have)) return 46;   // another shader's table is not this one's
  }
  // A literal table's runs into the window c248..c255 (the capture's reader): how much of a run is
  // read, and where its dwords land.
  static_assert(kWindLiteralFirst == 248 && kWindLiteralRegisters == 8);
  if (WindLiteralRunDwords(252, 16) != 16 || WindLiteralRunDwords(248, 32) != 32 || WindLiteralRunDwords(250, 4) != 4) return 50;
  if (WindLiteralRunDwords(248, 40) != 32 || WindLiteralRunDwords(255, 8) != 4 || WindLiteralRunDwords(0, 2000) != 1024) return 50;   // never past c255.w
  if (WindLiteralRunDwords(0, 16) != 0 || WindLiteralRunDwords(240, 32) != 0 || WindLiteralRunDwords(256, 4) != 0 ||
      WindLiteralRunDwords(248, 0) != 0 || WindLiteralRunDwords(0xFFFF, 0xFFFF) != 0) {
    return 50;   // a run that ends before the window or begins past it: nothing is read
  }
  if (WindLiteralRunDwords(240, 33) != 33 || WindLiteralRunDwords(0, 1024) != 1024) return 50;
  {
    uint8_t run[64 * 4];
    for (uint32_t d = 0; d < 64; ++d) {
      const uint32_t v = 0xA0000000u + d;
      run[4 * d] = uint8_t(v >> 24); run[4 * d + 1] = uint8_t(v >> 16); run[4 * d + 2] = uint8_t(v >> 8); run[4 * d + 3] = uint8_t(v);
    }
    Literals l;
    AddWindLiteralRun(252, 16, run, l.bits, &l.have);
    if (l.have != 0xFFFF0000u || l.bits[4][0] != 0xA0000000u || l.bits[7][3] != 0xA000000Fu || l.bits[3][3] != 0) return 51;
    // a run that begins before the window: its first dwords belong to c246 and c247
    Literals m;
    AddWindLiteralRun(246, 11, run, m.bits, &m.have);
    if (m.have != 0x7u || m.bits[0][0] != 0xA0000008u || m.bits[0][2] != 0xA000000Au || m.bits[0][3] != 0) return 52;
    // a later run over an earlier one: the later value
    AddWindLiteralRun(248, 2, run, m.bits, &m.have);
    if (m.have != 0x7u || m.bits[0][0] != 0xA0000000u || m.bits[0][1] != 0xA0000001u || m.bits[0][2] != 0xA000000Au) return 53;
    // past c255.w nothing is written (the arrays end there), and nothing without values
    Literals n;
    AddWindLiteralRun(255, 8, run, n.bits, &n.have);
    if (n.have != 0xF0000000u || n.bits[7][3] != 0xA0000003u) return 54;
    Literals none;
    AddWindLiteralRun(252, 16, nullptr, none.bits, &none.have);
    AddWindLiteralRun(200, 16, run, none.bits, &none.have);
    if (none.have != 0) return 54;
    // The game's own table of a plant (capture rows' "vs_lit": one run of sixteen from c252) holds the row's literals.
    const uint32_t grass_table[16] = {0, 0, 0, 0, 0xC0490FDBu, 0x40C90FDBu, 0x3E22F983u, 0, 0, 0x3F800000u, 0x3F000000u,
                                      0x3DCCCCCDu, 0x7FC00000u, 0x7FC00000u, 0x7FC00000u, 0x40A00000u};
    uint8_t bytes[64];
    for (uint32_t d = 0; d < 16; ++d) {
      const uint32_t v = grass_table[d];
      bytes[4 * d] = uint8_t(v >> 24); bytes[4 * d + 1] = uint8_t(v >> 16); bytes[4 * d + 2] = uint8_t(v >> 8); bytes[4 * d + 3] = uint8_t(v);
    }
    Literals g;
    AddWindLiteralRun(252, 16, bytes, g.bits, &g.have);
    if (!WindLiteralsMatch(*grass, g.bits, g.have)) return 55;
    if (WindLiteralsMatch(*FindWind(0xB636821F95DC9D8Eull), g.bits, g.have)) return 55;   // B636 holds them elsewhere
  }
  static_assert(sizeof(WindInputs) == 132);
  std::puts("PASS: wind table, wind inputs and their refusals, register bounds, literals each alone");
  return 0;
}
