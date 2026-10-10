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
//   <prefix>.sun    (optional; plan Task 14: the sun layer on the feed) "F2OU", u32 1 (the version), u32
//                   width, u32 height, u32 count, then width x height float32: the scene's depth copy as
//                   the PASS holds a depth, z / w with 0 near and 1 far (the exporter turns the game's
//                   reversed window depth round), row by row from the top; then `count` records of the
//                   frame's sun-layer quads, each kOracleQuadBytes: what the capture read at the call
//                   (capture::QuadInputs), from which the record is made by the capture's own
//                   capture::MakeQuadRecord.
//   the layer dumps "F2OL", u32 width, u32 height, then width x height little-endian words of
//                   R10G10B10A2_UNORM: layer 1, and the bright pass's tf1 and tf2.
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
#include "../capture/quad_record.h"

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
// The sun layer's pictures read back with it when the feed drew the layer: layer 1, tf1, tf2.
inline constexpr size_t kOracleLayerDumps = 3;

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
  kRadial,   // the block file names the radial part with registers that fail their rule (capture::SetPostRadial)
  kQuad,     // a record of the sun file is of no layer kind, or capture::MakeQuadRecord refuses it
  kCount
};
inline const char* OracleFaultName(OracleFault f) {
  static constexpr const char* kNames[] = {"none", "short", "magic", "version", "size", "value", "scale", "parts", "block", "pairing", "radial",
                                           "quad"};
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
// rule); the block pairing with c20.w (kPairing); the radial part, when the file names it, made by the
// capture's own rule (capture::SetPostRadial; kRadial when its registers fail it). The block is then the
// file's floats bit for bit. Invalid: the block all zero and `c20w` 0.
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
  capture::PostBlock b = capture::MakePostBlock(regs, kOracleTableFetch, table, sizeof(table), &reason);
  if (!b.valid) return fail(OracleFault::kBlock, reason);
  if (blur && !(b.parts & capture::kPostPartBlur)) return fail(OracleFault::kParts);
  if (!capture::PostBlockPairs(b, scale)) return fail(OracleFault::kPairing);
  if (parts & capture::kPostPartRadial) {
    // (4BE4's c31.xy, c46.x, c47.x, through the capture's rule: plan Task 14, the sun layer on the feed)
    const float c31[4] = {f[25], f[26], 0.0f, 0.0f}, c46[4] = {f[27], 0.0f, 0.0f, 0.0f}, c47[4] = {f[28], 0.0f, 0.0f, 0.0f};
    if (!capture::SetPostRadial(&b, c31, c46, c47)) return fail(OracleFault::kRadial);
  }
  if (why) *why = OracleFault::kNone;
  if (rule) *rule = capture::PostBlockReason::kNone;
  if (c20w) *c20w = scale;
  return b;
}

// The sun file (plan Task 14). One record's bytes, little-endian: u32 the pixel shader's hash (low), u32
// (high); 80 bytes of vertices as the builder's storage holds them (guest order); 16 float32 vertex
// c0..c3; u32 the count of pixel registers, then 12 x 4 float32; u32 the count of fetch constants, then
// 3 x 6 u32; u32 blend, depth, colour mask, colour control, the alpha reference's bits, the viewport Z
// scale's bits, PA_CL_VTE_CNTL.
inline constexpr uint32_t kOracleSunVersion = 1;
inline constexpr size_t kOracleSunHeaderBytes = 20;
inline constexpr size_t kOracleQuadBytes = 8 + 80 + 64 + 4 + 192 + 4 + 72 + 28;
static_assert(kOracleQuadBytes == 452);
// A parsed sun file: `depth` points into the file's bytes (width * height little-endian floats, read
// with OracleSunDepth), which must outlive it; `quads` are the layer's records in the file's order,
// each made by capture::MakeQuadRecord. Invalid: all zero.
struct OracleSun {
  uint32_t width = 0, height = 0;
  const uint8_t* depth = nullptr;
  uint32_t count = 0;
  capture::QuadRecord quads[capture::kMaxQuadRecords] = {};
  bool valid = false;
};
// The sun part of a file's `n` bytes. Valid only when, tried in this order: a header's bytes (kShort);
// the magic "F2OU" (kMagic); version 1 (kVersion); the header's size is `width` x `height`, the count is
// at most capture::kMaxQuadRecords and n is exactly the header, the depth and that many records
// (kSize); every depth value is a number in [0, 1] (kValue); every record's pixel shader is a layer
// kind's of the capture's table, its two counts are that kind's, and capture::MakeQuadRecord takes it
// (kQuad, with the capture's reason in `rule`).
inline OracleSun ParseOracleSun(const uint8_t* data, size_t n, OracleFault* why = nullptr, capture::QuadRecordReason* rule = nullptr,
                                uint32_t width = kOracleWidth, uint32_t height = kOracleHeight) {
  auto fail = [&](OracleFault f, capture::QuadRecordReason r = capture::QuadRecordReason::kNone) {
    if (why) *why = f;
    if (rule) *rule = r;
    return OracleSun{};
  };
  if (!data || n < kOracleSunHeaderBytes) return fail(OracleFault::kShort);
  if (std::memcmp(data, "F2OU", 4) != 0) return fail(OracleFault::kMagic);
  if (oracle_detail::Le32(data + 4) != kOracleSunVersion) return fail(OracleFault::kVersion);
  const uint32_t count = oracle_detail::Le32(data + 16);
  if (width == 0 || height == 0 || oracle_detail::Le32(data + 8) != width || oracle_detail::Le32(data + 12) != height ||
      count > capture::kMaxQuadRecords) {
    return fail(OracleFault::kSize);
  }
  const uint64_t texels = uint64_t(width) * height;
  if (texels > (uint64_t(1) << 30) || uint64_t(n) != kOracleSunHeaderBytes + texels * 4 + uint64_t(count) * kOracleQuadBytes) {
    return fail(OracleFault::kSize);
  }
  const uint8_t* depth = data + kOracleSunHeaderBytes;
  for (uint64_t i = 0; i < texels; ++i) {
    const float d = oracle_detail::LeFloat(depth + 4 * i);
    if (!(d >= 0.0f && d <= 1.0f)) return fail(OracleFault::kValue);
  }
  OracleSun s;
  const uint8_t* p = depth + texels * 4;
  for (uint32_t q = 0; q < count; ++q, p += kOracleQuadBytes) {
    const uint64_t ps = uint64_t(oracle_detail::Le32(p)) | (uint64_t(oracle_detail::Le32(p + 4)) << 32);
    const capture::PostQuadEntry* entry = capture::FindPostQuad(ps);
    if (!entry || entry->layer != 1) return fail(OracleFault::kQuad, capture::QuadRecordReason::kShader);
    float view_proj[4][4], regs[12][4];
    uint32_t fetch[3][6];
    for (int i = 0; i < 16; ++i) view_proj[i / 4][i % 4] = oracle_detail::LeFloat(p + 88 + 4 * i);
    const uint32_t reg_count = oracle_detail::Le32(p + 152);
    for (int i = 0; i < 48; ++i) regs[i / 4][i % 4] = oracle_detail::LeFloat(p + 156 + 4 * i);
    const uint32_t fetch_count = oracle_detail::Le32(p + 348);
    for (int i = 0; i < 18; ++i) fetch[i / 6][i % 6] = oracle_detail::Le32(p + 352 + 4 * i);
    capture::QuadInputs in{};
    in.vertices = p + 8;
    in.vertex_bytes = 80;
    in.stride = capture::kQuadStride;
    in.indices = capture::kQuadIndices;
    in.index_count = 6;
    in.view_proj = view_proj;
    in.ps = regs;
    in.ps_count = reg_count;
    in.fetch = fetch;
    in.fetch_count = fetch_count;
    in.blend = oracle_detail::Le32(p + 424);
    in.depth = oracle_detail::Le32(p + 428);
    in.colour_mask = oracle_detail::Le32(p + 432);
    in.colour_control = oracle_detail::Le32(p + 436);
    in.alpha_ref = oracle_detail::Le32(p + 440);
    in.vport_z_scale = oracle_detail::Le32(p + 444);
    in.vte_cntl = oracle_detail::Le32(p + 448);
    capture::QuadRecordReason reason = capture::QuadRecordReason::kNone;
    s.quads[q] = capture::MakeQuadRecord(*entry, in, &reason);
    if (!capture::QuadRecordValid(s.quads[q])) return fail(OracleFault::kQuad, reason);
    s.quads[q].sequence = uint16_t(q);
  }
  if (why) *why = OracleFault::kNone;
  if (rule) *rule = capture::QuadRecordReason::kNone;
  s.width = width;
  s.height = height;
  s.depth = depth;
  s.count = count;
  s.valid = true;
  return s;
}
// Depth value `i` of a sun part (row i / width); 0, the nearest depth, for an invalid part or beyond it.
inline float OracleSunDepth(const OracleSun& s, size_t i) {
  if (!s.valid || i >= size_t(s.width) * s.height) return 0.0f;
  return oracle_detail::LeFloat(s.depth + 4 * i);
}
// Whether two fetch constants name the same kind of texture at possibly another place: every bit equal
// but the two addresses (dword 1 bits 12 to 31, the base; dword 5 bits 12 to 31, the mips). A file's quad
// takes its texture from the LIVE frame's quad of the same kind and place among that kind, and only when
// this holds: the save loads its textures at other addresses in another run (the glare's base was
// 0x14E60000, 0x14EC0000 and 0x14DE0000 in three runs), so a file's address means nothing in the run that
// reads it. That the live texture's content is the captured one is inference; the comparison of the
// layer's dumps against the reference, which reads the capture's own texture dump, is its check.
inline bool OracleFetchSameKind(const uint32_t a[6], const uint32_t b[6]) {
  return a[0] == b[0] && ((a[1] ^ b[1]) & 0xFFFu) == 0 && a[2] == b[2] && a[3] == b[3] && a[4] == b[4] && ((a[5] ^ b[5]) & 0xFFFu) == 0;
}

// What the pass is handed with the switch set and both files valid (ClayPostRequest::oracle; the
// files' bytes are the caller's and outlive every Render). `sun`: the optional third file's part; not
// valid without the file, and then the feed has no sun layer.
struct OracleFeed {
  OracleScene scene;
  capture::PostBlock block;
  float c20w = 0.0f;
  OracleSun sun;
};

// The row pitch of the read-back copy of a `width`-texel row of 4 bytes: the next multiple of 256
// (nrhi::kRowPitchAlignment). 0 for no width or one whose pitch does not fit 32 bits.
inline constexpr uint32_t OracleDumpPitch(uint32_t width) {
  const uint64_t pitch = (uint64_t(width) * 4 + 255) / 256 * 256;
  return pitch > 0xFFFFFFFFull ? 0u : uint32_t(pitch);
}

// The dump file's bytes from the read-back rows (`height` rows of `pitch` bytes, the first 4 * width
// of each the row's texels): the header and the rows without their padding. False, with `out` empty,
// for no rows, no size or a pitch below a row. `magic`: four characters, "F2OD" for the mapped target's
// bytes, "F2OL" for a layer's 10-bit words (anything else counts as "F2OD").
inline bool PackOracleDump(const uint8_t* rows, uint32_t pitch, uint32_t width, uint32_t height, std::vector<uint8_t>* out,
                           const char* magic = "F2OD") {
  if (!out) return false;
  out->clear();
  if (!rows || width == 0 || height == 0 || uint64_t(pitch) < uint64_t(width) * 4) return false;
  const size_t row = size_t(width) * 4;
  out->resize(kOracleHeaderBytes + row * height);
  uint8_t* p = out->data();
  std::memcpy(p, "F2OD", 4);
  if (magic && std::strlen(magic) == 4) std::memcpy(p, magic, 4);
  for (int k = 0; k < 4; ++k) {
    p[4 + k] = uint8_t(width >> (8 * k));
    p[8 + k] = uint8_t(height >> (8 * k));
  }
  for (uint32_t y = 0; y < height; ++y) std::memcpy(p + kOracleHeaderBytes + row * y, rows + size_t(pitch) * y, row);
  return true;
}

}  // namespace fable2::native::render
