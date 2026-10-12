// Synthetic standalone test; no game or GPU. The far woods' impostor records (distant trees, plan Task 8).
#include "../../src/native/capture/impostor_record.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using namespace fable2::native::capture;

static uint16_t Half(float f) {   // exact for the small values used here
  uint32_t b;
  std::memcpy(&b, &f, 4);
  const int e = int((b >> 23) & 0xFF) - 127 + 15;
  if (f == 0.0f) return 0;
  return uint16_t(((b >> 16) & 0x8000) | (e << 10) | ((b >> 13) & 0x3FF));
}
// Quad `q`'s (x, y, z, s) into guest bytes, by the table's own order and endian.
static void PutQuad(std::vector<uint8_t>& bytes, uint32_t q, const float v[4]) {
  uint8_t host[8];   // what the GPU reads after its endian swap: four little-endian halves
  for (int k = 0; k < 4; ++k) {
    const uint16_t h = Half(v[k]);
    host[2 * kImpostorWordSource[k]] = uint8_t(h);
    host[2 * kImpostorWordSource[k] + 1] = uint8_t(h >> 8);
  }
  // render::CopySwapped's swaps (8-in-16, 8-in-32, 16-in-32), each its own inverse.
  const int flip = kImpostorWordEndian == 1 ? 1 : kImpostorWordEndian == 2 ? 3 : kImpostorWordEndian == 3 ? 2 : 0;
  for (int i = 0; i < 8; ++i) bytes[8 * q + i] = host[i ^ flip];
}
static ImpostorCall Good(uint32_t quads) {
  ImpostorCall c;
  c.read = true;
  c.ps_hash = kImpostorPs;
  c.vs_hash = kImpostorVs;
  c.literals_match = true;
  c.prim = kImpostorPrimitive;
  c.count = 4 * quads;
  c.stride = kImpostorStride;
  c.storage = 0xFF762700u;                     // capture 155547's first row: the 0xE0000000 view's upper part
  c.storage_dwords = 4 * quads;
  for (auto& r : c.vs) for (float& v : r) v = 0.5f;   // (inside every vertex register's bound of the table)
  const float c12[4] = {20.0f, 35.0f, 2.0f, 0.468f}, c19[4] = {82.0f, 157.0f, 50.0f, 1.0f}, c20[4] = {0.005f, 0.07f, 0.09f, 8.6f},
              c29[4] = {48.0f, 0.125f, 998.4f, 25.6f}, c31[4] = {0.3668f, 0.0f, 0.0f, 0.0f};
  std::memcpy(c.ps[kImpP12], c12, 16); std::memcpy(c.ps[kImpP19], c19, 16); std::memcpy(c.ps[kImpP20], c20, 16);
  std::memcpy(c.ps[kImpP29], c29, 16); std::memcpy(c.ps[kImpP31], c31, 16);
  c.pbool = 0x00000110u;                       // every captured row's word as its last writer left it
  const uint32_t tf0[6] = {kImpostorFetch0, 0x12704043u, 255u | (255u << 13), kImpostorSwizzle << 1, 3u << 6, (1u << 9) | 0x12724000u};
  std::memcpy(c.fetch, tf0, sizeof(tf0));
  c.vfetch[0] = 0x1F763703u;                   // the same row's packet: GpuAddress(storage) | 3
  c.vfetch[1] = 0x10000000u | (c.storage_dwords << 2) | kImpostorWordEndian;
  c.state.read = true;
  c.state.color_control = 0xAA00001Cu;
  c.state.alpha_ref = 0x3F008081u;             // 128 / 255: every captured row's
  c.state.blend_control = 0x00010001u;
  c.state.color_mask = 0xF;
  c.state.depth_control = 0x00708766u;
  c.state.vport_z_scale = 0xBF800000u;         // -1: the reversed range
  c.state.vte_cntl = 0x10;
  c.state.surface_info = 1u << 16;             // two samples
  return c;
}

int main() {
  bool known = true;
  if (ParseImpostorMode("off", &known) != ImpostorMode::kOff || !known) return 1;
  if (ParseImpostorMode("false", &known) != ImpostorMode::kOff || !known || ParseImpostorMode("0", &known) != ImpostorMode::kOff || !known) return 2;
  if (ParseImpostorMode("flat", &known) != ImpostorMode::kFlat || !known) return 3;
  if (ParseImpostorMode("on", &known) != ImpostorMode::kOn || !known || ParseImpostorMode("true", &known) != ImpostorMode::kOn ||
      ParseImpostorMode("1", &known) != ImpostorMode::kOn || !known) return 4;
  if (ParseImpostorMode("FLAT", &known) != ImpostorMode::kOff || known) return 5;              // unknown words are off, and said
  if (ParseImpostorMode("", &known) != ImpostorMode::kOff || known) return 6;
  if (std::strcmp(ImpostorModeName(ImpostorMode::kFlat), "flat") != 0 || std::strcmp(ImpostorReasonName(ImpostorReason::kGroundFog), "ground-fog") != 0 ||
      std::strcmp(ImpostorReasonName(ImpostorReason::kFogTable), "fog-table") != 0 || std::strcmp(ImpostorReasonName(ImpostorReason::kOverLimit), "over-limit") != 0) return 7;
  if (std::strcmp(ImpostorModeName(ImpostorMode::kOff), "off") != 0 || std::strcmp(ImpostorModeName(ImpostorMode::kOn), "on") != 0 ||
      std::strcmp(ImpostorReasonName(ImpostorReason::kPending), "pending") != 0 || std::strcmp(ImpostorReasonName(ImpostorReason::kCount), "?") != 0) return 7;
  if (ImpostorQuads(4) != 1 || ImpostorQuads(3712) != 928 || ImpostorQuads(0) != 0 || ImpostorQuads(6) != 0 ||
      ImpostorQuads(4 * kMaxImpostorQuads) != kMaxImpostorQuads || ImpostorQuads(4 * kMaxImpostorQuads + 4) != 0) return 8;
  // The closed numbers (plan Task 3's): fourteen and five registers, the word's order and endian, the swizzle, the bit.
  if (kImpostorVsRegisters != 14 || kImpostorPsRegisters != 5 || kImpostorVsRegister[kImpC8] != 8 || kImpostorVsRegister[kImpC117] != 117 ||
      kImpostorVsRegister[kImpC110] != 110 || kImpostorPsRegister[kImpP12] != 12 || kImpostorPsRegister[kImpP31] != 31) return 40;
  if (kImpostorWordSource[0] != 1 || kImpostorWordSource[1] != 0 || kImpostorWordSource[2] != 3 || kImpostorWordSource[3] != 2 ||
      kImpostorWordEndian != 2 || kImpostorSwizzle != 0x60Au || kImpostorB129 != 2u || kImpostorSizeMax != 4096.0f) return 41;       // (a quad's size: a sanity bound, ledger ruling 54)

  // The call's rules, each alone, in their order.
  if (CheckImpostorCall(Good(3)) != ImpostorReason::kNone) return 10;
  { ImpostorCall c = Good(3); c.read = false; c.ps_hash = 1; if (CheckImpostorCall(c) != ImpostorReason::kUnread) return 11; }   // unread before shader
  { ImpostorCall c = Good(3); c.vs_hash = kImpostorPs; if (CheckImpostorCall(c) != ImpostorReason::kShader) return 12; }
  { ImpostorCall c = Good(3); c.ps_hash = kImpostorVs; if (CheckImpostorCall(c) != ImpostorReason::kShader) return 12; }
  { ImpostorCall c = Good(3); c.literals_match = false; if (CheckImpostorCall(c) != ImpostorReason::kLiteral) return 13; }
  for (int k = 0; k < 9; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.prim = 4;
    if (k == 1) c.stride = 20;
    if (k == 2) c.count = 14;                  // not a multiple of four
    if (k == 3) c.storage = 0;
    if (k == 4) c.storage_dwords = 6;          // another draw's field
    if (k == 5) c.vfetch[0] ^= 0x1000u;        // the fetch constant names other memory
    if (k == 6) c.vfetch[1] ^= 1u;             // another endian: the words would be read in another order
    if (k == 7) c.vfetch[1] += 4u << 2;        // another size
    if (k == 8) c.vfetch[0] &= ~1u;            // not a vertex fetch constant
    if (CheckImpostorCall(c) != ImpostorReason::kStorage) return 14;
  }
  { ImpostorCall c = Good(3); c.storage = 0x4A120000u; c.vfetch[0] = 0x0A120003u;        // the address relation outside the upper view
    if (CheckImpostorCall(c) != ImpostorReason::kNone) return 14; }
  { ImpostorCall c = Good(3); c.vs[kImpC115][2] = std::numeric_limits<float>::quiet_NaN(); if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 15; }
  { ImpostorCall c = Good(3); c.ps[kImpP29][1] = 0.0f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }   // c29.y in (0, 4]
  { ImpostorCall c = Good(3); c.ps[kImpP31][0] = 65.0f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }  // c31.x in (0, 64]
  { ImpostorCall c = Good(3); c.vs[kImpC72][0] = 2e7f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }   // a matrix row beyond 1e7
  { ImpostorCall c = Good(3); c.ps[kImpP12][3] = 64.5f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }  // c12.w in [0, 64]
  { ImpostorCall c = Good(3); c.ps[kImpP12][3] = -0.5f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }
  { ImpostorCall c = Good(3); c.vs[kImpC8][2] = -70000.0f; if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 16; }   // the raise beyond a scale's reach
  // The bounds are sanity bounds by kind (review of the whole branch, I2; ledger ruling 54): what they were fitted
  // to before (four times one place's values) no longer refuses a call. Each of these was `register`.
  { ImpostorCall c = Good(3); c.ps[kImpP12][3] = 4.5f; if (CheckImpostorCall(c) != ImpostorReason::kNone) return 50; }
  { ImpostorCall c = Good(3); c.vs[kImpC8][2] = -4.0f; if (CheckImpostorCall(c) != ImpostorReason::kNone) return 50; }
  { ImpostorCall c = Good(3); c.vs[kImpC111][0] = 81.6f; if (CheckImpostorCall(c) != ImpostorReason::kNone) return 50; }      // a fog range starting beyond 81.565
  // A made far-away region: the eye at a world position of (5000, -5000, 300), a fog range that starts at 200 units
  // and ends 6,000 further, a dissolve that starts at 300, other scales and a view with a large translation.
  auto far = [] {
    ImpostorCall c = Good(3);
    const float c8[4] = {0.0f, 0.0f, 1.6f, 0.0f}, c47[4] = {5000.0f, -5000.0f, 300.0f, 0.0f}, c110[4] = {0.6f, -0.64f, 0.48f, 0.0f},
                c111[4] = {200.0f, 6500.0f, 1.0f, 1.0f / 6000.0f}, c114[4] = {3.0f, 4.0f, 0.0f, 0.0f}, c115[4] = {0.6f, 0.8f, 0.0f, 1000.0f},
                c116[4] = {-0.3f, 0.2f, 0.93f, -579.0f}, c117[4] = {0.74f, -0.56f, 0.37f, -6611.0f};
    std::memcpy(c.vs[kImpC8], c8, 16); std::memcpy(c.vs[kImpC47], c47, 16); std::memcpy(c.vs[kImpC110], c110, 16);
    std::memcpy(c.vs[kImpC111], c111, 16); std::memcpy(c.vs[kImpC114], c114, 16); std::memcpy(c.vs[kImpC115], c115, 16);
    std::memcpy(c.vs[kImpC116], c116, 16); std::memcpy(c.vs[kImpC117], c117, 16);
    const float c12[4] = {20.0f, 250.0f, 2.0f, 2.5f}, c19[4] = {5000.0f, -5000.0f, 300.0f, 1.0f}, c29[4] = {300.0f, 0.02f, 998.4f, 25.6f};
    std::memcpy(c.ps[kImpP12], c12, 16); std::memcpy(c.ps[kImpP19], c19, 16); std::memcpy(c.ps[kImpP29], c29, 16);
    return c;
  };
  if (CheckImpostorCall(far()) != ImpostorReason::kNone) return 51;
  // ... and garbage in any component a formula reads refuses it all the same: no number, an infinity, a magnitude no
  // value of its kind has (beyond every bound of the table).
  for (uint32_t kind = 0; kind < 2; ++kind) {
    for (uint32_t i = 0; i < (kind == 0 ? kImpostorVsRegisters : kImpostorPsRegisters); ++i) {
      for (uint32_t k = 0; k < 4; ++k) {
        if (!detail::ImpostorComponentRead(kind, i, k)) continue;
        for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1e20f, -1e20f, 3.0e38f}) {
          ImpostorCall c = far();
          (kind == 0 ? c.vs[i][k] : c.ps[i][k]) = bad;
          const ImpostorReason want = (kind == 1 && i == kImpP20 && k == 3) ? ImpostorReason::kScale : ImpostorReason::kRegister;
          if (CheckImpostorCall(c) != want) return 52;
        }
      }
    }
  }
  // The kinds' reaches, at their edges: a place or distance within 1e6, a scale within 65,536, a matrix term within 1e7.
  { ImpostorCall c = far(); c.vs[kImpC111][0] = 1.0e6f; c.vs[kImpC47][1] = -1.0e6f; c.vs[kImpC8][2] = 65536.0f; c.vs[kImpC113][1] = -65536.0f;
    c.vs[kImpC114][1] = 65536.0f; c.vs[kImpC110][0] = -65536.0f; c.vs[kImpC46][2] = 65536.0f; c.vs[kImpC117][3] = -1.0e7f;
    if (CheckImpostorCall(c) != ImpostorReason::kNone) return 53; }
  for (int k = 0; k < 8; ++k) {
    ImpostorCall c = far();
    if (k == 0) c.vs[kImpC111][0] = 1.0001e6f;
    if (k == 1) c.vs[kImpC47][1] = -1.0001e6f;
    if (k == 2) c.vs[kImpC8][2] = 65537.0f;
    if (k == 3) c.vs[kImpC113][1] = -65537.0f;
    if (k == 4) c.vs[kImpC114][1] = 65537.0f;
    if (k == 5) c.vs[kImpC110][0] = -65537.0f;
    if (k == 6) c.vs[kImpC46][2] = 65537.0f;
    if (k == 7) c.vs[kImpC117][3] = -1.0001e7f;
    if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 54;
  }
  for (float w : {0.0f, 64.5f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    ImpostorCall c = Good(3);
    c.ps[kImpP20][3] = w;
    if (CheckImpostorCall(c) != ImpostorReason::kScale) return 17;
  }
  { ImpostorCall c = Good(3); c.ps[kImpP20][3] = 64.0f; if (CheckImpostorCall(c) != ImpostorReason::kNone) return 17; }
  // Ledger ruling 34: a component no formula reads is carried and never judged, whatever it holds.
  { ImpostorCall c = Good(3);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    c.vs[kImpC8][0] = 1e30f; c.vs[kImpC8][1] = nan; c.vs[kImpC8][3] = inf; c.vs[kImpC46][3] = nan; c.vs[kImpC47][3] = 1e30f;
    c.vs[kImpC110][3] = nan; c.vs[kImpC111][1] = 8000.0f; c.vs[kImpC114][2] = inf; c.vs[kImpC114][3] = nan;
    c.ps[kImpP12][0] = 1e30f; c.ps[kImpP12][2] = nan; c.ps[kImpP19][3] = nan; c.ps[kImpP20][0] = inf; c.ps[kImpP20][1] = nan;
    c.ps[kImpP20][2] = -1e30f; c.ps[kImpP29][2] = nan; c.ps[kImpP29][3] = 1e30f; c.ps[kImpP31][1] = nan; c.ps[kImpP31][2] = inf;
    c.ps[kImpP31][3] = nan;
    if (CheckImpostorCall(c) != ImpostorReason::kNone) return 43; }
  // ... and every component a formula reads is judged: one that is no number refuses the call.
  for (uint32_t kind = 0; kind < 2; ++kind) {
    uint32_t read = 0;
    for (uint32_t i = 0; i < (kind == 0 ? kImpostorVsRegisters : kImpostorPsRegisters); ++i) {
      for (uint32_t k = 0; k < 4; ++k) {
        ImpostorCall c = Good(3);
        (kind == 0 ? c.vs[i][k] : c.ps[i][k]) = std::numeric_limits<float>::quiet_NaN();
        const ImpostorReason want = !detail::ImpostorComponentRead(kind, i, k) ? ImpostorReason::kNone
                                    : (kind == 1 && i == kImpP20 && k == 3) ? ImpostorReason::kScale : ImpostorReason::kRegister;
        if (CheckImpostorCall(c) != want) return 44;
        read += detail::ImpostorComponentRead(kind, i, k) ? 1 : 0;
      }
    }
    if (read != (kind == 0 ? 47u : 9u)) return 45;   // 56 - 9 vertex components, 20 - 11 pixel ones (the Tasks 4-5 report's list)
  }
  { ImpostorCall c = Good(3); c.ps[kImpP20][3] = 0.0f; c.vs[kImpC8][2] = 99000.0f;                // register before scale
    if (CheckImpostorCall(c) != ImpostorReason::kRegister) return 17; }
  for (int k = 0; k < 13; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.state.read = false;
    if (k == 1) c.state.depth_control &= ~0x4u;            // no depth write
    if (k == 2) c.state.blend_control = 0x07060706u;       // a blend
    if (k == 3) c.state.color_mask = 0x7;
    if (k == 4) c.state.color_control = 0xAA000014u;       // alpha-to-mask without the alpha test
    if (k == 5) c.state.color_control = 0x8700001Cu;       // other offsets
    if (k == 6) c.state.surface_info = 0;                  // one sample
    if (k == 7) c.state.vport_z_scale = 0x3F800000u;       // not the reversed range
    if (k == 8) c.state.depth_control &= ~0x2u;            // no depth test
    if (k == 9) c.state.depth_control = 0x00708736u;       // the function "less or equal"
    if (k == 10) c.state.color_control = 0xAA00000Cu;      // the alpha test without alpha-to-mask
    if (k == 11) c.state.color_control = 0xAA00001Eu;      // the alpha test's function "greater or equal"
    if (k == 12) c.state.alpha_ref = 0x3FC00000u;          // a reference of 1.5
    if (CheckImpostorCall(c) != ImpostorReason::kState) return 18;
  }
  for (int k = 0; k < 9; ++k) {
    ImpostorCall c = Good(3);
    if (k == 0) c.fetch[1] = 0x12704052u;                  // DXT1
    if (k == 1) c.fetch[2] = 127u | (127u << 13);          // 128 x 128
    if (k == 2) c.fetch[0] |= 1u << 2;                     // a sign field: the fetch is not as stored
    if (k == 3) c.fetch[4] = 2u << 6;                      // highest level 2
    if (k == 4) c.fetch[1] = 0x00000043u;                  // no base
    if (k == 5) c.fetch[1] = 0x12704083u;                  // endian 2
    if (k == 6) c.fetch[3] = 0u;                           // no swizzle: red and blue as stored
    if (k == 7) c.fetch[5] = (2u << 9) | 0x12724000u;      // not a 2D texture
    if (k == 8) c.fetch[0] &= ~(1u << 31);                 // not tiled
    if (CheckImpostorCall(c) != ImpostorReason::kFetch) return 19;
  }
  { ImpostorCall c = Good(3); c.pbool |= kImpostorB129; if (CheckImpostorCall(c) != ImpostorReason::kGroundFog) return 9; }
  { ImpostorCall c = Good(3); c.pbool = 0xFFFFFFFDu; if (CheckImpostorCall(c) != ImpostorReason::kNone) return 9; }      // every other bool: not this rule's

  // The words (Review Focus 2): whole, in the table's order, every value judged.
  const float tree[3][4] = {{100.0f, 200.0f, 30.0f, 8.0f}, {-64.0f, 512.0f, 0.5f, 12.0f}, {1024.0f, -2.0f, 96.0f, 0.25f}};
  std::vector<uint8_t> bytes(24);
  for (uint32_t q = 0; q < 3; ++q) PutQuad(bytes, q, tree[q]);
  // (a quad is four big-endian halves x, y, z, s: plan Task 3 on 4,744 captured quads)
  if (bytes[0] != 0x56 || bytes[1] != 0x40 || bytes[2] != 0x5A || bytes[3] != 0x40 || bytes[4] != 0x4F || bytes[5] != 0x80 ||
      bytes[6] != 0x48 || bytes[7] != 0x00) return 42;
  float words[12];
  ImpostorReason why = ImpostorReason::kPending;
  ImpostorRecord r = MakeImpostorRecord(Good(3), bytes.data(), bytes.size(), words, &why);
  if (why != ImpostorReason::kNone || r.ps_hash != kImpostorPs || r.vs_hash != kImpostorVs || r.quads != 3 || r.first_quad != 0 || r.place != 0) return 20;
  if (!ImpostorRecordValid(r) || ImpostorRecordValid(ImpostorRecord{})) return 20;
  if (std::memcmp(words, tree, sizeof(words)) != 0) return 21;
  if (r.ps[kImpP29][0] != 48.0f || r.depth != 0x00708766u || r.colour_control != 0xAA00001Cu || r.colour_mask != 0xF || r.blend != 0x00010001u ||
      r.surface_info != (1u << 16) || r.pbool != 0x110u || r.vfetch[0] != 0x1F763703u || r.fetch[1] != 0x12704043u || r.vs[kImpC47][1] != 0.5f) return 22;
  { uint32_t ref; std::memcpy(&ref, &r.alpha_ref, 4); if (ref != 0x3F008081u) return 22; }
  float kept[12];
  std::memcpy(kept, words, sizeof(kept));
  r = MakeImpostorRecord(Good(3), bytes.data(), 23, words, &why);                        // a byte short: nothing
  if (why != ImpostorReason::kUnread || r.ps_hash != 0 || std::memcmp(kept, words, sizeof(kept)) != 0) return 23;
  r = MakeImpostorRecord(Good(3), bytes.data(), 48, words, &why);                        // the storage whole is not the words
  if (why != ImpostorReason::kUnread || r.ps_hash != 0) return 23;
  r = MakeImpostorRecord(Good(3), nullptr, 24, words, &why);
  if (why != ImpostorReason::kUnread || r.ps_hash != 0) return 24;
  r = MakeImpostorRecord(Good(3), bytes.data(), bytes.size(), nullptr, &why);
  if (why != ImpostorReason::kUnread || r.ps_hash != 0) return 24;
  for (int k = 0; k < 6; ++k) {
    std::vector<uint8_t> bad = bytes;
    float v[4] = {100.0f, 200.0f, 30.0f, 8.0f};
    if (k == 0) v[3] = 0.0f;                                   // no size
    if (k == 1) v[3] = -8.0f;                                  // a negative size
    if (k == 3) v[3] = kImpostorSizeMax * 2.0f;                // beyond the bound
    PutQuad(bad, k == 4 ? 2 : 1, v);
    if (k == 2) { bad[8 + 0] = 0x7C; bad[8 + 1] = 0x7C; bad[8 + 2] = 0x7C; bad[8 + 3] = 0x7C; }   // two halves 0x7C7C: no numbers, whatever the endian
    if (k == 4) { bad[16 + 6] = 0x00; bad[16 + 7] = 0x00; }    // the LAST quad's size 0: every quad is judged
    if (k == 5) { bad[4] = 0x7C; bad[5] = 0x00; }              // an infinite height
    r = MakeImpostorRecord(Good(3), bad.data(), bad.size(), words, &why);
    if (why != ImpostorReason::kQuad || r.ps_hash != 0 || std::memcmp(kept, words, sizeof(kept)) != 0) return 25;
  }
  { std::vector<uint8_t> edge = bytes;                                                   // the bound itself is a size
    const float v[4] = {100.0f, 200.0f, 30.0f, kImpostorSizeMax};
    PutQuad(edge, 1, v);
    r = MakeImpostorRecord(Good(3), edge.data(), edge.size(), words, &why);
    if (why != ImpostorReason::kNone || words[7] != kImpostorSizeMax) return 25;
    std::memcpy(words, kept, sizeof(kept)); }
  { ImpostorCall c = Good(3); c.literals_match = false;                                  // the call's rules again, before any byte
    r = MakeImpostorRecord(c, bytes.data(), bytes.size(), words, &why);
    if (why != ImpostorReason::kLiteral || r.ps_hash != 0) return 26; }
  static_assert(sizeof(ImpostorRecord) % 8 == 0);
  static_assert(sizeof(ImpostorRecord) == 440);
  if (kMaxImpostorRecords != 32 || kMaxImpostorQuads != 4096) return 27;

  // The fog's identity with the frame's.
  FogBlock block{};
  block.valid = true;
  block.axis[0] = 0.25f; block.axis[1] = 0.5f; block.axis[2] = 0.75f;
  block.range[0] = 12.0f; block.range[1] = 1900.0f; block.range[2] = 0.05f; block.range[3] = 0.002f;
  const uint32_t base[2] = {0x1A100000u, 0x1A200000u};   // tf4, tf5
  ImpostorCall c = Good(3);
  std::memcpy(c.vs[kImpC110], block.axis, 12);
  c.vs[kImpC111][0] = 12.0f; c.vs[kImpC111][1] = 7.0f; c.vs[kImpC111][2] = 0.05f; c.vs[kImpC111][3] = 0.002f;   // (.y is not the fog's)
  c.fog_fetch[0][1] = base[1] | 0x1E;                    // slot 16 is tf5
  c.fog_fetch[1][1] = base[0] | 0x1E;                    // slot 17 is tf4
  r = MakeImpostorRecord(c, bytes.data(), bytes.size(), words, &why);
  if (why != ImpostorReason::kNone || !ImpostorFogMatches(r, block, base)) return 30;
  { ImpostorRecord o = r; o.fog_fetch[0][1] = base[0] | 0x1E; if (ImpostorFogMatches(o, block, base)) return 31; }   // the slots exchanged
  { ImpostorRecord o = r; o.vs[kImpC110][2] = 0.7500001f; if (ImpostorFogMatches(o, block, base)) return 32; }
  { ImpostorRecord o = r; o.vs[kImpC111][3] = 0.003f; if (ImpostorFogMatches(o, block, base)) return 33; }
  { ImpostorRecord o = r; o.vs[kImpC111][0] = 12.5f; if (ImpostorFogMatches(o, block, base)) return 33; }
  { ImpostorRecord o = r; o.vs[kImpC111][2] = 0.06f; if (ImpostorFogMatches(o, block, base)) return 33; }
  { ImpostorRecord o = r; o.vs[kImpC111][1] = 9.0f; if (!ImpostorFogMatches(o, block, base)) return 33; }             // c111.y is not compared
  { FogBlock off = block; off.valid = false; if (ImpostorFogMatches(r, off, base)) return 34; }                       // no block: nothing matches it
  { const uint32_t none[2] = {0, 0}; ImpostorRecord o = r; o.fog_fetch[0][1] = 0x1E; o.fog_fetch[1][1] = 0x1E;
    if (ImpostorFogMatches(o, block, none) || ImpostorFogMatches(r, block, nullptr)) return 34; }                     // no address is no table

  // The shaders' own literal tables (as the wind's: held once an object by the capture).
  uint32_t lit[8][4] = {};
  const uint32_t vs254[4] = {0x3F000000u, 0x3E800000u, 0x40000000u, 0u}, vs255[4] = {0u, 0x3F800000u, 0xBF800000u, 0x40800000u},
                 ps255[4] = {0u, 0x3F800000u, 0x3DCCCCCDu, 0x3F000000u};
  std::memcpy(lit[6], vs254, 16); std::memcpy(lit[7], vs255, 16);
  if (!ImpostorLiteralsMatch(0, lit, 0xFF000000u)) return 50;
  if (ImpostorLiteralsMatch(0, lit, 0xF0000000u) || ImpostorLiteralsMatch(0, lit, 0x7F000000u)) return 51;   // a register or one dword not held
  if (ImpostorLiteralsMatch(1, lit, 0xFF000000u)) return 52;                                                 // the vertex shader's are not the pixel shader's
  lit[6][1] ^= 1u;
  if (ImpostorLiteralsMatch(0, lit, 0xFF000000u)) return 53;                                                 // one bit
  std::memcpy(lit[7], ps255, 16);
  if (!ImpostorLiteralsMatch(1, lit, 0xF0000000u) || ImpostorLiteralsMatch(1, nullptr, 0xF0000000u) || ImpostorLiteralsMatch(2, lit, 0xFFFFFFFFu)) return 54;
  std::puts("PASS: impostor mode and reasons, the call's rules in their order, bounds on read components alone, the words whole, the fog's identity, literal tables");
  return 0;
}
