// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/main_scene.h"
#include <iostream>

using namespace fable2::native;
namespace xdk = capture::xdk;

int main() {
  // The flag byte is tested with the mask only.
  if (capture::TilingActive(0)) return 1;
  if (!capture::TilingActive(xdk::kDeviceTilingFlagMask)) return 2;
  if (capture::TilingActive(uint8_t(~xdk::kDeviceTilingFlagMask))) return 3;

  render::FrameBuilder b;
  capture::BracketStats s;
  // Two tiled runs in one frame: off, on, on, off, on, off.
  const bool flags[] = {false, true, true, false, true, false};
  const bool expect_open[] = {false, true, true, false, true, false};
  for (int i = 0; i < 6; ++i) {
    if (capture::ObserveDraw(b, flags[i], s) != flags[i]) return 10 + i;
    if (b.InMainScene() != expect_open[i]) return 20 + i;
  }
  if (s.draws != 6 || s.in_bracket != 3 || s.opens != 2 || s.closes != 2) return 30;

  // A bracket still open at the frame end is closed and counted.
  capture::ObserveDraw(b, true, s);
  if (s.opens != 3) return 31;
  const capture::BracketStats done = capture::EndFrame(b, s);
  if (b.InMainScene()) return 32;
  if (done.draws != 7 || done.in_bracket != 4 || done.opens != 3 || done.closes != 3) return 33;
  if (s.draws != 0 || s.in_bracket != 0 || s.opens != 0 || s.closes != 0) return 34;  // reset

  // EndFrame on a closed bracket counts no close.
  capture::ObserveDraw(b, false, s);
  if (capture::EndFrame(b, s).closes != 0) return 35;

  // Finish (at Swap) closes the builder; a draw that is still tiled
  // reopens it.
  capture::ObserveDraw(b, true, s);
  b.Finish(1);
  capture::ObserveDraw(b, true, s);
  if (!b.InMainScene() || s.opens != 2 || s.closes != 0) return 36;

  // --- The shadow passes (ShadowPassTracker) ---
  {
    using capture::ShadowPass;
    using capture::ShadowPassTracker;
    const uint32_t map = capture::kShadowMapSurfaceWord;
    if (map != 0x10000410u || (map & xdk::kSurfacePitchMask) != 1040) return 50;
    const uint32_t scene = 0x11810460u;
    ShadowPassTracker t;
    auto clear = [&] { return t.Observe(map, 0, true, false); };
    auto draw = [&] { return t.Observe(map, 0, false, false); };
    auto other = [&] { return t.Observe(0x04000140u, 0xF, false, false); };
    auto in_scene = [&] { return t.Observe(scene, 0xF, false, true); };

    // Two passes: the first run after a clear is the near map, the second the far one.
    if (clear() != ShadowPass::kNone) return 51;  // a clear is not a draw
    for (int i = 0; i < 3; ++i) {
      if (draw() != ShadowPass::kNear) return 52;
    }
    if (clear() != ShadowPass::kNone) return 53;
    for (int i = 0; i < 4; ++i) {
      if (draw() != ShadowPass::kFar) return 54;
    }
    if (t.passes() != 2) return 55;
    // The scene that follows is no shadow draw, whatever it is drawn on.
    if (in_scene() != ShadowPass::kNone || other() != ShadowPass::kNone) return 56;
    // Three passes: the third and later ones are extra.
    if (clear() != ShadowPass::kNone || draw() != ShadowPass::kExtra || draw() != ShadowPass::kExtra) return 57;
    if (clear() != ShadowPass::kNone || draw() != ShadowPass::kExtra || t.passes() != 4) return 58;

    // The frame boundary: the next frame's first pass is the near map again.
    t.Reset();
    if (t.passes() != 0) return 59;
    if (clear() != ShadowPass::kNone || draw() != ShadowPass::kNear) return 60;
    // Frame(n): a reset when the number changes, nothing when it does not.
    t.Frame(7);
    if (t.passes() != 0) return 61;
    clear();
    t.Frame(7);
    if (t.passes() != 1 || draw() != ShadowPass::kNear) return 62;
    t.Frame(8);
    if (t.passes() != 0) return 63;

    // A pass with no draws is a pass: what follows the second clear is the far map.
    if (clear() != ShadowPass::kNone || clear() != ShadowPass::kNone) return 64;
    if (draw() != ShadowPass::kFar || t.passes() != 2) return 65;

    // A scene draw between: the run has ended, and what comes on the map after it without a
    // clear is not the pass any more.
    t.Reset();
    clear();
    if (draw() != ShadowPass::kNear || in_scene() != ShadowPass::kNone) return 66;
    if (draw() != ShadowPass::kExtra || draw() != ShadowPass::kExtra) return 67;
    // ... and the next clear opens the second pass as usual.
    if (clear() != ShadowPass::kNone || draw() != ShadowPass::kFar) return 68;
    // The same for a draw on another surface between.
    t.Reset();
    clear();
    if (draw() != ShadowPass::kNear || other() != ShadowPass::kNone || draw() != ShadowPass::kExtra) return 69;
    // A draw between a clear and its run ends the run before it began.
    t.Reset();
    clear();
    if (other() != ShadowPass::kNone || draw() != ShadowPass::kExtra) return 70;

    // Draws on the map with no clear before them in the frame: no cascade is known for them.
    t.Reset();
    if (draw() != ShadowPass::kExtra || t.passes() != 0) return 71;

    // What is not a shadow draw, inside an open pass: a colour mask, a draw inside the scene, the
    // other depth-only passes outside it (pitch 560, word 0x09000230: the leaf cards' half-size
    // depth after the scene; pitch 320), a word with the map's pitch that is not the map's word.
    t.Reset();
    clear();
    if (t.Observe(map, 0xF, false, false) != ShadowPass::kNone) return 72;
    t.Reset();
    clear();
    if (t.Observe(map, 0, false, true) != ShadowPass::kNone) return 73;
    for (uint32_t word : {0x09000230u, 0x04000140u, 0x10010410u, 0x00000410u, 0u}) {
      t.Reset();
      clear();
      if (t.Observe(word, 0, false, false) != ShadowPass::kNone) return 74;
      if (draw() != ShadowPass::kExtra) return 75;  // (and it ended the run)
    }
    // A clear of another surface opens nothing, and a clear seen inside the scene neither.
    t.Reset();
    if (t.Observe(0x04000140u, 0, true, false) != ShadowPass::kNone || t.passes() != 0) return 76;
    if (draw() != ShadowPass::kExtra) return 77;
    t.Reset();
    if (t.Observe(map, 0, true, true) != ShadowPass::kNone || t.passes() != 0) return 78;
  }

  std::cout << "PASS: main-scene bracket, shadow pass tracker\n";
  return 0;
}
