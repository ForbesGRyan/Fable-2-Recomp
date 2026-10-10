#pragma once

// The oracle feed of the native post chain (pure; a diagnostic: fable2_native_post_oracle; frame-map
// section 17, "Oracle feed"; spec ruling 17). No test executes the pass's HLSL. For the post steps,
// which are full-screen steps over one input, the GPU's result can be held against the game's own
// picture pixel for pixel: tools/xdk_sigmatch/post_replay.py `export` writes the scene colour of a
// read-back capture's frame and that frame's post block into two files, the pass uploads the scene
// once and runs its post steps on it in place of its own linear target, with the file's block and
// the live ramp, and the mapped target is read back once into a third file, which post_replay.py
// `compare` holds against the reference's picture and against the game's front buffer.
//
// The files, little-endian:
//   <prefix>.scene  "F2OS", u32 width, u32 height, then width x height x 3 float32: red, green, blue
//                   of each texel, row by row from the top, in the game's scale (colour * c20.w).
//   <prefix>.block  "F2OB", u32 1 (the version), float32 c20.w, then capture::PostBlock's 541 floats
//                   in the struct's order (composite[6], bright[5], blur[2][7], radial[4], table[512]),
//                   u32 parts (kPostPartBlur, kPostPartRadial).
//   the dump        "F2OD", u32 width, u32 height, then width x height x 4 bytes: red, green, blue,
//                   alpha of the mapped target, row by row from the top.
// Everything here fails closed: a file that is short, long, of another kind or size, or that holds a
// value that is no number, gives nothing, and a block is taken only through the capture's own rule
// (capture::MakePostBlock) and the pairing with its scale.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../capture/position_decode.h"
#include "../capture/post_block.h"

namespace fable2::native::render {

// The scene file's size: the pass's logical size (clay_pass.cpp holds them to ClayPass::kWidth and
// kHeight; a file of another size is refused, never scaled).
inline constexpr uint32_t kOracleWidth = 1120, kOracleHeight = 720;
inline constexpr size_t kOracleHeaderBytes = 12;  // a magic and two words, in each of the three files
inline constexpr size_t kOracleBlockFloats = 541;
inline constexpr uint32_t kOracleBlockVersion = 1;
inline constexpr size_t kOracleBlockBytes = 8 + 4 + 4 * kOracleBlockFloats + 4;
static_assert(kOracleBlockFloats * sizeof(float) == offsetof(capture::PostBlock, parts) && kOracleBlockBytes == 2180);
// The mapped target is copied for the dump in the frame this many fed frames after the upload's.
inline constexpr uint32_t kOracleDumpAfter = 30;

// Why a file gave nothing (the log's word).
enum class OracleFault : uint8_t {
  kNone,
  kShort,    // no bytes, or fewer than a header
  kMagic,    // not this kind of file
  kVersion,  // a block file of another version
  kSize,     // the picture is not the wanted size, or the file's length is not its header's
  kValue,    // a value that is no number (a scene texel; a float of the block's radial part)
  kScale,    // the block file's c20.w is no number or not above 0
  kParts,    // an unknown part, or the blur's part named with weights that fail their rule
  kBlock,    // the block fails a rule of capture::MakePostBlock (its reason says which)
  kPairing,  // the block does not pair with the file's c20.w (capture::PostBlockPairs)
  kCount
};
inline const char* OracleFaultName(OracleFault f) {
  static constexpr const char* kNames[] = {"none", "short", "magic", "version", "size", "value", "scale", "parts", "block", "pairing"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(OracleFault::kCount));
  return f < OracleFault::kCount ? kNames[size_t(f)] : "?";
}

namespace oracle_detail {
inline uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline float LeFloat(const uint8_t* p) {
  const uint32_t bits = Le32(p);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
}  // namespace oracle_detail

// A float as a half (IEEE binary16), the inverse of capture::HalfToFloat: the nearest half, a tie to
// the even one; a value at or beyond 65520 is an infinity, one below half the smallest denormal a
// zero of its sign; an infinity stays one and a NaN stays a NaN.
inline uint16_t HalfFromFloat(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  const uint16_t sign = uint16_t((bits >> 16) & 0x8000u);
  const uint32_t exponent = (bits >> 23) & 0xFFu;
  uint32_t mantissa = bits & 0x7FFFFFu;
  if (exponent == 255) return uint16_t(sign | 0x7C00u | (mantissa ? (0x200u | (mantissa >> 13)) : 0u));
  const int e = int(exponent) - 127 + 15;  // the half's biased exponent
  if (e >= 31) return uint16_t(sign | 0x7C00u);
  if (e <= 0) {
    // A half's denormal, n * 2^-24: the float's 24 bits shifted down by 14 - e (14 to 24 here).
    if (e < -10) return sign;
    mantissa |= 0x800000u;
    const uint32_t shift = uint32_t(14 - e);
    uint32_t h = mantissa >> shift;
    const uint32_t rest = mantissa & ((1u << shift) - 1u), half = 1u << (shift - 1);
    if (rest > half || (rest == half && (h & 1u))) ++h;  // (up to 0x400, the smallest normal half)
    return uint16_t(sign | h);
  }
  uint32_t h = (uint32_t(e) << 10) | (mantissa >> 13);
  const uint32_t rest = mantissa & 0x1FFFu;
  if (rest > 0x1000u || (rest == 0x1000u && (h & 1u))) ++h;  // (a carry goes into the exponent, at the top to infinity)
  return uint16_t(sign | h);
}
// The largest half that is a number.
inline constexpr float kHalfMax = 65504.0f;

// A parsed scene file: `values` points into the file's bytes (width * height * 3 little-endian
// floats, not aligned: read them with OracleSceneValue), which must outlive it. Invalid: all zero.
struct OracleScene {
  uint32_t width = 0, height = 0;
  const uint8_t* values = nullptr;
  bool valid = false;
};

// The scene of a file's `n` bytes. Valid only when, tried in this order (`why`, optional, names the
// first that fails): there are a header's bytes (kShort); the magic is "F2OS" (kMagic); the header's
// size is `width` x `height`, neither 0, and n is exactly the header and that many texels (kSize: the
// size must be the pass's logical size); every value is finite (kValue: a NaN or an infinity
// anywhere refuses the file; the steps would read them as 0 and as the clamp, but a file that holds
// one is not what the exporter writes).
inline OracleScene ParseOracleScene(const uint8_t* data, size_t n, OracleFault* why = nullptr, uint32_t width = kOracleWidth,
                                    uint32_t height = kOracleHeight) {
  auto fail = [&](OracleFault f) {
    if (why) *why = f;
    return OracleScene{};
  };
  if (!data || n < kOracleHeaderBytes) return fail(OracleFault::kShort);
  if (std::memcmp(data, "F2OS", 4) != 0) return fail(OracleFault::kMagic);
  if (width == 0 || height == 0 || oracle_detail::Le32(data + 4) != width || oracle_detail::Le32(data + 8) != height) {
    return fail(OracleFault::kSize);
  }
  const uint64_t count = uint64_t(width) * height * 3;
  if (count > (uint64_t(1) << 32) || uint64_t(n) != kOracleHeaderBytes + count * 4) return fail(OracleFault::kSize);
  const uint8_t* p = data + kOracleHeaderBytes;
  for (uint64_t i = 0; i < count; ++i) {
    // (finite: the exponent's bits are not all set)
    if ((oracle_detail::Le32(p + 4 * i) & 0x7F800000u) == 0x7F800000u) return fail(OracleFault::kValue);
  }
  if (why) *why = OracleFault::kNone;
  OracleScene s;
  s.width = width;
  s.height = height;
  s.values = p;
  s.valid = true;
  return s;
}

// Value `i` of a scene (texel i / 3, channel i % 3); 0 for an invalid scene or beyond it.
inline float OracleSceneValue(const OracleScene& s, size_t i) {
  if (!s.valid || i >= size_t(s.width) * s.height * 3) return 0.0f;
  return oracle_detail::LeFloat(s.values + 4 * i);
}

// A value as the texture's half: bounded to the largest half first, so that no infinity is uploaded
// (the steps clamp the scene at 31.875: the bound changes nothing they compute).
inline uint16_t OracleHalf(float v) { return HalfFromFloat(v > kHalfMax ? kHalfMax : (v < -kHalfMax ? -kHalfMax : v)); }

// Row `y` of a scene as R16G16B16A16_FLOAT texels into out[4 * width]: red, green, blue, alpha 1.
// Nothing is written for an invalid scene or a row outside it.
inline void OracleSceneRowHalf(const OracleScene& s, uint32_t y, uint16_t* out) {
  if (!s.valid || y >= s.height || !out) return;
  const uint8_t* p = s.values + size_t(y) * s.width * 12;
  for (uint32_t x = 0; x < s.width; ++x, p += 12, out += 4) {
    out[0] = OracleHalf(oracle_detail::LeFloat(p));
    out[1] = OracleHalf(oracle_detail::LeFloat(p + 4));
    out[2] = OracleHalf(oracle_detail::LeFloat(p + 8));
    out[3] = 0x3C00;
  }
}

// How many of a scene's values the texture does not hold exactly (evidence for the log: the game's
// scene target has 7 bits of mantissa and at most 31.875, so an export of it loses nothing; 0 then).
inline uint64_t OracleSceneHalfChanged(const OracleScene& s) {
  if (!s.valid) return 0;
  const size_t count = size_t(s.width) * s.height * 3;
  uint64_t changed = 0;
  for (size_t i = 0; i < count; ++i) {
    const float v = oracle_detail::LeFloat(s.values + 4 * i);
    if (capture::HalfToFloat(OracleHalf(v)) != v) ++changed;
  }
  return changed;
}

// The table's fetch constant as every capture holds it (post_block.h, kPostTablePin): the file has
// none, and the block is made through the capture's rule with this one.
inline constexpr uint32_t kOracleTableFetch[6] = {0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018};

// The block of a block file's `n` bytes, and its scale in `c20w`. Valid only when, tried in this
// order (`why` names the first that fails): a header's bytes (kShort); the magic "F2OB" (kMagic);
// version 1 (kVersion); n exactly kOracleBlockBytes (kSize); c20.w a number above 0 (kScale); no part
// but the blur's and the radial blur's (kParts); the four floats of the radial part numbers (kValue);
// capture::MakePostBlock taking the registers, the table (as the bytes the guest holds: one float a
// texel, high byte first) and, when the file names the blur's part, the weights (kBlock, with the
// rule in `rule`); the blur's part, when the file names it, made (kParts: its weights fail their
// rule); the block pairing with c20.w (kPairing). The block is then the file's floats bit for bit.
// The radial part is carried by no block before plan Task 12: its bit is accepted and nothing of it
// is taken. Invalid: the block all zero and `c20w` 0.
inline capture::PostBlock ParseOracleBlock(const uint8_t* data, size_t n, float* c20w = nullptr, OracleFault* why = nullptr,
                                           capture::PostBlockReason* rule = nullptr) {
  auto fail = [&](OracleFault f, capture::PostBlockReason r = capture::PostBlockReason::kNone) {
    if (why) *why = f;
    if (rule) *rule = r;
    if (c20w) *c20w = 0.0f;
    return capture::PostBlock{};
  };
  if (!data || n < kOracleHeaderBytes) return fail(OracleFault::kShort);
  if (std::memcmp(data, "F2OB", 4) != 0) return fail(OracleFault::kMagic);
  if (oracle_detail::Le32(data + 4) != kOracleBlockVersion) return fail(OracleFault::kVersion);
  if (n != kOracleBlockBytes) return fail(OracleFault::kSize);
  const float scale = oracle_detail::LeFloat(data + 8);
  if (!(scale > 0.0f) || !std::isfinite(scale)) return fail(OracleFault::kScale);
  float f[kOracleBlockFloats];
  for (size_t i = 0; i < kOracleBlockFloats; ++i) f[i] = oracle_detail::LeFloat(data + 12 + 4 * i);
  const uint32_t parts = oracle_detail::Le32(data + 12 + 4 * kOracleBlockFloats);
  if (parts & ~uint32_t(capture::kPostPartBlur | capture::kPostPartRadial)) return fail(OracleFault::kParts);
  for (int i = 25; i < 29; ++i) {
    if (!std::isfinite(f[i])) return fail(OracleFault::kValue);
  }
  const bool blur = (parts & capture::kPostPartBlur) != 0;
  capture::PostRegisters regs{};
  regs.composite[0][0] = f[0], regs.composite[1][0] = f[1], regs.composite[2][0] = f[2];
  regs.composite[3][0] = f[3], regs.composite[3][1] = f[4], regs.composite[4][0] = f[5];
  regs.bright[0][0] = f[6], regs.bright[1][0] = f[7], regs.bright[2][0] = f[8];
  regs.bright[3][0] = f[9], regs.bright[3][1] = f[10];
  for (int s = 0; s < 2 && blur; ++s) {
    for (int i = 0; i < 7; ++i) regs.blur[s][i >> 2][i & 3] = f[11 + 7 * s + i];
  }
  regs.have_composite = regs.have_bright = true;
  regs.have_blur[0] = regs.have_blur[1] = blur;
  uint8_t table[capture::kPostTableBytes];
  for (uint32_t i = 0; i < capture::kPostTableTexels; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &f[29 + i], sizeof(bits));
    for (int k = 0; k < 4; ++k) table[4 * i + k] = uint8_t(bits >> (24 - 8 * k));
  }
  capture::PostBlockReason reason = capture::PostBlockReason::kNone;
  const capture::PostBlock b = capture::MakePostBlock(regs, kOracleTableFetch, table, sizeof(table), &reason);
  if (!b.valid) return fail(OracleFault::kBlock, reason);
  if (blur && !(b.parts & capture::kPostPartBlur)) return fail(OracleFault::kParts);
  if (!capture::PostBlockPairs(b, scale)) return fail(OracleFault::kPairing);
  if (why) *why = OracleFault::kNone;
  if (rule) *rule = capture::PostBlockReason::kNone;
  if (c20w) *c20w = scale;
  return b;
}

// What the pass is handed with the switch set and both files valid (ClayPostRequest::oracle; the
// files' bytes are the caller's and outlive every Render).
struct OracleFeed {
  OracleScene scene;
  capture::PostBlock block;
  float c20w = 0.0f;
};

// The row pitch of the read-back copy of a `width`-texel row of 4 bytes: the next multiple of 256
// (nrhi::kRowPitchAlignment). 0 for no width or one whose pitch does not fit 32 bits.
inline constexpr uint32_t OracleDumpPitch(uint32_t width) {
  const uint64_t pitch = (uint64_t(width) * 4 + 255) / 256 * 256;
  return pitch > 0xFFFFFFFFull ? 0u : uint32_t(pitch);
}

// The dump file's bytes from the read-back rows (`height` rows of `pitch` bytes, the first 4 * width
// of each the row's texels): the header and the rows without their padding. False, with `out` empty,
// for no rows, no size or a pitch below a row.
inline bool PackOracleDump(const uint8_t* rows, uint32_t pitch, uint32_t width, uint32_t height, std::vector<uint8_t>* out) {
  if (!out) return false;
  out->clear();
  if (!rows || width == 0 || height == 0 || uint64_t(pitch) < uint64_t(width) * 4) return false;
  const size_t row = size_t(width) * 4;
  out->resize(kOracleHeaderBytes + row * height);
  uint8_t* p = out->data();
  std::memcpy(p, "F2OD", 4);
  for (int k = 0; k < 4; ++k) {
    p[4 + k] = uint8_t(width >> (8 * k));
    p[8 + k] = uint8_t(height >> (8 * k));
  }
  for (uint32_t y = 0; y < height; ++y) std::memcpy(p + kOracleHeaderBytes + row * y, rows + size_t(pitch) * y, row);
  return true;
}

}  // namespace fable2::native::render
