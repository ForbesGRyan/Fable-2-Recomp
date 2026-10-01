// Synthetic standalone test; no game or GPU.
#include "../../src/native/native_render_state.h"
#include <iostream>

using namespace fable2::native;

int main() {
  // ParseMode
  if (ParseMode("overlay").mode != Mode::kOverlay || !ParseMode("overlay").recognized) return 1;
  if (ParseMode(" Replace ").mode != Mode::kReplace || !ParseMode(" Replace ").recognized) return 2;
  if (ParseMode("REPLACE").mode != Mode::kReplace) return 3;
  ParsedMode unknown = ParseMode("foo");
  if (unknown.mode != Mode::kOverlay || unknown.recognized) return 4;
  if (ParseMode("").recognized) return 5;

  // EdgeDetector: only up->down transitions fire.
  EdgeDetector edge;
  if (edge.Update(false)) return 6;
  if (!edge.Update(true)) return 7;
  if (edge.Update(true)) return 8;   // held
  if (edge.Update(false)) return 9;  // release
  if (!edge.Update(true)) return 10; // pressed again

  // FailureLatch: first Fail() reports, later ones don't; Clear() re-arms.
  FailureLatch latch;
  if (latch.IsFailed()) return 11;
  if (!latch.Fail()) return 12;
  if (latch.Fail()) return 13;
  if (!latch.IsFailed()) return 14;
  latch.Clear();
  if (latch.IsFailed()) return 15;
  if (!latch.Fail()) return 16;

  std::cout << "PASS: mode parsing, F6 edge detection, failure latch\n";
  return 0;
}
