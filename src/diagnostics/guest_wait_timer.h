// guest_wait_timer.h - times the frame limiter / GPU-fence wait for the F3
// overlay. Used by the tuned FrameLimiterWait_82242628 override. Waits inside
// swap or from non-render threads are ignored by guest_frame_rate::add_wait.
#pragma once

#include <chrono>

#include "guest_frame_rate.h"

namespace fable2::guest_wait_timer {

class Scope {
 public:
  Scope() = default;
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  ~Scope() {
    fable2::guest_frame_rate::add_wait(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_).count());
  }

 private:
  const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace fable2::guest_wait_timer
