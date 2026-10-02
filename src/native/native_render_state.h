#pragma once

// Pure state helpers for the native renderer plumbing (no SDK/GPU deps) so
// they are unit-testable: view parsing, F6 edge detection, sticky failure.

#include <atomic>
#include <cctype>
#include <string>
#include <string_view>

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
