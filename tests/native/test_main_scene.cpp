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

  std::cout << "PASS: main-scene bracket\n";
  return 0;
}
