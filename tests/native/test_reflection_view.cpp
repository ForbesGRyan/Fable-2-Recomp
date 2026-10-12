#include "../../src/native/capture/reflection_view.h"
#include <cstdio>
using namespace fable2::native::capture;
int main() {
  ReflectionViewTracker t;
  auto draw = [&](uint64_t ps, bool indexed = false, bool begin = false, uint32_t surface = 280,
                  bool shadow = false, bool scene = false) {
    return t.Observe(surface, 320, shadow, ps, indexed, begin, scene);
  };
  const uint32_t counts[] = {36, 4, 12, 3};
  for (uint32_t v = 0; v < 4; ++v) {
    for (uint32_t i = 0; i < counts[v]; ++i) if (draw(0xA4A9) != ReflectionDraw::kOther) return 1;
    for (uint32_t i = 0; i < counts[v]; ++i) if (draw(kReflectionTerrainPs) != ReflectionDraw::kTerrain) return 2;
    for (uint32_t i = 0; i < (v == 0 ? 18u : 2u); ++i) draw(0xECD6);
    if (draw(kReflectionSkyPs, true) != ReflectionDraw::kSky || t.views() != v + 1) return 3;
    if (draw(0xD72FD0903CCE7DD6ull, true) != ReflectionDraw::kOther || t.views() != v + 1) return 5;
    if (v < 2 && draw(kReflectionBlurPs, false, true, 320) != ReflectionDraw::kBlur) return 4;
  }
  if (draw(0, false, false, 320, false, true) != ReflectionDraw::kNone || t.views() != 4 || t.dropped()) return 6;
  t.Reset();
  if (draw(kReflectionTerrainPs, false, false, 320) != ReflectionDraw::kNone) return 7;
  if (draw(kReflectionTerrainPs, false, false, 280, true) != ReflectionDraw::kNone) return 8;
  if (draw(kReflectionTerrainPs, false, false, 320) != ReflectionDraw::kNone || t.views()) return 9;
  draw(kReflectionTerrainPs);
  draw(0, false, false, 320);
  if (!t.dropped()) return 10;
  t.Reset();
  for (int i = 0; i < 9; ++i) { draw(kReflectionTerrainPs); draw(kReflectionSkyPs, true); }
  if (!t.dropped() || t.views() != 8) return 11;
  t.Reset();
  if (t.views() || t.dropped()) return 12;
  if (draw(kReflectionBlurPs, false, true, 320) != ReflectionDraw::kNone) return 13;
  // A sky hash on the wrong hook does not complete a view.
  draw(kReflectionTerrainPs); draw(kReflectionSkyPs);
  draw(0, false, false, 320, false, true);
  if (!t.dropped() || t.views()) return 14;
  // A view can consist of export/sky without terrain; it still belongs
  // to the whole-frame census, and must not be mistaken for a flare.
  t.Reset();
  draw(kReflectionTerrainPs); draw(kReflectionSkyPs, true);
  draw(0xA4A965C189287B99ull);
  draw(0, false, false, 320, false, true);
  if (!t.dropped()) return 15;
  t.Reset();
  draw(kReflectionTerrainPs); draw(kReflectionSkyPs, true);
  draw(0xA4A965C189287B99ull); draw(kReflectionSkyPs, true);
  if (t.views() != 2 || t.dropped()) return 16;
  t.Reset();
  for (int i = 0; i < 8; ++i) { draw(kReflectionTerrainPs); draw(kReflectionSkyPs, true); }
  draw(0xA4A965C189287B99ull); draw(kReflectionSkyPs, true);
  if (!t.dropped() || t.views() != 8) return 17;
  std::puts("PASS: reflection view order, exclusions, incomplete and ninth view, reset, blur");
}
