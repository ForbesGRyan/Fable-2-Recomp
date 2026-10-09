#pragma once

// Main-scene bracket (pure): decides per guest draw call whether it belongs to
// the tiled main-scene pass, from the device tiling flag read at the draw
// (xdk_layout.h, kDeviceTilingFlag*; frame-map section 8), and opens/closes the
// FrameBuilder on transitions.

#include <cstdint>

#include "../render/frame_scene.h"
#include "xdk_layout.h"

namespace fable2::native::capture {

struct BracketStats {
  uint32_t draws = 0;       // guest draw calls seen this frame
  uint32_t in_bracket = 0;  // of those, inside the main scene
  uint32_t opens = 0;
  uint32_t closes = 0;
};

// The device tiling flag byte (device + xdk::kDeviceTilingFlagOffset).
inline bool TilingActive(uint8_t flag_byte) {
  return (flag_byte & xdk::kDeviceTilingFlagMask) != 0;
}

// One guest draw: open or close `b` on a transition (before the draw is
// recorded), count it, and return whether it is in the main scene.
inline bool ObserveDraw(render::FrameBuilder& b, bool tiling, BracketStats& s) {
  if (tiling && !b.InMainScene()) {
    b.Open();
    ++s.opens;
  } else if (!tiling && b.InMainScene()) {
    b.Close();
    ++s.closes;
  }
  ++s.draws;
  if (tiling) ++s.in_bracket;
  return tiling;
}

// Frame end (Swap): close a bracket left open, return the frame's counts and
// reset them.
inline BracketStats EndFrame(render::FrameBuilder& b, BracketStats& s) {
  if (b.InMainScene()) {
    b.Close();
    ++s.closes;
  }
  const BracketStats done = s;
  s = {};
  return done;
}

// --- The two sun shadow passes (frame-map section 15, "Passes") ---------------

// RB_SURFACE_INFO of the game's shadow maps: pitch 1040, no multisampling, a
// hierarchical-Z pitch of 1024 (the map is 1024 x 1024 whatever the game's
// resolution). The cascade test is this whole word, not "colour mask 0 outside
// the scene": the game has other depth-only passes there (the leaf cards'
// half-size depth after the scene on word 0x09000230, a pass at pitch 320).
inline constexpr uint32_t kShadowMapSurfaceWord = 0x10000410;

// Which shadow pass a draw belongs to. kExtra: a draw on the map that is in
// neither of the frame's two passes (counted, never recorded).
enum class ShadowPass : uint8_t { kNone, kNear, kFar, kExtra };

// Classifies the guest's draws of one frame, in the order they are issued on
// one thread (pure). The game clears the map and draws the near pass, clears
// it again and draws the far pass, each as one unbroken run of draws on the
// map with the colour mask 0, outside the main scene. So:
//  - a clear of the map opens a pass: the frame's first is the near map, the
//    second the far map, later ones are extra;
//  - a draw on the map (the surface word, colour mask 0, not in the scene) is
//    of the pass the last clear opened, while that pass's run lasts;
//  - any other draw ends the run. A draw on the map after that, and one before
//    the frame's first clear, has no pass that could be told: kExtra. (A pass
//    is recorded whole or its tail is counted; a map is never put together
//    from two runs.)
// A pass with no draws is a pass: after two clears the next run is the far map.
// Reset at the frame boundary (Reset, or Frame with the frame's number).
class ShadowPassTracker {
 public:
  // One outermost guest draw call, or a clear. `surface_word`: RB_SURFACE_INFO
  // as the device holds it at the draw (for a clear, the word the clear names);
  // 0 when it could not be read. `colour_mask`: RB_COLOR_MASK (anything but 0
  // when unread). `is_clear`: the call is the game's clear. `in_scene`: the
  // draw is inside the main scene's bracket. Returns the draw's pass; kNone for
  // a clear and for whatever is not a shadow draw.
  constexpr ShadowPass Observe(uint32_t surface_word, uint32_t colour_mask, bool is_clear, bool in_scene) {
    const bool on_map = !in_scene && surface_word == kShadowMapSurfaceWord;
    if (is_clear) {
      // A clear of another surface, or one seen inside the scene, opens nothing
      // (and is no draw: it does not end a run either).
      if (on_map) {
        ++passes_;
        open_ = true;
      }
      return ShadowPass::kNone;
    }
    if (!on_map || colour_mask != 0) {
      open_ = false;
      return ShadowPass::kNone;
    }
    if (!open_) return ShadowPass::kExtra;
    return passes_ == 1 ? ShadowPass::kNear : passes_ == 2 ? ShadowPass::kFar : ShadowPass::kExtra;
  }

  // The frame boundary.
  constexpr void Reset() {
    passes_ = 0;
    open_ = false;
  }
  // The same for a tracker that is not told of the boundary itself: `frame` is
  // any number that changes with the frame; another one than the last resets.
  constexpr void Frame(uint32_t frame) {
    if (frame != frame_) {
      frame_ = frame;
      Reset();
    }
  }
  // Passes opened in the frame so far (clears of the map).
  constexpr uint32_t passes() const { return passes_; }

 private:
  uint32_t frame_ = 0;
  uint32_t passes_ = 0;
  bool open_ = false;  // the last clear's run has not ended
};

}  // namespace fable2::native::capture
