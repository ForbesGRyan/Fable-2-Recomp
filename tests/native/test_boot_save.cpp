// Synthetic standalone test; no game or GPU.
#include "../../src/core/boot_save.h"
#include <fstream>
#include <iostream>

using namespace fable2::bootsave;
namespace fs = std::filesystem;

static void MakeSave(const fs::path& root, const char* profile, const char* hero) {
  const fs::path dir = root / profile / "4D5307F1" / "00000001" / hero;
  fs::create_directories(dir);
  std::ofstream(dir / "mainsave.bin") << "x";
}

int main() {
  // Names: exactly "Hero" + three digits; case is canonicalised; anything else is rejected.
  if (ParseHeroName("Hero003") != std::optional<std::string>("Hero003")) return 1;
  if (ParseHeroName(" hero012 ") != std::optional<std::string>("Hero012")) return 2;
  for (const char* bad : {"", " ", "Hero", "Hero1", "Hero0003", "Hero00a", "..\\Hero000",
                          "Hero000\\..", "Hero 00", "Hero000/x", "Zero000"})
    if (ParseHeroName(bad)) { std::cerr << "accepted '" << bad << "'\n"; return 3; }

  // Trigger: one load attempt every kAttemptEvery game updates while in the front end,
  // none after Finish (started or given up), none while the game is not in the front end.
  {
    Trigger t;
    for (uint32_t i = 1; i < kAttemptEvery; ++i)
      if (t.Step(true)) return 10;
    if (!t.Step(true)) return 11;                 // update kAttemptEvery
    if (t.Step(true)) return 12;
    for (uint32_t i = 2; i < kAttemptEvery; ++i)
      if (t.Step(true)) return 13;
    if (!t.Step(true)) return 14;                 // update 2 * kAttemptEvery
    t.Finish();
    for (uint32_t i = 0; i < 3 * kAttemptEvery; ++i)
      if (t.Step(true)) return 15;                // never again (quit to the front end later)
    if (t.updates != 5 * kAttemptEvery) return 16;
  }
  {
    Trigger t;
    for (uint32_t i = 0; i < 3 * kAttemptEvery; ++i)
      if (t.Step(false)) return 17;               // in game: no attempt
  }

  // Save folder lookup.
  const fs::path root = fs::temp_directory_path() / "fable2_boot_save_test";
  fs::remove_all(root);
  fs::create_directories(root);
  FindResult none = FindHeroDir(root, "Hero003");
  if (!none.dir.empty() || none.error.find("no save folder Hero003") == std::string::npos) return 30;
  MakeSave(root, "B13EBABEBABEBABE", "Hero003");
  FindResult one = FindHeroDir(root, "Hero003");
  if (!one.error.empty() || one.dir.filename() != "Hero003") return 31;
  if (!FindHeroDir(root, "Hero004").dir.empty()) return 32;
  MakeSave(root, "E000000000000001", "Hero003");
  FindResult two = FindHeroDir(root, "Hero003");
  if (!two.dir.empty() || two.error.find("2 profiles") == std::string::npos) return 33;
  FindResult missing_root = FindHeroDir(root / "does_not_exist", "Hero003");
  if (!missing_root.dir.empty() || missing_root.error.empty()) return 34;
  fs::remove_all(root);

  std::cout << "boot_save ok\n";
  return 0;
}
