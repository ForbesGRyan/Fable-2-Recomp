#pragma once

// Pure state helpers for the native renderer plumbing (no SDK/GPU deps) so
// they are unit-testable: view parsing, F6 edge detection, sticky failure,
// frame times per view.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::native {

enum class View { kOff, kOverlay, kSplit, kNative, kPattern };

struct ParsedView {
  View view;
  bool recognized;
};

inline ParsedView ParseView(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (lower == "off") return {View::kOff, true};
  if (lower == "overlay") return {View::kOverlay, true};
  if (lower == "split") return {View::kSplit, true};
  if (lower == "native") return {View::kNative, true};
  if (lower == "pattern") return {View::kPattern, true};
  return {View::kOff, false};
}

inline View NextView(View v) {
  switch (v) {
    case View::kOff: return View::kOverlay;
    case View::kOverlay: return View::kSplit;
    case View::kSplit: return View::kNative;
    case View::kNative: return View::kPattern;
    case View::kPattern: return View::kOff;
  }
  return View::kOff;
}

inline const char* ViewName(View v) {
  switch (v) {
    case View::kOff: return "off";
    case View::kOverlay: return "overlay";
    case View::kSplit: return "split";
    case View::kNative: return "native";
    case View::kPattern: return "pattern";
  }
  return "off";
}

// Views that draw the clay pass over the guest output.
inline bool IsCompositeView(View v) {
  return v == View::kOverlay || v == View::kSplit || v == View::kNative;
}

// Whether setting the view to `next` changes it. Only a change is counted for
// the "(changed)" mark of the clay log line (a plan may set the view it is in).
inline bool ViewChanges(View current, View next) { return current != next; }

// Whether the capture layer builds draw records: only for a consumer (a
// composite view while the failure latch is clear).
inline bool RecordsWanted(View v, bool latch_failed) {
  return !latch_failed && IsCompositeView(v);
}

// Guest frame times per view, for the log line "[native] frames: ..."
// (fable2_native_render.cpp, PollFrame): the `[frame] guest` windows of the SDK
// mix the views a plan switches through, this line does not. One Add per guest
// frame with the view in effect and a monotonic time in milliseconds; it
// returns the line's text when the view holds kWindow intervals, and at a view
// switch for the stretch that ends (if it has at least kMinFrames), else an
// empty string. An interval is the time between two consecutive frames of one
// view: the frame at which the view changes starts the next stretch and its
// interval is counted for neither. Cost per frame: the caller's timestamp and
// one stored number; a sort of at most kWindow numbers per line.
class ViewFrameLog {
 public:
  static constexpr size_t kWindow = 150, kMinFrames = 10;

  std::string Add(View view, double now_ms) {
    std::string line;
    if (started_ && view != view_) {
      if (intervals_.size() >= kMinFrames) line = Summary("until the switch");
      intervals_.clear();
    } else if (started_) {
      const double interval = now_ms - last_ms_;
      intervals_.push_back(interval > 0.0 ? interval : 0.0);
      if (intervals_.size() >= kWindow) {
        line = Summary("window");
        intervals_.clear();
      }
    }
    started_ = true;
    view_ = view;
    last_ms_ = now_ms;
    return line;
  }

 private:
  // "view V (why): N frames, mean, fps, median, p90, max" of intervals_ (not empty).
  std::string Summary(const char* why) {
    std::sort(intervals_.begin(), intervals_.end());
    const size_t n = intervals_.size();
    double sum = 0.0;
    for (double v : intervals_) sum += v;
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "view %s (%s): %zu frames, mean %.2f ms (%.1f fps), median %.2f ms, p90 %.2f ms, max %.2f ms",
                  ViewName(view_), why, n, sum / double(n), sum > 0.0 ? 1000.0 * double(n) / sum : 0.0,
                  intervals_[n / 2], intervals_[n * 9 / 10], intervals_[n - 1]);
    return buf;
  }

  std::vector<double> intervals_;
  View view_ = View::kOff;
  double last_ms_ = 0.0;
  bool started_ = false;
};

class EdgeDetector {
 public:
  bool Update(bool down) {
    const bool edge = down && !prev_down_;
    prev_down_ = down;
    return edge;
  }

 private:
  bool prev_down_ = false;
};

class FailureLatch {
 public:
  // Returns true only for the first failure since construction/Clear().
  bool Fail() { return !failed_.exchange(true, std::memory_order_acq_rel); }
  bool IsFailed() const { return failed_.load(std::memory_order_acquire); }
  void Clear() { failed_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> failed_{false};
};

}  // namespace fable2::native
