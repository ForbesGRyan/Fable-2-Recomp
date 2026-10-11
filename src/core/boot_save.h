// fable_2 - Boot save: pure logic (std only). The runtime side lives in
// src/core/fable2_boot_save.h; the driver side in tools/drive_game.ps1.
//
// FABLE2_BOOT_SAVE=Hero003 loads that save from the front end without menu input.
// FABLE2_SKIP_LOGOS=1 drops the boot logo videos. FABLE2_BOOT_TRACE=1 logs front-end
// state changes. All unset: nothing changes.
//
// Guest addresses are valid for code group "goty-identical-payload-2026-09-28"
// (constants/game_versions.h; fable2_boot_save.h asserts it at compile time).

#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>

namespace fable2::bootsave {

// The retail front end's Load Game / Continue handlers (sub_826C9280, sub_826C72A8,
// sub_826C81A0) read a save's info and call kStartFromSaveFn with its name and level.
inline constexpr uint32_t kStartFromSaveFn = 0x8237A2D8;  // (gui, save name, level name)
// Saves are read from the user's storage device; the Continue handler picks it first.
inline constexpr uint32_t kHasDeviceFn = 0x824E54C0;      // (save manager, user) -> bool
inline constexpr uint32_t kSelectDeviceFn = 0x824E5398;   // (save manager, user, show size ui = 0) -> bool
inline constexpr uint32_t kSaveInfoCtorFn = 0x824E4E30;   // (info)
inline constexpr uint32_t kSaveInfoFn = 0x824EA7A0;       // (save manager, info, name, user) -> bool; reads the save
inline constexpr uint32_t kStringCtorFn = 0x8222CF18;     // (dest, cstr, len = -1)
inline constexpr uint32_t kStringDtorFn = 0x82214F08;     // (str)
// Save info: 36 bytes; level name (game string) at +12, a second string at +16, a valid byte at +24.
inline constexpr uint32_t kInfoLevelName = 12;
inline constexpr uint32_t kInfoString2 = 16;
inline constexpr uint32_t kInfoValid = 24;
// root = *kGameRoot; game = *(root + 12); save manager = *(game + 40); gui = *(*(root + 8) + 80).
inline constexpr uint32_t kGameRoot = 0x83496928;
inline constexpr uint32_t kGameLeftFrontEnd = 92;       // byte in game: 0 while the front end runs
inline constexpr uint32_t kFrontEndState = 0x83496930;  // u32, traced only
inline constexpr uint32_t kSignedInMask = 0x834968D8;   // u32, bit per user, traced only
inline constexpr uint32_t kActiveUser = 0x83496934;     // u32, the pad that pressed A

inline constexpr uint32_t kAttemptEvery = 30;  // frames between load attempts (about one second)
// FABLE2_BOOT_SAVE_MIN_UPDATE=N: no load attempt before main-loop update N (default 0). A
// knob for the startup race seen twice (logs 575 and perf worktree 002: a guest read of
// 0x4 on the game thread in the millisecond the first attempt started the load).
inline uint32_t MinUpdate() {
  static const uint32_t v = [] {
    const char* e = std::getenv("FABLE2_BOOT_SAVE_MIN_UPDATE");
    return e ? uint32_t(std::max(0, std::atoi(e))) : 0u;
  }();
  return v;
}
inline constexpr uint32_t kMaxAttempts = 60;   // then give up and leave the menu alone

// Set once at startup, before any guest thread runs; read-only afterwards.
struct Config {
  std::string hero;  // empty: not armed
  bool skip_logos = false;
  bool trace = false;
  std::filesystem::path saves_root;
};
inline Config& config() {
  static Config c;
  return c;
}

// "Hero" + exactly three digits (surrounding spaces ignored, case canonicalised to "HeroNNN").
inline std::optional<std::string> ParseHeroName(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  if (s.size() != 7) return std::nullopt;
  static constexpr std::string_view kPrefix = "hero";
  for (size_t i = 0; i < 4; ++i)
    if (std::tolower(static_cast<unsigned char>(s[i])) != kPrefix[i]) return std::nullopt;
  for (size_t i = 4; i < 7; ++i)
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
  return "Hero" + std::string(s.substr(4));
}

// Paces the load attempts: one every kAttemptEvery frames while the front end runs, at
// most kMaxAttempts, none once the front end has been left (the player loaded by hand)
// and none after Finish. Touched only by the guest thread that runs the main loop.
struct Trigger {
  bool done = false;
  bool seen_front_end = false;
  bool gave_up = false;  // kMaxAttempts attempts were made and none started the load
  uint64_t updates = 0;
  uint32_t attempts = 0;

  // True when a load attempt should be made on this update.
  bool Step(bool in_front_end) {
    ++updates;
    if (done) return false;
    if (!in_front_end) {
      if (seen_front_end) done = true;
      return false;
    }
    seen_front_end = true;
    if (updates < MinUpdate()) return false;
    if (updates % kAttemptEvery != 0) return false;
    if (attempts >= kMaxAttempts) {
      done = true;
      gave_up = true;
      return false;
    }
    ++attempts;
    return true;
  }
  // The load was started, or boot save gave up: no further attempts this run.
  void Finish() { done = true; }
};

struct FindResult {
  std::filesystem::path dir;  // empty on failure
  std::string error;          // empty on success
};

// <saves_root>/<profile>/4D5307F1/00000001/<hero>/mainsave.bin, in exactly one profile.
inline FindResult FindHeroDir(const std::filesystem::path& saves_root, std::string_view hero) {
  FindResult out;
  std::error_code ec;
  std::filesystem::directory_iterator it(saves_root, ec);
  if (ec) {
    out.error = "saves folder not readable: " + saves_root.string();
    return out;
  }
  int found = 0;
  std::filesystem::path first;
  for (const auto& profile : it) {
    const std::filesystem::path dir = profile.path() / "4D5307F1" / "00000001" / hero;
    if (std::filesystem::is_regular_file(dir / "mainsave.bin", ec)) {
      if (found++ == 0) first = dir;
    }
  }
  if (found == 0) {
    out.error = "no save folder " + std::string(hero) + " under " + saves_root.string();
  } else if (found > 1) {
    out.error = "save folder " + std::string(hero) + " exists under " + std::to_string(found) +
                " profiles; keep one";
  } else {
    out.dir = first;
  }
  return out;
}

}  // namespace fable2::bootsave
