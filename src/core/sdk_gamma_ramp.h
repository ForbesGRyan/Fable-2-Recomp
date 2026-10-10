// The guest's scan-out gamma ramp as the SDK holds it, for the native renderer
// (fable2::native::SetGammaRampReader; frame map section 17, "Discovery").
//
// THIS READS A PROTECTED MEMBER OF THE SDK. CommandProcessor::gamma_ramp_256_entry_table()
// is declared in the class's protected section (rex/graphics/command_processor.h), so the
// application cannot call it on the object; it is named here through a derived class's
// using-declaration, and the resulting pointer to member (of type "member of
// CommandProcessor") is applied to the SDK's object. That is standard C++ and no object of
// the derived class is ever made. Why: the post chain's rulings 4 and 8 and the controller's
// ruling 20 (2026-10-09) want the ramp without an SDK change, and no hooked guest call
// carries it. A public accessor upstream replaces this file: then ReadSdkGammaRamp calls it
// and RampAccess goes.
//
// What it rests on: the accessor is inline, so it reads the table at the offset the
// application's copy of the header gives it; the build stages the SDK's headers and its
// source-built plugin from one tree (tools/build_runtime_sdk.cmd), which keeps them equal.
// Under a plugin built from another header the read would be of other memory inside the
// object; the consumer's rule (MakePostRamp: every entry at most 1023, no channel
// decreasing) is what stands between such bytes and a picture.
//
// Thread: the SDK writes the table in CommandProcessor::WriteRegister (the DC_LUT cases),
// which its packet execution calls on the command-processor thread; the guest's MMIO path
// (GraphicsSystem::WriteRegister) does not reach it. The native renderer calls the reader
// from the post-processor callback, on that same thread, so a read cannot be torn by a
// write. A ramp the guest has uploaded only in part when a frame is presented is not a torn
// read: it is the table the emulated picture of that frame went through as well.

#pragma once

#include <cstdint>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/runtime.h>

namespace fable2 {

namespace detail {
struct RampAccess : rex::graphics::CommandProcessor {
  using rex::graphics::CommandProcessor::gamma_ramp_256_entry_table;
};
}  // namespace detail

// Which ramp: this is the 256-entry table, which the SDK's swap blit applies to an 8-bit front
// buffer only; for a k_2_10_10_10 front buffer it applies the piecewise-linear ramp instead
// (d3d12/command_processor.cpp, the swap's gamma choice). Fable 2's front buffer is 8-bit (format
// 6). The choice is the consumer's to check: `out_front_format` gets the format field of the
// front buffer's texture fetch constant (the register file's fetch constant 0, which the swap
// itself reads through RegisterFile::GetTextureFetch(0): the reader runs inside that swap, on its
// thread, with no packet executed between), and the native renderer fails the ramp ("post-ramp")
// when that format takes the other ramp (native_render_state.h, RampTableApplies).
//
// `user` is the rex::Runtime. out_rgb10[i] = entry i as red, green, blue (10 bits each).
// False without a runtime, a graphics system or a command processor. The graphics system is
// taken to be the SDK's own rex::graphics::GraphicsSystem (what both staged plugins create).
inline bool ReadSdkGammaRamp(void* user, uint32_t out_rgb10[256][3], uint32_t* out_front_format) {
  auto* runtime = static_cast<rex::Runtime*>(user);
  if (!runtime) return false;
  auto* graphics = static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  const rex::graphics::CommandProcessor* cp = graphics ? graphics->command_processor() : nullptr;
  if (!cp) return false;
  const rex::graphics::reg::DC_LUT_30_COLOR* table = (cp->*&detail::RampAccess::gamma_ramp_256_entry_table)();
  if (!table) return false;
  for (int i = 0; i < 256; ++i) {
    out_rgb10[i][0] = table[i].color_10_red;
    out_rgb10[i][1] = table[i].color_10_green;
    out_rgb10[i][2] = table[i].color_10_blue;
  }
  if (out_front_format) *out_front_format = uint32_t(graphics->register_file()->GetTextureFetch(0).format);
  return true;
}

}  // namespace fable2
