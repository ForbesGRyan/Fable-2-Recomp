// Synthetic standalone test; no game or GPU.
// The impostors' pictures (sub-project 12, plan Task 13): the native levels' rule and the content rule of
// src/native/render/impostor_picture.h, as fitted to the captures (frame-map section 19, "The pictures: levels
// and content"). Tool mode at the end: FABLE2_1555_BIN names a dumped base whose decoded bytes
// tests/native/test_texture_decode.cpp wrote beside it (<bin>.cpp.rgba); this test writes <bin>.cpp.levels (the
// three levels, one after another) and <bin>.cpp.stats for tests/test_impostor_pictures.py.
#include "../../src/native/render/impostor_picture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace fable2::native::render;

static uint8_t Byte(uint32_t field) { return uint8_t((field * 255 + 15) / 31); }   // the decoder's five bits to eight

int main() {
  const size_t n = size_t(kImpostorPictureSize) * kImpostorPictureSize * 4;
  std::vector<uint8_t> base(n, 0), levels[3];
  if (!MakeImpostorLevels(base.data(), n, levels)) return 1;                       // an empty picture: empty levels
  if (levels[0].size() != 128u * 128u * 4u || levels[1].size() != 64u * 64u * 4u || levels[2].size() != 32u * 32u * 4u) return 2;
  for (const auto& level : levels) for (uint8_t b : level) if (b != 0) return 3;
  if (MakeImpostorLevels(base.data(), n - 4, levels) || !levels[0].empty() || !levels[2].empty()) return 4;   // a short base: nothing
  if (MakeImpostorLevels(nullptr, n, levels)) return 5;
  for (size_t i = 0; i < n; i += 4) { base[i] = 132; base[i + 1] = 255; base[i + 2] = 8; base[i + 3] = 255; }   // one colour, covered everywhere
  if (!MakeImpostorLevels(base.data(), n, levels)) return 6;
  for (const auto& level : levels) {
    for (size_t i = 0; i < level.size(); i += 4) {
      if (level[i] != 132 || level[i + 1] != 255 || level[i + 2] != 8 || level[i + 3] != 255) return 7;      // five-bit values stay themselves
    }
  }
  // The alpha's rule at its threshold: a level texel with exactly kImpostorLevelAlphaCount covered parents is covered,
  // with one fewer it is not.
  auto clear = [&](uint32_t x, uint32_t y) { base[(size_t(y) * 256 + x) * 4 + 3] = 0; };
  const uint32_t parents[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  for (uint32_t k = 0; k < 4 - kImpostorLevelAlphaCount; ++k) clear(parents[k][0], parents[k][1]);            // block (0, 0): exactly the count stays
  for (uint32_t k = 0; k < 5 - kImpostorLevelAlphaCount && k < 4; ++k) clear(2 + parents[k][0], parents[k][1]);   // block (1, 0): one fewer
  if (!MakeImpostorLevels(base.data(), n, levels)) return 8;
  if (levels[0][3] != 255 || levels[0][4 + 3] != 0 || levels[0][8 + 3] != 255) return 9;
  // The content rule's counts, against a count made here texel by texel.
  uint32_t ring = 0, covered = 0;
  for (uint32_t y = 0; y < 256; ++y) {
    for (uint32_t x = 0; x < 256; ++x) {
      const bool a = base[(size_t(y) * 256 + x) * 4 + 3] != 0;
      covered += a ? 1u : 0u;
      if ((x == 0 || y == 0 || x == 255 || y == 255) && !a) ++ring;
    }
  }
  ImpostorPictureStats s = MeasureImpostorPicture(base.data(), n);
  if (s.ring_clear != ring || s.covered != covered || ring == 0 || ring > 8) return 10;   // (the texels cleared above lie on the ring's top rows)
  // A picture covered to its edge, as this one: the fitted ring minimum is far above its few clear texels.
  if (kImpostorRingClearMin <= 8 || ImpostorPictureUsable(s)) return 15;
  std::vector<uint8_t> empty(n, 0);
  s = MeasureImpostorPicture(empty.data(), n);
  if (s.ring_clear != kImpostorRingTexels || s.covered != 0 || ImpostorPictureUsable(s)) return 11;            // an empty picture is not used
  s = MeasureImpostorPicture(empty.data(), n - 1);
  if (s.ring_clear != 0 || s.covered != 0) return 12;
  if (MeasureImpostorPicture(nullptr, n).covered != 0 || MeasureImpostorPicture(nullptr, n).ring_clear != 0) return 16;
  ImpostorPictureStats tree;
  tree.ring_clear = kImpostorRingTexels;
  tree.covered = 10381;                               // the median of D1's 28 covered counts (t13_content.txt)
  if (!ImpostorPictureUsable(tree)) return 13;                                                                 // a clear ring, a covered middle
  tree.covered = kImpostorCoverMax + 1;
  if (ImpostorPictureUsable(tree)) return 14;
  // Each bound at its own edge, and the fitted numbers against what the captures hold (t13_content.txt): every
  // tree read back (ring clear 1,017 to 1,020; covered 4,356 to 18,613) passes, the noise pictures (ring clear at
  // most 868; five of the six also covered 32,995 or more) and the empty ones fail.
  tree.covered = kImpostorCoverMax;
  if (!ImpostorPictureUsable(tree)) return 17;
  tree.covered = kImpostorCoverMin;
  if (!ImpostorPictureUsable(tree)) return 18;
  tree.covered = kImpostorCoverMin - 1;
  if (ImpostorPictureUsable(tree)) return 19;
  tree.covered = 10381;
  tree.ring_clear = kImpostorRingClearMin;
  if (!ImpostorPictureUsable(tree)) return 20;
  tree.ring_clear = kImpostorRingClearMin - 1;
  if (ImpostorPictureUsable(tree)) return 21;
  const ImpostorPictureStats seen_trees[] = {{1017, 4356}, {1020, 18613}, {1020, 4679}, {1017, 18579}};
  for (const ImpostorPictureStats& t : seen_trees) if (!ImpostorPictureUsable(t)) return 22;
  const ImpostorPictureStats seen_noise[] = {{478, 34484}, {485, 35165}, {490, 34758}, {493, 35535}, {506, 32995}, {868, 9924}, {1020, 0}};
  for (const ImpostorPictureStats& t : seen_noise) if (ImpostorPictureUsable(t)) return 23;
  // A picture half new and half noise (a read that met a half-written level at start-up): about half the ring of a
  // noise picture, which is below the minimum.
  if (ImpostorPictureUsable(ImpostorPictureStats{(1020 + 506) / 2, (10381 + 32995) / 2})) return 24;
  // The cover's upper bound by itself: the least covered of the five dense noise pictures, were its ring clear.
  if (ImpostorPictureUsable(ImpostorPictureStats{1020, 32995})) return 25;

  // The colour's rule: each five-bit field the mean of ALL four parents', covered or not, a half rounded up; a
  // level is made from the level above it.
  std::fill(base.begin(), base.end(), uint8_t(0));
  auto put = [&](uint32_t x, uint32_t y, uint32_t r, uint32_t g, uint32_t b, bool a) {
    uint8_t* p = &base[(size_t(y) * 256 + x) * 4];
    p[0] = Byte(r); p[1] = Byte(g); p[2] = Byte(b); p[3] = a ? 255 : 0;
  };
  // block (0, 0): fields 10, 20, 30, 4 -> 16; 1, 2, 2, 2 -> 1.75 -> 2; 1, 1, 2, 2 -> 1.5 -> 2; three covered
  put(0, 0, 10, 1, 1, true); put(1, 0, 20, 2, 1, true); put(0, 1, 30, 2, 2, true); put(1, 1, 4, 2, 2, false);
  // block (1, 0): 1, 1, 1, 2 -> 1.25 -> 1; 31 x 3 and 0 -> 23.25 -> 23; two covered: not covered, the colour kept
  put(2, 0, 1, 31, 0, true); put(3, 0, 1, 31, 0, true); put(2, 1, 1, 31, 0, false); put(3, 1, 2, 0, 0, false);
  // blocks (0, 1) and (1, 1): covered whole, fields 8 and 12
  for (uint32_t y = 2; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x) put(x, y, x < 2 ? 8 : 12, 0, 0, true);
  if (!MakeImpostorLevels(base.data(), n, levels)) return 30;
  const uint8_t* l1 = levels[0].data();
  if (l1[0] != Byte(16) || l1[1] != Byte(2) || l1[2] != Byte(2) || l1[3] != 255) return 31;
  if (l1[4] != Byte(1) || l1[5] != Byte(23) || l1[6] != 0 || l1[7] != 0) return 32;
  if (l1[128 * 4] != Byte(8) || l1[128 * 4 + 3] != 255 || l1[129 * 4] != Byte(12) || l1[129 * 4 + 3] != 255) return 33;
  // level 2's texel (0, 0): level 1's 16, 1, 8, 12 -> 9.25 -> 9; three of its four parents covered -> covered
  const uint8_t* l2 = levels[1].data();
  if (l2[0] != Byte(9) || l2[3] != 255) return 34;
  // level 3's texel (0, 0): level 2's (0, 0) and three empty ones: 9 / 4 = 2.25 -> 2; one covered parent -> clear
  const uint8_t* l3 = levels[2].data();
  if (l3[0] != Byte(2) || l3[3] != 0) return 35;
  // Every decoded byte goes back to its five bits and out again unchanged (the levels are made in five bits).
  for (uint32_t v = 0; v < 32; ++v) {
    std::fill(base.begin(), base.end(), Byte(v));
    if (!MakeImpostorLevels(base.data(), n, levels) || levels[0][0] != Byte(v) || levels[2][5] != Byte(v)) return 36;
  }

  const char* bin = std::getenv("FABLE2_1555_BIN");
  if (!bin) {
    std::printf("SKIPPED: real picture levels (set FABLE2_1555_BIN after tests/native/test_texture_decode.cpp wrote its .cpp.rgba)\n");
  } else {
    const std::string in = std::string(bin) + ".cpp.rgba";
    std::FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) return 40;
    std::vector<uint8_t> rgba(n + 1);
    const size_t got = std::fread(rgba.data(), 1, rgba.size(), f);
    std::fclose(f);
    if (got != n) return 41;
    if (!MakeImpostorLevels(rgba.data(), n, levels)) return 42;
    const std::string outp = std::string(bin) + ".cpp.levels";
    std::FILE* o = std::fopen(outp.c_str(), "wb");
    if (!o) return 43;
    for (const auto& level : levels) if (std::fwrite(level.data(), 1, level.size(), o) != level.size()) return 44;
    std::fclose(o);
    const ImpostorPictureStats st = MeasureImpostorPicture(rgba.data(), n);
    const std::string statp = std::string(bin) + ".cpp.stats";
    std::FILE* t = std::fopen(statp.c_str(), "wb");
    if (!t) return 45;
    std::fprintf(t, "%u %u %d\n", st.ring_clear, st.covered, ImpostorPictureUsable(st) ? 1 : 0);
    std::fclose(t);
    std::printf("picture levels made: wrote %s (ring clear %u, covered %u, %s)\n", outp.c_str(), st.ring_clear, st.covered,
                ImpostorPictureUsable(st) ? "usable" : "refused");
  }
  std::printf("PASS: impostor pictures (the native levels' rule, the content rule and its fitted bounds)\n");
  return 0;
}
