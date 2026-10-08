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

// The Direct3D debug layer's messages as lines of the game log (cvar
// fable2_native_d3d_messages; render/d3d_messages.cpp reads the layer's queue
// and hands each message here). The layer itself writes to the debugger output
// only, so a run without a debugger would not show what it rejects. Add
// returns the line for one message, "d3d ERROR id N (seen k): text" (CORRUPTION
// and warning alike; k counts the messages of that severity and id so far), or
// an empty string when the message is not written: another severity (the
// layer's information), the fifth and later message of one severity and id,
// and everything after kTotal lines (a layer that complains per draw would
// fill the log). Every error, corruption and warning is counted, written or
// not; Summary is the count, logged with the pass's other lines, so that a
// clean run says "errors 0" in so many words.
enum class D3dSeverity { kCorruption, kError, kWarning, kOther };

class D3dMessageLog {
 public:
  static constexpr unsigned kPerId = 4, kTotal = 400;
  static constexpr size_t kTextMax = 600;  // characters of a message's text that are written

  std::string Add(D3dSeverity severity, int id, std::string_view text) {
    if (severity == D3dSeverity::kOther) return {};
    ++(severity == D3dSeverity::kWarning ? warnings_ : errors_);
    if (severity == D3dSeverity::kCorruption) ++corruption_;
    unsigned seen = 0;
    for (Seen& s : seen_) {
      if (s.severity == severity && s.id == id) seen = ++s.count;
    }
    if (seen == 0) {
      seen_.push_back({severity, id, 1});
      seen = 1;
    }
    if (seen > kPerId || lines_ >= kTotal) return {};
    ++lines_;
    std::string line = std::string("d3d ") + (severity == D3dSeverity::kCorruption ? "CORRUPTION"
                                             : severity == D3dSeverity::kError    ? "ERROR"
                                                                                  : "warning") +
                       " id " + std::to_string(id) + " (seen " + std::to_string(seen) + "): ";
    // One line: a control character (a line break, a tab) is written as a space.
    for (const char c : text.substr(0, kTextMax)) line += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    return line;
  }

  std::string Summary() const {
    std::string text = "d3d messages: errors " + std::to_string(errors_) + ", warnings " + std::to_string(warnings_);
    if (corruption_) text += " (CORRUPTION " + std::to_string(corruption_) + ")";
    return text;
  }

 private:
  struct Seen {
    D3dSeverity severity;
    int id;
    unsigned count;
  };
  std::vector<Seen> seen_;  // one entry per severity and id (the layer has a few hundred ids)
  unsigned lines_ = 0, errors_ = 0, warnings_ = 0, corruption_ = 0;
};

// The line written when fable2_native_d3d_messages is first seen on. `queue`: the process's DXGI
// information queue was found (dxgidebug.dll answered; that alone reports nothing). `layer`: the
// SDK's --d3d12_debug is set, which is what turns the Direct3D debug layer on and makes it store
// messages. Only both together report anything.
inline std::string D3dMessagesStartText(bool queue, bool layer) {
  return std::string("d3d messages: on (fable2_native_d3d_messages); the queue is ") +
         (queue ? "available" : "NOT available (dxgidebug.dll gave no information queue: nothing will be reported)") +
         "; the debug layer is " +
         (layer ? "on (--d3d12_debug=true)" : "OFF (--d3d12_debug=true is not set: nothing will be reported)");
}

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
