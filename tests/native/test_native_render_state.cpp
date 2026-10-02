// Synthetic standalone test; no game or GPU.
#include "../../src/native/native_render_state.h"
#include <iostream>

using namespace fable2::native;

int main() {
  // View parsing and cycling
  auto pv = ParseView("  Split ");
  if (pv.view != View::kSplit || !pv.recognized) return 1;
  pv = ParseView("bogus");
  if (pv.view != View::kOff || pv.recognized) return 2;
  if (ParseView("pattern").view != View::kPattern) return 3;
  View v = View::kOff;
  const View expect[] = {View::kOverlay, View::kSplit, View::kNative, View::kPattern, View::kOff};
  for (View e : expect) { v = NextView(v); if (v != e) return 4; }
  if (std::string(ViewName(View::kNative)) != "native") return 5;

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

  std::cout << "PASS: view parsing/cycling, F6 edge detection, failure latch\n";
  return 0;
}
