// limiter_yield.h - experiment: yield the core while the guest render thread spins
// on the GPU progress counter.
//
// The game's frame limiter (sub_82242628, docs/FPS_CAP_INVESTIGATION.md) spins
// until the vblank counter the GPU thread advances reaches its deadline, calling
// sub_82B9BF90 ("has the GPU progressed?") on every turn. With the GPU emulation
// thread as the frame's bottleneck, the render thread spends most of the frame in
// that spin (76% of its samples at the bridge), burning a core that may be the
// SMT sibling of the GPU thread's. This override wraps sub_82B9BF90 and, when
// FABLE2_LIMITER_YIELD is set, yields after each call:
//   FABLE2_LIMITER_YIELD=1  SwitchToThread() (gives the core to any ready thread)
//   FABLE2_LIMITER_YIELD=2  Sleep(0) then a short pause
//   FABLE2_LIMITER_YIELD=3  a 50 us high-resolution waitable-timer sleep
// Guest semantics are unchanged: the original runs first and its result (r3) is
// kept; only time passes before the limiter's next turn. Inert without the
// variable. Not compatible with fps_probe.h's override of the same symbol (that
// header is not included).

#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdlib>

#include <rex/ppc/func.h>

extern "C" void __imp__GpuProgressCheck_82B9BF90(PPCContext& __restrict ctx,
                                                               uint8_t* base);

namespace fable2::limiter_yield {

inline int Mode() {
  static const int m = [] {
    const char* v = std::getenv("FABLE2_LIMITER_YIELD");
    return v ? std::atoi(v) : 0;
  }();
  return m;
}

inline HANDLE Timer() {
  static HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_MODIFY_STATE | SYNCHRONIZE);
  return t;
}

inline void YieldOnce() {
  switch (Mode()) {
    case 1:
      SwitchToThread();
      break;
    case 2:
      Sleep(0);
      for (int i = 0; i < 64; ++i) YieldProcessor();
      break;
    case 3: {
      HANDLE t = Timer();
      if (t) {
        LARGE_INTEGER due;
        due.QuadPart = -500;  // 50 us in 100 ns units
        if (SetWaitableTimer(t, &due, 0, nullptr, nullptr, FALSE)) {
          WaitForSingleObject(t, 1);
          break;
        }
      }
      SwitchToThread();
      break;
    }
    default:
      break;
  }
}

}  // namespace fable2::limiter_yield

extern "C" void GpuProgressCheck_82B9BF90(PPCContext& __restrict ctx,
                                                        uint8_t* base) {
  __imp__GpuProgressCheck_82B9BF90(ctx, base);
  if (fable2::limiter_yield::Mode() != 0) fable2::limiter_yield::YieldOnce();
}
