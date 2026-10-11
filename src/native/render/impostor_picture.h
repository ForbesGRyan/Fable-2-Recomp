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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable2::native::render {

inline constexpr uint32_t kImpostorPictureSize = 256, kImpostorPictureLevels = 4;
inline constexpr uint32_t kImpostorRingTexels = 1020;            // the base level's outermost ring: 4 * 255
inline constexpr uint32_t kImpostorLevelAlphaCount = 3;          // a texel is covered when this many of its four parents are
inline constexpr uint32_t kImpostorRingClearMin = 944;           // of the ring's texels, at least this many have alpha 0 (midway 868 | 1,017)
inline constexpr uint32_t kImpostorCoverMin = 1024, kImpostorCoverMax = 25800;   // covered texels of the base level (0 | 4,356 and 18,613 | 32,995)

// A decoded channel back to its five bits ((v * 255 + 15) / 31 is the decoder's way out; this inverts it for
// each of the 32 bytes it makes, and takes any other byte to the nearest).
constexpr uint32_t ImpostorFiveBits(uint8_t byte) { return (uint32_t(byte) * 31 + 127) / 255; }
constexpr uint8_t ImpostorEightBits(uint32_t field) { return uint8_t((field * 255 + 15) / 31); }

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
        const uint8_t* p[4] = {row0 + size_t(2 * x) * 4, row0 + size_t(2 * x + 1) * 4, row1 + size_t(2 * x) * 4, row1 + size_t(2 * x + 1) * 4};
        for (uint32_t c = 0; c < 3; ++c) {
          const uint32_t sum = ImpostorFiveBits(p[0][c]) + ImpostorFiveBits(p[1][c]) + ImpostorFiveBits(p[2][c]) + ImpostorFiveBits(p[3][c]);
          o[c] = ImpostorEightBits((2 * sum + 4) / 8);
        }
        const uint32_t covered = (p[0][3] ? 1u : 0u) + (p[1][3] ? 1u : 0u) + (p[2][3] ? 1u : 0u) + (p[3][3] ? 1u : 0u);
        o[3] = covered >= kImpostorLevelAlphaCount ? 255 : 0;
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
// A clear ring and a covered middle of a tree's size: what every tree read back has and no noise or empty picture.
constexpr bool ImpostorPictureUsable(const ImpostorPictureStats& s) {
  return s.ring_clear >= kImpostorRingClearMin && s.covered >= kImpostorCoverMin && s.covered <= kImpostorCoverMax;
}

static_assert(ImpostorFiveBits(ImpostorEightBits(0)) == 0 && ImpostorFiveBits(ImpostorEightBits(1)) == 1 &&
              ImpostorFiveBits(ImpostorEightBits(16)) == 16 && ImpostorFiveBits(ImpostorEightBits(31)) == 31);

}  // namespace fable2::native::render
