// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/draw_record.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstring>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
static int32_t Ref(int bank, int reg, int comp, bool neg = false) {
  return (int32_t(neg) << 11) | (bank << 10) | (reg * 4 + comp);
}

// --- light inputs (material.h, MakeLightInputs) ---------------------------------------------------

// The fetches of the three kinds of mesh vertex shader, as their dumps list them
// (out/native-evidence/lighting/task4_table_facts.txt): slot 95 is stream 0, 94 stream 1, 92 stream 3.
static VertexFetch Fetch(uint32_t slot, int32_t offset, uint32_t format, uint32_t stride, uint32_t swizzle,
                         bool is_signed, bool normalized, bool mini) {
  VertexFetch f;
  f.fetch_slot = slot; f.offset_dwords = offset; f.format = format; f.stride_dwords = stride;
  f.dst_swizzle = swizzle; f.is_signed = is_signed; f.normalized = normalized; f.mini = mini;
  return f;
}
// 0xECD6...: position, normal (10_11_11 at dword 2), uv, tangent of stream 0; the colour stream.
static std::vector<VertexFetch> StreamFetches() {
  return {Fetch(95, 0, 32, 5, 0xAC1, true, false, false), Fetch(95, 2, 16, 5, 0xE88, true, true, true),
          Fetch(95, 3, 31, 5, 0xFC1, true, false, true), Fetch(95, 4, 7, 5, 0x688, true, true, true),
          Fetch(92, 0, 6, 1, 0x60A, false, true, false)};
}
// 0x79EA...: the same vertex, then four float weights of stream 1.
static std::vector<VertexFetch> ProbeFetches() {
  return {Fetch(95, 0, 32, 5, 0x2C2, true, false, false), Fetch(95, 2, 16, 5, 0xE88, true, true, true),
          Fetch(95, 3, 31, 5, 0xFC1, true, false, true), Fetch(95, 4, 7, 5, 0x688, true, true, true),
          Fetch(94, 0, 38, 4, 0x688, true, false, false)};
}
// 0x8123...: three instance rows and the instance colour (dword 6) of stream 1, the mesh position
// and normal (half4 at dword 2) of stream 0.
static std::vector<VertexFetch> InstanceFetches() {
  return {Fetch(94, 0, 32, 7, 0x4C1, true, false, false), Fetch(94, 2, 32, 7, 0x099, true, false, true),
          Fetch(94, 4, 32, 7, 0x681, true, false, true), Fetch(94, 6, 6, 7, 0xE0A, false, true, true),
          Fetch(95, 0, 32, 6, 0x053, true, false, false), Fetch(95, 2, 32, 6, 0x4C1, true, false, true)};
}

static const PsLightSpec kObjectPs{0x8D90, LightFamily::kObject, false, 0.0f};
static const PsLightSpec kTrunkPs{0x00E0, LightFamily::kTreeTrunk, false, 0.0f};
static const PsLightSpec kLayerPs{0x9C21, LightFamily::kTerrainLayer, false, 8.0f};
static const PsLightSpec kFarPs{0x7DDA, LightFamily::kTerrainFar, false, 0.0f};
static const PsLightSpec kPlantPs{0xA7E4, LightFamily::kPlant, false, 0.0f};
static const PsLightSpec kPlantLitPs{0x165E, LightFamily::kPlantLit, true, 0.0f};
static const PsLightSpec kLeafPs{0x7CD5, LightFamily::kTreeLeaf, false, 0.0f};
// The sources as the table writes them (gen_transform_table.py, source_code): the components two
// bits each from bit 0, their number above: xyz, zyxw, xyzw, yxw, zyx.
static constexpr uint16_t kXyz = 0x324, kZyxw = 0x4C6, kXyzw = 0x4E4, kYxw = 0x331, kZyx = 0x306;
static const VsLightSpec kStreamVs{0xECD6, LightVertexKind::kStream, {1, 16, 2, kXyz}, {4, 6, 0, kZyxw}, -1, 7 * 4 + 3, 2.2f};
static const VsLightSpec kProbeVs{0x79EA, LightVertexKind::kProbe, {1, 16, 2, kXyz}, {4, 38, 0, kXyzw}, 2, 7 * 4 + 3, 0.0f};
static const VsLightSpec kInstanceVs{0x8123, LightVertexKind::kInstance, {5, 32, 2, kYxw}, {3, 6, 6, kZyx}, -1, -1, 0.0f};
static const VsLightSpec kTerrainVs{0xC30A, LightVertexKind::kTerrain, {-1, 0, 0, 0}, {-1, 0, 0, 0}, -1, -1, 0.0f};

// One draw of each kind with everything read, and the two banks it was read into.
struct LightCase {
  float vs[1024] = {}, ps[1024] = {};
  std::vector<VertexFetch> fetches;
  LightDraw d;  // points into this case: a copy points into itself
  LightCase() = default;
  LightCase(const LightCase& o) { *this = o; }
  LightCase& operator=(const LightCase& o) {
    std::memcpy(vs, o.vs, sizeof(vs));
    std::memcpy(ps, o.ps, sizeof(ps));
    fetches = o.fetches;
    d = o.d;
    if (d.fetches) d.fetches = &fetches;
    if (d.vs_regs.bank) d.vs_regs.bank = vs;
    if (d.ps_regs.bank) d.ps_regs.bank = ps;
    return *this;
  }
};
static void SetReg(float* bank, uint32_t reg, float x, float y, float z, float w) {
  bank[reg * 4] = x; bank[reg * 4 + 1] = y; bank[reg * 4 + 2] = z; bank[reg * 4 + 3] = w;
}
static void Fill(LightCase& c, const PsLightSpec& ps, const VsLightSpec& vs, std::vector<VertexFetch> fetches) {
  c.fetches = std::move(fetches);
  c.d = LightDraw{};
  c.d.ps = &ps;
  c.d.vs = &vs;
  c.d.pair = true;
  c.d.fetches = &c.fetches;
  c.d.vs_regs.bank = c.vs;
  c.d.ps_regs.bank = c.ps;
  LightRegisterRun runs[kMaxLightRegisterRuns];
  const size_t n = LightRegisterRuns(ps, vs, runs);
  for (size_t i = 0; i < n; ++i) (runs[i].bank ? c.d.ps_regs : c.d.vs_regs).Mark(runs[i].reg, runs[i].count);
  // Values no other register has, so that a wrong register or component shows.
  SetReg(c.vs, 4, 0.0f, 1.0f, 0.0f, 91.0f);
  SetReg(c.vs, 5, -1.0f, 0.0f, 0.0f, 92.0f);
  SetReg(c.vs, 6, 0.0f, 0.0f, 1.0f, 93.0f);
  SetReg(c.vs, 7, 71.0f, 72.0f, 73.0f, 1.25f);
  SetReg(c.vs, 8, 0.625f, 74.0f, 75.0f, 76.0f);
  SetReg(c.vs, 20, 224.0f, 81.0f, 82.0f, 83.0f);
  for (uint32_t r = 0; r < 12; ++r) SetReg(c.vs, 28 + r, 100.0f + r, 200.0f + r, 300.0f + r, 400.0f + r);
  SetReg(c.vs, 45, 1.0f, -200.0f, 100.0f, 0.5f);
  SetReg(c.vs, 50, 1.0f, 100.0f, 1.0f, 0.4f);
  SetReg(c.ps, 11, 0.488f, 61.0f, 62.0f, 63.0f);
  SetReg(c.ps, 26, 0.9f, 0.8f, 0.7f, 0.6f);
  SetReg(c.ps, 27, 51.0f, 52.0f, 53.0f, 0.75f);
  SetReg(c.ps, 49, 0.5f, 0.25f, 0.125f, 54.0f);
  c.d.normal_stream = {true, {0x100000, 4000}, 2};  // 200 vertices of 20 bytes
  c.d.second_stream = {true, {0x200000, 1696}, 2};  // 424 colours, or 106 weight sets
  c.d.max_vertex = 99;
}
static LightCase StreamCase() { LightCase c; Fill(c, kObjectPs, kStreamVs, StreamFetches()); return c; }
static LightCase ProbeCase() { LightCase c; Fill(c, kObjectPs, kProbeVs, ProbeFetches()); return c; }
// (the leaves' vertex shaders have the probe shaders' elements: normal, weights, the set in the position)
static LightCase LeafCase() { LightCase c; Fill(c, kLeafPs, kProbeVs, ProbeFetches()); return c; }
static LightCase InstanceCase() {
  LightCase c;
  Fill(c, kPlantPs, kInstanceVs, InstanceFetches());
  c.d.normal_stream = {true, {0x100000, 2400}, 2};   // the mesh: 100 vertices of 24 bytes
  c.d.second_stream = {true, {0x200000, 2800}, 2};   // 100 instances of 28 bytes
  c.d.instanced = true;
  return c;
}
static LightCase TerrainCase(const PsLightSpec& ps) {
  LightCase c;
  Fill(c, ps, kTerrainVs, {});
  c.d.normal_stream = c.d.second_stream = LightStream{};
  c.d.max_vertex = -1;
  return c;
}
static bool IsDefault(const LightInputs& l) {
  const LightInputs zero{};
  return std::memcmp(&l, &zero, sizeof(l)) == 0;
}
// The reason and the word of a draw; on any reason the inputs must be the default ones.
static bool Fails(const LightDraw& d, LightReason want, const char* word) {
  LightInputs out;
  out.family = LightFamily::kPlant;  // stale: must be overwritten
  const char* why = "?";
  if (MakeLightInputs(d, &out, &why) != want || std::strcmp(why, word) != 0) return false;
  LightInputs again;
  return IsDefault(out) && MakeLightInputs(d, &again) == want;  // also without the word
}

static int LightTests() {
  const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
  // Names and codes (the generated tables write the codes; the log and the rows the names).
  if (std::strcmp(LightFamilyName(LightFamily::kTerrainLayer), "terrain-layer") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kTerrainFar), "terrain-far") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kObject), "object") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kTreeTrunk), "tree-trunk") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kPlant), "plant") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kPlantLit), "plant-lit") != 0 ||
      std::strcmp(LightFamilyName(LightFamily::kTreeLeaf), "tree-leaf") != 0 || size_t(LightFamily::kCount) != 8 ||
      std::strcmp(LightFamilyName(LightFamily::kNone), "none") != 0) {
    return 200;
  }
  if (std::strcmp(LightReasonName(LightReason::kLightUnknown), "light-unknown") != 0 ||
      std::strcmp(LightReasonName(LightReason::kNormalUnsupported), "normal-unsupported") != 0 ||
      std::strcmp(LightReasonName(LightReason::kNone), "none") != 0 ||
      std::strcmp(kLightBlockReason, "light-block") != 0 || size_t(LightReason::kCount) != 3) {
    return 201;
  }
  if (std::strcmp(LightVertexKindName(LightVertexKind::kStream), "stream") != 0 ||
      std::strcmp(LightVertexKindName(LightVertexKind::kProbe), "probe") != 0 ||
      std::strcmp(LightVertexKindName(LightVertexKind::kInstance), "instance") != 0 ||
      std::strcmp(LightVertexKindName(LightVertexKind::kTerrain), "terrain") != 0) {
    return 202;
  }
  // Which vertex side a family can stand on: every combination.
  {
    using F = LightFamily;
    using K = LightVertexKind;
    const struct { F f; K k; } fits[] = {{F::kTerrainLayer, K::kTerrain}, {F::kTerrainFar, K::kTerrain},
                                         {F::kObject, K::kStream},        {F::kObject, K::kProbe},
                                         {F::kTreeTrunk, K::kProbe},      {F::kPlant, K::kInstance},
                                         {F::kPlantLit, K::kInstance},    {F::kTreeLeaf, K::kProbe}};
    for (int f = 0; f <= int(F::kCount); ++f) {
      for (int k = 0; k <= 5; ++k) {
        bool want = false;
        for (const auto& p : fits) want = want || (int(p.f) == f && int(p.k) == k);
        if (LightKindFits(F(f), K(k)) != want) return 203;
      }
    }
  }
  if (LightElementBytes(6) != 4 || LightElementBytes(16) != 4 || LightElementBytes(32) != 8 ||
      LightElementBytes(38) != 16 || LightElementBytes(7) != 0 || LightElementBytes(31) != 0 || LightElementBytes(0) != 0) {
    return 204;
  }
  if (!IsLightNormalFormat(16) || !IsLightNormalFormat(32) || IsLightNormalFormat(6) || IsLightNormalFormat(38) ||
      IsLightNormalFormat(7) || IsLightNormalFormat(0)) {
    return 205;
  }
  // RegisterView: only what was marked, inside the bank.
  {
    float bank[1024] = {};
    RegisterView v;
    if (v.Has(4)) return 206;  // no bank
    v.bank = bank;
    if (v.Has(4)) return 207;
    v.Mark(4, 3);
    v.Mark(255, 1);
    if (!v.Has(4) || !v.Has(4, 3) || !v.Has(6) || v.Has(3) || v.Has(7) || v.Has(4, 4) || v.Has(3, 2)) return 208;
    if (!v.Has(255) || v.Has(255, 2) || v.Has(256) || v.Has(4, 0) || v.Has(0xFFFFFFFF, 2) || v.Has(254, 0xFFFFFFFF)) return 209;
    v.Mark(250, 100);  // runs out of the bank: its registers up to 255
    if (!v.Has(250, 6) || v.Has(249)) return 210;
    v.Mark(64, 1);     // the first bit of the second word
    if (!v.Has(64) || v.Has(63) || v.Has(65)) return 211;
  }
  // The registers a draw reads, by family and vertex kind.
  {
    LightRegisterRun r[kMaxLightRegisterRuns];
    auto is = [&](size_t i, uint32_t bank, uint32_t reg, uint32_t count) {
      return r[i].bank == bank && r[i].reg == reg && r[i].count == count;
    };
    if (LightRegisterRuns(kLayerPs, kTerrainVs, r) != 1 || !is(0, 1, 11, 1)) return 212;
    if (LightRegisterRuns(kFarPs, kTerrainVs, r) != 1 || !is(0, 1, 11, 1)) return 213;
    if (LightRegisterRuns(kObjectPs, kStreamVs, r) != 5 || !is(0, 0, 4, 3) || !is(1, 0, 7, 1) || !is(2, 0, 20, 1) ||
        !is(3, 1, 26, 2) || !is(4, 1, 49, 1)) {
      return 214;
    }
    // A trunk: an object's registers and, last, vertex c8 (.x: the scale of the trees' in-scatter; fog
    // sub-project, ruling B), which the light does not need (LightRunOptional).
    if (LightRegisterRuns(kTrunkPs, kProbeVs, r) != 6 || !is(0, 0, 4, 3) || !is(1, 0, 7, 1) || !is(2, 0, 28, 12) ||
        !is(3, 1, 26, 2) || !is(4, 1, 49, 1) || !is(5, 0, 8, 1)) {
      return 215;
    }
    if (!LightRunOptional(r[5]) || LightRunOptional(r[0]) || LightRunOptional(r[2]) || LightRunOptional(r[4]) ||
        LightRunOptional(LightRegisterRun{1, 8, 1})) {
      return 215;
    }
    // An object has no such register: its fog is the pixel block's.
    if (LightRegisterRuns(kObjectPs, kProbeVs, r) != 5 || is(4, 0, 8, 1)) return 215;
    VsLightSpec skinned = kProbeVs;
    skinned.scale_ref = -1;  // the skinned shaders multiply by no constant
    if (LightRegisterRuns(kObjectPs, skinned, r) != 4 || !is(0, 0, 4, 3) || !is(1, 0, 28, 12)) return 216;
    if (LightRegisterRuns(kPlantPs, kInstanceVs, r) != 1 || !is(0, 0, 45, 1)) return 217;
    if (LightRegisterRuns(kPlantLitPs, kInstanceVs, r) != 1 || !is(0, 0, 45, 1)) return 218;
    const PsLightSpec none{1, LightFamily::kNone, false, 0.0f};
    if (LightRegisterRuns(none, kStreamVs, r) != 0) return 219;
    // A leaf (plan Task 8c): the trunk's registers and the wrap of its front light, vertex c50 (.w).
    if (LightRegisterRuns(kLeafPs, kProbeVs, r) != 7 || !is(0, 0, 4, 3) || !is(1, 0, 7, 1) || !is(2, 0, 28, 12) ||
        !is(3, 0, 50, 1) || !is(4, 1, 26, 2) || !is(5, 1, 49, 1) || !is(6, 0, 8, 1)) {
      return 380;
    }
    static_assert(kMaxLightRegisterRuns == 7 && kLightVsFogScaleReg == 8);
  }
  // The fetch an entry names: by ordinal, held against the format and the offset it was written for.
  {
    const std::vector<VertexFetch> f = StreamFetches();
    if (LightFetch(f, {1, 16, 2}) != &f[1] || LightFetch(f, {4, 6, 0}) != &f[4]) return 220;
    if (LightFetch(f, {-1, 16, 2}) || LightFetch(f, {5, 6, 0}) || LightFetch(f, {127, 6, 0})) return 221;
    if (LightFetch(f, {1, 7, 2}) || LightFetch(f, {1, 16, 3}) || LightFetch(f, {2, 16, 2})) return 222;
    if (LightFetch({}, {0, 32, 0})) return 223;
  }
  // An element in its stream.
  {
    const std::vector<VertexFetch> f = StreamFetches();
    LightElement e;
    if (!MakeLightElement(f[1], 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 224;
    if (e.vb.phys_addr != 0x100000 || e.vb.size != 4000 || e.offset_bytes != 8 || e.stride_bytes != 20 ||
        e.source != 0x24 || e.source_count != 3 || e.format != 16 ||
        e.flags != (kLightElementSigned | kLightElementNormalized | (2 << 2))) {
      return 225;
    }
    if (LightSourceComponent(e, 0) != 0 || LightSourceComponent(e, 1) != 1 || LightSourceComponent(e, 2) != 2) return 225;
    if (!MakeLightElement(f[4], 0xC6, 4, {true, {0x200000, 4}, 0}, &e) || e.flags != kLightElementNormalized ||
        e.stride_bytes != 4 || e.source != 0xC6 || e.source_count != 4) {
      return 226;
    }
    if (LightSourceComponent(e, 0) != 2 || LightSourceComponent(e, 1) != 1 || LightSourceComponent(e, 2) != 0 ||
        LightSourceComponent(e, 3) != 3) {
      return 226;
    }
    if (MakeLightElement(f[4], 0xC6, 4, {true, {0x200000, 3}, 0}, &e)) return 227;     // no whole element
    if (MakeLightElement(f[1], 0x24, 3, {false, {0x100000, 4000}, 2}, &e)) return 228;  // not resolved
    if (MakeLightElement(f[1], 0x24, 3, {true, {0x100000, 4000}, 4}, &e)) return 229;   // not an endian
    VertexFetch bad = f[1];
    bad.offset_dwords = 5;  // past the vertex (stride 5 dwords)
    if (MakeLightElement(bad, 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 230;
    bad = f[1]; bad.offset_dwords = -1;
    if (MakeLightElement(bad, 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 231;
    bad = f[1]; bad.stride_dwords = 0;
    if (MakeLightElement(bad, 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 232;
    bad = f[3];             // a format the light has no element of (2_10_10_10)
    if (MakeLightElement(bad, 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 233;
    bad = f[0]; bad.offset_dwords = 4;  // a half4 (8 bytes) at the last dword of a 5-dword vertex
    if (MakeLightElement(bad, 0x24, 3, {true, {0x100000, 4000}, 2}, &e)) return 234;
  }
  // The source components of an element: the table's selector, admitted for the shader's fetch.
  {
    // What a fetch delivers: xyzw, xyz_, _xyz, yzxw all of x, y, z; a constant or a component left
    // as it was delivers nothing.
    if (FetchedComponents(0x688) != 0xF || FetchedComponents(0xE88) != 7 || FetchedComponents(0x447) != 7 ||
        FetchedComponents(0x611) != 0xF || FetchedComponents(0x213) != 0xF) {
      return 235;
    }
    if (FetchedComponents(0xE80) != 5 || FetchedComponents(0xFC8) != 3 || FetchedComponents(0xE8C) != 6 ||
        FetchedComponents(0xE45) != 3 || FetchedComponents(0xFFF) != 0 || FetchedComponents(0xB6D) != 0 ||
        FetchedComponents(0x057) != 7 || FetchedComponents(0xE41) != 3) {
      return 236;
    }
    if (LightFormatComponents(16) != 3 || LightFormatComponents(32) != 4 || LightFormatComponents(6) != 4 ||
        LightFormatComponents(38) != 4 || LightFormatComponents(7) != 0 || LightFormatComponents(0) != 0) {
      return 237;
    }
    const VertexFetch n10 = Fetch(95, 2, 16, 5, 0xE88, true, true, true);
    uint8_t src = 0x77;
    auto with = [](VertexFetch f, uint32_t swizzle) { f.dst_swizzle = swizzle; return f; };
    auto refused = [&](uint32_t spec, uint32_t count, const VertexFetch& f) {
      src = 0x77;
      return !LightSource(spec, count, f, &src) && src == 0x77;  // and the output untouched
    };
    // The 10_11_11 normal: source x, y, z, fetched xyz_ or (the skinned shaders) _xyz.
    if (!LightSource(kXyz, 3, n10, &src) || src != 0x24) return 238;
    src = 0x77;
    if (!LightSource(kXyz, 3, with(n10, 0x447), &src) || src != 0x24) return 239;
    // Any order of its three components is a selector; the capture does not judge the order.
    if (!LightSource(0x312, 3, n10, &src) || src != 0x12 || !LightSource(0x309, 3, n10, &src) || src != 0x09) return 353;
    // A component the fetch does not deliver: xxz_ (no y), xy__ (no z), 0yz_ (a constant where x
    // would be), 1xy_ (no z).
    for (uint32_t swizzle : {0xE80u, 0xFC8u, 0xE8Cu, 0xE45u, 0xFFFu}) {
      if (!refused(kXyz, 3, with(n10, swizzle))) return 354;
    }
    // Not three components, or not the number asked: a count of two, four, five; a selector of four
    // where three are asked and the reverse; no selector.
    if (!refused(0x224, 3, n10) || !refused(0x524, 3, n10) || !refused(kXyzw, 3, n10) || !refused(0, 3, n10) ||
        !refused(0x24, 3, n10) || !refused(kXyz, 4, n10) || !refused(kXyz, 2, n10) || !refused(0x224, 2, n10) ||
        !refused(0x5E4, 5, with(n10, 0x688))) {
      return 355;
    }
    // A component the format does not have (w of 10_11_11), one named twice, bits past the three.
    if (!refused(0x334, 3, n10) || !refused(0x304, 3, n10) || !refused(0x300, 3, n10) || !refused(0x364, 3, n10) ||
        !refused(0x3E4, 3, n10)) {
      return 356;
    }
    // The format has no w whatever the fetch's swizzle says of one (xyzw on a 10_11_11 element).
    if (!refused(0x334, 3, with(n10, 0x688)) || !refused(0x4E4, 4, with(n10, 0x688))) return 356;
    if (!LightSource(kXyz, 3, with(n10, 0x688), &src) || src != 0x24) return 356;
    // A format the light has no element of.
    if (!refused(kXyz, 3, Fetch(95, 4, 7, 5, 0x688, true, true, true))) return 357;
    // The plants' halves: source y, x, w, fetched yxwz or yzxw; yxy_ does not deliver w.
    const VertexFetch half = Fetch(95, 2, 32, 6, 0x4C1, true, false, true);
    if (!LightSource(kYxw, 3, half, &src) || src != 0x31) return 358;
    if (!LightSource(kYxw, 3, with(half, 0x611), &src) || src != 0x31) return 359;
    if (!refused(kYxw, 3, with(half, 0xE41)) || !refused(0x311, 3, half)) return 360;
    // Colours: the stream's four (fetched zyxw), an instance's three (zyx_ and _zyx: its fourth
    // byte is not delivered, so four cannot be asked of it).
    const VertexFetch colour = Fetch(92, 0, 6, 1, 0x60A, false, true, false);
    if (!LightSource(kZyxw, 4, colour, &src) || src != 0xC6) return 361;
    if (!LightSource(kZyx, 3, with(colour, 0xE0A), &src) || src != 0x06) return 362;
    if (!LightSource(kZyx, 3, with(colour, 0x057), &src) || src != 0x06) return 363;
    if (!refused(kZyxw, 4, with(colour, 0xE0A)) || !refused(kZyxw, 4, with(colour, 0xA0A)) ||
        !refused(kZyx, 3, with(colour, 0xFCA)) || !refused(kZyxw, 3, colour) || !refused(kZyx, 4, colour)) {
      return 364;
    }
    // The four weights, fetched xyzw or (the skinned shaders) wzxy.
    const VertexFetch weights = Fetch(94, 0, 38, 4, 0x688, true, false, false);
    if (!LightSource(kXyzw, 4, weights, &src) || src != 0xE4) return 365;
    if (!LightSource(kXyzw, 4, with(weights, 0x213), &src) || src != 0xE4) return 366;
    if (!refused(kXyzw, 4, with(weights, 0xE88)) || !refused(0x4E0, 4, weights)) return 367;
  }

  // --- a draw of the object family with the colour stream (0x8D90 with 0xECD6) ---
  {
    LightCase c = StreamCase();
    LightInputs l;
    const char* why = "?";
    if (MakeLightInputs(c.d, &l, &why) != LightReason::kNone || why[0] != 0) return 240;
    if (l.family != LightFamily::kObject || l.kind != LightVertexKind::kStream || l.multi || l.a0_comp != -1) return 241;
    if (l.normal.vb.phys_addr != 0x100000 || l.normal.offset_bytes != 8 || l.normal.stride_bytes != 20 || l.normal.format != 16 ||
        l.normal.source != 0x24 || l.normal.source_count != 3) {
      return 242;
    }
    if (l.second.vb.phys_addr != 0x200000 || l.second.vb.size != 1696 || l.second.stride_bytes != 4 ||
        l.second.format != 6 || l.second.source != 0xC6 || l.second.source_count != 4) {
      return 243;
    }
    if (l.colour_first != 224 || l.colour_power != 2.2f || l.facing_scale != 0.0f) return 244;
    const float rot[3][3] = {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
    if (std::memcmp(l.world_rot, rot, sizeof(rot)) != 0 || l.world_scale != 1.25f) return 245;
    if (l.c27w != 0.75f || l.c49[0] != 0.5f || l.c49[1] != 0.25f || l.c49[2] != 0.125f || l.c26[0] != 0.9f ||
        l.c26[1] != 0.8f || l.c26[2] != 0.7f || l.c11x != 1.0f) {
      return 246;
    }
    for (const auto& reg : l.vs_const) {
      for (float v : reg) {
        if (v != 0.0f) return 247;  // no probe set at a stream draw
      }
    }
    // The table: no entry for the pixel shader, none for the vertex shader, a pair the checker did not run.
    LightCase t = StreamCase();
    t.d.ps = nullptr;
    if (!Fails(t.d, LightReason::kLightUnknown, "no-family")) return 248;
    const PsLightSpec none{0x1, LightFamily::kNone, false, 0.0f}, beyond{0x1, LightFamily::kCount, false, 0.0f};
    t.d.ps = &none;
    if (!Fails(t.d, LightReason::kLightUnknown, "no-family")) return 249;
    t.d.ps = &beyond;
    if (!Fails(t.d, LightReason::kLightUnknown, "no-family")) return 250;
    t = StreamCase(); t.d.vs = nullptr;
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 251;
    t = StreamCase(); t.d.vs = &kInstanceVs;  // a kind the family cannot stand on
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 252;
    t = StreamCase(); t.d.ps = &kTrunkPs;      // trunks have no colour stream
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 253;
    t = StreamCase(); t.d.pair = false;
    if (!Fails(t.d, LightReason::kLightUnknown, "pair-not-checked")) return 254;
    t = StreamCase(); t.d.fetches = nullptr;
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 255;
    // A register that was not read: each one of the draw's.
    for (uint32_t reg : {4u, 5u, 6u, 7u, 20u}) {
      t = StreamCase();
      t.d.vs_regs.have[reg / 64] &= ~(1ull << (reg % 64));
      if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 256;
    }
    for (uint32_t reg : {26u, 27u, 49u}) {
      t = StreamCase();
      t.d.ps_regs.have[reg / 64] &= ~(1ull << (reg % 64));
      if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 257;
    }
    t = StreamCase(); t.d.vs_regs.bank = nullptr;
    if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 258;
    t = StreamCase(); t.d.ps_regs.bank = nullptr;
    if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 259;
    // A rotation that is not finite, in each of its nine components, and a translation (c4.w, c5.w,
    // c6.w: read for every object since the local lights, as a leaf's always were); then every other
    // value the draw copies. (c7.xyz are not read: not finite there is fine.)
    for (uint32_t i = 0; i < 12; ++i) {
      for (float v : {inf, -inf, nan}) {
        t = StreamCase();
        t.vs[4 * 4 + i] = v;
        if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return i % 4 == 3 ? 260 : 261;
      }
    }
    for (uint32_t comp : {7u * 4 + 3, 20u * 4}) {
      t = StreamCase();
      t.vs[comp] = nan;
      if (MakeLightInputs(t.d, &l) == LightReason::kNone) return 262;
    }
    for (uint32_t comp : {26u * 4, 26u * 4 + 1, 26u * 4 + 2, 27u * 4 + 3, 49u * 4, 49u * 4 + 1, 49u * 4 + 2}) {
      t = StreamCase();
      t.ps[comp] = inf;
      if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 263;
    }
    t = StreamCase();
    t.ps[26 * 4 + 3] = nan; t.ps[27 * 4] = nan; t.ps[49 * 4 + 3] = nan; t.vs[7 * 4] = nan; t.vs[20 * 4 + 1] = nan;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 264;  // components no formula reads
    // The normal: no such fetch, another format at the ordinal, a format no reader decodes.
    VsLightSpec vs = kStreamVs;
    t = StreamCase(); t.d.vs = &vs;
    vs.normal = {-1, 16, 2};
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 265;
    vs.normal = {9, 16, 2};
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 266;
    vs.normal = {1, 16, 2};
    t.fetches[1].format = 7;   // the shader fetches a 2_10_10_10 there: not the entry's element
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 267;
    vs.normal = {3, 7, 4};     // an entry naming the tangent (2_10_10_10): matches, and is no normal format
    t.fetches = StreamFetches();
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 268;
    vs.normal = {4, 6, 0};     // nor is the colour
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 269;
    vs.normal = {1, 16, 2};
    t.fetches[1].offset_dwords = 3;  // another offset than the entry's
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 270;
    t.fetches = StreamFetches();
    t.fetches[1].stride_dwords = 2;  // the element does not fit its vertex
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 271;
    // Its source components: the entry's three, each delivered by the shader's fetch. Otherwise the
    // normal is not one a reader may decode, whatever else is wrong with the draw.
    t = StreamCase(); t.fetches[1].dst_swizzle = 0x447;  // _xyz: the same three, elsewhere in the register
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.normal.source != 0x24 || l.normal.source_count != 3) return 295;
    for (uint32_t swizzle : {0xE80u, 0xFC8u, 0xE8Cu, 0xE45u}) {  // xxz_, xy__, 0yz_, 1xy_
      t = StreamCase(); t.fetches[1].dst_swizzle = swizzle;
      if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-source")) return 296;
      t.d.normal_stream.resolved = false;
      if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-source")) return 296;
    }
    for (uint16_t spec : {uint16_t(0), uint16_t(0x24), kXyzw, uint16_t(0x334), uint16_t(0x304), uint16_t(0x224), uint16_t(0x3E4)}) {
      vs = kStreamVs; vs.normal.source = spec;  // no selector, no count, four, w, x twice, two, stray bits
      t = StreamCase(); t.d.vs = &vs;
      if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-source")) return 297;
    }
    vs = kStreamVs; vs.normal.source = 0x312;  // z, x, y: a selector the capture records as the table names it
    t = StreamCase(); t.d.vs = &vs;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.normal.source != 0x12) return 298;
    // Its stream: not resolved, too short for the draw's largest vertex, the range unknown.
    t = StreamCase(); t.d.normal_stream.resolved = false;
    if (!Fails(t.d, LightReason::kLightUnknown, "normal-stream")) return 272;
    t = StreamCase(); t.d.normal_stream.vb.size = 1999;  // 100 vertices need 2000 bytes
    if (!Fails(t.d, LightReason::kLightUnknown, "normal-stream")) return 273;
    t.d.normal_stream.vb.size = 2000;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 274;
    t = StreamCase(); t.d.max_vertex = -1;
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-range")) return 275;
    t = StreamCase(); t.d.instanced = true;  // an instance set on a shader that is not an instancing one
    if (!Fails(t.d, LightReason::kLightUnknown, "instance-set")) return 276;
    // The colour stream: of zero size, not resolved, another format, no such fetch.
    t = StreamCase(); t.d.second_stream.vb.size = 0;
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 277;
    t = StreamCase(); t.d.second_stream.resolved = false;
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 278;
    t = StreamCase(); t.fetches[4].format = 38;
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 279;
    vs = kStreamVs; vs.second = {-1, 6, 0};
    t = StreamCase(); t.d.vs = &vs;
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 280;
    vs.second = {3, 7, 4};  // the entry names an element that is no colour
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 281;
    vs.second = {1, 16, 2}; // nor is the normal, though the light has an element of that format
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 293;
    vs.second = {0, 32, 0}; // or the position
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 294;
    // Its source components: red, green, blue and alpha, all four delivered.
    for (uint32_t swizzle : {0xE0Au, 0xA0Au, 0xFCAu}) {  // zyx_, zyx1, zy__
      t = StreamCase(); t.fetches[4].dst_swizzle = swizzle;
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 299;
      t.d.second_stream.vb.size = 0;
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 299;
    }
    for (uint16_t spec : {uint16_t(0), kZyx, uint16_t(0x4C2), uint16_t(0xC6)}) {  // none, three, z twice, no count
      vs = kStreamVs; vs.second.source = spec;
      t = StreamCase(); t.d.vs = &vs;
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 319;
    }
    t = StreamCase(); t.fetches[4].dst_swizzle = 0x688;  // fetched in another order: the same element
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.second.source != 0xC6) return 337;
    // Its index offset c20.x: a whole number from 0, and offset plus the largest vertex inside the
    // stream (424 colours: 224 + 199 is the last one).
    for (float bad : {-1.0f, 0.5f, 224.25f, 16777216.0f, 1e30f, inf, nan}) {
      t = StreamCase();
      t.vs[20 * 4] = bad;
      if (MakeLightInputs(t.d, &l) != LightReason::kLightUnknown || !IsDefault(l)) return 282;
    }
    for (float bad : {0.5f, -1.0f, -4294967296.0f, 16777216.0f, nan}) {  // by its own word: not left to the range check
      t = StreamCase(); t.vs[20 * 4] = bad;
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-offset")) return 283;
    }
    t = StreamCase(); t.vs[20 * 4] = 0.0f;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.colour_first != 0) return 284;
    t = StreamCase(); t.d.max_vertex = 199; t.d.normal_stream.vb.size = 4000;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.colour_first != 224) return 285;
    t.d.max_vertex = 200; t.d.normal_stream.vb.size = 8000;
    if (!Fails(t.d, LightReason::kLightUnknown, "colour-range")) return 286;
    t = StreamCase(); t.vs[20 * 4] = 325.0f;  // 325 + 99 = 424: one past the end
    if (!Fails(t.d, LightReason::kLightUnknown, "colour-range")) return 287;
    t.vs[20 * 4] = 324.0f;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.colour_first != 324) return 288;
    // The literal power.
    for (float bad : {0.0f, -2.2f, inf, nan}) {
      vs = kStreamVs; vs.power = bad;
      t = StreamCase(); t.d.vs = &vs;
      if (!Fails(t.d, LightReason::kLightUnknown, "literal")) return 289;
    }
    // A shader that multiplies by no constant: the scale stays 1 and c7 need not be read.
    vs = kStreamVs; vs.scale_ref = -1;
    t = StreamCase(); t.d.vs = &vs;
    t.d.vs_regs.have[0] &= ~(1ull << 7);
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.world_scale != 1.0f) return 290;
    vs.scale_ref = 1024;  // not a register
    if (MakeLightInputs(t.d, &l) != LightReason::kLightUnknown) return 291;
    // The "-multi" form is the same family with the flag.
    const PsLightSpec multi{0x648B, LightFamily::kObject, true, 0.0f};
    t = StreamCase(); t.d.ps = &multi;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || !l.multi || l.family != LightFamily::kObject) return 292;
  }

  // --- probe colours (0x8D90 with 0x79EA; trunks 0x00E0 with 0x475E) ---
  {
    LightCase c = ProbeCase();
    LightInputs l;
    if (MakeLightInputs(c.d, &l) != LightReason::kNone) return 300;
    if (l.family != LightFamily::kObject || l.kind != LightVertexKind::kProbe || l.a0_comp != 2) return 301;
    if (l.second.format != 38 || l.second.stride_bytes != 16 || l.second.vb.phys_addr != 0x200000 ||
        l.second.flags != (kLightElementSigned | (2 << 2)) || l.second.source != 0xE4 || l.second.source_count != 4 ||
        l.normal.source != 0x24 || l.normal.source_count != 3) {
      return 302;
    }
    for (uint32_t r = 0; r < 12; ++r) {
      if (l.vs_const[r][0] != 100.0f + r || l.vs_const[r][1] != 200.0f + r || l.vs_const[r][2] != 300.0f + r ||
          l.vs_const[r][3] != 400.0f + r) {
        return 303;
      }
    }
    if (l.colour_first != 0 || l.colour_power != 0.0f || l.world_scale != 1.25f || l.c27w != 0.75f) return 304;
    LightCase t = ProbeCase(); t.d.ps = &kTrunkPs;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.family != LightFamily::kTreeTrunk) return 305;
    // Each of the twelve registers must have been read and be finite in all four components.
    for (uint32_t reg = 28; reg < 40; ++reg) {
      t = ProbeCase();
      t.d.vs_regs.have[0] &= ~(1ull << reg);
      if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 306;
      for (uint32_t comp = 0; comp < 4; ++comp) {
        t = ProbeCase();
        t.vs[reg * 4 + comp] = nan;
        if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 307;
      }
    }
    t = ProbeCase(); t.vs[27 * 4 + 3] = nan; t.vs[40 * 4] = nan; t.vs[20 * 4] = nan;  // not this draw's
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 308;
    // The weights: one set of sixteen bytes per vertex up to the draw's largest (106 sets).
    t = ProbeCase(); t.d.max_vertex = 105; t.d.normal_stream.vb.size = 4000;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 309;
    t.d.max_vertex = 106;
    if (!Fails(t.d, LightReason::kLightUnknown, "weights-range")) return 310;
    t = ProbeCase(); t.d.second_stream.vb.size = 0;
    if (!Fails(t.d, LightReason::kLightUnknown, "weights")) return 311;
    t = ProbeCase(); t.fetches[4].format = 6;
    if (!Fails(t.d, LightReason::kLightUnknown, "weights")) return 312;
    {
      VsLightSpec other = kProbeVs;
      other.second = {1, 16, 2};  // an entry naming the normal as the weights
      t = ProbeCase(); t.d.vs = &other;
      if (!Fails(t.d, LightReason::kLightUnknown, "weights")) return 317;
      other.second = {0, 32, 0};  // or the position (a half4: an element the light has, not the weights')
      if (!Fails(t.d, LightReason::kLightUnknown, "weights")) return 318;
    }
    // Their source components: the four, as the two skinned shaders fetch them too (wzxy; their
    // normal _xyz).
    t = ProbeCase(); t.fetches[4].dst_swizzle = 0x213; t.fetches[1].dst_swizzle = 0x447;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.second.source != 0xE4 || l.normal.source != 0x24) return 338;
    t = ProbeCase(); t.fetches[4].dst_swizzle = 0xE88;  // xyz_: the fourth weight is not delivered
    if (!Fails(t.d, LightReason::kLightUnknown, "weights-source")) return 339;
    t.d.second_stream.vb.size = 0;
    if (!Fails(t.d, LightReason::kLightUnknown, "weights-source")) return 339;
    {
      VsLightSpec other = kProbeVs;
      for (uint16_t spec : {uint16_t(0), kXyz, uint16_t(0x4E0), uint16_t(0xE4)}) {  // none, three, x twice, no count
        other.second.source = spec;
        t = ProbeCase(); t.d.vs = &other;
        if (!Fails(t.d, LightReason::kLightUnknown, "weights-source")) return 368;
      }
    }
    t = ProbeCase(); t.d.max_vertex = -1;
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-range")) return 313;
    VsLightSpec vs = kProbeVs;
    for (int8_t bad : {int8_t(-1), int8_t(4), int8_t(127)}) {
      vs.a0_comp = bad;
      t = ProbeCase(); t.d.vs = &vs;
      if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 314;
    }
    for (int8_t ok : {int8_t(0), int8_t(3)}) {
      vs.a0_comp = ok;
      t = ProbeCase(); t.d.vs = &vs;
      if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.a0_comp != ok) return 315;
    }
    t = ProbeCase(); t.fetches[1].format = 32;  // a normal in another format than the entry's
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 316;
  }

  // --- tree leaves (0x7CD5 with 0xA584, 0xD7DD, 0x2D40; plan Task 8c) ---
  {
    LightCase c = LeafCase();
    LightInputs l;
    const char* why = "x";
    if (MakeLightInputs(c.d, &l, &why) != LightReason::kNone || why[0] != 0) return 381;
    if (l.family != LightFamily::kTreeLeaf || l.kind != LightVertexKind::kProbe || l.multi || l.a0_comp != 2) return 382;
    // The trunk's inputs: the rows and their scale, the probe sets, c27.w, c49, c26 ...
    const float rot[3][3] = {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
    if (std::memcmp(l.world_rot, rot, sizeof(rot)) != 0 || l.world_scale != 1.25f || l.c27w != 0.75f ||
        l.c49[0] != 0.5f || l.c49[1] != 0.25f || l.c49[2] != 0.125f || l.c26[0] != 0.9f || l.c26[1] != 0.8f || l.c26[2] != 0.7f) {
      return 383;
    }
    for (uint32_t r = 0; r < 12; ++r) {
      if (l.vs_const[r][0] != 100.0f + r || l.vs_const[r][3] != 400.0f + r) return 384;
    }
    if (l.second.format != 38 || l.second.source != 0xE4 || l.normal.source != 0x24) return 385;
    // ... and its own: the rows' fourth components (the world position's translation: the leaf's
    // light needs the vector to the eye) and c50.w.
    if (l.world_pos[0] != 91.0f || l.world_pos[1] != 92.0f || l.world_pos[2] != 93.0f || l.wrap != 0.4f) return 386;
    // No other family carries the wrap (neutral); an object or trunk carries the translation as the
    // leaf does (the lantern vector of the local lights needs the object's world position).
    LightCase o = ProbeCase();
    if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.world_pos[0] != 91.0f || l.world_pos[1] != 92.0f ||
        l.world_pos[2] != 93.0f || l.wrap != 0.0f) {
      return 387;
    }
    o = StreamCase();
    if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.world_pos[1] != 92.0f || l.wrap != 0.0f) return 387;
    o = ProbeCase(); o.d.ps = &kTrunkPs;
    if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.world_pos[2] != 93.0f || l.wrap != 0.0f) return 387;
    const LightInputs fresh;
    if (fresh.world_pos[0] != 0.0f || fresh.world_pos[1] != 0.0f || fresh.world_pos[2] != 0.0f || fresh.wrap != 0.0f) return 388;
    static_assert(sizeof(LightInputs) == 336);
    // Every value it reads is a number, the translation and the wrap included (an object's
    // translation too, since the local lights: case 260 above).
    for (uint32_t reg = 4; reg <= 6; ++reg) {
      LightCase t = LeafCase(); t.vs[reg * 4 + 3] = nan;
      if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 389;
    }
    LightCase t = LeafCase(); t.vs[50 * 4 + 3] = inf;
    if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 390;
    t = LeafCase(); t.vs[50 * 4] = nan; t.vs[50 * 4 + 1] = nan; t.vs[50 * 4 + 2] = inf;  // c50.xyz: not the light's
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.wrap != 0.4f) return 391;
    t = LeafCase(); t.d.vs_regs.have[0] &= ~(1ull << 50);
    if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 392;
    t = LeafCase(); t.vs[35 * 4 + 2] = nan;
    if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 393;
    t = LeafCase(); t.ps[49 * 4 + 1] = inf;
    if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 393;
    // On probe vertex shaders only, with the weights inside their stream and the set's component.
    t = LeafCase(); t.d.vs = &kStreamVs; t.fetches = StreamFetches();
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 394;
    t = LeafCase(); t.d.vs = &kInstanceVs;
    if (!Fails(t.d, LightReason::kLightUnknown, "vertex-shader")) return 394;
    t = LeafCase(); t.d.max_vertex = 106;  // one weight set more than the stream holds
    if (!Fails(t.d, LightReason::kLightUnknown, "weights-range")) return 395;
    t = LeafCase(); t.d.pair = false;
    if (!Fails(t.d, LightReason::kLightUnknown, "pair-not-checked")) return 396;
    // The trees' in-scatter scale, vertex c8.x (fog sub-project, ruling B): carried by a trunk and a
    // leaf when it was read, is a number and lies in [0, kTreeFogScaleMax]; otherwise the draw keeps
    // its light and carries kTreeFogNone: it is drawn without fog. No other family carries one.
    {
      static_assert(kTreeFogNone < 0.0f && kTreeFogScaleMax == 4.0f);
      LightCase f = LeafCase();
      if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.tree_fog != 0.625f) return 398;
      f = ProbeCase(); f.d.ps = &kTrunkPs;
      Fill(f, kTrunkPs, kProbeVs, ProbeFetches());
      if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.family != LightFamily::kTreeTrunk || l.tree_fog != 0.625f) return 398;
      for (float edge : {0.0f, 1.0f, 4.0f}) {
        f = LeafCase(); f.vs[8 * 4] = edge;
        if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.tree_fog != edge) return 399;
      }
      for (float bad : {nan, inf, -inf, -0.001f, 4.001f, 1e30f}) {
        for (bool leaf : {true, false}) {
          f = LeafCase();
          if (!leaf) Fill(f, kTrunkPs, kProbeVs, ProbeFetches());
          f.vs[8 * 4] = bad;
          if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone) return 400;
          if (l.c27w != 0.75f || l.c49[0] != 0.5f) return 400;  // (the light is whole)
        }
      }
      // Not read (the shader loads c8 itself, or the bank could not be read): the same.
      f = LeafCase(); f.d.vs_regs.have[0] &= ~(1ull << 8);
      if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone || l.wrap != 0.4f) return 401;
      // The other components of c8 are not the fog's.
      f = LeafCase(); f.vs[8 * 4 + 1] = nan; f.vs[8 * 4 + 2] = inf; f.vs[8 * 4 + 3] = -inf;
      if (MakeLightInputs(f.d, &l) != LightReason::kNone || l.tree_fog != 0.625f) return 402;
      // An object, a plant and the terrain carry none, whatever c8 holds.
      LightCase o = ProbeCase();
      if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone) return 403;
      o.d.vs_regs.Mark(8, 1);  // (also when the register happens to have been read)
      if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.family != LightFamily::kObject || l.tree_fog != kTreeFogNone) return 403;
      o = StreamCase();
      if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone) return 403;
      o = InstanceCase();
      if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone) return 403;
      o = TerrainCase(kLayerPs);
      if (MakeLightInputs(o.d, &l) != LightReason::kNone || l.tree_fog != kTreeFogNone) return 403;
      if (LightInputs{}.tree_fog != kTreeFogNone) return 404;
      // A draw that fails for another reason has default inputs, the marker among them.
      f = LeafCase(); f.vs[50 * 4 + 3] = inf;
      if (MakeLightInputs(f.d, &l) == LightReason::kNone || l.tree_fog != kTreeFogNone) return 404;
    }
    // Which families' pixel shaders read the light set's first light (c80, c87), so that the frame's
    // light block may be read at their draws: every family but the leaves, whose light is the vertex
    // shader's (0x7CD5... reads no light register; frame-map section 14, "Tree leaves").
    for (int f = 0; f <= int(LightFamily::kCount); ++f) {
      const bool want = f >= 1 && f < int(LightFamily::kCount) && LightFamily(f) != LightFamily::kTreeLeaf;
      if (LightFamilyReadsSun(LightFamily(f)) != want) return 397;
    }
  }

  // --- instanced plants (0xA7E4 with 0x8123; 0x165E with 0xB636) ---
  {
    LightCase c = InstanceCase();
    LightInputs l;
    if (MakeLightInputs(c.d, &l) != LightReason::kNone) return 320;
    if (l.family != LightFamily::kPlant || l.kind != LightVertexKind::kInstance || l.a0_comp != -1) return 321;
    if (l.normal.format != 32 || l.normal.offset_bytes != 8 || l.normal.stride_bytes != 24 || l.normal.vb.phys_addr != 0x100000 ||
        l.normal.source != 0x31 || l.normal.source_count != 3) {
      return 322;
    }
    if (l.second.format != 6 || l.second.offset_bytes != 24 || l.second.stride_bytes != 28 ||
        l.second.vb.phys_addr != 0x200000 || l.second.source != 0x06 || l.second.source_count != 3) {
      return 323;
    }
    if (l.vs_const[0][0] != 1.0f || l.vs_const[0][1] != -200.0f || l.vs_const[0][2] != 100.0f || l.vs_const[0][3] != 0.5f) return 324;
    for (uint32_t r = 1; r < 12; ++r) {
      for (float v : l.vs_const[r]) {
        if (v != 0.0f) return 325;
      }
    }
    const float identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (std::memcmp(l.world_rot, identity, sizeof(identity)) != 0 || l.world_scale != 1.0f || l.c27w != 1.0f ||
        l.c49[0] != 1.0f || l.c26[2] != 1.0f) {
      return 326;  // the plants' rotation is the instance's rows; they read none of these
    }
    LightCase t = InstanceCase(); t.d.ps = &kPlantLitPs;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.family != LightFamily::kPlantLit || !l.multi) return 327;
    // No vertex range is needed (the index names a copy and a mesh vertex), but the instance set is.
    t = InstanceCase(); t.d.max_vertex = -1;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 328;
    t.d.max_vertex = 1000000;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 329;
    t = InstanceCase(); t.d.instanced = false;
    if (!Fails(t.d, LightReason::kLightUnknown, "instance-set")) return 330;
    for (uint32_t comp = 0; comp < 4; ++comp) {
      t = InstanceCase(); t.vs[45 * 4 + comp] = inf;
      if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) return 331;
    }
    t = InstanceCase(); t.d.vs_regs.have[0] &= ~(1ull << 45);
    if (!Fails(t.d, LightReason::kLightUnknown, "register-unread")) return 332;
    t = InstanceCase(); t.vs[4 * 4] = nan; t.ps[49 * 4] = nan;  // not read by a plant
    if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 333;
    t = InstanceCase(); t.d.second_stream.vb.size = 27;  // not one whole instance
    if (!Fails(t.d, LightReason::kLightUnknown, "colour")) return 334;
    t = InstanceCase(); t.fetches[5].format = 16;        // the mesh normal is not the entry's half4
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-fetch")) return 335;
    t = InstanceCase(); t.d.normal_stream.vb.size = 23;
    if (!Fails(t.d, LightReason::kLightUnknown, "normal-stream")) return 336;
    // The source components: the halves y, x, w as 0x48D3... and 0xFC4F... fetch them too (yzxw),
    // the colour's three as both fetches of it deliver them (zyx_ and _zyx).
    t = InstanceCase(); t.fetches[5].dst_swizzle = 0x611; t.fetches[3].dst_swizzle = 0x057;
    if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.normal.source != 0x31 || l.second.source != 0x06 ||
        l.second.source_count != 3) {
      return 369;
    }
    t = InstanceCase(); t.fetches[5].dst_swizzle = 0xE41;  // yxy_: w, the third half in memory, is not delivered
    if (!Fails(t.d, LightReason::kNormalUnsupported, "normal-source")) return 370;
    t = InstanceCase(); t.fetches[3].dst_swizzle = 0xFCA;  // zy__: no red
    if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 371;
    {
      VsLightSpec other = kInstanceVs;
      other.second.source = kZyxw;  // four of an instance colour: its alpha is not the light's
      t = InstanceCase(); t.d.vs = &other;
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 372;
      t.fetches[3].dst_swizzle = 0x60A;  // (nor when a fetch would deliver it)
      if (!Fails(t.d, LightReason::kLightUnknown, "colour-source")) return 372;
      other = kInstanceVs;
      other.normal.source = kXyz;  // the 10_11_11 order on the halves: a selector, recorded as named
      t = InstanceCase(); t.d.vs = &other;
      if (MakeLightInputs(t.d, &l) != LightReason::kNone || l.normal.source != 0x24) return 373;
    }
  }

  // --- terrain (layers 0x9C21 with 0xC30A; far 0x7DDA with 0x5003) ---
  {
    LightCase c = TerrainCase(kLayerPs);
    LightInputs l;
    if (MakeLightInputs(c.d, &l) != LightReason::kNone) return 340;
    if (l.family != LightFamily::kTerrainLayer || l.kind != LightVertexKind::kTerrain || l.multi) return 341;
    if (l.c11x != 0.488f || l.facing_scale != 8.0f || l.normal.format != 0 || l.second.vb.size != 0) return 342;
    c = TerrainCase(kFarPs);
    if (MakeLightInputs(c.d, &l) != LightReason::kNone || l.family != LightFamily::kTerrainFar || l.facing_scale != 0.0f ||
        l.c11x != 0.488f) {
      return 343;
    }
    const PsLightSpec multi{0x4524, LightFamily::kTerrainLayer, true, 8.0f};
    c = TerrainCase(multi);
    if (MakeLightInputs(c.d, &l) != LightReason::kNone || !l.multi) return 344;
    c = TerrainCase(kLayerPs); c.ps[11 * 4] = nan;
    if (!Fails(c.d, LightReason::kLightUnknown, "register-not-finite")) return 345;
    c = TerrainCase(kLayerPs); c.ps[11 * 4 + 1] = nan;  // c11.y is not read
    if (MakeLightInputs(c.d, &l) != LightReason::kNone) return 346;
    c = TerrainCase(kLayerPs); c.d.ps_regs.have[0] &= ~(1ull << 11);
    if (!Fails(c.d, LightReason::kLightUnknown, "register-unread")) return 347;
    for (float bad : {0.0f, -8.0f, inf, nan}) {  // a layer without its facing literal
      const PsLightSpec no_literal{0x9C21, LightFamily::kTerrainLayer, false, bad};
      c = TerrainCase(no_literal);
      if (!Fails(c.d, LightReason::kLightUnknown, "literal")) return 348;
    }
    c = TerrainCase(kLayerPs); c.d.vs = &kStreamVs;  // a terrain pixel shader on a mesh vertex shader
    if (!Fails(c.d, LightReason::kLightUnknown, "vertex-shader")) return 349;
    c = TerrainCase(kLayerPs); c.d.pair = false;
    if (!Fails(c.d, LightReason::kLightUnknown, "pair-not-checked")) return 350;
    c = StreamCase(); c.d.vs = &kTerrainVs;          // and the reverse
    if (!Fails(c.d, LightReason::kLightUnknown, "vertex-shader")) return 351;
  }
  // The entry's size: one per lit draw is copied into the frame.
  if (sizeof(LightInputs) != 336 || sizeof(LightElement) != 16) {  // (312 before the leaves' four floats, plan Task 8c; 328 before the trees' fog scale, 332 before the light set)
    std::printf("FAIL: sizeof(LightInputs) = %zu, sizeof(LightElement) = %zu\n", sizeof(LightInputs), sizeof(LightElement));
    return 352;
  }
  return 0;
}

int main() {
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextured), "textured") != 0) return 1;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextureDynamic), "texture-dynamic") != 0) return 2;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kUvUnsupported), "uv-unsupported") != 0) return 3;

  float vs[1024] = {}, ps[1024] = {};
  vs[10 * 4 + 1] = 2.0f;   // c10.y (vertex)
  vs[10 * 4 + 3] = 0.5f;   // c10.w
  ps[8 * 4 + 1] = 3.0f;    // c8.y (pixel)
  ps[8 * 4 + 3] = -1.0f;   // c8.w
  if (!Near(RefValue(kNoRef, 1.0f, vs, ps), 1.0f) || !Near(RefValue(Ref(1, 8, 1), 0, vs, ps), 3.0f)) return 4;
  if (!Near(RefValue(Ref(0, 10, 3, true), 0, vs, ps), -0.5f)) return 5;
  // VS: x*c10.y + c10.w, then PS: x*c8.y + c8.w  ->  x*6 + (0.5*3 - 1) = x*6 + 0.5
  UvStage vst[2] = {{Ref(0, 10, 1), Ref(0, 10, 3)}, {}};
  UvStage pst[2] = {{Ref(1, 8, 1), Ref(1, 8, 3)}, {}};
  float a = 0, b = 0;
  ComposeAxis(vst, pst, vs, ps, &a, &b);
  if (!Near(a, 6.0f) || !Near(b, 0.5f)) return 6;
  UvStage none[2] = {};
  ComposeAxis(none, none, vs, ps, &a, &b);
  if (!Near(a, 1.0f) || !Near(b, 0.0f)) return 7;

  // ResolveUvFetch: both axes from fetch 1 (half2 at offset 3), sources y then x.
  std::vector<VertexFetch> f(2);
  f[0].format = 32; f[0].stride_dwords = 5; f[0].offset_dwords = 0;
  f[1].format = 31; f[1].stride_dwords = 5; f[1].offset_dwords = 3; f[1].mini = true;
  VsUvSpec u{0x1, 0, 0, 1, 1, 31, 3, {}};
  VsUvSpec v{0x1, 0, 1, 1, 0, 31, 3, {}};
  UvLayout l;
  if (!ResolveUvFetch(f, u, v, &l) || l.comp_u != 1 || l.comp_v != 0 || l.offset_bytes != 12) return 8;
  VsUvSpec wrong_fmt = u; wrong_fmt.xenos_format = 37;      // table disagrees with the decoded fetch
  if (ResolveUvFetch(f, wrong_fmt, v, &l)) return 9;
  VsUvSpec wrong_off = v; wrong_off.offset_dwords = 0;
  if (ResolveUvFetch(f, u, wrong_off, &l)) return 10;
  VsUvSpec other = v; other.fetch_index = 0; other.xenos_format = 32; other.offset_dwords = 0;
  if (ResolveUvFetch(f, u, other, &l)) return 11;           // u and v from fetches of different formats
  VsUvSpec past = u; past.fetch_index = 5;
  if (ResolveUvFetch(f, past, v, &l)) return 12;
  if (ResolveUvFetch(f, u, past, &l)) return 30;
  // u and v from two fetches of one stream with the same element format (the
  // instancing shaders: source z of the position element and of the normal element).
  std::vector<VertexFetch> g(3);
  g[0].format = 32; g[0].fetch_slot = 95; g[0].stride_dwords = 6; g[0].offset_dwords = 0;
  g[1] = g[0]; g[1].offset_dwords = 2; g[1].mini = true;
  g[2] = g[1]; g[2].offset_dwords = 4;
  VsUvSpec gu{0x2, 0, 0, 0, 2, 32, 0, {}};
  VsUvSpec gv{0x2, 0, 1, 1, 2, 32, 2, {}};
  if (!ResolveUvFetch(g, gu, gv, &l) || l.comp_u != 2 || l.comp_v != 2 || l.offset_bytes != 0 ||
      l.v_element_delta != 8 || l.stride_bytes != 24 || l.fetch_slot != 95) {
    return 31;
  }
  VsUvSpec gv_wrong = gv; gv_wrong.offset_dwords = 4;       // the table names another element
  if (ResolveUvFetch(g, gu, gv_wrong, &l)) return 32;
  g[1].fetch_slot = 94;                                     // v in another stream
  if (ResolveUvFetch(g, gu, gv, &l)) return 33;

  // Refs visited by ForEachRef (the capture reads only these registers).
  std::vector<int32_t> seen;
  ForEachRef(vst, 2, [&](int32_t r) { seen.push_back(r); });
  if (seen.size() != 2 || seen[0] != Ref(0, 10, 1) || seen[1] != Ref(0, 10, 3)) return 13;

  // Records carry the material through AssembleRecord.
  DrawInputs in;
  in.prim = 4; in.have_shader = true; in.have_pos = true; in.have_vb = true; in.vb = {0x1000, 64};
  in.pos.format = PosFormat::kFloat3; in.pos.stride_bytes = 12;
  TransformInfo ti{0, TransformLayout::kDot, -1};
  float bank[1024] = {};
  in.transform = &ti; in.bank = bank;
  in.material.status = MaterialStatus::kTextured;
  in.material.uv_xform[0] = 4.0f;
  DrawRecord r = AssembleRecord(in, 7);
  if (r.skip != SkipReason::kNone || r.material.status != MaterialStatus::kTextured || r.material.uv_xform[0] != 4.0f) return 14;
  // The alpha recipe, its scale and the kind ride along too.
  in.material.alpha = {AlphaSource::kFetch, 8, 1, {26 * 4 + 3, 15 * 4 + 3}, true};
  in.material.alpha_scale = 0.25f;
  in.material.kind = MaterialKind::kTerrainFar;
  r = AssembleRecord(in, 7);
  if (r.material.alpha.source != AlphaSource::kFetch || r.material.alpha.slot != 8 ||
      r.material.alpha.component != 1 || r.material.alpha.scale_ref[0] != 26 * 4 + 3 ||
      r.material.alpha.scale_ref[1] != 15 * 4 + 3 || !r.material.alpha.divide_colour ||
      r.material.alpha_scale != 0.25f || r.material.kind != MaterialKind::kTerrainFar) {
    return 16;
  }
  // Default material status is ps-unknown.
  if (Material{}.status != MaterialStatus::kPsUnknown) return 15;
  {
    // Defaults: no alpha recipe (scale refs -1, component w, colour not divided), scale 1, plain.
    const Material d;
    if (d.alpha.source != AlphaSource::kNone || d.alpha.slot != 0 || d.alpha.component != 3 ||
        d.alpha.scale_ref[0] != -1 || d.alpha.scale_ref[1] != -1 || d.alpha.divide_colour ||
        d.alpha_scale != 1.0f || d.kind != MaterialKind::kPlain) {
      return 17;
    }
  }
  // ComposeAlphaScale: the product of the constants read; nothing finite-unsafe is accepted.
  {
    float out = 7.0f;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float two[2] = {0.5f, 4.0f};
    if (!ComposeAlphaScale(two, 0, &out) || out != 1.0f) return 50;
    if (!ComposeAlphaScale(two, 1, &out) || out != 0.5f) return 51;
    if (!ComposeAlphaScale(two, 2, &out) || out != 2.0f) return 52;
    out = 7.0f;
    const float bad_nan[2] = {0.5f, nan}, bad_inf[2] = {inf, 2.0f}, bad_ninf[2] = {-inf, 2.0f};
    if (ComposeAlphaScale(bad_nan, 2, &out) || ComposeAlphaScale(bad_inf, 2, &out) ||
        ComposeAlphaScale(bad_ninf, 2, &out)) {
      return 53;
    }
    const float big[2] = {1e30f, 1e30f};  // finite factors whose product overflows
    if (ComposeAlphaScale(big, 2, &out)) return 54;
    if (out != 7.0f) return 55;           // out untouched on failure
  }
  // DecideAlpha, every input: (test or mask, blend, colour known, has recipe).
  for (int bits = 0; bits < 16; ++bits) {
    const bool test = bits & 1, blend = bits & 2, colour = bits & 4, recipe = bits & 8;
    AlphaRule want;
    if (!test && !blend) want = AlphaRule::kOpaque;
    else if (blend && !colour) want = AlphaRule::kSkipBlendUnknown;
    else if (recipe) want = AlphaRule::kApply;
    else want = AlphaRule::kDrawAlphaUnknown;
    if (DecideAlpha(test, blend, colour, recipe) != want) return 100 + bits;
  }
  // Spot checks of the named cases, so the loop above cannot mirror a wrong rule.
  if (DecideAlpha(false, false, false, false) != AlphaRule::kOpaque) return 40;
  if (DecideAlpha(true, false, false, false) != AlphaRule::kDrawAlphaUnknown) return 41;   // test, no recipe
  if (DecideAlpha(true, false, false, true) != AlphaRule::kApply) return 42;               // colour unknown only matters for blend
  if (DecideAlpha(false, true, false, true) != AlphaRule::kSkipBlendUnknown) return 43;
  if (DecideAlpha(false, true, true, false) != AlphaRule::kDrawAlphaUnknown) return 44;    // blend, colour, no recipe
  if (DecideAlpha(false, true, true, true) != AlphaRule::kApply) return 45;
  if (const int code = LightTests()) {
    std::printf("FAIL: light inputs case %d\n", code);  // (an exit code would wrap at 256)
    return 199;
  }
  std::puts("PASS: material refs, uv fetch, alpha scale, alpha rule, light inputs");
  return 0;
}
