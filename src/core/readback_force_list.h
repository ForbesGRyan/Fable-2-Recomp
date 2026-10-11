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

// The far woods' impostors (sub-project 12; docs/native-renderer/frame-map.md section 19, "The read-back
// list"): the game renders one 256 x 256 picture a tree model every frame and resolves each to its own
// texture; the fourteen bases lie 0x2B000 apart from the hero's entry on (impostor 0's picture IS the
// hero and dog resolve's address: measured in five captures of one save at two places; the addresses are
// the game's allocations, so another region may put them elsewhere, which this fixed list would not
// follow: the native view then draws no impostor, as with the switch off). A picture's three stored
// lower levels start 0x20000 from its base and are made by copy passes of their own: no entry's window
// reaches them and none is asked for (the native view makes its levels itself).
inline constexpr uint32_t kImpostorPictureBase = 0x12704000u, kImpostorPictureStep = 0x2B000u, kImpostorPictures = 14;
inline constexpr uint32_t kImpostorLevelOffset = 0x20000u;       // a picture's lower levels start here from its base
constexpr uint32_t ImpostorPictureBase(uint32_t n) { return kImpostorPictureBase + n * kImpostorPictureStep; }
static_assert(kImpostorPictureBase == kHeroDogResolveBase);      // impostor 0's base is the hero's entry
static_assert(kImpostorPictureStep >= kForceReadbackWindow);     // no window reaches a neighbour's
static_assert(kImpostorLevelOffset >= kForceReadbackWindow && kImpostorLevelOffset < kImpostorPictureStep);   // no level address lies in a window
static_assert(kLuminanceResolveBases[1] - ImpostorPictureBase(kImpostorPictures - 1) >= kForceReadbackWindow);

// The list for the things the application wants read back: the hero and dog texture fix, the game's
// exposure (--fable2_exposure_readback) and the impostors' pictures (--fable2_native_impostors=on). Each
// leaves the others' entries as they are. `impostors`: the fourteen picture bases as well (the first is
// the hero's entry: not listed twice). Without the third input, and with it false, the lists of before.
inline std::string ForceReadbackList(bool hero_dog, bool exposure, bool impostors = false) {
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
  if (impostors)
    for (uint32_t n = hero_dog ? 1 : 0; n < kImpostorPictures; ++n) add(ImpostorPictureBase(n));
  return list;
}

// What the seeding's log lines gain inside their parentheses: nothing without the pictures (the lines of
// before, byte for byte), else the switch that asked for them.
constexpr const char* ForceReadbackSeedSuffix(bool impostors) { return impostors ? ", fable2_native_impostors=on" : ""; }

}  // namespace fable2
