// Synthetic standalone test; no game or GPU. With one argument, a prefix, it also parses the two files
// <prefix>.scene and <prefix>.block of tools/xdk_sigmatch/post_replay.py export and prints what they hold
// (by hand, against real exports: out/native-evidence/post/oracle/).
#define _CRT_SECURE_NO_WARNINGS  // (fopen, in that report)
#include "../../src/native/render/post_oracle.h"
#include "../../src/native/capture/position_decode.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

using namespace fable2::native::render;
using fable2::native::capture::HalfToFloat;
using fable2::native::capture::kPostPartBlur;
using fable2::native::capture::kPostPartRadial;
using fable2::native::capture::MakePostBlock;
using fable2::native::capture::PostBlock;
using fable2::native::capture::PostBlockReason;
using fable2::native::capture::PostRegisters;

#include "post_table_fixture.inc"

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

static float Bits(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
static uint32_t BitsOf(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}
static void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));  // little-endian
}
static void PutFloat(std::vector<uint8_t>& out, float v) { PutU32(out, BitsOf(v)); }
static void SetFloat(std::vector<uint8_t>& file, size_t at, float v) {
  const uint32_t bits = BitsOf(v);
  for (int k = 0; k < 4; ++k) file[at + k] = uint8_t(bits >> (8 * k));
}

// --- HalfFromFloat against HalfToFloat ---

static int HalfTests() {
  // Every half that is a number comes back from its own float; a NaN stays a NaN.
  for (uint32_t h = 0; h < 0x10000; ++h) {
    const float f = HalfToFloat(uint16_t(h));
    const uint16_t back = HalfFromFloat(f);
    if (f != f) {
      if ((back & 0x7C00) != 0x7C00 || (back & 0x3FF) == 0) return 1;
    } else if (back != h) {
      return 2;
    }
  }
  // Between two neighbours: the nearest, and at the middle the even one. All positive finite pairs,
  // and the last finite half against the first value that rounds to infinity (65520).
  for (uint32_t h = 0; h < 0x7C00; ++h) {
    const double a = HalfToFloat(uint16_t(h));
    const double b = h + 1 < 0x7C00 ? double(HalfToFloat(uint16_t(h + 1))) : 65536.0;
    const float mid = float((a + b) / 2);  // (exact: a half has 11 significant bits, a float 24)
    if (double(mid) != (a + b) / 2) return 3;
    const uint16_t even = (h & 1) ? uint16_t(h + 1) : uint16_t(h);
    if (HalfFromFloat(mid) != even) return 4;
    if (HalfFromFloat(std::nextafter(mid, kInf)) != h + 1) return 5;
    if (HalfFromFloat(std::nextafter(mid, 0.0f)) != h) return 6;
    if (HalfFromFloat(-mid) != (even | 0x8000)) return 7;
  }
  if (HalfFromFloat(1.0f) != 0x3C00 || HalfFromFloat(-2.0f) != 0xC000 || HalfFromFloat(0.0f) != 0 || HalfFromFloat(-0.0f) != 0x8000) return 8;
  if (HalfFromFloat(65504.0f) != 0x7BFF || HalfFromFloat(65520.0f) != 0x7C00 || HalfFromFloat(std::nextafter(65520.0f, 0.0f)) != 0x7BFF) return 9;
  if (HalfFromFloat(1e10f) != 0x7C00 || HalfFromFloat(-1e10f) != 0xFC00 || HalfFromFloat(kInf) != 0x7C00 || HalfFromFloat(-kInf) != 0xFC00) return 10;
  // The smallest: half of the smallest denormal is a tie and goes to 0; a float's own denormals are 0.
  const float tiny = std::ldexp(1.0f, -25);
  if (HalfFromFloat(tiny) != 0 || HalfFromFloat(std::nextafter(tiny, 1.0f)) != 1 || HalfFromFloat(std::ldexp(1.0f, -24)) != 1) return 11;
  if (HalfFromFloat(std::numeric_limits<float>::denorm_min()) != 0 || HalfFromFloat(-std::numeric_limits<float>::min()) != 0x8000) return 12;
  // The game's scene target holds at most 31.875 with 7 bits of mantissa: such values are exact in a half.
  if (HalfToFloat(HalfFromFloat(31.875f)) != 31.875f || HalfToFloat(HalfFromFloat(0.0078125f)) != 0.0078125f) return 13;
  return 0;
}

// --- <prefix>.scene ---

static std::vector<uint8_t> SceneFile(uint32_t w, uint32_t h, const std::vector<float>& values, const char* magic = "F2OS") {
  std::vector<uint8_t> out(magic, magic + 4);
  PutU32(out, w);
  PutU32(out, h);
  for (float v : values) PutFloat(out, v);
  return out;
}
static bool SceneRefused(const std::vector<uint8_t>& file, OracleFault want, uint32_t w, uint32_t h) {
  OracleFault why = OracleFault::kNone;
  const OracleScene s = ParseOracleScene(file.data(), file.size(), &why, w, h);
  return !s.valid && s.width == 0 && s.height == 0 && s.values == nullptr && why == want;
}

static int SceneTests() {
  static_assert(kOracleWidth == 1120 && kOracleHeight == 720 && kOracleHeaderBytes == 12);
  std::vector<float> values;
  for (int i = 0; i < 4 * 2 * 3; ++i) values.push_back(0.125f * float(i) - 0.25f);  // (two negative values among them)
  const std::vector<uint8_t> file = SceneFile(4, 2, values);
  OracleFault why = OracleFault::kShort;
  const OracleScene s = ParseOracleScene(file.data(), file.size(), &why, 4, 2);
  if (!s.valid || why != OracleFault::kNone || s.width != 4 || s.height != 2 || s.values != file.data() + 12) return 20;
  for (size_t i = 0; i < values.size(); ++i) {
    if (BitsOf(OracleSceneValue(s, i)) != BitsOf(values[i])) return 21;
  }
  if (OracleSceneValue(s, values.size()) != 0.0f) return 22;  // (beyond the picture: 0, nothing is read)
  // The parse without a reason pointer, and the default size, the pass's logical size.
  if (!ParseOracleScene(file.data(), file.size(), nullptr, 4, 2).valid) return 23;
  if (ParseOracleScene(file.data(), file.size()).valid) return 24;
  {
    std::vector<uint8_t> whole(12 + size_t(1120) * 720 * 12, 0);
    std::memcpy(whole.data(), "F2OS", 4);
    whole[4] = 0x60, whole[5] = 0x04;  // 1120
    whole[8] = 0xD0, whole[9] = 0x02;  // 720
    const OracleScene full = ParseOracleScene(whole.data(), whole.size());
    if (!full.valid || full.width != 1120 || full.height != 720) return 25;
    if (OracleSceneHalfChanged(full) != 0) return 26;
    whole.pop_back();
    if (ParseOracleScene(whole.data(), whole.size()).valid) return 27;
  }
  // Short files: nothing, no bytes, each length below the header's, the header alone, one byte short.
  if (!SceneRefused({}, OracleFault::kShort, 4, 2)) return 30;
  why = OracleFault::kNone;
  if (ParseOracleScene(nullptr, 100, &why, 4, 2).valid || why != OracleFault::kShort) return 31;
  for (size_t n = 0; n < 12; ++n) {
    if (!SceneRefused(std::vector<uint8_t>(file.begin(), file.begin() + n), OracleFault::kShort, 4, 2)) return 32;
  }
  if (!SceneRefused(std::vector<uint8_t>(file.begin(), file.begin() + 12), OracleFault::kSize, 4, 2)) return 33;
  if (!SceneRefused(std::vector<uint8_t>(file.begin(), file.end() - 1), OracleFault::kSize, 4, 2)) return 34;
  {
    std::vector<uint8_t> longer = file;
    longer.push_back(0);
    if (!SceneRefused(longer, OracleFault::kSize, 4, 2)) return 35;
  }
  // The magic: each of its bytes, and the other files' magics.
  for (int k = 0; k < 4; ++k) {
    std::vector<uint8_t> bad = file;
    bad[k] ^= 0x20;
    if (!SceneRefused(bad, OracleFault::kMagic, 4, 2)) return 36;
  }
  if (!SceneRefused(SceneFile(4, 2, values, "F2OB"), OracleFault::kMagic, 4, 2)) return 37;
  if (!SceneRefused(SceneFile(4, 2, values, "F2OD"), OracleFault::kMagic, 4, 2)) return 38;
  // The size: the header's against the wanted one (a file of another picture, whole in itself), a
  // transposed one, zero, and sizes whose product does not fit 32 bits.
  if (!SceneRefused(file, OracleFault::kSize, 2, 4)) return 39;
  if (!SceneRefused(file, OracleFault::kSize, 8, 1)) return 40;
  if (!SceneRefused(SceneFile(4, 1, std::vector<float>(12, 0.0f)), OracleFault::kSize, 4, 2)) return 41;
  if (!SceneRefused(SceneFile(0, 0, {}), OracleFault::kSize, 0, 0)) return 42;
  if (!SceneRefused(SceneFile(0x10000, 0x10000, {}), OracleFault::kSize, 0x10000, 0x10000)) return 43;
  if (!SceneRefused(SceneFile(0x40000000, 4, values), OracleFault::kSize, 0x40000000, 4)) return 44;
  // A value that is no number, anywhere: the first, one inside, the last; each kind.
  for (size_t at : {size_t(0), size_t(11), values.size() - 1}) {
    for (float v : {kNaN, kInf, -kInf, Bits(0x7F800001), Bits(0xFFC00000)}) {
      std::vector<uint8_t> bad = file;
      SetFloat(bad, 12 + 4 * at, v);
      if (!SceneRefused(bad, OracleFault::kValue, 4, 2)) return 45;
    }
  }
  // The largest float is a number: the file is taken, and the texel is the largest half (no infinity
  // reaches the texture); negative values and the sign of zero pass as they are.
  {
    std::vector<uint8_t> big = file;
    SetFloat(big, 12, std::numeric_limits<float>::max());
    SetFloat(big, 16, -1e9f);
    SetFloat(big, 20, 65519.0f);
    SetFloat(big, 24, -0.0f);
    const OracleScene b = ParseOracleScene(big.data(), big.size(), nullptr, 4, 2);
    if (!b.valid) return 46;
    uint16_t row[16];
    OracleSceneRowHalf(b, 0, row);
    if (row[0] != 0x7BFF || row[1] != 0xFBFF || row[2] != 0x7BFF || row[3] != 0x3C00 || row[4] != 0x8000) return 47;
    if (OracleSceneHalfChanged(b) != 3) return 48;
  }
  // A row as the texture takes it: red, green, blue as halves, alpha 1, row by row.
  uint16_t rows[2][16];
  std::memset(rows, 0xAB, sizeof(rows));
  OracleSceneRowHalf(s, 0, rows[0]);
  OracleSceneRowHalf(s, 1, rows[1]);
  for (uint32_t y = 0; y < 2; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      for (uint32_t c = 0; c < 3; ++c) {
        if (HalfToFloat(rows[y][4 * x + c]) != values[(y * 4 + x) * 3 + c]) return 49;
      }
      if (rows[y][4 * x + 3] != 0x3C00) return 50;
    }
  }
  if (OracleSceneHalfChanged(s) != 0) return 51;
  // A row outside the picture, and an invalid scene: nothing is written.
  uint16_t guard[16];
  std::memset(guard, 0xCD, sizeof(guard));
  OracleSceneRowHalf(s, 2, guard);
  OracleSceneRowHalf(OracleScene{}, 0, guard);
  for (uint16_t g : guard) {
    if (g != 0xCDCD) return 52;
  }
  if (OracleSceneHalfChanged(OracleScene{}) != 0) return 53;
  // A value a half cannot hold exactly is counted.
  {
    std::vector<uint8_t> fine = file;
    SetFloat(fine, 12 + 4 * 5, 0.1f);
    const OracleScene f = ParseOracleScene(fine.data(), fine.size(), nullptr, 4, 2);
    if (!f.valid || OracleSceneHalfChanged(f) != 1) return 54;
  }
  return 0;
}

// --- <prefix>.block ---

// Frame 1 of capture native_discovery_20261009_205049 (test_post_block.cpp's frame): its block's 541
// floats in the struct's order, as post_replay.py's pack_block writes them, and the frame's c20.w.
struct BlockFile {
  float c20w = 8.45011711f;
  float floats[541] = {};
  uint32_t version = 1, parts = 1;
  const char* magic = "F2OB";
  BlockFile() {
    const uint32_t head[25] = {0x403E4612, 0x3E800000, 0x42480000, 0xBBAA4E61, 0x3F8D2176, 0x3EE8BA2E,              // 37B1
                               0x3FBE4612, 0x3E4CCCCE, 0x40000000, 0x3DB22120, 0x3DF25D0F,                          // EE92
                               0x3E545A2D, 0x3E398C6D, 0x3DF790AC, 0x3D7C2FE0, 0x3CC422CE, 0x3BE8EDC3, 0x3AD332BF,  // EA83
                               0x3E3A39E9, 0x3E27F2C5, 0x3DF661FC, 0x3D92FD2B, 0x3D0EA54D, 0x3C612E70, 0x3B908F2F}; // 28DB
    for (int i = 0; i < 25; ++i) floats[i] = Bits(head[i]);
    for (int i = 0; i < 512; ++i) {
      const unsigned char* p = kFixturePostTable + 4 * i;
      floats[29 + i] = Bits((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]));
    }
  }
  std::vector<uint8_t> Bytes() const {
    std::vector<uint8_t> out(magic, magic + 4);
    PutU32(out, version);
    PutFloat(out, c20w);
    for (float v : floats) PutFloat(out, v);
    PutU32(out, parts);
    return out;
  }
};
static bool AllZero(const PostBlock& b) {
  static const unsigned char zero[offsetof(PostBlock, parts)] = {};
  return !b.valid && b.parts == 0 && std::memcmp(&b, zero, sizeof(zero)) == 0;
}
static bool BlockRefused(const std::vector<uint8_t>& file, OracleFault want, PostBlockReason want_rule = PostBlockReason::kNone) {
  OracleFault why = OracleFault::kNone;
  PostBlockReason rule = PostBlockReason::kCount;
  float c20w = 123.0f;
  const PostBlock b = ParseOracleBlock(file.data(), file.size(), &c20w, &why, &rule);
  return AllZero(b) && c20w == 0.0f && why == want && rule == want_rule;
}

static int BlockTests() {
  static_assert(kOracleBlockFloats == 541 && kOracleBlockVersion == 1 && kOracleBlockBytes == 2180);
  static_assert(kOracleBlockFloats * 4 == offsetof(PostBlock, parts), "the file's floats are the struct's, in its order");
  const BlockFile good;
  const std::vector<uint8_t> file = good.Bytes();
  if (file.size() != kOracleBlockBytes) return 60;
  OracleFault why = OracleFault::kShort;
  PostBlockReason rule = PostBlockReason::kCount;
  float c20w = 0.0f;
  const PostBlock b = ParseOracleBlock(file.data(), file.size(), &c20w, &why, &rule);
  if (!b.valid || why != OracleFault::kNone || rule != PostBlockReason::kNone || c20w != good.c20w) return 61;
  // The block is the file's floats bit for bit, in the struct's order; the radial part is not taken.
  if (std::memcmp(&b, good.floats, 25 * 4) != 0 || std::memcmp(b.table, good.floats + 29, 512 * 4) != 0) return 62;
  if (b.parts != kPostPartBlur || b.radial[0] != 0.0f || b.radial[3] != 0.0f) return 63;
  // And it is the block the capture's own rule makes of the same registers and bytes.
  {
    PostRegisters regs{};
    const float* f = good.floats;
    regs.composite[0][0] = f[0], regs.composite[1][0] = f[1], regs.composite[2][0] = f[2];
    regs.composite[3][0] = f[3], regs.composite[3][1] = f[4], regs.composite[4][0] = f[5];
    regs.bright[0][0] = f[6], regs.bright[1][0] = f[7], regs.bright[2][0] = f[8], regs.bright[3][0] = f[9], regs.bright[3][1] = f[10];
    for (int s = 0; s < 2; ++s) {
      for (int i = 0; i < 7; ++i) regs.blur[s][i >> 2][i & 3] = f[11 + 7 * s + i];
    }
    regs.have_composite = regs.have_bright = regs.have_blur[0] = regs.have_blur[1] = true;
    const uint32_t fc[6] = {0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018};
    const PostBlock direct = MakePostBlock(regs, fc, kFixturePostTable, sizeof(kFixturePostTable));
    if (!direct.valid || std::memcmp(&direct, &b, offsetof(PostBlock, parts)) != 0 || direct.parts != b.parts) return 64;
  }
  if (!ParseOracleBlock(file.data(), file.size()).valid) return 65;  // (no pointer is needed)
  // Short and long files, nothing.
  if (!BlockRefused({}, OracleFault::kShort)) return 70;
  why = OracleFault::kNone;
  if (ParseOracleBlock(nullptr, kOracleBlockBytes, nullptr, &why).valid || why != OracleFault::kShort) return 71;
  for (size_t n : {size_t(1), size_t(3), size_t(4), size_t(11)}) {
    if (!BlockRefused(std::vector<uint8_t>(file.begin(), file.begin() + n), OracleFault::kShort)) return 72;
  }
  for (size_t n : {size_t(12), size_t(2176), size_t(2179)}) {
    if (!BlockRefused(std::vector<uint8_t>(file.begin(), file.begin() + n), OracleFault::kSize)) return 73;
  }
  {
    std::vector<uint8_t> longer = file;
    longer.push_back(0);
    if (!BlockRefused(longer, OracleFault::kSize)) return 74;
  }
  // Magic and version.
  for (const char* magic : {"F2OS", "F2OD", "f2ob", "F2O\0"}) {
    BlockFile f;
    f.magic = magic;
    if (!BlockRefused(f.Bytes(), OracleFault::kMagic)) return 75;
  }
  for (uint32_t version : {0u, 2u, 0x01000000u}) {
    BlockFile f;
    f.version = version;
    if (!BlockRefused(f.Bytes(), OracleFault::kVersion)) return 76;
  }
  // The scale: a number above 0, and the one the block pairs with (the bright pass's c77.y times it is 1).
  for (float scale : {kNaN, kInf, 0.0f, -8.45011711f}) {
    BlockFile f;
    f.c20w = scale;
    if (!BlockRefused(f.Bytes(), OracleFault::kScale)) return 77;
  }
  for (float scale : {16.0f, 8.45011711f * 1.002f, 8.45011711f * 0.998f}) {
    BlockFile f;
    f.c20w = scale;
    if (!BlockRefused(f.Bytes(), OracleFault::kPairing)) return 78;
  }
  // A block that fails a rule of MakePostBlock: the rule is named.
  for (int i = 0; i < 11; ++i) {
    for (float v : {kNaN, kInf}) {
      BlockFile f;
      f.floats[i] = v;
      if (!BlockRefused(f.Bytes(), OracleFault::kBlock, PostBlockReason::kRegister)) return 79;
    }
  }
  {
    const struct { int at; float v; } range[] = {{0, -0.1f}, {0, 65.0f}, {1, 0.0f}, {2, 2e6f}, {3, 4.5f}, {4, 0.0f}, {5, 0.0f},
                                                 {5, 1.5f}, {6, 65.0f}, {7, -1.0f}, {8, 0.0f}, {9, -4.5f}, {10, 0.0f}, {10, 65.0f}};
    for (const auto& r : range) {
      BlockFile f;
      f.floats[r.at] = r.v;
      if (!BlockRefused(f.Bytes(), OracleFault::kBlock, PostBlockReason::kRange)) return 80;
    }
  }
  for (int texel : {0, 1, 128, 511}) {
    for (float v : {kNaN, kInf, -1e-6f, 4.0f}) {
      BlockFile f;
      f.floats[29 + texel] = v;
      if (!BlockRefused(f.Bytes(), OracleFault::kBlock, PostBlockReason::kTableTexel)) return 81;
    }
  }
  // The parts. An unknown bit; the blur's bit with weights that fail their rule (a file that says it
  // has what it has not); without the bit the weights are not taken, whatever they are.
  for (uint32_t parts : {4u, 5u, 0x80000001u}) {
    BlockFile f;
    f.parts = parts;
    if (!BlockRefused(f.Bytes(), OracleFault::kParts)) return 82;
  }
  for (float w : {kNaN, -0.01f, 1.5f, 0.5f}) {
    BlockFile f;
    f.floats[11 + 3] = w;
    if (!BlockRefused(f.Bytes(), OracleFault::kParts)) return 83;
    f.floats[11 + 3] = good.floats[11 + 3];
    f.floats[18 + 6] = w;
    if (!BlockRefused(f.Bytes(), OracleFault::kParts)) return 84;
    f.parts = 0;
    const std::vector<uint8_t> bytes = f.Bytes();
    const PostBlock no_blur = ParseOracleBlock(bytes.data(), bytes.size());
    if (!no_blur.valid || no_blur.parts != 0) return 85;
    for (int s = 0; s < 2; ++s) {
      for (float v : no_blur.blur[s]) {
        if (v != 0.0f) return 86;
      }
    }
  }
  // (and weights that would pass their rule are not taken without the bit either)
  {
    BlockFile f;
    f.parts = 0;
    const std::vector<uint8_t> bytes = f.Bytes();
    const PostBlock no_blur = ParseOracleBlock(bytes.data(), bytes.size());
    if (!no_blur.valid || no_blur.parts != 0) return 90;
    for (int s = 0; s < 2; ++s) {
      for (float v : no_blur.blur[s]) {
        if (v != 0.0f) return 91;
      }
    }
  }
  // The radial part (plan Task 12): its bit is accepted and nothing of it is taken; its floats must be numbers.
  {
    BlockFile f;
    f.parts = 3;
    f.floats[25] = 0.5f, f.floats[26] = -0.25f, f.floats[27] = 0.9f, f.floats[28] = 0.8f;
    const std::vector<uint8_t> bytes = f.Bytes();
    const PostBlock r = ParseOracleBlock(bytes.data(), bytes.size());
    if (!r.valid || r.parts != kPostPartBlur || (r.parts & kPostPartRadial)) return 87;
    for (float v : r.radial) {
      if (v != 0.0f) return 88;
    }
    f.floats[27] = kNaN;
    if (!BlockRefused(f.Bytes(), OracleFault::kValue)) return 89;
  }
  return 0;
}

// --- the dump ---

static int DumpTests() {
  static_assert(kOracleDumpAfter == 30);
  if (OracleDumpPitch(1120) != 4608 || OracleDumpPitch(64) != 256 || OracleDumpPitch(65) != 512 || OracleDumpPitch(0) != 0) return 100;
  if (OracleDumpPitch(1120) % 256 != 0 || OracleDumpPitch(1120) < 1120 * 4) return 101;
  if (OracleDumpPitch(0x40000000) != 0) return 102;  // (no pitch fits 32 bits)
  // 3 x 2 texels in rows of 256 bytes: the header, then the rows without their padding.
  std::vector<uint8_t> rows(512, 0xEE);
  for (int y = 0; y < 2; ++y) {
    for (int i = 0; i < 12; ++i) rows[256 * y + i] = uint8_t(16 * y + i);
  }
  std::vector<uint8_t> out = {1, 2, 3};
  if (!PackOracleDump(rows.data(), 256, 3, 2, &out)) return 103;
  const uint8_t want[12 + 24] = {'F', '2', 'O', 'D', 3, 0, 0, 0, 2, 0, 0, 0, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                 16,  17,  18,  19,  20, 21, 22, 23, 24, 25, 26, 27};
  if (out.size() != sizeof(want) || std::memcmp(out.data(), want, sizeof(want)) != 0) return 104;
  // Refused, and the output empty: nothing to read, no size, a pitch below a row.
  if (PackOracleDump(nullptr, 256, 3, 2, &out) || !out.empty()) return 105;
  out = {1};
  if (PackOracleDump(rows.data(), 256, 0, 2, &out) || !out.empty()) return 106;
  out = {1};
  if (PackOracleDump(rows.data(), 256, 3, 0, &out) || !out.empty()) return 107;
  out = {1};
  if (PackOracleDump(rows.data(), 11, 3, 2, &out) || !out.empty()) return 108;
  if (PackOracleDump(rows.data(), 256, 3, 2, nullptr)) return 109;
  if (PackOracleDump(rows.data(), 12, 3, 2, &out) && out.size() != 36) return 110;  // (a pitch of exactly a row is one)
  // The names of the faults, each once.
  const char* names[] = {"none", "short", "magic", "version", "size", "value", "scale", "parts", "block", "pairing"};
  if (sizeof(names) / sizeof(names[0]) != size_t(OracleFault::kCount)) return 111;
  for (size_t i = 0; i < size_t(OracleFault::kCount); ++i) {
    if (std::string(OracleFaultName(OracleFault(i))) != names[i]) return 112;
  }
  if (std::string(OracleFaultName(OracleFault::kCount)) != "?") return 113;
  return 0;
}

// By hand: the two files of a real export.
static int Report(const char* prefix) {
  auto read = [](const std::string& path, std::vector<uint8_t>* out) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), fp)) > 0;) out->insert(out->end(), buf, buf + n);
    std::fclose(fp);
    return true;
  };
  std::vector<uint8_t> scene_bytes, block_bytes;
  if (!read(std::string(prefix) + ".scene", &scene_bytes) || !read(std::string(prefix) + ".block", &block_bytes)) {
    std::printf("%s: a file could not be read\n", prefix);
    return 1;
  }
  OracleFault scene_why, block_why;
  PostBlockReason rule;
  float c20w = 0.0f;
  const OracleScene scene = ParseOracleScene(scene_bytes.data(), scene_bytes.size(), &scene_why);
  const PostBlock block = ParseOracleBlock(block_bytes.data(), block_bytes.size(), &c20w, &block_why, &rule);
  std::printf("%s.scene: %zu bytes, %s", prefix, scene_bytes.size(), scene.valid ? "valid" : OracleFaultName(scene_why));
  if (scene.valid) {
    float largest = 0.0f;
    size_t negative = 0, above = 0;
    const size_t n = size_t(scene.width) * scene.height * 3;
    for (size_t i = 0; i < n; ++i) {
      const float v = OracleSceneValue(scene, i);
      largest = v > largest ? v : largest;
      negative += v < 0.0f;
      above += v > 31.875f;
    }
    std::printf(", %u x %u, largest value %.6f, negative %zu, above 31.875 %zu, values a half changes %llu", scene.width, scene.height,
                double(largest), negative, above, static_cast<unsigned long long>(OracleSceneHalfChanged(scene)));
  }
  std::printf("\n%s.block: %zu bytes, %s", prefix, block_bytes.size(), block.valid ? "valid" : OracleFaultName(block_why));
  if (block.valid) {
    std::printf(", c20.w %.5f, parts %u, 37B1 (%.4f, %.4f, %.2f, %.5f, %.5f, %.5f), table texels 0, 1, 128, 511: %.5f, %.5f, %.5f, %.5f", double(c20w),
                unsigned(block.parts), double(block.composite[0]), double(block.composite[1]), double(block.composite[2]),
                double(block.composite[3]), double(block.composite[4]), double(block.composite[5]), double(block.table[0]),
                double(block.table[1]), double(block.table[128]), double(block.table[511]));
  } else if (block_why == OracleFault::kBlock) {
    std::printf(" (%s)", fable2::native::capture::PostBlockReasonName(rule));
  }
  std::printf("\n");
  return scene.valid && block.valid ? 0 : 1;
}

int main(int argc, char** argv) {
  for (int (*test)() : {HalfTests, SceneTests, BlockTests, DumpTests}) {
    if (const int code = test()) {
      std::printf("FAIL: post oracle case %d\n", code);
      return 1;
    }
  }
  std::puts("PASS: post oracle files: the half conversion against HalfToFloat (every half, every tie), the scene's and the block's "
            "parsers (short files, magic, size, values that are no numbers, the block's rules, parts and pairing), the dump's rows");
  return argc > 1 ? Report(argv[1]) : 0;
}
