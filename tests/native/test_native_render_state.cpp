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

  // Capture builds draw records only for a consumer: a composite view with
  // the latch clear.
  if (IsCompositeView(View::kOff) || IsCompositeView(View::kPattern)) return 17;
  if (!IsCompositeView(View::kOverlay) || !IsCompositeView(View::kSplit) ||
      !IsCompositeView(View::kNative)) return 18;
  if (!RecordsWanted(View::kSplit, false) || RecordsWanted(View::kSplit, true)) return 19;
  if (RecordsWanted(View::kOff, false) || RecordsWanted(View::kPattern, false)) return 20;

  // Setting the view to what it already is (a plan's VIEW= token) is no change: it is not counted
  // for the clay log's "(changed)" mark. Every F6 step is one.
  if (ViewChanges(View::kNative, View::kNative) || ViewChanges(View::kOff, View::kOff)) return 21;
  if (!ViewChanges(View::kOff, View::kNative) || !ViewChanges(View::kSplit, View::kOverlay)) return 22;
  for (View from : {View::kOff, View::kOverlay, View::kSplit, View::kNative, View::kPattern}) {
    if (!ViewChanges(from, NextView(from))) return 23;
  }

  // Guest frame times per view (the "[native] frames:" log line). An interval is the time between two
  // consecutive frames of one view; a line comes when a view holds kWindow of them, and at a switch.
  {
    ViewFrameLog log;
    if (!log.Add(View::kNative, 1000.0).empty()) return 24;  // the first frame has no interval
    double t = 1000.0;
    for (size_t i = 1; i < ViewFrameLog::kWindow; ++i) {
      t += 40.0;
      if (!log.Add(View::kNative, t).empty()) return 25;
    }
    t += 40.0;
    if (log.Add(View::kNative, t) !=
        "view native (window): 150 frames, mean 40.00 ms (25.0 fps), median 40.00 ms, p90 40.00 ms, max 40.00 ms") {
      return 26;
    }
    // The next window starts empty: 9 frames of 30 ms, 9 of 36 and 2 of 50, then the view changes.
    for (int i = 0; i < 20; ++i) {
      t += (i == 5 || i == 12) ? 50.0 : i < 10 ? 30.0 : 36.0;
      if (!log.Add(View::kNative, t).empty()) return 27;
    }
    t += 70.0;  // the frame of the switch: its interval belongs to neither view
    if (log.Add(View::kOff, t) !=
        "view native (until the switch): 20 frames, mean 34.70 ms (28.8 fps), median 36.00 ms, p90 50.00 ms, max 50.00 ms") {
      return 28;
    }
    // A stretch shorter than kMinFrames is not reported, and leaves nothing behind.
    for (size_t i = 0; i + 1 < ViewFrameLog::kMinFrames; ++i) {
      t += 33.0;
      if (!log.Add(View::kOff, t).empty()) return 29;
    }
    t += 33.0;
    if (!log.Add(View::kSplit, t).empty()) return 30;
    for (size_t i = 1; i < ViewFrameLog::kWindow; ++i) {
      t += 25.0;
      if (!log.Add(View::kSplit, t).empty()) return 31;
    }
    t += 25.0;
    if (log.Add(View::kSplit, t) !=
        "view split (window): 150 frames, mean 25.00 ms (40.0 fps), median 25.00 ms, p90 25.00 ms, max 25.00 ms") {
      return 32;
    }
    // Exactly kMinFrames intervals at a switch are reported; a clock that steps back counts as 0.
    for (size_t i = 0; i < ViewFrameLog::kMinFrames; ++i) {
      t += i == 0 ? -5.0 : 10.0;
      if (!log.Add(View::kSplit, t).empty()) return 33;
    }
    if (log.Add(View::kNative, t + 10.0) !=
        "view split (until the switch): 10 frames, mean 9.00 ms (111.1 fps), median 10.00 ms, p90 10.00 ms, max 10.00 ms") {
      return 34;
    }
  }

  std::cout << "PASS: view parsing/cycling, F6 edge detection, failure latch, frame times per view\n";
  return 0;
}
