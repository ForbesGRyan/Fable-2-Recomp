#pragma once

// The Direct3D debug layer's stored messages, read for the game log (cvar
// fable2_native_d3d_messages; D3dMessageLog in native_render_state.h makes the
// lines). The layer is the SDK's switch --d3d12_debug=true; without it the
// queue does not exist or holds nothing. Reads the process's DXGI information
// queue (every producer, Direct3D 12 among them) and clears what it read, so
// each message is seen once. Any thread, one at a time.

#include <functional>
#include <string>

#include "native_render_state.h"

namespace fable2::native::render {

// Whether the layer's queue can be read (the DXGI debug interface is there).
bool D3dMessagesAvailable();

// Hands every message stored since the last call to `log` and writes the lines
// it returns through `write`. Nothing happens without the queue.
void PollD3dMessages(D3dMessageLog& log, const std::function<void(const std::string&)>& write);

}  // namespace fable2::native::render
