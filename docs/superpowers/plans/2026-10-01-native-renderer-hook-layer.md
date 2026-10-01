# Native Renderer Hook Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port skate3recomp's native-renderer hook layer into our ReXGlue SDK fork (D3D12 only) and add Fable 2 game plumbing that proves it end to end with a test pattern (`replace` mode) and a grid drawn over the emulated frame (`overlay` mode), toggled live with F6 and falling back to emulation on any failure.

**Architecture:** The SDK gains a callback registry (`native_guest_renderer`, compiled into `rexruntime.dll` and exported through the curated `.def`), a backend-agnostic RHI interface (`native_rhi.h`), and its D3D12 implementation (in the `rexgpu-xenos` plugin). The D3D12 command processor calls the registered renderer at swap, invokes an optional post-processor after the emulated blit, and can suppress emulated draws/resolves by render-target pitch. The game registers both callbacks from `src/native/` behind cvars.

**Tech Stack:** C++23, clang/lld, CMake + Ninja, D3D12, D3DCompile (runtime HLSL, SM 5.0), Python 3 `unittest`, ReXGlue SDK fork `ForbesGRyan/rexglue-sdk` branch `renderer` (based on `babc769`).

**Spec:** `docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md` (sub-project 1)

## Global Constraints

- SDK work happens in `thirdparty/rexglue-sdk` on branch `renderer` (remote `origin` = `https://github.com/ForbesGRyan/rexglue-sdk.git`); commits there are normal SDK commits, and the parent repo records the new submodule commit.
- Source material is skate3's SDK fork: `https://github.com/mchughalex/rexglue-skate3.git`, branch `skate3-sdk-clean`. Fetch it into the submodule as `FETCH_HEAD`; never copy from a scratch directory.
- D3D12 only. The Vulkan command processor must not call the native hook (Vulkan = always emulated) until sub-project 8.
- `native_guest_renderer.cpp` is compiled into `rexruntime.dll`; every public function it defines is exported via `src/system/rexruntime.def`.
- Default behaviour must be unchanged: cvar `fable2_native_render` defaults to `false`, `native_render_suppress_emulated_draws` defaults to `false`, `native_render_suppress_pitches` defaults to empty (suppress nothing).
- Toggle hotkey is **F6** (VK `0x75`); F5 stays the Lua runner.
- Game files (XEX, data) never enter the repo.
- Build entry point: `build.cmd -release fable_2` from the repo root (runs codegen, `tools\build_runtime_sdk.cmd`, then the game). If local antivirus blocks the official prebuilt SDK download, put a source-built SDK's `bin` (containing `rexglue.exe`) first on `PATH`; `build.cmd` prefers a `rexglue.exe` found on `PATH`.
- Shell commands in this repo follow the user's convention of prefixing with `rtk` where a dedicated filter exists (e.g. `rtk git status`); plain commands are fine otherwise.

## Review Focus

- **Malformed pitch lists** (`native_render_suppress_pitches = "1280,abc, 640,,"`): valid tokens apply, invalid tokens are ignored and counted so a warning can be logged; the game must never crash or suppress everything by accident. Test: Task 2 `test_native_suppress_policy.cpp` cases 3–4.
- **Keep list beats suppress list** (pitch present in both, or `*` with a keep entry): the pass must execute. Test: Task 2 case 5.
- **Shader compile failure at runtime** (bad HLSL or a driver without SM 5.0): one log line, sticky fallback to emulated output, F6 retries. Test: Task 6 `test_native_render_state.cpp` latch cases + Task 8 manual step "force a compile error".
- **Unknown `fable2_native_render_mode` value** (e.g. `"Replace "`, `"foo"`): treated as `overlay` after trimming/lower-casing, with a one-time warning; never a crash. Test: Task 6 `ParseMode` cases.
- **Vulkan plugin selected** (`--gpu_plugin=xenos-vulkan`) with native enabled: callbacks never run, game renders emulated, no errors. Test: Task 8 manual step "Vulkan unaffected".

---

### Task 0: Baseline build at the current pin

Establish that the unmodified tree builds and runs before changing anything, so later failures are attributable.

**Files:** none modified.

- [ ] **Step 1: Confirm submodule state**

Run:
```
rtk git -C thirdparty/rexglue-sdk status -sb
rtk git -C thirdparty/rexglue-sdk log --oneline -1
```
Expected: `## renderer`, HEAD `babc769 Point libmspack at a commit that exists upstream`, clean tree.

- [ ] **Step 2: Build Release**

Run (Developer PowerShell or any shell with clang/cmake/ninja on PATH):
```
build.cmd -release fable_2
```
Expected: ends with the `fable_2.exe` link and staging steps, exit code 0. If the official SDK download is blocked by antivirus, prepend a source-built SDK `bin` directory (one containing `rexglue.exe`) to `PATH` and rerun.

- [ ] **Step 3: Smoke-run the game**

Stage content if `out\build\win-amd64-release\data` is not the full game (see `tools\stage_content.cmd`), then run `out\build\win-amd64-release\fable_2.exe --fullscreen=false`. Expected: reaches the main menu. Note the F3 overlay FPS in the main menu for later comparison.

- [ ] **Step 4: Record baseline**

No commit. Write the FPS and build success into your task notes.

---

### Task 1: Fold runtime patches into the `renderer` branch and relax the pin check

`tools/prepare_runtime_sdk.py` refuses any SDK HEAD other than `babc769` and applies two patch files to the worktree on every build. Fold those patches into the branch as a commit and let the script accept any descendant of the pin, so new SDK commits don't require pin bumps.

**Files:**
- Modify: `tools/prepare_runtime_sdk.py`
- Create: `tests/test_prepare_runtime_sdk.py`
- SDK commit on `renderer`: files touched by `thirdparty/rexglue-sdk-runtime-fixes.patch` and `thirdparty/rexglue-sdk-debug-exports.patch`, plus the `thirdparty/libmspack` gitlink.

**Interfaces:**
- Produces: `prepare_runtime_sdk.check_revision(source: Path) -> None` (raises `SystemExit` when HEAD is neither `SDK_PIN` nor a descendant of it).

- [ ] **Step 1: Write the failing test**

Create `tests/test_prepare_runtime_sdk.py`:
```python
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_runtime_sdk", ROOT / "tools" / "prepare_runtime_sdk.py")
prep = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prep)


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def commit(repo, name):
    (repo / name).write_text(name)
    git(repo, "add", name)
    git(repo, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", name)
    return git(repo, "rev-parse", "HEAD")


class CheckRevisionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        git(self.repo, "init", "-q")
        self.pin = commit(self.repo, "pin")
        self.saved_pin = prep.SDK_PIN
        prep.SDK_PIN = self.pin

    def tearDown(self):
        prep.SDK_PIN = self.saved_pin
        self.tmp.cleanup()

    def test_accepts_exact_pin(self):
        prep.check_revision(self.repo)

    def test_accepts_descendant_of_pin(self):
        commit(self.repo, "renderer-work")
        prep.check_revision(self.repo)

    def test_rejects_unrelated_history(self):
        git(self.repo, "checkout", "-q", "--orphan", "other")
        commit(self.repo, "unrelated")
        with self.assertRaises(SystemExit):
            prep.check_revision(self.repo)

    def test_rejects_ancestor_of_pin(self):
        git(self.repo, "checkout", "-q", "HEAD")
        commit(self.repo, "newer")
        prep.SDK_PIN = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", self.pin)
        with self.assertRaises(SystemExit):
            prep.check_revision(self.repo)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_prepare_runtime_sdk -v`
Expected: FAIL/ERROR with `AttributeError: module 'prepare_runtime_sdk' has no attribute 'check_revision'`.

- [ ] **Step 3: Implement `check_revision`**

In `tools/prepare_runtime_sdk.py`, add above `main()`:
```python
def check_revision(source):
    """Accept the pinned SDK commit or any commit that descends from it."""
    head = git(source, "rev-parse", "HEAD").stdout.strip()
    if head == SDK_PIN:
        return
    if git(source, "merge-base", "--is-ancestor", SDK_PIN, "HEAD", check=False).returncode == 0:
        return
    raise SystemExit(f"Expected SDK commit {SDK_PIN} or a descendant; got {head}.")
```
and replace the two lines
```python
    if git(source, "rev-parse", "HEAD").stdout.strip() != SDK_PIN:
        raise SystemExit(f"Expected SDK commit {SDK_PIN}; refusing to patch another revision.")
```
with
```python
    check_revision(source)
```
Also update the module docstring's first line to: `"""Apply the source-only runtime fixes to the tested SDK revision (or a descendant)."""`

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.test_prepare_runtime_sdk -v`
Expected: 4 tests, OK.

- [ ] **Step 5: Apply the patches and commit them on the SDK branch**

Run:
```
python tools\prepare_runtime_sdk.py thirdparty\rexglue-sdk
rtk git -C thirdparty/rexglue-sdk status -s
```
Expected: `Applied: rexglue-sdk-runtime-fixes.patch`, `Applied: rexglue-sdk-debug-exports.patch`, submodule update output, and a list of modified/new SDK files (e.g. `src/graphics/frame_limiter.cpp`, `src/system/rexruntime.def`, `thirdparty/libmspack`).

Then:
```
rtk git -C thirdparty/rexglue-sdk add -A
rtk git -C thirdparty/rexglue-sdk commit -m "Fold Fable 2 runtime fixes and debug exports into renderer branch"
python tools\prepare_runtime_sdk.py thirdparty\rexglue-sdk --skip-dependencies
```
Expected from the last command: `Already applied: rexglue-sdk-runtime-fixes.patch` and `Already applied: rexglue-sdk-debug-exports.patch`.

- [ ] **Step 6: Verify the build still works**

Run: `build.cmd -release fable_2`
Expected: exit code 0.

- [ ] **Step 7: Commit (parent repo)**

```
rtk git add tools/prepare_runtime_sdk.py tests/test_prepare_runtime_sdk.py thirdparty/rexglue-sdk
rtk git commit -m "Accept SDK descendants of the pin; record renderer branch with folded runtime fixes"
```

---

### Task 2: Pitch suppression policy (pure, header-only, tested)

Replace skate3's Skate-specific suppress modes with a generic pitch allow/deny policy. Keep the logic pure so it is unit-testable without the SDK runtime.

**Files:**
- Create (SDK): `thirdparty/rexglue-sdk/include/rex/graphics/native_suppress_policy.h`
- Create: `tests/native/test_native_suppress_policy.cpp`
- Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces (namespace `rex::graphics::native_suppress`):
  - `struct PitchList { bool all = false; std::vector<uint32_t> pitches; uint32_t invalid_tokens = 0; };`
  - `PitchList ParsePitchList(std::string_view text);`
  - `bool Contains(const PitchList& list, uint32_t pitch);`
  - `bool ShouldSuppressPitch(const PitchList& suppress, const PitchList& keep, uint32_t pitch);`

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_native_suppress_policy.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include <rex/graphics/native_suppress_policy.h>
#include <iostream>

using namespace rex::graphics::native_suppress;

int main() {
  // 1. Empty text suppresses nothing.
  PitchList none = ParsePitchList("");
  if (none.all || !none.pitches.empty() || none.invalid_tokens) return 1;
  if (ShouldSuppressPitch(none, none, 1280)) return 2;

  // 2. Explicit list with spaces.
  PitchList list = ParsePitchList(" 1280, 640 ,1024");
  if (list.pitches.size() != 3 || !Contains(list, 640) || Contains(list, 512)) return 3;

  // 3. Invalid and empty tokens are skipped and counted, valid ones kept.
  PitchList bad = ParsePitchList("1280,abc, 640,,99999999999,-5");
  if (bad.pitches.size() != 2 || !Contains(bad, 1280) || !Contains(bad, 640)) return 4;
  if (bad.invalid_tokens != 3) return 5;  // "abc", "99999999999", "-5"

  // 4. Garbage-only text never means "all".
  PitchList garbage = ParsePitchList("*x,?");
  if (garbage.all || garbage.invalid_tokens != 2) return 6;

  // 5. Wildcard suppresses everything except the keep list.
  PitchList all = ParsePitchList("*");
  PitchList keep = ParsePitchList("1024, 512");
  if (!all.all) return 7;
  if (!ShouldSuppressPitch(all, keep, 1280)) return 8;
  if (ShouldSuppressPitch(all, keep, 1024)) return 9;
  if (ShouldSuppressPitch(list, keep, 1024)) return 10;  // in both: keep wins

  std::cout << "PASS: empty, list, invalid tokens, garbage, wildcard + keep\n";
  return 0;
}
```

Add to `tests/run_native_tests.cmd`, before the final `exit /b 0`:
```
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_native_suppress_policy.cpp" -o "%OUT%\suppress.exe" || exit /b 1
"%OUT%\suppress.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tests\run_native_tests.cmd`
Expected: clang error `'rex/graphics/native_suppress_policy.h' file not found`.

- [ ] **Step 3: Implement the header**

Create `thirdparty/rexglue-sdk/include/rex/graphics/native_suppress_policy.h`:
```cpp
#pragma once

// Pure render-target-pitch suppression policy for the native guest-output
// renderer. Header-only so it is unit-testable without the runtime.
//
// A list is a comma-separated set of surface pitches ("1280, 640") or "*"
// (every pitch). Invalid tokens are skipped and counted so callers can warn;
// they never widen the list.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rex::graphics::native_suppress {

struct PitchList {
  bool all = false;
  std::vector<uint32_t> pitches;
  uint32_t invalid_tokens = 0;
};

inline std::string_view TrimToken(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

inline PitchList ParsePitchList(std::string_view text) {
  PitchList out;
  while (true) {
    const size_t comma = text.find(',');
    const std::string_view token = TrimToken(text.substr(0, comma));
    if (token == "*") {
      out.all = true;
    } else if (!token.empty()) {
      uint32_t value = 0;
      const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
      if (ec == std::errc{} && end == token.data() + token.size()) {
        out.pitches.push_back(value);
      } else {
        ++out.invalid_tokens;
      }
    }
    if (comma == std::string_view::npos) break;
    text.remove_prefix(comma + 1);
  }
  return out;
}

inline bool Contains(const PitchList& list, uint32_t pitch) {
  return list.all || std::find(list.pitches.begin(), list.pitches.end(), pitch) != list.pitches.end();
}

inline bool ShouldSuppressPitch(const PitchList& suppress, const PitchList& keep, uint32_t pitch) {
  if (Contains(keep, pitch)) return false;  // keep always wins
  return Contains(suppress, pitch);
}

}  // namespace rex::graphics::native_suppress
```

- [ ] **Step 4: Run test to verify it passes**

Run: `tests\run_native_tests.cmd`
Expected: earlier tests pass, then `PASS: empty, list, invalid tokens, garbage, wildcard + keep`, exit 0.

- [ ] **Step 5: Commit**

```
rtk git -C thirdparty/rexglue-sdk add include/rex/graphics/native_suppress_policy.h
rtk git -C thirdparty/rexglue-sdk commit -m "Add pure pitch suppression policy for the native renderer"
rtk git add tests/native/test_native_suppress_policy.cpp tests/run_native_tests.cmd thirdparty/rexglue-sdk
rtk git commit -m "Test native renderer pitch suppression policy"
```

---

### Task 3: Native guest-renderer registry and RHI interface in `rexruntime.dll`

**Files:**
- Create (SDK, from skate3 fork): `include/rex/graphics/native_rhi.h`
- Create (SDK, adapted from skate3 fork): `include/rex/graphics/native_guest_renderer.h`, `src/graphics/native_guest_renderer.cpp`
- Modify (SDK): `src/system/CMakeLists.txt` (add the .cpp to `REXSYSTEM_SOURCES`), `src/system/rexruntime.def`

**Interfaces:**
- Consumes: `rex::graphics::native_suppress::{ParsePitchList, ShouldSuppressPitch}` (Task 2).
- Produces (namespace `rex::graphics`, all exported from `rexruntime.dll`):
  - `enum class NativeGuestOutputBackend { kUnknown, kD3D12, kVulkan };`
  - `struct NativeGuestOutputRenderContext { backend; guest_output_width; guest_output_height; display_width; display_height; nrhi::Device* device; nrhi::Cmd* cmd; nrhi::Texture* guest_output; };`
  - `using NativeGuestOutputRenderer = bool (*)(const NativeGuestOutputRenderContext&, void*);`
  - `void SetNativeGuestOutputRenderer(NativeGuestOutputRenderer, void*);`
  - `bool TryRenderNativeGuestOutput(const NativeGuestOutputRenderContext&);`
  - `bool HasNativeGuestOutputRenderer();` `bool IsNativeGuestOutputActive();`
  - `void SetNativeGuestOutputWideAspect(double);` `double GetNativeGuestOutputWideAspect();`
  - `bool ApplyNativeGuestOutputWideAspect(uint32_t&, uint32_t, uint32_t&, uint32_t&);`
  - `bool ShouldSuppressEmulatedDraws();` `bool ShouldSuppressPassAtPitch(uint32_t);`
  - `using NativeGuestOutputPostProcessor = void (*)(const NativeGuestOutputRenderContext&, void*);`
  - `void SetNativeGuestOutputPostProcessor(NativeGuestOutputPostProcessor, void*);` `bool HasNativeGuestOutputPostProcessor();` `void InvokeNativeGuestOutputPostProcessor(const NativeGuestOutputRenderContext&);` `void RequestNativeGuestOutputPostProcess(bool);` `bool IsNativeGuestOutputPostProcessRequested();`
  - `rex::graphics::nrhi::SetShaderBytecodeCacheDirectory(const char*)`, `GetShaderBytecodeCacheDirectory()`.
  - Cvars: `native_render_suppress_emulated_draws` (bool, default false), `native_render_suppress_pitches` (string, default ""), `native_render_keep_pitches` (string, default "").

- [ ] **Step 1: Fetch skate3's SDK fork and copy the RHI header**

Run:
```
rtk git -C thirdparty/rexglue-sdk fetch https://github.com/mchughalex/rexglue-skate3.git skate3-sdk-clean
git -C thirdparty/rexglue-sdk show FETCH_HEAD:include/rex/graphics/native_rhi.h > thirdparty/rexglue-sdk/include/rex/graphics/native_rhi.h
git -C thirdparty/rexglue-sdk show FETCH_HEAD:include/rex/graphics/native_guest_renderer.h > thirdparty/rexglue-sdk/include/rex/graphics/native_guest_renderer.h
```
Expected: two files created (`native_rhi.h` ≈ 564 lines, `native_guest_renderer.h` ≈ 110 lines). Note the fetched commit hash for the commit message: `git -C thirdparty/rexglue-sdk rev-parse FETCH_HEAD`.

- [ ] **Step 2: Adapt the header**

In `native_guest_renderer.h`:
1. Replace the doc comment above `ShouldSuppressPassAtPitch` with:
```cpp
// While the native guest-output renderer is active: should the emulated pass
// currently targeting `surface_pitch`-wide surfaces be suppressed? Driven by
// native_render_suppress_pitches / native_render_keep_pitches (see
// rex/graphics/native_suppress_policy.h); shared by all command processors.
// Draw and resolve suppression must agree: executed passes need their
// resolves, suppressed passes leave garbage EDRAM that must never be copied
// out.
```
2. Delete the `ShouldSuppressExemptDepthOnlyDraws()` declaration and its comment (Skate-specific; not ported).

- [ ] **Step 3: Write `native_guest_renderer.cpp`**

Create `thirdparty/rexglue-sdk/src/graphics/native_guest_renderer.cpp`. Start from skate3's file (`git -C thirdparty/rexglue-sdk show FETCH_HEAD:src/graphics/native_guest_renderer.cpp`) and make it exactly this (the registry, wide-aspect and post-processor functions are unchanged from skate3; the cvars and suppression functions are replaced):
```cpp
#include <rex/graphics/native_guest_renderer.h>

#include <algorithm>
#include <atomic>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/native_suppress_policy.h>
#include <rex/logging.h>

// Defined in rexruntime.dll so the title and the GPU plugin share one
// registry, one active flag and one set of cvars.
REXCVAR_DEFINE_BOOL(native_render_suppress_emulated_draws, false, "GPU",
                    "While the registered native guest-output renderer is actively "
                    "replacing frames, skip emulated draw and resolve execution for the "
                    "passes selected by native_render_suppress_pitches (PM4 parsing, "
                    "fences, queries and memexport draws still run).");
REXCVAR_DEFINE_STRING(native_render_suppress_pitches, "", "GPU",
                      "Comma-separated render-target surface pitches whose emulated "
                      "draws/resolves are skipped while native output is active, or * "
                      "for all. Empty = suppress nothing.");
REXCVAR_DEFINE_STRING(native_render_keep_pitches, "", "GPU",
                      "Comma-separated surface pitches that always execute emulated, "
                      "even when matched by native_render_suppress_pitches.");

namespace rex::graphics {
namespace {

std::atomic<NativeGuestOutputRenderer> g_renderer{nullptr};
std::atomic<void*> g_renderer_user_data{nullptr};
std::atomic<bool> g_native_output_active{false};
std::atomic<NativeGuestOutputPostProcessor> g_post_processor{nullptr};
std::atomic<void*> g_post_processor_user_data{nullptr};
std::atomic<bool> g_post_process_requested{false};
std::atomic<double> g_wide_aspect{0.0};

// Parsed pitch lists, re-parsed only when the cvar text changes. Only the
// command-processor thread calls ShouldSuppressPassAtPitch.
struct CachedList {
  std::string text;
  native_suppress::PitchList list;
  bool parsed = false;
};

const native_suppress::PitchList& Cached(CachedList& cache, const std::string& text,
                                         const char* cvar_name) {
  if (!cache.parsed || cache.text != text) {
    cache.text = text;
    cache.list = native_suppress::ParsePitchList(text);
    cache.parsed = true;
    if (cache.list.invalid_tokens) {
      REXLOG_WARN("{}: ignored {} invalid pitch token(s) in '{}'", cvar_name,
                  cache.list.invalid_tokens, text);
    }
  }
  return cache.list;
}

}  // namespace

void SetNativeGuestOutputRenderer(NativeGuestOutputRenderer renderer, void* user_data) {
  g_renderer_user_data.store(user_data, std::memory_order_release);
  g_renderer.store(renderer, std::memory_order_release);
}

bool TryRenderNativeGuestOutput(const NativeGuestOutputRenderContext& context) {
  NativeGuestOutputRenderer renderer = g_renderer.load(std::memory_order_acquire);
  if (renderer == nullptr) {
    g_native_output_active.store(false, std::memory_order_relaxed);
    return false;
  }
  const bool rendered = renderer(context, g_renderer_user_data.load(std::memory_order_acquire));
  g_native_output_active.store(rendered, std::memory_order_relaxed);
  return rendered;
}

bool HasNativeGuestOutputRenderer() {
  return g_renderer.load(std::memory_order_acquire) != nullptr;
}

bool IsNativeGuestOutputActive() {
  return g_native_output_active.load(std::memory_order_relaxed);
}

void SetNativeGuestOutputPostProcessor(NativeGuestOutputPostProcessor post_processor,
                                       void* user_data) {
  g_post_processor_user_data.store(user_data, std::memory_order_release);
  g_post_processor.store(post_processor, std::memory_order_release);
}

bool HasNativeGuestOutputPostProcessor() {
  return g_post_processor.load(std::memory_order_acquire) != nullptr;
}

void InvokeNativeGuestOutputPostProcessor(const NativeGuestOutputRenderContext& context) {
  NativeGuestOutputPostProcessor post_processor = g_post_processor.load(std::memory_order_acquire);
  if (post_processor != nullptr) {
    post_processor(context, g_post_processor_user_data.load(std::memory_order_acquire));
  }
}

void RequestNativeGuestOutputPostProcess(bool requested) {
  g_post_process_requested.store(requested, std::memory_order_release);
}

bool IsNativeGuestOutputPostProcessRequested() {
  return g_post_process_requested.load(std::memory_order_acquire);
}

void SetNativeGuestOutputWideAspect(double aspect) {
  g_wide_aspect.store(aspect > 0.0 ? aspect : 0.0, std::memory_order_relaxed);
}

double GetNativeGuestOutputWideAspect() {
  return g_wide_aspect.load(std::memory_order_relaxed);
}

bool ApplyNativeGuestOutputWideAspect(uint32_t& guest_output_width, uint32_t guest_output_height,
                                      uint32_t& display_width, uint32_t& display_height) {
  const double aspect = g_wide_aspect.load(std::memory_order_relaxed);
  if (aspect <= 0.0 || !guest_output_width || !guest_output_height ||
      !HasNativeGuestOutputRenderer() || !g_native_output_active.load(std::memory_order_relaxed)) {
    return false;
  }
  uint32_t wide_width =
      uint32_t(std::clamp(aspect * double(guest_output_height) + 0.5, 1.0, 16384.0)) & ~1u;
  if (wide_width <= guest_output_width) {
    return false;
  }
  guest_output_width = wide_width;
  display_width = guest_output_width;
  display_height = guest_output_height;
  return true;
}

bool ShouldSuppressEmulatedDraws() {
  return REXCVAR_GET(native_render_suppress_emulated_draws) &&
         g_native_output_active.load(std::memory_order_relaxed);
}

bool ShouldSuppressPassAtPitch(uint32_t surface_pitch) {
  static CachedList suppress_cache;
  static CachedList keep_cache;
  const auto& suppress = Cached(suppress_cache, REXCVAR_GET(native_render_suppress_pitches),
                                "native_render_suppress_pitches");
  const auto& keep = Cached(keep_cache, REXCVAR_GET(native_render_keep_pitches),
                            "native_render_keep_pitches");
  return native_suppress::ShouldSuppressPitch(suppress, keep, surface_pitch);
}

}  // namespace rex::graphics

namespace rex::graphics::nrhi {
namespace {
std::string g_shader_bytecode_cache_dir;
}  // namespace

void SetShaderBytecodeCacheDirectory(const char* path) {
  g_shader_bytecode_cache_dir = path != nullptr ? path : "";
}

const char* GetShaderBytecodeCacheDirectory() {
  return g_shader_bytecode_cache_dir.c_str();
}

}  // namespace rex::graphics::nrhi
```
- [ ] **Step 4: Compile it into `rexruntime`**

In `thirdparty/rexglue-sdk/src/system/CMakeLists.txt`, add to the end of the `set(REXSYSTEM_SOURCES ...)` list (before the closing `)`):
```cmake
    # Native guest-output renderer registry: lives in rexruntime so the
    # title and the GPU plugin share one instance (see rexruntime.def).
    ../graphics/native_guest_renderer.cpp
```

- [ ] **Step 5: Build the runtime and collect the mangled names**

Run:
```
tools\build_runtime_sdk.cmd
```
Expected: builds (exports not yet listed, so nothing references them yet). Then list the symbols to export:
```
for /f "delims=" %F in ('dir /s /b out\build\runtime-sdk\native_guest_renderer.cpp.obj') do llvm-nm --defined-only --extern-only "%F"
```
Expected: lines like `00000000 T ?SetNativeGuestOutputRenderer@graphics@rex@@YAXP6A_NAEBUNativeGuestOutputRenderContext@12@PEAX@Z1@Z`. Take every `T` symbol whose name contains `NativeGuestOutput`, `ShouldSuppress`, or `ShaderBytecodeCacheDirectory`.

- [ ] **Step 6: Export them**

Append to `thirdparty/rexglue-sdk/src/system/rexruntime.def` (after the last existing line):
```
; Native guest-output renderer registry (native_guest_renderer.cpp)
```
followed by each mangled name from Step 5, one per line.

- [ ] **Step 7: Rebuild and verify the exports**

Run:
```
tools\build_runtime_sdk.cmd
llvm-readobj --coff-exports out\build\runtime-sdk\rexruntime.dll | findstr /c:"NativeGuestOutput" /c:"ShouldSuppress" /c:"ShaderBytecodeCache"
```
Expected: one export line per name appended in Step 6 (16 functions: 12 `*NativeGuestOutput*` functions, `ShouldSuppressEmulatedDraws`, `ShouldSuppressPassAtPitch`, `SetShaderBytecodeCacheDirectory`, `GetShaderBytecodeCacheDirectory`). If the DLL path differs, locate it with `dir /s /b out\build\runtime-sdk\rexruntime.dll`.

- [ ] **Step 8: Commit**

```
rtk git -C thirdparty/rexglue-sdk add include/rex/graphics/native_rhi.h include/rex/graphics/native_guest_renderer.h src/graphics/native_guest_renderer.cpp src/system/CMakeLists.txt src/system/rexruntime.def
rtk git -C thirdparty/rexglue-sdk commit -m "Add native guest-output renderer registry and RHI interface (from rexglue-skate3)"
```

---

### Task 4: D3D12 RHI backend in the GPU plugin

**Files:**
- Create (SDK, from skate3 fork): `include/rex/graphics/d3d12/native_rhi_d3d12.h`, `src/graphics/d3d12/native_rhi_d3d12.cpp`
- Modify (SDK): `src/graphics/CMakeLists.txt` (D3D12 source list), `include/rex/graphics/d3d12/command_processor.h` (member), `src/graphics/d3d12/command_processor.cpp` (destroy on shutdown), `src/ui/d3d12/d3d12_presenter.cpp` (render-target flag)

**Interfaces:**
- Consumes: `nrhi::*` from Task 3.
- Produces (namespace `rex::graphics::d3d12`):
  - `nrhi::Device* CreateNativeRhiDevice(D3D12CommandProcessor* command_processor);`
  - `void DestroyNativeRhiDevice(nrhi::Device* device);`
  - `nrhi::Cmd* NativeRhiBeginFrame(nrhi::Device*, ID3D12Resource* guest_output_resource, DXGI_FORMAT, D3D12_RESOURCE_STATES internal_state, uint32_t width, uint32_t height, nrhi::Texture** guest_output_out);`
  - Member `nrhi::Device* native_rhi_device_ = nullptr;` on `D3D12CommandProcessor`.

- [ ] **Step 1: Copy the backend files**

Run:
```
git -C thirdparty/rexglue-sdk show FETCH_HEAD:include/rex/graphics/d3d12/native_rhi_d3d12.h > thirdparty/rexglue-sdk/include/rex/graphics/d3d12/native_rhi_d3d12.h
git -C thirdparty/rexglue-sdk show FETCH_HEAD:src/graphics/d3d12/native_rhi_d3d12.cpp > thirdparty/rexglue-sdk/src/graphics/d3d12/native_rhi_d3d12.cpp
```
(If `FETCH_HEAD` moved, re-run the fetch from Task 3 Step 1.)

- [ ] **Step 2: Remove the GPU-timestamp profiling dependency**

Our `D3D12CommandProcessor` has no `BeginGpuTimestampedDraw`/`EndGpuTimestampedDraw`, and `rex::perf::DrawBucket` has no native buckets. In `native_rhi_d3d12.cpp`:
1. Delete the whole `ProfileStageBucket(...)` helper function (it returns `rex::perf::DrawBucket` values).
2. Replace the body of `NrCmdD3D12::ProfileRegion` with:
```cpp
void NrCmdD3D12::ProfileRegion(nrhi::ProfileStage stage) {
  // GPU timestamp buckets are not available in this SDK; regions are no-ops.
  (void)stage;
}
```
3. Delete the `profile_region_query_` member declaration and any remaining references to it (`grep -n profile_region_query_` must return nothing).
4. Remove any `#include` of a perf header that is now unused (`grep -n "rex/perf" native_rhi_d3d12.cpp`; delete if `rex::perf` no longer appears).

- [ ] **Step 3: Add the source to the plugin build**

In `thirdparty/rexglue-sdk/src/graphics/CMakeLists.txt`, inside the `if(REXGLUE_USE_D3D12)` block, add `d3d12/native_rhi_d3d12.cpp` to the D3D12 backend source list (next to `d3d12/command_processor.cpp`). Ensure the plugin links `d3dcompiler`: if `grep -n d3dcompiler src/graphics/CMakeLists.txt` finds nothing, add inside the same `if` block after the existing `target_link_libraries(rexgpu-xenos PRIVATE ...)`:
```cmake
    target_link_libraries(rexgpu-xenos PRIVATE d3dcompiler)
```

- [ ] **Step 4: Add the device member and shutdown**

In `include/rex/graphics/d3d12/command_processor.h`, add near the top with the other includes:
```cpp
#include <rex/graphics/native_rhi.h>
```
and in the private member section (next to `occlusion_query_heap_`):
```cpp
  // Native guest-output renderer RHI device (created lazily at the first
  // swap that has a registered renderer or post-processor).
  nrhi::Device* native_rhi_device_ = nullptr;
```
In `src/graphics/d3d12/command_processor.cpp`, add with the other includes:
```cpp
#include <rex/graphics/d3d12/native_rhi_d3d12.h>
#include <rex/graphics/native_guest_renderer.h>
```
and in `D3D12CommandProcessor::ShutdownContext()`, directly after `AwaitAllQueueOperationsCompletion();`:
```cpp
  if (native_rhi_device_ != nullptr) {
    DestroyNativeRhiDevice(native_rhi_device_);
    native_rhi_device_ = nullptr;
  }
```

- [ ] **Step 5: Let the guest output be a render target**

In `thirdparty/rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp`, change the guest-output resource flags (currently `guest_output_resource_new_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;`, around line 521) to:
```cpp
    // Render-target capable so the native guest-output renderer can draw into
    // it (rex/graphics/native_guest_renderer.h).
    guest_output_resource_new_desc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
```

- [ ] **Step 6: Build**

Run: `tools\build_runtime_sdk.cmd`
Expected: `rexgpu-xenos` and `rexruntime` link with exit code 0. Fix compile errors only by adapting to our SDK's existing APIs (e.g. include paths, provider accessors); do not change RHI behaviour. Record any adaptation in the commit message.

- [ ] **Step 7: Commit**

```
rtk git -C thirdparty/rexglue-sdk add -A include/rex/graphics/d3d12 src/graphics src/ui/d3d12/d3d12_presenter.cpp
rtk git -C thirdparty/rexglue-sdk commit -m "Add D3D12 native RHI backend (from rexglue-skate3; GPU timestamp regions stubbed)"
```

---

### Task 5: Hook the D3D12 command processor

**Files:**
- Modify (SDK): `src/graphics/d3d12/command_processor.cpp` (IssueSwap refresh lambda, post-processor, IssueDraw, IssueCopy, EVENT_WRITE_ZPD)

**Interfaces:**
- Consumes: everything from Tasks 3–4.
- Produces: runtime behaviour only (no new symbols).

- [ ] **Step 1: Capture display size in the refresh lambda**

In `D3D12CommandProcessor::IssueSwap`, change the lambda capture list of `presenter->RefreshGuestOutput(...)` from
```cpp
      [this, &swap_texture_srv_desc, frontbuffer_format, swap_texture_resource, guest_output_width,
       guest_output_height](ui::Presenter::GuestOutputRefreshContext& context) -> bool {
```
to
```cpp
      [this, &swap_texture_srv_desc, frontbuffer_format, swap_texture_resource, guest_output_width,
       guest_output_height, display_width,
       display_height](ui::Presenter::GuestOutputRefreshContext& context) -> bool {
```

- [ ] **Step 2: Call the native renderer before the emulated blit**

Directly after the line `context.SetIs8bpc(!use_pwl_gamma_ramp && !use_fxaa);` inside that lambda, insert:
```cpp
        // Native guest-output renderer (rex/graphics/native_guest_renderer.h):
        // when it returns true the frame is fully native and the emulated
        // gamma/FXAA blit below is skipped; false falls through (fallback).
        if (HasNativeGuestOutputRenderer()) {
          ID3D12Resource* native_guest_output_resource =
              static_cast<ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context)
                  .resource_uav_capable();
          NativeGuestOutputRenderContext native_context;
          native_context.backend = NativeGuestOutputBackend::kD3D12;
          native_context.guest_output_width = guest_output_width;
          native_context.guest_output_height = guest_output_height;
          native_context.display_width = display_width;
          native_context.display_height = display_height;
          if (native_rhi_device_ == nullptr) {
            native_rhi_device_ = CreateNativeRhiDevice(this);
          }
          native_context.device = native_rhi_device_;
          native_context.cmd = NativeRhiBeginFrame(
              native_rhi_device_, native_guest_output_resource,
              ui::d3d12::D3D12Presenter::kGuestOutputFormat,
              ui::d3d12::D3D12Presenter::kGuestOutputInternalState, guest_output_width,
              guest_output_height, &native_context.guest_output);
          if (TryRenderNativeGuestOutput(native_context)) {
            SubmitBarriers();
            EndSubmission(true);
            return true;
          }
        }
```

- [ ] **Step 3: Invoke the post-processor after the emulated blit**

At the end of the same lambda, replace
```cpp
        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue.
        SubmitBarriers();
        EndSubmission(true);
        return true;
      });
```
with
```cpp
        // Host post-processor over the emulated output (e.g. the native
        // parity overlay): runs after the gamma/FXAA pass has fully written
        // the image and returned it to kGuestOutputInternalState.
        if (IsNativeGuestOutputPostProcessRequested() && HasNativeGuestOutputPostProcessor()) {
          NativeGuestOutputRenderContext native_context;
          native_context.backend = NativeGuestOutputBackend::kD3D12;
          native_context.guest_output_width = guest_output_width;
          native_context.guest_output_height = guest_output_height;
          native_context.display_width = display_width;
          native_context.display_height = display_height;
          if (native_rhi_device_ == nullptr) {
            native_rhi_device_ = CreateNativeRhiDevice(this);
          }
          native_context.device = native_rhi_device_;
          native_context.cmd = NativeRhiBeginFrame(
              native_rhi_device_, guest_output_resource,
              ui::d3d12::D3D12Presenter::kGuestOutputFormat,
              ui::d3d12::D3D12Presenter::kGuestOutputInternalState, guest_output_width,
              guest_output_height, &native_context.guest_output);
          InvokeNativeGuestOutputPostProcessor(native_context);
        }

        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue.
        SubmitBarriers();
        EndSubmission(true);
        return true;
      });
```

- [ ] **Step 4: Suppress emulated draws by pitch**

In `D3D12CommandProcessor::IssueDraw`, directly before the line `if (!BeginSubmission(true)) {` that follows `bool memexport_used = memexport_used_vertex || memexport_used_pixel;`, insert:
```cpp
  // Native output active: skip draws of suppressed passes (never memexport).
  if (!memexport_used && ShouldSuppressEmulatedDraws()) {
    const uint32_t pitch = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch;
    if (ShouldSuppressPassAtPitch(pitch)) {
      return true;
    }
    // Census of passes still executing under suppression (each pitch once):
    // the data for tuning native_render_suppress_pitches.
    static std::atomic<uint32_t> logged_pitch_mask[8192 / 32] = {};
    if (pitch < 8192) {
      const uint32_t bit = 1u << (pitch % 32);
      if ((logged_pitch_mask[pitch / 32].fetch_or(bit) & bit) == 0) {
        REXGPU_INFO("suppression census: executing draws at surface_pitch={}", pitch);
      }
    }
  }
```
Add `#include <atomic>` to the includes if not already present.

- [ ] **Step 5: Suppress resolves of suppressed passes**

In `D3D12CommandProcessor::IssueCopy`, insert at the top of the function body (after the `#endif` of the profile scope, before `if (!BeginSubmission(true))`):
```cpp
  // Resolves of suppressed passes would copy garbage EDRAM over guest
  // textures; draws and resolves use the same pitch predicate.
  if (ShouldSuppressEmulatedDraws() &&
      ShouldSuppressPassAtPitch(register_file_->Get<reg::RB_SURFACE_INFO>().surface_pitch)) {
    return true;
  }
```

- [ ] **Step 6: Fake-visible occlusion queries while suppressing**

In `D3D12CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD`, directly after the `write_fallback_result` lambda definition (before `bool is_end_via_z_pass = ...`), insert:
```cpp
  // Suppressed passes never rasterize, so a host query would report 0 and
  // the game would cull geometry the native renderer is drawing. Report a
  // positive count instead.
  if (ShouldSuppressEmulatedDraws()) {
    if (active_occlusion_query_.valid) {
      const uint32_t host_index = active_occlusion_query_.host_index;
      active_occlusion_query_ = {};
      if (occlusion_query_heap_ && BeginSubmission(true)) {
        deferred_command_list_.D3DEndQuery(occlusion_query_heap_.Get(),
                                           D3D12_QUERY_TYPE_OCCLUSION, host_index);
        EndSubmission(false);
      }
    }
    const bool ending = sample_counts->ZPass_A == kQueryFinished ||
                        sample_counts->ZPass_B == kQueryFinished ||
                        sample_counts->ZFail_A == kQueryFinished ||
                        sample_counts->ZFail_B == kQueryFinished;
    if (ending) {
      const int32_t fake = std::max(1, int32_t(REXCVAR_GET(query_occlusion_fake_sample_count)));
      std::memset(sample_counts, 0, sizeof(xenos::xe_gpu_depth_sample_counts));
      sample_counts->ZPass_A = uint32_t(fake);
      sample_counts->Total_A = uint32_t(fake);
    }
    return true;
  }
```
If `deferred_command_list_` has no `D3DEndQuery`, check `grep -n "EndQuery" include/rex/graphics/d3d12/deferred_command_list.h` and use the name it defines.

- [ ] **Step 7: Build and run with nothing registered**

Run: `build.cmd -release fable_2`, then start the game. Expected: identical behaviour to Task 0 — main menu reached, FPS within noise of the baseline, and `grep -c "suppression census" out\build\win-amd64-release\logs\*.log` returns 0 (no renderer registered → never active).

- [ ] **Step 8: Commit**

```
rtk git -C thirdparty/rexglue-sdk add src/graphics/d3d12/command_processor.cpp
rtk git -C thirdparty/rexglue-sdk commit -m "Hook native guest-output renderer, post-processor and pitch suppression into the D3D12 command processor"
rtk git add thirdparty/rexglue-sdk
rtk git commit -m "Bump SDK to renderer branch with D3D12 native hook layer"
```

---

### Task 6: Game-side state helpers (pure, tested)

**Files:**
- Create: `src/native/native_render_state.h`
- Create: `tests/native/test_native_render_state.cpp`
- Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces (namespace `fable2::native`):
  - `enum class Mode { kOverlay, kReplace };`
  - `struct ParsedMode { Mode mode; bool recognized; };`
  - `ParsedMode ParseMode(std::string_view text);` — trims spaces, case-insensitive; unknown → `{kOverlay, false}`.
  - `class EdgeDetector { public: bool Update(bool down); };` — true only on the up→down transition.
  - `class FailureLatch { public: bool Fail(); bool IsFailed() const; void Clear(); };` — `Fail()` returns true only the first time after a `Clear()`; thread-safe.

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_native_render_state.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/native_render_state.h"
#include <iostream>

using namespace fable2::native;

int main() {
  // ParseMode
  if (ParseMode("overlay").mode != Mode::kOverlay || !ParseMode("overlay").recognized) return 1;
  if (ParseMode(" Replace ").mode != Mode::kReplace || !ParseMode(" Replace ").recognized) return 2;
  if (ParseMode("REPLACE").mode != Mode::kReplace) return 3;
  ParsedMode unknown = ParseMode("foo");
  if (unknown.mode != Mode::kOverlay || unknown.recognized) return 4;
  if (ParseMode("").recognized) return 5;

  // EdgeDetector: only up->down transitions fire.
  EdgeDetector edge;
  if (edge.Update(false)) return 6;
  if (!edge.Update(true)) return 7;
  if (edge.Update(true)) return 8;   // held
  if (edge.Update(false)) return 9;  // release
  if (!edge.Update(true)) return 10; // pressed again

  // FailureLatch: first Fail() reports, later ones don't; Clear() re-arms.
  FailureLatch latch;
  if (latch.IsFailed()) return 11;
  if (!latch.Fail()) return 12;
  if (latch.Fail()) return 13;
  if (!latch.IsFailed()) return 14;
  latch.Clear();
  if (latch.IsFailed()) return 15;
  if (!latch.Fail()) return 16;

  std::cout << "PASS: mode parsing, F6 edge detection, failure latch\n";
  return 0;
}
```
Add to `tests/run_native_tests.cmd`, before the final `exit /b 0`:
```
clang++ -std=c++23 "%~dp0native\test_native_render_state.cpp" -o "%OUT%\native_state.exe" || exit /b 1
"%OUT%\native_state.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tests\run_native_tests.cmd`
Expected: clang error `'../../src/native/native_render_state.h' file not found`.

- [ ] **Step 3: Implement the header**

Create `src/native/native_render_state.h`:
```cpp
#pragma once

// Pure state helpers for the native renderer plumbing (no SDK/GPU deps) so
// they are unit-testable: mode parsing, F6 edge detection, sticky failure.

#include <atomic>
#include <cctype>
#include <string>
#include <string_view>

namespace fable2::native {

enum class Mode { kOverlay, kReplace };

struct ParsedMode {
  Mode mode;
  bool recognized;
};

inline ParsedMode ParseMode(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (lower == "overlay") return {Mode::kOverlay, true};
  if (lower == "replace") return {Mode::kReplace, true};
  return {Mode::kOverlay, false};
}

class EdgeDetector {
 public:
  bool Update(bool down) {
    const bool edge = down && !prev_down_;
    prev_down_ = down;
    return edge;
  }

 private:
  bool prev_down_ = false;
};

class FailureLatch {
 public:
  // Returns true only for the first failure since construction/Clear().
  bool Fail() { return !failed_.exchange(true, std::memory_order_acq_rel); }
  bool IsFailed() const { return failed_.load(std::memory_order_acquire); }
  void Clear() { failed_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> failed_{false};
};

}  // namespace fable2::native
```

- [ ] **Step 4: Run test to verify it passes**

Run: `tests\run_native_tests.cmd`
Expected: `PASS: mode parsing, F6 edge detection, failure latch`, exit 0.

- [ ] **Step 5: Commit**

```
rtk git add src/native/native_render_state.h tests/native/test_native_render_state.cpp tests/run_native_tests.cmd
rtk git commit -m "Add native renderer state helpers with tests"
```

---

### Task 7: Test-pattern and overlay shaders (compile-tested)

**Files:**
- Create: `src/native/fable2_native_shaders.h`
- Create: `tests/native/test_native_shaders.cpp`
- Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Produces (namespace `fable2::native::shaders`): `inline constexpr const char* kFullscreenVs`, `kPatternPs`, `kGridPs` — HLSL SM 5.0 sources, entry point `main`. Pixel shaders read `cbuffer Params : register(b0) { float4 params; }` where `params = {frame_index, width, height, 0}`.

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_native_shaders.cpp`:
```cpp
// Compiles the native renderer's HLSL with D3DCompile (no GPU needed).
#include "../../src/native/fable2_native_shaders.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iostream>

static bool Compile(const char* name, const char* src, const char* target) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (FAILED(hr)) {
    std::cerr << name << " failed:\n"
              << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "") << "\n";
  }
  if (code) code->Release();
  if (errors) errors->Release();
  return SUCCEEDED(hr);
}

int main() {
  using namespace fable2::native::shaders;
  if (!Compile("fullscreen_vs", kFullscreenVs, "vs_5_0")) return 1;
  if (!Compile("pattern_ps", kPatternPs, "ps_5_0")) return 2;
  if (!Compile("grid_ps", kGridPs, "ps_5_0")) return 3;
  // Negative control: broken HLSL must fail (proves the harness reports errors).
  if (Compile("broken_ps", "float4 main() : SV_Target { return undefined_symbol; }", "ps_5_0"))
    return 4;
  std::cout << "PASS: fullscreen VS, pattern PS, grid PS compile; broken shader rejected\n";
  return 0;
}
```
Add to `tests/run_native_tests.cmd`, before the final `exit /b 0`:
```
clang++ -std=c++23 "%~dp0native\test_native_shaders.cpp" -ld3dcompiler -o "%OUT%\native_shaders.exe" || exit /b 1
"%OUT%\native_shaders.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `tests\run_native_tests.cmd`
Expected: clang error `'../../src/native/fable2_native_shaders.h' file not found`.

- [ ] **Step 3: Write the shaders**

Create `src/native/fable2_native_shaders.h`:
```cpp
#pragma once

// HLSL (SM 5.0) for the native renderer plumbing tests, compiled at runtime
// by the D3D12 RHI (D3DCompile). Entry point "main" for all shaders.

namespace fable2::native::shaders {

// Fullscreen triangle from SV_VertexID (no vertex buffer).
inline constexpr const char* kFullscreenVs = R"hlsl(
struct VsOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};
VsOut main(uint id : SV_VertexID) {
  VsOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return o;
}
)hlsl";

// Replace mode: 32px checkerboard plus an orange bar sweeping left to right.
inline constexpr const char* kPatternPs = R"hlsl(
cbuffer Params : register(b0) { float4 params; }  // frame, width, height, 0
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  uint2 cell = uint2(pos.xy) / 32;
  float checker = ((cell.x + cell.y) & 1) ? 0.85 : 0.25;
  float bar_x = frac(params.x / 240.0) * params.y;
  float3 color = abs(pos.x - bar_x) < 16.0 ? float3(1.0, 0.45, 0.0)
                                           : float3(checker, checker, checker);
  return float4(color, 1.0);
}
)hlsl";

// Overlay mode: translucent green 64px grid drawn over the emulated frame.
inline constexpr const char* kGridPs = R"hlsl(
cbuffer Params : register(b0) { float4 params; }  // frame, width, height, 0
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  bool on_line = fmod(pos.x, 64.0) < 1.0 || fmod(pos.y, 64.0) < 1.0;
  if (!on_line) discard;
  return float4(0.0, 1.0, 0.4, 0.5);
}
)hlsl";

}  // namespace fable2::native::shaders
```

- [ ] **Step 4: Run test to verify it passes**

Run: `tests\run_native_tests.cmd`
Expected: `PASS: fullscreen VS, pattern PS, grid PS compile; broken shader rejected`, exit 0.

- [ ] **Step 5: Commit**

```
rtk git add src/native/fable2_native_shaders.h tests/native/test_native_shaders.cpp tests/run_native_tests.cmd
rtk git commit -m "Add native renderer test-pattern and overlay shaders with compile test"
```

---

### Task 8: Native renderer plumbing in the game

**Files:**
- Create: `src/native/fable2_native_render.h`, `src/native/fable2_native_render.cpp`
- Modify: `CMakeLists.txt` (sources + include dir), `src/core/fable2_config.h`, `src/core/fable2_config.cpp`, `src/core/fable_2_app.h`, `src/diagnostics/fps_meter.h`

**Interfaces:**
- Consumes: Task 3 registry API; Task 6 `ParseMode`, `EdgeDetector`, `FailureLatch`; Task 7 shader strings; `nrhi::*`.
- Produces (namespace `fable2::native`): `void Install();` (call once from `Fable2App::OnPostSetup`), `void PollFrame();` (call once per guest frame from the MainRenderLoop override). Cvars: `fable2_native_render` (bool, default false), `fable2_native_render_active` (bool, default true), `fable2_native_render_mode` (string, default `"overlay"`).

- [ ] **Step 1: Header**

Create `src/native/fable2_native_render.h`:
```cpp
#pragma once

// Native guest-output renderer plumbing (sub-project 1): registers a
// renderer (replace mode: test pattern) and a post-processor (overlay mode:
// grid over the emulated frame) with the SDK. Off unless the
// fable2_native_render cvar is true at startup.

namespace fable2::native {

// Registers the SDK callbacks if fable2_native_render is true. Call once,
// after the GPU plugin is loaded (Fable2App::OnPostSetup).
void Install();

// Per guest frame (MainRenderLoop override): F6 toggle and the overlay
// post-process request flag.
void PollFrame();

}  // namespace fable2::native
```

- [ ] **Step 2: Implementation**

Create `src/native/fable2_native_render.cpp`:
```cpp
#include "fable2_native_render.h"

#include <atomic>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>
#include <rex/logging.h>

#include "fable2_native_shaders.h"
#include "native_render_state.h"

REXCVAR_DEFINE_BOOL(fable2_native_render, false, "Fable2",
                    "Register the native guest-output renderer at startup (restart "
                    "required). Seeded from fable2_config.toml [native] enabled.");
REXCVAR_DEFINE_BOOL(fable2_native_render_active, true, "Fable2",
                    "Live switch for the native renderer (F6 toggles).");
REXCVAR_DEFINE_STRING(fable2_native_render_mode, "overlay", "Fable2",
                      "overlay = grid drawn over the emulated frame; replace = native "
                      "test pattern replaces the frame.");

namespace fable2::native {
namespace nrhi = rex::graphics::nrhi;
using rex::graphics::NativeGuestOutputBackend;
using rex::graphics::NativeGuestOutputRenderContext;

namespace {

constexpr int kToggleVk = 0x75;  // VK_F6

struct Resources {
  nrhi::Device* device = nullptr;
  nrhi::Format format = nrhi::Format::kUnknown;
  nrhi::BindingLayout* layout = nullptr;
  nrhi::Shader* vs = nullptr;
  nrhi::Shader* pattern_ps = nullptr;
  nrhi::Shader* grid_ps = nullptr;
  nrhi::Pipeline* pattern = nullptr;
  nrhi::Pipeline* grid = nullptr;
};

bool g_installed = false;
Resources g_res;  // command-processor thread only
FailureLatch g_latch;
EdgeDetector g_toggle;
std::atomic<uint32_t> g_frame{0};

Mode CurrentMode() {
  static std::atomic<bool> warned{false};
  const ParsedMode parsed = ParseMode(REXCVAR_GET(fable2_native_render_mode));
  if (!parsed.recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_render_mode '{}', using overlay",
                REXCVAR_GET(fable2_native_render_mode));
  }
  return parsed.mode;
}

void Fail(const char* what) {
  if (g_latch.Fail()) {
    REXLOG_ERROR("[native] {} failed; falling back to emulated output (F6 retries)", what);
  }
}

bool EnsureResources(nrhi::Device* device, nrhi::Format format) {
  if (g_res.device == device && g_res.format == format && g_res.pattern && g_res.grid) {
    return true;
  }
  g_res = {};
  g_res.device = device;
  g_res.format = format;

  nrhi::BindingLayoutDesc layout_desc;
  layout_desc.params[0].kind = nrhi::BindingParamKind::kConstants;
  layout_desc.params[0].shader_register = 0;
  layout_desc.params[0].count = 4;
  layout_desc.params[0].visibility = nrhi::Visibility::kPixel;
  layout_desc.param_count = 1;
  layout_desc.allow_input_layout = false;
  g_res.layout = device->CreateBindingLayout(layout_desc);
  if (!g_res.layout) return Fail("binding layout"), false;

  auto make_shader = [&](nrhi::ShaderStage stage, const char* name, const char* src) {
    nrhi::ShaderDesc desc;
    desc.stage = stage;
    desc.name = name;
    desc.hlsl_source = src;
    desc.entry_point = "main";
    return device->CreateShader(desc);
  };
  g_res.vs = make_shader(nrhi::ShaderStage::kVertex, "fable2_fullscreen_vs", shaders::kFullscreenVs);
  g_res.pattern_ps = make_shader(nrhi::ShaderStage::kPixel, "fable2_pattern_ps", shaders::kPatternPs);
  g_res.grid_ps = make_shader(nrhi::ShaderStage::kPixel, "fable2_grid_ps", shaders::kGridPs);
  if (!g_res.vs || !g_res.pattern_ps || !g_res.grid_ps) return Fail("shader compile"), false;

  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = g_res.layout;
  pipe.vs = g_res.vs;
  pipe.rtv_format = format;
  pipe.ps = g_res.pattern_ps;
  g_res.pattern = device->CreateGraphicsPipeline(pipe);
  pipe.ps = g_res.grid_ps;
  pipe.blend.enable = true;
  pipe.blend.src = nrhi::BlendFactor::kSrcAlpha;
  pipe.blend.dst = nrhi::BlendFactor::kInvSrcAlpha;
  g_res.grid = device->CreateGraphicsPipeline(pipe);
  if (!g_res.pattern || !g_res.grid) return Fail("pipeline creation"), false;
  return true;
}

void DrawFullscreen(const NativeGuestOutputRenderContext& ctx, nrhi::Pipeline* pipeline) {
  nrhi::Cmd* cmd = ctx.cmd;
  nrhi::Texture* out = ctx.guest_output;
  const float w = float(ctx.guest_output_width);
  const float h = float(ctx.guest_output_height);
  cmd->Barrier(out, nrhi::ResourceState::kGuestOutput, nrhi::ResourceState::kRenderTarget);
  cmd->SetRenderTargets(out, nullptr);
  cmd->SetViewport({0.0f, 0.0f, w, h, 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(w), int32_t(h)});
  cmd->SetBindingLayout(g_res.layout);
  cmd->SetPipeline(pipeline);
  const float params[4] = {float(g_frame.load(std::memory_order_relaxed)), w, h, 0.0f};
  cmd->SetRootConstants(0, 4, params, 0);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->Draw(3, 0);
  cmd->Barrier(out, nrhi::ResourceState::kRenderTarget, nrhi::ResourceState::kGuestOutput);
}

bool Usable(const NativeGuestOutputRenderContext& ctx) {
  return REXCVAR_GET(fable2_native_render_active) && !g_latch.IsFailed() &&
         ctx.backend == NativeGuestOutputBackend::kD3D12 && ctx.device && ctx.cmd &&
         ctx.guest_output;
}

bool RenderCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  if (!Usable(ctx) || CurrentMode() != Mode::kReplace) return false;
  if (!EnsureResources(ctx.device, ctx.guest_output->format())) return false;
  DrawFullscreen(ctx, g_res.pattern);
  g_frame.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void OverlayCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  if (!Usable(ctx) || CurrentMode() != Mode::kOverlay) return;
  if (!EnsureResources(ctx.device, ctx.guest_output->format())) return;
  DrawFullscreen(ctx, g_res.grid);
  g_frame.fetch_add(1, std::memory_order_relaxed);
}

bool WindowFocused() {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return true;
#endif
}

}  // namespace

void Install() {
  if (!REXCVAR_GET(fable2_native_render)) {
    REXLOG_INFO("[native] native renderer disabled (fable2_native_render=false)");
    return;
  }
  rex::graphics::SetNativeGuestOutputRenderer(&RenderCallback, nullptr);
  rex::graphics::SetNativeGuestOutputPostProcessor(&OverlayCallback, nullptr);
  g_installed = true;
  REXLOG_INFO("[native] native renderer installed (mode={}, F6 toggles)",
              REXCVAR_GET(fable2_native_render_mode));
}

void PollFrame() {
  if (!g_installed) return;
#ifdef _WIN32
  const bool down = WindowFocused() && (GetAsyncKeyState(kToggleVk) & 0x8000) != 0;
  if (g_toggle.Update(down)) {
    const bool now_active = !REXCVAR_GET(fable2_native_render_active);
    REXCVAR_SET(fable2_native_render_active, now_active);
    g_latch.Clear();
    REXLOG_INFO("[native] F6: native renderer {}", now_active ? "on" : "off");
  }
#endif
  rex::graphics::RequestNativeGuestOutputPostProcess(
      REXCVAR_GET(fable2_native_render_active) && !g_latch.IsFailed() &&
      CurrentMode() == Mode::kOverlay);
}

}  // namespace fable2::native
```
(`REXLOG_INFO/WARN/ERROR` are defined in `rex/logging/macros.h`, reached through `rex/logging.h`.)

- [ ] **Step 3: Build wiring**

In `CMakeLists.txt`, find where `FABLE2_SOURCES` is assembled (the `file(GLOB_RECURSE ...)` for `src/core/hotfunc` is a good anchor) and add:
```cmake
# Native guest-output renderer plumbing (docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md)
file(GLOB FABLE2_NATIVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/native/*.cpp)
list(APPEND FABLE2_SOURCES ${FABLE2_NATIVE_SOURCES})
```
before `add_executable(fable_2 ...)`, and add `${CMAKE_CURRENT_SOURCE_DIR}/src/native` to the `target_include_directories(fable_2 ...)` list (next to `src/core`, `src/diagnostics`, `src/input`).

- [ ] **Step 4: Config `[native] enabled`**

In `src/core/fable2_config.h`, inside `struct Values` after `hero_dog_texture_readback`:
```cpp
  // [native] enabled: register the native guest-output renderer (seeds cvar
  // fable2_native_render). Experimental; see docs/superpowers/specs/.
  bool native_render = false;
```
In `src/core/fable2_config.cpp`, in the default config template text, append after the `[perf]` section's last line:
```toml

[native]
# Experimental native renderer (docs/superpowers/specs/
# 2026-10-01-native-renderer-foundation-design.md). true = register the native
# renderer at startup; F6 toggles it live. Mode is the cvar
# fable2_native_render_mode (overlay | replace).
# Default: false
enabled = false
```
and in the parser, after the `perf` table block:
```cpp
  const toml::path native_path{"native"};
  const auto native = root[native_path];
  if (native.is_table()) {
    const toml::table& native_table = *native.as_table();
    values.native_render =
        Read<bool>(native_table, "native", "enabled", "boolean", values.native_render);
  }
```
In `src/core/fable_2_app.h` `OnPostInitLogging`, after `seed_cvar("mouse_look_scale", ...);` add:
```cpp
    seed_cvar("fable2_native_render", cfg.native_render ? "true" : "false");
```

- [ ] **Step 5: Install and poll**

In `src/core/fable_2_app.h`, add `#include "fable2_native_render.h"` with the other project includes, and at the end of `OnPostSetup()` (after the hero/dog seeding block and the existing setup), add:
```cpp
    // Native guest-output renderer (registers SDK callbacks if enabled).
    fable2::native::Install();
```
In `src/diagnostics/fps_meter.h`, add `#include "fable2_native_render.h"` near the top includes, and in `MainRenderLoop_82B9CD68`, directly before `fable2::f5lua::poll_mainloop(ctx, base);`:
```cpp
  // F6 native-renderer toggle + overlay request (per frame).
  fable2::native::PollFrame();
```

- [ ] **Step 6: Build**

Run: `build.cmd -release fable_2`
Expected: exit 0.

- [ ] **Step 7: Commit**

```
rtk git add src/native CMakeLists.txt src/core/fable2_config.h src/core/fable2_config.cpp src/core/fable_2_app.h src/diagnostics/fps_meter.h
rtk git commit -m "Add native renderer plumbing: overlay/replace test modes, F6 toggle, [native] config"
```

---

### Task 9: In-game verification and docs

**Files:**
- Modify: `README.md` (short "Experimental native renderer" section)

- [ ] **Step 1: Disabled = unchanged**

Run the game with default config. Expected: log contains `[native] native renderer disabled`; main-menu FPS within noise of Task 0; no `suppression census` lines.

- [ ] **Step 2: Overlay mode**

Set `[native] enabled = true` in `out\build\win-amd64-release\fable2_config.toml`; run. Expected: log `[native] native renderer installed (mode=overlay, F6 toggles)`; a green 64px grid over the menu and gameplay; F6 hides/shows it (log `[native] F6: native renderer off/on`).

- [ ] **Step 3: Replace mode**

Run with `--fable2_native_render_mode=replace`. Expected: checkerboard with a sweeping orange bar replaces the game image; F6 returns to the live emulated game and back; game audio/logic keep running throughout.

- [ ] **Step 4: Forced failure falls back**

Temporarily change `kPatternPs` in `src/native/fable2_native_shaders.h` to contain `return undefined_symbol;`, rebuild, run in replace mode. Expected: exactly one `[native] shader compile failed; falling back to emulated output (F6 retries)` line and the normal game image. Revert the change and rebuild.

- [ ] **Step 5: Suppression smoke test**

In replace mode run with `--native_render_suppress_emulated_draws=true --native_render_suppress_pitches=*`. Expected: test pattern shows; F3 overlay FPS is higher than in plain replace mode (emulated draws skipped); toggling F6 back to emulated shows a correct game image within a frame or two (no stale garbage after the first emulated frame).

- [ ] **Step 6: Vulkan unaffected**

Run `fable2.cmd vulkan` (or `--gpu_plugin=xenos-vulkan` if the Vulkan plugin is staged) with `[native] enabled = true`. Expected: normal emulated rendering, `[native] native renderer installed` logged, no grid, no errors. If the Vulkan plugin is not staged in this build flow, record "skipped: Vulkan plugin not staged".

- [ ] **Step 7: Run all tests**

Run:
```
python -m unittest discover -s tests -p "test_*.py"
tests\run_native_tests.cmd
```
Expected: all pass.

- [ ] **Step 8: README section**

Add to `README.md` under the command-line options section:
```markdown
### Experimental native renderer

Foundation for a native D3D12 renderer (see
`docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md`).
Enable with `[native] enabled = true` in `fable2_config.toml`. Modes (cvar
`fable2_native_render_mode`): `overlay` (default) draws a grid over the
emulated frame; `replace` draws a test pattern instead of the game. F6 toggles
it live. Any native failure falls back to the emulated image. D3D12 only.
```

- [ ] **Step 9: Commit**

```
rtk git add README.md
rtk git commit -m "Document the experimental native renderer toggle"
```
