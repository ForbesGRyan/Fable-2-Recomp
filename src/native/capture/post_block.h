#pragma once

// The frame's post block (pure): what the native pass needs of the game's post
// chain (frame-map section 17, "Post block"; docs/native-renderer/
// lighting-formulas.md, "The post chain"). The game ends a frame with a row of
// full-screen rectangles outside the main scene: a bright pass, two 13-tap
// blurs and one composite, whose pixel shader maps the scene to the display
// through a 1D table of 512 floats that the CPU rewrites in guest memory every
// frame (the exposure's gain by scene luminance; fetch slot 1, "tf1"). The
// block holds those shaders' registers of one frame and that table.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fog_block.h"

namespace fable2::native::capture {

// The post chain's pixel shaders (tools/xdk_sigmatch/light_formulas.py,
// POST_SHADERS): a hooked BeginVertices call outside the main scene that runs
// one of them is that step's rectangle. The radial blur's registers are read
// only with the quads wanted (fable2_native_sun; SetPostRadial).
inline constexpr uint64_t kPostCompositePs = 0x37B186F4BD3288F7ull, kPostBrightPs = 0xEE922CFB4426EA83ull,
                          kPostBlurXPs = 0xEA83C7E7E24EC4C8ull, kPostBlurYPs = 0x28DBE18F313350BFull,
                          kPostRadialPs = 0x4BE4344C27016E04ull;
// kPostTableSlot: the composite's fetch slot of the table. kPostBlockMaxAge: a
// frame that read no block of its own may take the last valid one while that
// one is at most this many frames old (ChoosePostBlock).
inline constexpr uint32_t kPostTableSlot = 1, kPostTableTexels = 512, kPostBlockMaxAge = 4;
// The table's bytes from its fetch constant's base address: 512 texels of one float.
inline constexpr size_t kPostTableBytes = 2048;
// PostBlock::parts: the blurs' weights are there; the radial blur's registers are (SetPostRadial).
inline constexpr uint8_t kPostPartBlur = 1, kPostPartRadial = 2;

// Reason string (the plan's): a frame with the post chain wanted that has no post block.
inline constexpr const char* kPostBlockReason = "post-block";

// composite: 37B1's registers. With L = lum(scene) (0.2125, 0.7154, 0.0721):
//   e = c77.y * T(c46.x * L);  c = sat(e * (L + sat(c47.x * L) * (scene - L)))
//   b = sat(bloom * c31.x);  s = c + b * (1 - c);  t = s + c77.x * (s - lum(s));  out = |t| ^ c78.x
// bright: EE92's: out = (d * sat(L * c77.y - c77.x) / L * c31.x + tf1 * 6 * c46.x + tf2) / c47.x.
//   Its c77.y is 1 / c20.w, the scene's output scale, in every captured frame: the block's tie to
//   the frame's light block (PostBlockPairs).
// blur: the 13 point taps' weights of EA83 (along x) and 28DB (along y): the centre's, then those
//   of the taps 1 to 6 texels away on either side. All zero without kPostPartBlur.
// radial: 4BE4's (SetPostRadial); all zero without kPostPartRadial.
// table: T, decoded: texel i is the float stored high byte first at byte 4 * i. The composite
//   reads it at the NEAREST texel, floor(u * 512 + 0.5) clamped to 511 (controller ruling 19:
//   the fetch's OffsetX = 0.5), not at floor(u * 512); the block carries the texels, the
//   addressing is the reader's (clay_logic.h from plan Task 5 on).
// An invalid block is all zero: nothing of it may be used.
struct PostBlock {
  float composite[6];   // 37B1: c31.x, c46.x, c47.x, c77.x, c77.y, c78.x
  float bright[5];      // EE92: c31.x, c46.x, c47.x, c77.x, c77.y
  float blur[2][7];     // EA83 then 28DB: c46.x (centre), c46.y, .z, .w, c47.x, .y, .z
  float radial[4];      // 4BE4: c31.x, c31.y, c46.x, c47.x
  float table[512];     // the composite's table, one big-endian float a texel
  uint8_t parts;        // kPostPartBlur, kPostPartRadial
  bool valid;
};
// 541 floats, the parts and the flag: one is copied into each frame's scene (FrameScene::post).
static_assert(sizeof(PostBlock) == 2168, "PostBlock grew or shrank: the documents state its size");
static_assert(sizeof(PostBlock::table) == kPostTableBytes && kPostTableTexels * 4 == kPostTableBytes);

// What was read at the frame's post rectangles, as host floats (their bits are the guest's),
// whole registers: MakePostBlock takes the components the shaders read.
struct PostRegisters {
  float composite[5][4];   // 37B1's c31, c46, c47, c77, c78
  float bright[4][4];      // EE92's c31, c46, c47, c77
  float blur[2][2][4];     // EA83's c46, c47; 28DB's c46, c47
  bool have_composite, have_bright, have_blur[2];
};

// The pixel registers of each step, in the order of PostRegisters' rows. None of them is a
// register a LoadAluConstants call can name (light_block.h, kBufferLoadRanges: pixel c0..c7 and
// c80..c115): the device's bank holds their value at the step's rectangle.
inline constexpr uint32_t kPostCompositeRegisters[5] = {31, 46, 47, 77, 78};
inline constexpr uint32_t kPostBrightRegisters[4] = {31, 46, 47, 77};
inline constexpr uint32_t kPostBlurRegisters[2] = {46, 47};

// What the table's fetch constant must be for its bytes to be read as MakePostBlock reads them:
// 512 texels of k_32_FLOAT (36) with the number format bit 1, endian 8-in-32, swizzle 0xB68, all
// four sign fields 1. The captured constant, the same in every frame of every capture of
// frame-map section 17: 04000956 1FAA00A4 000001FF 010016D1 00000000 00000018.
inline constexpr FogTablePin kPostTablePin = {512, 36, 2, 0xB68, 0x55, 1};

// The fault of the table's fetch constant against its pin (never kBytes): the fog tables' rules
// in the fog's order (fog_block.h, FogTableFetchFault), but for the filter, which is POINT here
// (dword 3 bits 19, 20 and 21, 22 both 0): the composite reads single texels. A null constant
// is no texture.
inline FogTableFault PostTableFetchFault(const uint32_t fc[6]) {
  const FogTablePin& pin = kPostTablePin;
  if (!fc || (fc[0] & 3) != 2 || ((fc[5] >> 9) & 3) != 0) return FogTableFault::kDimension;
  if ((fc[1] & 0xFFFFF000u) == 0) return FogTableFault::kBase;
  if ((fc[2] & 0xFFFFFFu) + 1 != pin.width) return FogTableFault::kWidth;
  if ((fc[1] & 0x3F) != pin.format || (fc[3] & 1) != pin.num_format) return FogTableFault::kFormat;
  if (((fc[1] >> 6) & 3) != pin.endian) return FogTableFault::kEndian;
  if (((fc[3] >> 1) & 0xFFF) != pin.swizzle) return FogTableFault::kSwizzle;
  if (((fc[0] >> 2) & 0xFF) != pin.signs) return FogTableFault::kSign;
  if (((fc[3] >> 13) & 0x3F) != 0) return FogTableFault::kExpAdjust;
  if (((fc[0] >> 10) & 7) != 2) return FogTableFault::kClamp;
  if (((fc[3] >> 19) & 3) != 0 || ((fc[3] >> 21) & 3) != 0) return FogTableFault::kFilter;
  if (((fc[4] >> 6) & 0xF) != 0) return FogTableFault::kMip;
  return FogTableFault::kNone;
}

// Why a frame has no post block (FrameScene::post_reason); kNone: it has one.
enum class PostBlockReason : uint8_t {
  kNone,
  kOff,       // the post chain was not wanted this frame (the view, the light or the post switch off): nothing was read
  kNoDraw,    // no rectangle of the frame ran the composite's pixel shader (a menu, a loading screen), and none is kept
  kUnread,    // a register, the fetch constant or the table's memory could not be read, or the bright pass was not seen
  kRegister,  // a component the block takes is not finite
  kRange,     // a register outside its bound (MakePostBlock)
  kPairing,   // the bright pass's c77.y times the frame's c20.w is not 1 within 1e-3, or the frame has no light block
  // The table's fetch constant against its pin (FogTableFault, in its order), then its bytes.
  kTableDimension, kTableBase, kTableWidth, kTableFormat, kTableEndian, kTableSwizzle, kTableSign,
  kTableExpAdjust, kTableClamp, kTableFilter, kTableMip, kTableBytes,
  kTableTexel,  // a texel is not finite or outside [0, 4)
  kStale,       // no block was read this frame and the kept one is older than kPostBlockMaxAge frames
  kCount
};

// The block's reason for a fault of the table.
inline constexpr PostBlockReason PostTableReason(FogTableFault fault) {
  if (fault == FogTableFault::kNone || fault >= FogTableFault::kCount) return PostBlockReason::kNone;
  return PostBlockReason(int(PostBlockReason::kTableDimension) + int(fault) - 1);
}
static_assert(PostTableReason(FogTableFault::kDimension) == PostBlockReason::kTableDimension &&
              PostTableReason(FogTableFault::kFilter) == PostBlockReason::kTableFilter &&
              PostTableReason(FogTableFault::kBytes) == PostBlockReason::kTableBytes);

inline const char* PostBlockReasonName(PostBlockReason r) {
  static constexpr const char* kNames[] = {
      "none", "off", "no-draw", "unread", "register", "range", "pairing",
      "table-dimension", "table-base", "table-width", "table-format", "table-endian", "table-swizzle", "table-sign",
      "table-exp-adjust", "table-clamp", "table-filter", "table-mip", "table-bytes",
      "table-texel", "stale"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(PostBlockReason::kCount));
  return r < PostBlockReason::kCount ? kNames[size_t(r)] : "?";
}

// The frame's post block from what was read at its post rectangles.
// regs: the steps' registers (PostRegisters). fetch1: the fetch constant of slot 1 at the
// composite, host-order dwords. table_bytes, n: the n bytes read at fetch1's base address.
// Valid only when, tried in this order (`why`, optional, names the first that fails):
//   - the composite's and the bright pass's registers were read and fetch1 is not null (kUnread);
//   - every component the block takes of those two is finite (kRegister);
//   - composite c31.x in [0, 64], c46.x in (0, 64], c47.x in [0, 1e6], |c77.x| <= 4, c77.y in
//     (0, 64], c78.x in (0, 1]; bright c31.x in [0, 64], c46.x in [0, 64], c47.x in [1e-3, 64] (a
//     divisor), |c77.x| <= 4, c77.y in (0, 64] (kRange). Measured (frame-map section 17, the 128
//     frames of plan Task 1's runs under read-back and the blind frames of a normal run):
//     composite 2.97, 0.25, 50, -0.005, 1.10, 0.4545; bright 1.49, 0.2, 2, 0.086 to 0.089, and
//     c77.y 0.0625 (blind) to 0.1315;
//   - the fetch constant is the table's pin (PostTableFetchFault: 1D, an address, width 512,
//     format 36 with its number format bit, endian, swizzle, sign fields, exponent adjust 0,
//     clamp to edge, point filter, highest mip level 0);
//   - n is 2,048, with the bytes there (kTableBytes);
//   - every texel is finite and in [0, 4) (kTableTexel; measured: texel 0 is 0, the largest
//     texel 0.14 blind and 0.21 to 0.37 under read-back).
// The blur part (kPostPartBlur) is set, and the fourteen weights taken, when both blurs were
// read, every weight is in [0, 1] and each set's w0 + 2 (w1 + .. + w6) is 1 within 1e-3; without
// it the block is still valid and its blur is zero (a consumer then draws no bloom). A weight
// that is no number fails its bound, so none reaches the block.
// The pairing with the frame's light block is not judged here (PostBlockPairs: the frame's
// decision). An invalid block is all zero.
inline PostBlock MakePostBlock(const PostRegisters& regs, const uint32_t fetch1[6], const uint8_t* table_bytes, size_t n,
                               PostBlockReason* why) {
  auto fail = [&](PostBlockReason r) {
    if (why) *why = r;
    return PostBlock{};
  };
  if (!regs.have_composite || !regs.have_bright || !fetch1) return fail(PostBlockReason::kUnread);
  const float (*c)[4] = regs.composite;
  const float (*h)[4] = regs.bright;
  const float composite[6] = {c[0][0], c[1][0], c[2][0], c[3][0], c[3][1], c[4][0]};
  const float bright[5] = {h[0][0], h[1][0], h[2][0], h[3][0], h[3][1]};
  for (float v : composite) {
    if (!std::isfinite(v)) return fail(PostBlockReason::kRegister);
  }
  for (float v : bright) {
    if (!std::isfinite(v)) return fail(PostBlockReason::kRegister);
  }
  if (!(composite[0] >= 0.0f && composite[0] <= 64.0f) || !(composite[1] > 0.0f && composite[1] <= 64.0f) ||
      !(composite[2] >= 0.0f && composite[2] <= 1e6f) || !(std::fabs(composite[3]) <= 4.0f) ||
      !(composite[4] > 0.0f && composite[4] <= 64.0f) || !(composite[5] > 0.0f && composite[5] <= 1.0f)) {
    return fail(PostBlockReason::kRange);
  }
  if (!(bright[0] >= 0.0f && bright[0] <= 64.0f) || !(bright[1] >= 0.0f && bright[1] <= 64.0f) ||
      !(bright[2] >= 1e-3f && bright[2] <= 64.0f) || !(std::fabs(bright[3]) <= 4.0f) ||
      !(bright[4] > 0.0f && bright[4] <= 64.0f)) {
    return fail(PostBlockReason::kRange);
  }
  if (const FogTableFault f = PostTableFetchFault(fetch1); f != FogTableFault::kNone) return fail(PostTableReason(f));
  if (!table_bytes || n != kPostTableBytes) return fail(PostBlockReason::kTableBytes);
  PostBlock b{};
  for (uint32_t i = 0; i < kPostTableTexels; ++i) {
    const uint8_t* p = table_bytes + 4 * i;
    const uint32_t bits = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    float texel;
    std::memcpy(&texel, &bits, sizeof(texel));
    // (written so that a texel that is no number fails it)
    if (!(texel >= 0.0f && texel < 4.0f)) return fail(PostBlockReason::kTableTexel);
    b.table[i] = texel;
  }
  std::memcpy(b.composite, composite, sizeof(b.composite));
  std::memcpy(b.bright, bright, sizeof(b.bright));
  if (regs.have_blur[0] && regs.have_blur[1]) {
    float weights[2][7];
    bool ok = true;
    for (int s = 0; s < 2; ++s) {
      const float (*w)[4] = regs.blur[s];
      const float set[7] = {w[0][0], w[0][1], w[0][2], w[0][3], w[1][0], w[1][1], w[1][2]};
      double sum = set[0];
      for (int i = 0; i < 7; ++i) {
        ok = ok && set[i] >= 0.0f && set[i] <= 1.0f;
        if (i) sum += 2.0 * set[i];
      }
      ok = ok && std::fabs(sum - 1.0) <= 1e-3;
      std::memcpy(weights[s], set, sizeof(set));
    }
    if (ok) {
      std::memcpy(b.blur, weights, sizeof(b.blur));
      b.parts |= kPostPartBlur;
    }
  }
  b.valid = true;
  if (why) *why = PostBlockReason::kNone;
  return b;
}
inline PostBlock MakePostBlock(const PostRegisters& regs, const uint32_t fetch1[6], const uint8_t* table_bytes, size_t n) {
  return MakePostBlock(regs, fetch1, table_bytes, n, nullptr);
}

// Whether a block belongs to a frame whose scene wrote with the output scale `out_scale` (the
// frame's light block's, pixel c20.w): the bright pass's c77.y times it is 1 within 1e-3 (spec
// ruling 11; within 6e-8 in every captured frame, also while the exposure moves). False for an
// invalid block and for a scale that is no number or 0 (a frame without a light block).
inline bool PostBlockPairs(const PostBlock& block, float out_scale) {
  return block.valid && std::fabs(double(block.bright[4]) * double(out_scale) - 1.0) <= 1e-3;
}

// The radial blur's registers (4BE4, two rectangles a frame with the same registers: the sun
// layer's beams, plan Task 12) into a valid block: radial = c31.x, c31.y (the blur's centre in the
// picture's units: the sun's place, (0.5, 0.5) with the sun behind the camera), c46.x and c47.x
// (the two weights; 0.87 and 0.935 in every captured frame), and the part kPostPartRadial.
// Taken only when c31.x and c31.y are finite and in [-4, 4] (a sun far outside the picture) and
// c46.x and c47.x are in (0, 1]; otherwise, and for an invalid block or a null argument, false,
// with the block's four values zero and the part clear (a consumer then draws no sun layer). The
// other components of the three registers are not read.
inline bool SetPostRadial(PostBlock* block, const float c31[4], const float c46[4], const float c47[4]) {
  if (!block) return false;
  for (float& v : block->radial) v = 0.0f;
  block->parts &= uint8_t(~kPostPartRadial);
  if (!block->valid || !c31 || !c46 || !c47) return false;
  // (written so that a value that is no number fails its bound)
  if (!(c31[0] >= -4.0f && c31[0] <= 4.0f) || !(c31[1] >= -4.0f && c31[1] <= 4.0f) || !(c46[0] > 0.0f && c46[0] <= 1.0f) ||
      !(c47[0] > 0.0f && c47[0] <= 1.0f)) {
    return false;
  }
  block->radial[0] = c31[0];
  block->radial[1] = c31[1];
  block->radial[2] = c46[0];
  block->radial[3] = c47[0];
  block->parts |= kPostPartRadial;
  return true;
}
inline constexpr uint32_t kPostRadialRegisters[3] = {31, 46, 47};

// Where a frame's post block's table was read, for evidence only (the log and the discovery
// frame row; no shader input): the base address of the fetch constant and the XXH3 hash of the
// 2,048 bytes that were decoded, which is the "content" a discovery capture names the table by
// in the composite row's "tfc". Zero without a block.
struct PostSource { uint32_t base = 0; uint64_t content = 0; };

// Which block a frame gets (FrameBuilder::Finish; pure).
enum class PostPick : uint8_t { kNone, kOwn, kKept };
struct PostChoice {
  PostPick pick;
  uint32_t age;            // 0 for the frame's own block, the kept one's age otherwise (0 without a block)
  PostBlockReason reason;  // kNone with a block
};
// own: the block read at this frame's composite (invalid: none was read). own_tries, own_reason:
//   the reads of this frame that gave none, and the last one's reason.
// kept, kept_age: the last valid block read in an earlier frame and how many frames ago (1: the
//   frame before); an invalid `kept`: there is none.
// out_scale: the frame's light block's c20.w, 0 for a frame without a valid light block.
// The frame's own block if it is valid and pairs with out_scale; else the kept one if it is at
// most kPostBlockMaxAge frames old and pairs; else none, with the reason: kPairing when a block
// was at hand (the frame's own, or a kept one young enough) and none paired, which is also what
// a frame without a light block gets when a block was at hand (no scale to pair with); else the
// frame's last failed read's reason; else kStale for a kept block that is too old; else kNoDraw.
// A block from another frame is never taken beyond the bound, and never without the pairing.
inline PostChoice ChoosePostBlock(const PostBlock& own, uint32_t own_tries, PostBlockReason own_reason, const PostBlock& kept,
                                  uint32_t kept_age, float out_scale) {
  if (own.valid && PostBlockPairs(own, out_scale)) return {PostPick::kOwn, 0, PostBlockReason::kNone};
  const bool kept_young = kept.valid && kept_age >= 1 && kept_age <= kPostBlockMaxAge;
  if (kept_young && PostBlockPairs(kept, out_scale)) return {PostPick::kKept, kept_age, PostBlockReason::kNone};
  if (own.valid || kept_young) return {PostPick::kNone, 0, PostBlockReason::kPairing};
  if (own_tries) return {PostPick::kNone, 0, own_reason};
  if (kept.valid) return {PostPick::kNone, 0, PostBlockReason::kStale};
  return {PostPick::kNone, 0, PostBlockReason::kNoDraw};
}

}  // namespace fable2::native::capture
