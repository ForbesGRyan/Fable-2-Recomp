#pragma once

// Pure state helpers for the native renderer plumbing (no SDK/GPU deps) so
// they are unit-testable: view parsing, F6 edge detection, sticky failure,
// frame times per view.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::native {

// kShadow: the clay pass's two shadow maps as grey depth over the whole output (evidence shots).
// Set by name only (the cvar, a plan's VIEW=shadow): F6 never cycles to it.
enum class View { kOff, kOverlay, kSplit, kNative, kPattern, kShadow };

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
  if (lower == "shadow") return {View::kShadow, true};
  return {View::kOff, false};
}

inline View NextView(View v) {
  switch (v) {
    case View::kOff: return View::kOverlay;
    case View::kOverlay: return View::kSplit;
    case View::kSplit: return View::kNative;
    case View::kNative: return View::kPattern;
    case View::kPattern: return View::kOff;
    case View::kShadow: return View::kOff;  // (no key leads to it; the key leaves it)
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
    case View::kShadow: return "shadow";
  }
  return "off";
}

// Views that draw the clay pass over the guest output (the shadow view: the
// pass's shadow maps; the pass renders for it as for the others).
inline bool IsCompositeView(View v) {
  return v == View::kOverlay || v == View::kSplit || v == View::kNative || v == View::kShadow;
}

// Whether setting the view to `next` changes it. Only a change is counted for
// the "(changed)" mark of the clay log line (a plan may set the view it is in).
inline bool ViewChanges(View current, View next) { return current != next; }

// Whether the capture layer builds draw records: only for a consumer (a
// composite view while the failure latch is clear).
inline bool RecordsWanted(View v, bool latch_failed) {
  return !latch_failed && IsCompositeView(v);
}

// Whether the capture also takes the game's two shadow passes: only for a
// frame the clay pass could draw with shadows. `records_wanted`: RecordsWanted.
// `shadows`: fable2_native_shadows. `textures`, `light`: the frame is a lit
// one (the light is of the material: no textures, no light). `usable`: the
// pass's shadow objects have not failed (ClayPass::shadows_usable): once they
// have, nothing is recorded for shadows that cannot be drawn.
inline bool ShadowRecordsWanted(bool records_wanted, bool shadows, bool textures, bool light, bool usable) {
  return records_wanted && shadows && textures && light && usable;
}

// Whether the capture also reads the frame's fog and sky blocks: only for a
// frame the clay pass could draw with them. `records_wanted`: RecordsWanted.
// `fog`: fable2_native_fog. `textures`, `light`: the frame is a lit one (the
// fog is of the lit colour: no textures or no light, no fog).
inline bool FogRecordsWanted(bool records_wanted, bool fog, bool textures, bool light) {
  return records_wanted && fog && textures && light;
}

// Whether the capture also reads the frame's post block: only for a frame the
// clay pass could draw through the game's composite. `records_wanted`:
// RecordsWanted. `post`: fable2_native_post. `textures`, `light`: the frame is
// a lit one (the composite's scale is the lit colour's: no textures or no
// light, today's mapping and no post block).
inline bool PostRecordsWanted(bool records_wanted, bool post, bool textures, bool light) {
  return records_wanted && post && textures && light;
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

// The scan-out gamma ramp (frame-map section 17, "Discovery"): the guest's 256-entry table of 10
// bits a channel, which the SDK's swap blit applies to an 8-bit front buffer and the native
// picture has never passed through. `rgb10[i]` is entry i as red, green, blue. MakePostRamp gives
// it as floats (entry / 1023); `valid` only when every entry is at most 1023 and no channel
// decreases from one entry to the next. A ramp that is not valid carries zeros: nothing of a
// table that failed its rule is handed on.
struct PostRamp {
  float rgb[256][3] = {};
  bool valid = false;
};

inline PostRamp MakePostRamp(const uint32_t rgb10[256][3]) {
  PostRamp r;
  for (int i = 0; i < 256; ++i) {
    for (int c = 0; c < 3; ++c) {
      if (rgb10[i][c] > 1023 || (i > 0 && rgb10[i][c] < rgb10[i - 1][c])) return PostRamp{};
      r.rgb[i][c] = float(rgb10[i][c]) / 1023.0f;
    }
  }
  r.valid = true;
  return r;
}

// The ramp as the log line "[native] ramp: read ..." writes it: 256 entries of nine hex digits,
// rrrgggbbb (10 bits fit three digits; a value that does not is written FFF).
inline std::string FormatRampHex(const uint32_t rgb10[256][3]) {
  std::string text;
  text.reserve(256 * 9);
  char buf[16];
  for (int i = 0; i < 256; ++i) {
    std::snprintf(buf, sizeof(buf), "%03X%03X%03X", unsigned(std::min<uint32_t>(rgb10[i][0], 0xFFF)),
                  unsigned(std::min<uint32_t>(rgb10[i][1], 0xFFF)), unsigned(std::min<uint32_t>(rgb10[i][2], 0xFFF)));
    text += buf;
  }
  return text;
}

// How many of a run's ramp tables the log holds in full (2.3 KiB a line): the first read and the
// changes after it up to this many lines; a later change is counted and said once. `logged` is the
// number of full lines written so far.
inline constexpr uint64_t kRampFullLogs = 16;
inline bool RampLoggedInFull(uint64_t logged) { return logged < kRampFullLogs; }

inline bool SameRamp(const uint32_t a[256][3], const uint32_t b[256][3]) {
  for (int i = 0; i < 256; ++i) {
    for (int c = 0; c < 3; ++c) {
      if (a[i][c] != b[i][c]) return false;
    }
  }
  return true;
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
