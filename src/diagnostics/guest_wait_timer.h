// guest_wait_timer.h - times the game's frame limiter / GPU-fence wait
// (sub_82242628, see docs/FPS_CAP_INVESTIGATION.md) per guest frame so the F3
// overlay can show it separately from CPU "work". Same override pattern as
// fps_meter.h. Waits made from inside the swap call or from non-render threads
// are ignored (see guest_frame_rate.h add_wait).
//
// Only one definition of this symbol may be compiled: fps_probe.h also
// overrides it but is commented out in fable_2_app.h.

#pragma once

#include <chrono>
#include <cstdint>

#include <rex/ppc/func.h>

#include "guest_frame_rate.h"

extern "C" void __imp__ProcessAndProcessAndProcess1087_82242628(PPCContext& __restrict ctx,
                                                                uint8_t* base);

extern "C" void ProcessAndProcessAndProcess1087_82242628(PPCContext& __restrict ctx,
                                                         uint8_t* base) {
  const auto t0 = std::chrono::steady_clock::now();
  __imp__ProcessAndProcessAndProcess1087_82242628(ctx, base);
  fable2::guest_frame_rate::add_wait(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
          .count());
}
