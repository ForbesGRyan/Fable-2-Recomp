# Native Renderer Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify Fable 2's statically linked XDK D3D functions by masked-instruction matching against Skate 3 title update 3 (whose D3D functions skate3recomp names), hook them with an in-game census, and produce `docs/native-renderer/frame-map.md`: the D3D function map, the per-frame pass structure, the CPU-vs-GPU timing split, and a concrete recommendation for the first native pass.

**Architecture:** A small C++ tool loads a XEX through the SDK runtime in tool mode (which also applies a sibling `default.xexp`) and writes the in-memory image plus a JSON section/`.pdata` description. A pure-Python matcher reads two such dumps, derives function boundaries from `.pdata`, masks build-variant instruction fields, matches reference functions, propagates names along the call graph, and writes a JSON + Markdown map. A Python generator turns the map into strong-symbol census hooks; a census header logs per-frame call counts, callers and arguments; the SDK exposes per-frame emulated-draw stats (draw count, CP time, swap interval, draws per pitch) so the census can split CPU vs GPU-emulation cost.

**Tech Stack:** C++23 (clang/lld), ReXGlue SDK runtime (`rexruntime.dll`), Python 3 `unittest`, optional `capstone` (PPC disassembly for verification).

**Spec:** `docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md` (sub-project 2)

**Depends on:** `docs/superpowers/plans/2026-10-01-native-renderer-hook-layer.md` Tasks 1, 3 and 5 (SDK `renderer` branch with `native_guest_renderer.cpp` in `rexruntime.dll` and the D3D12 command-processor hooks). Tasks 1–4 here only need Task 1 of that plan.

## Global Constraints

- Game files never enter the repo. Inputs are passed by path:
  - Fable 2 game dir: the directory containing Fable 2's `default.xex` (repo root or `out\build\win-amd64-release`).
  - Skate 3 game dir: `C:\Users\Ryan\Downloads\Skate3Recomp-Windows\game` (contains `default.xex` SHA-256 `1db39496585c521d17a2137804f42cf73ebed2b32cac166ec42dbf772f4dcf7f` and `default.xexp` SHA-256 `eb9ef9109dfa6d940df2e156e7eaeda4603d2b2319ca6451f324b1c27f2b1f4c`).
- Image dumps go under `out\xdk\` (gitignored via `out`). Committed outputs contain only addresses, names and statistics — no instruction bytes.
- Skate 3 reference addresses are for **title update 3** (base 3.0.0.0 + `default.xexp`); the dump must show the patch was applied.
- The census is inert unless the environment variable `FABLE2_D3D_CENSUS=<frames>` is set; emulated-frame stats are collected only while the SDK cvar `native_render_collect_stats` is true (default false).
- Strong overrides must not collide with existing ones: any symbol whose `__imp__<symbol>` already appears under `src/` is skipped by the generator.
- Shell commands follow the user's `rtk` prefix convention where a filter exists.

## Review Focus

- **`.pdata` entries with zero length or addresses outside executable sections** (padding, data-in-code): must be ignored, never produce matches. Test: Task 2 `test_parse_pdata_skips_invalid`.
- **Multiple equally good candidates** for a reference function (duplicated XDK helpers): reported as `ambiguous` with all candidates, never silently picking one. Test: Task 3 `test_ambiguous_candidates_reported`.
- **Call-graph conflicts** (two matched parents imply different targets for the same reference callee): the callee is marked `conflict` and dropped, not overwritten. Test: Task 3 `test_callgraph_conflict_dropped`.
- **Hook generator meets an already-overridden symbol** (e.g. `MainRenderLoop_82B9CD68` in `fps_meter.h`): emits a comment instead of a duplicate strong symbol that would break the link. Test: Task 5 `test_skips_existing_override`.
- **Census enabled for more frames than the game runs / game closed mid-capture**: every completed frame is already flushed to disk line by line; nothing is buffered to exit. Test: Task 6 Step 4 (kill the game mid-capture, file is valid JSONL).

---

### Task 1: XEX image dump tool

**Files:**
- Create: `tools/xdk_sigmatch/xex_image_dump.cpp`
- Modify: `CMakeLists.txt` (new target `fable_2_xex_image_dump`, Windows only)
- Possibly modify (SDK `renderer` branch): `src/system/rexruntime.def` (missing exports)

**Interfaces:**
- Produces: CLI `fable_2_xex_image_dump.exe <game_dir> <out_prefix>` writing `<out_prefix>.img` (raw image bytes from the image base) and `<out_prefix>.json`:
  ```json
  {"base": 2181038080, "size": 23199744, "entry": 2183123456, "patched": true,
   "pdata": {"address": 2181042176, "size": 123456},
   "sections": [{"name": ".text", "address": 2183135232, "size": 1234, "executable": true}]}
  ```
  (`patched` is true when a sibling `default.xexp` existed.)

- [ ] **Step 1: Write the tool**

Create `tools/xdk_sigmatch/xex_image_dump.cpp`:
```cpp
// Loads <game_dir>\default.xex through the ReXGlue runtime in tool mode (a
// sibling default.xexp is applied automatically by UserModule) and writes the
// in-memory image plus a JSON description. Local analysis input only: never
// commit the outputs.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <game_dir> <out_prefix>\n", argv[0]);
    return 2;
  }
  const std::filesystem::path game_dir = argv[1];
  const std::string out_prefix = argv[2];
  const bool patched = std::filesystem::exists(game_dir / "default.xexp");

  rex::Runtime runtime(game_dir);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                       .tool_mode = true}) != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  if (runtime.LoadXexImage("game:\\default.xex") != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "LoadXexImage failed\n");
    return 1;
  }
  auto user_module = runtime.kernel_state()->GetExecutableModule();
  auto* xex = user_module ? user_module->xex_module() : nullptr;
  if (!xex) {
    std::fprintf(stderr, "no executable module\n");
    return 1;
  }

  const uint32_t base = xex->base_address();
  uint32_t end = base;
  for (const auto& s : xex->binary_sections()) {
    end = std::max(end, s.virtual_address + s.virtual_size);
  }
  std::vector<uint8_t> image(end - base, 0);
  for (const auto& s : xex->binary_sections()) {
    if (s.host_data && s.virtual_size) {
      std::memcpy(image.data() + (s.virtual_address - base), s.host_data, s.virtual_size);
    }
  }
  std::ofstream(out_prefix + ".img", std::ios::binary)
      .write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));

  std::ofstream json(out_prefix + ".json");
  json << "{\"base\": " << base << ", \"size\": " << image.size()
       << ", \"entry\": " << xex->entry_point() << ", \"patched\": " << (patched ? "true" : "false")
       << ", \"pdata\": {\"address\": " << xex->exception_directory_address()
       << ", \"size\": " << xex->exception_directory_size() << "}, \"sections\": [";
  bool first = true;
  for (const auto& s : xex->binary_sections()) {
    json << (first ? "" : ", ") << "{\"name\": \"" << s.name << "\", \"address\": "
         << s.virtual_address << ", \"size\": " << s.virtual_size
         << ", \"executable\": " << (s.executable ? "true" : "false") << "}";
    first = false;
  }
  json << "]}\n";
  std::printf("dumped 0x%08X..0x%08X (%zu bytes), patched=%d\n", base, end, image.size(), patched);
  return 0;
}
```
Add `#include <algorithm>` and `#include <cstring>` at the top. (`XexModule::entry_point()`, `exception_directory_address()/size()` and `binary_sections()` are public members in `rex/system/xex_module.h`.)

- [ ] **Step 2: Add the target**

In `CMakeLists.txt`, next to `fable_2_vulkan_smoke` (inside the same `if(WIN32 ...)` region), add:
```cmake
# XDK signature matching input (docs/superpowers/plans/2026-10-01-native-renderer-discovery.md).
add_executable(fable_2_xex_image_dump tools/xdk_sigmatch/xex_image_dump.cpp)
target_include_directories(fable_2_xex_image_dump PRIVATE ${FABLE2_SDK_ROOT}/include)
target_link_libraries(fable_2_xex_image_dump PRIVATE ${FABLE2_SDK_ROOT}/lib/rexruntime.lib)
target_compile_features(fable_2_xex_image_dump PRIVATE cxx_std_23)
add_custom_command(TARGET fable_2_xex_image_dump POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${FABLE2_SDK_ROOT}/bin/rexruntime.dll
          $<TARGET_FILE_DIR:fable_2_xex_image_dump>)
```

- [ ] **Step 3: Build; export anything the curated .def is missing**

Run: `cmake --build out\build\win-amd64-release --target fable_2_xex_image_dump`
If it links, go to Step 4. If lld reports `undefined symbol` errors, collect the decorated names and export them:
```
cmake --build out\build\win-amd64-release --target fable_2_xex_image_dump -- -v 2> link.txt
```
Re-run the printed final link command manually with `-Xlinker /errorlimit:0 -Xlinker /demangle:no` appended, extract every `undefined symbol: <name>` (decorated), append them under a comment line `; Used by fable_2_xex_image_dump (tools/xdk_sigmatch)` to `thirdparty/rexglue-sdk/src/system/rexruntime.def` (skip names already present), then `tools\build_runtime_sdk.cmd` and rebuild the target. Expected: links.

- [ ] **Step 4: Dump both games**

Run:
```
mkdir out\xdk
out\build\win-amd64-release\fable_2_xex_image_dump.exe . out\xdk\fable2
out\build\win-amd64-release\fable_2_xex_image_dump.exe "C:\Users\Ryan\Downloads\Skate3Recomp-Windows\game" out\xdk\skate3
```
Expected: Fable 2 prints `dumped 0x82000000..0x83620000 (23199744 bytes), patched=0` (image range matches the runtime log line `image=82000000-83620000`). Skate 3 prints `patched=1`. Both `.json` files contain a non-zero `pdata.size`.

- [ ] **Step 5: Commit**

```
rtk git add tools/xdk_sigmatch/xex_image_dump.cpp CMakeLists.txt
rtk git -C thirdparty/rexglue-sdk add src/system/rexruntime.def
rtk git -C thirdparty/rexglue-sdk commit -m "Export runtime symbols used by the XEX image dump tool"
rtk git add thirdparty/rexglue-sdk
rtk git commit -m "Add XEX image dump tool for XDK signature matching"
```
(If Step 3 needed no `.def` changes, skip the two SDK lines and the `thirdparty/rexglue-sdk` add.)

---

### Task 2: Matcher core — image loading, `.pdata`, masking, scanning

**Files:**
- Create: `tools/xdk_sigmatch/sigmatch.py`
- Create: `tests/test_xdk_sigmatch.py`

**Interfaces:**
- Produces (module `sigmatch`):
  - `class Image` with `base: int`, `data: bytes`, `sections: list[dict]`, `word(addr) -> int` (big-endian u32), `is_executable(addr) -> bool`.
  - `load_image(prefix: str) -> Image` (reads `<prefix>.json` + `<prefix>.img`).
  - `parse_pdata(image: Image, address: int, size: int) -> dict[int, int]` — function start → length in bytes.
  - `mask_for(word: int, level: str) -> int` — `level` is `"strict"` or `"loose"`.
  - `function_words(image, start, length) -> list[int]`.
  - `matches(ref_words, cand_words, level) -> bool`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_xdk_sigmatch.py`:
```python
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sigmatch", ROOT / "tools" / "xdk_sigmatch" / "sigmatch.py")
sm = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sm)

BASE = 0x82000000


def be(*words):
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in words)


def make_image(code_words, text_at=0x1000, pdata=()):
    """Synthetic image: .pdata at BASE+0x100, .text at BASE+text_at."""
    data = bytearray(text_at + 4 * len(code_words))
    data[text_at:text_at + 4 * len(code_words)] = be(*code_words)
    pd = b"".join(struct.pack(">II", start, (length_words << 8)) for start, length_words in pdata)
    data[0x100:0x100 + len(pd)] = pd
    sections = [
        {"name": ".pdata", "address": BASE + 0x100, "size": len(pd), "executable": False},
        {"name": ".text", "address": BASE + text_at, "size": 4 * len(code_words), "executable": True},
    ]
    return sm.Image(BASE, bytes(data), sections), BASE + 0x100, len(pd)


def bl(src, dst):
    return 0x48000001 | ((dst - src) & 0x03FFFFFC)


class MaskTests(unittest.TestCase):
    def test_branch_displacements_masked_at_every_level(self):
        for level in ("strict", "loose"):
            self.assertEqual(sm.mask_for(0x48001235, level), 0xFC000003)  # bl
            self.assertEqual(sm.mask_for(0x4182000C, level), 0xFFFF0003)  # beq

    def test_immediates_masked_only_when_loose(self):
        lis = 0x3D608200   # lis r11,0x8200
        lwz = 0x816B1234   # lwz r11,0x1234(r11)
        self.assertEqual(sm.mask_for(lis, "strict"), 0xFFFFFFFF)
        self.assertEqual(sm.mask_for(lis, "loose"), 0xFFFF0000)
        self.assertEqual(sm.mask_for(lwz, "loose"), 0xFFFF0000)

    def test_stack_relative_kept_when_loose(self):
        stw_sp = 0x91810008  # stw r12,8(r1)
        self.assertEqual(sm.mask_for(stw_sp, "loose"), 0xFFFFFFFF)


class PdataTests(unittest.TestCase):
    def test_parse_pdata(self):
        img, addr, size = make_image([0x60000000] * 8, pdata=[(BASE + 0x1000, 4), (BASE + 0x1010, 4)])
        funcs = sm.parse_pdata(img, addr, size)
        self.assertEqual(funcs, {BASE + 0x1000: 16, BASE + 0x1010: 16})

    def test_parse_pdata_skips_invalid(self):
        img, addr, size = make_image([0x60000000] * 4,
                                     pdata=[(BASE + 0x1000, 4), (BASE + 0x1000, 0), (BASE + 0x100, 2), (0, 0)])
        funcs = sm.parse_pdata(img, addr, size)
        self.assertEqual(funcs, {BASE + 0x1000: 16})


class MatchTests(unittest.TestCase):
    def test_relocated_copy_matches_loose_not_strict(self):
        a = [0x3D608200, 0x816B1234, 0x4E800020]
        b = [0x3D608300, 0x816B5678, 0x4E800020]
        self.assertFalse(sm.matches(a, b, "strict"))
        self.assertTrue(sm.matches(a, b, "loose"))

    def test_different_opcode_never_matches(self):
        self.assertFalse(sm.matches([0x7C0802A6], [0x7C0903A6], "loose"))

    def test_length_mismatch_never_matches(self):
        self.assertFalse(sm.matches([0x4E800020], [0x4E800020, 0x60000000], "loose"))

    def test_load_image_roundtrip(self):
        img, _, _ = make_image([0x4E800020])
        with tempfile.TemporaryDirectory() as tmp:
            prefix = str(Path(tmp) / "g")
            Path(prefix + ".img").write_bytes(img.data)
            Path(prefix + ".json").write_text(json.dumps({
                "base": BASE, "size": len(img.data), "entry": BASE, "patched": False,
                "pdata": {"address": BASE + 0x100, "size": 0}, "sections": img.sections}))
            loaded = sm.load_image(prefix)
            self.assertEqual(loaded.word(BASE + 0x1000), 0x4E800020)
            self.assertTrue(loaded.is_executable(BASE + 0x1000))
            self.assertFalse(loaded.is_executable(BASE + 0x100))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest tests.test_xdk_sigmatch -v`
Expected: error loading module (`FileNotFoundError` for `sigmatch.py`).

- [ ] **Step 3: Implement the core**

Create `tools/xdk_sigmatch/sigmatch.py`:
```python
"""Match statically linked XDK functions between two Xbox 360 images.

Inputs are dumps from fable_2_xex_image_dump (<prefix>.img + <prefix>.json).
Function boundaries come from .pdata. Build-variant instruction fields are
masked: branch displacements always; at the "loose" level also the 16-bit
immediates of D-form instructions whose base register is not r1 (absolute
addresses, globals, struct offsets that moved between XDK builds).
"""
import json
import struct
from pathlib import Path

# D-form opcodes with a 16-bit immediate/displacement: addi, addis, ori,
# oris, lwz..stfdu (32-55), ld/std family (58, 62).
D_FORM_IMM = {14, 15, 24, 25, *range(32, 56), 58, 62}


class Image:
    def __init__(self, base, data, sections):
        self.base = base
        self.data = data
        self.sections = sections

    def word(self, addr):
        off = addr - self.base
        return struct.unpack_from(">I", self.data, off)[0]

    def contains(self, addr):
        return 0 <= addr - self.base <= len(self.data) - 4

    def is_executable(self, addr):
        return any(s["executable"] and s["address"] <= addr < s["address"] + s["size"]
                   for s in self.sections)


def load_image(prefix):
    meta = json.loads(Path(prefix + ".json").read_text())
    data = Path(prefix + ".img").read_bytes()
    img = Image(meta["base"], data, meta["sections"])
    img.meta = meta
    return img


def parse_pdata(image, address, size):
    """Function start -> length in bytes. Skips zero-length and non-code entries."""
    funcs = {}
    for off in range(0, size - size % 8, 8):
        start, packed = struct.unpack_from(">II", image.data, address - image.base + off)
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if length == 0 or not image.is_executable(start) or not image.is_executable(start + length - 4):
            continue
        funcs[start] = length
    return funcs


def mask_for(word, level):
    op = word >> 26
    if op == 18:            # b / bl: LI displacement
        return 0xFC000003
    if op == 16:            # bc: BD displacement
        return 0xFFFF0003
    if level == "strict":
        return 0xFFFFFFFF
    ra = (word >> 16) & 0x1F
    if op in D_FORM_IMM and ra != 1:
        return 0xFFFF0000
    return 0xFFFFFFFF


def function_words(image, start, length):
    return [image.word(start + i) for i in range(0, length, 4)]


def matches(ref_words, cand_words, level):
    if len(ref_words) != len(cand_words):
        return False
    for r, c in zip(ref_words, cand_words):
        m = mask_for(r, level)
        if (r & m) != (c & m):
            return False
    return True
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest tests.test_xdk_sigmatch -v`
Expected: 9 tests OK.

- [ ] **Step 5: Commit**

```
rtk git add tools/xdk_sigmatch/sigmatch.py tests/test_xdk_sigmatch.py
rtk git commit -m "Add XDK signature matcher core (pdata, masking, matching)"
```

---

### Task 3: Reference matching, call-graph propagation, report

**Files:**
- Modify: `tools/xdk_sigmatch/sigmatch.py`
- Create: `tools/xdk_sigmatch/skate3_tu3_reference.json`
- Modify: `tests/test_xdk_sigmatch.py`

**Interfaces:**
- Consumes: Task 2 API.
- Produces:
  - `match_references(ref_img, ref_funcs, tgt_img, tgt_funcs, references: dict[str, int]) -> list[dict]` — each result `{"name", "ref", "target" (int|None), "confidence": "strict"|"loose"|"prefix"|"ambiguous"|"none", "candidates": [int]}`.
  - `call_targets(image, start, length) -> list[int]` (bl targets in order).
  - `propagate(ref_img, ref_funcs, tgt_img, tgt_funcs, results) -> list[dict]` — appends `callgraph` results named `<parent>.callee<i>` and marks conflicts `{"confidence": "conflict"}`.
  - `render_markdown(results) -> str`.
  - CLI: `python tools/xdk_sigmatch/sigmatch.py --ref PREFIX --target PREFIX --refs JSON --out JSON --markdown MD`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_xdk_sigmatch.py` (before the `if __name__` block):
```python
def two_images(ref_code, tgt_code, ref_pdata, tgt_pdata):
    r, ra, rs = make_image(ref_code, pdata=ref_pdata)
    t, ta, ts = make_image(tgt_code, pdata=tgt_pdata)
    return r, sm.parse_pdata(r, ra, rs), t, sm.parse_pdata(t, ta, ts)


class ReferenceTests(unittest.TestCase):
    def test_unique_loose_match(self):
        ref = [0x3D608200, 0x816B1234, 0x4E800020]
        tgt = [0x60000000, 0x3D608300, 0x816B5678, 0x4E800020]
        r, rf, t, tf = two_images(ref, tgt, [(BASE + 0x1000, 3)], [(BASE + 0x1000, 1), (BASE + 0x1004, 3)])
        res = sm.match_references(r, rf, t, tf, {"SetIndices": BASE + 0x1000})
        self.assertEqual(res[0]["target"], BASE + 0x1004)
        self.assertEqual(res[0]["confidence"], "loose")

    def test_ambiguous_candidates_reported(self):
        f = [0x38600000, 0x4E800020]
        r, rf, t, tf = two_images(f, f + f, [(BASE + 0x1000, 2)], [(BASE + 0x1000, 2), (BASE + 0x1008, 2)])
        res = sm.match_references(r, rf, t, tf, {"Dup": BASE + 0x1000})
        self.assertIsNone(res[0]["target"])
        self.assertEqual(res[0]["confidence"], "ambiguous")
        self.assertEqual(res[0]["candidates"], [BASE + 0x1000, BASE + 0x1008])

    def test_no_match_reported(self):
        r, rf, t, tf = two_images([0x38600000, 0x4E800020], [0x38800001, 0x4E800020],
                                  [(BASE + 0x1000, 2)], [(BASE + 0x1000, 2)])
        res = sm.match_references(r, rf, t, tf, {"Gone": BASE + 0x1000})
        self.assertEqual(res[0]["confidence"], "none")


class CallgraphTests(unittest.TestCase):
    def test_callee_propagated(self):
        # parent at +0x1000 calls child at +0x1010 (ref) / +0x1020 (target)
        p = BASE + 0x1000
        ref = [bl(p, BASE + 0x1010), 0x4E800020, 0x60000000, 0x60000000, 0x38600007, 0x4E800020]
        tgt = [bl(p, BASE + 0x1020), 0x4E800020] + [0x60000000] * 6 + [0x38600007, 0x4E800020]
        r, rf, t, tf = two_images(ref, tgt, [(p, 2), (BASE + 0x1010, 2)], [(p, 2), (BASE + 0x1020, 2)])
        res = sm.match_references(r, rf, t, tf, {"Parent": p})
        res = sm.propagate(r, rf, t, tf, res)
        child = [x for x in res if x["name"] == "Parent.callee0"][0]
        self.assertEqual(child["target"], BASE + 0x1020)
        self.assertEqual(child["confidence"], "callgraph")

    def test_callgraph_conflict_dropped(self):
        # A (2 words) and B (3 words, distinct shape) both call C in the
        # reference; in the target A calls X and B calls Y.
        a, b = BASE + 0x1000, BASE + 0x1008
        c = BASE + 0x1014
        li1, li2, blr = 0x38600001, 0x38600002, 0x4E800020
        ref = [bl(a, c), blr, li2, bl(b + 4, c), blr, li1, blr]
        x, y = BASE + 0x1014, BASE + 0x101C
        tgt = [bl(a, x), blr, li2, bl(b + 4, y), blr, li1, blr, li1, blr]
        r, rf, t, tf = two_images(ref, tgt, [(a, 2), (b, 3), (c, 2)], [(a, 2), (b, 3), (x, 2), (y, 2)])
        res = sm.match_references(r, rf, t, tf, {"A": a, "B": b})
        res = sm.propagate(r, rf, t, tf, res)
        conflicts = [e for e in res if e["confidence"] == "conflict"]
        self.assertEqual(len(conflicts), 1)
        self.assertIsNone(conflicts[0]["target"])

    def test_markdown_lists_every_result(self):
        md = sm.render_markdown([
            {"name": "SetIndices", "ref": BASE, "target": BASE + 4, "confidence": "loose", "candidates": []},
            {"name": "Gone", "ref": BASE + 8, "target": None, "confidence": "none", "candidates": []},
        ])
        self.assertIn("| SetIndices | 0x82000000 | 0x82000004 | loose |", md)
        self.assertIn("| Gone | 0x82000008 | - | none |", md)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest tests.test_xdk_sigmatch -v`
Expected: `AttributeError: module 'sigmatch' has no attribute 'match_references'` (and similar).

- [ ] **Step 3: Implement**

Append to `tools/xdk_sigmatch/sigmatch.py`:
```python
PREFIX_WORDS = 16


def _candidates(ref_words, tgt_img, tgt_funcs, level, prefix=False):
    out = []
    for start, length in sorted(tgt_funcs.items()):
        if prefix:
            if length < 4 * len(ref_words):
                continue
            cand = function_words(tgt_img, start, 4 * len(ref_words))
        else:
            if length != 4 * len(ref_words):
                continue
            cand = function_words(tgt_img, start, length)
        if matches(ref_words, cand, level):
            out.append(start)
    return out


def _match_one(name, ref_addr, ref_img, ref_funcs, tgt_img, tgt_funcs):
    result = {"name": name, "ref": ref_addr, "target": None, "confidence": "none", "candidates": []}
    length = ref_funcs.get(ref_addr)
    if length is None:
        result["confidence"] = "no-ref-function"
        return result
    words = function_words(ref_img, ref_addr, length)
    for level, prefix in (("strict", False), ("loose", False), ("loose", True)):
        ws = words[:PREFIX_WORDS] if prefix else words
        if prefix and len(words) <= PREFIX_WORDS:
            continue
        cands = _candidates(ws, tgt_img, tgt_funcs, level, prefix)
        if len(cands) == 1:
            result.update(target=cands[0], confidence="prefix" if prefix else level)
            return result
        if len(cands) > 1:
            result.update(confidence="ambiguous", candidates=cands)
            return result
    return result


def match_references(ref_img, ref_funcs, tgt_img, tgt_funcs, references):
    return [_match_one(name, addr, ref_img, ref_funcs, tgt_img, tgt_funcs)
            for name, addr in references.items()]


def call_targets(image, start, length):
    targets = []
    for addr in range(start, start + length, 4):
        w = image.word(addr)
        if (w >> 26) == 18 and (w & 3) == 1:  # bl (LK=1, AA=0)
            disp = w & 0x03FFFFFC
            if disp & 0x02000000:
                disp -= 0x04000000
            targets.append(addr + disp)
    return targets


def propagate(ref_img, ref_funcs, tgt_img, tgt_funcs, results):
    results = list(results)
    by_ref = {r["ref"]: r for r in results if r["target"] is not None}
    implied = {}  # ref callee -> set of target callees
    names = {}
    changed = True
    while changed:
        changed = False
        for r in list(by_ref.values()):
            if r["confidence"] not in ("strict", "loose", "callgraph"):
                continue
            rc = call_targets(ref_img, r["ref"], ref_funcs[r["ref"]])
            tc = call_targets(tgt_img, r["target"], tgt_funcs[r["target"]])
            if len(rc) != len(tc):
                continue
            for i, (a, b) in enumerate(zip(rc, tc)):
                if a not in ref_funcs or b not in tgt_funcs:
                    continue
                implied.setdefault(a, set()).add(b)
                names.setdefault(a, f'{r["name"]}.callee{i}')
                if a not in by_ref:
                    entry = {"name": names[a], "ref": a, "target": b, "confidence": "callgraph",
                             "candidates": []}
                    by_ref[a] = entry
                    results.append(entry)
                    changed = True
    for a, targets in implied.items():
        if len(targets) > 1 and a in by_ref and by_ref[a]["confidence"] == "callgraph":
            by_ref[a].update(target=None, confidence="conflict", candidates=sorted(targets))
    return results


def render_markdown(results):
    lines = ["| Name | Skate 3 TU3 | Fable 2 | Confidence | Notes |",
             "|---|---|---|---|---|"]
    for r in results:
        tgt = f'0x{r["target"]:08X}' if r["target"] is not None else "-"
        notes = ", ".join(f"0x{c:08X}" for c in r["candidates"])
        lines.append(f'| {r["name"]} | 0x{r["ref"]:08X} | {tgt} | {r["confidence"]} | {notes} |')
    return "\n".join(lines) + "\n"


def main():
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ref", required=True)
    p.add_argument("--target", required=True)
    p.add_argument("--refs", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--markdown", required=True)
    a = p.parse_args()
    ref_img, tgt_img = load_image(a.ref), load_image(a.target)
    rp, tp = ref_img.meta["pdata"], tgt_img.meta["pdata"]
    ref_funcs = parse_pdata(ref_img, rp["address"], rp["size"])
    tgt_funcs = parse_pdata(tgt_img, tp["address"], tp["size"])
    refs = {k: int(v, 16) for k, v in json.loads(Path(a.refs).read_text()).items()}
    results = propagate(ref_img, ref_funcs, tgt_img, tgt_funcs,
                        match_references(ref_img, ref_funcs, tgt_img, tgt_funcs, refs))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(results, indent=2))
    Path(a.markdown).write_text(render_markdown(results))
    found = sum(1 for r in results if r["target"] is not None)
    print(f"{found}/{len(results)} functions mapped "
          f"({len(ref_funcs)} ref / {len(tgt_funcs)} target functions)")


if __name__ == "__main__":
    main()
```

Create `tools/xdk_sigmatch/skate3_tu3_reference.json` (addresses from skate3recomp's D3D-level hooks, Skate 3 TU3):
```json
{
  "D3DDevice_SetIndices": "0x82B79190",
  "D3DDevice_SetStreamSource": "0x82B78FF0",
  "D3DDevice_DrawIndexedVertices": "0x82B7AD68",
  "D3DDevice_DrawVertices": "0x82B7A970",
  "D3DDevice_BeginVertices": "0x82B79FC0",
  "D3DDevice_SetPixelShader": "0x82B7F408",
  "D3DDevice_SetVertexShader": "0x82B7F150",
  "D3DDevice_SetRenderState": "0x82B83C48",
  "D3DDevice_SetViewport": "0x82B74310",
  "D3DDevice_SetScissorRect": "0x82B769C0",
  "D3DDevice_SetPending_AluConstants": "0x82B83FE0",
  "D3DDevice_Swap": "0x82B82E08",
  "RegisterTexture": "0x82C9A618"
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest tests.test_xdk_sigmatch -v`
Expected: 15 tests OK.

- [ ] **Step 5: Commit**

```
rtk git add tools/xdk_sigmatch/sigmatch.py tools/xdk_sigmatch/skate3_tu3_reference.json tests/test_xdk_sigmatch.py
rtk git commit -m "Add XDK reference matching, call-graph propagation and report output"
```

---

### Task 4: Run the matcher on the real games and verify

**Files:**
- Create: `docs/native-renderer/xdk-map.json`, `docs/native-renderer/xdk-map.md`
- Modify: `tools/xdk_sigmatch/sigmatch.py` (add `--disasm` verification helper)

- [ ] **Step 1: Add a disassembly helper**

Append to `sigmatch.py` before `main()`:
```python
def disasm(prefix, addr, count):
    """Print `count` instructions at `addr` (requires: pip install capstone)."""
    import capstone
    img = load_image(prefix)
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    code = img.data[addr - img.base: addr - img.base + 4 * count]
    for ins in md.disasm(code, addr):
        print(f"0x{ins.address:08X}: {ins.mnemonic:8} {ins.op_str}")
```
and at the start of `main()` handle a subcommand:
```python
    import sys
    if len(sys.argv) >= 2 and sys.argv[1] == "disasm":
        disasm(sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4]) if len(sys.argv) > 4 else 24)
        return
```

- [ ] **Step 2: Run the matcher**

Run:
```
python tools\xdk_sigmatch\sigmatch.py --ref out\xdk\skate3 --target out\xdk\fable2 --refs tools\xdk_sigmatch\skate3_tu3_reference.json --out docs\native-renderer\xdk-map.json --markdown docs\native-renderer\xdk-map.md
```
Expected: `N/M functions mapped (...)`. Record N. If the 13 references are mostly `none`/`no-ref-function`, check that `out\xdk\skate3.json` has `"patched": true` and that the reference addresses fall inside Skate 3's `.pdata` functions (a reference that is not a function start reports `no-ref-function`; then set it to the containing function's start, found by scanning `parse_pdata` keys `<= addr`).

- [ ] **Step 3: Verify five mappings by hand**

`pip install capstone`, then for at least 5 mapped entries (prefer `D3DDevice_DrawIndexedVertices`, `D3DDevice_SetStreamSource`, `D3DDevice_SetIndices`, `D3DDevice_SetPixelShader`, and one `callgraph` entry):
```
python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\skate3 <skate3 addr> 32
python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 <fable2 addr> 32
```
Expected: same instruction sequence apart from branch targets and address/offset immediates. Add a "Verified" column note in `xdk-map.md` for each checked row (`verified by disassembly`).

- [ ] **Step 4: Fallback for unmatched draw functions**

If `D3DDevice_DrawIndexedVertices` or `D3DDevice_DrawVertices` is unmatched: run the game with the function tracer filtered to the D3D range (`tools\fable2-functrace.cmd` with filter `82B9` and `82BA`, as documented in that script), and pick functions whose per-frame call count equals the SDK's draw count from the plan-1 suppression census / Task 6 stats. Record them in `xdk-map.md` with confidence `trace-correlated`.

- [ ] **Step 5: Commit**

```
rtk git add tools/xdk_sigmatch/sigmatch.py docs/native-renderer/xdk-map.json docs/native-renderer/xdk-map.md
rtk git commit -m "Map Fable 2 XDK D3D functions against Skate 3 TU3"
```

---

### Task 5: SDK emulated-frame stats and census hook generator

**Files:**
- Create (SDK): `include/rex/graphics/emulated_frame_stats.h` (pure accumulator)
- Modify (SDK): `include/rex/graphics/native_guest_renderer.h`, `src/graphics/native_guest_renderer.cpp`, `src/system/rexruntime.def`, `src/graphics/d3d12/command_processor.cpp`
- Create: `tests/native/test_emulated_frame_stats.cpp`; modify `tests/run_native_tests.cmd`
- Create: `tools/xdk_sigmatch/gen_census_hooks.py`, `tests/test_gen_census_hooks.py`

**Interfaces:**
- Produces (SDK, namespace `rex::graphics`):
  ```cpp
  struct EmulatedFrameStats {
    static constexpr uint32_t kMaxPitches = 16;
    uint64_t frame = 0;            // swaps observed so far
    uint32_t draws = 0;            // emulated IssueDraw calls in the frame
    uint64_t draw_cpu_ns = 0;      // CP-thread time inside IssueDraw
    uint64_t swap_interval_ns = 0; // time since the previous swap
    uint32_t pitch_count = 0;
    uint32_t pitches[kMaxPitches] = {};
    uint32_t pitch_draws[kMaxPitches] = {};
    uint32_t other_pitch_draws = 0; // draws at pitches beyond kMaxPitches distinct values
  };
  bool ShouldCollectEmulatedFrameStats();             // cvar native_render_collect_stats
  // cvar native_render_suppress_debug (ablation): ShouldSuppressEmulatedDraws()
  // returns true even without native output, so suppress_pitches applies.
  void NoteEmulatedDraw(uint32_t surface_pitch, uint64_t cpu_ns);
  void NoteEmulatedSwap(uint64_t now_ns);             // closes the frame
  bool GetLastEmulatedFrameStats(EmulatedFrameStats* out);  // false until one frame closed
  ```
  Pure accumulator `rex::graphics::EmulatedFrameAccumulator` with `AddDraw(pitch, ns)`, `CloseFrame(now_ns) -> EmulatedFrameStats`.
- Produces (tool): `python tools/xdk_sigmatch/gen_census_hooks.py --map docs/native-renderer/xdk-map.json --init generated/default/fable_2_init.cpp --src src --out src/diagnostics/fable2_d3d_census_hooks.inc` emitting lines `FABLE2_D3D_CENSUS_HOOK(<id>, "<name>", <symbol>)` or `// skipped <name> (<symbol>): already overridden in <file>`.

- [ ] **Step 1: Write failing tests**

Create `tests/native/test_emulated_frame_stats.cpp`:
```cpp
// Synthetic standalone test; no game or GPU.
#include <rex/graphics/emulated_frame_stats.h>
#include <iostream>

int main() {
  rex::graphics::EmulatedFrameAccumulator acc;
  acc.AddDraw(1280, 100);
  acc.AddDraw(1280, 50);
  acc.AddDraw(640, 25);
  auto f1 = acc.CloseFrame(1'000'000);
  if (f1.frame != 1 || f1.draws != 3 || f1.draw_cpu_ns != 175) return 1;
  if (f1.swap_interval_ns != 0) return 2;  // first frame has no previous swap
  if (f1.pitch_count != 2 || f1.pitches[0] != 1280 || f1.pitch_draws[0] != 2) return 3;
  if (f1.pitches[1] != 640 || f1.pitch_draws[1] != 1) return 4;
  auto f2 = acc.CloseFrame(17'000'000);
  if (f2.frame != 2 || f2.draws != 0 || f2.swap_interval_ns != 16'000'000) return 5;
  for (uint32_t p = 0; p < 20; ++p) acc.AddDraw(100 + p, 1);  // overflow distinct pitches
  auto f3 = acc.CloseFrame(18'000'000);
  if (f3.pitch_count != 16 || f3.other_pitch_draws != 4 || f3.draws != 20) return 6;
  std::cout << "PASS: per-frame draws, time, intervals, pitch histogram + overflow\n";
  return 0;
}
```
Add to `tests/run_native_tests.cmd` before `exit /b 0`:
```
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_emulated_frame_stats.cpp" -o "%OUT%\frame_stats.exe" || exit /b 1
"%OUT%\frame_stats.exe" || exit /b 1
```
Create `tests/test_gen_census_hooks.py`:
```python
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen", ROOT / "tools" / "xdk_sigmatch" / "gen_census_hooks.py")
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)


class GenTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.map = d / "map.json"
        self.map.write_text(json.dumps([
            {"name": "D3DDevice_DrawIndexedVertices", "ref": 1, "target": 0x82BA0000, "confidence": "loose", "candidates": []},
            {"name": "D3DDevice_Swap", "ref": 2, "target": 0x82B9CD68, "confidence": "loose", "candidates": []},
            {"name": "Gone", "ref": 3, "target": None, "confidence": "none", "candidates": []},
        ]))
        self.init = d / "init.cpp"
        self.init.write_text("\t{ 0x82BA0000, sub_82BA0000 },\n\t{ 0x82B9CD68, MainRenderLoop_82B9CD68 },\n")
        self.src = d / "src"
        (self.src / "diagnostics").mkdir(parents=True)
        (self.src / "diagnostics" / "fps_meter.h").write_text("__imp__MainRenderLoop_82B9CD68(ctx, base);\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_emits_hook_for_mapped_function(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertIn('FABLE2_D3D_CENSUS_HOOK(0, "D3DDevice_DrawIndexedVertices", sub_82BA0000)', out)

    def test_skips_existing_override(self):
        out = gen.generate(self.map, self.init, self.src)
        self.assertNotIn("FABLE2_D3D_CENSUS_HOOK(1", out)
        self.assertIn("// skipped D3DDevice_Swap (MainRenderLoop_82B9CD68): already overridden in", out)

    def test_unmapped_omitted(self):
        self.assertNotIn("Gone", gen.generate(self.map, self.init, self.src))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `tests\run_native_tests.cmd` → clang error `emulated_frame_stats.h file not found`.
Run: `python -m unittest tests.test_gen_census_hooks -v` → `FileNotFoundError` for `gen_census_hooks.py`.

- [ ] **Step 3: Implement the accumulator**

Create `thirdparty/rexglue-sdk/include/rex/graphics/emulated_frame_stats.h`:
```cpp
#pragma once

// Per-frame statistics of the emulated GPU path (draw count, command-
// processor time in IssueDraw, swap interval, draws per render-target
// pitch). Pure accumulator; the runtime wraps it with a mutex.

#include <cstdint>

namespace rex::graphics {

struct EmulatedFrameStats {
  static constexpr uint32_t kMaxPitches = 16;
  uint64_t frame = 0;
  uint32_t draws = 0;
  uint64_t draw_cpu_ns = 0;
  uint64_t swap_interval_ns = 0;
  uint32_t pitch_count = 0;
  uint32_t pitches[kMaxPitches] = {};
  uint32_t pitch_draws[kMaxPitches] = {};
  uint32_t other_pitch_draws = 0;
};

class EmulatedFrameAccumulator {
 public:
  void AddDraw(uint32_t pitch, uint64_t cpu_ns) {
    ++current_.draws;
    current_.draw_cpu_ns += cpu_ns;
    for (uint32_t i = 0; i < current_.pitch_count; ++i) {
      if (current_.pitches[i] == pitch) {
        ++current_.pitch_draws[i];
        return;
      }
    }
    if (current_.pitch_count < EmulatedFrameStats::kMaxPitches) {
      current_.pitches[current_.pitch_count] = pitch;
      current_.pitch_draws[current_.pitch_count] = 1;
      ++current_.pitch_count;
    } else {
      ++current_.other_pitch_draws;
    }
  }

  EmulatedFrameStats CloseFrame(uint64_t now_ns) {
    EmulatedFrameStats done = current_;
    done.frame = ++frames_;
    done.swap_interval_ns = last_swap_ns_ ? now_ns - last_swap_ns_ : 0;
    last_swap_ns_ = now_ns;
    current_ = {};
    return done;
  }

 private:
  EmulatedFrameStats current_;
  uint64_t frames_ = 0;
  uint64_t last_swap_ns_ = 0;
};

}  // namespace rex::graphics
```

- [ ] **Step 4: Expose it from the runtime and feed it from the D3D12 CP**

In `native_guest_renderer.h` add `#include <rex/graphics/emulated_frame_stats.h>` and, inside the namespace:
```cpp
// ---- Emulated-frame statistics (discovery / CPU-vs-GPU split) -------------
bool ShouldCollectEmulatedFrameStats();
void NoteEmulatedDraw(uint32_t surface_pitch, uint64_t cpu_ns);
void NoteEmulatedSwap(uint64_t now_ns);
bool GetLastEmulatedFrameStats(EmulatedFrameStats* out);
```
In `native_guest_renderer.cpp` add `#include <mutex>`, the cvar after the others:
```cpp
REXCVAR_DEFINE_BOOL(native_render_collect_stats, false, "GPU",
                    "Collect per-frame emulated draw statistics (draw count, IssueDraw "
                    "time, swap interval, draws per pitch) for the Fable 2 D3D census.");
REXCVAR_DEFINE_BOOL(native_render_suppress_debug, false, "GPU",
                    "Ablation: apply native_render_suppress_pitches to emulated frames "
                    "too (no native renderer needed), to see which pass each pitch is.");
```
and change `ShouldSuppressEmulatedDraws()` (from plan 1, Task 3) to:
```cpp
bool ShouldSuppressEmulatedDraws() {
  if (REXCVAR_GET(native_render_suppress_debug)) return true;
  return REXCVAR_GET(native_render_suppress_emulated_draws) &&
         g_native_output_active.load(std::memory_order_relaxed);
}
```
and inside `namespace rex::graphics`:
```cpp
namespace {
std::mutex g_stats_mutex;
EmulatedFrameAccumulator g_stats_acc;
EmulatedFrameStats g_stats_last;
bool g_stats_valid = false;
}  // namespace

bool ShouldCollectEmulatedFrameStats() { return REXCVAR_GET(native_render_collect_stats); }

void NoteEmulatedDraw(uint32_t surface_pitch, uint64_t cpu_ns) {
  std::lock_guard<std::mutex> lock(g_stats_mutex);
  g_stats_acc.AddDraw(surface_pitch, cpu_ns);
}

void NoteEmulatedSwap(uint64_t now_ns) {
  std::lock_guard<std::mutex> lock(g_stats_mutex);
  g_stats_last = g_stats_acc.CloseFrame(now_ns);
  g_stats_valid = true;
}

bool GetLastEmulatedFrameStats(EmulatedFrameStats* out) {
  std::lock_guard<std::mutex> lock(g_stats_mutex);
  if (!g_stats_valid || !out) return false;
  *out = g_stats_last;
  return true;
}
```
Build the runtime (`tools\build_runtime_sdk.cmd`), list the four new decorated names with `llvm-nm --defined-only --extern-only` on `native_guest_renderer.cpp.obj` (as in plan 1, Task 3 Step 5), and append them to `src/system/rexruntime.def` under `; Emulated-frame statistics`.

In `src/graphics/d3d12/command_processor.cpp`:
- At the very top of `D3D12CommandProcessor::IssueDraw` (first statement after the `#endif` of the profile scope), add:
```cpp
  // Discovery stats: time the whole IssueDraw (all return paths).
  struct DrawStatsScope {
    const RegisterFile& regs;
    bool on = ShouldCollectEmulatedFrameStats();
    std::chrono::steady_clock::time_point t0 =
        on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    ~DrawStatsScope() {
      if (!on) return;
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - t0).count();
      NoteEmulatedDraw(regs.Get<reg::RB_SURFACE_INFO>().surface_pitch, uint64_t(ns));
    }
  } draw_stats_scope{*register_file_};
```
- At the top of `D3D12CommandProcessor::IssueSwap`, add:
```cpp
  if (ShouldCollectEmulatedFrameStats()) {
    NoteEmulatedSwap(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count()));
  }
```
Add `#include <chrono>` if missing.

- [ ] **Step 5: Implement the generator**

Create `tools/xdk_sigmatch/gen_census_hooks.py`:
```python
"""Generate strong-symbol census hooks for mapped XDK D3D functions.

Reads the matcher output (xdk-map.json) and the generated address->symbol
table (generated/default/fable_2_init.cpp). Skips any symbol whose original
(__imp__<symbol>) is already referenced under src/ (existing override).
"""
import argparse
import json
import re
from pathlib import Path

ENTRY = re.compile(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(\w+)\s*\}")


def symbol_table(init_path):
    return {int(a, 16): s for a, s in ENTRY.findall(Path(init_path).read_text())}


def existing_override(src_dir, symbol):
    needle = f"__imp__{symbol}"
    for f in sorted(Path(src_dir).rglob("*")):
        if f.suffix in (".h", ".cpp", ".inc") and f.is_file() and needle in f.read_text(errors="ignore"):
            return f
    return None


def generate(map_path, init_path, src_dir):
    entries = json.loads(Path(map_path).read_text())
    symbols = symbol_table(init_path)
    lines = ["// Generated by tools/xdk_sigmatch/gen_census_hooks.py - do not edit."]
    for i, e in enumerate(entries):
        if e.get("target") is None:
            continue
        sym = symbols.get(e["target"])
        if sym is None:
            lines.append(f'// skipped {e["name"]}: no generated symbol at 0x{e["target"]:08X}')
            continue
        hit = existing_override(src_dir, sym)
        if hit is not None:
            lines.append(f'// skipped {e["name"]} ({sym}): already overridden in {hit.as_posix()}')
            continue
        lines.append(f'FABLE2_D3D_CENSUS_HOOK({i}, "{e["name"]}", {sym})')
    return "\n".join(lines) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--map", required=True)
    p.add_argument("--init", required=True)
    p.add_argument("--src", required=True)
    p.add_argument("--out", required=True)
    a = p.parse_args()
    Path(a.out).write_text(generate(a.map, a.init, a.src))


if __name__ == "__main__":
    main()
```
Note: `generate` must not scan the output file itself on regeneration; the generated `.inc` contains only symbol names (not `__imp__`), so it never self-matches.

- [ ] **Step 6: Run tests to verify they pass**

Run: `tests\run_native_tests.cmd` → `PASS: per-frame draws, time, intervals, pitch histogram + overflow`.
Run: `python -m unittest tests.test_gen_census_hooks -v` → 3 tests OK.
Run: `tools\build_runtime_sdk.cmd` → exit 0.

- [ ] **Step 7: Commit**

```
rtk git -C thirdparty/rexglue-sdk add include/rex/graphics/emulated_frame_stats.h include/rex/graphics/native_guest_renderer.h src/graphics/native_guest_renderer.cpp src/system/rexruntime.def src/graphics/d3d12/command_processor.cpp
rtk git -C thirdparty/rexglue-sdk commit -m "Add emulated-frame statistics for native renderer discovery"
rtk git add thirdparty/rexglue-sdk tests/native/test_emulated_frame_stats.cpp tests/run_native_tests.cmd tools/xdk_sigmatch/gen_census_hooks.py tests/test_gen_census_hooks.py
rtk git commit -m "Add emulated-frame stats test and census hook generator"
```

---

### Task 6: In-game D3D census

**Files:**
- Create: `src/diagnostics/fable2_d3d_census.h`
- Create (generated, committed): `src/diagnostics/fable2_d3d_census_hooks.inc`
- Modify: `src/main.cpp` (include), `src/diagnostics/fps_meter.h` (per-frame call)

**Interfaces:**
- Consumes: Task 5 SDK stats API; generated `.inc`.
- Produces: `fable2::d3dcensus::OnFrame()` (per guest frame); log file `logs/d3d_census_<YYYYMMDD_HHMMSS>.jsonl`, one JSON object per frame:
  ```json
  {"frame":12,"guest_ms":16.6,"gpu":{"draws":812,"draw_cpu_ms":4.1,"swap_interval_ms":16.7,"pitches":{"1280":640,"640":90},"other":0},
   "funcs":{"D3DDevice_DrawIndexedVertices":{"calls":790,"callers":{"0x82345678":512},"args":[["0x00000004","0x00000024","0x00000000","0x00000000","0x00000000","0x00000000"]]}}}
  ```
  (`args` holds r3–r8 for the first 4 calls per function per frame; `callers` holds the top 8 link-register values.)

- [ ] **Step 1: Generate the hooks**

Run:
```
python tools\xdk_sigmatch\gen_census_hooks.py --map docs\native-renderer\xdk-map.json --init generated\default\fable_2_init.cpp --src src --out src\diagnostics\fable2_d3d_census_hooks.inc
```
Expected: one `FABLE2_D3D_CENSUS_HOOK` line per mapped function, `// skipped` lines for already-overridden ones (e.g. a Swap that maps onto `MainRenderLoop_82B9CD68`).

- [ ] **Step 2: Write the census header**

Create `src/diagnostics/fable2_d3d_census.h`:
```cpp
#pragma once

// D3D census (docs/superpowers/plans/2026-10-01-native-renderer-discovery.md):
// strong overrides on the XDK D3D functions mapped by tools/xdk_sigmatch,
// logging per-frame call counts, callers (LR) and r3-r8 samples, plus the
// SDK's emulated-frame stats. Inert unless FABLE2_D3D_CENSUS=<frames>.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>

namespace fable2::d3dcensus {

struct FuncFrame {
  const char* name = nullptr;
  uint64_t calls = 0;
  std::map<uint32_t, uint64_t> callers;
  std::vector<std::array<uint32_t, 6>> args;
};

inline int FramesRequested() {
  static const int frames = [] {
    const char* v = std::getenv("FABLE2_D3D_CENSUS");
    return v ? std::max(0, std::atoi(v)) : 0;
  }();
  return frames;
}

inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::map<uint32_t, FuncFrame>& Current() { static std::map<uint32_t, FuncFrame> m; return m; }
inline std::atomic<int>& FramesWritten() { static std::atomic<int> n{0}; return n; }

inline bool Active() {
  return FramesRequested() > 0 && FramesWritten().load(std::memory_order_relaxed) < FramesRequested();
}

inline std::FILE* Log() {
  static std::FILE* f = [] {
    char name[96];
    const std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof(name), "logs/d3d_census_%Y%m%d_%H%M%S.jsonl", std::localtime(&t));
    return std::fopen(name, "w");
  }();
  return f;
}

inline void OnCall(uint32_t id, const char* name, const PPCContext& ctx) {
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(Mutex());
  FuncFrame& f = Current()[id];
  f.name = name;
  ++f.calls;
  ++f.callers[uint32_t(ctx.lr)];
  if (f.args.size() < 4) {
    f.args.push_back({ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32});
  }
}

// Per guest frame (MainRenderLoop override). Writes one JSON line and flushes.
inline void OnFrame() {
  if (!Active()) return;
  static bool stats_enabled = [] {
    rex::cvar::SetFlagByName("native_render_collect_stats", "true");
    return true;
  }();
  (void)stats_enabled;
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double guest_ms = std::chrono::duration<double, std::milli>(now - last).count();
  last = now;

  std::string line;
  char buf[160];
  const int frame = FramesWritten().fetch_add(1) + 1;
  std::snprintf(buf, sizeof(buf), "{\"frame\":%d,\"guest_ms\":%.3f", frame, guest_ms);
  line += buf;

  rex::graphics::EmulatedFrameStats gpu;
  if (rex::graphics::GetLastEmulatedFrameStats(&gpu)) {
    std::snprintf(buf, sizeof(buf),
                  ",\"gpu\":{\"draws\":%u,\"draw_cpu_ms\":%.3f,\"swap_interval_ms\":%.3f,\"pitches\":{",
                  gpu.draws, gpu.draw_cpu_ns / 1e6, gpu.swap_interval_ns / 1e6);
    line += buf;
    for (uint32_t i = 0; i < gpu.pitch_count; ++i) {
      std::snprintf(buf, sizeof(buf), "%s\"%u\":%u", i ? "," : "", gpu.pitches[i], gpu.pitch_draws[i]);
      line += buf;
    }
    std::snprintf(buf, sizeof(buf), "},\"other\":%u}", gpu.other_pitch_draws);
    line += buf;
  }

  line += ",\"funcs\":{";
  {
    std::lock_guard<std::mutex> lock(Mutex());
    bool first_fn = true;
    for (auto& [id, f] : Current()) {
      std::snprintf(buf, sizeof(buf), "%s\"%s\":{\"calls\":%llu,\"callers\":{", first_fn ? "" : ",",
                    f.name, (unsigned long long)f.calls);
      line += buf;
      first_fn = false;
      std::vector<std::pair<uint64_t, uint32_t>> top;
      for (auto& [lr, n] : f.callers) top.push_back({n, lr});
      std::sort(top.rbegin(), top.rend());
      for (size_t i = 0; i < top.size() && i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "%s\"0x%08X\":%llu", i ? "," : "", top[i].second,
                      (unsigned long long)top[i].first);
        line += buf;
      }
      line += "},\"args\":[";
      for (size_t i = 0; i < f.args.size(); ++i) {
        const auto& a = f.args[i];
        std::snprintf(buf, sizeof(buf),
                      "%s[\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"]",
                      i ? "," : "", a[0], a[1], a[2], a[3], a[4], a[5]);
        line += buf;
      }
      line += "]}";
    }
    Current().clear();
  }
  line += "}}\n";
  if (std::FILE* f = Log()) {
    std::fputs(line.c_str(), f);
    std::fflush(f);
  }
}

}  // namespace fable2::d3dcensus

#define FABLE2_D3D_CENSUS_HOOK(ID, NAME, SYM)                                  \
  extern "C" void __imp__##SYM(PPCContext& ctx, uint8_t* base);               \
  extern "C" void SYM(PPCContext& __restrict ctx, uint8_t* base) {            \
    fable2::d3dcensus::OnCall(ID, NAME, ctx);                                 \
    __imp__##SYM(ctx, base);                                                  \
  }
#include "fable2_d3d_census_hooks.inc"
#undef FABLE2_D3D_CENSUS_HOOK
```
Add `#include <array>` to the include list.

- [ ] **Step 3: Wire it in**

In `src/main.cpp`, add after `#include "fps_meter.h"`:
```cpp
#include "fable2_d3d_census.h"
```
In `src/diagnostics/fps_meter.h`, add a forward declaration near the top (after includes):
```cpp
namespace fable2::d3dcensus { inline void OnFrame(); }
```
and in `MainRenderLoop_82B9CD68`, directly before `fable2::f5lua::poll_mainloop(ctx, base);`:
```cpp
  // D3D census (FABLE2_D3D_CENSUS=<frames>); inert otherwise.
  fable2::d3dcensus::OnFrame();
```
Build: `build.cmd -release fable_2` → exit 0 (a duplicate-symbol link error means the generator missed an existing override: add that file's `__imp__` reference check and regenerate).

- [ ] **Step 4: Capture and check**

Run (from `out\build\win-amd64-release`, PowerShell):
```
$env:FABLE2_D3D_CENSUS = "600"; .\fable_2.exe --fullscreen=false
```
Load a save into gameplay before ~600 frames elapse if possible, or rerun with a larger count. Close the game mid-capture once to confirm the file is valid line-by-line:
```
python -c "import json,glob; f=sorted(glob.glob('logs/d3d_census_*.jsonl'))[-1]; rows=[json.loads(l) for l in open(f)]; print(f, len(rows), rows[-1]['frame'])"
```
Expected: parses without error. Check the success criterion: for frames with a `gpu` object, `funcs.D3DDevice_DrawIndexedVertices.calls + funcs.D3DDevice_DrawVertices.calls` ≈ `gpu.draws` (equal up to clears/memexport draws; note the typical difference).

- [ ] **Step 5: Commit**

```
rtk git add src/diagnostics/fable2_d3d_census.h src/diagnostics/fable2_d3d_census_hooks.inc src/main.cpp src/diagnostics/fps_meter.h
rtk git commit -m "Add in-game D3D census with emulated-frame stats"
```

---

### Task 7: Frame map and sub-project 3 recommendation

**Files:**
- Create: `tools/xdk_sigmatch/summarize_census.py`, `tests/test_summarize_census.py`
- Create: `docs/native-renderer/frame-map.md`

**Interfaces:**
- Produces: `summarize(rows: list[dict]) -> dict` with keys `frames`, `guest_ms_median`, `swap_interval_ms_median`, `draw_cpu_ms_median`, `draws_median`, `pitches` (pitch → median draws per frame), `top_callers` (function → list of `(lr, total)` top 10), `draw_call_ratio` (median of hooked-draws / gpu.draws). CLI prints Markdown sections for `frame-map.md`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_summarize_census.py`:
```python
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("summ", ROOT / "tools" / "xdk_sigmatch" / "summarize_census.py")
summ = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summ)


def row(frame, guest, draws, cpu, pitches, dip_calls, callers):
    return {"frame": frame, "guest_ms": guest,
            "gpu": {"draws": draws, "draw_cpu_ms": cpu, "swap_interval_ms": guest,
                    "pitches": pitches, "other": 0},
            "funcs": {"D3DDevice_DrawIndexedVertices": {"calls": dip_calls, "callers": callers, "args": []}}}


class SummarizeTests(unittest.TestCase):
    def test_medians_and_ratio(self):
        rows = [row(1, 16.0, 100, 4.0, {"1280": 80, "640": 20}, 95, {"0x82000010": 60}),
                row(2, 17.0, 110, 5.0, {"1280": 90, "640": 20}, 105, {"0x82000010": 70}),
                row(3, 40.0, 120, 6.0, {"1280": 100}, 115, {"0x82000020": 5})]
        s = summ.summarize(rows)
        self.assertEqual(s["frames"], 3)
        self.assertEqual(s["guest_ms_median"], 17.0)
        self.assertEqual(s["draws_median"], 110)
        self.assertEqual(s["pitches"]["1280"], 90)
        self.assertEqual(s["pitches"]["640"], 20)
        self.assertEqual(s["top_callers"]["D3DDevice_DrawIndexedVertices"][0], ("0x82000010", 130))
        self.assertAlmostEqual(s["draw_call_ratio"], 105 / 110, places=3)

    def test_rows_without_gpu_are_tolerated(self):
        s = summ.summarize([{"frame": 1, "guest_ms": 16.0, "funcs": {}}])
        self.assertEqual(s["frames"], 1)
        self.assertIsNone(s["draws_median"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m unittest tests.test_summarize_census -v`
Expected: `FileNotFoundError` for `summarize_census.py`.

- [ ] **Step 3: Implement**

Create `tools/xdk_sigmatch/summarize_census.py`:
```python
"""Summarize a d3d_census_*.jsonl capture into frame-map.md sections."""
import json
import statistics
import sys
from collections import Counter, defaultdict

DRAW_FUNCS = ("D3DDevice_DrawIndexedVertices", "D3DDevice_DrawVertices")


def _median(values):
    return statistics.median(values) if values else None


def summarize(rows):
    gpu_rows = [r for r in rows if "gpu" in r]
    pitch_series = defaultdict(list)
    for r in gpu_rows:
        for p in set().union(*(x["gpu"]["pitches"].keys() for x in gpu_rows)):
            pitch_series[p].append(r["gpu"]["pitches"].get(p, 0))
    callers = defaultdict(Counter)
    for r in rows:
        for name, f in r.get("funcs", {}).items():
            callers[name].update({lr: n for lr, n in f["callers"].items()})
    ratios = []
    for r in gpu_rows:
        hooked = sum(r["funcs"].get(n, {}).get("calls", 0) for n in DRAW_FUNCS)
        if r["gpu"]["draws"]:
            ratios.append(hooked / r["gpu"]["draws"])
    return {
        "frames": len(rows),
        "guest_ms_median": _median([r["guest_ms"] for r in rows]),
        "swap_interval_ms_median": _median([r["gpu"]["swap_interval_ms"] for r in gpu_rows]),
        "draw_cpu_ms_median": _median([r["gpu"]["draw_cpu_ms"] for r in gpu_rows]),
        "draws_median": _median([r["gpu"]["draws"] for r in gpu_rows]),
        "pitches": {p: _median(v) for p, v in sorted(pitch_series.items(), key=lambda kv: -_median(kv[1]))},
        "top_callers": {n: c.most_common(10) for n, c in callers.items()},
        "draw_call_ratio": _median(ratios),
    }


def to_markdown(s):
    out = ["## Timing", "",
           f"- Frames captured: {s['frames']}",
           f"- Guest frame time (median): {s['guest_ms_median']} ms",
           f"- Swap interval (median): {s['swap_interval_ms_median']} ms",
           f"- Emulated IssueDraw CP time (median): {s['draw_cpu_ms_median']} ms/frame",
           f"- Emulated draws (median): {s['draws_median']} per frame",
           f"- Hooked draw calls / emulated draws (median): {s['draw_call_ratio']}", "",
           "## Draws per render-target pitch (median per frame)", "",
           "| Pitch | Draws |", "|---|---|"]
    out += [f"| {p} | {d} |" for p, d in s["pitches"].items()]
    out += ["", "## Top call sites", ""]
    for name, top in s["top_callers"].items():
        out += [f"### {name}", "", "| Caller (LR) | Calls |", "|---|---|"]
        out += [f"| {lr} | {n} |" for lr, n in top]
        out.append("")
    return "\n".join(out) + "\n"


def main():
    rows = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
    print(to_markdown(summarize(rows)))


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest tests.test_summarize_census -v`
Expected: 2 tests OK.

- [ ] **Step 5: Capture menu and gameplay, write the frame map**

Capture one census in the main menu and one in gameplay (Task 6 Step 4 procedure, separate runs), then:
```
python tools\xdk_sigmatch\summarize_census.py out\build\win-amd64-release\logs\<menu>.jsonl > menu.md
python tools\xdk_sigmatch\summarize_census.py out\build\win-amd64-release\logs\<gameplay>.jsonl > gameplay.md
```
Create `docs/native-renderer/frame-map.md` with these sections, filled from the outputs and the plan-1 `suppression census` log lines:
1. **D3D function map** — include `docs/native-renderer/xdk-map.md`'s table (copy) with the verified column.
2. **Main menu frame** — paste `menu.md`.
3. **Gameplay frame** — paste `gameplay.md`.
4. **Pass list** — for each significant pitch: its draw count, and its likely purpose, established by ablation. For each pitch `<p>`, run gameplay with `--native_render_suppress_debug=true --native_render_suppress_pitches=<p>` (emulated frames, no native renderer needed) and record what disappears or breaks on screen. Name each pass (shadow map, main scene, water, post, UI).
5. **CPU vs GPU emulation** — state which dominates: if `draw_cpu_ms_median` plus the remaining command-processor work is a large fraction of `swap_interval_ms_median` and the guest frame time tracks the swap interval, emulation-bound; if `guest_ms_median` is far above the swap interval with low draw CPU time, guest-CPU-bound.
6. **Engine submission sites** — the top callers of the draw functions, each with the function it lies in (`grep -n "0x<LR rounded to function>" generated/default/fable_2_init.cpp` or the `.pdata` start ≤ LR).
7. **Recommendation for sub-project 3** — the first pass to replace (the largest pass by draw count that is plain world geometry), its pitch for `native_render_suppress_pitches`, the pitches to put in `native_render_keep_pitches`, and the draw function + caller to capture first.

- [ ] **Step 6: Commit**

```
rtk git add tools/xdk_sigmatch/summarize_census.py tests/test_summarize_census.py docs/native-renderer/frame-map.md
rtk git commit -m "Add census summarizer and Fable 2 frame map"
```
