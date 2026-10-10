// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/post_block.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>

using namespace fable2::native::capture;

#include "post_table_fixture.inc"

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

// A float from its bits, as the capture's rows print a register ("pfull").
static float Bits(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
static float Up(float v) { return std::nextafter(v, kInf); }
static float Down(float v) { return std::nextafter(v, -kInf); }

// Frame 1 of capture native_discovery_20261009_205049 (plan Task 1's run 1; frame-map section 17):
// the registers of its composite's, bright pass's and blurs' rows ("pfull", by their bits), the
// table's fetch constant and the table's bytes (post_table_fixture.inc). The frame's c20.w is
// 8.45011711 (bits 410733AE).
struct Inputs {
  PostRegisters regs{};
  uint32_t fc[6] = {0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018};
  unsigned char table[sizeof(kFixturePostTable)];
  Inputs() {
    const uint32_t composite[5][2] = {{0x403E4612, 0}, {0x3E800000, 0}, {0x42480000, 0}, {0xBBAA4E61, 0x3F8D2176}, {0x3EE8BA2E, 0}};
    const uint32_t bright[4][2] = {{0x3FBE4612, 0}, {0x3E4CCCCE, 0}, {0x40000000, 0}, {0x3DB22120, 0x3DF25D0F}};
    const uint32_t blur[2][2][4] = {{{0x3E545A2D, 0x3E398C6D, 0x3DF790AC, 0x3D7C2FE0}, {0x3CC422CE, 0x3BE8EDC3, 0x3AD332BF, 0}},
                                    {{0x3E3A39E9, 0x3E27F2C5, 0x3DF661FC, 0x3D92FD2B}, {0x3D0EA54D, 0x3C612E70, 0x3B908F2F, 0}}};
    for (int r = 0; r < 5; ++r) {
      for (int c = 0; c < 2; ++c) regs.composite[r][c] = Bits(composite[r][c]);
    }
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 2; ++c) regs.bright[r][c] = Bits(bright[r][c]);
    }
    for (int s = 0; s < 2; ++s) {
      for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) regs.blur[s][r][c] = Bits(blur[s][r][c]);
      }
    }
    regs.have_composite = regs.have_bright = regs.have_blur[0] = regs.have_blur[1] = true;
    std::memcpy(table, kFixturePostTable, sizeof(table));
  }
  PostBlock Make(PostBlockReason* why = nullptr) const { return MakePostBlock(regs, fc, table, sizeof(table), why); }
  // Texel `i` of the table set to `v` (stored high byte first).
  void Texel(int i, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int k = 0; k < 4; ++k) table[4 * i + k] = static_cast<unsigned char>(bits >> (24 - 8 * k));
  }
};
static constexpr float kFrameScale = 8.45011711f;

// Every float of a block zero, no part and the flag clear (the padding after the flag is not compared).
static bool AllZero(const PostBlock& b) {
  static const unsigned char zero[offsetof(PostBlock, parts)] = {};
  return !b.valid && b.parts == 0 && std::memcmp(&b, zero, sizeof(zero)) == 0;
}
// The block of `in` is refused for `want`, and is all zero.
static bool Refused(const Inputs& in, PostBlockReason want) {
  PostBlockReason why = PostBlockReason::kNone;
  const PostBlock b = in.Make(&why);
  return AllZero(b) && why == want;
}
// The components the block takes, by (row, component) in PostRegisters.
static const int kCompositeTaken[6][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {4, 0}};
static const int kBrightTaken[5][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}};

static int ValidTests() {
  static_assert(sizeof(kFixturePostTable) == kPostTableBytes && kPostTableBytes == 2048 && kPostTableTexels == 512);
  static_assert(sizeof(PostBlock) == 2168, "the documents state the block's size");
  static_assert(kPostTableSlot == 1 && kPostBlockMaxAge == 4 && kPostPartBlur == 1 && kPostPartRadial == 2);
  static_assert(kPostCompositePs == 0x37B186F4BD3288F7ull && kPostBrightPs == 0xEE922CFB4426EA83ull &&
                kPostBlurXPs == 0xEA83C7E7E24EC4C8ull && kPostBlurYPs == 0x28DBE18F313350BFull &&
                kPostRadialPs == 0x4BE4344C27016E04ull);
  static_assert(sizeof(PostSource) == 16);
  const Inputs in;
  PostBlockReason why = PostBlockReason::kUnread;
  const PostBlock b = in.Make(&why);
  if (!b.valid || why != PostBlockReason::kNone) return 1;
  // The registers, each where the interface says (the capture's own report of the frame,
  // out/native-evidence/post/t1_run1_table.txt: 2.9730, 0.25, 50, (-0.00520, 1.10258), 0.45455;
  // 1.4865, 0.2, 2, (0.08698, 0.11834)).
  const float composite[6] = {2.97302675f, 0.25f, 50.0f, -0.00519733177f, 1.10258365f, 0.454545438f};
  const float bright[5] = {1.48651338f, 0.200000018f, 2.0f, 0.0869772434f, 0.118341558f};
  if (std::memcmp(b.composite, composite, sizeof(composite)) != 0) return 2;
  if (std::memcmp(b.bright, bright, sizeof(bright)) != 0) return 3;
  // The blurs: the centre's weight, then the six on either side; EA83's, then 28DB's.
  const float blur[2][7] = {{0.207375243f, 0.181199744f, 0.120881408f, 0.0615690947f, 0.0239423774f, 0.00710842153f, 0.0016113146f},
                            {0.181861535f, 0.16401203f, 0.120304078f, 0.0717719421f, 0.0348256119f, 0.0137439817f, 0.00441160006f}};
  if (b.parts != kPostPartBlur || std::memcmp(b.blur, blur, sizeof(blur)) != 0) return 4;
  for (float v : b.radial) {
    if (v != 0.0f) return 5;  // (Task 12's)
  }
  // The table: one float a texel, high byte first. Texel 0 is 00000000, texel 1 3DC490B4.
  if (b.table[0] != 0.0f || b.table[1] != Bits(0x3DC490B4) || b.table[1] != 0.0959791243f) return 6;
  if (b.table[128] != 0.242364183f || b.table[511] != 0.32034272f) return 7;
  // This frame's table rises from 0 and stays below 0.33 (what the capture's bytes are in this
  // reading, not a rule of the block), with its largest texel at 510.
  float largest = 0.0f;
  int at = 0;
  for (int i = 0; i < 512; ++i) {
    if (!(b.table[i] >= 0.0f && b.table[i] < 0.33f)) return 8;
    if (b.table[i] > largest) {
      largest = b.table[i];
      at = i;
    }
  }
  if (largest != 0.320970237f || at != 510) return 9;
  // The frame's own tie: the bright pass's c77.y is 1 / c20.w.
  if (!PostBlockPairs(b, kFrameScale) || std::fabs(double(b.bright[4]) * kFrameScale - 1.0) > 1e-7) return 10;
  // The interface's other form (no reason asked for) gives the same block.
  const PostBlock plain = MakePostBlock(in.regs, in.fc, in.table, sizeof(in.table));
  if (!plain.valid || std::memcmp(&plain, &b, offsetof(PostBlock, valid)) != 0) return 11;
  // What is not pinned does not refuse: the table's address, the low bits beside it that are not
  // format or endian (none in this constant), and the clamps of the two axes a 1D texture has not.
  {
    Inputs o;
    o.fc[1] = (o.fc[1] & 0xFFFu) | 0x1FAB3000u;
    o.fc[0] |= (1u << 13) | (1u << 16);
    if (!o.Make().valid) return 12;
  }
  // The components the block does not take are not judged.
  {
    Inputs o;
    for (int r = 0; r < 5; ++r) {
      for (int c = 0; c < 4; ++c) {
        bool taken = false;
        for (const int* t : kCompositeTaken) taken = taken || (t[0] == r && t[1] == c);
        if (!taken) o.regs.composite[r][c] = c & 1 ? kNaN : -kInf;
      }
    }
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        bool taken = false;
        for (const int* t : kBrightTaken) taken = taken || (t[0] == r && t[1] == c);
        if (!taken) o.regs.bright[r][c] = kNaN;
      }
    }
    o.regs.blur[0][1][3] = kNaN;  // c47.w of a blur: no tap's weight
    o.regs.blur[1][1][3] = 1e30f;
    const PostBlock t = o.Make();
    if (!t.valid || t.parts != kPostPartBlur || std::memcmp(&t, &b, offsetof(PostBlock, valid)) != 0) return 13;
  }
  // The bounds of the rules themselves are inside.
  {
    const float lo_c[6] = {0.0f, std::numeric_limits<float>::denorm_min(), 0.0f, -4.0f, std::numeric_limits<float>::denorm_min(),
                           std::numeric_limits<float>::denorm_min()};
    const float hi_c[6] = {64.0f, 64.0f, 1e6f, 4.0f, 64.0f, 1.0f};
    const float lo_b[5] = {0.0f, 0.0f, 1e-3f, -4.0f, std::numeric_limits<float>::denorm_min()};
    const float hi_b[5] = {64.0f, 64.0f, 64.0f, 4.0f, 64.0f};
    for (int i = 0; i < 6; ++i) {
      for (const float* set : {lo_c, hi_c}) {
        Inputs o;
        o.regs.composite[kCompositeTaken[i][0]][kCompositeTaken[i][1]] = set[i];
        const PostBlock t = o.Make();
        if (!t.valid || t.composite[i] != set[i]) return 20 + i;
      }
    }
    for (int i = 0; i < 5; ++i) {
      for (const float* set : {lo_b, hi_b}) {
        Inputs o;
        o.regs.bright[kBrightTaken[i][0]][kBrightTaken[i][1]] = set[i];
        const PostBlock t = o.Make();
        if (!t.valid || t.bright[i] != set[i]) return 30 + i;
      }
    }
    Inputs o;  // a texel just below 4, and the blind run's flat table's kind of value
    o.Texel(0, Down(4.0f));
    o.Texel(511, 0.0f);
    const PostBlock t = o.Make();
    if (!t.valid || t.table[0] != Down(4.0f) || t.table[511] != 0.0f) return 36;
  }
  return 0;
}

static int RegisterTests() {
  // Not read: the composite's registers, the bright pass's, the fetch constant; before every other rule.
  {
    Inputs o;
    o.regs.have_composite = false;
    if (!Refused(o, PostBlockReason::kUnread)) return 40;
    Inputs p;
    p.regs.have_bright = false;
    p.regs.composite[0][0] = kNaN;  // (unread comes first)
    if (!Refused(p, PostBlockReason::kUnread)) return 41;
    const Inputs q;
    PostBlockReason why = PostBlockReason::kNone;
    if (!AllZero(MakePostBlock(q.regs, nullptr, q.table, sizeof(q.table), &why)) || why != PostBlockReason::kUnread) return 42;
    if (!AllZero(MakePostBlock(q.regs, nullptr, q.table, sizeof(q.table)))) return 43;
  }
  // A component the block takes that is no number (Review Focus 2), before the ranges and the table.
  for (float bad : {kNaN, kInf, -kInf}) {
    for (int i = 0; i < 6; ++i) {
      Inputs o;
      o.regs.composite[kCompositeTaken[i][0]][kCompositeTaken[i][1]] = bad;
      o.regs.bright[2][0] = 0.0f;  // (out of range as well: the register rule comes first)
      o.fc[2] = 0xFF;
      if (!Refused(o, PostBlockReason::kRegister)) return 50 + i;
    }
    for (int i = 0; i < 5; ++i) {
      Inputs o;
      o.regs.bright[kBrightTaken[i][0]][kBrightTaken[i][1]] = bad;
      o.regs.composite[1][0] = 0.0f;
      if (!Refused(o, PostBlockReason::kRegister)) return 60 + i;
    }
  }
  // The ranges, each just outside; before the table's rules.
  {
    // composite: c31.x [0, 64], c46.x (0, 64], c47.x [0, 1e6], |c77.x| <= 4, c77.y (0, 64], c78.x (0, 1]
    const float below_c[6] = {Down(0.0f), 0.0f, Down(0.0f), Down(-4.0f), 0.0f, 0.0f};
    const float above_c[6] = {Up(64.0f), Up(64.0f), Up(1e6f), Up(4.0f), Up(64.0f), Up(1.0f)};
    // bright: c31.x [0, 64], c46.x [0, 64], c47.x [1e-3, 64], |c77.x| <= 4, c77.y (0, 64]
    const float below_b[5] = {Down(0.0f), Down(0.0f), Down(1e-3f), Down(-4.0f), 0.0f};
    const float above_b[5] = {Up(64.0f), Up(64.0f), Up(64.0f), Up(4.0f), Up(64.0f)};
    for (int i = 0; i < 6; ++i) {
      for (const float* set : {below_c, above_c}) {
        Inputs o;
        o.regs.composite[kCompositeTaken[i][0]][kCompositeTaken[i][1]] = set[i];
        o.fc[2] = 0xFF;  // (a wrong width as well: the range comes first)
        if (!Refused(o, PostBlockReason::kRange)) return 70 + i;
      }
    }
    for (int i = 0; i < 5; ++i) {
      for (const float* set : {below_b, above_b}) {
        Inputs o;
        o.regs.bright[kBrightTaken[i][0]][kBrightTaken[i][1]] = set[i];
        if (!Refused(o, PostBlockReason::kRange)) return 80 + i;
      }
    }
    // The values a rule exists for: a negative gamma or table scale, a divisor of zero.
    Inputs o;
    o.regs.composite[4][0] = -0.4545f;
    if (!Refused(o, PostBlockReason::kRange)) return 86;
    Inputs p;
    p.regs.bright[2][0] = 0.0f;
    if (!Refused(p, PostBlockReason::kRange)) return 87;
    Inputs q;
    q.regs.composite[1][0] = -0.25f;
    if (!Refused(q, PostBlockReason::kRange)) return 88;
  }
  return 0;
}

static int FetchTests() {
  struct Case {
    PostBlockReason want;
    int dword;
    uint32_t clear, set;
  };
  const Case cases[] = {
      {PostBlockReason::kTableDimension, 0, 3u, 0u},                 // not a texture constant
      {PostBlockReason::kTableDimension, 0, 3u, 3u},
      {PostBlockReason::kTableDimension, 5, 3u << 9, 1u << 9},       // a 2D texture
      {PostBlockReason::kTableDimension, 5, 3u << 9, 3u << 9},       // a cube
      {PostBlockReason::kTableBase, 1, 0xFFFFF000u, 0u},             // no address
      {PostBlockReason::kTableWidth, 2, 0xFFFFFFu, 0x1FEu},          // 511 texels
      {PostBlockReason::kTableWidth, 2, 0xFFFFFFu, 0x200u},          // 513
      {PostBlockReason::kTableWidth, 2, 0xFFFFFFu, 0xFFu},           // 256: the fog's transmittance table's
      {PostBlockReason::kTableFormat, 1, 0x3Fu, 35u},                // another format
      {PostBlockReason::kTableFormat, 1, 0x3Fu, 6u},                 // k_8_8_8_8
      {PostBlockReason::kTableFormat, 3, 1u, 0u},                    // the number format bit
      {PostBlockReason::kTableEndian, 1, 3u << 6, 0u},               // no swap
      {PostBlockReason::kTableEndian, 1, 3u << 6, 1u << 6},          // 8-in-16
      {PostBlockReason::kTableEndian, 1, 3u << 6, 3u << 6},          // 16-in-32
      {PostBlockReason::kTableSwizzle, 3, 0xFFFu << 1, 0x688u << 1},  // r g b a as stored
      {PostBlockReason::kTableSign, 0, 0xFFu << 2, 0u},              // unsigned fields
      {PostBlockReason::kTableSign, 0, 3u << 2, 2u << 2},            // one field another
      {PostBlockReason::kTableExpAdjust, 3, 0x3Fu << 13, 1u << 13},
      {PostBlockReason::kTableExpAdjust, 3, 0x3Fu << 13, 0x3Fu << 13},
      {PostBlockReason::kTableClamp, 0, 7u << 10, 0u},               // wrap
      {PostBlockReason::kTableClamp, 0, 7u << 10, 1u << 10},         // mirror
      {PostBlockReason::kTableFilter, 3, 3u << 19, 1u << 19},        // magnification linear: the fog tables' filter
      {PostBlockReason::kTableFilter, 3, 3u << 21, 1u << 21},        // minification linear
      {PostBlockReason::kTableFilter, 3, 0xFu << 19, 0x5u << 19},    // both
      {PostBlockReason::kTableMip, 4, 0xFu << 6, 1u << 6},           // a second level
  };
  int n = 0;
  for (const Case& c : cases) {
    Inputs o;
    o.fc[c.dword] = (o.fc[c.dword] & ~c.clear) | c.set;
    if (!Refused(o, c.want)) return 100 + n;
    // The same without bytes: the constant is judged before any byte is looked at (the capture's first call).
    PostBlockReason why = PostBlockReason::kNone;
    if (!AllZero(MakePostBlock(o.regs, o.fc, nullptr, 0, &why)) || why != c.want) return 100 + n;
    ++n;
  }
  // The fog's order among the faults: the dimension before the address, the width before the format.
  {
    Inputs o;
    o.fc[5] |= 1u << 9;
    o.fc[1] &= 0xFFFu;
    if (!Refused(o, PostBlockReason::kTableDimension)) return 130;
    Inputs p;
    p.fc[2] = 0xFF;
    p.fc[1] = (p.fc[1] & ~0x3Fu) | 6u;
    if (!Refused(p, PostBlockReason::kTableWidth)) return 131;
    Inputs q;  // the filter before the mip level
    q.fc[3] |= 1u << 19;
    q.fc[4] |= 1u << 6;
    if (!Refused(q, PostBlockReason::kTableFilter)) return 132;
  }
  // The fog tables' own constants are not this table.
  {
    const uint32_t tf4[6] = {0x00804956, 0x1FAB505D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018};
    const uint32_t tf5[6] = {0x02004802, 0x1FAB4086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018};
    if (PostTableFetchFault(tf4) != FogTableFault::kWidth || PostTableFetchFault(tf5) != FogTableFault::kWidth) return 133;
    if (PostTableFetchFault(nullptr) != FogTableFault::kDimension) return 134;
    const Inputs in;
    if (PostTableFetchFault(in.fc) != FogTableFault::kNone) return 135;
    if (FogTableFetchFault(in.fc, kPostTablePin) != FogTableFault::kFilter) return 136;  // (the one rule that differs)
  }
  return 0;
}

static int BytesTests() {
  const Inputs in;
  // The bytes: there, and 2,048 of them. A pin's constant without bytes is "table-bytes": what the
  // capture's first call sees before it follows the address.
  for (size_t n : {size_t(0), size_t(2047), size_t(2049), size_t(1024), size_t(4096)}) {
    PostBlockReason why = PostBlockReason::kNone;
    if (!AllZero(MakePostBlock(in.regs, in.fc, in.table, n, &why)) || why != PostBlockReason::kTableBytes) return 140;
  }
  {
    PostBlockReason why = PostBlockReason::kNone;
    if (!AllZero(MakePostBlock(in.regs, in.fc, nullptr, kPostTableBytes, &why)) || why != PostBlockReason::kTableBytes) return 141;
  }
  // A texel that is no number or outside [0, 4), at the first texel, in the middle and at the last
  // (Review Focus 2): no block, all zero.
  for (int at : {0, 1, 128, 510, 511}) {
    for (float bad : {kNaN, -kNaN, kInf, -kInf, 4.0f, 16.0f, Down(0.0f), -1.0f, 3.4e38f}) {
      Inputs o;
      o.Texel(at, bad);
      if (!Refused(o, PostBlockReason::kTableTexel)) return 142;
    }
  }
  // A signalling pattern and the largest exponent with a mantissa, written as bytes.
  {
    Inputs o;
    const unsigned char snan[4] = {0x7F, 0xA0, 0x00, 0x01};
    std::memcpy(o.table + 4 * 77, snan, 4);
    if (!Refused(o, PostBlockReason::kTableTexel)) return 143;
  }
  // The bytes are read high byte first: the same bytes turned round are another table (here no table).
  {
    Inputs o;
    for (int i = 0; i < 512; ++i) {
      std::swap(o.table[4 * i], o.table[4 * i + 3]);
      std::swap(o.table[4 * i + 1], o.table[4 * i + 2]);
    }
    PostBlockReason why = PostBlockReason::kNone;
    const PostBlock t = o.Make(&why);
    if (t.valid && t.table[1] == in.Make().table[1]) return 144;
  }
  return 0;
}

static int BlurTests() {
  const Inputs in;
  const PostBlock whole = in.Make();
  // Without both blurs the block is valid, without the part, its blur zero, the rest the same.
  auto no_blur = [&](const Inputs& o) {
    PostBlockReason why = PostBlockReason::kUnread;
    const PostBlock t = o.Make(&why);
    static const float zero[2][7] = {};
    return t.valid && why == PostBlockReason::kNone && t.parts == 0 && std::memcmp(t.blur, zero, sizeof(zero)) == 0 &&
           std::memcmp(t.composite, whole.composite, sizeof(t.composite)) == 0 &&
           std::memcmp(t.bright, whole.bright, sizeof(t.bright)) == 0 && std::memcmp(t.table, whole.table, sizeof(t.table)) == 0;
  };
  for (int s = 0; s < 2; ++s) {
    Inputs o;
    o.regs.have_blur[s] = false;
    if (!no_blur(o)) return 150 + s;
  }
  // A weight outside [0, 1] or no number, in either set, at the centre and at a tap.
  for (int s = 0; s < 2; ++s) {
    for (int w = 0; w < 7; ++w) {
      for (float bad : {kNaN, kInf, -kInf, Down(0.0f), Up(1.0f), -0.2f}) {
        Inputs o;
        o.regs.blur[s][w / 4][w % 4] = bad;
        if (!no_blur(o)) return 152;
      }
    }
  }
  // A set whose taps do not sum to 1 within 1e-3 (w0 + 2 (w1 + .. + w6)).
  for (int s = 0; s < 2; ++s) {
    Inputs o;
    o.regs.blur[s][0][0] += 0.002f;
    if (!no_blur(o)) return 153;
    Inputs p;
    p.regs.blur[s][0][1] -= 0.001f;  // (counted twice: 0.002 off)
    if (!no_blur(p)) return 154;
    Inputs q;  // within the tolerance: the part stays, with the weights as they were read
    q.regs.blur[s][0][0] += 0.0005f;
    const PostBlock t = q.Make();
    if (!t.valid || t.parts != kPostPartBlur || t.blur[s][0] != q.regs.blur[s][0][0]) return 155;
  }
  // Weights that are in range and sum to 1 but all in the centre: a part (the rule is the sum's).
  {
    Inputs o;
    for (int s = 0; s < 2; ++s) {
      std::memset(o.regs.blur[s], 0, sizeof(o.regs.blur[s]));
      o.regs.blur[s][0][0] = 1.0f;
    }
    const PostBlock t = o.Make();
    if (!t.valid || t.parts != kPostPartBlur || t.blur[0][0] != 1.0f || t.blur[1][6] != 0.0f) return 156;
    // ... and the bound of a weight is its own rule: a centre of 1.0005 sums to 1 within the
    // tolerance and is above 1 (found by the mutation check: nothing else held the upper bound).
    o.regs.blur[1][0][0] = 1.0005f;
    if (!no_blur(o)) return 157;
  }
  return 0;
}

static int PairTests() {
  const Inputs in;
  const PostBlock b = in.Make();
  if (!PostBlockPairs(b, kFrameScale)) return 160;
  // Within 1e-3 of 1, on both sides; outside it, not.
  if (!PostBlockPairs(b, kFrameScale * 1.0009f) || !PostBlockPairs(b, kFrameScale * 0.9991f)) return 161;
  if (PostBlockPairs(b, kFrameScale * 1.0011f) || PostBlockPairs(b, kFrameScale * 0.9989f)) return 162;
  // The blind run's pair: c77.y 0.0625 with c20.w 16 (15.99999 in the captures).
  {
    Inputs o;
    o.regs.bright[3][1] = 0.0625f;
    const PostBlock blind = o.Make();
    if (!PostBlockPairs(blind, 16.0f) || !PostBlockPairs(blind, 15.99999f) || PostBlockPairs(blind, kFrameScale)) return 163;
    if (PostBlockPairs(b, 16.0f)) return 164;  // an adapted block against a blind scene
  }
  // No scale (a frame without a light block), a scale that is no number, a negative one.
  for (float bad : {0.0f, kNaN, kInf, -kInf, -kFrameScale}) {
    if (PostBlockPairs(b, bad)) return 165;
  }
  // An invalid block pairs with nothing, whatever its fields say.
  PostBlock none{};
  none.bright[4] = 1.0f / kFrameScale;
  if (PostBlockPairs(none, kFrameScale) || PostBlockPairs(PostBlock{}, 0.0f)) return 166;
  return 0;
}

static int ChoiceTests() {
  const Inputs in;
  const PostBlock own = in.Make();
  Inputs other;  // a block of another exposure: c77.y = 1 / 10
  other.regs.bright[3][1] = 0.1f;
  const PostBlock ten = other.Make();
  if (!own.valid || !ten.valid) return 170;
  const PostBlock none{};
  auto is = [](const PostChoice& c, PostPick pick, uint32_t age, PostBlockReason reason) {
    return c.pick == pick && c.age == age && c.reason == reason;
  };
  const PostBlockReason kNoRead = PostBlockReason::kNoDraw;
  // The frame's own block, valid and pairing: it, at age 0, whatever is kept.
  if (!is(ChoosePostBlock(own, 0, kNoRead, none, 0, kFrameScale), PostPick::kOwn, 0, PostBlockReason::kNone)) return 171;
  if (!is(ChoosePostBlock(own, 2, PostBlockReason::kUnread, ten, 1, kFrameScale), PostPick::kOwn, 0, PostBlockReason::kNone)) return 172;
  // None of its own: the kept one at ages 1 to 4 if it pairs; at 5 and beyond, stale.
  for (uint32_t age = 1; age <= 6; ++age) {
    const PostChoice c = ChoosePostBlock(none, 0, kNoRead, own, age, kFrameScale);
    if (age <= kPostBlockMaxAge ? !is(c, PostPick::kKept, age, PostBlockReason::kNone)
                                : !is(c, PostPick::kNone, 0, PostBlockReason::kStale)) {
      return 173;
    }
  }
  if (!is(ChoosePostBlock(none, 0, kNoRead, own, 0xFFFFFFFFu, kFrameScale), PostPick::kNone, 0, PostBlockReason::kStale)) return 174;
  // A kept block is never the frame's at age 0 (that is the own block's age).
  if (ChoosePostBlock(none, 0, kNoRead, own, 0, kFrameScale).pick != PostPick::kNone) return 175;
  // Kept and not pairing (the exposure moved on, or the scene is another's): none, "pairing".
  for (uint32_t age = 1; age <= kPostBlockMaxAge; ++age) {
    if (!is(ChoosePostBlock(none, 0, kNoRead, ten, age, kFrameScale), PostPick::kNone, 0, PostBlockReason::kPairing)) return 176;
  }
  // ... and a kept block that is too old is stale whether it would pair or not.
  if (!is(ChoosePostBlock(none, 0, kNoRead, ten, 5, kFrameScale), PostPick::kNone, 0, PostBlockReason::kStale)) return 177;
  // The frame's own does not pair: the kept one if that does, else "pairing".
  if (!is(ChoosePostBlock(ten, 0, kNoRead, own, 1, kFrameScale), PostPick::kKept, 1, PostBlockReason::kNone)) return 178;
  if (!is(ChoosePostBlock(ten, 0, kNoRead, none, 0, kFrameScale), PostPick::kNone, 0, PostBlockReason::kPairing)) return 179;
  if (!is(ChoosePostBlock(ten, 0, kNoRead, own, 5, kFrameScale), PostPick::kNone, 0, PostBlockReason::kPairing)) return 180;
  // The frame's reads failed: the kept one if it serves; else the last read's reason, before "stale".
  if (!is(ChoosePostBlock(none, 3, PostBlockReason::kTableTexel, own, 2, kFrameScale), PostPick::kKept, 2, PostBlockReason::kNone)) {
    return 181;
  }
  if (!is(ChoosePostBlock(none, 3, PostBlockReason::kTableTexel, none, 0, kFrameScale), PostPick::kNone, 0,
          PostBlockReason::kTableTexel)) {
    return 182;
  }
  if (!is(ChoosePostBlock(none, 1, PostBlockReason::kRegister, own, 9, kFrameScale), PostPick::kNone, 0, PostBlockReason::kRegister)) {
    return 183;
  }
  // Nothing read and nothing kept: "no-draw".
  if (!is(ChoosePostBlock(none, 0, kNoRead, none, 0, kFrameScale), PostPick::kNone, 0, PostBlockReason::kNoDraw)) return 184;
  // A frame without a light block (scale 0, or one that is no number) has no post block: "pairing"
  // with a block at hand, the frame's own reason without.
  for (float scale : {0.0f, kNaN, kInf}) {
    if (!is(ChoosePostBlock(own, 0, kNoRead, own, 1, scale), PostPick::kNone, 0, PostBlockReason::kPairing)) return 185;
    if (!is(ChoosePostBlock(none, 0, kNoRead, own, 1, scale), PostPick::kNone, 0, PostBlockReason::kPairing)) return 186;
    if (!is(ChoosePostBlock(none, 0, kNoRead, none, 0, scale), PostPick::kNone, 0, PostBlockReason::kNoDraw)) return 187;
    if (!is(ChoosePostBlock(none, 1, PostBlockReason::kUnread, none, 0, scale), PostPick::kNone, 0, PostBlockReason::kUnread)) return 188;
  }
  return 0;
}

static int NameTests() {
  std::set<std::string> names;
  for (size_t i = 0; i < size_t(PostBlockReason::kCount); ++i) {
    const std::string name = PostBlockReasonName(PostBlockReason(i));
    if (name.empty() || name == "?" || !names.insert(name).second) return 190;
  }
  if (std::string(PostBlockReasonName(PostBlockReason::kCount)) != "?") return 191;
  const std::pair<PostBlockReason, const char*> want[] = {
      {PostBlockReason::kNone, "none"},          {PostBlockReason::kOff, "off"},
      {PostBlockReason::kNoDraw, "no-draw"},     {PostBlockReason::kUnread, "unread"},
      {PostBlockReason::kRegister, "register"},  {PostBlockReason::kRange, "range"},
      {PostBlockReason::kPairing, "pairing"},    {PostBlockReason::kTableDimension, "table-dimension"},
      {PostBlockReason::kTableBase, "table-base"},        {PostBlockReason::kTableWidth, "table-width"},
      {PostBlockReason::kTableFormat, "table-format"},    {PostBlockReason::kTableEndian, "table-endian"},
      {PostBlockReason::kTableSwizzle, "table-swizzle"},  {PostBlockReason::kTableSign, "table-sign"},
      {PostBlockReason::kTableExpAdjust, "table-exp-adjust"}, {PostBlockReason::kTableClamp, "table-clamp"},
      {PostBlockReason::kTableFilter, "table-filter"},    {PostBlockReason::kTableMip, "table-mip"},
      {PostBlockReason::kTableBytes, "table-bytes"},      {PostBlockReason::kTableTexel, "table-texel"},
      {PostBlockReason::kStale, "stale"}};
  if (sizeof(want) / sizeof(want[0]) != size_t(PostBlockReason::kCount)) return 192;
  for (const auto& [reason, name] : want) {
    if (std::string(PostBlockReasonName(reason)) != name) return 193;
  }
  // Every fault of the table has its reason, in the fog's order; no fault, no reason.
  for (int f = 1; f < int(FogTableFault::kCount); ++f) {
    if (int(PostTableReason(FogTableFault(f))) != int(PostBlockReason::kTableDimension) + f - 1) return 194;
  }
  if (PostTableReason(FogTableFault::kNone) != PostBlockReason::kNone || PostTableReason(FogTableFault::kCount) != PostBlockReason::kNone) {
    return 195;
  }
  if (std::string(kPostBlockReason) != "post-block") return 196;
  static_assert(kPostCompositeRegisters[3] == 77 && kPostCompositeRegisters[4] == 78 && kPostBrightRegisters[3] == 77 &&
                kPostBlurRegisters[0] == 46 && kPostBlurRegisters[1] == 47);
  return 0;
}

int main() {
  for (int (*test)() : {ValidTests, RegisterTests, FetchTests, BytesTests, BlurTests, PairTests, ChoiceTests, NameTests}) {
    if (const int code = test()) {
      std::printf("FAIL: post block case %d\n", code);
      return 1;
    }
  }
  std::puts("PASS: post block from a captured frame's bytes, its register, range, fetch constant and texel rules, the blur part, "
            "the pairing, the frame's choice");
  return 0;
}
