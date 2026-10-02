#pragma once

// Per-thread draw-hook nesting (pure). A hooked draw function may call another
// hooked draw function (e.g. a DRAW_INDX_2 builder reached from another one);
// only the outermost draw of a call chain is recorded, so geometry is never
// captured twice. Enter() runs in OnXdkCall, Leave() in OnXdkReturn.

#include <cstdint>

namespace fable2::native::capture {

class DrawNesting {
 public:
  // A draw hook is entered; returns true when it is nested inside another.
  bool Enter() {
    const bool nested = depth_ != 0;
    if (nested) ++nested_;
    ++depth_;
    return nested;
  }
  // A draw hook returns; true when it was the outermost draw. A return without
  // a matching Enter (hooks enabled mid-call) counts as outermost.
  bool Leave() {
    if (depth_ == 0) return true;
    return --depth_ == 0;
  }
  uint32_t depth() const { return depth_; }
  uint64_t nested() const { return nested_; }  // nested draws seen so far

 private:
  uint32_t depth_ = 0;
  uint64_t nested_ = 0;
};

}  // namespace fable2::native::capture
