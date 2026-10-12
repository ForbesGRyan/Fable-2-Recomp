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

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

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

// Whether the pictures' bases go into the list (ledger ruling 47): the switch at `on` AND the native renderer
// on (--fable2_native_render). The pictures' only reader is the native view; with the renderer off the list
// would cost about 1.8 ms a frame for nothing (the emulated view samples the host's texture). The exposure's
// entries are different: the game itself reads them, renderer or not.
constexpr bool ImpostorPicturesInList(bool native_render, bool impostors_on) { return native_render && impostors_on; }

// The list for the things the application wants read back: the hero and dog texture fix, the game's
// exposure (--fable2_exposure_readback) and the impostors' pictures (ImpostorPicturesInList). Each
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

// What the seeded line gains inside its parentheses: nothing with the impostors' switch off or flat (the line
// of before, byte for byte, whatever the renderer); with it on, whether the bases were listed and why not.
constexpr const char* ForceReadbackSeedSuffix(bool impostors_on, bool native_render) {
  return !impostors_on ? ""
         : native_render ? ", fable2_native_impostors=on with fable2_native_render=true: the fourteen picture bases listed"
                         : ", fable2_native_impostors=on without fable2_native_render: the picture bases NOT listed";
}
// And the line of a list given by hand, which is never seeded: nothing with the switch off or flat.
constexpr const char* ForceReadbackHandSuffix(bool impostors_on) {
  return impostors_on ? ", fable2_native_impostors=on: the pictures are read back only if this list holds their fourteen bases" : "";
}

// The SDK's rule for one base, as this file's head states it (a reading of ShouldForceReadbackResolve's list
// part; tests/native/test_readback_force_list.cpp holds it to that test's own copy): entries divided by commas,
// blanks around an entry dropped, each read by std::stoull with base 0 (a malformed entry is address 0), an
// entry matching [entry, entry + 64 KiB).
inline bool ForceListForces(std::string_view spec, uint32_t base) {
  size_t start = 0;
  while (start < spec.size()) {
    size_t end = spec.find(',', start);
    if (end == std::string_view::npos) end = spec.size();
    std::string_view token = spec.substr(start, end - start);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    if (!token.empty()) {
      unsigned long long addr = 0;
      try {
        size_t pos = 0;
        addr = std::stoull(std::string(token), &pos, 0);
      } catch (const std::exception&) {
        addr = 0;
      }
      const uint32_t a = static_cast<uint32_t>(addr);
      if (base >= a && base - a < kForceReadbackWindow) return true;
    }
    start = end + 1;
  }
  return false;
}
// Whether a list, however it came about (seeded or given by hand), brings all fourteen pictures to guest
// memory: each base forced by some entry. A hand-given list of the fourteen bases has the pictures as a seeded
// one has (the cost sessions' `fourteen` runs were such lists).
inline bool ForceListHasImpostorPictures(std::string_view spec) {
  for (uint32_t n = 0; n < kImpostorPictures; ++n) {
    if (!ForceListForces(spec, ImpostorPictureBase(n))) return false;
  }
  return true;
}

// The read-back mode and the pictures (review of the whole branch, I1; ledger ruling 54). The SDK has four modes
// (--readback_resolve; D3D12CommandProcessor::IssueCopy and IssueCopy_ReadbackResolvePath, thirdparty/rexglue-sdk/
// src/graphics/d3d12/command_processor.cpp). What a resolve of one of the fourteen bases does in each:
//   none (the default): only a resolve the list forces takes the read-back path, and there it waits for the GPU to
//         finish everything queued and copies THIS resolve's bytes into guest memory: the frame's own picture.
//         A base the list does not force is not copied at all.
//   full: every resolve takes that path, waits and copies its own bytes: the frame's own picture, list or not.
//   fast: every resolve takes the path, the list changes nothing, and nothing waits: guest memory gets the buffer
//         of the PREVIOUS resolve of the same address and length (the first resolve of an address alone waits and
//         copies itself). Every picture is a frame old.
//   some: as fast, but guest memory is written only when there is no previous buffer: the FIRST resolve of an
//         address. Every picture stays its first frame's for the whole run.
// The legacy --d3d12_readback_resolve=true means fast while --readback_resolve is at its default.
// So the pictures are each frame's own in `none` with the fourteen bases forced, and in `full`. In `fast` and
// `some` they are whole pictures that pass the content rule and are old or frozen, which nothing downstream can
// tell: the native view draws none there, and says why.
enum class ReadbackMode : uint8_t { kNone, kFast, kSome, kFull };
// The SDK's effective mode from its two switches' values (CommandProcessor::GetReadbackResolveMode with
// ParseReadbackResolveMode, graphics/command_processor.cpp): the shared switch's word, another word than the four
// being none; the legacy switch counts only while the shared one holds its default, the word "none".
constexpr ReadbackMode EffectiveReadbackMode(std::string_view readback_resolve, bool legacy_d3d12_readback_resolve) {
  if (readback_resolve == "fast") return ReadbackMode::kFast;
  if (readback_resolve == "some") return ReadbackMode::kSome;
  if (readback_resolve == "full") return ReadbackMode::kFull;
  return readback_resolve == "none" && legacy_d3d12_readback_resolve ? ReadbackMode::kFast : ReadbackMode::kNone;
}
// Whether the run brings each frame's own pictures to guest memory, and if not, why.
enum class ImpostorReadback : uint8_t {
  kListed,     // none, the list forces the fourteen bases: available
  kFull,       // full: available, whatever the list holds
  kNoList,     // none, and the list does not force all fourteen: not available ("picture: base")
  kModeFast,   // fast: a frame old, not available ("picture: mode")
  kModeSome,   // some: the first frame's, not available ("picture: mode")
};
// `list_forces_fourteen`: ForceListHasImpostorPictures of the list as it stands (seeded or given by hand).
constexpr ImpostorReadback DecideImpostorReadback(ReadbackMode mode, bool list_forces_fourteen) {
  switch (mode) {
    case ReadbackMode::kFull: return ImpostorReadback::kFull;
    case ReadbackMode::kFast: return ImpostorReadback::kModeFast;
    case ReadbackMode::kSome: return ImpostorReadback::kModeSome;
    default: return list_forces_fourteen ? ImpostorReadback::kListed : ImpostorReadback::kNoList;
  }
}
constexpr bool ImpostorReadbackAvailable(ImpostorReadback r) { return r == ImpostorReadback::kListed || r == ImpostorReadback::kFull; }
// Not available because of the mode (the pass counts such a record "picture: mode"; a missing list "picture: base").
constexpr bool ImpostorReadbackByMode(ImpostorReadback r) { return r == ImpostorReadback::kModeFast || r == ImpostorReadback::kModeSome; }
// The words of the log's line and of the install line.
constexpr const char* ImpostorReadbackWhy(ImpostorReadback r) {
  switch (r) {
    case ImpostorReadback::kListed:
      return "available: readback_resolve none and the read-back list forces the fourteen bases (each forced resolve waits for the GPU and "
             "copies its own bytes: the frame's own pictures)";
    case ImpostorReadback::kFull:
      return "available: readback_resolve full (every resolve waits for the GPU and copies its own bytes: the frame's own pictures)";
    case ImpostorReadback::kNoList:
      return "NOT available: readback_resolve none and the read-back list does not force the fourteen bases; no far tree is drawn, every "
             "record is \"picture: base\"";
    case ImpostorReadback::kModeFast:
      return "NOT available: readback_resolve fast (also what --d3d12_readback_resolve=true means) copies the PREVIOUS resolve's bytes: "
             "every picture would be a frame old; no far tree is drawn, every record is \"picture: mode\" (use readback_resolve none or full)";
    case ImpostorReadback::kModeSome:
      return "NOT available: readback_resolve some copies only the first resolve of an address: every picture would stay its first "
             "frame's for the whole run; no far tree is drawn, every record is \"picture: mode\" (use readback_resolve none or full)";
  }
  return "?";
}

}  // namespace fable2
