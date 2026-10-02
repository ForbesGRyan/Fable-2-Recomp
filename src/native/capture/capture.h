#pragma once

#include <cstdint>

#include <rex/ppc/func.h>

#include "../render/frame_scene.h"

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
// The main-scene FrameScene, published at each OnSwap while the native
// renderer is on (fable2_native_render).
render::ScenePublisher& Publisher();
}  // namespace fable2::native::capture
