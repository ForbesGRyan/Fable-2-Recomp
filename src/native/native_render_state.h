#pragma once

// Pure state helpers for the native renderer plumbing (no SDK/GPU deps) so
// they are unit-testable: mode parsing, F6 edge detection, sticky failure.

#include <atomic>
#include <cctype>
#include <string>
#include <string_view>

namespace fable2::native {

enum class Mode { kOverlay, kReplace };

struct ParsedMode {
  Mode mode;
  bool recognized;
};

inline ParsedMode ParseMode(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (lower == "overlay") return {Mode::kOverlay, true};
  if (lower == "replace") return {Mode::kReplace, true};
  return {Mode::kOverlay, false};
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
