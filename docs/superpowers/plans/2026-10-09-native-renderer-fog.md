# Native renderer fog, in-scatter and sky: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. One fresh
> implementer and one fresh reviewer per task; steps use checkboxes.

**Goal:** every lit native draw is fogged with the game's arithmetic and tables, and the sky is
drawn by a native pass.

**Architecture:** a per-frame fog block (six pixel registers and two 1D tables read from guest
memory) and a per-frame sky block, captured like the light and shadow blocks; one shared `Fog`
function in the lit pixel shaders; a full-screen sky step between the opaque and the blended
records.

**Tech stack:** C++ (capture in `src/native/capture`, pass in `src/native/render`), HLSL in
`fable2_native_shaders.h`, Python tools in `tools/xdk_sigmatch`, tests in `tests/` and
`tests/native/`.

**Spec:** `docs/superpowers/specs/2026-10-09-native-renderer-fog-design.md`. Research:
`.superpowers/sdd/fog-research.md` (read it before any task; its section numbers are cited).

## Global Constraints

- Branch `native-renderer-fog`. Never push, stash, reset or switch branches. Never stage
  `fable_2_manifest.toml`; never `git add -A`; `*.ps1` needs `git add -f`. Commit trailer, exactly:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`.
- No SDK change (`thirdparty/rexglue-sdk`) in this plan. If a task seems to need one: stop, report.
- The game starts only through `tools/drive_game.ps1`, with `-BootSave Hero000`. Agents never
  pass `-UserPresent`: idle gate, or NEEDS_RUN with exact command lines. Runs by the controller
  with the user present are labelled `RUN UNCHECKED`. Every run is checked for the freeze
  signature before a number or picture is used.
- A build that changes bindings, targets or shader text is not run on the real GPU before one
  clean 240 s software-adapter run with the debug layer (the controller makes it).
- Timing only from a quiet machine. Shot brackets for anything that moves with the sun: 3 s.
- Evidence rule (spec criterion 9). Evidence goes to `out/native-evidence/fog/`.
- Guest reads are bounded and fail closed; no value from the guest reaches a shader without a
  finiteness check. `DrawRecord` stays 616 bytes. Nothing is read with the native view off;
  view-off capture median stays under 0.5 ms.
- Nothing assumes the game's resolution.
- Tests first where a pure function can hold the rule. Antivirus and shell rules: the
  workspace's `implementer-rules.md`.
- New reason strings: `fog-block`, `sky-block`, `sky-texture`.

## Review Focus

1. A frame without a valid fog block (menus, loading, first frames, a frame the view was switched
   on in after the first fogged draw, a table address outside guest memory): no fog that frame,
   counted; never a stale table or another frame's registers.
2. Non-finite or out-of-range inputs: a NaN register, a zero `c66`, `d = 0` at the eye, a table
   texel of infinity, `c67.w * dD` far beyond 1: the function is finite for every pixel and a
   test feeds these.
3. The sky step's order and depth: nothing opaque is covered, nothing blended is covered, the
   shadow mask's depth read and the sky's depth test do not conflict (debug layer clean), and
   with the sky block missing the picture is the clear colour, not garbage.
4. Fog off, light off, shadows off and their combinations: each is the earlier sub-project's
   picture; the white mask and table fallbacks are bound whenever a shader declares them.
5. View switches, resize, device loss: the table textures and the sky's textures are recreated
   with the pass's other objects; a table is never uploaded while bound.

---

### Task 1: Discovery (capture only) and the oracle

**Files:** `src/native/capture/capture.cpp` (and headers it needs), `src/native/render/texture_decode.h`
only if a pure helper for 1D fetch constants is added there (with tests in `tests/native/`),
`tools/xdk_sigmatch/fog_tables.py` (new), `tests/test_fog_tables.py` (new),
`docs/native-renderer/frame-map.md` (new section 16 "Fog and sky", subsection "Discovery").

**Produces:** discovery captures holding (a) dumps of 1D textures, each distinct address and
content of a capture kept; (b) the vertex shader's fetch slots in draw rows' `"tf"`; (c) a
decoded row for `DrawIndx:82217EE8` draws (shader hashes, `"pfull"`, `"tf"`, `"rs"`, vertex
constants, the stream bytes within the existing caps); (d) format 29 dumped in full (bounded by
the existing 8 MB cap). `fog_tables.py`: `decode_tf4(bytes) -> 64 x (r, g, b, a)` floats,
`decode_tf5(bytes) -> 256 x (r, g, b, a)` after the swizzle, `untile_scene(bytes, w, h)` for the
format-29 scene picture, each with tests on hand-made bytes.

- [ ] Read research sections 2, 3, 6, 7. Write the tests of the pure helpers; see them fail.
- [ ] Implement (a) to (d) inside the discovery path only (nothing outside the discovery gate
      changes; the product path and the records are untouched). Build; suites green.
- [ ] Runs (research section 7, runs 1 to 3; if the driver takes two `DISC=` tokens, add a
      second capture 120 s later to run 2). Gate or NEEDS_RUN.
- [ ] From the captures answer the research's open questions 1 to 5 with files and numbers:
      table texels (range, change per frame, is tf5 `exp(-k d)`), tf16 / tf17 against tf5 and
      {`c68`, tf4}, the sky's textures and constants, the oracle picture untiled and viewed
      (PNG written, described), its relation to the shader output (scale 16).
- [ ] Offline replay: the sky formula of research section 3 on the oracle's sky pixels and the
      fog block on far-terrain pixels where the unfogged colour can be bounded; report medians.
- [ ] Frame map section 16 "Discovery"; commit "Fog discovery: ...".

### Task 2: Formulas and checks (parallel with Task 1; no game)

**Files:** `tools/xdk_sigmatch/light_formulas.py`, `tools/xdk_sigmatch/fog_check.py` (new, from
`out/tests/fog-research/fog_check.py`), `tools/xdk_sigmatch/ucode_eval.py` (only what `F760`,
`A17D`, `F6D9` lack), `tests/test_fog_formulas.py` (new), `docs/native-renderer/lighting-formulas.md`
(part "Fog and sky").

**Produces:** `light_formulas.fog(C, P, eye, c28, c66, c67, c68, tf4, tf5) -> C'` with
`tf4(u)`, `tf5(u)` callables (linear, clamped, texel centres as the microcode's `tfetch1D`
addresses them: state the convention and test it); `light_formulas.sky(n, block, tf4, tf13, tf14)`
with `sky_latlong(n) -> (u, v)`, the path lengths and `E`; the list of fogged pixel shaders with
the interpolator that is the world position, as data (`FOG_SHADERS`), for Task 3's flag.

- [ ] Tests first: `fog` on hand cases (inside the start: identity; `T = 1`: identity; `T = 0`:
      `c28.y * F`; non-finite inputs raise or clamp as the product will: state which).
- [ ] `fog_check.py` in the tracked tools reproduces 5,728 of 5,728; a unit test runs a reduced
      trial count on every shader.
- [ ] `sky`: machine check against `F760` on random directions, constants and table texels
      (extend the interpreter if needed); the seam of the lat-long coordinate and the poles get
      explicit trials. If a part cannot be checked, say exactly which instructions.
- [ ] The vertex form of `475E` / `A584` checked the same way (per vertex), so that ruling 4
      rests on a checked reading.
- [ ] Document; commit "Fog formulas: ...".

### Task 3: Fog block and sky block (capture)

**Files:** `src/native/capture/fog_block.h` (new), `src/native/capture/capture.cpp`,
`src/native/render/frame_scene.h`, a generated table of fogged pixel shaders
(`src/native/capture/fog_shader_table.inc` from `FOG_SHADERS`, generator in `tools/xdk_sigmatch/`),
`tests/native/test_fog_block.cpp` (new), README (counters).

**Interfaces (produce exactly):**

```cpp
struct FogBlock {
  float amount[2];        // c28.xy
  float axis[3];          // c66.xyz
  float range[4];         // c67
  float near_colour[3];   // c68.rgb
  float ext[2][3];        // c64.rgb, c65.rgb (the sky's)
  float scatter[64][3];   // tf4 decoded
  float transmit[256][3]; // tf5 decoded, swizzled
  bool valid;
};
struct SkyBlock {
  float c31x, c46x, c47x;
  uint32_t fetch[2][6];   // tf13, tf14
  bool valid;
};
FogBlock MakeFogBlock(const float regs[6][4], const uint32_t fetch4[6], const uint32_t fetch5[6],
                      const uint8_t* tf4_bytes, size_t n4, const uint8_t* tf5_bytes, size_t n5);
SkyBlock MakeSkyBlock(const float c31[4], const float c46[4], const float c47[4],
                      const uint32_t fetch13[6], const uint32_t fetch14[6]);
```

`FrameScene` gains `fog` and `sky`. Validation as spec Design 3. The read happens once a frame
at the first main-scene draw whose pixel shader is in the fog table, with the frame-current
recheck the shadow paths have; the sky block at the first `82217EE8` draw with pixel shader
`F760...` (how the product recognises that draw without decoding its geometry is this task's
design: the shader pointers at the device are readable at any hooked call; state it). Only with
the native view, the light and the fog switch on.

- [ ] Tests of `MakeFogBlock` / `MakeSkyBlock` (valid case from Task 1's real bytes as a fixture;
      every rejection of Review Focus 2).
- [ ] Capture code; counters `fog-block`, `sky-block` by reason in the periodic line.
- [ ] Discovery cross-check: a capture whose rows' `"pfull"` and table dumps are compared with
      the block logged for the same frames (criterion 2). Gate or NEEDS_RUN.
- [ ] Capture cost measured (records on, view off); commit "Fog block: ...".

### Task 4: Native fog

**Files:** `src/native/render/fable2_native_shaders.h`, `clay_pass.cpp/.h`, `clay_logic.h`
(a CPU mirror `FogMirror` of the shader function, tested against `light_formulas.fog` pins),
`composite.cpp` only if a debug view is added, `fable2_native_render.cpp` (cvar
`--fable2_native_fog`), `tests/native/`, `tests/test_fog_literals.py`.

- [ ] Mirror and pins first (pin generator in `tools/xdk_sigmatch/`, as `mask_pins.py`).
- [ ] Table textures (created lazily, uploaded when the block changes, neutral fallback when
      off or invalid: transmittance 1), bound as one texture pair if a binding parameter is
      free; if none is, stop and report the options.
- [ ] `Fog()` in every lit pixel shader at the game's place (spec Design 4), trees included; the
      exposure and the shadow mask untouched. Fog off: programs identical to sub-project 8's
      but for the call (show the comparison).
- [ ] Suites; build; NEEDS_RUN for the controller's software-adapter run; then the three spots
      with fog off and on (3 s brackets), frame rates, regions near and far.
- [ ] Frame map "Native fog"; commit "Native fog: ...".

### Task 5: Native sky

**Files:** as Task 4, plus the texture cache's use for tf13 / tf14.

- [ ] `SkyMirror` on the CPU with pins from `light_formulas.sky`.
- [ ] The sky step: one triangle at the far depth, depth test as the game's (drawn where the
      depth is clear), no write, placed after the opaque records and terrain layers and before
      the first blended record; view ray from the frame's inverse view-projection; decided by a
      pure function with a table test (switch off, no fog block, no sky block, no view, texture
      missing: which picture each gives).
- [ ] Software-adapter run (controller), then the three spots; the sky against the oracle
      (criterion 3 and 4), a shot looking up if a plan can do it.
- [ ] Frame map "Native sky"; commit "Native sky: ...".

### Task 6: Validation and documents

- [ ] Criteria 1 to 9 each with its evidence, verdict words exact (met / not met as written /
      by a substituted measure). Regions file `docs/native-renderer/fog-regions.json`.
- [ ] Quiet A/B of the capture cost and frame rates (controller's runs).
- [ ] Spec status and "Built differently"; frame map section 16 "Validation" and "Limits";
      `lighting-formulas.md`; `user-checks.md` (night, town, interior, weather, looking up,
      dawn or dusk if reachable); README.
- [ ] Commit "Fog validation: ...". Then whole-branch code review and documents review.
