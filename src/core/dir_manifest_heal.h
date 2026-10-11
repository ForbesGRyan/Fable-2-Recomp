// fable2_dir_manifest_heal.h
//
// Self-heals <game_data_root>/data/dir.manifest at startup.
//
// Why: the game's VFS indexes ALL of data/ from data/dir.manifest; a file that
// is not listed there is invisible to the guest, so content loads fail with
// null pointers (observed: deterministic "read of guest 0x0" AV at startup).
// The manifest is easy to lose or truncate:
//   - content migrated with an mtime-respecting copy (xcopy /D, robocopy, ...)
//     skips a newer build-created stub, or
//   - the build (tools/ensure_recomp_manifest.cmake) creates a stub containing
//     only the staged scripts\recomp\*.lua entries before the content exists.
// Fixing this by hand works but is fragile, so the exe repairs it itself.
//
// The manifest stores paths relative to the data/ root with backslashes and in
// the game's canonical (mostly lowercase) casing; the emulated FS matches
// case-insensitively (the shipped content ships e.g. as Shaders\Shaders.sbk
// while the manifest lists shaders\shaders.sbk), so we:
//   - compare existing entries case-insensitively,
//   - append missing entries in lowercase (the canonical casing),
//   - never rewrite or drop existing lines.
//
// Idempotent: a second run appends nothing. Only ever APPENDS, so hand-added
// lines (e.g. by tools/stage_content.cmd's merge or ensure_recomp_manifest)
// survive.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rex/logging/macros.h>  // REXSYS_* logging

namespace fable2::manifestheal {

// Ensures every file under <game_data_root>/data/ has an entry in
// <game_data_root>/data/dir.manifest. Returns the number of entries appended
// (0 when the manifest was already complete). Never fails hard: problems are
// logged and the startup continues with whatever manifest exists.
inline int EnsureComplete(const std::filesystem::path& game_data_root) {
  const std::filesystem::path manifest = game_data_root / "data" / "dir.manifest";
  std::error_code ec;
  const std::filesystem::path data_root = manifest.parent_path();
  if (!std::filesystem::is_directory(data_root, ec)) {
    REXSYS_WARN("[manifest] no {} directory; skipping dir.manifest self-heal",
                data_root.string());
    return 0;
  }

  // 1. Existing entries (compared case-insensitively).
  std::vector<std::string> existing;
  {
    std::ifstream in(manifest, std::ios::binary);
    std::string line;
    if (in) {
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string key = line;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) {
                         return static_cast<char>(std::tolower(c));
                       });
        existing.push_back(std::move(key));
      }
    }
  }
  std::sort(existing.begin(), existing.end());

  // 2. Everything under data/ (relative, backslash form, lowercased).
  std::vector<std::string> missing;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(
           data_root,
           std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    std::string rel =
        std::filesystem::relative(entry.path(), data_root, ec).generic_string();
    if (rel.empty() || rel == "dir.manifest") continue;  // the index itself
    std::transform(rel.begin(), rel.end(), rel.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    // Compare in the manifest's own form (backslashes): generic_string() gives
    // forward slashes, so this comparison matched nothing and every start
    // appended the whole tree again (582 copies of each entry were found in a
    // staged manifest on 2026-10-10).
    std::replace(rel.begin(), rel.end(), '/', '\\');
    if (std::binary_search(existing.begin(), existing.end(), rel)) continue;
    missing.push_back(std::move(rel));
  }
  if (missing.empty()) return 0;

  std::sort(missing.begin(), missing.end());

  // 3. Append (CRLF), starting a fresh line if the file did not end with one.
  std::ofstream out(manifest, std::ios::binary | std::ios::app);
  if (!out) {
    REXSYS_ERROR("[manifest] cannot open {} for append; guest VFS index will "
                 "be incomplete ({} unlisted file(s))",
                 manifest.string(), missing.size());
    return 0;
  }
  {
    std::ifstream in(manifest, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    if (!all.empty() && all.back() != '\n') out << "\r\n";
  }
  for (const auto& m : missing) {
    // lowercase relative path with backslashes = the canonical manifest form
    out << m << "\r\n";
  }
  out.flush();
  REXSYS_INFO("[manifest] appended {} missing entr{} to {}", missing.size(),
              missing.size() == 1 ? "y" : "ies", manifest.string());
  return static_cast<int>(missing.size());
}

}  // namespace fable2::manifestheal
