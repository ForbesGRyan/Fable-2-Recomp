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
// The content rule (spec ruling 7), fitted to 374 whole bases of eighteen captures (t13_content.txt): 296 trees
// read back (ring clear 1,017 to 1,020 of 1,020; covered 4,356 to 18,613 texels of 65,536), 36 noise pictures of
// runs that did not ask for them (ring clear 478 to 868; five of the six kinds covered 32,995 to 35,535, the
// sixth 9,924: the ring is what refuses it) and 42 empty ones. It says "this is not a tree's picture"; it cannot
// say "this picture is old": a whole, earlier picture of the same tree passes (a limit; the captures show a
// picture differing from the frame before on about 1.2% of its texels at 55 frames a second).

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
inline constexpr uint32_t kImpostorCoverMin = 1024, kImpostorCoverMax = 25800;   // covered texels of the base level (0 | 4,356 and 18,613 | 32,995)

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
// A clear ring and a covered middle of a tree's size: what every tree read back has and no noise or empty picture.
constexpr bool ImpostorPictureUsable(const ImpostorPictureStats& s) {
  return s.ring_clear >= kImpostorRingClearMin && s.covered >= kImpostorCoverMin && s.covered <= kImpostorCoverMax;
}

// The pass's decoder of a picture's base level: what render::Decode1555Level gives for a 256 x 256 tiled
// level of pitch 256 and endian 1 (8-in-16), which is what the capture holds every impostor's tf0 to
// (capture::CheckImpostorCall's fetch rule), without that function's work a texel: fourteen pictures change
// every frame. A tiled level of 16-bit texels lies in runs of 8 texels along x (16 bytes), 8,192 of them; the
// layout says which texels each run holds, made once from capture::TiledOffset2D and refused (`ok` false) unless
// every texel of the level lies where that function puts it.
inline constexpr size_t kImpostorBaseBytes = size_t(kImpostorPictureSize) * kImpostorPictureSize * 2;   // 131,072
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
