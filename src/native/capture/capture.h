#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

namespace fable2::native::capture {
// Called by every XDK dispatch override before / after the original.
inline void OnXdkCall(uint32_t, PPCContext&, uint8_t*) {}
inline void OnXdkReturn(uint32_t, PPCContext&, uint8_t*) {}
}  // namespace fable2::native::capture
