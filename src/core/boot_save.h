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
#include <string>
#include <string_view>
#include <system_error>

namespace fable2::bootsave {

inline constexpr uint32_t kStartGameFn = 0x826962D0;      // (front end, user index)
inline constexpr uint32_t kSaveQueryCtorFn = 0x824E4E30;  // (query); strings at +12 and +16
inline constexpr uint32_t kSaveExistsFn = 0x824EA7A0;     // (save manager, query, name, user) -> bool
inline constexpr uint32_t kStringCtorFn = 0x8222CF18;     // (dest, cstr, len = -1)
inline constexpr uint32_t kStringAssignFn = 0x82265160;   // (dest, src)
inline constexpr uint32_t kStringDtorFn = 0x82214F08;     // (str)
inline constexpr uint32_t kInitialSaveName = 0x834973BC;  // game string
inline constexpr uint32_t kLoadClearedByte = 0x8349693F;  // the Load Game handler stores 0 here
inline constexpr uint32_t kFrontEndState = 0x83496930;    // u32
inline constexpr uint32_t kGameRoot = 0x83496928;         // save manager = *(*(*root + 12) + 40)

// The front-end input dispatcher (sub_82692048) handles input only in this state range.
inline constexpr uint32_t kFireStateMin = 3;
inline constexpr uint32_t kFireStateMax = 8;
inline constexpr uint32_t kStableUpdates = 2;  // consecutive front-end updates in range before firing
inline constexpr uint32_t kUserIndex = 0;

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

enum class Action { kWait, kFire, kDone };

// Decides when to start the load: once, after kStableUpdates consecutive front-end
// updates with the state in range. Touched only by the guest thread that runs the update.
struct Trigger {
  uint32_t stable = 0;
  bool fired = false;
  uint64_t updates = 0;

  Action Step(uint32_t fe_state) {
    ++updates;
    if (fired) return Action::kDone;
    if (fe_state < kFireStateMin || fe_state > kFireStateMax) {
      stable = 0;
      return Action::kWait;
    }
    if (++stable < kStableUpdates) return Action::kWait;
    fired = true;
    return Action::kFire;
  }
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
