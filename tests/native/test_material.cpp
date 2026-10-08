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
static const VsLightSpec kStreamVs{0xECD6, LightVertexKind::kStream, {1, 16, 2}, {4, 6, 0}, -1, 7 * 4 + 3, 2.2f};
static const VsLightSpec kProbeVs{0x79EA, LightVertexKind::kProbe, {1, 16, 2}, {4, 38, 0}, 2, 7 * 4 + 3, 0.0f};
static const VsLightSpec kInstanceVs{0x8123, LightVertexKind::kInstance, {5, 32, 2}, {3, 6, 6}, -1, -1, 0.0f};
static const VsLightSpec kTerrainVs{0xC30A, LightVertexKind::kTerrain, {-1, 0, 0}, {-1, 0, 0}, -1, -1, 0.0f};

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
  SetReg(c.vs, 20, 224.0f, 81.0f, 82.0f, 83.0f);
  for (uint32_t r = 0; r < 12; ++r) SetReg(c.vs, 28 + r, 100.0f + r, 200.0f + r, 300.0f + r, 400.0f + r);
  SetReg(c.vs, 45, 1.0f, -200.0f, 100.0f, 0.5f);
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
                                         {F::kPlantLit, K::kInstance}};
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
    if (LightRegisterRuns(kTrunkPs, kProbeVs, r) != 5 || !is(0, 0, 4, 3) || !is(1, 0, 7, 1) || !is(2, 0, 28, 12) ||
        !is(3, 1, 26, 2) || !is(4, 1, 49, 1)) {
      return 215;
    }
    VsLightSpec skinned = kProbeVs;
    skinned.scale_ref = -1;  // the skinned shaders multiply by no constant
    if (LightRegisterRuns(kObjectPs, skinned, r) != 4 || !is(0, 0, 4, 3) || !is(1, 0, 28, 12)) return 216;
    if (LightRegisterRuns(kPlantPs, kInstanceVs, r) != 1 || !is(0, 0, 45, 1)) return 217;
    if (LightRegisterRuns(kPlantLitPs, kInstanceVs, r) != 1 || !is(0, 0, 45, 1)) return 218;
    const PsLightSpec none{1, LightFamily::kNone, false, 0.0f};
    if (LightRegisterRuns(none, kStreamVs, r) != 0) return 219;
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
    if (!MakeLightElement(f[1], {true, {0x100000, 4000}, 2}, &e)) return 224;
    if (e.vb.phys_addr != 0x100000 || e.vb.size != 4000 || e.offset_bytes != 8 || e.stride_bytes != 20 ||
        e.swizzle != 0xE88 || e.format != 16 || e.flags != (kLightElementSigned | kLightElementNormalized | (2 << 2))) {
      return 225;
    }
    if (!MakeLightElement(f[4], {true, {0x200000, 4}, 0}, &e) || e.flags != kLightElementNormalized || e.stride_bytes != 4) return 226;
    if (MakeLightElement(f[4], {true, {0x200000, 3}, 0}, &e)) return 227;     // no whole element
    if (MakeLightElement(f[1], {false, {0x100000, 4000}, 2}, &e)) return 228;  // not resolved
    if (MakeLightElement(f[1], {true, {0x100000, 4000}, 4}, &e)) return 229;   // not an endian
    VertexFetch bad = f[1];
    bad.offset_dwords = 5;  // past the vertex (stride 5 dwords)
    if (MakeLightElement(bad, {true, {0x100000, 4000}, 2}, &e)) return 230;
    bad = f[1]; bad.offset_dwords = -1;
    if (MakeLightElement(bad, {true, {0x100000, 4000}, 2}, &e)) return 231;
    bad = f[1]; bad.stride_dwords = 0;
    if (MakeLightElement(bad, {true, {0x100000, 4000}, 2}, &e)) return 232;
    bad = f[3];             // a format the light has no element of (2_10_10_10)
    if (MakeLightElement(bad, {true, {0x100000, 4000}, 2}, &e)) return 233;
    bad = f[0]; bad.offset_dwords = 4;  // a half4 (8 bytes) at the last dword of a 5-dword vertex
    if (MakeLightElement(bad, {true, {0x100000, 4000}, 2}, &e)) return 234;
  }

  // --- a draw of the object family with the colour stream (0x8D90 with 0xECD6) ---
  {
    LightCase c = StreamCase();
    LightInputs l;
    const char* why = "?";
    if (MakeLightInputs(c.d, &l, &why) != LightReason::kNone || why[0] != 0) return 240;
    if (l.family != LightFamily::kObject || l.kind != LightVertexKind::kStream || l.multi || l.a0_comp != -1) return 241;
    if (l.normal.vb.phys_addr != 0x100000 || l.normal.offset_bytes != 8 || l.normal.stride_bytes != 20 || l.normal.format != 16) return 242;
    if (l.second.vb.phys_addr != 0x200000 || l.second.vb.size != 1696 || l.second.stride_bytes != 4 ||
        l.second.format != 6 || l.second.swizzle != 0x60A) {
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
    // A rotation that is not finite, in each of its nine components; then every other value the
    // draw copies. (c4.w, c5.w, c6.w and c7.xyz are not read: not finite there is fine.)
    for (uint32_t i = 0; i < 12; ++i) {
      for (float v : {inf, -inf, nan}) {
        t = StreamCase();
        t.vs[4 * 4 + i] = v;
        if (i % 4 == 3) {
          if (MakeLightInputs(t.d, &l) != LightReason::kNone) return 260;
        } else if (!Fails(t.d, LightReason::kLightUnknown, "register-not-finite")) {
          return 261;
        }
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
        l.second.flags != (kLightElementSigned | (2 << 2))) {
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

  // --- instanced plants (0xA7E4 with 0x8123; 0x165E with 0xB636) ---
  {
    LightCase c = InstanceCase();
    LightInputs l;
    if (MakeLightInputs(c.d, &l) != LightReason::kNone) return 320;
    if (l.family != LightFamily::kPlant || l.kind != LightVertexKind::kInstance || l.a0_comp != -1) return 321;
    if (l.normal.format != 32 || l.normal.offset_bytes != 8 || l.normal.stride_bytes != 24 || l.normal.vb.phys_addr != 0x100000) return 322;
    if (l.second.format != 6 || l.second.offset_bytes != 24 || l.second.stride_bytes != 28 ||
        l.second.vb.phys_addr != 0x200000 || l.second.swizzle != 0xE0A) {
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
  if (sizeof(LightInputs) != 312 || sizeof(LightElement) != 16) {
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
