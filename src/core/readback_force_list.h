// The list of resolves the SDK is told to read back to guest memory although
// --readback_resolve is "none" (the SDK's switch readback_resolve_force_addresses, seeded by
// Fable2App::OnPostSetup). Pure: no SDK header, no game; tests/native/test_readback_force_list.cpp.
//
// The SDK's rule (CommandProcessor::ShouldForceReadbackResolve, thirdparty/rexglue-sdk/src/
// graphics/command_processor.cpp): the list is read at every resolve (a hot-reload switch, parsed
// each time: entries divided by commas, each 0x-hex or decimal), and a resolve is forced when it
// was issued as PM4_DRAW_INDX_2, is a rectangle list of 3 indices and has its destination base in
// [entry, entry + 64 KiB). A forced resolve is read back at once: the command processor waits for
// the GPU to finish everything queued so far, then copies the bytes into guest memory.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace fable2 {

// The width of an entry's window in the SDK (kForceReadbackWindow there; its help text says 1 MiB).
inline constexpr uint32_t kForceReadbackWindow = 1u << 16;

// The resolve that regenerates the hero's and the dog's face and skin texture
// (plans/hero-dog-texture-readback.md; fable2_config.toml, hero_dog_texture_readback).
inline constexpr uint32_t kHeroDogResolveBase = 0x12704000u;

// The post chain's luminance resolve (280 x 180 floats at pitch 288, format 36; pixel shader
// 5821366EE8FDE893), which the game's CPU reads to set its exposure: without it the game stays at
// its blind scale (c20.w = 16). The game resolves to two buffers in turn, frame by frame, 0x36000
// bytes apart (the tiled size of one), so one entry cannot cover both. Measured:
// docs/native-renderer/frame-map.md section 17, "Discovery", question 1 (seven captures of five
// boots of one save at one place; the addresses are the game's allocations, so another region or
// boot path may put the buffers elsewhere, which this fixed list would not follow: the game then
// stays blind, as with the switch off).
inline constexpr uint32_t kLuminanceResolveBases[2] = {0x1A385000u, 0x1A34F000u};

// No window reaches into another entry's, so each resolve is matched by its own entry only.
static_assert(kLuminanceResolveBases[0] - kLuminanceResolveBases[1] >= kForceReadbackWindow);
static_assert(kLuminanceResolveBases[1] - kHeroDogResolveBase >= kForceReadbackWindow);

// The list for the two things the application wants read back: the hero and dog texture fix, and
// the game's exposure (--fable2_exposure_readback). Either leaves the other's entries as they are.
inline std::string ForceReadbackList(bool hero_dog, bool exposure) {
  std::string list;
  const auto add = [&list](uint32_t base) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", base);
    if (!list.empty()) list += ',';
    list += text;
  };
  if (hero_dog) add(kHeroDogResolveBase);
  if (exposure)
    for (uint32_t base : kLuminanceResolveBases) add(base);
  return list;
}

}  // namespace fable2
