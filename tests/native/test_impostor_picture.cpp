// Synthetic standalone test; no game or GPU.
// The impostors' pictures (sub-project 12, plan Task 13): the native levels' rule and the content rule of
// src/native/render/impostor_picture.h, as fitted to the captures (frame-map section 19, "The pictures: levels
// and content"). Tool mode at the end: FABLE2_1555_BIN names a dumped base whose decoded bytes
// tests/native/test_texture_decode.cpp wrote beside it (<bin>.cpp.rgba); this test writes <bin>.cpp.levels (the
// three levels, one after another) and <bin>.cpp.stats for tests/test_impostor_pictures.py.
#include "../../src/native/render/impostor_picture.h"
#include "../../src/native/render/texture_decode.h"
#include "../../src/native/render/texture_residency.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
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
  // The cover has no upper bound and its lower bound is 1 (ledger ruling 45; review of Tasks 10 to 14, I1): the
  // rule refuses what is provably no picture (a ring that is not clear; a base without a covered texel) and
  // nothing for its size. A round crown inscribed in the square covers 78% of it before its gaps.
  static_assert(kImpostorCoverMin == 1);
  tree.covered = 25801;                               // (one more than the bound of before)
  if (!ImpostorPictureUsable(tree)) return 14;
  tree.covered = 65536 - kImpostorRingTexels;         // everything inside a clear ring
  if (!ImpostorPictureUsable(tree)) return 17;
  tree.covered = 1;
  if (!ImpostorPictureUsable(tree)) return 18;
  tree.covered = 0;
  if (ImpostorPictureUsable(tree)) return 19;
  // The ring's bound at its own edge, and the numbers the captures hold (t13_content.txt): every tree read back
  // (ring clear 1,017 to 1,020) passes, the noise pictures (ring clear at most 868) and the empty ones fail.
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
  // The survey: the 374 whole bases of eighteen captures, {ring clear, covered} in the order of
  // out/native-evidence/distant-trees/t13_content.txt. Each keeps the verdict the rule of before gave it (ring
  // clear at least 944 and 1,024 to 25,800 covered): 296 usable, 78 refused. tests/test_impostor_pictures.py
  // reads this table for the Python twin.
  {
    static const ImpostorPictureStats survey[] = {
      // survey-begin
      {1020, 18211}, {1019, 17012}, {1020, 4679}, {1020, 9591}, {1020, 10386}, {1020, 9577}, {1020, 10431}, {1020, 8323}, {1020, 7618}, {1020, 18579},
      {1020, 18457}, {1020, 10541}, {1020, 18387}, {1020, 10333}, {1020, 18252}, {1019, 17027}, {1020, 4681}, {1020, 9608}, {1020, 10376}, {1020, 9513},
      {1020, 10351}, {1020, 8328}, {1020, 7619}, {1020, 18561}, {1020, 18534}, {1020, 10372}, {1020, 18354}, {1020, 10446}, {1020, 18503}, {1017, 16687},
      {1020, 4664}, {1020, 9405}, {1020, 10362}, {1020, 9452}, {1020, 10435}, {1020, 8304}, {1020, 7630}, {1020, 18216}, {1020, 18255}, {1020, 10347},
      {1020, 18224}, {1020, 10375}, {1020, 18528}, {1017, 16664}, {1020, 4657}, {1020, 9386}, {1020, 10396}, {1020, 9462}, {1020, 10457}, {1020, 8308},
      {1020, 7622}, {1020, 18216}, {1020, 18192}, {1020, 10355}, {1020, 18206}, {1020, 10366}, {1020, 18405}, {1017, 16781}, {1020, 4707}, {1020, 9565},
      {1020, 10415}, {1020, 9470}, {1020, 10374}, {1020, 8301}, {1020, 7672}, {1020, 18304}, {1020, 18432}, {1017, 16829}, {1020, 4644}, {1020, 9478},
      {1020, 10407}, {1020, 9515}, {1020, 10348}, {1020, 8318}, {1020, 7643}, {1020, 18262}, {1020, 18225}, {1019, 17041}, {1020, 4678}, {1020, 9615},
      {1020, 10385}, {1020, 9561}, {1020, 10496}, {1020, 8378}, {1020, 7610}, {1020, 18341}, {1020, 18154}, {1019, 17006}, {1020, 4705}, {1020, 9558},
      {1020, 10369}, {1020, 9567}, {1020, 10476}, {1020, 8344}, {1020, 7612}, {1020, 18298}, {1020, 17583}, {1020, 17083}, {1020, 4362}, {1020, 9753},
      {1020, 9368}, {1020, 8743}, {1020, 9526}, {1020, 7608}, {1020, 7188}, {1020, 17503}, {1020, 17629}, {1020, 17079}, {1020, 4356}, {1020, 9842},
      {1020, 9384}, {1020, 8762}, {1020, 9495}, {1020, 7639}, {1020, 7163}, {1020, 17561}, {1020, 18246}, {478, 34484}, {485, 35165}, {490, 34758},
      {506, 32995}, {493, 35535}, {868, 9924}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0},
      {1020, 18247}, {1020, 18255}, {1019, 17007}, {1020, 4713}, {1020, 9589}, {1020, 10440}, {1020, 9554}, {1020, 10501}, {1020, 8435}, {1020, 7666},
      {1020, 18594}, {1020, 18390}, {1020, 10551}, {1020, 18210}, {1020, 10409}, {1020, 18270}, {1019, 17014}, {1020, 4716}, {1020, 9590}, {1020, 10443},
      {1020, 9549}, {1020, 10494}, {1020, 8452}, {1020, 7674}, {1020, 18593}, {1020, 18394}, {1020, 10553}, {1020, 18236}, {1020, 10419}, {1020, 18228},
      {1019, 16985}, {1020, 4702}, {1020, 9582}, {1020, 10422}, {1020, 9558}, {1020, 10487}, {1020, 8466}, {1020, 7657}, {1020, 18610}, {1020, 18390},
      {1020, 10458}, {1020, 18255}, {1020, 10427}, {1020, 18232}, {1019, 16980}, {1020, 4704}, {1020, 9605}, {1020, 10427}, {1020, 9555}, {1020, 10469},
      {1020, 8440}, {1020, 7649}, {1020, 18601}, {1020, 18373}, {1020, 10490}, {1020, 18238}, {1020, 10435}, {1020, 18230}, {478, 34484}, {485, 35165},
      {490, 34758}, {506, 32995}, {493, 35535}, {868, 9924}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0},
      {1020, 0}, {1020, 18237}, {1020, 18222}, {478, 34484}, {485, 35165}, {490, 34758}, {506, 32995}, {493, 35535}, {868, 9924}, {1020, 0},
      {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 18205}, {1020, 18184}, {1019, 16990}, {1020, 4704},
      {1020, 9600}, {1020, 10406}, {1020, 9560}, {1020, 10492}, {1020, 8435}, {1020, 7636}, {1020, 18607}, {1020, 18342}, {1020, 10479}, {1020, 18216},
      {1020, 10436}, {1020, 18201}, {1019, 16985}, {1020, 4696}, {1020, 9591}, {1020, 10410}, {1020, 9553}, {1020, 10485}, {1020, 8432}, {1020, 7623},
      {1020, 18613}, {1020, 18335}, {1020, 10464}, {1020, 18217}, {1020, 10442}, {1020, 18180}, {1019, 16991}, {1020, 4702}, {1020, 9608}, {1020, 10405},
      {1020, 9561}, {1020, 10494}, {1020, 8435}, {1020, 7639}, {1020, 18610}, {1020, 18350}, {1020, 10466}, {1020, 18218}, {1020, 10439}, {1020, 18200},
      {1019, 16984}, {1020, 4695}, {1020, 9590}, {1020, 10406}, {1020, 9550}, {1020, 10483}, {1020, 8423}, {1020, 7622}, {1020, 18610}, {1020, 18336},
      {1020, 10465}, {1020, 18216}, {1020, 10442}, {1020, 18244}, {478, 34484}, {485, 35165}, {490, 34758}, {506, 32995}, {493, 35535}, {868, 9924},
      {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 18232}, {1020, 18266}, {478, 34484},
      {485, 35165}, {490, 34758}, {506, 32995}, {493, 35535}, {868, 9924}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0},
      {1020, 0}, {1020, 0}, {1020, 18284}, {1020, 18219}, {1019, 17019}, {1020, 4718}, {1020, 9592}, {1020, 10382}, {1020, 9545}, {1020, 10461},
      {1020, 8431}, {1020, 7627}, {1020, 18571}, {1020, 18356}, {1020, 10443}, {1020, 18224}, {1020, 10442}, {1020, 18194}, {1019, 17030}, {1020, 4714},
      {1020, 9606}, {1020, 10337}, {1020, 9551}, {1020, 10458}, {1020, 8435}, {1020, 7607}, {1020, 18571}, {1020, 18382}, {1020, 10415}, {1020, 18218},
      {1020, 10448}, {1020, 18219}, {1019, 17017}, {1020, 4703}, {1020, 9601}, {1020, 10391}, {1020, 9539}, {1020, 10462}, {1020, 8435}, {1020, 7630},
      {1020, 18567}, {1020, 18345}, {1020, 10441}, {1020, 18220}, {1020, 10449}, {1020, 18197}, {1019, 17028}, {1020, 4715}, {1020, 9624}, {1020, 10354},
      {1020, 9540}, {1020, 10457}, {1020, 8428}, {1020, 7607}, {1020, 18581}, {1020, 18369}, {1020, 10430}, {1020, 18221}, {1020, 10450}, {1020, 18216},
      {478, 34484}, {485, 35165}, {490, 34758}, {506, 32995}, {493, 35535}, {868, 9924}, {1020, 0}, {1020, 0}, {1020, 0}, {1020, 0},
      {1020, 0}, {1020, 0}, {1020, 0}, {1020, 18212},
      // survey-end
    };
    uint32_t usable = 0, refused = 0;
    for (const ImpostorPictureStats& t : survey) {
      const bool before = t.ring_clear >= 944 && t.covered >= 1024 && t.covered <= 25800;
      if (ImpostorPictureUsable(t) != before) return 100;
      ++(before ? usable : refused);
    }
    if (sizeof(survey) / sizeof(survey[0]) != 374 || usable != 296 || refused != 78) return 101;
  }
  // Made pictures, through the two counters the pass uses: an honest picture covering 60% of its texels and one
  // covering 100 texels are used; noise (every other texel covered by a generator's bit) and an empty base are not.
  {
    auto usable = [&](const std::vector<uint8_t>& rgba, uint32_t want_covered) {
      const ImpostorPictureStats m = MeasureImpostorPicture(rgba.data(), n);
      if (want_covered != ~0u && m.covered != want_covered) return -1;
      if (ImpostorRingClear(rgba.data(), n) != m.ring_clear) return -1;
      return ImpostorPictureUsable(m) ? 1 : 0;
    };
    auto block = [&](uint32_t w, uint32_t h) {
      std::vector<uint8_t> rgba(n, 0);
      for (uint32_t y = (256 - h) / 2; y < (256 - h) / 2 + h; ++y) {
        for (uint32_t x = (256 - w) / 2; x < (256 - w) / 2 + w; ++x) {
          uint8_t* p = &rgba[(size_t(y) * 256 + x) * 4];
          p[0] = 41; p[1] = 99; p[2] = 16; p[3] = 255;
        }
      }
      return rgba;
    };
    if (200u * 197u * 100u < 60u * 65536u || usable(block(200, 197), 200u * 197u) != 1) return 102;   // 39,400 texels: 60.1%
    if (usable(block(10, 10), 100) != 1) return 103;
    if (usable(block(1, 1), 1) != 1) return 104;
    std::vector<uint8_t> noise(n, 0);
    uint32_t lcg = 12345u;
    for (size_t i = 0; i < n; i += 4) {
      lcg = lcg * 1664525u + 1013904223u;
      noise[i + 3] = (lcg >> 31) ? 255 : 0;
    }
    const ImpostorPictureStats noisy = MeasureImpostorPicture(noise.data(), n);
    if (noisy.ring_clear < 400 || noisy.ring_clear > 620 || noisy.covered < 30000 || noisy.covered > 35536) return 105;   // about half of each
    if (usable(noise, ~0u) != 0) return 106;
    if (usable(std::vector<uint8_t>(n, 0), 0) != 0) return 107;
    // (a block that reaches the ring on every side: covered to its edge, refused by the ring however small its cover)
    if (usable(block(256, 256), 65536) != 0) return 108;
  }

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

  // The pass's own decoder of a base (plan Task 14: fourteen pictures every frame): DecodeImpostorBase against
  // render::Decode1555Level, the reference, on made bytes of every kind; and the levels against a plain reading of
  // their rule written out here.
  {
    const ImpostorPictureLayout& layout = ImpostorPictureLayoutOnce();
    if (!layout.ok) return 50;
    std::vector<uint8_t> src(kImpostorBaseBytes), fast(n, 0xEE), slow;
    if (kImpostorBaseBytes != 131072u) return 51;
    uint32_t seed = 0x1234567u;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return uint8_t(seed >> 24); };
    for (int round = 0; round < 6; ++round) {
      for (uint8_t& b : src) b = round == 0 ? uint8_t(0) : round == 1 ? uint8_t(0xFF) : next();
      if (round == 5) for (size_t i = 0; i < src.size(); i += 2) { src[i] = uint8_t(i >> 9); src[i + 1] = uint8_t(i >> 1); }   // every word value
      uint32_t covered = 77;
      if (!DecodeImpostorBase(src.data(), src.size(), fast.data(), &covered)) return 52;
      if (!Decode1555Level(src.data(), src.size(), 256, 256, 256, true, 1, &slow) || slow.size() != n) return 53;
      if (std::memcmp(fast.data(), slow.data(), n) != 0) return 54;
      if (covered != MeasureImpostorPicture(slow.data(), n).covered) return 55;
      // (the ring alone, for the pass: the decoder already counted the covered texels)
      if (ImpostorRingClear(fast.data(), n) != MeasureImpostorPicture(slow.data(), n).ring_clear || ImpostorRingClear(fast.data(), n - 4) != 0 ||
          ImpostorRingClear(nullptr, n) != 0) return 61;
      // the levels, by the rule written plainly: five bits a channel, the mean of four with a half up, three of four
      if (!MakeImpostorLevels(slow.data(), n, levels)) return 56;
      const uint8_t* above = slow.data();
      uint32_t edge = 256;
      for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t half = edge / 2;
        for (uint32_t y = 0; y < half; ++y) {
          for (uint32_t x = 0; x < half; ++x) {
            uint32_t sum[3] = {}, on = 0;
            for (uint32_t q = 0; q < 4; ++q) {
              const uint8_t* t4 = above + ((size_t(2 * y + (q >> 1)) * edge) + 2 * x + (q & 1)) * 4;
              for (uint32_t c = 0; c < 3; ++c) sum[c] += (uint32_t(t4[c]) * 31 + 127) / 255;
              on += t4[3] != 0;
            }
            const uint8_t* got = &levels[k][(size_t(y) * half + x) * 4];
            for (uint32_t c = 0; c < 3; ++c) if (got[c] != Byte((2 * sum[c] + 4) / 8)) return 57;
            if (got[3] != (on >= 3 ? 255 : 0)) return 58;
          }
        }
        above = levels[k].data();
        edge = half;
      }
    }
    uint32_t covered = 5;
    if (DecodeImpostorBase(src.data(), src.size() - 1, fast.data(), &covered) || DecodeImpostorBase(nullptr, src.size(), fast.data(), &covered) ||
        DecodeImpostorBase(src.data(), src.size(), nullptr, &covered) || DecodeImpostorBase(src.data(), src.size() + 2, fast.data(), &covered)) return 59;
    // The layout itself: every texel's bytes where render::TiledOffset2D puts them.
    for (uint32_t r = 0; r < 8192; ++r) {
      const uint32_t texel = layout.run_texel[r], x = texel & 255, y = texel >> 8;
      if ((x & 7) != 0 || uint32_t(fable2::native::capture::TiledOffset2D(int32_t(x), int32_t(y), 256, 1)) != r * 16) return 60;
    }
  }

  // The picture's hash (ImpostorPictureHash; review of Tasks 10 to 14, Minor 5): over every byte of the base, so
  // that a change of any one byte is a change of the picture. The texture cache's SampleHash reads sixteen
  // ranges of 256 bytes of a base this size: a change between them left a picture as it was.
  {
    std::vector<uint8_t> bytes(kImpostorBaseBytes);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i * 31 + (i >> 8) * 7 + 3);
    const uint64_t h0 = ImpostorPictureHash(bytes.data(), bytes.size());
    if (ImpostorPictureHash(bytes.data(), bytes.size()) != h0) return 110;
    uint32_t unsampled = 0;
    for (size_t at = 0; at < bytes.size(); at += 97) {                         // 1,352 places, every residue of 8 among them
      const uint64_t s0 = SampleHash(bytes.data(), uint32_t(bytes.size()));
      bytes[at] ^= 0x40;
      if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 111;
      if (SampleHash(bytes.data(), uint32_t(bytes.size())) == s0) ++unsampled;
      bytes[at] ^= 0x40;
    }
    if (unsampled < 1000) return 112;                                          // (what the sampled hash did not see)
    for (size_t at : {size_t(0), size_t(7), size_t(8), bytes.size() - 9, bytes.size() - 8, bytes.size() - 1}) {
      bytes[at] ^= 1;
      if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 113;
      bytes[at] ^= 1;
    }
    // Two bytes exchanged, the same bit changed in two words, a shorter and a longer range: other hashes.
    std::swap(bytes[1000], bytes[1008]);
    if (bytes[1000] == bytes[1008] || ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 114;
    std::swap(bytes[1000], bytes[1008]);
    bytes[2000] ^= 4;
    bytes[2008] ^= 4;
    if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 115;
    bytes[2000] ^= 4;
    bytes[2008] ^= 4;
    if (ImpostorPictureHash(bytes.data(), bytes.size()) != h0) return 116;
    if (ImpostorPictureHash(bytes.data(), bytes.size() - 1) == h0 || ImpostorPictureHash(bytes.data(), bytes.size() - 8) == h0) return 117;
    std::vector<uint8_t> zeros(kImpostorBaseBytes, 0);
    if (ImpostorPictureHash(zeros.data(), zeros.size()) == ImpostorPictureHash(zeros.data(), zeros.size() - 8)) return 118;
    if (ImpostorPictureHash(nullptr, 0) != ImpostorPictureHash(nullptr, 5) || ImpostorPictureHash(nullptr, 0) == h0) return 119;
    // A range that does not end on a word: its last bytes count, and the size does (zeros that the padding would
    // also give are another range).
    const uint64_t h13 = ImpostorPictureHash(bytes.data(), 13);
    bytes[12] ^= 0x10;
    if (ImpostorPictureHash(bytes.data(), 13) == h13) return 120;
    bytes[12] ^= 0x10;
    bytes[8] ^= 0x10;
    if (ImpostorPictureHash(bytes.data(), 13) == h13) return 121;
    bytes[8] ^= 0x10;
    if (ImpostorPictureHash(zeros.data(), 5) == ImpostorPictureHash(zeros.data(), 6) || ImpostorPictureHash(zeros.data(), 13) == ImpostorPictureHash(zeros.data(), 16)) return 122;
    // The cheap whole-picture hash (ledger rulings 52 and 54): eight lanes of eight bytes a block of 64, so that
    // eight words are mixed side by side. Every byte still counts: each byte of the first and last two blocks and
    // of a block in the middle, each of the eight bits of one byte, and so a byte of every lane.
    for (size_t at = 0; at < bytes.size(); at = at == 127 ? 65536 : at == 65536 + 63 ? bytes.size() - 128 : at + 1) {
      bytes[at] ^= 0x01;
      if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 123;
      bytes[at] ^= 0x01;
    }
    for (int bit = 0; bit < 8; ++bit) {
      bytes[70001] ^= uint8_t(1u << bit);
      if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 124;
      bytes[70001] ^= uint8_t(1u << bit);
    }
    // What a hash of lanes could lose: the same bit changed in two words of ONE lane (64 bytes apart and further),
    // the word's top bit too; two lanes' words exchanged; two whole blocks exchanged.
    for (uint8_t bit : {uint8_t(0x04), uint8_t(0x80)}) {
      for (size_t apart : {size_t(64), size_t(128), size_t(64) * 2047}) {
        bytes[7] ^= bit;
        bytes[7 + apart] ^= bit;
        if (ImpostorPictureHash(bytes.data(), bytes.size()) == h0) return 125;
        bytes[7] ^= bit;
        bytes[7 + apart] ^= bit;
      }
    }
    {
      std::vector<uint8_t> moved = bytes;
      for (size_t k = 0; k < 8; ++k) std::swap(moved[4096 + k], moved[4096 + 8 + k]);            // lanes 0 and 1 of one block
      if (std::memcmp(moved.data(), bytes.data(), bytes.size()) == 0 || ImpostorPictureHash(moved.data(), moved.size()) == h0) return 126;
      moved = bytes;
      for (size_t k = 0; k < 64; ++k) std::swap(moved[8192 + k], moved[8192 + 64 + k]);          // two blocks
      if (std::memcmp(moved.data(), bytes.data(), bytes.size()) == 0 || ImpostorPictureHash(moved.data(), moved.size()) == h0) return 127;
    }
    // Every length from 0 to 200 gives another hash than every other (blocks, whole words after them, a partial
    // word), over zeros and over the made bytes.
    for (const std::vector<uint8_t>* v : {&zeros, &bytes}) {
      std::vector<uint64_t> seen;
      for (size_t len = 0; len <= 200; ++len) {
        const uint64_t h = ImpostorPictureHash(v->data(), len);
        for (uint64_t s : seen) if (s == h) return 128;
        seen.push_back(h);
      }
    }
    // The function itself, by three values of the made bytes (a pictures' file carries a picture's hash, and a
    // change of the mixing that the properties above do not see is still a change): whole, 77 bytes, none.
    if (h0 != 0x9BD96C5AB79F9BE6ull || ImpostorPictureHash(bytes.data(), 77) != 0xFCBF198B5C602ABDull ||
        ImpostorPictureHash(bytes.data(), 0) != 0x7AF4D8F85ABF6391ull) return 144;
    // And each byte of a range that ends inside a block counts (the tail's words and its last partial word).
    for (size_t len : {size_t(64 + 8), size_t(64 + 13), size_t(127), size_t(190)}) {
      const uint64_t whole = ImpostorPictureHash(bytes.data(), len);
      for (size_t at = 0; at < len; ++at) {
        bytes[at] ^= 0x20;
        if (ImpostorPictureHash(bytes.data(), len) == whole) return 129;
        bytes[at] ^= 0x20;
      }
    }
  }

  // The texture's bookkeeping (review of the whole branch, question 7: the most dangerous thing held only by text
  // pins): what the pass holds of a base's texture, and what it does with this frame's picture.
  {
    const uint32_t sA = 0x60A, sB = 0x688;
    ImpostorPictureHeld none;
    if (none.texture || none.view || none.filled || none.shader_state) return 130;
    // The table: held -> the plan for (hash, swizzle).
    struct PlanRow { ImpostorPictureHeld held; uint64_t hash; uint32_t swizzle; bool reuse, make_texture, make_view; };
    const ImpostorPictureHeld full{true, true, true, true, sA, 11};
    const PlanRow plans[] = {
        {none, 11, sA, false, true, true},                                         // nothing yet: a texture and its view
        {full, 11, sA, true, false, false},                                        // an unchanged picture: reused
        {full, 12, sA, false, false, false},                                       // a changed picture: refreshed into the texture there
        {full, 11, sB, false, false, true},                                        // another swizzle: the view again, and the picture staged again
        {{true, true, false, false, sA, 11}, 11, sA, false, false, false},         // made but never filled (its upload failed): not reused
        {{true, true, true, false, sA, 11}, 11, sA, false, false, false},          // staged but its upload never ran: not reused
        {{true, false, true, true, sA, 11}, 11, sA, false, false, true},           // its view could not be made: not reused, the view again
        {{false, false, true, true, sA, 11}, 11, sA, false, true, true},           // no texture, whatever the rest says
        {{false, true, false, false, sA, 0}, 0, sA, false, true, true},
        {{true, true, false, true, sA, 11}, 11, sA, false, false, false},          // (no history gives this: each of the four is asked all the same)
        {full, 0, sA, false, false, false},                                        // (a hash of 0 is a hash like another)
        {{true, true, true, true, sA, 0}, 0, sA, true, false, false},
    };
    for (const PlanRow& r : plans) {
      const ImpostorPicturePlan p = PlanImpostorPicture(r.held, r.hash, r.swizzle);
      if (p.reuse != r.reuse || p.make_texture != r.make_texture || p.make_view != r.make_view) return 131;
    }
    // The outcome: what is held afterwards and what happened, for what the device gave.
    struct CommitRow { ImpostorPictureHeld held; uint64_t hash; uint32_t swizzle; bool texture_ok, view_ok, staged; ImpostorPictureStep step; ImpostorPictureHeld after; };
    const CommitRow commits[] = {
        {none, 11, sA, true, true, true, ImpostorPictureStep::kStaged, {true, true, true, false, sA, 11}},
        {none, 11, sA, false, true, true, ImpostorPictureStep::kNoTexture, {false, false, false, false, 0, 0}},
        {none, 11, sA, true, false, true, ImpostorPictureStep::kNoView, {true, false, false, false, sA, 0}},
        {none, 11, sA, true, true, false, ImpostorPictureStep::kNoStaging, {true, true, false, false, sA, 0}},
        {full, 11, sA, true, true, true, ImpostorPictureStep::kReuse, full},
        {full, 11, sA, false, false, false, ImpostorPictureStep::kReuse, full},                       // (a reuse asks the device for nothing)
        {full, 12, sA, true, true, true, ImpostorPictureStep::kStaged, {true, true, true, true, sA, 12}},
        {full, 12, sA, true, true, false, ImpostorPictureStep::kNoStaging, full},                     // the texture keeps the picture it has, and its hash
        {full, 11, sB, true, true, true, ImpostorPictureStep::kStaged, {true, true, true, true, sB, 11}},
        {full, 11, sB, true, false, true, ImpostorPictureStep::kNoView, {true, false, true, true, sB, 11}},
        {full, 12, sB, true, true, false, ImpostorPictureStep::kNoStaging, {true, true, true, true, sB, 11}},
        // (a new texture is empty and in the state it was made in, whatever was held of the one before)
        {{false, false, true, true, sA, 11}, 12, sA, true, true, false, ImpostorPictureStep::kNoStaging, {true, true, false, false, sA, 0}},
        {{false, true, true, true, sB, 11}, 11, sA, true, false, true, ImpostorPictureStep::kNoView, {true, false, false, false, sA, 0}},
    };
    for (const CommitRow& r : commits) {
      const ImpostorPictureOutcome o = CommitImpostorPicture(r.held, PlanImpostorPicture(r.held, r.hash, r.swizzle), r.hash, r.swizzle, r.texture_ok, r.view_ok, r.staged);
      const ImpostorPictureHeld& a = o.held;
      if (o.step != r.step) return 132;
      if (a.texture != r.after.texture || a.view != r.after.view || a.filled != r.after.filled || a.shader_state != r.after.shader_state ||
          a.swizzle != r.after.swizzle || a.hash != r.after.hash) return 133;
    }
    if (!ImpostorPictureStepUsable(ImpostorPictureStep::kReuse) || !ImpostorPictureStepUsable(ImpostorPictureStep::kStaged) ||
        ImpostorPictureStepUsable(ImpostorPictureStep::kNoTexture) || ImpostorPictureStepUsable(ImpostorPictureStep::kNoView) ||
        ImpostorPictureStepUsable(ImpostorPictureStep::kNoStaging)) return 134;
    // The upload block's barrier: from the shader-resource state only for a texture that was uploaded before.
    if (ImpostorPictureUploadFromShaderState(none) || !ImpostorPictureUploadFromShaderState(full) || !ImpostorPictureUploaded(none).shader_state ||
        ImpostorPictureUploaded(full).hash != 11) return 135;
    // Every history of four frames over two pictures, two swizzles, a content refusal and each of the device's
    // three failures (64 kinds of frame: 16,777,216 histories), against a model of the device's texture: what it
    // holds, the state it is in, its view's swizzle. In every frame: a record is called usable only when the
    // texture, after this frame's uploads, holds exactly this frame's picture, in the shader-resource state, behind
    // a view of the record's swizzle; a barrier is never asked from a state the texture is not in; an unchanged
    // picture behind the same view is never staged again, and a changed one is never reused.
    struct Model { bool texture = false, view = false, shader = false, has = false; uint64_t content = 0; uint32_t view_swizzle = 0; };
    uint64_t histories = 0, reused = 0, staged_count = 0, refused = 0;
    for (uint32_t code = 0; code < 64u * 64u * 64u * 64u; ++code) {
      ImpostorPictureHeld held;
      Model m;
      for (uint32_t f = 0; f < 4; ++f) {
        const uint32_t k = (code >> (6 * f)) & 63u;
        const uint64_t hash = 11 + (k & 1u);
        const uint32_t swizzle = (k & 2u) ? sB : sA;
        const bool content_ok = (k & 4u) == 0, texture_ok = (k & 8u) == 0, view_ok = (k & 16u) == 0, staging_ok = (k & 32u) == 0;
        const ImpostorPicturePlan plan = PlanImpostorPicture(held, hash, swizzle);
        bool usable = false;
        if (plan.reuse) {
          usable = true;
          ++reused;
          if (!m.has || hash != m.content) return 136;                             // (a changed picture reused)
        } else if (!content_ok) {
          ++refused;                                                               // (the pass returns before anything is made: nothing held changes)
        } else {
          // (an unchanged picture behind the same view, uploaded: it must have been reused)
          if (m.texture && m.has && m.shader && m.content == hash && m.view && m.view_swizzle == swizzle) return 137;
          bool have_texture = true, have_view = true;
          if (plan.make_texture) {
            m = Model{};
            m.texture = have_texture = texture_ok;
          }
          if (have_texture && plan.make_view) {
            m.view = have_view = view_ok;
            m.view_swizzle = swizzle;
          }
          const bool stage = have_texture && have_view && staging_ok;
          const ImpostorPictureOutcome o = CommitImpostorPicture(held, plan, hash, swizzle, have_texture, have_view, stage);
          held = o.held;
          const bool staged = o.step == ImpostorPictureStep::kStaged;
          usable = ImpostorPictureStepUsable(o.step);
          if (staged != stage || usable != staged) return 138;
          if (staged) {
            ++staged_count;
            // (the upload block, later in the same Render)
            if (ImpostorPictureUploadFromShaderState(held) != m.shader) return 139;   // (a barrier from a state the texture is not in)
            m.shader = true;
            m.has = true;
            m.content = hash;
            held = ImpostorPictureUploaded(held);
          }
        }
        if (held.texture != m.texture || (held.texture && held.view != m.view) || (held.texture && held.shader_state != m.shader)) return 140;
        if (usable && !(m.texture && m.view && m.shader && m.has && m.content == hash && m.view_swizzle == swizzle)) return 141;
        if (held.filled && !(m.has && m.content == held.hash)) return 142;         // (the hash held is the hash of what the texture holds)
      }
      ++histories;
    }
    if (histories != 16777216ull || reused == 0 || staged_count == 0 || refused == 0) return 143;
  }

  // The diagnostic feed's file (fable2_native_impostor_feed; tools/xdk_sigmatch/impostor_pictures.py `feed`): one
  // or two sets of a capture's dumped bases, which the pass reads in place of guest memory.
  {
    const uint32_t first = 0x12704000u, step = 0x2B000u, count = 14;
    auto make = [&](uint32_t sets, const std::vector<uint32_t>& bases) {
      std::vector<uint8_t> f(16);
      const uint32_t head[4] = {kImpostorFeedMagic, 1u, sets, uint32_t(bases.size())};
      std::memcpy(f.data(), head, 16);
      for (uint32_t s = 0; s < sets; ++s) {
        for (uint32_t base : bases) {
          const uint32_t one[2] = {base, 0u};
          const size_t at = f.size();
          f.resize(at + 8 + kImpostorBaseBytes, uint8_t(1 + s + ((base - first) / step) * 2));
          std::memcpy(f.data() + at, one, 8);
        }
      }
      return f;
    };
    std::vector<uint8_t> f = make(2, {first, first + 3 * step, first + 13 * step});
    ImpostorFeed feed;
    if (!ParseImpostorFeed(f.data(), f.size(), first, step, count, &feed) || feed.sets != 2 || feed.pictures != 3) return 70;
    if (!feed.bytes[0][0] || !feed.bytes[1][3] || !feed.bytes[1][13] || feed.bytes[0][1] || feed.bytes[1][12]) return 71;
    if (feed.bytes[0][0][0] != 1 || feed.bytes[1][0][kImpostorBaseBytes - 1] != 2 || feed.bytes[0][3][5] != 7 || feed.bytes[1][13][9] != 28) return 72;
    if (kImpostorFeedMagic != 0x46503246u || f[0] != 'F' || f[1] != '2' || f[2] != 'P' || f[3] != 'F') return 73;
    // Refused, the feed left empty: a byte short or long, another magic or version, no set or three, fifteen
    // pictures, a base that is none of the fourteen, a base twice, a reserved word.
    auto refused = [&](const std::vector<uint8_t>& bad) {
      ImpostorFeed g;
      g.sets = 9;
      return !ParseImpostorFeed(bad.data(), bad.size(), first, step, count, &g) && g.sets == 0 && g.pictures == 0 && !g.bytes[0][0];
    };
    std::vector<uint8_t> bad = f;
    bad.pop_back();
    if (!refused(bad)) return 74;
    bad = f;
    bad.push_back(0);
    if (!refused(bad)) return 75;
    bad = f;
    bad[3] = 'S';
    if (!refused(bad)) return 76;
    bad = f;
    bad[4] = 2;
    if (!refused(bad)) return 77;
    if (!refused(make(0, {first})) || !refused(make(3, {first}))) return 78;
    std::vector<uint32_t> fifteen;
    for (uint32_t i = 0; i < 15; ++i) fifteen.push_back(first + i * step);
    if (!refused(make(1, fifteen)) || !refused(make(1, {first + 0x1000u})) || !refused(make(1, {first, first}))) return 79;
    bad = f;
    bad[16 + 4] = 1;
    if (!refused(bad) || !refused(std::vector<uint8_t>(15)) || ParseImpostorFeed(nullptr, f.size(), first, step, count, &feed)) return 80;
    f = make(1, {first + step});
    if (!ParseImpostorFeed(f.data(), f.size(), first, step, count, &feed) || feed.sets != 1 || feed.pictures != 1 || !feed.bytes[0][1] || feed.bytes[1][1]) return 81;
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
  // Tool mode: what a picture's hash costs (FABLE2_HASH_BENCH set; meaningful only in a build with optimisation:
  // the suite's own build has none). The hash of before the closing fix is kept here for the comparison.
  if (std::getenv("FABLE2_HASH_BENCH")) {
    const auto before = [](const uint8_t* data, size_t size) {
      constexpr uint64_t kP1 = 0x9E3779B185EBCA87ull, kP2 = 0xC2B2AE3D27D4EB4Full, kP4 = 0x85EBCA77C2B2AE63ull;
      uint64_t h = 1469598103934665603ull ^ uint64_t(size);
      for (size_t i = 0; i + 8 <= size; i += 8) {
        uint64_t w;
        std::memcpy(&w, data + i, 8);
        w *= kP2;
        w = (w << 31) | (w >> 33);
        w *= kP1;
        h ^= w;
        h = ((h << 27) | (h >> 37)) * kP1 + kP4;
      }
      return h;
    };
    std::vector<std::vector<uint8_t>> pictures(14, std::vector<uint8_t>(kImpostorBaseBytes));
    uint32_t seed = 12345;
    for (auto& p : pictures) for (uint8_t& b : p) { seed = seed * 1664525u + 1013904223u; b = uint8_t(seed >> 24); }
    volatile uint64_t sink = 0;
    const auto measure = [&](auto&& hash) {
      double best = 1e30, sum = 0.0;
      const int rounds = 400;                                                      // (400 frames of fourteen pictures: the best and the mean)
      for (int round = 0; round < rounds; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        for (const auto& p : pictures) sink = sink ^ hash(p.data(), p.size());
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, us);
        sum += us;
      }
      return std::pair<double, double>(best / 14.0, sum / rounds / 14.0);
    };
    const auto sampled = measure([](const uint8_t* d, size_t n) { return SampleHash(d, uint32_t(n)); });
    const auto old_us = measure(before);
    const auto now_us = measure([](const uint8_t* d, size_t n) { return ImpostorPictureHash(d, n); });
    std::printf("hash bench, a picture of %zu bytes, best and mean of 400 frames of fourteen, microseconds a picture: ImpostorPictureHash %.2f "
                "and %.2f (fourteen: %.3f ms); the hash of before (one chain of words) %.2f and %.2f (fourteen: %.3f ms); the sampled hash "
                "%.2f and %.2f\n",
                kImpostorBaseBytes, now_us.first, now_us.second, now_us.second * 14.0 / 1000.0, old_us.first, old_us.second,
                old_us.second * 14.0 / 1000.0, sampled.first, sampled.second);
  }
  std::printf("PASS: impostor pictures (the native levels' rule; the content rule: a clear ring and not empty, the survey's 374 verdicts; the picture's "
              "hash, every byte in eight lanes; the texture's bookkeeping: the plan, the outcome and 16,777,216 histories against a model)\n");
  return 0;
}
