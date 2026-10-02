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

}  // namespace fable2::native::capture
