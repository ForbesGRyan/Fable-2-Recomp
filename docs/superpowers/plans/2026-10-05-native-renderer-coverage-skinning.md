# Native Renderer Coverage and Skinning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw the main-scene geometry the clay pass still skips or draws in the wrong pose (instanced meshes, weighted bone skinning, wind and displacement shaders without sway), textured where the albedo table already covers the pixel shader, reaching at least 90% of main-scene non-terrain draws.

**Architecture:** All per-vertex work stays on the CPU in the geometry cache. An instanced draw becomes a flat position (and UV) buffer indexed by the guest index, built as `rows(copy(i)) * mesh[vertex(i)] + offset`, so index buffers, draw calls and clay shaders do not change. Skinning extends the rigid path to 1-4 weighted bones. Every table entry is derived from the shader dump and accepted by an offline replay on captured bytes (`position_check.py`) before the runtime uses it.

**Tech Stack:** C++23 (clang-cl via CMake/Ninja), ReXGlue SDK (unchanged), nrhi D3D12 backend, Python 3 standard library (unittest).

**Spec:** `docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md`

## Global Constraints

- Environment: put `C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64\bin` FIRST on PATH, then `C:\Program Files\CMake\bin`, `%LOCALAPPDATA%\Microsoft\WinGet\Links`, `C:\Program Files\LLVM\bin` (`tests\run_native_tests.cmd` calls bare `clang++`).
- Game build: `& .\build.cmd -release fable_2` from PowerShell; no `fable_2.exe` may be running. The SDK is not changed by this plan.
- Python tests: `python -m unittest discover -s tests -p "<file>.py" -v`. Native tests: `& .\tests\run_native_tests.cmd` (add new tests before its final `exit /b 0`; the "broken_ps failed ... X3004" output is an expected negative test).
- Preserve each edited file's line endings (`core.autocrlf=true`).
- Git: branch `native-renderer-coverage`; stage files explicitly; never stage `fable_2_manifest.toml`; never `git add -A`; never push, stash, reset or switch branches. Every commit message ends with a blank line and these two lines exactly:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`
- Gameplay runs ONLY via `tools\drive_game.ps1` (in-process autoplay; the window may stay in the background; world fully up at ~50 s; the save loads the outdoor bridge scene). Never `-KeyboardInput`, never any other input method.
- Checks only the user can do are added to `docs/native-renderer/user-checks.md` under "Pending" with the exact command and the expected result; the plan never waits on them.
- Unit-tested headers marked "pure" must not include SDK, Windows or GPU headers.
- New skip reasons (exact strings): `instance-unsupported`, `skin-unsupported`.
- Evidence rule: a table entry is added only with (a) the shader-dump reading, instruction numbers included, and (b) the `position_check.py` result (capture name, draws sampled, in-clip share). Acceptance (amended 2026-10-05 after Task 4 measured the trusted baseline): the candidate's in-clip share must be at least the same capture's baseline (the share of the trusted shader `0xECD66A10092E6562`) minus 0.10, and never below 0.60; the original fixed 0.90 is not reachable because the game draws many off-screen objects (trusted shaders score 0.82-0.87, wrong entries 0.24-0.32). Skin entries must also pass the checker's structural metrics: bone orthonormality within tolerance under the entry's row swizzles, and edge stretch (skinned / bind-pose edge length) within [0.5, 2.0] for at least 98% of the sampled draws' edges. Exception path (amended 2026-10-05 after Task 5): a world-space shader whose meshes are spread around the camera may be accepted although the in-clip rule says REJECT, when the evidence records all of: its `c0..c3` equal to the trusted shader's view-projection in the same frames, where the failing draws are (outside the view), and an independent component-order check (the dump reading plus either the face-normal agreement of the candidate positions with the stored normals or a native-view overlay). Entries are `"manual": true`.
- Reusable evidence: capture `out\build\win-amd64-release\logs\native_discovery_20261002_194918.jsonl` (Bowerstone, per-VS counts in the spec; no stream dumps), shader dumps `out\shader_dump\shader_<HASH>.ucode.vert`.
- The emulated frame is never modified; with `fable2_native_view=off` records are not built and capture overhead stays under 0.5 ms per frame.
- No draw is drawn from guessed data: a table entry that does not match the decoded fetches skips the draw with `instance-unsupported` or `skin-unsupported`.

## Review Focus

1. Garbage instance constants (count 0, `inv_count` 0, NaN, a count that disagrees with `inv_count`, a huge first copy): the draw is `bad-index`; no allocation larger than the draw-count cap and no out-of-range read. Pinned in Task 1 (`InstanceBoundsOk`) and Task 6.
2. An index that maps to a copy past the instance stream or a vertex past the mesh: that vertex is NaN (culled); nothing is read out of range. Pinned in Task 1 (`ExpandInstances`).
3. A bone index past the palette on an influence whose weight is zero must not cull the vertex; with a nonzero weight it must. Pinned in Task 2.
4. All four weights zero (garbage vertex): the vertex is culled, not left at the origin. Pinned in Task 2.
5. A table entry whose row, index or weight fetch is not what the decoder finds (other format, other stream, missing fetch): `instance-unsupported` / `skin-unsupported`, never a draw from guessed data. Pinned in Task 1 (`SelectInstanceRows` rejects) and Task 2 (`SelectSkin` rejects).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/native/capture/instance_expand.h` (pure) | Instance index math, bounds, row selection, flat position and UV build | 1 |
| `src/native/capture/bone_skin.h` (pure, replaces `rigid_skin.h`) | Palette decode and 1-4 weighted bones | 2 |
| `tools/xdk_sigmatch/gen_transform_table.py`, `src/native/capture/vs_transform_table.inc`, table blocks in `capture.cpp` | `FABLE2_VS_INSTANCE`, extended `FABLE2_VS_SKIN` | 3 |
| `tools/xdk_sigmatch/position_check.py`, discovery stream dumps in `capture.cpp` | Offline replay and acceptance of entries | 4 |
| `docs/native-renderer/vs-transforms.json`, `docs/native-renderer/frame-map.md` (section 12) | Entries and evidence | 5, 7, 9 |
| `src/native/capture/draw_record.h`, `capture.cpp`, `src/native/render/clay_logic.h`, `geometry_cache.{h,cpp}`, `frame_scene.h` | Instancing runtime, skip reasons, stats | 6 |
| same files | Weighted skinning runtime | 8 |
| `docs/native-renderer/frame-map.md`, `docs/native-renderer/user-checks.md`, spec status | Validation | 10 |

---

### Task 1: Instance index math and flat expansion (pure)

**Files:**
- Create: `src/native/capture/instance_expand.h`, `tests/native/test_instance_expand.cpp`
- Modify: `tests/run_native_tests.cmd`

**Interfaces:**
- Consumes: `PosLayout`, `Float4`, `DecodePositions`, `SelectPosition` (position_decode.h, vfetch_decode.h), `Float2` (uv_decode.h).
- Produces (namespace `fable2::native::capture`): `kMaxInstanceCopies`; `struct InstanceSet {active, rows_addr, rows_size, rows[3], inv_count, count, first, bias, offset[3], flat_count}`; `struct InstanceSpec {row_fetch[3], row_swizzle[3], inv_count_ref, count_ref, first_ref, bias, offset_ref}`; `bool SelectInstanceRows(const std::vector<VertexFetch>&, const InstanceSpec&, uint32_t mesh_slot, InstanceSet*, uint32_t* rows_slot)`; `bool InstanceIndex(const InstanceSet&, uint32_t index, uint32_t* copy, uint32_t* vertex)`; `bool InstanceBoundsOk(const InstanceSet&, uint32_t max_index, uint32_t copies, uint32_t vertices)`; `uint32_t InstanceCopies(const InstanceSet&)`; `bool ExpandInstances(const Float4* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size, const InstanceSet&, uint32_t flat_count, Float4* out)`; `void ExpandInstanceUvs(const Float2* mesh_uv, uint32_t mesh_count, const InstanceSet&, uint32_t flat_count, Float2* out)`.

- [ ] **Step 1: Write the failing test**

Create `tests/native/test_instance_expand.cpp`:

```cpp
// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/instance_expand.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
static void PutBeF(uint8_t* p, float f) {
  uint32_t u; std::memcpy(&u, &f, 4);
  p[0] = uint8_t(u >> 24); p[1] = uint8_t(u >> 16); p[2] = uint8_t(u >> 8); p[3] = uint8_t(u);
}
static void PutRow(uint8_t* p, float a, float b, float c, float d) {
  PutBeF(p, a); PutBeF(p + 4, b); PutBeF(p + 8, c); PutBeF(p + 12, d);
}

int main() {
  // 4 vertices per copy, first copy 2, rounding bias 0.5 (shader 0x8123...:
  // copy = trunc((i + bias) * inv_count) + trunc(first), vertex = i - trunc(...) * count).
  InstanceSet s;
  s.active = true;
  s.inv_count = 0.25f; s.count = 4.0f; s.first = 2.0f; s.bias = 0.5f;
  uint32_t copy = 0, vertex = 0;
  if (!InstanceIndex(s, 0, &copy, &vertex) || copy != 2 || vertex != 0) return 1;
  if (!InstanceIndex(s, 3, &copy, &vertex) || copy != 2 || vertex != 3) return 2;
  if (!InstanceIndex(s, 4, &copy, &vertex) || copy != 3 || vertex != 0) return 3;
  if (!InstanceIndex(s, 5, &copy, &vertex) || copy != 3 || vertex != 1) return 4;
  // The bias matters when 1/count is not exact: 3 vertices per copy, inv 0.3333.
  InstanceSet t = s;
  t.inv_count = 0.3333f; t.count = 3.0f; t.first = 0.0f;
  if (!InstanceIndex(t, 3, &copy, &vertex) || copy != 1 || vertex != 0) return 5;
  if (!InstanceIndex(t, 2, &copy, &vertex) || copy != 0 || vertex != 2) return 6;
  // Garbage constants never produce a mapping.
  InstanceSet g = s;
  g.inv_count = std::nanf("");
  if (InstanceIndex(g, 1, &copy, &vertex)) return 7;
  g = s; g.first = 1e9f;
  if (InstanceIndex(g, 1, &copy, &vertex)) return 8;
  g = s; g.first = -3.0f;
  if (InstanceIndex(g, 1, &copy, &vertex)) return 9;

  // --- bounds (capture-side) ---
  if (!InstanceBoundsOk(s, 7, 4, 4)) return 10;        // copies 2..3 of 4, vertices 0..3 of 4
  if (InstanceBoundsOk(s, 8, 4, 4)) return 11;         // index 8 -> copy 4: past the stream
  if (InstanceBoundsOk(s, 7, 4, 3)) return 12;         // 4 vertices per copy, mesh has 3
  g = s; g.count = 0.0f;
  if (InstanceBoundsOk(g, 7, 4, 4)) return 13;
  g = s; g.inv_count = 0.5f;                           // disagrees with count 4
  if (InstanceBoundsOk(g, 7, 4, 4)) return 14;
  g = s; g.inv_count = 0.0f;
  if (InstanceBoundsOk(g, 7, 4, 4)) return 15;

  // --- expansion: float4 rows, 48 bytes per copy (rows at 0, 16, 32) ---
  for (int k = 0; k < 3; ++k) {
    s.rows[k].format = PosFormat::kFloat4;
    s.rows[k].stride_bytes = 48;
    s.rows[k].offset_bytes = uint32_t(16 * k);
  }
  s.offset[0] = 100.0f; s.offset[1] = 0.0f; s.offset[2] = 0.0f;
  std::vector<uint8_t> rows(4 * 48, 0);
  // Copy 2: translate x by 10. Copy 3: scale by 2.
  PutRow(&rows[2 * 48], 1, 0, 0, 10); PutRow(&rows[2 * 48 + 16], 0, 1, 0, 0); PutRow(&rows[2 * 48 + 32], 0, 0, 1, 0);
  PutRow(&rows[3 * 48], 2, 0, 0, 0); PutRow(&rows[3 * 48 + 16], 0, 2, 0, 0); PutRow(&rows[3 * 48 + 32], 0, 0, 2, 0);
  if (InstanceCopies(s) != 0) return 16;               // no stream attached yet
  s.rows_size = uint32_t(rows.size());
  if (InstanceCopies(s) != 4) return 17;
  const Float4 mesh[4] = {{1, 2, 3, 1}, {0, 1, 0, 1}, {5, 5, 5, 1}, {-1, 0, 2, 1}};
  std::vector<Float4> out(10);
  if (!ExpandInstances(mesh, 4, rows.data(), rows.size(), s, 10, out.data())) return 18;
  // i 0: copy 2, vertex 0 -> (1 + 10 + 100, 2, 3).
  if (!Near(out[0].x, 111) || !Near(out[0].y, 2) || !Near(out[0].z, 3) || !Near(out[0].w, 1)) return 19;
  // i 5: copy 3, vertex 1 -> (0 + 100, 2, 0).
  if (!Near(out[5].x, 100) || !Near(out[5].y, 2) || !Near(out[5].z, 0)) return 20;
  // i 8, 9: copy 4 is past the stream -> NaN (culled), no out-of-range read.
  if (!std::isnan(out[8].x) || !std::isnan(out[9].x)) return 21;
  // A mesh shorter than the vertices per copy: those vertices are NaN.
  if (!ExpandInstances(mesh, 3, rows.data(), rows.size(), s, 8, out.data())) return 22;
  if (!std::isnan(out[3].x) || std::isnan(out[2].x)) return 23;
  if (ExpandInstances(mesh, 4, rows.data(), rows.size(), s, 0, out.data())) return 24;  // nothing to build
  if (ExpandInstances(mesh, 4, nullptr, 0, s, 4, out.data())) return 25;

  // --- flat UVs follow the mesh vertex ---
  const Float2 uv[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  std::vector<Float2> fuv(10);
  ExpandInstanceUvs(uv, 4, s, 10, fuv.data());
  if (!Near(fuv[5].u, 1) || !Near(fuv[5].v, 0) || !Near(fuv[7].u, 1) || !Near(fuv[7].v, 1)) return 26;

  // --- row selection from the decoded fetches ---
  // Fetches: 0-2 instance rows (slot 94, stride 7 dwords, half4 at 0/2/4), 3 mesh position (slot 95).
  std::vector<VertexFetch> f(4);
  for (int k = 0; k < 3; ++k) {
    f[k].format = 32; f[k].fetch_slot = 94; f[k].stride_dwords = 7; f[k].offset_dwords = 2 * k;
    f[k].mini = k != 0;
  }
  f[3].format = 32; f[3].fetch_slot = 95; f[3].stride_dwords = 6;
  InstanceSpec spec{{0, 1, 2}, {0, 0, 0}, 48, 49, 50, 0.5f, 28};
  InstanceSet sel;
  uint32_t slot = 0;
  if (!SelectInstanceRows(f, spec, 95, &sel, &slot) || slot != 94) return 27;
  if (sel.rows[1].offset_bytes != 8 || sel.rows[2].stride_bytes != 28 || sel.bias != 0.5f) return 28;
  // A swizzle override replaces the row fetch's own.
  InstanceSpec swz = spec; swz.row_swizzle[1] = 0xAC1;
  if (!SelectInstanceRows(f, swz, 95, &sel, &slot) || sel.rows[1].swizzle != 0xAC1) return 29;
  // Rejections: rows in the mesh's own stream, a missing fetch, rows from two streams, a non-position format.
  if (SelectInstanceRows(f, spec, 94, &sel, &slot)) return 30;
  InstanceSpec bad = spec; bad.row_fetch[2] = 9;
  if (SelectInstanceRows(f, bad, 95, &sel, &slot)) return 31;
  std::vector<VertexFetch> two = f; two[2].fetch_slot = 93;
  if (SelectInstanceRows(two, spec, 95, &sel, &slot)) return 32;
  std::vector<VertexFetch> fmt = f; fmt[1].format = 6;
  if (SelectInstanceRows(fmt, spec, 95, &sel, &slot)) return 33;
  return 0;
}
```

Add to `tests/run_native_tests.cmd` before `exit /b 0`:

```bat
clang++ -std=c++23 "%~dp0native\test_instance_expand.cpp" -o "%OUT%\instance_expand.exe" || exit /b 1
"%OUT%\instance_expand.exe" || exit /b 1
```

- [ ] **Step 2: Run test to verify it fails**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile error, `instance_expand.h` not found.

- [ ] **Step 3: Write the implementation**

Create `src/native/capture/instance_expand.h`:

```cpp
#pragma once

// Instanced meshes (pure: no SDK/GPU deps). Fable 2's instancing shaders
// (0x8123C16DBF583F92 and relatives, frame-map section 12) draw many copies
// of a small mesh in one indexed draw. The guest index encodes both the copy
// and the mesh vertex:
//   t      = trunc((index + bias) * inv_count)
//   copy   = t + trunc(first)
//   vertex = index + trunc(-(count * t))
// Each copy has three rows in a per-instance stream; a mesh vertex p (w = 1)
// becomes (dot(row0, p), dot(row1, p), dot(row2, p)) + offset. These helpers
// build that as one flat stream indexed by the guest index, so the index
// buffer and the clay shaders are used unchanged.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "position_decode.h"
#include "uv_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

inline constexpr uint32_t kMaxInstanceCopies = 65536;

struct InstanceSet {
  bool active = false;
  uint32_t rows_addr = 0;  // per-instance stream, GPU physical
  uint32_t rows_size = 0;
  PosLayout rows[3];       // rows[k].stride_bytes = bytes per copy
  float inv_count = 0.0f, count = 0.0f, first = 0.0f, bias = 0.0f;
  float offset[3] = {};
  uint32_t flat_count = 0;  // positions to build: largest index + base vertex + 1
};

// vs-transforms.json "instance" (FABLE2_VS_INSTANCE): row fetches by their
// index in DecodeVertexFetches order, a destination swizzle override per row
// (0 = the fetch's own), vertex-constant references as register * 4 +
// component, the shader's literal rounding bias, and the offset's x reference
// (y and z follow).
struct InstanceSpec {
  int row_fetch[3];
  uint32_t row_swizzle[3];
  int32_t inv_count_ref, count_ref, first_ref;
  float bias;
  int32_t offset_ref;
};

// The three rows must be position-format fetches of one stream that is not
// the mesh's. Endians are applied by the caller from the stream's fetch
// constant (ApplyFetchEndian on each row). *rows_slot receives the stream's
// fetch slot; the constants are filled by the caller.
inline bool SelectInstanceRows(const std::vector<VertexFetch>& fetches, const InstanceSpec& spec,
                               uint32_t mesh_slot, InstanceSet* out, uint32_t* rows_slot) {
  InstanceSet s;
  for (int k = 0; k < 3; ++k) {
    if (spec.row_fetch[k] < 0 || size_t(spec.row_fetch[k]) >= fetches.size()) return false;
    if (!SelectPosition(fetches, spec.row_fetch[k], &s.rows[k], spec.row_swizzle[k])) return false;
    if (s.rows[k].fetch_slot != s.rows[0].fetch_slot || s.rows[k].stride_bytes != s.rows[0].stride_bytes) {
      return false;
    }
  }
  if (s.rows[0].fetch_slot == mesh_slot) return false;
  s.bias = spec.bias;
  s.active = true;
  *rows_slot = s.rows[0].fetch_slot;
  *out = s;
  return true;
}

inline bool InstanceIndex(const InstanceSet& s, uint32_t index, uint32_t* copy, uint32_t* vertex) {
  const float t = std::trunc((float(index) + s.bias) * s.inv_count);
  const float c = t + std::trunc(s.first);
  const float v = float(index) + std::trunc(-(s.count * t));
  if (!std::isfinite(c) || !std::isfinite(v) || c < 0.0f || v < 0.0f ||
      c >= float(kMaxInstanceCopies) || v >= 16777216.0f) {
    return false;
  }
  *copy = uint32_t(c);
  *vertex = uint32_t(v);
  return true;
}

// Whole copies in the attached stream.
inline uint32_t InstanceCopies(const InstanceSet& s) {
  uint32_t end = 0;  // bytes one copy's rows need
  for (const PosLayout& r : s.rows) {
    const uint32_t bytes = PositionBytes(r.format);
    if (bytes == 0 || r.stride_bytes == 0) return 0;
    if (r.offset_bytes + bytes > end) end = r.offset_bytes + bytes;
  }
  if (s.rows_size < end) return 0;
  return (s.rows_size - end) / s.rows[0].stride_bytes + 1;
}

// Capture-side check before a draw is recorded: sane constants, and every
// index up to max_index stays inside the copies and mesh vertices that exist.
inline bool InstanceBoundsOk(const InstanceSet& s, uint32_t max_index, uint32_t copies, uint32_t vertices) {
  if (!std::isfinite(s.count) || !std::isfinite(s.inv_count) || !std::isfinite(s.first)) return false;
  if (s.count < 1.0f || s.count != std::trunc(s.count) || s.first < 0.0f) return false;
  if (std::fabs(s.count * s.inv_count - 1.0f) > 0.01f) return false;
  if (uint64_t(s.count) > vertices) return false;
  uint32_t copy = 0, vertex = 0;
  if (!InstanceIndex(s, max_index, &copy, &vertex)) return false;  // copy grows with the index
  return copy < copies;
}

inline bool ExpandInstances(const Float4* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                            const InstanceSet& s, uint32_t flat_count, Float4* out) {
  if (!mesh || !rows || !out || flat_count == 0) return false;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<Float4> decoded;   // three rows per copy, decoded on first use
  std::vector<uint8_t> state;    // 0 unknown, 1 decoded, 2 past the stream
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = {nan, nan, nan, 1.0f};
    if (!InstanceIndex(s, i, &copy, &vertex) || vertex >= mesh_count) continue;
    if (copy >= state.size()) {
      state.resize(size_t(copy) + 1, 0);
      decoded.resize((size_t(copy) + 1) * 3);
    }
    if (state[copy] == 0) {
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) {
        ok = DecodePositions(rows, rows_size, s.rows[k], copy, 1, &decoded[size_t(copy) * 3 + k]);
      }
      state[copy] = ok ? 1 : 2;
    }
    if (state[copy] != 1) continue;
    const Float4* r = &decoded[size_t(copy) * 3];
    const Float4 p = mesh[vertex];
    auto dot = [&](const Float4& a) { return a.x * p.x + a.y * p.y + a.z * p.z + a.w * p.w; };
    out[i] = {dot(r[0]) + s.offset[0], dot(r[1]) + s.offset[1], dot(r[2]) + s.offset[2], 1.0f};
  }
  return true;
}

inline void ExpandInstanceUvs(const Float2* mesh_uv, uint32_t mesh_count, const InstanceSet& s,
                              uint32_t flat_count, Float2* out) {
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = (InstanceIndex(s, i, &copy, &vertex) && vertex < mesh_count) ? mesh_uv[vertex] : Float2{0.0f, 0.0f};
  }
}

}  // namespace fable2::native::capture
```

Check against the test: with the float4 test rows `DecodePositions` applies the default swizzle `0x688` (xyzw); row decode of copy 4 fails the bounds check inside `DecodePositions` (4 * 48 + 16 > 192), giving NaN at indices 8 and 9.

- [ ] **Step 4: Run tests to verify they pass**

Run: `& .\tests\run_native_tests.cmd`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/native/capture/instance_expand.h tests/native/test_instance_expand.cpp tests/run_native_tests.cmd
git commit -m "Native coverage: instance index math and flat expansion"
```

---

### Task 2: Weighted bone skinning (pure, replaces rigid_skin.h)

**Files:**
- Create: `src/native/capture/bone_skin.h` (via `git mv src/native/capture/rigid_skin.h src/native/capture/bone_skin.h`), `tests/native/test_bone_skin.cpp` (via `git mv tests/native/test_rigid_skin.cpp tests/native/test_bone_skin.cpp`)
- Modify: `src/native/capture/draw_record.h`, `src/native/capture/capture.cpp`, `src/native/render/clay_logic.h`, `src/native/render/geometry_cache.cpp`, `tests/run_native_tests.cmd`, any test that names `RigidSkin`

**Interfaces:**
- Produces: `struct BoneSkin {active, bones, index_offset_bytes, index_shift[4], index_endian, weighted, weight_offset_bytes, weight_shift[4], palette_addr, palette_size, bone_stride, rows[3]}`; `struct SkinSpec {index_fetch, weight_fetch, row_fetch[3], row_swizzle[3], bones, index_component[4], weight_component[4]}`; `bool SelectSkin(const std::vector<VertexFetch>&, const SkinSpec&, const PosLayout& pos, BoneSkin*, uint32_t* bone_slot)`; `bool SkinPositions(const uint8_t* vb, size_t vb_size, const uint8_t* palette, size_t palette_size, const BoneSkin&, uint32_t stride, uint32_t first_vertex, uint32_t count, Float4* positions)`.
- `weight_fetch = -1` is the rigid form: one bone, weight 1 (shader `0xA1F7...` keeps working). Weighted position: `M_k = sum_j(w_j * row_k(bone_j))`, `p' = (dot(M_0, p), dot(M_1, p), dot(M_2, p), 1)`, `w_j = weight byte / 255`.

- [ ] **Step 1: Rename and write the failing tests**

`git mv` both files; in `tests/run_native_tests.cmd` rename `test_rigid_skin.cpp`/`rigid_skin.exe` to `test_bone_skin.cpp`/`bone_skin.exe`. In the test, replace `RigidSkin` with `BoneSkin`, update the include, adapt every `SkinSpec` initialiser to the new field order (rigid: `{index_fetch, -1, {r0, r1, r2}, {0, 0, 0}, 1, {index_component, 0, 0, 0}, {0, 0, 0, 0}}`) and every `index_shift` use to `index_shift[0]`, keeping every existing assertion. Then add, before the final `return 0;` (uses the file's existing `PutBe32`, `PutHalf4`, `Near` helpers; pick return codes the file does not use yet):

```cpp
  // --- weighted blend: two bones at 50/50, the other two influences zero ---
  {
    const uint32_t wstride = 28;
    std::vector<uint8_t> wvb(2 * wstride, 0);
    PutHalf4(&wvb[0], 2, 0, 0, 99);
    // Index word (8in32, big-endian in memory): x = bone 0, y = bone 1, z = 9 (past the palette), w = 9.
    PutBe32(&wvb[12], 0x09090100);
    // Weight word: x = 128, y = 127, z = 0, w = 0  -> 128/255 and 127/255.
    PutBe32(&wvb[16], 0x00007F80);
    // Vertex 1: all weights zero.
    PutHalf4(&wvb[wstride], 1, 1, 1, 99);
    PutBe32(&wvb[wstride + 12], 0x00000000);
    PutBe32(&wvb[wstride + 16], 0x00000000);
    std::vector<uint8_t> wpal(2 * 24, 0);
    // Bone 0: identity. Bone 1: x' = x + 10.
    PutHalf4(&wpal[0], 1, 0, 0, 0); PutHalf4(&wpal[8], 0, 1, 0, 0); PutHalf4(&wpal[16], 0, 0, 1, 0);
    PutHalf4(&wpal[24], 1, 0, 0, 10); PutHalf4(&wpal[32], 0, 1, 0, 0); PutHalf4(&wpal[40], 0, 0, 1, 0);
    PosLayout wpos;
    wpos.format = PosFormat::kHalf4; wpos.stride_bytes = wstride; wpos.swizzle = 0xAC1; wpos.swap16 = true;
    BoneSkin w;
    w.active = true; w.bones = 4; w.weighted = true;
    w.index_offset_bytes = 12; w.index_endian = 2;
    w.weight_offset_bytes = 16;
    for (uint32_t k = 0; k < 4; ++k) { w.index_shift[k] = 8 * k; w.weight_shift[k] = 8 * k; }
    w.bone_stride = 24;
    for (int k = 0; k < 3; ++k) {
      w.rows[k].format = PosFormat::kHalf4; w.rows[k].stride_bytes = 24; w.rows[k].offset_bytes = uint32_t(8 * k);
      w.rows[k].swizzle = 0x688; w.rows[k].swap16 = false;
    }
    Float4 wp[2];
    if (!DecodePositions(wvb.data(), wvb.size(), wpos, 0, 2, wp)) return 60;
    if (!SkinPositions(wvb.data(), wvb.size(), wpal.data(), wpal.size(), w, wstride, 0, 2, wp)) return 61;
    // x' = 2 + 10 * 127/255; the zero-weight influences name bone 9 (past the palette) and must not cull.
    if (!Near(wp[0].x, 2.0f + 10.0f * 127.0f / 255.0f) || !Near(wp[0].y, 0) || !Near(wp[0].w, 1)) return 62;
    // All weights zero: culled (NaN), not left at the origin.
    if (!std::isnan(wp[1].x)) return 63;
    // A nonzero weight on a bone past the palette culls the vertex.
    PutBe32(&wvb[16], 0x00017F80);  // z weight 1 -> bone 9
    if (!DecodePositions(wvb.data(), wvb.size(), wpos, 0, 1, wp)) return 64;
    if (!SkinPositions(wvb.data(), wvb.size(), wpal.data(), wpal.size(), w, wstride, 0, 1, wp)) return 65;
    if (!std::isnan(wp[0].x)) return 66;
  }
  // --- SelectSkin with a weight fetch and pairs ---
  {
    std::vector<VertexFetch> sf(6);
    sf[0].format = 32; sf[0].fetch_slot = 95; sf[0].stride_dwords = 7;                       // position
    sf[1].format = 6; sf[1].fetch_slot = 95; sf[1].stride_dwords = 7; sf[1].offset_dwords = 3;
    sf[1].normalized = false; sf[1].mini = true; sf[1].dst_swizzle = 0x688;                  // indices
    sf[2].format = 6; sf[2].fetch_slot = 95; sf[2].stride_dwords = 7; sf[2].offset_dwords = 4;
    sf[2].normalized = true; sf[2].mini = true; sf[2].dst_swizzle = 0x60A;                   // weights .zyxw
    for (int k = 0; k < 3; ++k) {
      sf[3 + k].format = 32; sf[3 + k].fetch_slot = 92; sf[3 + k].stride_dwords = 6; sf[3 + k].offset_dwords = 2 * k;
    }
    PosLayout sp;
    sp.format = PosFormat::kHalf4; sp.stride_bytes = 28; sp.fetch_slot = 95;
    // Pairs (index component, weight component): (x, z), (y, y), (z, x), (w, w).
    SkinSpec spec{1, 2, {3, 4, 5}, {0, 0, 0}, 4, {0, 1, 2, 3}, {2, 1, 0, 3}};
    BoneSkin sel;
    uint32_t slot = 0;
    if (!SelectSkin(sf, spec, sp, &sel, &slot) || slot != 92 || !sel.weighted || sel.bones != 4) return 67;
    // Weight register component z reads source x (swizzle .zyxw): shift 0; component x reads source z: shift 16.
    if (sel.weight_shift[0] != 0 || sel.weight_shift[2] != 16 || sel.weight_offset_bytes != 16) return 68;
    if (sel.index_shift[1] != 8 || sel.index_offset_bytes != 12) return 69;
    // Rejections: weights not normalized 8_8_8_8, weights in another stream, zero or five bones.
    std::vector<VertexFetch> bad = sf; bad[2].normalized = false;
    if (SelectSkin(bad, spec, sp, &sel, &slot)) return 70;
    bad = sf; bad[2].fetch_slot = 94;
    if (SelectSkin(bad, spec, sp, &sel, &slot)) return 71;
    SkinSpec none = spec; none.bones = 0;
    if (SelectSkin(sf, none, sp, &sel, &slot)) return 72;
    SkinSpec five = spec; five.bones = 5;
    if (SelectSkin(sf, five, sp, &sel, &slot)) return 73;
  }
```

(`0x60A` is the destination swizzle `zyxw`: x<-z = 2, y<-y = 1 << 3, z<-x = 0 << 6, w<-w = 3 << 9.)

- [ ] **Step 2: Run tests to verify they fail**

Run: `& .\tests\run_native_tests.cmd`
Expected: compile errors (`BoneSkin` undeclared).

- [ ] **Step 3: Implement `bone_skin.h`**

Rewrite the header (keep the existing explanatory comments where still true; replace the "bone also carries the object placement" sentence with: "the bones are near identity in gameplay; the object placement is in c0..c3 (frame-map section 9)"):

```cpp
struct BoneSkin {
  bool active = false;
  uint32_t bones = 1;  // influences per vertex, 1..4
  // Bone indices: 8_8_8_8 components of the 32-bit word at this byte offset in
  // the position stream's vertex (big-endian word under fetch endian 8in32,
  // little-endian under none); index_shift[k] selects influence k's byte.
  uint32_t index_offset_bytes = 0;
  uint32_t index_shift[4] = {};
  uint32_t index_endian = 0;
  // Weights: normalized 8_8_8_8 word at weight_offset_bytes, same endian;
  // weighted == false means one bone with weight 1.
  bool weighted = false;
  uint32_t weight_offset_bytes = 0;
  uint32_t weight_shift[4] = {};
  uint32_t palette_addr = 0;
  uint32_t palette_size = 0;
  uint32_t bone_stride = 0;
  PosLayout rows[3];
};

// vs-transforms.json "skin" (FABLE2_VS_SKIN). Fetches by DecodeVertexFetches
// index. Influence k uses register component index_component[k] of the index
// fetch and weight_component[k] of the weight fetch; weight_fetch -1 = rigid.
// row_swizzle[k] (0 = the fetch's own) replaces row k's destination swizzle.
struct SkinSpec {
  int index_fetch;
  int weight_fetch;
  int row_fetch[3];
  uint32_t row_swizzle[3];
  uint32_t bones;
  uint32_t index_component[4];
  uint32_t weight_component[4];
};

inline bool SelectSkin(const std::vector<VertexFetch>& fetches, const SkinSpec& spec, const PosLayout& pos,
                       BoneSkin* out, uint32_t* bone_slot) {
  auto valid = [&](int i) { return i >= 0 && size_t(i) < fetches.size(); };
  if (!valid(spec.index_fetch) || spec.bones < 1 || spec.bones > 4) return false;
  const bool weighted = spec.weight_fetch >= 0;
  if (!weighted && spec.bones != 1) return false;
  // A per-vertex 8_8_8_8 word of the position's stream; returns its byte
  // offset and the shift of register component `comp`.
  auto word = [&](const VertexFetch& f, bool normalized, uint32_t comp, uint32_t* offset, uint32_t* shift) {
    if (f.fetch_slot != pos.fetch_slot || f.stride_dwords * 4 != pos.stride_bytes) return false;
    if (f.format != 6 || f.normalized != normalized || f.offset_dwords < 0 || comp > 3) return false;
    const uint32_t src = (f.dst_swizzle >> (3 * comp)) & 7;
    if (src > 3) return false;  // component not fetched
    *offset = uint32_t(f.offset_dwords) * 4;
    *shift = 8 * src;
    return true;
  };
  BoneSkin s;
  s.bones = spec.bones;
  s.weighted = weighted;
  for (uint32_t k = 0; k < spec.bones; ++k) {
    if (!word(fetches[size_t(spec.index_fetch)], false, spec.index_component[k], &s.index_offset_bytes,
              &s.index_shift[k])) {
      return false;
    }
    if (weighted && (!valid(spec.weight_fetch) ||
                     !word(fetches[size_t(spec.weight_fetch)], true, spec.weight_component[k],
                           &s.weight_offset_bytes, &s.weight_shift[k]))) {
      return false;
    }
  }
  for (int k = 0; k < 3; ++k) {
    if (!valid(spec.row_fetch[k])) return false;
    if (!SelectPosition(fetches, spec.row_fetch[k], &s.rows[k], spec.row_swizzle[k])) return false;
    if (s.rows[k].fetch_slot != s.rows[0].fetch_slot || s.rows[k].stride_bytes != s.rows[0].stride_bytes) {
      return false;
    }
  }
  s.bone_stride = s.rows[0].stride_bytes;
  s.active = true;
  *bone_slot = s.rows[0].fetch_slot;
  *out = s;
  return true;
}

namespace detail {
// The 32-bit word at `at` as the GPU reads it, or false if outside the stream
// or the endian is not handled.
inline bool SkinWord(const uint8_t* vb, size_t vb_size, uint64_t at, uint32_t endian, uint32_t* out) {
  if (!vb || at + 4 > vb_size) return false;
  const uint8_t* p = vb + at;
  if (endian == 2) {
    *out = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  } else if (endian == 0) {
    *out = (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
  } else {
    return false;
  }
  return true;
}
}  // namespace detail

inline bool SkinPositions(const uint8_t* vb, size_t vb_size, const uint8_t* palette, size_t palette_size,
                          const BoneSkin& s, uint32_t stride, uint32_t first_vertex, uint32_t count,
                          Float4* positions) {
  if (!s.active || stride == 0 || s.bone_stride == 0 || s.bones < 1 || s.bones > 4) return false;
  std::vector<Float4> bones;  // three rows per bone; the index is 8 bits
  for (uint32_t b = 0; b < 256; ++b) {
    Float4 r[3];
    bool ok = true;
    for (int k = 0; k < 3 && ok; ++k) ok = DecodePositions(palette, palette_size, s.rows[k], b, 1, &r[k]);
    if (!ok) break;
    bones.insert(bones.end(), {r[0], r[1], r[2]});
  }
  if (bones.empty()) return false;
  const uint32_t n = uint32_t(bones.size() / 3);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = uint64_t(first_vertex + i) * stride;
    uint32_t iw = 0, ww = 0;
    if (!detail::SkinWord(vb, vb_size, at + s.index_offset_bytes, s.index_endian, &iw)) return false;
    if (s.weighted && !detail::SkinWord(vb, vb_size, at + s.weight_offset_bytes, s.index_endian, &ww)) return false;
    float m[3][4] = {};
    bool any = false, bad = false;
    for (uint32_t k = 0; k < s.bones; ++k) {
      const float w = s.weighted ? float((ww >> s.weight_shift[k]) & 0xFF) / 255.0f : 1.0f;
      if (w == 0.0f) continue;  // an unused influence may name any bone
      const uint32_t bone = (iw >> s.index_shift[k]) & 0xFF;
      if (bone >= n) { bad = true; break; }
      const Float4* r = &bones[size_t(bone) * 3];
      for (int row = 0; row < 3; ++row) {
        m[row][0] += w * r[row].x; m[row][1] += w * r[row].y; m[row][2] += w * r[row].z; m[row][3] += w * r[row].w;
      }
      any = true;
    }
    if (bad || !any) {
      positions[i] = {nan, nan, nan, 1.0f};
      continue;
    }
    const Float4 p = positions[i];
    auto dot = [&](const float* a) { return a[0] * p.x + a[1] * p.y + a[2] * p.z + a[3] * p.w; };
    positions[i] = {dot(m[0]), dot(m[1]), dot(m[2]), 1.0f};
  }
  return true;
}
```

Remove `BoneIndex` if nothing else uses it (grep); otherwise keep it as a thin wrapper over `SkinWord` for influence 0.

- [ ] **Step 4: Update the users (no behaviour change)**

- `draw_record.h`: include `bone_skin.h`; `DrawRecord::skin` and `DrawInputs::skin` become `BoneSkin`.
- `capture.cpp`: `VsInfo::skin` type; the `kSkinTable` block maps the existing macro `FABLE2_VS_SKIN(H, I, C, R0, R1, R2)` to `{H, {I, -1, {R0, R1, R2}, {0, 0, 0}, 1, {C, 0, 0, 0}, {0, 0, 0, 0}}}` (Task 3 changes the macro's shape); where the code set `s.index_endian` keep it (weights use the same endian).
- `clay_logic.h` `PositionKey`: hash every `BoneSkin` field that changes the decode: `bones`, `index_offset_bytes`, the four `index_shift`, `index_endian`, `uint32_t(weighted)`, `weight_offset_bytes`, the four `weight_shift`, `palette_addr`, `palette_size`, `bone_stride`, the three row layout hashes. Update `tests/native/test_clay_logic.cpp` if it names `RigidSkin`/`index_shift`, and add one assertion: two records that differ only in `skin.weight_shift[1]` have different `PositionKey`s.
- `geometry_cache.cpp`: type name only.

- [ ] **Step 5: Run tests and build**

Run: `& .\tests\run_native_tests.cmd` (exit 0) and `& .\build.cmd -release fable_2` (exit 0).

- [ ] **Step 6: Commit**

```bash
git add src/native/capture/bone_skin.h src/native/capture/draw_record.h src/native/capture/capture.cpp src/native/render/clay_logic.h src/native/render/geometry_cache.cpp tests/native/test_bone_skin.cpp tests/native/test_clay_logic.cpp tests/run_native_tests.cmd
git commit -m "Native coverage: weighted bone skinning (1-4 bones) replaces rigid skin"
```

(`git mv` already staged the renames; `git status` must show `renamed:` for both files.)

---

### Task 3: Table generator and table blocks for instancing and weighted skin

**Files:**
- Modify: `tools/xdk_sigmatch/gen_transform_table.py`, `tests/test_gen_transform_table.py`, `src/native/capture/vs_transform_table.inc` (regenerated), `src/native/capture/capture.cpp`

**Interfaces:**
- JSON (`docs/native-renderer/vs-transforms.json` entry):
  - `"instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "row_swizzles": ["xyzw", "xyzw", "xyzw"] (optional), "inv_count": "c12.x", "count": "c12.y", "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}`. `mesh_fetch` becomes the transform line's `pos_fetch` (an entry that also has a different `"pos_fetch"` is an error).
  - `"skin"`: the existing rigid form `{"index_fetch", "index_component", "row_fetches"}` stays valid; the weighted form is `{"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5], "row_swizzles": [...] (optional), "pairs": [["x", "z"], ["y", "y"], ["z", "x"], ["w", "w"]]}` (1 to 4 pairs of index component, weight component).
- Generated macros:
  - `FABLE2_VS_SKIN(hash, index_fetch, weight_fetch, r0, r1, r2, s0, s1, s2, bones, ic0, wc0, ic1, wc1, ic2, wc2, ic3, wc3)` (swizzles as hex, `0x0` = keep; unused pairs 0).
  - `FABLE2_VS_INSTANCE(hash, r0, r1, r2, s0, s1, s2, inv_count_ref, count_ref, first_ref, bias, offset_ref)` (refs = register * 4 + component; `bias` printed as a C float literal such as `0.5f`).
- capture.cpp: `kSkinTable` built from the new macro shape; new `const std::vector<std::pair<uint64_t, InstanceSpec>> kInstanceTable` and `const InstanceSpec* FindInstance(uint64_t hash)`; every table block defines and undefines `FABLE2_VS_INSTANCE`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_gen_transform_table.py`, update the existing skin expectation to the new macro shape and add:

```python
    def test_skin_rigid_form(self):
        data = {"0xA1F7E9885EC466DF": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}}}
        out = gen_transform_table.generate(data).splitlines()
        self.assertIn("FABLE2_VS_SKIN(0xA1F7E9885EC466DFull, 2, -1, 6, 7, 8, 0x0, 0x0, 0x0, 1, 2, 0, 0, 0, 0, 0, 0, 0)", out)

    def test_skin_weighted_form(self):
        data = {"0xD4D558DA6A82BDC8": {"base": 0, "layout": "dot", "pos_fetch": -1,
                                       "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                                "row_swizzles": ["xyzw", "xzyw", "yxzw"],
                                                "pairs": [["x", "z"], ["y", "y"], ["z", "x"], ["w", "w"]]}}}
        out = gen_transform_table.generate(data).splitlines()
        self.assertIn("FABLE2_VS_SKIN(0xD4D558DA6A82BDC8ull, 1, 2, 3, 4, 5, 0x688, 0x650, 0x681, 4, "
                      "0, 2, 1, 1, 2, 0, 3, 3)", out)

    def test_skin_rejects_bad_pairs(self):
        for pairs in ([], [["x", "x"]] * 5, [["q", "x"]]):
            with self.assertRaises(ValueError):
                gen_transform_table.generate({"0x1": {"base": 0, "layout": "dot",
                                                      "skin": {"index_fetch": 1, "weight_fetch": 2,
                                                               "row_fetches": [3, 4, 5], "pairs": pairs}}})

    def test_instance(self):
        data = {"0x8123C16DBF583F92": {"base": 0, "layout": "dot",
                                       "instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x",
                                                    "count": "c12.y", "first": "c12.z", "bias": 0.5,
                                                    "offset": "c7.xyz"}}}
        out = gen_transform_table.generate(data).splitlines()
        self.assertIn("FABLE2_VS_TRANSFORM(0x8123C16DBF583F92ull, 0, 0, 4, 0)", out)
        self.assertIn("FABLE2_VS_INSTANCE(0x8123C16DBF583F92ull, 0, 1, 2, 0x0, 0x0, 0x0, 48, 49, 50, 0.5f, 28)", out)

    def test_instance_rejects_conflicts(self):
        base = {"base": 0, "layout": "dot",
                "instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "inv_count": "c12.x", "count": "c12.y",
                             "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}
        with self.assertRaises(ValueError):
            gen_transform_table.generate({"0x1": dict(base, pos_fetch=2)})       # disagrees with mesh_fetch
        bad = dict(base); bad["instance"] = dict(base["instance"], offset="c7.yzw")
        with self.assertRaises(ValueError):
            gen_transform_table.generate({"0x1": bad})                           # offset must be .xyz
        bad = dict(base); bad["instance"] = dict(base["instance"], row_fetches=[0, 1])
        with self.assertRaises(ValueError):
            gen_transform_table.generate({"0x1": bad})
```

- [ ] **Step 2: Run to verify they fail**

Run: `python -m unittest discover -s tests -p "test_gen_transform_table.py" -v`
Expected: the new tests fail (old macro shape, no instance lines).

- [ ] **Step 3: Implement the generator changes**

In `gen_transform_table.py` (document both macros in the docstring):

```python
def _swizzles(items):
    items = list(items or ["", "", ""])
    if len(items) != 3:
        raise ValueError("three row swizzles are required")
    return ["0x%X" % (swizzle_bits(s) if s else 0) for s in items]


def _skin_line(vs, s):
    rows = s["row_fetches"]
    if len(rows) != 3:
        raise ValueError(f"{vs}: skin needs three row fetches")
    if "weight_fetch" in s:
        pairs = s["pairs"]
        if not 1 <= len(pairs) <= 4:
            raise ValueError(f"{vs}: skin needs 1 to 4 (index, weight) pairs")
        comps = [(_COMP.index(i), _COMP.index(w)) for i, w in pairs]
        weight_fetch = int(s["weight_fetch"])
    else:
        comps = [(_COMP.index(s["index_component"]), 0)]
        weight_fetch = -1
    flat = [c for pair in comps + [(0, 0)] * (4 - len(comps)) for c in pair]
    fields = [int(s["index_fetch"]), weight_fetch, *rows, *_swizzles(s.get("row_swizzles")), len(comps), *flat]
    return f"FABLE2_VS_SKIN({vs}ull, {', '.join(str(f) for f in fields)})"


def _instance_line(vs, e):
    inst = e["instance"]
    rows = inst["row_fetches"]
    if len(rows) != 3:
        raise ValueError(f"{vs}: instance needs three row fetches")
    m = re.fullmatch(r"c(\d+)\.xyz", inst["offset"])
    if not m or int(m.group(1)) > 255:
        raise ValueError(f"{vs}: instance offset must be c<N>.xyz")
    fields = [*rows, *_swizzles(inst.get("row_swizzles")), reg_comp(inst["inv_count"]), reg_comp(inst["count"]),
              reg_comp(inst["first"]), repr(float(inst["bias"])) + "f", int(m.group(1)) * 4]
    return f"FABLE2_VS_INSTANCE({vs}ull, {', '.join(str(f) for f in fields)})"
```

`_COMP.index` raises `ValueError` for a bad component, which the tests expect. In `generate`: the transform line's `pos_fetch` is `e["instance"]["mesh_fetch"]` when the entry has `"instance"` (raise `ValueError` if `"pos_fetch"` is present and different); replace the inline skin code with `extras.append(_skin_line(vs, e["skin"]))`; append `_instance_line(vs, e)` for instance entries.

- [ ] **Step 4: Regenerate and update the table blocks**

```
python tools\xdk_sigmatch\gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc
```

Expected diff of the `.inc`: only the `FABLE2_VS_SKIN` line of `0xA1F7E9885EC466DF` changes shape. In `capture.cpp`: every table block gets `#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF)` (empty) and its `#undef`; every `FABLE2_VS_SKIN` define takes the 18 parameters; the skin block expands to `{H, {I, W, {R0, R1, R2}, {S0, S1, S2}, N, {IC0, IC1, IC2, IC3}, {WC0, WC1, WC2, WC3}}},`; add the instance block expanding to `{H, {{R0, R1, R2}, {S0, S1, S2}, INV, CNT, FIRST, BIAS, OFF}},` into `kInstanceTable` and `FindInstance` (linear scan, like `FindSkin`). `#include "instance_expand.h"`.

- [ ] **Step 5: Run tests and build**

`python -m unittest discover -s tests -p "test_*.py"`, `& .\tests\run_native_tests.cmd`, `& .\build.cmd -release fable_2`: all green. One gameplay smoke run (`.\tools\drive_game.ps1 -Total 100 -Shots "90" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`): the `[native] capture:` breakdown still shows `0xA1F7E9885EC466DF(skin)` among the drawable shaders with the same count as before the change (compare with `fable_2_149.log`: record both numbers in the report).

- [ ] **Step 6: Commit**

```bash
git add tools/xdk_sigmatch/gen_transform_table.py tests/test_gen_transform_table.py src/native/capture/vs_transform_table.inc src/native/capture/capture.cpp
git commit -m "Native coverage: table generator and blocks for instancing and weighted skin"
```

---

### Task 4: Discovery stream dumps and the offline position checker

**Files:**
- Create: `tools/xdk_sigmatch/position_check.py`, `tests/test_position_check.py`
- Modify: `src/native/capture/capture.cpp` (discovery rows only)

**Interfaces:**
- Discovery draw rows (in-scene only) gain:
  - `"fetches": [{"i": ordinal, "slot": n, "fmt": xenos format, "off": dwords, "stride": dwords, "mini": bool, "signed": bool, "norm": bool, "exp": n, "swz": dst swizzle}]` — the `DecodeVertexFetches` list of the draw's vertex shader;
  - `"streams": [{"slot": n, "phys": addr, "size": bytes, "stride": bytes, "endian": fc1 & 3, "file": "<PHYS8>_<SIZE8>.bin"}]` — one per distinct fetch slot the shader reads, each dumped once per capture to `<exe folder>\logs\native_geo_<stamp>\` (at most 1 MB per stream, 4096 files, 512 MB in total; `"file"` is omitted when a cap is hit);
  - `"idx": [first 512 indices of the draw, after the index endian swap, base vertex not added]` and `"base_vertex"`;
  - `"vconst": [64 floats]` — vertex constants `c0..c15` in host order (the existing `"bank"` stays as is).
- `position_check.py <capture.jsonl> --json docs\native-renderer\vs-transforms.json [--entry <file.json>] [--vs <HASH>] [--max-draws 200] [--cpp-fixture <HASH>]`:
  - `--entry` overlays candidate entries (same shape as `vs-transforms.json`) for iteration without touching the table;
  - per vertex shader with an entry: builds positions for the draw's first 512 indices in Python (plain: position fetch + swizzle; instanced: the Task 1 math; skinned: the Task 2 math), projects with the draw's own `c0..c3` (dot or combine layout), and prints `VS <hash> <kind>: draws N, in-clip share S (accept >= 0.90), ...` plus, for skins, `weight sum min/median/max` and `bone index max / palette bones`, and, for instancing, `copies max / available` and `vertices per copy`;
  - a vertex is inside the clip volume when `w > 0`, `|x| <= w`, `|y| <= w`, `0 <= z <= w`; a draw passes when at least half of its sampled vertices are inside (NaN vertices count as outside);
  - `--cpp-fixture` prints, for one sampled draw of that shader, C++ arrays (stream bytes actually used, constants, the first 16 expected positions) to paste into a native regression test.
- Exit code 0 always (the report is the result); a row whose stream file is missing is counted as `unreadable`.

- [ ] **Step 1: Write the failing Python tests**

Create `tests/test_position_check.py` with synthetic captures written to a temp directory (JSONL rows plus `.bin` stream files), one test per kind:

```python
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "xdk_sigmatch"))

import position_check as pc  # noqa: E402

IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0, 0, 0, 1]  # clip = (x, y, 0.5, 1): in clip when |x|,|y| <= 1


def be_floats(*v):
    return b"".join(struct.pack(">f", x) for x in v)


def fetch(i, slot, fmt, off, stride, swz=0x688, norm=True, signed=False, mini=False):
    return {"i": i, "slot": slot, "fmt": fmt, "off": off, "stride": stride, "mini": mini, "signed": signed,
            "norm": norm, "exp": 0, "swz": swz}


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "native_geo_x").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def stream(self, slot, phys, data, stride, endian=2):
        name = f"{phys:08X}_{len(data):08X}.bin"
        (self.dir / "native_geo_x" / name).write_bytes(data)
        return {"slot": slot, "phys": phys, "size": len(data), "stride": stride, "endian": endian,
                "file": f"native_geo_x/{name}"}

    def capture(self, rows):
        p = self.dir / "cap.jsonl"
        p.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
        return p

    def row(self, vs, fetches, streams, idx, vconst=None):
        v = list(IDENTITY) + [0.0] * 48 if vconst is None else vconst
        return {"kind": "draw", "in_scene": True, "vs_hash": vs, "fetches": fetches, "streams": streams,
                "idx": idx, "base_vertex": 0, "vconst": v}


class PlainTest(Base):
    def test_plain_float4_in_and_out_of_clip(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.5, 0.5, 0, 1) + be_floats(9, 9, 0, 1)
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)], [self.stream(95, 0x1000, mesh, 16)], [0, 1, 2])]
        entry = {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}}
        rep = pc.check(self.capture(rows), entry)
        self.assertEqual(rep["0xAAAA"]["kind"], "plain")
        self.assertEqual(rep["0xAAAA"]["draws"], 1)
        self.assertEqual(rep["0xAAAA"]["passed"], 1)          # 2 of 3 vertices inside
        self.assertAlmostEqual(rep["0xAAAA"]["share"], 1.0)


class InstanceTest(Base):
    def test_instanced_rows_and_offset(self):
        mesh = be_floats(0, 0, 0, 1) + be_floats(0.25, 0, 0, 1)                      # 2 vertices per copy
        inst = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        vconst = list(IDENTITY) + [0.0] * 48
        vconst[12 * 4:12 * 4 + 4] = [0.5, 2.0, 0.0, 0.0]                             # c12 = (1/2, 2, first 0)
        vconst[7 * 4:7 * 4 + 3] = [0.1, 0.0, 0.0]                                    # c7 offset
        fetches = [fetch(0, 94, 38, 0, 12), fetch(1, 94, 38, 4, 12, mini=True), fetch(2, 94, 38, 8, 12, mini=True),
                   fetch(3, 95, 38, 0, 4)]
        rows = [self.row("0xBBBB", fetches, [self.stream(94, 0x2000, inst, 48), self.stream(95, 0x3000, mesh, 16)],
                         [0, 1, 2, 3], vconst)]
        entry = {"0xBBBB": {"base": 0, "layout": "dot",
                            "instance": {"mesh_fetch": 3, "row_fetches": [0, 1, 2], "inv_count": "c12.x",
                                         "count": "c12.y", "first": "c12.z", "bias": 0.5, "offset": "c7.xyz"}}}
        rep = pc.check(self.capture(rows), entry)
        r = rep["0xBBBB"]
        self.assertEqual((r["kind"], r["draws"], r["passed"]), ("instance", 1, 1))
        self.assertEqual(r["copies_max"], 1)
        # index 3 -> copy 1, vertex 1 -> x = 0.25 + 0.5 + 0.1
        self.assertAlmostEqual(pc.positions_for(rows[0], entry["0xBBBB"], self.dir)[3][0], 0.85, places=5)


class SkinTest(Base):
    def test_weighted_two_bones(self):
        # Vertex: float4 position (16 bytes), index word, weight word (8in32: big-endian words).
        vb = be_floats(0.2, 0, 0, 1) + struct.pack(">I", 0x00000100) + struct.pack(">I", 0x00007F80)
        pal = b"".join(be_floats(1, 0, 0, dx) + be_floats(0, 1, 0, 0) + be_floats(0, 0, 1, 0) for dx in (0.0, 0.5))
        fetches = [fetch(0, 95, 38, 0, 6), fetch(1, 95, 6, 4, 6, norm=False, mini=True),
                   fetch(2, 95, 6, 5, 6, norm=True, mini=True),
                   fetch(3, 92, 38, 0, 12), fetch(4, 92, 38, 4, 12, mini=True), fetch(5, 92, 38, 8, 12, mini=True)]
        rows = [self.row("0xCCCC", fetches, [self.stream(95, 0x4000, vb, 24), self.stream(92, 0x5000, pal, 48)], [0])]
        entry = {"0xCCCC": {"base": 0, "layout": "dot", "pos_fetch": -1,
                            "skin": {"index_fetch": 1, "weight_fetch": 2, "row_fetches": [3, 4, 5],
                                     "pairs": [["x", "x"], ["y", "y"]]}}}
        rep = pc.check(self.capture(rows), entry)
        r = rep["0xCCCC"]
        self.assertEqual((r["kind"], r["draws"], r["passed"]), ("skin", 1, 1))
        self.assertAlmostEqual(r["weight_sum_median"], 1.0, places=2)
        self.assertEqual(r["bone_max"], 1)
        self.assertEqual(r["palette_bones"], 2)
        self.assertAlmostEqual(pc.positions_for(rows[0], entry["0xCCCC"], self.dir)[0][0],
                               0.2 + 0.5 * 127 / 255, places=4)

    def test_missing_stream_file_is_unreadable(self):
        rows = [self.row("0xAAAA", [fetch(0, 95, 38, 0, 4)],
                         [{"slot": 95, "phys": 1, "size": 16, "stride": 16, "endian": 2}], [0])]
        rep = pc.check(self.capture(rows), {"0xAAAA": {"base": 0, "layout": "dot", "pos_fetch": -1}})
        self.assertEqual(rep["0xAAAA"]["unreadable"], 1)
        self.assertEqual(rep["0xAAAA"]["draws"], 0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail** (`python -m unittest discover -s tests -p "test_position_check.py" -v`: ImportError).

- [ ] **Step 3: Implement `position_check.py`**

Public functions: `check(capture_path, entries, max_draws=200, only_vs=None) -> dict[vs] -> {kind, draws, passed, share, unreadable, ...}` and `positions_for(row, entry, capture_dir) -> list[(x, y, z, w)]` (pre-transform positions for the row's `idx`, base vertex added). Element decode rules mirror the C++ decoders exactly: formats 38/57 (float4/float3), 32 (half4), 26 (16-bit ×4 with signed/normalized/exp), big-endian components; a stream endian of 2 (8in32) pair-swaps 16-bit components (`memory = component ^ 1`), 32-bit components need endian 2; the destination swizzle (`swz`, or the entry's `pos_swizzle` / `row_swizzles` when given) maps as in `DecodePositions` (3 bits per output component: 0-3 source, 4 = 0.0, 5 = 1.0, 7 = keep-default, i.e. 0 for x/y/z and 1 for w). Index and weight words: 8_8_8_8 read big-endian under endian 2, little-endian under 0; register component → source byte through the fetch's `swz`. Instancing and skinning follow Tasks 1 and 2 literally (float32 arithmetic via `struct` round-trips or `numpy`-free helpers: use Python floats but apply `math.trunc` exactly where the C++ does). Projection: `rows = vconst[base*4 : base*4+16]`; dot layout `clip[i] = dot(rows[i], p)`; combine layout `clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3`. CLI prints one line per shader plus totals, and `--cpp-fixture`.

- [ ] **Step 4: Capture-side dumps**

In `WriteDrawRow` (discovery only, under `g_mutex` as the texture dumps are): emit `"fetches"`, `"streams"` (dump each distinct `(phys, size)` once; reuse the pattern of `DumpTexture`: a `g_geo_dir`, a seen-set, counters and caps), `"idx"`, `"base_vertex"`, `"vconst"`. Stream extent: the bound stream object's size from its offset (as `ResolveStream` gives), capped at 1 MB. Nothing on the record path changes.

- [ ] **Step 5: Verify end to end**

`python -m unittest discover -s tests -p "test_*.py"`, `& .\tests\run_native_tests.cmd`, `& .\build.cmd -release fable_2`: green. Then one capture and a self-check of the tool on shaders whose entries are already trusted:

```
.\tools\drive_game.ps1 -Total 150 -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump" -Env @{FABLE2_NATIVE_DISCOVERY="120"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="4"}
python tools\xdk_sigmatch\position_check.py out\build\win-amd64-release\logs\native_discovery_<stamp>.jsonl --json docs\native-renderer\vs-transforms.json
```

Expected: `0xECD66A10092E6562` (plain, exact) and `0xA1F7E9885EC466DF` (skin, rigid) report an in-clip share of at least 0.90 — this validates the checker against entries sub-project 3 already confirmed visually. If either is below 0.90 the checker (not the table) is wrong: fix it before committing. Record the capture name, the per-shader lines and the list of vertex shaders seen in scene with no entry (hash, draws) in the report: Tasks 5, 7 and 9 use them.

- [ ] **Step 6: Commit**

```bash
git add tools/xdk_sigmatch/position_check.py tests/test_position_check.py src/native/capture/capture.cpp
git commit -m "Native coverage: discovery stream dumps and offline position checker"
```

---

### Task 5: Wind and displacement shader entries

**Files:**
- Modify: `docs/native-renderer/vs-transforms.json`, `src/native/capture/vs_transform_table.inc` (regenerated), `docs/native-renderer/frame-map.md` (new section 12 "Coverage and skinning", subsection "Wind and displacement")

Shaders: `0x475EC9F795E5EDBB`, `0x7C5710DEF3EE33C4`, `0xA5846836C90E1192` (and `0x2D40B53C926109BE`, which shares `0xA584...`'s fetch layout, if it appears in the capture).

- [ ] **Step 1: Read each dump** (`out\shader_dump\shader_<HASH>.ucode.vert`): the position fetch (its ordinal among vertex fetches, format, destination swizzle), the instructions that reach `oPos` (`dp4 ... c0..c3`), and what is added to the fetched position. Write the reading (instruction numbers) into frame-map section 12.

- [ ] **Step 2: Candidate entries**: `{"base": 0, "layout": "dot", "pos_fetch": <ordinal or -1>, "pos_swizzle": "<from the dump, when the fetch's own swizzle does not give (x, y, z, 1)>", "deformed": true, "manual": true, "evidence": "..."}`. Iterate with `position_check.py --entry <candidates.json> --vs <HASH>` on the Task 4 capture and, for shaders absent from the bridge scene, note that the Bowerstone capture `native_discovery_20261002_194918` has no stream dumps (so it cannot be replayed): such a shader gets an entry only if the disassembly alone is unambiguous (plain `dp4 c0..c3` of the fetched position plus a displacement) and the entry is marked `"evidence": "disassembly only; position_check pending a capture containing it"`; add a line to `docs/native-renderer/user-checks.md` asking for a capture where it appears.
- `0xA584...`: accept the plain position only if `position_check` reaches 0.90 AND the screenshot of Step 4 shows its meshes in place; otherwise keep it rejected and record why.

- [ ] **Step 3: UV entries**: for each accepted shader run `python tools\xdk_sigmatch\shader_trace.py out\shader_dump <PS_HASH> --vs <VS_HASH>` with the pixel shaders the capture pairs it with (the draw rows give `ps_hash`); add `"uv"` (with `"uv_manual": true`, `"uv_evidence"`) when the tracer gives one or a hand trace is unambiguous; otherwise leave it out (the draws are drawn as flat clay, `uv-unsupported`).

- [ ] **Step 4: Regenerate, build, measure**

```
python tools\xdk_sigmatch\gen_transform_table.py --json docs\native-renderer\vs-transforms.json --out src\native\capture\vs_transform_table.inc
& .\build.cmd -release fable_2
.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"
```

Report the before/after `[native] capture:` lines (captured, drawable, no-transform by vs), the `(deformed)` counts, and describe the screenshots (view with Read): the newly drawn meshes sit where the emulated half shows them. `python -m unittest discover -s tests -p "test_*.py"` and `& .\tests\run_native_tests.cmd` stay green.

- [ ] **Step 5: Commit**

```bash
git add docs/native-renderer/vs-transforms.json src/native/capture/vs_transform_table.inc docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md
git commit -m "Native coverage: wind and displacement shader entries (drawn undeformed)"
```

---

### Task 6: Instancing runtime (capture, records, geometry cache, stats)

**Files:**
- Modify: `src/native/capture/draw_record.h`, `src/native/capture/capture.cpp`, `src/native/render/clay_logic.h`, `src/native/render/geometry_cache.{h,cpp}`, `src/native/render/frame_scene.h` (if tallies need the new reasons), `tests/native/test_material.cpp` or a new `tests/native/test_draw_record.cpp`, `tests/native/test_clay_logic.cpp`

**Interfaces:**
- Consumes: Task 1 (`InstanceSet`, `InstanceSpec`, `SelectInstanceRows`, `InstanceBoundsOk`, `InstanceCopies`, `ExpandInstances`, `ExpandInstanceUvs`), Task 3 (`FindInstance`).
- Produces:
  - `SkipReason::kInstanceUnsupported` ("instance-unsupported") and `SkipReason::kSkinUnsupported` ("skin-unsupported") appended before `kCount` (Task 8 uses the second).
  - `DrawRecord::instances` / `DrawInputs::instances` (`InstanceSet`), copied by `AssembleRecord`.
  - `GeoKey` kind 5 = instanced positions, kind 6 = instanced UVs; `PositionKey`/`UvKey` return them for instanced records (key = mesh stream addr/size/stride/layout hash combined with rows addr/size, the three row layout hashes, the four constants' bits, the three offsets' bits and `flat_count`).
  - `RecordVertexCount(r)` returns `r.instances.flat_count` for instanced records.
  - `ClayStats::instanced` and `ClayStats::skinned` (draws built by each path this frame); the `Geometry:` F3 line becomes `Geometry: U uploads, H hits, M MB resident, instanced I, skinned K | hash ... ms, decode ... ms, record ... ms`.
  - The per-shader log breakdown tags `(instanced)`.

- [ ] **Step 1: Failing tests**

`tests/native/test_clay_logic.cpp`: `SkipReasonName` for both new reasons; two records identical except `instances.first` have different `PositionKey`s and kind 5; an instanced record's `RecordVertexCount` equals `instances.flat_count`; `FormatStatusText` contains `instanced 3, skinned 2` for `ClayStats{.instanced = 3, .skinned = 2}`. Record-assembly test (new file or the material test): `AssembleRecord` copies `in.instances`; an instanced `DrawInputs` whose `instances.active` is false but `instance_shader` is true gives `kInstanceUnsupported` (add `bool instance_shader` to `DrawInputs`, like `terrain_shader`).

- [ ] **Step 2: Capture**

`VsInfo` gains `const InstanceSpec* instance`, `bool have_instance`, `InstanceSet instance_rows`, `uint32_t instance_slot` (filled in `FillShader` with `SelectInstanceRows(fetches, *spec, e.pos.fetch_slot, ...)`; the mesh position comes from the transform's `pos_fetch`, which the generator set to `mesh_fetch`). In `FillDrawInputs`, when the shader has an instance spec:
- `in.instance_shader = true`; if `!have_instance` → leave `instances` inactive (assembly gives `instance-unsupported`);
- resolve the rows stream (`ResolveStream(dev, instance_slot, ...)`, `fc_match`, `ApplyFetchEndian` on the three rows; failure → `instance-unsupported`);
- read the constants through `ReadBankRegisters` for the registers named by `inv_count_ref`, `count_ref`, `first_ref`, `offset_ref` (x, y, z) and fill the `InstanceSet`;
- replace the "index past the stream" check: `copies = InstanceCopies(set)`, `vertices = vb.size / pos.stride_bytes`, `max = scan.max_index + base_vertex`; `base_vertex < 0` or `!InstanceBoundsOk(set, max, copies, vertices)` → `kBadIndex`; `flat_count = max + 1`, and `flat_count > kMaxDrawCount` → `kBadMemory`;
- non-indexed instanced draws → `instance-unsupported`.
`FillMaterial`: for instanced draws the UV stream must be the mesh stream (`uv.fetch_slot == pos.fetch_slot`), otherwise `uv-unsupported`.

- [ ] **Step 3: Geometry cache**

`Positions`: for `r.instances.active` — read the mesh stream and the rows stream (`ReadPhysical`; a failed read skips the draw as today), hash = `FrameHash(mesh) ^ (FrameHash(rows) * 0x9E3779B97F4A7C15)`, key kind 5; on a miss decode the whole mesh stream with `DecodePositions`, then `ExpandInstances(mesh, mesh_count, rows, rows_size, r.instances, flat_count, out)`, upload `flat_count` positions, `++st.instanced` (on hits too). `Uvs`: for instanced records decode the mesh UVs then `ExpandInstanceUvs` into `flat_count` UVs (key kind 6, same hash). `IndexRangeValid` uses the flat count.

- [ ] **Step 4: Stats and log** as in the Interfaces; `LogShaderBreakdown` tags shaders that have an instance spec.

- [ ] **Step 5: Verify**

`& .\tests\run_native_tests.cmd`, `python -m unittest discover -s tests -p "test_*.py"`, `& .\build.cmd -release fable_2`: green. No table entry has `"instance"` yet, so a gameplay run (`.\tools\drive_game.ps1 -Total 100 -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`) must show the same captured/drawable numbers as Task 5's run and `instanced 0` (regression check); a view-off run reports capture median under 0.5 ms.

- [ ] **Step 6: Commit**

```bash
git add src/native/capture/draw_record.h src/native/capture/capture.cpp src/native/render/clay_logic.h src/native/render/geometry_cache.h src/native/render/geometry_cache.cpp src/native/render/frame_scene.h tests/native
git commit -m "Native coverage: instancing runtime (records, flat positions and UVs, stats)"
```

(Stage the test files you changed by name instead of `tests/native` if other files there are modified.)

---

### Task 7: Instanced shader entries

**Files:**
- Modify: `docs/native-renderer/vs-transforms.json`, `src/native/capture/vs_transform_table.inc`, `docs/native-renderer/frame-map.md` (section 12 "Instancing"), `tests/native/test_instance_expand.cpp` (one regression case from real bytes), `docs/native-renderer/user-checks.md`

Shaders, in order: `0x8123C16DBF583F92`, `0xB636821F95DC9D8E`, then `0x6AD4108C3FF07966`, `0x48D30ECCF684F488`, `0xFC4F2EF6930768BE`, `0x33C00226C154C1F2`, `0x36B543CECAE5C781`.

- [ ] **Step 1: Read the dumps.** For each shader record in frame-map section 12: the index arithmetic (which constants, the literal used as bias), the three row fetches and the mesh position fetch (ordinals, formats, destination swizzles), how the rows meet the position (`cndeq`/`dp4` swizzles), the offset constant, and the transform. Derive `row_swizzles` and `pos_swizzle` so that, with the position decoded as `(x, y, z, 1)`, output component k is `dot(row_k, p)`; write the derivation instruction by instruction.
- [ ] **Step 2: The bias literal.** `c254`/`c255` are shader literals, not in the device bank. Find their values the way frame-map section 9 did for the terrain shaders (the `[vtess]`/register-file evidence: search frame-map for `c255 = (0, 1, 3, 2)` and follow that method; the SDK's `--dump_shaders` binary plus the game's literal upload at `0x82221CB0` is the other route). Record the value and its source in the evidence. If it cannot be established, try 0.5 and 0.0 in `position_check` and accept only if one passes and the other visibly fails on a shader whose `1/count` is inexact; say so in the evidence.
- [ ] **Step 3: Check offline.** `position_check.py --entry <candidates.json> --vs <HASH>` on a capture that contains the shader with stream dumps. The bridge scene may have few instanced draws: if a shader has none in the Task 4 capture, take one more autoplay capture at another delay; if it still does not appear, add the entry only when it shares the exact scheme (same fetch layout and arithmetic, verified in its dump) with an accepted shader, mark the evidence "scheme identical to 0x8123...; not sampled", and add a user check for a town capture.
- [ ] **Step 4: UV entries** for the accepted shaders (mesh stream element; `shader_trace.py` or a recorded hand trace).
- [ ] **Step 5: Regression fixture.** `position_check.py --cpp-fixture 8123C16DBF583F92 ...` and paste the arrays as one more case in `tests/native/test_instance_expand.cpp` (real half4 rows with 8in32, the entry's swizzles, expected first 16 positions within 1e-3).
- [ ] **Step 6: Regenerate, build, measure.** As Task 5 Step 4. Report: `instanced I` on F3/log, the coverage before/after, bad-index by vs, and the split screenshot (instanced meshes in place). `position_check` lines and capture names go into frame-map section 12.
- [ ] **Step 7: Commit**

```bash
git add docs/native-renderer/vs-transforms.json src/native/capture/vs_transform_table.inc docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md tests/native/test_instance_expand.cpp
git commit -m "Native coverage: instanced shader entries"
```

---

### Task 8: Weighted skinning runtime

**Files:**
- Modify: `src/native/capture/capture.cpp`, `src/native/render/geometry_cache.cpp`, `src/native/render/clay_logic.h` (if needed), tests touched by the behaviour change

**Interfaces:**
- Consumes: Task 2 (`BoneSkin`, `SelectSkin`, `SkinPositions`), Task 3 (`kSkinTable` in the new shape), Task 6 (`kSkinUnsupported`, `ClayStats::skinned`).
- Behaviour:
  - a shader with a skin entry whose layout `SelectSkin` rejects, or whose palette stream does not resolve, is skipped with `skin-unsupported` (today: `unknown-pos-format` / `no-stream`);
  - a record with an active skin is not `deformed`: the table's `deformed` flag applies only to entries without `"skin"` (a skin entry that is rejected at runtime skips the draw; it does not fall back to bind pose); count skinned draws in `ClayStats::skinned` and tag `(skin)` in the log breakdown (existing tag);
  - weights use the index word's endian (`index_endian`); the palette range stays "the stream from its offset to its end".

- [ ] **Step 1: Failing test.** In the record-assembly test: a `DrawInputs` with `skin.active` and a transform whose `deformed` is true yields a record with `deformed == false`; with `skin_shader == true` and `!skin.active` the skip is `kSkinUnsupported` (add `bool skin_shader` to `DrawInputs`).
- [ ] **Step 2: Implement** in `AssembleRecord`/`FillDrawInputs`; `GeometryCache::Positions` increments `st.skinned` for records with an active skin (hits included).
- [ ] **Step 3: Verify.** Suites and build green; a gameplay run shows `0xA1F7E9885EC466DF(skin)` drawn as before and `skinned K` on the `Geometry:` line with K equal to that shader's drawable count; decode time unchanged within noise (compare with Task 6's run).
- [ ] **Step 4: Commit**

```bash
git add src/native/capture/draw_record.h src/native/capture/capture.cpp src/native/render/geometry_cache.cpp src/native/render/clay_logic.h tests/native
git commit -m "Native coverage: weighted skinning runtime"
```

---

### Task 9: Skinned shader entries

**Files:**
- Modify: `docs/native-renderer/vs-transforms.json`, `src/native/capture/vs_transform_table.inc`, `docs/native-renderer/frame-map.md` (section 12 "Skinning"), `tests/native/test_bone_skin.cpp` (one regression case from real bytes), `docs/native-renderer/user-checks.md`

Shaders: `0xD4D558DA6A82BDC8` first; then `0x3A0F9098B839DDBC`, `0x82F6433A69263C75`, `0x9ED0BA440DBD51D4` (same four-bone layout) and `0x5F4416192E87005F` (one bone).

- [ ] **Step 1: Read the `0xD4D5...` dump instruction by instruction** (instr 9-43 and 69-74): which register component of the index fetch (`r2`) selects each bone's three row fetches, which component of the weight fetch (`r4`, fetched `.zyxw`) multiplies that bone's rows (the `mul`/`mad` chain at 29-40), and the permutations the accumulators carry into the final `dp4`s (41-43), so that with the position decoded as `(x, y, z, 1)` (its existing `pos_swizzle`) output component k is `dot(M_k, p)`. Write the pairs and the three `row_swizzles` with the derivation into frame-map section 12.
- [ ] **Step 2: The `cexec b0` block (instr 69).** Establish whether its condition is set for in-scene draws: find where the guest stores vertex shader boolean constants (the D3D `SetVertexShaderConstantB` path writes the GPU's `BOOL_CONSTANT` register; look for the device shadow next to the ALU constant shadow, or use the emulator's register file through the existing discovery "frame"/"raw" rows if they carry it). If it can be read, add it to the discovery row (`"vbool": "0x..."`) and report the share of in-scene `0xD4D5...` draws with the bit set. If it cannot be established, record "undetermined" and rely on Step 4's screenshot: a pose that matches the emulated half shows the block does not change x/y for those draws.
- [ ] **Step 3: Check offline.** `position_check.py --entry ... --vs D4D558DA6A82BDC8`: in-clip share at least 0.90, weight sum median within 0.98-1.02, bone index max below the palette's bone count. Iterate on the pairs/swizzles only by re-reading the dump, never by trial alone: a candidate that passes the clip test but contradicts the disassembly is rejected.
- [ ] **Step 4: Entries, regenerate, build, screenshot.** Add `"skin"` to `0xD4D5...`'s entry and remove its `"deformed": true` (the bind-pose form stays in the evidence text as the documented fallback), regenerate, build, and run `.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"` plus one `--fable2_native_view=native` run with `-Shots "95"`. Acceptance: in the native-view screenshot the hero's eyes and the sword on the back sit on the body, the dog's body matches its emulated pose, and no vertices are smeared across the screen. Describe the screenshots; if the pose is broken, return to Step 1 (do not ship a wrong pose: the fallback is the bind-pose entry with the reason recorded).
- [ ] **Step 5: The other four shaders**: same reading per dump; entries only with a `position_check` pass on sampled draws, or "layout identical to 0xD4D5...; not sampled in scene" when they do not appear (they are the quest-glow pass and may be absent), plus a user check.
- [ ] **Step 6: Regression fixture** from `--cpp-fixture D4D558DA6A82BDC8` into `tests/native/test_bone_skin.cpp`.
- [ ] **Step 7: Cost.** Report the F3/log `decode` time with skinning on against Task 8's run, and the `skinned K` count.
- [ ] **Step 8: Commit**

```bash
git add docs/native-renderer/vs-transforms.json src/native/capture/vs_transform_table.inc docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md tests/native/test_bone_skin.cpp src/native/capture/capture.cpp
git commit -m "Native coverage: weighted skinning entries for character shaders"
```

---

### Task 10: Validation and documentation

**Files:**
- Modify: `docs/native-renderer/frame-map.md` (section 12 "Validation"), `docs/native-renderer/user-checks.md`, `docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md` (status line, deviations), `README.md` (F3 line and reasons, if they changed)

- [ ] **Step 1: Autoplay measurements** (120 s each, one after another): `--fable2_native_render=false` with `-Env @{FABLE2_GUEST_WORK_LOG="1"}`; `--fable2_native_render=true --fable2_native_view=off`; `--fable2_native_render=true --fable2_native_view=split` with `-Shots "70,95"`; `--fable2_native_view=native` with `-Shots "95"`. Record: fps, guest work medians, view-off capture median (criterion: under 0.5 ms), captured/drawable and the non-terrain drawn share (criterion 1: at least 90% of main-scene non-terrain draws; compute it from the capture line as `(drawable - terrain) / (captured - terrain)` using the per-shader breakdown for the terrain count), `instanced I`, `skinned K`, decode ms, textured share, remaining skips by reason and shader.
- [ ] **Step 2: Screenshots** judged against criteria 2 and 3 (pose with eyes and sword attached; instanced meshes aligned). Describe each.
- [ ] **Step 3: Write section 12 "Validation"**: a table against success criteria 1-4, the logs, the screenshots, the remaining skips with next actions, known limitations (wind sway not animated, CPU skinning cost, shaders accepted on disassembly only).
- [ ] **Step 4: User checks.** Add to `docs/native-renderer/user-checks.md` (Pending, "Sub-project 5"): alignment in town, field and interior with characters moving (exact command, what to look for: eyes/sword attached while walking, crowd NPCs posed, fences/foliage clumps in place); a 10-minute run with an area transition; the Bowerstone-streets F3 numbers (`Native:` and `Geometry:` lines); any capture requests from Tasks 5, 7 and 9.
- [ ] **Step 5: Spec status** `Status: implemented (sub-project 5); autoplay validation in docs/native-renderer/frame-map.md section 12, user checks pending.` plus an "Implementation notes (deviations)" section listing what differed from the spec.
- [ ] **Step 6: Commit**

```bash
git add docs/native-renderer/frame-map.md docs/native-renderer/user-checks.md docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md README.md
git commit -m "Coverage and skinning validation results (autoplay; user checks pending)"
```
