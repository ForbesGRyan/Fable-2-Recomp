#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

namespace rex::memory {
class Memory;
}

namespace fable2::native::capture {
// Guest memory used by guest_read.h and discovery dumps. Called from Install.
void SetMemory(rex::memory::Memory* memory);
// Called by every XDK dispatch override before / after the original.
void OnXdkCall(uint32_t id, PPCContext& ctx, uint8_t* base);
void OnXdkReturn(uint32_t id, PPCContext& ctx, uint8_t* base);
// Once per guest frame (before the D3D census frame hook).
void OnSwap();
}  // namespace fable2::native::capture
