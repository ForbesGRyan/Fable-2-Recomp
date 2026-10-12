// The water records' rules (sub-project 13, plan Task 9): pure, no game.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include "../../src/native/capture/water_record.h"
using namespace fable2::native::capture;

namespace {
uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
// A draw every rule accepts: the bridge's first A17D row in shape (values of WE/b_water_d1_f1.txt, rounded).
WaterDraw Good(WaterKind kind = WaterKind::kSurface) {
  WaterDraw d;
  d.read = d.pbool_known = d.literals_match = true;
  d.vs_hash = kWaterVs;
  d.ps_hash = kind == WaterKind::kOpaque ? kWaterOpaquePs : kWaterPs;
  d.streams = 1; d.stride_dwords = 5; d.position_format = 57; d.position_offset = 0; d.pair_format = 37; d.pair_offset = 3;
  const float garbage = std::numeric_limits<float>::quiet_NaN();
  for (auto& r : d.vs) for (float& v : r) v = garbage;                 // every component starts as garbage:
  for (auto& r : d.ps) for (float& v : r) v = garbage;                 // only what a formula reads is set below
  auto set = [](float* r, std::initializer_list<float> v) { int i = 0; for (float x : v) r[i++] = x; };
  set(d.vs[kWatV115], {1.0365f, 1.1775f}); set(d.vs[kWatV116], {-2.3504f, 1.0095f});
  set(d.vs[kWatV117], {0.08f, 0.08f});     set(d.vs[kWatV118], {0.06f, 0.06f});
  set(d.ps[kWatP0], {0.0f, 0.0f, 1.0f / 1120.0f, 1.0f / 720.0f});
  set(d.ps[kWatP19], {82.07f, 157.34f, 50.17f});
  d.ps[kWatP20][3] = 8.58f;
  set(d.ps[kWatP28], {1.0f, 1.0f});
  set(d.ps[kWatP31], {0.666667f, 0.666667f});
  set(d.ps[kWatP66], {-0.83f, 0.09f, 0.55f});
  d.ps[kWatP67][0] = 19.07f; d.ps[kWatP67][2] = 1.0f; d.ps[kWatP67][3] = 0.0005f;
  set(d.ps[kWatP68], {0.1f, 0.2f, 0.3f});
  d.ps[kWatP116][0] = 0.104081f; d.ps[kWatP117][0] = 0.0f;
  set(d.ps[kWatP126], {0.027f, 0.075f, 0.043f}); set(d.ps[kWatP127], {0.059f, 0.129f, 0.122f});
  d.ps[kWatP128][0] = 0.3f;
  set(d.ps[kWatP129], {0.84f, 0.84f}); set(d.ps[kWatP130], {0.84f, 0.84f});
  d.ps[kWatP131][0] = 0.5f; d.ps[kWatP136][0] = 0.279f; d.ps[kWatP137][0] = 0.5f; d.ps[kWatP138][0] = 128.0f;
  d.ps[kWatP140][0] = 0.05f;
  d.ps[kWatP141][0] = 2.003f; d.ps[kWatP141][1] = 2.0f; d.ps[kWatP141][3] = 15.0f / 255.0f;
  set(d.ps[kWatP142], {0.5f, -0.5f, -0.7071f}); set(d.ps[kWatP143], {1.0f, 0.9f, 0.8f});
  d.pbool = kWaterB136;
  // slot 1: 2D k_8 64 x 64; slot 2: 2D k_DXN 256 x 256; slots 13, 14: 2D k_8_8_8_8 256 x 256 (dword 0 of the rows: 0x82000002)
  auto fc = [](uint32_t* p, uint32_t format, uint32_t side, uint32_t base, uint32_t d0) {
    p[0] = d0 | ((side >> 5) << 22); p[1] = base | format; p[2] = (side - 1) | ((side - 1) << 13); p[3] = 0; p[4] = 0; p[5] = 1u << 9;
  };
  fc(d.fetch[kWatMask], 2, 64, 0x1BD03000u, 0x80000002u);
  fc(d.fetch[kWatNormal], 49, 256, 0x14D40000u, 0x80000002u);
  fc(d.fetch[kWatPicture], 6, 256, 0x13401000u, 0x80000002u);
  fc(d.fetch[kWatNear], 6, 256, 0x13441000u, 0x80000002u);
  d.fog_base[0] = 0x1A500000u; d.fog_base[1] = 0x1A501000u;
  return d;
}
// The measured words (Task 4's t4_states.txt): depth, blend, colour control, the reference 0x3D70F0F2, four
// channels; and what makes the range reversed in the scene (PA_CL_VTE_CNTL bit 4 with a negative Z scale).
StateWords State(WaterKind kind) {
  StateWords s{};
  s.read = true;
  s.depth_control = kind == WaterKind::kOpaque ? 0x00708766u : 0x00708746u;
  s.blend_control = kind == WaterKind::kOpaque ? 0x00010001u : 0x07060706u;
  s.color_control = 0x0000000Cu;
  s.alpha_ref = 0x3D70F0F2u;
  s.color_mask = 0xFu;
  s.vte_cntl = 0x10u;
  s.vport_z_scale = Bits(-1.0f);
  return s;
}
}  // namespace

int main() {
  WaterReason why = WaterReason::kCount;
  // 1 to 5: the good draw of each kind is a record that carries what was read, garbage included, bit for bit
  {
    const WaterDraw d = Good();
    const WaterRecord r = MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
    if (why != WaterReason::kNone || !WaterRecordValid(r)) return 1;
    if (r.kind != uint32_t(WaterKind::kSurface) || r.place != 0 || r.pbool != kWaterB136 || r.stride_dwords != 5) return 2;
    if (std::memcmp(r.vs, d.vs, sizeof(r.vs)) != 0 || std::memcmp(r.ps, d.ps, sizeof(r.ps)) != 0 ||
        std::memcmp(r.fetch, d.fetch, sizeof(r.fetch)) != 0 || r.fog_base[0] != 0x1A500000u || r.fog_base[1] != 0x1A501000u) return 3;
    const WaterRecord o = MakeWaterRecord(Good(WaterKind::kOpaque), State(WaterKind::kOpaque), 1120, 720, &why);
    if (why != WaterReason::kNone || o.kind != uint32_t(WaterKind::kOpaque) || o.ps_hash != kWaterOpaquePs) return 4;
    if (WaterKindOf(kWaterVs, kWaterPs) != WaterKind::kSurface || WaterKindOf(kWaterVs, kWaterOpaquePs) != WaterKind::kOpaque ||
        WaterKindOf(kWaterVs, 1) != WaterKind::kNone || WaterKindOf(1, kWaterPs) != WaterKind::kNone) return 5;
    if (MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, nullptr).ps_hash != kWaterPs) return 5;   // (no `why` asked)
  }
  auto refused = [&](WaterDraw d, WaterReason want, WaterKind kind = WaterKind::kSurface) {
    WaterReason got = WaterReason::kCount;
    const WaterRecord r = MakeWaterRecord(d, State(kind), 1120, 720, &got);
    const WaterRecord zero{};
    return got == want && !WaterRecordValid(r) && std::memcmp(&r, &zero, sizeof(r)) == 0;       // no record: all zero
  };
  // 10 to 16: the rules before the registers, each alone
  { WaterDraw d = Good(); d.read = false; if (!refused(d, WaterReason::kUnread)) return 10; }
  { WaterDraw d = Good(); d.pbool_known = false; if (!refused(d, WaterReason::kUnread)) return 11; }
  { WaterDraw d = Good(); d.ps_hash = 0x1234; if (!refused(d, WaterReason::kShader)) return 12; }
  { WaterDraw d = Good(); d.vs_hash = 0x1234; if (!refused(d, WaterReason::kShader)) return 12; }
  { WaterDraw d = Good(); d.literals_match = false; if (!refused(d, WaterReason::kLiteral)) return 13; }
  { WaterDraw d = Good(); d.stride_dwords = 6; if (!refused(d, WaterReason::kFetchList)) return 14; }
  { WaterDraw d = Good(); d.pair_offset = 2; if (!refused(d, WaterReason::kFetchList)) return 15; }
  { WaterDraw d = Good(); d.streams = 2; if (!refused(d, WaterReason::kFetchList)) return 16; }
  { WaterDraw d = Good(); d.position_format = 37; if (!refused(d, WaterReason::kFetchList)) return 16; }
  { WaterDraw d = Good(); d.pair_format = 57; if (!refused(d, WaterReason::kFetchList)) return 16; }
  { WaterDraw d = Good(); d.position_offset = 1; if (!refused(d, WaterReason::kFetchList)) return 16; }
  // (the order: an unread draw is `unread` whatever else is wrong with it; a wrong pair is `shader` before `literal`)
  { WaterDraw d = Good(); d.read = false; d.ps_hash = 1; d.literals_match = false; if (!refused(d, WaterReason::kUnread)) return 17; }
  { WaterDraw d = Good(); d.ps_hash = 1; d.literals_match = false; d.streams = 0; if (!refused(d, WaterReason::kShader)) return 17; }
  // 20 to 24 (Review Focus 3): only the components a formula reads are judged
  {
    WaterDraw d = Good();                                         // every unread component IS no number in Good(): accepted (case 1)
    d.ps[kWatP129][2] = std::numeric_limits<float>::infinity(); d.ps[kWatP139][3] = 1e29f; d.ps[kWatP140][3] = 1e22f;
    if (MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why), why != WaterReason::kNone) return 20;
    d = Good(); d.ps[kWatP129][0] = std::numeric_limits<float>::quiet_NaN();
    if (!refused(d, WaterReason::kRegister)) return 21;           // one read component that is no number
    d = Good(); d.ps[kWatP19][1] = 2e6f;
    if (!refused(d, WaterReason::kRegister)) return 22;           // a place beyond 1e6
    d = Good(); d.vs[kWatV117][1] = std::numeric_limits<float>::infinity();
    if (!refused(d, WaterReason::kRegister)) return 23;
    d = Good(); d.ps[kWatP138][0] = 70000.0f;
    if (!refused(d, WaterReason::kRegister)) return 24;           // an exponent beyond 65,536
    // every read component, one at a time, no number: refused; every unread one: accepted
    for (uint32_t i = 0; i < kWaterPsRegisters; ++i) {
      for (uint32_t k = 0; k < 4; ++k) {
        d = Good();
        d.ps[i][k] = std::numeric_limits<float>::quiet_NaN();
        const bool read = ((kWaterPsRead[i] >> k) & 1u) != 0 && i != kWatP139;      // (c139: under b137 alone, which gives no record)
        MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
        const WaterReason want = !read ? WaterReason::kNone : (i == kWatP20 ? WaterReason::kScale : WaterReason::kRegister);
        if (why != want) { std::printf("ps register index %u component %u: %s\n", i, k, WaterReasonName(why)); return 24; }
      }
    }
    for (uint32_t i = 0; i < kWaterVsRegisters; ++i) {
      for (uint32_t k = 0; k < 4; ++k) {
        d = Good();
        d.vs[i][k] = std::numeric_limits<float>::infinity();
        MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
        if (why != (k < 2 ? WaterReason::kRegister : WaterReason::kNone)) return 24;
      }
    }
  }
  // 25 to 29: the named bounds
  { WaterDraw d = Good(); d.ps[kWatP31][0] = 0.0f; if (!refused(d, WaterReason::kRegister)) return 25; }
  { WaterDraw d = Good(); d.ps[kWatP31][1] = 4.5f; if (!refused(d, WaterReason::kRegister)) return 25; }
  { WaterDraw d = Good(); d.ps[kWatP31][1] = 4.0f; if (refused(d, WaterReason::kRegister)) return 25; }
  { WaterDraw d = Good(); d.ps[kWatP131][0] = 65.0f; if (!refused(d, WaterReason::kRegister)) return 26; }
  { WaterDraw d = Good(); d.ps[kWatP131][0] = -0.5f; if (!refused(d, WaterReason::kRegister)) return 26; }
  { WaterDraw d = Good(); d.ps[kWatP131][0] = 0.0f; if (refused(d, WaterReason::kRegister)) return 26; }
  { WaterDraw d = Good(); d.ps[kWatP141][3] = 1.5f; if (!refused(d, WaterReason::kRegister)) return 27; }
  { WaterDraw d = Good(); d.ps[kWatP0][2] = 1.0f / 1280.0f; if (!refused(d, WaterReason::kRegister)) return 28; }   // not 1 / the scene's width
  { WaterDraw d = Good(); d.ps[kWatP0][3] = 1.0f / 721.0f; if (!refused(d, WaterReason::kRegister)) return 28; }
  { WaterReason w; MakeWaterRecord(Good(), State(WaterKind::kSurface), 0, 720, &w); if (w != WaterReason::kRegister) return 28; }
  { WaterDraw d = Good(); d.ps[kWatP20][3] = 0.0f; if (!refused(d, WaterReason::kScale)) return 29; }
  { WaterDraw d = Good(); d.ps[kWatP20][3] = 64.5f; if (!refused(d, WaterReason::kScale)) return 29; }
  // 30 to 37: the state, by kind
  {
    StateWords s = State(WaterKind::kSurface);
    s.blend_control = 0x00010001u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 30;      // a surface that does not blend
    s = State(WaterKind::kSurface); s.depth_control = 0x00708742u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 31;      // (ledger ruling 27) no depth write is not the game's water
    s = State(WaterKind::kSurface); s.alpha_ref = Bits(1.5f);
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 32;
    s = State(WaterKind::kSurface); s.color_mask = 0x7u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 33;
    s = State(WaterKind::kSurface); s.read = false;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kUnread) return 34;
    if (MakeWaterRecord(Good(WaterKind::kOpaque), State(WaterKind::kSurface), 1120, 720, &why), why != WaterReason::kState) return 35;
    if (MakeWaterRecord(Good(), State(WaterKind::kOpaque), 1120, 720, &why), why != WaterReason::kState) return 36;
    s = State(WaterKind::kSurface); s.color_control = 0x0000001Cu;                                // alpha-to-mask
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kSurface); s.color_control = 0x00000004u;                                // no alpha test
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kSurface); s.vte_cntl = 0;                                               // not the reversed range
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kSurface); s.blend_control = 0x07060106u;                                // source alpha, one
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kSurface); s.blend_control = 0x07010701u;                                // one, inverse source alpha
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kSurface); s.depth_control = 0x00708766u;                                // the surface with the twin's depth function
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kOpaque); s.depth_control = 0x00708746u;                                 // the twin with the surface's
    if (MakeWaterRecord(Good(WaterKind::kOpaque), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    s = State(WaterKind::kOpaque); s.blend_control = 0x07060706u;                                 // the twin blended
    if (MakeWaterRecord(Good(WaterKind::kOpaque), s, 1120, 720, &why), why != WaterReason::kState) return 37;
    // bits of the words that no field of the decoded state reads are not compared (a whole-word compare would
    // refuse a draw for a bit whose meaning is not established: sub-project 12's review of Tasks 8 and 9)
    s = State(WaterKind::kSurface); s.depth_control ^= 0x00100000u;
    if (MakeWaterRecord(Good(), s, 1120, 720, &why), why != WaterReason::kNone) return 38;
    // the captured reference is one unit in the last place above c141.w: two numbers, both accepted as they are
    s = State(WaterKind::kSurface);
    WaterDraw d = Good();
    d.ps[kWatP141][3] = std::bit_cast<float>(0x3D70F0F1u);
    if (MakeWaterRecord(d, s, 1120, 720, &why), why != WaterReason::kNone || Bits(d.ps[kWatP141][3]) == s.alpha_ref) return 39;
  }
  // 40 to 49: the fetches, the shadow term and the bools
  { WaterDraw d = Good(); d.fetch[kWatMask][1] = 0x1BD03000u | 49; if (!refused(d, WaterReason::kFetch)) return 40; }
  { WaterDraw d = Good(); d.fetch[kWatNormal][1] = 0x14D40000u | 18; if (!refused(d, WaterReason::kFetch)) return 41; }
  { WaterDraw d = Good(); d.fetch[kWatPicture][2] = 127u | (127u << 13); if (!refused(d, WaterReason::kFetch)) return 42; }   // 128 x 128
  { WaterDraw d = Good(); d.fetch[kWatPicture][1] = 0; if (!refused(d, WaterReason::kFetch)) return 43; }
  { WaterDraw d = Good(); d.fetch[kWatPicture][1] = 6; if (!refused(d, WaterReason::kFetch)) return 43; }                    // the format without a base
  { WaterDraw d = Good(); d.fetch[kWatNear][1] = 0; if (refused(d, WaterReason::kFetch)) return 44; }   // slot 14 is no texture of the native draw: not judged
  { WaterDraw d = Good(); d.pbool |= kWaterB137; if (!refused(d, WaterReason::kShadowTerm)) return 45; }
  { WaterDraw d = Good(WaterKind::kOpaque); d.pbool |= kWaterB137; if (!refused(d, WaterReason::kShadowTerm, WaterKind::kOpaque)) return 45; }
  // (ledger ruling 33) a mask or a normal map that is not delivered as an unsigned fraction; a picture with a gamma sign
  { WaterDraw d = Good(); d.fetch[kWatMask][0] |= 1u << 2; if (!refused(d, WaterReason::kFetch)) return 46; }      // signed
  { WaterDraw d = Good(); d.fetch[kWatMask][0] |= 2u << 2; if (!refused(d, WaterReason::kFetch)) return 46; }      // biased
  { WaterDraw d = Good(); d.fetch[kWatMask][3] |= 5u << 13; if (!refused(d, WaterReason::kFetch)) return 46; }     // an exponent adjust
  { WaterDraw d = Good(); d.fetch[kWatMask][3] |= 1u; if (!refused(d, WaterReason::kFetch)) return 46; }           // the integer format
  { WaterDraw d = Good(); d.fetch[kWatNormal][0] |= 1u << 4; if (!refused(d, WaterReason::kFetch)) return 46; }    // the map's y signed
  { WaterDraw d = Good(); d.fetch[kWatPicture][0] |= 3u << 4; if (!refused(d, WaterReason::kFetch)) return 47; }   // gamma on green
  { WaterDraw d = Good(); d.fetch[kWatPicture][0] |= 3u << 8; if (refused(d, WaterReason::kFetch)) return 47; }    // (alpha: not a colour of the lookup)
  // (ledger ruling 34) c128.x, c136.x and c138.x strictly above 0: the console's zero-product points give no record
  { WaterDraw d = Good(); d.ps[kWatP128][0] = 0.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP136][0] = 0.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP138][0] = 0.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP138][0] = -2.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP128][0] = 1.0f / 131072.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP128][0] = 65.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP136][0] = 65.0f; if (!refused(d, WaterReason::kRegister)) return 48; }
  { WaterDraw d = Good(); d.ps[kWatP128][0] = 1.0f / 65536.0f; d.ps[kWatP136][0] = 64.0f; if (refused(d, WaterReason::kRegister)) return 48; }
  // a draw with b136 clear does not read the sun's registers: whatever they hold, it is a record
  {
    WaterDraw d = Good();
    d.pbool = 0;
    for (uint32_t i : {uint32_t(kWatP136), uint32_t(kWatP137), uint32_t(kWatP138), uint32_t(kWatP142), uint32_t(kWatP143)}) {
      for (float& v : d.ps[i]) v = std::numeric_limits<float>::quiet_NaN();
    }
    const WaterRecord r = MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
    if (why != WaterReason::kNone || r.pbool != 0 || std::memcmp(r.ps, d.ps, sizeof(r.ps)) != 0) return 49;
    d.pbool = kWaterB136;
    if (!refused(d, WaterReason::kRegister)) return 49;
  }
  // 50 to 54: the fog rule, bit for bit
  {
    FogBlock b{};
    b.valid = true;
    const WaterDraw d = Good();
    const WaterRecord r = MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why);
    b.amount[0] = d.ps[kWatP28][0]; b.amount[1] = d.ps[kWatP28][1];
    for (int i = 0; i < 3; ++i) { b.axis[i] = d.ps[kWatP66][i]; b.near_colour[i] = d.ps[kWatP68][i]; }
    b.range[0] = d.ps[kWatP67][0]; b.range[2] = d.ps[kWatP67][2]; b.range[3] = d.ps[kWatP67][3];
    b.range[1] = 123.0f;                                           // c67.y: not read by the water, not compared
    const uint32_t base[2] = {0x1A500000u, 0x1A501000u};
    if (!WaterFogMatches(r, b, base)) return 50;
    FogBlock other = b; other.amount[0] = std::nextafter(b.amount[0], 2.0f);
    if (WaterFogMatches(r, other, base)) return 51;
    other = b; other.valid = false;
    if (WaterFogMatches(r, other, base)) return 52;
    const uint32_t swapped[2] = {0x1A501000u, 0x1A500000u};
    if (WaterFogMatches(r, b, swapped) || WaterFogMatches(r, b, nullptr)) return 53;
    const uint32_t none[2] = {0, 0x1A501000u};
    WaterRecord unbound = r;
    unbound.fog_base[0] = 0;
    if (WaterFogMatches(unbound, b, none)) return 53;              // an address of 0 is no table
    other = b; other.range[3] = -b.range[3];
    if (WaterFogMatches(r, other, base)) return 54;
    other = b; other.axis[2] = std::nextafter(b.axis[2], 2.0f);
    if (WaterFogMatches(r, other, base)) return 54;
    other = b; other.near_colour[1] = std::nextafter(b.near_colour[1], 2.0f);
    if (WaterFogMatches(r, other, base)) return 54;
    other = b; other.amount[1] = std::nextafter(b.amount[1], 2.0f);
    if (WaterFogMatches(r, other, base)) return 54;
  }
  // 55, 56: the literal table's rule
  {
    uint32_t bits[8][4] = {};
    uint32_t have = 0;
    for (const WaterTable::Literal& l : kWaterTable.literal) {
      for (uint32_t c = 0; c < 4; ++c) {
        bits[l.reg - 248][c] = l.bits[c];
        have |= 1u << ((l.reg - 248) * 4 + c);
      }
    }
    if (!WaterLiteralsMatch(bits, have) || WaterLiteralsMatch(nullptr, have)) return 55;
    if (WaterLiteralsMatch(bits, have & ~(1u << (7 * 4 + 3)))) return 56;                 // c255.w not held
    bits[6][1] ^= 1u;                                                                     // c254.y by one unit
    if (WaterLiteralsMatch(bits, have)) return 56;
    bits[6][1] ^= 1u;
    bits[6][3] ^= 1u;                                                                     // c254.w: no formula reads it; it is still the shader's
    if (WaterLiteralsMatch(bits, have)) return 56;
  }
  if (std::strcmp(WaterReasonName(WaterReason::kFetchList), "fetch-list") != 0 || std::strcmp(WaterReasonName(WaterReason::kShadowTerm), "shadow-term") != 0 ||
      std::strcmp(WaterReasonName(WaterReason::kOverLimit), "over-limit") != 0 || size_t(WaterReason::kCount) != 12 || Bits(1.0f) != 0x3F800000u) return 60;
  if (std::strcmp(WaterReasonName(WaterReason::kNone), "none") != 0 || std::strcmp(WaterReasonName(WaterReason::kUnread), "unread") != 0 ||
      std::strcmp(WaterReasonName(WaterReason::kFog), "fog") != 0 || std::strcmp(WaterReasonName(WaterReason::kCount), "?") != 0 ||
      std::strcmp(kWaterRecordReason, "water-record") != 0 || std::strcmp(WaterKindName(WaterKind::kOpaque), "opaque") != 0) return 60;
  // 62: the fetch list from the shader's decoded fetches (the captured rows': slot 95, 57 at 0 and 37 at 3 of 5 dwords)
  {
    VertexFetch f[3] = {};
    f[0].fetch_slot = 95; f[0].format = 57; f[0].offset_dwords = 0; f[0].stride_dwords = 5;
    f[1].fetch_slot = 95; f[1].format = 37; f[1].offset_dwords = 3; f[1].stride_dwords = 5; f[1].mini = true;
    WaterDraw d = Good();
    d.streams = 9; d.stride_dwords = 9; d.pair_offset = 9;
    WaterFetchList(f, 2, &d);
    if (d.streams != 1 || d.stride_dwords != 5 || d.position_format != 57 || d.position_offset != 0 || d.pair_format != 37 || d.pair_offset != 3) return 62;
    if (MakeWaterRecord(d, State(WaterKind::kSurface), 1120, 720, &why), why != WaterReason::kNone) return 62;
    f[1].fetch_slot = 94;                                           // two streams
    WaterFetchList(f, 2, &d);
    if (d.streams != 2 || !refused(d, WaterReason::kFetchList)) return 62;
    f[1].fetch_slot = 95;
    for (size_t n : {size_t(0), size_t(1), size_t(3)}) {            // not two fetches: no list
      d = Good();
      WaterFetchList(f, n, &d);
      if (d.streams != 0 || d.stride_dwords != 0 || !refused(d, WaterReason::kFetchList)) return 62;
    }
    d = Good();
    WaterFetchList(nullptr, 2, &d);
    if (d.streams != 0 || !refused(d, WaterReason::kFetchList)) return 62;
    f[1].offset_dwords = -1;                                        // (a negative offset is not 3)
    d = Good();
    WaterFetchList(f, 2, &d);
    if (!refused(d, WaterReason::kFetchList)) return 62;
  }
  // 63: the pixel shader alone tells a draw of the water
  if (WaterKindOfPs(kWaterPs) != WaterKind::kSurface || WaterKindOfPs(kWaterOpaquePs) != WaterKind::kOpaque || WaterKindOfPs(kWaterVs) != WaterKind::kNone ||
      WaterKindOfPs(0) != WaterKind::kNone || kWaterSceneWidth != 1120 || kWaterSceneHeight != 720) return 63;
  // 61: the table as the header reads it
  if (kWaterVs != 0xBEAD84BD72072E0Eull || kWaterPs != 0xA17D8AEC3A817D45ull || kWaterOpaquePs != 0xF6D98C7B4D98438Bull || kMaxWaterRecords != 64 ||
      kWaterB136 != 0x100u || kWaterB137 != 0x200u || kWaterPsRegister[kWatP141] != 141 || kWaterPsRead[kWatP141] != 0xB || kWaterPsKind[kWatP67] != 0 ||
      kWaterPsKind[kWatP138] != 3 || kWaterVsRead[kWatV118] != 0x3 || sizeof(WaterRecord) != 584) return 61;
  // 64: the registers' rule alone (what the pass asks again of a record), and the scale's
  {
    WaterDraw d = Good();
    if (!WaterRegistersBounded(d.vs, d.ps, d.pbool) || !WaterScaleBounded(d.ps[kWatP20][3])) return 64;
    d.ps[kWatP0][2] = 0.5f;                                         // (c0.zw against the scene's size is not this function's)
    if (!WaterRegistersBounded(d.vs, d.ps, d.pbool)) return 64;
    d.ps[kWatP126][1] = std::numeric_limits<float>::infinity();
    if (WaterRegistersBounded(d.vs, d.ps, d.pbool)) return 64;
    if (WaterScaleBounded(0.0f) || WaterScaleBounded(64.5f) || WaterScaleBounded(std::numeric_limits<float>::quiet_NaN()) || !WaterScaleBounded(64.0f)) return 64;
  }
  std::puts("PASS: water records (the pair, the literal table, the fetch list, bounds on read components alone, the named bounds, the state by kind, the fetch rule, the bools, the fog rule)");
  return 0;
}
