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
// The capture layer is active (native renderer on or discovery requested).
bool Enabled();
// Whether a consumer (a composite debug view) wants draw records. Set once
// per guest frame by fable2::native::PollFrame; takes effect from the next
// guest frame. Without it main-scene draws are only counted.
void SetRecordsWanted(bool wanted);
// Whether the consumer also wants the game's two sun shadow passes (the native
// view, the light and the shadows on: fable2_native_shadows). Set with
// SetRecordsWanted and latched with it at the frame boundary; it has an effect
// only in a frame with records. Without it no shadow draw is read or recorded
// and a frame's scene says so (FrameScene::shadow_records false).
void SetShadowsWanted(bool wanted);
// Whether the consumer also wants the frame's fog and sky blocks (the native
// view, the light and the fog on: fable2_native_fog). Set and latched like
// SetShadowsWanted. Without it no fog register, fetch constant or table is read
// and a frame's scene says so (FrameScene::fog_records false).
void SetFogWanted(bool wanted);
// Whether the consumer also wants the frame's post block (the native view, the
// light and the post chain on: fable2_native_post). Set and latched like
// SetShadowsWanted. Without it no hooked draw outside the main scene is looked
// at for the post chain, no register, fetch constant or table is read, and a
// frame's scene says so (FrameScene::post_records false).
void SetPostWanted(bool wanted);
// Whether the consumer also wants the frame's quad records and the radial blur's registers (the
// native view, the light, the post chain and the sun's switch on: fable2_native_sun). Set and
// latched like SetPostWanted, and it has an effect only in a frame whose post block is wanted.
// Without it no call of the indexed builder is looked at for a furniture quad, nothing of one is
// read, and a frame's scene says so (FrameScene::quad_records false).
void SetSunWanted(bool wanted);
// Starts a discovery capture of `frames` guest frames (1..100000) sampling every `every`th
// draw (1..64) at the next guest frame, into a new file, like FABLE2_NATIVE_DISCOVERY
// after its delay. Callable from any thread. Returns false, with a log line, when the
// arguments are out of range, the capture layer is off, or a capture is running or still
// waiting for its environment delay (no queue).
bool ArmDiscoveryNow(uint32_t frames, uint32_t every);
// The frame number of the scene published with the first frame of the discovery capture that is
// running or ran last (that frame row's "scene_frame"): 0 from the moment a capture is armed until
// its first frame is published, and when that frame published no scene (the renderer off, or a frame
// without records: no native view was drawn). Stored before the scene is
// published, so whoever holds that scene reads its number here. For the stored native scene
// (render/scene_dump.h), which is paired with a capture by it. Callable from any thread.
uint64_t DiscoveryFirstSceneFrame();
// The main-scene FrameScene, published at each OnSwap while the native
// renderer is on (fable2_native_render). Frames without records publish
// nullptr.
render::ScenePublisher& Publisher();
}  // namespace fable2::native::capture
