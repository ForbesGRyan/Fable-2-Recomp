#pragma once

// Native guest-output renderer plumbing (sub-project 1): registers a
// renderer (replace mode: test pattern) and a post-processor (overlay mode:
// grid over the emulated frame) with the SDK. Off unless the
// fable2_native_render cvar is true at startup.

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

}  // namespace fable2::native
