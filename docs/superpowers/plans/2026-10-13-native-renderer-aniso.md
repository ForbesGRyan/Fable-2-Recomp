# Native anisotropic texture filtering (sub-project 15) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The native colour pass (and the passes sharing its layout) filter textures as the SDK does under `anisotropic_override`, so native ground at grazing angles carries the game's detail.

**Architecture:** The main clay binding layout is created per filter mode (six, cached, like the water's), with s1/s3 anisotropic in modes 1..5; a mode change read from the cvar in `Ensure` rebuilds the pass's objects. The sampler index carried to the shaders gains bit 2 "level 0 only" for textures the SDK would not filter anisotropically (single level, base-map mip filter); the five changed shader sites then sample with `SampleLevel(…, 0)`. Evidence reuses the water-ripple scripts with a ground box.

**Tech Stack:** C++23 (clang), HLSL SM 5.0 through the SDK's native RHI (D3D12), Python 3.12 unittest, PowerShell driver `tools/drive_game.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-13-native-renderer-aniso-design.md`

## Global Constraints

- Work in the worktree `C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\aniso` (branch `native-renderer-aniso`), never in the main checkout; never write `out\sdk-install` or `out\tooling\runtime-sdk` of the main tree; no SDK source change (spec ruling 2).
- Build: `cmake --build out\build\win-amd64-release --target fable_2` in the worktree (Task 1 configures it); no `fable_2.exe` may be running when building; never kill one you did not start.
- Native tests: `& .\tests\run_native_tests.cmd C:\Users\Ryan\code\Fable-2-Recomp\thirdparty\rexglue-sdk` from the worktree with `C:\Program Files\LLVM\bin` first on PATH. Python: `python -m unittest discover -s tests -p "test_*.py"` from the worktree (copy `C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump` to the worktree's `out\shader_dump` first, or dump-backed cases are skipped).
- Game runs: allowed ("run when needed"); announce each to the lanterns session (SendMessage to `fable-2-recomp-62`) before starting; never while another `fable_2.exe` is open; `-UserPresent` runs are labelled `RUN UNCHECKED`; raw evidence under `out\native-evidence\aniso\`.
- Commit after every task; messages end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; no push.
- Shader bind points stay below `kClayStaticSamplers == 4`; constants' field offsets (`kDrawFields`, `kClayTerrainFields`) do not move; `SamplerIndex` bits 0 and 1 keep their meaning; mode 0 must produce today's bytes-for-bytes picture.
- Every ruling made during execution goes into `.superpowers/sdd/2026-10-13-native-renderer-aniso/progress.md` as `Ruling N: … - why: … - cost if wrong: …`.

## Review Focus

1. A linear texture with one level (no mip address, DXT3A, or `base_only` after an unreadable mip region) under mode 3 must be sampled isotropically at level 0, not anisotropically (Task 3/4 tests: bit 2 set for each of the three causes).
2. A cached texture whose mips were readable at upload but not in a later frame must keep the bit of the texture actually bound (the cache entry's level count), not of this frame's plan (Task 4: the bit comes from `Entry::levels`).
3. The water step's sampler indices must never carry bit 2 (its shader's select chains treat 4..7 as linear clamp): Task 4 sets the bit only on the albedo path (`water == nullptr`), pinned by a test on the helper's contract and by the water tests' existing values.
4. A cvar change at run time (F3 console, remote-control `cvar anisotropic_override 0`) must rebuild the pass once, not every frame, and mode 0 after a rebuild must give the mode-0 layout (Task 7: rebuild test through the pure `FilterRebuildNeeded` rule; the clay-logic test covers the sequence 3 → 3 → 0 → 0 → 5).
5. A parse failure of the cvar text (empty, garbage, "3 ") must read as mode 0, never as a crash or a stale mode (Task 7: `ParseFilterSetting` pinned for "", "x", "3 ", "-1", "5", "6").

---

### Task 1: Worktree build and the "before" binary

**Files:**
- Create: `out/native-evidence/aniso/before/README.md` (evidence; `out/` is git-ignored)
- Modify: none in git

**Interfaces:**
- Produces: a configured `out\build\win-amd64-release` in the worktree, a staged game folder there, and `out\native-evidence\aniso\before\fable_2.exe` + `sha256.txt` built from fa6b4cd (code of 3c84229) for the "before" runs of Task 9.

- [ ] **Step 1: Configure the worktree build (PowerShell, from the worktree)**

```powershell
$M = "C:\Users\Ryan\code\Fable-2-Recomp"; $W = "$M\.claude\worktrees\aniso"
Set-Location $W
$env:PATH = "C:\Program Files\LLVM\bin;" + $env:PATH
if (-not (Test-Path "$W\default.xex")) { New-Item -ItemType HardLink -Path "$W\default.xex" -Target "$M\default.xex" | Out-Null }
if (-not (Test-Path "$W\generated\rexglue.cmake")) { & "$M\out\sdk-install\win-amd64\bin\rexglue.exe" codegen fable_2_manifest.toml }
$sdk = "$M\out\tooling\runtime-sdk\win-amd64"
cmake --preset win-amd64-release "-DCMAKE_PREFIX_PATH=$sdk" "-Drexglue_DIR=$sdk\lib\cmake\rexglue" "-DREXGLUE_SDK_ROOT=$sdk" "-DREXGLUE_SDK_SOURCE=$M\thirdparty\rexglue-sdk" "-DFABLE2_CODEGEN_TOOL=$M\out\sdk-install\win-amd64\bin\rexglue.exe" "-DFABLE2_BUILD_PROFILE=goty-compatible"
```

Expected: "Configuring done", "Generating done". If `rexglue.exe codegen` complains about the manifest, read `docs/superpowers/plans/2026-10-07-boot-save.md` Task 2 (lines 316-397) for the full recipe and follow it.

- [ ] **Step 2: Build (20-minute timeout; no fable_2.exe running: check `Get-Process fable_2 -ErrorAction SilentlyContinue` is empty first)**

```powershell
cmake --build out\build\win-amd64-release --target fable_2 2>&1 | Select-Object -Last 15
```

Expected: `fable_2.exe` under `out\build\win-amd64-release\` (the exact subfolder is the one the main tree's build uses: `Get-ChildItem -Recurse -Filter fable_2.exe out\build\win-amd64-release`).

- [ ] **Step 3: Stage the game content beside the exe (from boot-save plan Task 2, Step 3)**

```powershell
$D = (Get-ChildItem -Recurse -Filter fable_2.exe "$W\out\build\win-amd64-release" | Select-Object -First 1).DirectoryName
$MD = (Get-ChildItem -Recurse -Filter fable_2.exe "$M\out\build\win-amd64-release" | Select-Object -First 1).DirectoryName
Copy-Item "$MD\data\dir.manifest" "$D\data\dir.manifest" -Force
Get-ChildItem -Recurse -File "$MD\data" | Where-Object { $_.FullName -notlike "*\data\dir.manifest" -and $_.FullName -notlike "*\data\scripts\recomp\*" } | ForEach-Object {
  $rel = $_.FullName.Substring($MD.Length + 1); $dst = Join-Path $D $rel
  New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
  if (-not (Test-Path $dst)) { New-Item -ItemType HardLink -Path $dst -Target $_.FullName | Out-Null }
}
foreach ($d in @('$SystemUpdate')) { if (Test-Path "$MD\$d") { Get-ChildItem -Recurse -File "$MD\$d" | ForEach-Object { $rel = $_.FullName.Substring($MD.Length + 1); $dst = Join-Path $D $rel; New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null; if (-not (Test-Path $dst)) { New-Item -ItemType HardLink -Path $dst -Target $_.FullName | Out-Null } } } }
if (-not (Test-Path "$D\default.xex")) { New-Item -ItemType HardLink -Path "$D\default.xex" -Target "$M\default.xex" | Out-Null }
Copy-Item -Recurse -Force "$MD\saves" "$D\saves"; Copy-Item -Recurse -Force "$MD\cache" "$D\cache"; Copy-Item -Force "$MD\fable2_config.toml" "$D\fable2_config.toml"
cmake --build out\build\win-amd64-release --target fable_2 2>&1 | Select-Object -Last 3
fsutil hardlink list "$D\data\dir.manifest"
```

Expected: the `fsutil` list shows one path (the manifest is a real copy, not a link to the main tree's). If `saves` or `cache` do not exist beside the main exe, look for them one folder up and copy from there; record where in the README.

- [ ] **Step 4: Keep the "before" binary and record it**

```powershell
New-Item -ItemType Directory -Force "$M\out\native-evidence\aniso\before" | Out-Null
Copy-Item "$D\fable_2.exe" "$M\out\native-evidence\aniso\before\fable_2.exe" -Force
(Get-FileHash "$D\fable_2.exe" -Algorithm SHA256).Hash | Set-Content "$M\out\native-evidence\aniso\before\sha256.txt"
@"
Before binary: fable_2.exe built from native-renderer-aniso at fa6b4cd (code of 3c84229) in the aniso worktree, $(Get-Date -Format s).
Game folder: $D
"@ | Set-Content "$M\out\native-evidence\aniso\before\README.md"
```

- [ ] **Step 5: Record in the ledger**

Append to `.superpowers/sdd/2026-10-13-native-renderer-aniso/progress.md` under "Progress": the game folder path, the exe SHA-256, and any deviation from the recipe. No commit (nothing in git changed).

---

### Task 2: Count the filter cases in a discovery capture (offline evidence)

**Files:**
- Create: `out/native-evidence/aniso/fetch_filters.py`, `out/native-evidence/aniso/fetch_filters.json`

**Interfaces:**
- Consumes: `tools/xdk_sigmatch/water_textures.py` `read_rows(path)` and `_words(fc)` (a row's `tf` dict maps slot → six fetch words).
- Produces: counts of scene-draw fetches by (mag, min, mip filter, has mip address) feeding the ledger ruling on how many textures take bit 2.

- [ ] **Step 1: Write the script**

```python
"""Count texture fetch filter cases in a native discovery capture (sub-project 15, plan Task 2).
usage: python fetch_filters.py <native_discovery_*.jsonl> [out.json]"""
import collections, json, sys
sys.path.insert(0, r"C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\aniso\tools\xdk_sigmatch")
import water_textures as wt

FILTER = {0: "point", 1: "linear", 2: "basemap", 3: "fetch"}

def main(argv):
    rows = wt.read_rows(argv[0])
    counts = collections.Counter()
    seen = set()
    for r in rows:
        if r.get("kind") not in ("draw", "indx", "verts") and "tf" not in r:
            continue
        for slot, fc in (r.get("tf") or {}).items():
            words = tuple(wt._words(fc))
            if len(words) != 6 or (words[0] & 3) != 2:
                continue
            key = words[1] & 0xFFFFF000, words[5] & 0xFFFFF000, words[2]
            if key in seen:
                continue
            seen.add(key)
            mag = FILTER[(words[3] >> 19) & 3]; mn = FILTER[(words[3] >> 21) & 3]; mip = FILTER[(words[3] >> 23) & 3]
            aniso = (words[3] >> 25) & 7
            has_mips = (words[5] & 0xFFFFF000) != 0 and ((words[4] >> 6) & 0xF) > 0
            counts[f"mag={mag} min={mn} mip={mip} mips={'yes' if has_mips else 'no'} aniso={aniso}"] += 1
    out = {"capture": argv[0], "unique_textures": len(seen), "cases": dict(sorted(counts.items()))}
    print(json.dumps(out, indent=2))
    if len(argv) > 1:
        with open(argv[1], "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)

if __name__ == "__main__":
    main(sys.argv[1:])
```

If `read_rows` filters rows in a way that drops draw rows, read `water_textures.py:267-300` and adapt: the need is every row with a `tf` dict.

- [ ] **Step 2: Run it on the newest world capture with records**

```powershell
python out\native-evidence\aniso\fetch_filters.py C:\Users\Ryan\code\Fable-2-Recomp\out\build\win-amd64-release\logs\native_discovery_20261012_195640.jsonl out\native-evidence\aniso\fetch_filters.json
```

(That stamp is run 774's capture, a bridge world frame; if it is missing, pick the newest `native_discovery_*.jsonl` whose rows have `in_scene` true.) Expected: a JSON with cases like `mag=linear min=linear mip=linear mips=yes aniso=0`.

- [ ] **Step 3: Ruling**

Write in the ledger: the counts; `Ruling 8: textures with mag linear and min point (or mip base map) with mips number N of M; bit 2 covers … ; the approximation of spec section 3 concerns N textures - why: the counts - cost if wrong: those textures filter anisotropically where the game filters them trilinearly`.

---

### Task 3: Decode the min and mip filters; the level-0 bit

**Files:**
- Modify: `src/native/render/texture_decode.h:66-79` (struct), `:96-104` (decode), `:765-769` (`SamplerIndex`)
- Test: `tests/native/test_texture_decode.cpp:137-141`

**Interfaces:**
- Produces: `TextureFetch::min_linear` (bool), `TextureFetch::mip_filter` (uint8_t, fc[3] bits 23..24: 0 point, 1 linear, 2 base map, 3 use-fetch), `inline constexpr uint32_t kSamplerLevel0 = 4u`, `inline uint32_t SamplerBits(const TextureFetch& t, uint32_t levels)` = `SamplerIndex(t) | (levels > 1 && t.mip_filter != 2 ? 0u : kSamplerLevel0)`. `SamplerIndex` unchanged.

- [ ] **Step 1: Add the failing checks (after the existing return-31 line)**

```cpp
  // Sub-project 15: the min and mip filters are decoded; SamplerBits adds bit 2 "level 0 only" for a texture
  // the SDK would not filter anisotropically (one level, or a base-map mip filter).
  a = {}; a.filter = 1; a.clamp_x = 0; MakeFc(a, fc);
  fc[3] = (fc[3] & ~(3u << 21) & ~(3u << 23)) | (1u << 21) | (1u << 23);   // min linear, mip linear
  DecodeTextureFetch(fc, &t);
  if (!t.min_linear || t.mip_filter != 1) return 33;
  if (SamplerBits(t, 9) != 1) return 34;                                  // mipped linear: no bit 2
  if (SamplerBits(t, 1) != (1u | kSamplerLevel0)) return 35;              // one level: bit 2
  fc[3] = (fc[3] & ~(3u << 23)) | (2u << 23);                              // mip base map
  DecodeTextureFetch(fc, &t);
  if (t.mip_filter != 2 || SamplerBits(t, 9) != (1u | kSamplerLevel0)) return 36;
  fc[3] = (fc[3] & ~(3u << 21) & ~(3u << 23));                             // min point, mip point
  DecodeTextureFetch(fc, &t);
  if (t.min_linear || t.mip_filter != 0 || SamplerBits(t, 9) != 1) return 37;   // (approximated, as today)
  if (kSamplerLevel0 != 4u || (SamplerBits(t, 0) & 3u) != SamplerIndex(t)) return 38;
```

Check how `MakeFc` builds word 3 (`test_texture_decode.cpp`, the `MakeFc` helper near the top): if it already has fields for min/mip filters, use them instead of the bit arithmetic above.

- [ ] **Step 2: Run to verify it fails**

```powershell
$env:PATH = "C:\Program Files\LLVM\bin;" + $env:PATH
clang++ -std=c++23 tests\native\test_texture_decode.cpp -o out\tests\runtime-unit-tests\texture_decode.exe; .\out\tests\runtime-unit-tests\texture_decode.exe; $LASTEXITCODE
```

Expected: compile error (`min_linear`, `SamplerBits`, `kSamplerLevel0` undeclared).

- [ ] **Step 3: Implement**

In `TextureFetch` after `bool linear_filter = false;  // mag filter linear`:

```cpp
  bool min_linear = false;               // min filter linear (fc[3] bits 21..22 == 1)
  uint8_t mip_filter = 0;                // fc[3] bits 23..24: 0 point, 1 linear, 2 base map (level 0 only), 3 fetch
```

In `DecodeTextureFetch` after the `linear_filter` line:

```cpp
  t.min_linear = ((fc[3] >> 21) & 3) == 1;
  t.mip_filter = uint8_t((fc[3] >> 23) & 3);
```

After `SamplerIndex`:

```cpp
// Bit 2 of the sampler word the shaders read (sub-project 15): "level 0 only". Set for a texture the SDK would
// not filter anisotropically under anisotropic_override (thirdparty/rexglue-sdk/src/graphics/d3d12/
// texture_cache.cpp, the has_mips && !mip_base_map test): a host texture of one level (`levels`, the levels
// the cache bound: no mip address, DXT3A, or base only), or a base-map mip filter. The shaders then sample
// with SampleLevel(…, 0): bilinear at level 0, the SDK's picture, whatever samplers s1 and s3 are. A mipped
// texture with a point min filter keeps the filtered path (an approximation, as before the sub-project).
inline constexpr uint32_t kSamplerLevel0 = 4u;
inline uint32_t SamplerBits(const TextureFetch& t, uint32_t levels) {
  return SamplerIndex(t) | (levels > 1 && t.mip_filter != 2 ? 0u : kSamplerLevel0);
}
```

- [ ] **Step 4: Run to verify it passes**

Same command. Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/texture_decode.h tests/native/test_texture_decode.cpp
git commit -m "feat: decode min/mip filters and the level-0 sampler bit"
```

---

### Task 4: The cache reports the bound texture's level count

**Files:**
- Modify: `src/native/render/texture_cache.h:89-95` (`Entry`), `src/native/render/texture_cache.cpp:328-336` (hit branch), `:393` (sampler), `:504-512` (entry fill)
- Test: `tests/native/test_texture_decode.cpp` (one more check: the helper contract), and the existing water tests (`tests/native/test_clay_logic.cpp` WaterTests, `tests/test_water*.py`) stay green.

**Interfaces:**
- Consumes: `SamplerBits`, `kSamplerLevel0` (Task 3).
- Produces: `Entry::levels` (uint8_t, levels of the host texture); `Resolve`'s `*sampler` for an albedo is `SamplerBits(t, bound_levels)`; for a water texture it stays `SamplerIndex(t)` (no bit 2).

- [ ] **Step 1: Write the failing check (a pure contract, in test_texture_decode.cpp after return 38)**

```cpp
  // The cache's rule for the word it hands out (texture_cache.cpp, ResolveFetch): the bound levels decide,
  // and a water texture never carries bit 2 (its shader treats 4..7 as linear clamp).
  if (ResolveSamplerWord(t, 1, false) != (SamplerIndex(t) | kSamplerLevel0) || ResolveSamplerWord(t, 9, false) != SamplerIndex(t) ||
      ResolveSamplerWord(t, 1, true) != SamplerIndex(t)) return 39;
```

- [ ] **Step 2: Run to verify it fails** (same command as Task 3). Expected: `ResolveSamplerWord` undeclared.

- [ ] **Step 3: Implement**

`texture_decode.h`, after `SamplerBits`:

```cpp
// The word TextureCache::Resolve hands out: an albedo's SamplerBits of the levels the cache bound; a water
// texture's SamplerIndex (the water step's own rule, water_sampler.h, judges its filtering).
inline uint32_t ResolveSamplerWord(const TextureFetch& t, uint32_t bound_levels, bool water) {
  return water ? SamplerIndex(t) : SamplerBits(t, bound_levels);
}
```

`texture_cache.h`, `Entry`:

```cpp
  struct Entry {
    nrhi::Texture* tex = nullptr;
    nrhi::TextureView* view = nullptr;
    uint64_t bytes = 0;  // host texture bytes
    float uv_fix[2] = {1.0f, 1.0f};
    uint8_t levels = 1;  // host levels (SamplerBits: one level means "level 0 only")
  };
```

`texture_cache.cpp`, the hit branch (line ~328):

```cpp
  nrhi::TextureView* view = nullptr;
  uint32_t bound_levels = plan.levels;
  const LookupResult found = index_.Lookup(key, state.sample_hash);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      view = it->second.view;
      uv_fix[0] = it->second.uv_fix[0];
      uv_fix[1] = it->second.uv_fix[1];
      bound_levels = it->second.levels;
    }
  }
```

Line ~393: `*sampler = ResolveSamplerWord(t, bound_levels, water != nullptr);`

Entry fill (line ~504): add `e.levels = uint8_t(std::min<uint32_t>(plan.levels, 255));` — and in the upload branch of `ResolveFetch`, after a successful `Upload`, `bound_levels = plan.levels;` (it already is; keep the assignment explicit).

- [ ] **Step 4: Run the decode test and the clay-logic water tests**

```powershell
clang++ -std=c++23 tests\native\test_texture_decode.cpp -o out\tests\runtime-unit-tests\texture_decode.exe; .\out\tests\runtime-unit-tests\texture_decode.exe; $LASTEXITCODE
clang++ -std=c++23 -fsanitize=float-cast-overflow -fsanitize-trap=float-cast-overflow -Wl,/STACK:16777216 tests\native\test_clay_logic.cpp -o out\tests\runtime-unit-tests\clay_logic.exe; .\out\tests\runtime-unit-tests\clay_logic.exe | Select-Object -Last 1; $LASTEXITCODE
```

Expected: both exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/native/render/texture_decode.h src/native/render/texture_cache.h src/native/render/texture_cache.cpp tests/native/test_texture_decode.cpp
git commit -m "feat: hand the shaders the level-0 bit from the bound texture"
```

---

### Task 5: Shared filter-mode helpers and sampler configuration

**Files:**
- Modify: `src/native/render/clay_logic.h:7529-7537` (rename), `src/native/render/water_sampler.h` → rename to `src/native/render/clay_samplers.h` (git mv), `src/native/render/clay_pass.cpp` (include, `ConfigureWaterSamplers` → `ConfigureClaySamplers`, `WaterFilter*` → `Filter*`, `kWaterFilterModes` → `kFilterModes`), `src/native/render/clay_pass.h:927` (`kWaterFilterModes` → `kFilterModes`), `tests/native/test_clay_logic.cpp:2674-2684` (renamed names, same numbers)
- Test: `tests/native/test_water_sampler.cpp` → `tests/native/test_clay_samplers.cpp` (git mv), `tests/run_native_tests.cmd:30-31`

**Interfaces:**
- Produces (in `clay_logic.h`): `inline constexpr uint32_t kFilterModes = 6;`, `constexpr uint32_t FilterMode(int32_t override_value)`, `constexpr uint32_t FilterAnisotropy(uint32_t mode)` (same bodies as the water's). In `clay_samplers.h`: `inline void ConfigureClaySamplers(rex::graphics::nrhi::BindingLayoutDesc& desc, uint32_t mode)` (the body of `ConfigureWaterSamplers`), and `WaterSurfaceFilterMode` unchanged. In `texture_decode.h`: nothing.

- [ ] **Step 1: Rename the test and make it fail on the new names**

```bash
git mv tests/native/test_water_sampler.cpp tests/native/test_clay_samplers.cpp
git mv src/native/render/water_sampler.h src/native/render/clay_samplers.h
```

Edit `tests/native/test_clay_samplers.cpp`: include `"../../src/native/render/clay_samplers.h"`, call `ConfigureClaySamplers(desc, FilterMode(setting))`, keep `WaterSurfaceFilterMode` checks, and add after the loop:

```cpp
  if (kFilterModes != 6 || FilterAnisotropy(3) != 4 || FilterMode(7) != 0) return 7;
  std::puts("clay_samplers PASS (point/wrap/clamp, all override modes, water eligibility)");
```

(replace the old `puts`). In `tests/run_native_tests.cmd` lines 30-31 change `test_water_sampler.cpp` / `water_sampler.exe` to `test_clay_samplers.cpp` / `clay_samplers.exe`.

- [ ] **Step 2: Run to verify it fails**

```powershell
clang++ -std=c++23 -I"C:\Users\Ryan\code\Fable-2-Recomp\thirdparty\rexglue-sdk\include" tests\native\test_clay_samplers.cpp -o out\tests\runtime-unit-tests\clay_samplers.exe
```

Expected: compile error (`ConfigureClaySamplers`, `FilterMode` undeclared).

- [ ] **Step 3: Rename in the sources**

`clay_logic.h` 7529-7537: replace `kWaterFilterModes` → `kFilterModes`, `WaterFilterMode` → `FilterMode`, `WaterFilterAnisotropy` → `FilterAnisotropy`, and the comment's first line to `// The SDK's anisotropic_override as one filter mode for the clay's and the water's samplers (sub-projects 13 and 15).`.
`clay_samplers.h`: `#include "clay_logic.h"`; rename `ConfigureWaterSamplers` → `ConfigureClaySamplers`, `WaterFilterAnisotropy` → `FilterAnisotropy`, `kWaterFilterModes` → `kFilterModes`, `WaterFilterMode` → `FilterMode`. Then in `clay_pass.cpp` and `clay_pass.h` and `test_clay_logic.cpp` (WaterTests, CHECK 867) apply the same renames:

```bash
grep -rln "WaterFilterMode\|WaterFilterAnisotropy\|kWaterFilterModes\|ConfigureWaterSamplers\|water_sampler.h" src tests
```

must print nothing afterwards.

- [ ] **Step 4: Run the sampler test and the clay-logic test** (commands of Task 4 Step 4 plus the sampler compile). Expected: exit 0, `clay_samplers PASS`.

- [ ] **Step 5: Commit**

```bash
git add -A src/native/render tests/native/test_clay_samplers.cpp tests/native/test_clay_logic.cpp tests/run_native_tests.cmd
git commit -m "refactor: share the filter-mode helpers between water and clay"
```

---

### Task 6: The shaders honour the level-0 bit

**Files:**
- Modify: `src/native/fable2_native_shaders.h:811-814` (textured select), `:1028-1033` (`sample_colour`), `:1048-1053` (`sample_base`), `:1095-1100` (`sample_side`), `:1198-1201` (shadow select)
- Test: `tests/native/test_native_shaders.cpp:905-925` (`kPinTexturedMain`), `:1013-1020` and `:1076-1079` (`kPinTerrainCommon`), `:1637-1639` (side strstr), the three `before[]` blocks at `:4950-4990`, `:5340-5370`, `:5670-5700`, and a new case.

**Interfaces:**
- Consumes: the sampler words' bit 2 (`kSamplerLevel0 == 4`, Task 3).
- Produces: HLSL helper macro text in the common blocks, used by five sites; a test transform `SamplerSelectBefore(std::string)` that turns the new select text back into the old, so the three historical hash arrays keep their recorded values (the pattern of `EffectsOff` in the presentation work: the arrays record bytes of before a feature; the transform shows the change is confined to these lines).

- [ ] **Step 1: Write the failing test case (a free number: pick the next unused one; grep `// <n> (` headings) near the other shader-text cases**

```cpp
  // <n>: sub-project 15: the sampler words' bit 2 (level 0 only). Each of the five sites selects the register by
  // the word's low two bits and, with bit 2, samples with SampleLevel 0 (bilinear at level 0, isotropic whatever
  // s1 and s3 are: D3D11.3 functional spec 7.18.11); without it as before. The compiled programs read sample_l
  // beside sample / sample_d at those sites.
  {
    using namespace fable2::native::shaders;
    const char* sites[] = {
        "    uint sampler_select = sampler_index & 3;\n    if (sampler_index & 4) {\n      if (sampler_select == 0) t = albedo.SampleLevel(s_point_wrap, uv, 0);\n",
        "float4 sample_colour(float2 at) {\n  uint sel = t_colour_sampler & 3;\n  if (t_colour_sampler & 4) {\n",
        "float4 sample_base(float2 at, float4 d) {\n  float4 t;\n  uint sel = t_base_sampler & 3;\n  if (t_base_sampler & 4) {\n",
        "float4 sample_side(float2 at, float4 d) {\n  uint sel = t_side_sampler & 3;\n  if (t_side_sampler & 4) {\n",
    };
    if (!strstr(kClayTexturedPs, sites[0]) || !strstr(kClayShadowPs, sites[0]) || !strstr(kClayTerrainLayerPs, sites[1]) ||
        !strstr(kClayTerrainFarPs, sites[1]) || !strstr(kClayTerrainLayerPs, sites[2]) || !strstr(kClayTerrainLayerPs, sites[3])) return <n>;
    for (const char* src : {kClayTexturedPs, kClayShadowPs, kClayTerrainLayerPs, kClayTerrainFarPs}) {
      const std::string l = Disassembly("level0", src, "ps_5_0");
      if (l.find("sample_l") == std::string::npos) return <n>;
    }
    if (!DisassemblyHas("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "sample_d")) return <n>;
    std::cout << "level-0 bit: five sites select by the low bits and SampleLevel 0 with bit 2\n";
  }
```

Also add, next to `EffectsOff`-style helpers (search for `static std::string PreviousOff(`), the transform:

```cpp
// Sub-project 15: the sampler sites' text before the level-0 bit, for the historical hash arrays (the wind's,
// the impostors', the water's): the new select blocks replaced by the old four-line chains, nothing else touched.
static std::string SamplerSelectBefore(std::string text) {
  struct Pair { const char* now; const char* before; };
  const Pair pairs[] = {
      {"    uint sampler_select = sampler_index & 3;\n    if (sampler_index & 4) {\n"
       "      if (sampler_select == 0) t = albedo.SampleLevel(s_point_wrap, uv, 0);\n"
       "      else if (sampler_select == 1) t = albedo.SampleLevel(s_linear_wrap, uv, 0);\n"
       "      else if (sampler_select == 2) t = albedo.SampleLevel(s_point_clamp, uv, 0);\n"
       "      else t = albedo.SampleLevel(s_linear_clamp, uv, 0);\n"
       "    } else if (sampler_select == 0) t = albedo.Sample(s_point_wrap, uv);\n"
       "    else if (sampler_select == 1) t = albedo.Sample(s_linear_wrap, uv);\n"
       "    else if (sampler_select == 2) t = albedo.Sample(s_point_clamp, uv);\n"
       "    else t = albedo.Sample(s_linear_clamp, uv);\n",
       "    if (sampler_index == 0) t = albedo.Sample(s_point_wrap, uv);\n"
       "    else if (sampler_index == 1) t = albedo.Sample(s_linear_wrap, uv);\n"
       "    else if (sampler_index == 2) t = albedo.Sample(s_point_clamp, uv);\n"
       "    else t = albedo.Sample(s_linear_clamp, uv);\n"},
      {"float4 sample_colour(float2 at) {\n  uint sel = t_colour_sampler & 3;\n  if (t_colour_sampler & 4) {\n"
       "    if (sel == 0) return t_colour.SampleLevel(s_point_wrap, at, 0);\n"
       "    if (sel == 1) return t_colour.SampleLevel(s_linear_wrap, at, 0);\n"
       "    if (sel == 2) return t_colour.SampleLevel(s_point_clamp, at, 0);\n"
       "    return t_colour.SampleLevel(s_linear_clamp, at, 0);\n  }\n"
       "  if (sel == 0) return t_colour.Sample(s_point_wrap, at);\n"
       "  if (sel == 1) return t_colour.Sample(s_linear_wrap, at);\n"
       "  if (sel == 2) return t_colour.Sample(s_point_clamp, at);\n"
       "  return t_colour.Sample(s_linear_clamp, at);\n}\n",
       "float4 sample_colour(float2 at) {\n"
       "  if (t_colour_sampler == 0) return t_colour.Sample(s_point_wrap, at);\n"
       "  if (t_colour_sampler == 1) return t_colour.Sample(s_linear_wrap, at);\n"
       "  if (t_colour_sampler == 2) return t_colour.Sample(s_point_clamp, at);\n"
       "  return t_colour.Sample(s_linear_clamp, at);\n}\n"},
      {"float4 sample_base(float2 at, float4 d) {\n  float4 t;\n  uint sel = t_base_sampler & 3;\n  if (t_base_sampler & 4) {\n"
       "    if (sel == 0) t = t_base.SampleLevel(s_point_wrap, at, 0);\n"
       "    else if (sel == 1) t = t_base.SampleLevel(s_linear_wrap, at, 0);\n"
       "    else if (sel == 2) t = t_base.SampleLevel(s_point_clamp, at, 0);\n"
       "    else t = t_base.SampleLevel(s_linear_clamp, at, 0);\n"
       "  } else if (sel == 0) t = t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);\n"
       "  else if (sel == 1) t = t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw);\n"
       "  else if (sel == 2) t = t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw);\n"
       "  else t = t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw);\n",
       "float4 sample_base(float2 at, float4 d) {\n  float4 t;\n"
       "  if (t_base_sampler == 0) t = t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);\n"
       "  else if (t_base_sampler == 1) t = t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw);\n"
       "  else if (t_base_sampler == 2) t = t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw);\n"
       "  else t = t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw);\n"},
      {"float4 sample_side(float2 at, float4 d) {\n  uint sel = t_side_sampler & 3;\n  if (t_side_sampler & 4) {\n"
       "    if (sel == 0) return t_side.SampleLevel(s_point_wrap, at, 0);\n"
       "    if (sel == 1) return t_side.SampleLevel(s_linear_wrap, at, 0);\n"
       "    if (sel == 2) return t_side.SampleLevel(s_point_clamp, at, 0);\n"
       "    return t_side.SampleLevel(s_linear_clamp, at, 0);\n  }\n"
       "  if (sel == 0) return t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw);\n"
       "  if (sel == 1) return t_side.SampleGrad(s_linear_wrap, at, d.xy, d.zw);\n"
       "  if (sel == 2) return t_side.SampleGrad(s_point_clamp, at, d.xy, d.zw);\n"
       "  return t_side.SampleGrad(s_linear_clamp, at, d.xy, d.zw);\n}\n",
       "float4 sample_side(float2 at, float4 d) {\n"
       "  if (t_side_sampler == 0) return t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw);\n"
       "  if (t_side_sampler == 1) return t_side.SampleGrad(s_linear_wrap, at, d.xy, d.zw);\n"
       "  if (t_side_sampler == 2) return t_side.SampleGrad(s_point_clamp, at, d.xy, d.zw);\n"
       "  return t_side.SampleGrad(s_linear_clamp, at, d.xy, d.zw);\n}\n"},
  };
  for (const Pair& p : pairs) {
    for (size_t at = text.find(p.now); at != std::string::npos; at = text.find(p.now, at + 1)) text.replace(at, std::strlen(p.now), p.before);
  }
  return text;
}
```

In each of the three `before[]` loops (lines ~4987, ~5366, ~5691) hash `SamplerSelectBefore(before[i].src)` as the presentation work does with `EffectsOff` (the loop variable: `const std::string historical = SamplerSelectBefore(before[i].src); CodeHash(before[i].name, historical.c_str(), ...)`), and add before each loop a guard that the transform changed the four programs it names (textured, terrain layer, terrain far, shadow): `if (SamplerSelectBefore(kClayTexturedPs) == kClayTexturedPs) return fails(__LINE__);`. If the presentation sub-project's `EffectsOff` is already applied in those loops when this branch is rebased, compose: `SamplerSelectBefore(EffectsOff(...))`.

Update the text pins: `kPinTexturedMain` (905-925) to the new textured block; `kPinTerrainCommon` (1013-1020 `sample_colour`, 1076-1079 `sample_side`, and `sample_base` if pinned there) to the new text; the `strstr` at 1637-1639 to `"t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw)"` (still present), plus `"if (t_side_sampler & 4)"` in place of `"if (t_side_sampler == 0)"`.

- [ ] **Step 2: Run to verify it fails**

```powershell
clang++ -std=c++23 tests\native\test_native_shaders.cpp -ld3dcompiler -o out\tests\runtime-unit-tests\native_shaders.exe; .\out\tests\runtime-unit-tests\native_shaders.exe | Select-Object -Last 3; $LASTEXITCODE
```

Expected: the new case's code (the sites are not in the text yet).

- [ ] **Step 3: Change the five shader sites** (exactly the `now` texts of the transform above; the textured block at 811-814 and the identical shadow block at 1198-1201 both become the `sampler_select` block; `sample_colour`, `sample_base`, `sample_side` become their `now` texts). Add one comment line above the textured block: `// (the word's bit 2: level 0 only, an isotropic SampleLevel; texture_decode.h, SamplerBits)`. The far shader shares `sample_colour` and `sample_base` through `FABLE2_CLAY_TERRAIN_COMMON_HLSL`, so it changes with them.

- [ ] **Step 4: Run the shader test; expect exit 0.** If a historical hash still mismatches, the transform's `now` text differs from the source by whitespace: diff them (`std::cerr` the two strings' first differing offset) and fix the transform, not the hash.

- [ ] **Step 5: Run the Python literal tests that quote shader text**

```powershell
python -m unittest discover -s tests -p "test_*literals*.py" 2>&1 | Select-Object -Last 3
```

Expected: OK. (None quotes these lines today; if one fails, update its quote to the new text.)

- [ ] **Step 6: Commit**

```bash
git add src/native/fable2_native_shaders.h tests/native/test_native_shaders.cpp
git commit -m "feat: sample level 0 only where the sampler word says so"
```

---

### Task 7: The pass follows the override: layouts per mode, a rebuild on change

**Files:**
- Modify: `src/native/render/clay_logic.h` (after `FilterAnisotropy`: `ParseFilterSetting`, `FilterRebuildNeeded`; `ClayStats::filter_mode`), `src/native/render/clay_pass.h:655-658` (`layouts_[kFilterModes]`, `filter_mode_`), `src/native/render/clay_pass.cpp:409-470` (`Ensure`: the mode read and the rebuild), `:496-527` (layout per mode through `ConfigureClaySamplers`), `:656-658` (log line), `:5014-5020` (the water's cvar read uses the helper), `:3759-3762` (`Render` sets `st.filter_mode`)
- Test: `tests/native/test_clay_logic.cpp` (new checks beside 867)

**Interfaces:**
- Consumes: `FilterMode`, `FilterAnisotropy`, `kFilterModes`, `ConfigureClaySamplers` (Task 5).
- Produces (`clay_logic.h`): `inline int32_t ParseFilterSetting(std::string_view text)` (the water block's `from_chars` parse: a whole-string integer, else 0); `constexpr bool FilterRebuildNeeded(bool ready, uint32_t built_mode, uint32_t wanted_mode)` = `ready && built_mode != wanted_mode`; `uint32_t ClayStats::filter_mode`. `clay_pass.h`: `nrhi::BindingLayout* layouts_[kFilterModes] = {};` (cached per mode, never destroyed), `layout_` stays the current mode's pointer, `uint32_t filter_mode_ = 0;` (the mode the objects were built with). `clay_pass.cpp`: `uint32_t ClayPass::ReadFilterMode() const` = `FilterMode(ParseFilterSetting(rex::cvar::GetFlagByName("anisotropic_override")))`.

- [ ] **Step 1: Write the failing checks (in WaterTests after the 867 block)**

```cpp
  // 868: sub-project 15. The cvar's text as a mode: a whole-string integer, else 0; the rebuild rule: only a
  // ready pass whose objects were built with another mode rebuilds (3 → 3 → 0 → 0 → 5: two rebuilds).
  CHECK(868, ParseFilterSetting("3") == 3 && ParseFilterSetting("-1") == -1 && ParseFilterSetting("5") == 5 &&
                 ParseFilterSetting("6") == 6 && ParseFilterSetting("") == 0 && ParseFilterSetting("x") == 0 &&
                 ParseFilterSetting("3 ") == 0 && ParseFilterSetting(" 3") == 0);
  CHECK(868, FilterMode(ParseFilterSetting("6")) == 0 && FilterMode(ParseFilterSetting("3")) == 3);
  {
    const int32_t sequence[] = {3, 3, 0, 0, 5};
    uint32_t built = FilterMode(sequence[0]);
    int rebuilds = 0;
    bool ready = true;
    for (int32_t s : sequence) {
      const uint32_t wanted = FilterMode(s);
      if (FilterRebuildNeeded(ready, built, wanted)) {
        ++rebuilds;
        built = wanted;
      }
    }
    CHECK(868, rebuilds == 2 && built == 5 && !FilterRebuildNeeded(false, 0, 3) && !FilterRebuildNeeded(true, 3, 3));
  }
  {
    ClayStats fs;
    CHECK(868, fs.filter_mode == 0);
  }
```

- [ ] **Step 2: Run to verify it fails** (clay-logic compile command of Task 4). Expected: `ParseFilterSetting` undeclared.

- [ ] **Step 3: Implement the pure parts (`clay_logic.h`)**

After `FilterAnisotropy`:

```cpp
// The cvar's text as the SDK's setting: the whole string an integer (std::from_chars), else 0 (no override).
// The installed SDK exports the registry accessor, not typed Query specializations (the water step's note).
inline int32_t ParseFilterSetting(std::string_view text) {
  int32_t setting = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), setting);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? setting : 0;
}
// A ready pass whose objects were built under another mode rebuilds them (Ensure): the layouts are cached per
// mode (the RHI destroys none), the pipelines of one mode only are kept.
constexpr bool FilterRebuildNeeded(bool ready, uint32_t built_mode, uint32_t wanted_mode) {
  return ready && built_mode != wanted_mode;
}
```

(`#include <charconv>` and `<string_view>` at the top of `clay_logic.h` if missing.) In `ClayStats` add `uint32_t filter_mode = 0;  // the pass's texture filtering this frame: 0 linear, 1..5 = 1x..16x anisotropic (FilterMode)` beside `impostor_mode`.

- [ ] **Step 4: Run the clay-logic test; expect exit 0.**

- [ ] **Step 5: The pass (`clay_pass.h`, `clay_pass.cpp`)**

`clay_pass.h` near line 658: replace `nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction` with

```cpp
  // The main layout, one per filter mode (sub-project 15; the RHI has no layout destruction: the six are cached
  // for the pass's life, `layout_` is the current mode's). `filter_mode_` is the mode the objects were built
  // with; Ensure rebuilds them when the cvar's mode differs (FilterRebuildNeeded).
  nrhi::BindingLayout* layouts_[kFilterModes] = {};
  nrhi::BindingLayout* layout_ = nullptr;
  uint32_t filter_mode_ = 0;
```

and declare `uint32_t ReadFilterMode() const;` among the private helpers.

`clay_pass.cpp`:

```cpp
uint32_t ClayPass::ReadFilterMode() const {
  return FilterMode(ParseFilterSetting(rex::cvar::GetFlagByName("anisotropic_override")));
}
```

`Ensure` (line 409): replace the first two lines with

```cpp
bool ClayPass::Ensure(nrhi::Device* dev) {
  const uint32_t wanted_mode = ReadFilterMode();
  if (ready_ && dev == device_ && !FilterRebuildNeeded(ready_, filter_mode_, wanted_mode)) return true;
  if (ready_ && dev == device_) {
    // The override changed: the objects are made again under the new mode's layout (the layouts stay).
    REXLOG_INFO("[native] clay: texture filtering changes to {} (anisotropic_override): the pass's objects are rebuilt",
                FilterText(wanted_mode));
    DestroyObjects();
  }
  if (dev != device_) {
```

In the device-change block replace `layout_ = nullptr;` with `for (nrhi::BindingLayout*& layout : layouts_) layout = nullptr;\n    layout_ = nullptr;`. At the layout creation (496-527): `nrhi::BindingLayout*& layout = layouts_[wanted_mode]; if (!layout) { ... ConfigureClaySamplers(desc, wanted_mode); ... layout = dev->CreateBindingLayout(desc); if (!layout) {...} } layout_ = layout; filter_mode_ = wanted_mode;` — replacing the four-sampler loop and `desc.static_sampler_count = 4;` with the `ConfigureClaySamplers` call (it sets the count). The log line at 658 becomes

```cpp
  REXLOG_INFO("[native] clay: {} (generation {}; texture filtering {}, anisotropic_override mode {})",
              FormatTargetText(kWidth, kHeight), targets_generation_, FilterText(filter_mode_), filter_mode_);
```

with, in `clay_logic.h` beside `FilterAnisotropy`:

```cpp
inline std::string FilterText(uint32_t mode) {
  return mode ? std::to_string(FilterAnisotropy(mode)) + "x anisotropic" : std::string("linear");
}
```

(add a check to 868: `FilterText(0) == "linear" && FilterText(3) == "4x anisotropic"`). `DestroyObjects` leaves `layouts_` alone (they are never destroyed) and keeps `filter_mode_`. `Render` (3762): `st.filter_mode = filter_mode_;`. The water block (5014-5020): replace the inline parse with `const int32_t setting = ParseFilterSetting(rex::cvar::GetFlagByName("anisotropic_override"));` (one read a frame is fine; the water's own eligibility stays).

`#include "clay_samplers.h"` replaces the old include (Task 5 did this).

- [ ] **Step 6: Syntax-check the pass against the project's flags, then build**

```powershell
cmake --build out\build\win-amd64-release --target fable_2 2>&1 | Select-Object -Last 12
```

Expected: a linked `fable_2.exe`, no errors. Then the native suite:

```powershell
& .\tests\run_native_tests.cmd C:\Users\Ryan\code\Fable-2-Recomp\thirdparty\rexglue-sdk 2>&1 | Select-Object -Last 5; $LASTEXITCODE
```

Expected: 0.

- [ ] **Step 7: Commit**

```bash
git add src/native/render/clay_logic.h src/native/render/clay_pass.h src/native/render/clay_pass.cpp tests/native/test_clay_logic.cpp
git commit -m "feat: filter the world passes by the SDK anisotropic override"
```

---

### Task 8: Full suites and a bounded software/debug run

**Files:**
- Create: `out/native-evidence/aniso/run_warp.ps1` (from `out/native-evidence/water/ripples/run_warp.ps1` with the aniso exe path and shot folder), `out/native-evidence/aniso/warp_result.json`

**Interfaces:**
- Consumes: the Task 7 build.
- Produces: a green full native suite, a green full Python suite, and a WARP run log with zero D3D errors.

- [ ] **Step 1: Python suite (with the shader dumps present)**

```powershell
if (-not (Test-Path out\shader_dump)) { Copy-Item -Recurse C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump out\shader_dump }
python -m unittest discover -s tests -p "test_*.py" 2>&1 | Select-Object -Last 4
```

Expected: `OK` (skips allowed only for tests that name a missing resource).

- [ ] **Step 2: The WARP run.** Copy `out\native-evidence\water\ripples\run_warp.ps1` to `out\native-evidence\aniso\run_warp.ps1`; edit the exe folder to Task 1's game folder, the shot folder to `...\logs\shots\aniso_warp`, keep `--d3d12_adapter=-2 --d3d12_debug=true --fable2_native_d3d_messages=true`, plan `'32:VIEW=native,80:DISC=2/1'`, shots `'85,95'`, `-Total 100`. Before launching: `SendMessage` to `fable-2-recomp-62`: "Launching a 100 s WARP software/debug run of the aniso build now (no hardware GPU use); tell me if that collides." Check no `fable_2.exe` runs (`Get-Process fable_2`). Run it. Read the game log named in the driver output: count `D3D12 ERROR` lines (expected 0) and list warning IDs (expected only 1328/1008).

- [ ] **Step 3: Record** `warp_result.json` (run stamp, log path, error count, warning IDs, exe SHA-256) and a ledger line. Commit nothing (evidence only) unless a bug was fixed; then commit the fix with its test.

---

### Task 9: Paired captures and the ground-detail measure

**Files:**
- Create: `out/native-evidence/aniso/prepare.py` (writes `run_before.ps1`, `run_after.ps1`, `run_control.ps1`), `out/native-evidence/aniso/ground_detail.py`, `out/native-evidence/aniso/results.json`

**Interfaces:**
- Consumes: `out/native-evidence/water/ripples/prepare.py`, `analyse.py`, `filter_control.py` (the measure), `tools/xdk_sigmatch/water_replay._native_planes` (the game plane from the read-back scene and the native plane from the stored scene, both 1120 x 720 linear RGB), the "before" exe of Task 1 and the "after" exe of Task 7.
- Produces: `results.json` with, per run, the ground box's detail RMS (game, native, ratio), the all-colour measure, and the acceptance verdicts of spec acceptance 1 to 3.

- [ ] **Step 1: Prepare the three run scripts.** Copy the ripple `prepare.py` and change: the output folder `out\native-evidence\aniso`, the run names `aniso_before`, `aniso_after`, `aniso_control`, the exe folder per run (before: a copy of Task 1's game folder with `before\fable_2.exe` swapped in — make `out\native-evidence\aniso\before-game\` by copying Task 1's folder and replacing the exe; after/control: Task 1's folder with the Task 7 exe), `$verifyArgs` as the ripple's (`--fable2_native_water=on` stays; wind off), and for `run_control` add `'--anisotropic_override=0'`. Keep `-BootSave Hero000 -Total 100 -Plan '32:VIEW=split,45:DISC=2/1,70:DISC=2/1' -Shots '46,72,90' -UserPresent`.

- [ ] **Step 2: Write `ground_detail.py`**

```python
"""Ground-patch detail of paired captures (sub-project 15, plan Task 9; the ripple fix's measure on a ground box).
usage: python ground_detail.py <run_name> <native_discovery_*.jsonl> <native_scene_*.f2ns> [--box y0 y1 x0 x1]"""
import json, sys
from pathlib import Path
import numpy as np
from scipy.ndimage import gaussian_filter
sys.path.insert(0, r"C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\aniso\tools\xdk_sigmatch")
import water_replay as wr

LOGS = Path(r"C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\aniso\out\build\win-amd64-release\logs")
HERE = Path(__file__).resolve().parent
DEFAULT_BOX = (560, 700, 300, 820)   # rows 560..700 (the lower fifth: ground at a grazing angle), columns 300..820

def detail(rgb, sel):
    lum = rgb @ np.array([.2125, .7154, .0721])
    high = lum - gaussian_filter(lum, 2)
    return float(np.sqrt(np.mean(high[sel] ** 2)))

def main(argv):
    run, capture, dump = argv[0], argv[1], argv[2]
    box = tuple(int(v) for v in argv[argv.index("--box") + 1:argv.index("--box") + 5]) if "--box" in argv else DEFAULT_BOX
    game, native, head = wr._native_planes(LOGS / capture, LOGS / dump)   # read water_replay.py:1870-1885 for the exact signature
    y0, y1, x0, x1 = box
    sel = np.zeros(game.shape[:2], bool); sel[y0:y1, x0:x1] = True
    g, n = detail(game, sel), detail(native, sel)
    colour = wr.all_colour_measure(game, native)                          # the ripple fix's colour verdict (dict)
    out = {"run": run, "capture": capture, "dump": dump, "box": box, "game_detail": g, "native_detail": n,
           "ratio": n / g if g else None, "detail_met": g > 0 and .8 <= n / g <= 1.2, "colour": colour}
    results = HERE / "results.json"
    data = json.loads(results.read_text()) if results.exists() else {}
    data[run] = out
    results.write_text(json.dumps(data, indent=2))
    print(json.dumps(out, indent=2))

if __name__ == "__main__":
    main(sys.argv[1:])
```

Before running it, open `tools/xdk_sigmatch/water_replay.py` lines 1860-1890 and `out/native-evidence/water/ripples/after_analysis.py` and make the two calls (`_native_planes`, `all_colour_measure`) match their real signatures and return shapes; if `_native_planes` needs the frame's scale or exposure, take it as `after_analysis.py` does. Choose the box by looking at `game_native_details.png` of the ripple folder or shot 46 of the "before" run: a ground region below the lake, no water, no character.

- [ ] **Step 3: Run the three (announce each to `fable-2-recomp-62` first; no other `fable_2.exe` open; label the evidence `RUN UNCHECKED`).** Order: before, after, control. After each, find the new `native_discovery_*.jsonl` and `native_scene_*.f2ns` in the run's logs folder (the driver output names the log; the captures are the DISC marks at 45 s and 70 s) and run `ground_detail.py <run> <capture> <dump>` on the 45 s pair (and the 70 s pair as a second sample).

- [ ] **Step 4: Judge.** Acceptance 1: `aniso_after.ratio` in 0.8..1.2 (record `aniso_before.ratio` beside it). Acceptance 2: `aniso_control.ratio` in 0.8..1.2; if the first "after" pair is not conclusive (ratio under 0.8 while "before" is lower still), add a `run_before_control` (before exe, override 0) and compare the two control native planes' RMS difference (expected under 1%). Acceptance 3: the colour dict's ratios within the ripple fix's bounds (`after_metrics.json` of the ripple folder names them). Write the verdicts into `results.json` and the ledger. If acceptance 1 fails, stop and record why (box choice, a texture class on the level-0 path, or the derivative scale) before changing code; a code change goes through a test first.

---

### Task 10: Documents

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (a new section after section 20: "21. Texture filtering (sub-project 15)"), `docs/native-renderer/roadmap.md` (a "Sub-project 15" entry after the water-ripple section; its "Limits (15)"), `docs/native-renderer/user-checks.md` (one check: ground at a grazing angle in split view, before/after override 0), `README.md` (the native section: `anisotropic_override` now governs the native world passes and water)
- Create: `docs/native-renderer/aniso-filtering.md` (the closing report: cause, design, numbers of Task 9, limits)

- [ ] **Step 1: Frame-map section** with: the SDK rule quoted (Task 2's counts), the native rule (layouts per mode, bit 2, the five sites, what does not change), validation (Task 8 WARP, Task 9 numbers with run stamps, box, exe hashes), limits (impostor/quad/cloud layouts linear; min-point mipped textures approximated; a mode change rebuilds the pass; no per-draw instruction filter override).
- [ ] **Step 2: Roadmap, user check, README, report** as listed. The user check text: "Stand at the bridge facing along the path so the ground recedes at a low angle; split view; expect the native half's ground as sharp as the game's along the path; then `--anisotropic_override=0` and expect both halves equally soft."
- [ ] **Step 3: Commit**

```bash
git add docs/native-renderer/frame-map.md docs/native-renderer/roadmap.md docs/native-renderer/user-checks.md README.md docs/native-renderer/aniso-filtering.md
git commit -m "docs: texture filtering section, roadmap and report for sub-project 15"
```

---

### Task 11: Whole-branch review and local merge

- [ ] **Step 1:** Fresh whole-branch code review (superpowers:requesting-code-review) against the spec and this plan; fix findings with tests first; commit.
- [ ] **Step 2:** Rerun the full native suite and Python suite after the fixes; record in the ledger.
- [ ] **Step 3:** superpowers:finishing-a-development-branch: merge `native-renderer-aniso` into `master` locally (no push); tell the lanterns session the merge commit; note in the ledger that the main checkout's dirty presentation work and the user's wind edits are untouched.
