#pragma once

// Native guest-output renderer plumbing (sub-project 1): registers a
// renderer (replace mode: test pattern) and a post-processor (overlay mode:
// grid over the emulated frame) with the SDK. Off unless the
// fable2_native_render cvar is true at startup.

#include <cstdint>
#include <string>

#include "native_render_state.h"

namespace rex::memory {
class Memory;
}

namespace fable2::native {

// Registers the SDK callbacks if fable2_native_render is true. Call once,
// after the GPU plugin is loaded (Fable2App::OnPostSetup).
void Install(rex::memory::Memory* memory);

// Per guest frame (MainRenderLoop override): F6 toggle and the overlay
// post-process request flag.
void PollFrame();

// Sets the view the F6 key cycles. Any thread (the autoplay thread): the change is applied
// by PollFrame on the game thread, at the next guest frame, exactly as an F6 press (cvar,
// log line, draw records requested). Logged and ignored while the renderer is not installed.
void SetView(View view);

// Starts a discovery capture now (capture::ArmDiscoveryNow): any thread; refused with a
// log line while another capture runs. Returns whether it was accepted.
bool ArmDiscovery(uint32_t frames, uint32_t every);

// F3 status lines from the clay pass (empty while the renderer is not
// installed). Thread-safe copy.
std::string StatusText();

}  // namespace fable2::native
