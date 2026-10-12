// The far woods' impostors: a picture's lower levels and whether a picture read from guest memory may be used
// (sub-project 12, plan Task 13; docs/native-renderer/frame-map.md section 19, "The pictures: levels and
// content"). Pure: no SDK header, no game; tests/native/test_impostor_picture.cpp. The Python twin and the fit
// are tools/xdk_sigmatch/impostor_pictures.py (make_levels, content_stats, picture_usable; `levels`, `content`).
//
// A picture is the game's own 256 x 256 render of one tree model, format 3 (k_1_5_5_5), decoded to RGBA8 by
// render::Decode1555Level (texture_decode.h): each channel one of 32 bytes, alpha 0 or 255. The emulator's forced
// read-back brings the base level to guest memory every frame and NOT the three stored levels the game makes from
// it by copy passes (measured: frame-map, "The cost pair"), so the native view makes them (spec ruling 8).
//
// The levels' rule, fitted to the levels the game stores (captures D1 and D2 under full read-back, 56 pictures:
// out/native-evidence/distant-trees/t13_levels.txt): a level is made from the level above it; each five-bit
// field is the mean of ALL four parents' fields, covered or not, a half rounded up; a texel is covered when at
// least three of its four parents are (more than half). Of the sixteen rules tried this one alone gives the
// game's alpha bit on every texel of every level of every picture, and its colour within 2 of 31 on every texel
// the game's level covers (spec criterion 5: 97% and 95%).
//
// The content rule (spec ruling 7; ledger ruling 45), held to 374 whole bases of eighteen captures
// (t13_content.txt): 296 trees read back (ring clear 1,017 to 1,020 of 1,020; covered 4,356 to 18,613 texels of
// 65,536), 36 noise pictures of runs that did not ask for them (ring clear 478 to 868) and 42 empty ones. It
// refuses only what is provably no picture: a ring that is not clear (what refuses every noise picture seen) and
// a base without one covered texel. The cover has no other bound: the 296 trees are fourteen models at one place
// in the world, and a tree elsewhere may fill most of its picture or be a bare trunk (the bounds of before, 1,024
// to 25,800 covered, refused nothing in the survey that the ring does not, and would have left such a model's
// trees missing). It says "this is not a picture"; it cannot say "this picture is old" or "this is another
// kind of 256 x 256 texture with a clear ring": a whole, earlier picture of the same tree passes (a limit; the
// captures show a picture differing from the frame before on about 1.2% of its texels at 55 frames a second).

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../capture/xenos_tiling.h"

namespace fable2::native::render {

inline constexpr uint32_t kImpostorPictureSize = 256, kImpostorPictureLevels = 4;
inline constexpr uint32_t kImpostorRingTexels = 1020;            // the base level's outermost ring: 4 * 255
inline constexpr uint32_t kImpostorLevelAlphaCount = 3;          // a texel is covered when this many of its four parents are
inline constexpr uint32_t kImpostorRingClearMin = 944;           // of the ring's texels, at least this many have alpha 0 (midway 868 | 1,017)
inline constexpr uint32_t kImpostorCoverMin = 1;                 // covered texels of the base level: at least one (an empty base is no picture); no upper bound

// A decoded channel back to its five bits ((v * 255 + 15) / 31 is the decoder's way out; its 32 bytes are 8 v and
// up to 7 more, so the upper five bits invert it; another byte is taken by its upper five bits too).
constexpr uint32_t ImpostorFiveBits(uint8_t byte) { return uint32_t(byte) >> 3; }
constexpr uint8_t ImpostorEightBits(uint32_t field) { return uint8_t((field * 255 + 15) / 31); }
namespace detail {
constexpr bool ImpostorBitsRoundTrip() {
  for (uint32_t v = 0; v < 32; ++v) {
    if (ImpostorFiveBits(ImpostorEightBits(v)) != v || ((v * 527 + 23) >> 6) != ImpostorEightBits(v)) return false;
  }
  return true;
}
inline constexpr uint8_t kImpostorEight[32] = {0,   8,   16,  25,  33,  41,  49,  58,  66,  74,  82,  90,  99,  107, 115, 123,
                                               132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255};
constexpr bool ImpostorEightTable() {
  for (uint32_t v = 0; v < 32; ++v) {
    if (kImpostorEight[v] != ImpostorEightBits(v)) return false;
  }
  return true;
}
}  // namespace detail
static_assert(detail::ImpostorBitsRoundTrip() && detail::ImpostorEightTable());
static_assert(std::endian::native == std::endian::little, "a texel is written as one little-endian word: red in its low byte");

// Levels 1 to 3 (128, 64, 32 texels square, RGBA8) from the decoded base level, by the rule above. False, the
// three left empty, unless `bytes` is 256 * 256 * 4 and `base_rgba` is there.
inline bool MakeImpostorLevels(const uint8_t* base_rgba, size_t bytes, std::vector<uint8_t> levels[3]) {
  for (uint32_t k = 0; k < 3; ++k) levels[k].clear();
  if (!base_rgba || bytes != size_t(kImpostorPictureSize) * kImpostorPictureSize * 4) return false;
  const uint8_t* above = base_rgba;
  uint32_t edge = kImpostorPictureSize;
  for (uint32_t k = 0; k < 3; ++k) {
    const uint32_t half = edge / 2;
    std::vector<uint8_t>& out = levels[k];
    out.resize(size_t(half) * half * 4);
    for (uint32_t y = 0; y < half; ++y) {
      const uint8_t* row0 = above + size_t(2 * y) * edge * 4;
      const uint8_t* row1 = row0 + size_t(edge) * 4;
      uint8_t* o = &out[size_t(y) * half * 4];
      for (uint32_t x = 0; x < half; ++x, o += 4) {
        // (a texel as one word: red, green, blue in its three low bytes, alpha in the top one; each channel's
        // five bits are its byte's upper five, and four of them add up inside a byte)
        uint32_t p[4];
        std::memcpy(&p[0], row0 + size_t(x) * 8, 8);
        std::memcpy(&p[2], row1 + size_t(x) * 8, 8);
        const uint32_t sum = ((p[0] >> 3) & 0x001F1F1Fu) + ((p[1] >> 3) & 0x001F1F1Fu) + ((p[2] >> 3) & 0x001F1F1Fu) + ((p[3] >> 3) & 0x001F1F1Fu);
        const uint32_t mean = ((sum + 0x00020202u) >> 2) & 0x001F1F1Fu;   // (2 * sum + 4) / 8 a channel
        const uint32_t covered = ((p[0] >> 24) != 0 ? 1u : 0u) + ((p[1] >> 24) != 0 ? 1u : 0u) + ((p[2] >> 24) != 0 ? 1u : 0u) + ((p[3] >> 24) != 0 ? 1u : 0u);
        const uint32_t word = uint32_t(detail::kImpostorEight[mean & 31]) | (uint32_t(detail::kImpostorEight[(mean >> 8) & 31]) << 8) |
                              (uint32_t(detail::kImpostorEight[(mean >> 16) & 31]) << 16) | (covered >= kImpostorLevelAlphaCount ? 0xFF000000u : 0u);
        std::memcpy(o, &word, 4);
      }
    }
    above = out.data();
    edge = half;
  }
  return true;
}

// ring_clear: the texels of the base level's outermost ring whose alpha is 0; covered: the texels of the level
// whose alpha is not.
struct ImpostorPictureStats { uint32_t ring_clear = 0, covered = 0; };
// All zero for a size that is not the base's, or no bytes.
inline ImpostorPictureStats MeasureImpostorPicture(const uint8_t* base_rgba, size_t bytes) {
  ImpostorPictureStats s;
  constexpr uint32_t n = kImpostorPictureSize;
  if (!base_rgba || bytes != size_t(n) * n * 4) return s;
  for (uint32_t y = 0; y < n; ++y) {
    const uint8_t* row = base_rgba + size_t(y) * n * 4;
    const bool edge_row = y == 0 || y == n - 1;
    for (uint32_t x = 0; x < n; ++x) {
      const bool a = row[size_t(x) * 4 + 3] != 0;
      if (a) ++s.covered;
      else if (edge_row || x == 0 || x == n - 1) ++s.ring_clear;
    }
  }
  return s;
}
// The diagnostic feed (fable2_native_impostor_feed; frame-map section 19, "The pictures' feed"): a file of one or
// two sets of picture bases as a discovery capture dumped them (tools/xdk_sigmatch/impostor_pictures.py `feed`),
// which the pass reads IN PLACE OF GUEST MEMORY. It exists because the software adapter's forced read-back
// leaves every picture's base empty (131,072 zero bytes: capture 204721), so that the pictures' textures, views,
// uploads and draws could not run under the debug layer otherwise. The file: u32 magic "F2PF", u32 version 1,
// u32 sets (1 or 2), u32 pictures (1 to `count`), then, set after set, for each picture u32 base, u32 0 and its
// kImpostorBaseBytes bytes; every set has the same bases in the same order.
inline constexpr size_t kImpostorFeedBaseBytes = size_t(256) * 256 * 2;
inline constexpr uint32_t kImpostorFeedMagic = 0x46503246u, kImpostorFeedVersion = 1, kImpostorFeedSets = 2, kImpostorFeedPictures = 14;
struct ImpostorFeed {
  uint32_t sets = 0, pictures = 0;
  const uint8_t* bytes[kImpostorFeedSets][kImpostorFeedPictures] = {};   // [set][which of the fourteen]: into the file, or null
};
// False, with `feed` empty, unless the file is exactly what its head asks for, each base is one of the `count`
// bases first + n * step, none twice, and every reserved word is 0. `count` at most kImpostorFeedPictures.
inline bool ParseImpostorFeed(const uint8_t* file, size_t bytes, uint32_t first, uint32_t step, uint32_t count, ImpostorFeed* feed) {
  if (!feed) return false;
  *feed = ImpostorFeed{};
  if (!file || bytes < 16 || count > kImpostorFeedPictures || step == 0) return false;
  uint32_t head[4];
  std::memcpy(head, file, 16);
  const uint32_t sets = head[2], pictures = head[3];
  if (head[0] != kImpostorFeedMagic || head[1] != kImpostorFeedVersion || sets < 1 || sets > kImpostorFeedSets || pictures < 1 || pictures > count) return false;
  const size_t one = 8 + kImpostorFeedBaseBytes;
  if (bytes != 16 + size_t(sets) * pictures * one) return false;
  ImpostorFeed f;
  for (uint32_t s = 0; s < sets; ++s) {
    for (uint32_t i = 0; i < pictures; ++i) {
      const uint8_t* at = file + 16 + (size_t(s) * pictures + i) * one;
      uint32_t word[2];
      std::memcpy(word, at, 8);
      if (word[1] != 0 || word[0] < first || (word[0] - first) % step != 0 || (word[0] - first) / step >= count) return false;
      const uint32_t n = (word[0] - first) / step;
      if (f.bytes[s][n]) return false;
      f.bytes[s][n] = at + 8;
    }
  }
  for (uint32_t n = 0; n < kImpostorFeedPictures; ++n) {   // (the same bases in every set)
    if (sets == 2 && (f.bytes[0][n] != nullptr) != (f.bytes[1][n] != nullptr)) return false;
  }
  f.sets = sets;
  f.pictures = pictures;
  *feed = f;
  return true;
}

// MeasureImpostorPicture's ring_clear alone (the pass has the covered count from its decoder): the ring's 1,020
// texels and no other. 0 for a size that is not the base's, or no bytes.
inline uint32_t ImpostorRingClear(const uint8_t* base_rgba, size_t bytes) {
  constexpr uint32_t n = kImpostorPictureSize;
  if (!base_rgba || bytes != size_t(n) * n * 4) return 0;
  uint32_t clear = 0;
  const uint8_t* top = base_rgba;
  const uint8_t* bottom = base_rgba + size_t(n - 1) * n * 4;
  for (uint32_t x = 0; x < n; ++x) clear += (top[size_t(x) * 4 + 3] == 0 ? 1u : 0u) + (bottom[size_t(x) * 4 + 3] == 0 ? 1u : 0u);
  for (uint32_t y = 1; y + 1 < n; ++y) {
    const uint8_t* row = base_rgba + size_t(y) * n * 4;
    clear += (row[3] == 0 ? 1u : 0u) + (row[size_t(n - 1) * 4 + 3] == 0 ? 1u : 0u);
  }
  return clear;
}
// A clear ring and something inside it: what every tree read back has and no noise or empty picture. Nothing is
// refused for how much of the picture it covers (ledger ruling 45).
constexpr bool ImpostorPictureUsable(const ImpostorPictureStats& s) {
  return s.ring_clear >= kImpostorRingClearMin && s.covered >= kImpostorCoverMin;
}

// A hash of EVERY byte of a picture's base as it lies in guest memory (the pass asks it whether the picture
// changed since its texture was made; the texture cache's SampleHash reads 4,096 of these 131,072 bytes, and a
// change between its ranges would leave a picture stale). Eight bytes at a time through the round of XXH64, as
// SampleHash mixes its words: a change of any one byte changes the hash, and one bit changed in two words does
// not cancel. A last partial word is padded with zeros; the size is part of the hash. No bytes: the seed.
inline uint64_t ImpostorPictureHash(const uint8_t* data, size_t size) {
  constexpr uint64_t kP1 = 0x9E3779B185EBCA87ull, kP2 = 0xC2B2AE3D27D4EB4Full, kP4 = 0x85EBCA77C2B2AE63ull;
  uint64_t h = 1469598103934665603ull;
  if (!data) return h;
  h ^= uint64_t(size);
  auto word = [&](uint64_t w) {
    w *= kP2;
    w = (w << 31) | (w >> 33);
    w *= kP1;
    h ^= w;
    h = ((h << 27) | (h >> 37)) * kP1 + kP4;
  };
  size_t i = 0;
  for (; i + 8 <= size; i += 8) {
    uint64_t w;
    std::memcpy(&w, data + i, 8);
    word(w);
  }
  if (i < size) {
    uint64_t w = 0;
    std::memcpy(&w, data + i, size - i);
    word(w);
  }
  return h;
}

// The pass's decoder of a picture's base level: what render::Decode1555Level gives for a 256 x 256 tiled
// level of pitch 256 and endian 1 (8-in-16), which is what the capture holds every impostor's tf0 to
// (capture::CheckImpostorCall's fetch rule), without that function's work a texel: fourteen pictures change
// every frame. A tiled level of 16-bit texels lies in runs of 8 texels along x (16 bytes), 8,192 of them; the
// layout says which texels each run holds, made once from capture::TiledOffset2D and refused (`ok` false) unless
// every texel of the level lies where that function puts it.
inline constexpr size_t kImpostorBaseBytes = size_t(kImpostorPictureSize) * kImpostorPictureSize * 2;   // 131,072
static_assert(kImpostorBaseBytes == kImpostorFeedBaseBytes);
struct ImpostorPictureLayout {
  bool ok = false;
  uint16_t run_texel[8192] = {};   // the texel index (y * 256 + x) of the first texel of the run at byte 16 * r
};
inline ImpostorPictureLayout MakeImpostorPictureLayout() {
  ImpostorPictureLayout l;
  uint8_t seen[8192] = {};
  constexpr uint32_t n = kImpostorPictureSize;
  for (uint32_t y = 0; y < n; ++y) {
    for (uint32_t x = 0; x < n; ++x) {
      const uint32_t o = uint32_t(capture::TiledOffset2D(int32_t(x), int32_t(y), n, 1));
      const uint32_t run = o >> 4;
      if (run >= 8192 || (o & 15) != 2 * (x & 7)) return l;
      if ((x & 7) == 0) {
        if (seen[run]) return l;
        seen[run] = 1;
        l.run_texel[run] = uint16_t(y * n + x);
      } else if (!seen[run] || l.run_texel[run] != uint16_t(y * n + (x & ~7u))) {
        return l;
      }
    }
  }
  for (uint8_t s : seen) {
    if (!s) return l;
  }
  l.ok = true;
  return l;
}
inline const ImpostorPictureLayout& ImpostorPictureLayoutOnce() {
  static const ImpostorPictureLayout layout = MakeImpostorPictureLayout();
  return layout;
}
// `src`: the level's kImpostorBaseBytes bytes as they lie in guest memory; `out_rgba`: 256 * 256 * 4 bytes,
// written whole; `covered`: the texels with the alpha bit. False, with nothing written, for another size, no
// bytes or a layout that did not hold.
inline bool DecodeImpostorBase(const uint8_t* src, size_t src_bytes, uint8_t* out_rgba, uint32_t* covered) {
  const ImpostorPictureLayout& layout = ImpostorPictureLayoutOnce();
  if (!src || !out_rgba || !covered || src_bytes != kImpostorBaseBytes || !layout.ok) return false;
  uint32_t on = 0;
  for (uint32_t r = 0; r < 8192; ++r) {
    uint16_t raw[8];
    std::memcpy(raw, src + size_t(r) * 16, 16);
    uint32_t texel[8];
    for (uint32_t k = 0; k < 8; ++k) {
      const uint32_t w = uint32_t(uint16_t((raw[k] << 8) | (raw[k] >> 8)));   // the two bytes exchanged (endian 1)
      on += w >> 15;
      texel[k] = (((w & 31) * 527 + 23) >> 6) | (((((w >> 5) & 31) * 527 + 23) >> 6) << 8) | (((((w >> 10) & 31) * 527 + 23) >> 6) << 16) |
                 ((w >> 15) ? 0xFF000000u : 0u);
    }
    std::memcpy(out_rgba + size_t(layout.run_texel[r]) * 4, texel, 32);
  }
  *covered = on;
  return true;
}

}  // namespace fable2::native::render
