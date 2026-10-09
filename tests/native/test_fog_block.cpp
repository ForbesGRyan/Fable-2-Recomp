// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/fog_block.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>

using namespace fable2::native::capture;

#include "fog_table_fixture.inc"

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

// Frame 1 of capture native_discovery_20261009_092459 (frame-map section 16): the registers of its
// first fogged main-scene row (out/native-evidence/fog/task1_092459_tables.txt), the two tables'
// fetch constants, and the tables' bytes (fog_table_fixture.inc).
struct Inputs {
  float regs[6][4] = {{1.0f, 1.0f, 0.0f, 0.0f},                                // c28
                      {0.000305443653f, 0.000393790426f, 0.00107105088f, 0.0f},  // c64
                      {0.00525102764f, 0.00596226612f, 0.00983294938f, 0.0f},    // c65
                      {-0.82958281f, 0.0445664451f, 0.556602359f, 0.0f},         // c66
                      {19.8817368f, 1900.0f, 1.0f, 0.000500000024f},             // c67
                      {0.266644657f, 0.399933934f, 0.466666698f, 0.0f}};         // c68
  uint32_t fc4[6] = {0x00804956, 0x1FAB505D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018};
  uint32_t fc5[6] = {0x02004802, 0x1FAB4086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018};
  unsigned char tf4[sizeof(kFixtureTf4)];
  unsigned char tf5[sizeof(kFixtureTf5)];
  Inputs() {
    std::memcpy(tf4, kFixtureTf4, sizeof(tf4));
    std::memcpy(tf5, kFixtureTf5, sizeof(tf5));
  }
  FogBlock Make(FogBlockReason* why = nullptr) const {
    return MakeFogBlock(regs, fc4, fc5, tf4, sizeof(tf4), tf5, sizeof(tf5), why);
  }
};

// Every float of a block zero and the flag clear (the padding after the flag is not compared).
static bool AllZero(const FogBlock& b) {
  static const unsigned char zero[offsetof(FogBlock, valid)] = {};
  return !b.valid && std::memcmp(&b, zero, sizeof(zero)) == 0;
}
static bool AllZero(const SkyBlock& b) {
  static const unsigned char zero[offsetof(SkyBlock, valid)] = {};
  return !b.valid && std::memcmp(&b, zero, sizeof(zero)) == 0;
}
// The block of `in` is refused for `want`, and is all zero.
static bool Refused(const Inputs& in, FogBlockReason want) {
  FogBlockReason why = FogBlockReason::kNone;
  const FogBlock b = in.Make(&why);
  return AllZero(b) && why == want;
}
static bool Same3(const float a[3], float x, float y, float z) { return a[0] == x && a[1] == y && a[2] == z; }

static int ValidTests() {
  static_assert(sizeof(kFixtureTf4) == kFogScatterBytes && sizeof(kFixtureTf5) == kFogTransmitBytes);
  static_assert(kFogScatterBytes == 512 && kFogTransmitBytes == 1024);
  static_assert(sizeof(FogBlock) == 3916 && sizeof(SkyBlock) == 64, "the documents state the blocks' sizes");
  const Inputs in;
  FogBlockReason why = FogBlockReason::kUnread;
  const FogBlock b = in.Make(&why);
  if (!b.valid || why != FogBlockReason::kNone) return 1;
  // The registers, each where the interface says.
  if (b.amount[0] != 1.0f || b.amount[1] != 1.0f) return 2;
  if (!Same3(b.axis, -0.82958281f, 0.0445664451f, 0.556602359f)) return 3;
  if (b.range[0] != 19.8817368f || b.range[1] != 1900.0f || b.range[2] != 1.0f || b.range[3] != 0.000500000024f) return 4;
  if (!Same3(b.near_colour, 0.266644657f, 0.399933934f, 0.466666698f)) return 5;
  if (!Same3(b.ext[0], 0.000305443653f, 0.000393790426f, 0.00107105088f)) return 6;
  if (!Same3(b.ext[1], 0.00525102764f, 0.00596226612f, 0.00983294938f)) return 7;
  // tf4: half floats, high byte first, r g b a. The values are the capture's own report
  // (task1_092459_tables.txt: first (0.30811, 0.38403, 0.46533), middle (0.55615, 0.46191, 0.43579),
  // last (0.93311, 0.46655, 0.3064)), exact as half floats (texel 0 is 34EE 3625 3772 0000).
  if (!Same3(b.scatter[0], 0.30810546875f, 0.384033203125f, 0.46533203125f)) return 8;
  if (!Same3(b.scatter[1], 0.3134765625f, 0.38623046875f, 0.46484375f)) return 9;
  if (!Same3(b.scatter[32], 0.55615234375f, 0.4619140625f, 0.435791015625f)) return 10;
  if (!Same3(b.scatter[63], 0.93310546875f, 0.466552734375f, 0.306396484375f)) return 11;
  // tf5: stored a r g b. Texel 0 is FF FF FF FF, texel 1 FF FB FB F8, texel 128 FF 5C 4F 22,
  // texel 255 FF 26 1D 06 (the report: middle (0.36078, 0.3098, 0.13333), last (0.14902, 0.11373, 0.023529)).
  if (!Same3(b.transmit[0], 1.0f, 1.0f, 1.0f)) return 12;
  if (!Same3(b.transmit[1], 251.0f / 255.0f, 251.0f / 255.0f, 248.0f / 255.0f)) return 13;
  if (!Same3(b.transmit[128], 92.0f / 255.0f, 79.0f / 255.0f, 34.0f / 255.0f)) return 14;
  if (!Same3(b.transmit[255], 38.0f / 255.0f, 29.0f / 255.0f, 6.0f / 255.0f)) return 15;
  if (std::fabs(b.transmit[128][0] - 0.36078f) > 1e-5f || std::fabs(b.transmit[255][2] - 0.023529f) > 1e-6f) return 16;
  // The real table falls with distance in every channel and stays in 0..1; the in-scatter table is
  // finite and in 0..1 here (not a rule of the block: what this capture's bytes are in this reading).
  for (int i = 0; i < 256; ++i) {
    for (int c = 0; c < 3; ++c) {
      if (!(b.transmit[i][c] >= 0.0f && b.transmit[i][c] <= 1.0f)) return 17;
      if (i && b.transmit[i][c] > b.transmit[i - 1][c]) return 18;
    }
  }
  for (int i = 0; i < 64; ++i) {
    for (int c = 0; c < 3; ++c) {
      if (!(b.scatter[i][c] > 0.3f && b.scatter[i][c] < 0.96f)) return 19;
    }
  }
  // The interface's own form (no reason asked for) gives the same block.
  const FogBlock plain = MakeFogBlock(in.regs, in.fc4, in.fc5, in.tf4, sizeof(in.tf4), in.tf5, sizeof(in.tf5));
  if (!plain.valid || std::memcmp(&plain, &b, offsetof(FogBlock, valid)) != 0) return 20;
  // What is not pinned does not refuse: the tables rotate through three addresses, and a 1D
  // texture has one axis (the clamp of v and w addresses nothing).
  {
    Inputs o;
    o.fc4[1] = (o.fc4[1] & 0xFFFu) | 0x1FAB1000u;
    o.fc5[1] = (o.fc5[1] & 0xFFFu) | 0x1FAB2000u;
    o.fc4[0] &= ~((7u << 13) | (7u << 16));
    o.fc5[0] |= (1u << 13) | (1u << 16);
    if (!o.Make().valid) return 21;
  }
  // A table above 1 is a table (half floats; the block has no ceiling for it), and so is a negative one.
  {
    Inputs o;
    o.tf4[8 * 5 + 0] = 0x44;  // 4.0 = 0x4400
    o.tf4[8 * 5 + 1] = 0x00;
    o.tf4[8 * 6 + 2] = 0xBC;  // -1.0 = 0xBC00
    o.tf4[8 * 6 + 3] = 0x00;
    const FogBlock t = o.Make();
    if (!t.valid || t.scatter[5][0] != 4.0f || t.scatter[6][1] != -1.0f) return 22;
  }
  // The bounds of the rules themselves are inside.
  {
    Inputs o;
    o.regs[0][0] = 0.0f;
    o.regs[0][1] = 4.0f;
    o.regs[4][0] = 0.0f;
    o.regs[4][1] = 0.0f;
    o.regs[4][2] = 0.0f;
    o.regs[1][0] = 0.0f;
    if (!o.Make().valid) return 23;
    Inputs u;  // a unit axis within 1e-3
    for (int c = 0; c < 3; ++c) u.regs[3][c] *= 1.0009f;
    if (!u.Make().valid) return 24;
    for (int c = 0; c < 3; ++c) u.regs[3][c] = in.regs[3][c] * 0.9991f;
    if (!u.Make().valid) return 25;
  }
  return 0;
}

static int RegisterTests() {
  // Review Focus 2: a register that is not finite, in any component of any of the six.
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 4; ++c) {
      for (float bad : {kNaN, kInf, -kInf}) {
        Inputs in;
        in.regs[r][c] = bad;
        if (!Refused(in, FogBlockReason::kRegister)) return 30;
      }
    }
  }
  // c28.xy in [0, 4].
  for (int c = 0; c < 2; ++c) {
    for (float bad : {-0.01f, 4.01f, 1e30f}) {
      Inputs in;
      in.regs[0][c] = bad;
      if (!Refused(in, FogBlockReason::kAmount)) return 31;
    }
  }
  // c66: a unit vector within 1e-3. A zero axis (Review Focus 2), one too long, one too short.
  {
    Inputs in;
    in.regs[3][0] = in.regs[3][1] = in.regs[3][2] = 0.0f;
    if (!Refused(in, FogBlockReason::kAxis)) return 32;
    for (float scale : {1.002f, 0.998f, 2.0f, 1e20f}) {
      Inputs o;
      for (int c = 0; c < 3; ++c) o.regs[3][c] *= scale;
      if (!Refused(o, FogBlockReason::kAxis)) return 33;
    }
  }
  // c67: start >= 0, the sky's height >= 0, the near slope >= 0, the distance scale > 0.
  {
    Inputs in;
    in.regs[4][0] = -0.001f;
    if (!Refused(in, FogBlockReason::kRange)) return 34;
    in = Inputs();
    in.regs[4][1] = -1.0f;
    if (!Refused(in, FogBlockReason::kRange)) return 35;
    in = Inputs();
    in.regs[4][2] = -0.5f;
    if (!Refused(in, FogBlockReason::kRange)) return 36;
    for (float bad : {0.0f, -0.0005f}) {
      in = Inputs();
      in.regs[4][3] = bad;
      if (!Refused(in, FogBlockReason::kRange)) return 37;
    }
  }
  // c64, c65: an extinction is not negative (the sky's exp(-(l1 c64 + l2 c65)) would grow).
  for (int r = 1; r <= 2; ++r) {
    for (int c = 0; c < 3; ++c) {
      Inputs in;
      in.regs[r][c] = -1e-6f;
      if (!Refused(in, FogBlockReason::kExtinction)) return 38;
    }
  }
  // Null inputs: nothing was read.
  {
    const Inputs in;
    FogBlockReason why = FogBlockReason::kNone;
    if (!AllZero(MakeFogBlock(nullptr, in.fc4, in.fc5, in.tf4, 512, in.tf5, 1024, &why)) || why != FogBlockReason::kUnread) return 39;
    if (!AllZero(MakeFogBlock(in.regs, nullptr, in.fc5, in.tf4, 512, in.tf5, 1024, &why)) || why != FogBlockReason::kUnread) return 40;
    if (!AllZero(MakeFogBlock(in.regs, in.fc4, nullptr, in.tf4, 512, in.tf5, 1024, &why)) || why != FogBlockReason::kUnread) return 41;
    if (!MakeFogBlock(in.regs, in.fc4, in.fc5, in.tf4, 512, in.tf5, 1024, nullptr).valid) return 42;  // (no reason asked for)
  }
  return 0;
}

// One pinned property of a table's fetch constant, changed: `scatter` the in-scatter table's (tf4),
// else the transmittance table's (tf5).
static bool TableRefused(bool scatter, void (*change)(uint32_t fc[6]), FogTableFault fault) {
  Inputs in;
  change(scatter ? in.fc4 : in.fc5);
  return Refused(in, FogTableReason(scatter ? 0 : 1, fault)) &&
         FogTableFetchFault(scatter ? in.fc4 : in.fc5, scatter ? kFogScatterPin : kFogTransmitPin) == fault;
}

static int FetchTests() {
  const Inputs good;
  if (FogTableFetchFault(good.fc4, kFogScatterPin) != FogTableFault::kNone) return 50;
  if (FogTableFetchFault(good.fc5, kFogTransmitPin) != FogTableFault::kNone) return 51;
  // Each table's constant against the other's pin: the first difference in the order of the rules
  // is the width (both are 1D and have an address).
  if (FogTableFetchFault(good.fc4, kFogTransmitPin) != FogTableFault::kWidth) return 52;
  if (FogTableFetchFault(good.fc5, kFogScatterPin) != FogTableFault::kWidth) return 53;
  if (FogTableFetchFault(nullptr, kFogScatterPin) != FogTableFault::kDimension) return 54;
  for (bool scatter : {true, false}) {
    // Not a texture constant; a 2D, a 3D and a cube texture.
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] &= ~3u; }, FogTableFault::kDimension)) return 55;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[5] |= 1u << 9; }, FogTableFault::kDimension)) return 56;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[5] |= 2u << 9; }, FogTableFault::kDimension)) return 57;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[5] |= 3u << 9; }, FogTableFault::kDimension)) return 58;
    // No address.
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[1] &= 0xFFFu; }, FogTableFault::kBase)) return 59;
    // Another width: one texel less, twice as many, and a width only the upper bits of the
    // 24-bit field tell from the right one.
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[2] -= 1; }, FogTableFault::kWidth)) return 60;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[2] = fc[2] * 2 + 1; }, FogTableFault::kWidth)) return 61;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[2] |= 1u << 20; }, FogTableFault::kWidth)) return 62;
    // Another format (k_16_16_16_16_FLOAT, k_8).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[1] = (fc[1] & ~0x3Fu) | 32; }, FogTableFault::kFormat)) return 63;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[1] = (fc[1] & ~0x3Fu) | 2; }, FogTableFault::kFormat)) return 64;
    // Another endian (none; 16-in-32).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[1] &= ~(3u << 6); }, FogTableFault::kEndian)) return 65;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[1] |= 3u << 6; }, FogTableFault::kEndian)) return 66;
    // Another swizzle (one component's source changed).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] ^= 1u << 1; }, FogTableFault::kSwizzle)) return 67;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] ^= 4u << 10; }, FogTableFault::kSwizzle)) return 68;
    // Other sign fields (one component's).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] ^= 1u << 2; }, FogTableFault::kSign)) return 69;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] ^= 2u << 8; }, FogTableFault::kSign)) return 70;
    // An exponent adjust (+1, -1).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] |= 1u << 13; }, FogTableFault::kExpAdjust)) return 71;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] |= 0x3Fu << 13; }, FogTableFault::kExpAdjust)) return 72;
    // (an even adjust and the field's top bit alone: +2, -32)
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] |= 2u << 13; }, FogTableFault::kExpAdjust)) return 80;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] |= 0x20u << 13; }, FogTableFault::kExpAdjust)) return 81;
    // Not clamped to the edge along the table (wrap, mirror, clamp to border).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] &= ~(7u << 10); }, FogTableFault::kClamp)) return 73;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] = (fc[0] & ~(7u << 10)) | (1u << 10); }, FogTableFault::kClamp)) return 74;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[0] = (fc[0] & ~(7u << 10)) | (6u << 10); }, FogTableFault::kClamp)) return 75;
    // Not linear: the magnification filter, the minification filter (point each).
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] &= ~(3u << 19); }, FogTableFault::kFilter)) return 76;
    if (!TableRefused(scatter, [](uint32_t fc[6]) { fc[3] &= ~(3u << 21); }, FogTableFault::kFilter)) return 77;
  }
  // The two tables exchanged: each constant fails the other's pin.
  {
    Inputs in;
    std::memcpy(in.fc4, good.fc5, sizeof(in.fc4));
    if (!Refused(in, FogBlockReason::kScatterWidth)) return 78;
    in = Inputs();
    std::memcpy(in.fc5, good.fc4, sizeof(in.fc5));
    if (!Refused(in, FogBlockReason::kTransmitWidth)) return 79;
  }
  return 0;
}

static int BytesTests() {
  const Inputs in;
  FogBlockReason why = FogBlockReason::kNone;
  // The bytes: exactly 512 and 1,024, and there.
  for (size_t n : {size_t(0), size_t(511), size_t(513), size_t(1024)}) {
    if (!AllZero(MakeFogBlock(in.regs, in.fc4, in.fc5, in.tf4, n, in.tf5, 1024, &why)) || why != FogBlockReason::kScatterBytes) return 90;
  }
  for (size_t n : {size_t(0), size_t(1023), size_t(1025), size_t(512)}) {
    if (!AllZero(MakeFogBlock(in.regs, in.fc4, in.fc5, in.tf4, 512, in.tf5, n, &why)) || why != FogBlockReason::kTransmitBytes) return 91;
  }
  if (!AllZero(MakeFogBlock(in.regs, in.fc4, in.fc5, nullptr, 512, in.tf5, 1024, &why)) || why != FogBlockReason::kScatterBytes) return 92;
  if (!AllZero(MakeFogBlock(in.regs, in.fc4, in.fc5, in.tf4, 512, nullptr, 1024, &why)) || why != FogBlockReason::kTransmitBytes) return 93;
  // Review Focus 2: a table texel of infinity, or one that is no number, in any channel of any
  // texel (the alpha too: the texel is the table's).
  const unsigned char bad[3][2] = {{0x7C, 0x00}, {0xFC, 0x00}, {0x7E, 0x00}};  // +inf, -inf, NaN as half floats
  for (int texel : {0, 1, 31, 63}) {
    for (int c = 0; c < 4; ++c) {
      for (const auto& half : bad) {
        Inputs o;
        o.tf4[8 * texel + 2 * c] = half[0];
        o.tf4[8 * texel + 2 * c + 1] = half[1];
        if (!Refused(o, FogBlockReason::kScatterTexel)) return 94;
      }
    }
  }
  // The largest half float is a number.
  {
    Inputs o;
    o.tf4[8 * 63 + 4] = 0x7B;
    o.tf4[8 * 63 + 5] = 0xFF;
    const FogBlock b = o.Make();
    if (!b.valid || b.scatter[63][2] != 65504.0f) return 95;
  }
  // The byte order and the channel order, on hand-made bytes: tf4 texel 2 = (1, 2, 0.5, 0) as
  // 3C00 4000 3800 0000; tf5 texel 3 stored a r g b = 00 FF 80 01.
  {
    Inputs o;
    const unsigned char t4[8] = {0x3C, 0x00, 0x40, 0x00, 0x38, 0x00, 0x00, 0x00};
    const unsigned char t5[4] = {0x00, 0xFF, 0x80, 0x01};
    std::memcpy(o.tf4 + 16, t4, sizeof(t4));
    std::memcpy(o.tf5 + 12, t5, sizeof(t5));
    const FogBlock b = o.Make();
    if (!b.valid || !Same3(b.scatter[2], 1.0f, 2.0f, 0.5f)) return 96;
    if (!Same3(b.transmit[3], 1.0f, 128.0f / 255.0f, 1.0f / 255.0f)) return 97;
  }
  return 0;
}

static int SkyTests() {
  // The sky row of the same frame (task1_092459_sky.txt): c31 = (150, 0, 0, 0), c46 = (100, 0, 0, 0),
  // c47 = (0, 0, 0, 0); tf13 and tf14 one 16 x 16 k_8_8_8_8 texture at 0x1A544000.
  const float c31[4] = {150.0f, 0.0f, 0.0f, 0.0f}, c46[4] = {100.0f, 0.0f, 0.0f, 0.0f}, c47[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const uint32_t fc13[6] = {0x00800002, 0x1A544086, 0x0001E00F, 0x00A80C14, 0x00000003, 0x00000218};
  uint32_t fc14[6];
  std::memcpy(fc14, fc13, sizeof(fc14));
  fc14[1] = 0x1A545086;  // (another address, so that the two are told apart)
  SkyBlockReason why = SkyBlockReason::kUnread;
  const SkyBlock b = MakeSkyBlock(c31, c46, c47, fc13, fc14, &why);
  if (!b.valid || why != SkyBlockReason::kNone) return 110;
  if (b.c31x != 150.0f || b.c46x != 100.0f || b.c47x != 0.0f) return 111;
  if (std::memcmp(b.fetch[0], fc13, sizeof(fc13)) != 0 || std::memcmp(b.fetch[1], fc14, sizeof(fc14)) != 0) return 112;
  const SkyBlock plain = MakeSkyBlock(c31, c46, c47, fc13, fc14);
  if (!plain.valid || std::memcmp(&plain, &b, offsetof(SkyBlock, valid)) != 0) return 113;
  // Only the x of each register is the block's: what the other components hold decides nothing.
  {
    const float a[4] = {150.0f, kNaN, kInf, -kInf}, c[4] = {1.0f, kNaN, -5.0f, kInf};
    const SkyBlock s = MakeSkyBlock(a, a, c, fc13, fc14, &why);
    if (!s.valid || s.c31x != 150.0f || s.c46x != 150.0f || s.c47x != 1.0f) return 114;
  }
  // The texture constants are carried as they are, whatever they hold (a texture the cache cannot
  // decode is the sky pass's "sky-texture", not the block's).
  {
    const uint32_t none[6] = {};
    if (!MakeSkyBlock(c31, c46, c47, none, none, &why).valid) return 115;
  }
  for (float bad : {kNaN, kInf, -kInf}) {
    const float x[4] = {bad, 0.0f, 0.0f, 0.0f};
    if (!AllZero(MakeSkyBlock(x, c46, c47, fc13, fc14, &why)) || why != SkyBlockReason::kRegister) return 116;
    if (!AllZero(MakeSkyBlock(c31, x, c47, fc13, fc14, &why)) || why != SkyBlockReason::kRegister) return 117;
    if (!AllZero(MakeSkyBlock(c31, c46, x, fc13, fc14, &why)) || why != SkyBlockReason::kRegister) return 118;
  }
  // A path length is not negative; the blend of the two textures is in [0, 1].
  {
    const float neg[4] = {-1.0f, 0.0f, 0.0f, 0.0f}, over[4] = {1.001f, 0.0f, 0.0f, 0.0f}, zero[4] = {};
    if (!AllZero(MakeSkyBlock(neg, c46, c47, fc13, fc14, &why)) || why != SkyBlockReason::kRange) return 119;
    if (!AllZero(MakeSkyBlock(c31, neg, c47, fc13, fc14, &why)) || why != SkyBlockReason::kRange) return 120;
    if (!AllZero(MakeSkyBlock(c31, c46, neg, fc13, fc14, &why)) || why != SkyBlockReason::kRange) return 121;
    if (!AllZero(MakeSkyBlock(c31, c46, over, fc13, fc14, &why)) || why != SkyBlockReason::kRange) return 122;
    if (!MakeSkyBlock(zero, zero, zero, fc13, fc14, &why).valid) return 123;
  }
  if (!AllZero(MakeSkyBlock(nullptr, c46, c47, fc13, fc14, &why)) || why != SkyBlockReason::kUnread) return 124;
  if (!AllZero(MakeSkyBlock(c31, c46, c47, nullptr, fc14, &why)) || why != SkyBlockReason::kUnread) return 125;
  if (!AllZero(MakeSkyBlock(c31, c46, c47, fc13, nullptr, &why)) || why != SkyBlockReason::kUnread) return 126;
  if (!AllZero(MakeSkyBlock(c31, c46, c47, fc13, nullptr))) return 127;
  return 0;
}

static int NameAndTableTests() {
  // Every reason has a name of its own.
  std::set<std::string> names;
  for (size_t i = 0; i < size_t(FogBlockReason::kCount); ++i) {
    const std::string name = FogBlockReasonName(FogBlockReason(i));
    if (name.empty() || name == "?" || !names.insert(name).second) return 140;
  }
  if (std::string(FogBlockReasonName(FogBlockReason::kCount)) != "?") return 141;
  if (std::string(FogBlockReasonName(FogBlockReason::kScatterSwizzle)) != "tf4-swizzle") return 142;
  if (std::string(FogBlockReasonName(FogBlockReason::kTransmitFilter)) != "tf5-filter") return 143;
  if (std::string(FogBlockReasonName(FogBlockReason::kScatterTexel)) != "tf4-texel") return 144;
  if (FogTableReason(0, FogTableFault::kNone) != FogBlockReason::kNone || FogTableReason(1, FogTableFault::kNone) != FogBlockReason::kNone) return 145;
  if (FogTableReason(0, FogTableFault::kDimension) != FogBlockReason::kScatterDimension) return 146;
  if (FogTableReason(1, FogTableFault::kBytes) != FogBlockReason::kTransmitBytes) return 147;
  names.clear();
  for (size_t i = 0; i < size_t(SkyBlockReason::kCount); ++i) {
    const std::string name = SkyBlockReasonName(SkyBlockReason(i));
    if (name.empty() || name == "?" || !names.insert(name).second) return 148;
  }
  if (std::string(kFogBlockReason) != "fog-block" || std::string(kSkyBlockReason) != "sky-block") return 149;
  // The registers and slots, as the formulas name them (lighting-formulas.md, "Fog and sky").
  static_assert(kFogBlockRegisters[0] == 28 && kFogBlockRegisters[1] == 64 && kFogBlockRegisters[2] == 65 &&
                kFogBlockRegisters[3] == 66 && kFogBlockRegisters[4] == 67 && kFogBlockRegisters[5] == 68);
  static_assert(kSkyBlockRegisters[0] == 31 && kSkyBlockRegisters[1] == 46 && kSkyBlockRegisters[2] == 47);
  static_assert(kFogTableSlots[0] == 4 && kFogTableSlots[1] == 5 && kSkyTextureSlots[0] == 13 && kSkyTextureSlots[1] == 14);
  static_assert(kSkyPixelShader == 0xF7601FBE485936FDull);
  // The generated table of the fogged pixel shaders (light_formulas.FOG_SHADERS): 40, the water
  // that is each captured frame's first fogged draw among them; the lake (not of the form), the
  // sky and the tree pixel shaders (which read no fog register) are not in it.
  static_assert(sizeof(kFogShaderTable) / sizeof(kFogShaderTable[0]) == 40);
  static_assert(FogShaderTableIsWellFormed());
  const FogShaderEntry* water = FindFogShader(0xF6D98C7B4D98438Bull);
  if (!water || water->world != 0 || water->alpha) return 150;
  const FogShaderEntry* far_ground = FindFogShader(0x7DDAD5C29B3F56EEull);
  if (!far_ground || far_ground->world != 1 || far_ground->alpha) return 151;
  const FogShaderEntry* blended = FindFogShader(0x401A01FD4E5F8757ull);
  if (!blended || blended->world != 2 || !blended->alpha) return 152;
  const FogShaderEntry* layer = FindFogShader(0x9C2144E42E95A76Full);
  if (!layer || layer->world != 4) return 153;
  if (!FindFogShader(0x014F8A02DB7B19CAull) || !FindFogShader(0xFC0922A3A8D8DA7Aull)) return 154;  // the first and the last
  if (FindFogShader(0x2B091BD0C45A7454ull) || FindFogShader(kSkyPixelShader) || FindFogShader(0) ||
      FindFogShader(0x00E0000000000000ull)) {
    return 155;
  }
  return 0;
}

int main() {
  for (int (*test)() : {ValidTests, RegisterTests, FetchTests, BytesTests, SkyTests, NameAndTableTests}) {
    if (const int code = test()) {
      std::printf("FAIL: fog block case %d\n", code);
      return 1;
    }
  }
  std::puts("PASS: fog block from a captured frame's bytes, its register, fetch constant and texel rules, the sky block, the fogged shader table");
  return 0;
}
