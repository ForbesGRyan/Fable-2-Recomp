# Native renderer post chain (exposure, composite, bloom, sun layer, sky furniture): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. One fresh
> implementer and one fresh reviewer per task; steps use checkboxes.

**Goal:** the native picture goes through the game's own post chain: the game's exposure (its
luminance read back so that it adapts in normal runs), its composite with its table and the
scan-out ramp (10a), its bloom (10b), its sun layer and the sky's furniture (10c).

**Architecture:** a per-frame post block (the composite's, bright pass's and blurs' registers and
the 512-texel table, read at the game's own post draws outside the main scene) carried in the
frame's scene; lit draws write the game's scale `c20.w`; full-screen post steps with one layout of
their own between the scene's last draw and the mapped target (downsample, bright pass, two blurs,
composite into an 8-bit front target, ramp); a quad record for the indexed builder's furniture
draws; a half-size sun layer feeding the bright pass. A frame without a valid block is today's
picture, unchanged.

**Tech stack:** C++ (capture in `src/native/capture`, pass in `src/native/render`, the switch's
seeding in `src/core`), HLSL in `src/native/fable2_native_shaders.h`, Python tools in
`tools/xdk_sigmatch`, tests in `tests/` and `tests/native/`.

**Spec:** `docs/superpowers/specs/2026-10-09-native-renderer-post-design.md` (its Design and
ruling numbers are cited). Research: `.superpowers/sdd/post-research.md` (read it before any task;
its section and run numbers are cited; its scripts are in `out/tests/post-research/`).

## Global Constraints

- Branch `native-renderer-post`. Never push, stash, reset or switch branches. Never stage
  `fable_2_manifest.toml`; never `git add -A`; `*.ps1` needs `git add -f`. Two implementers at
  work: `git commit --only -F <message file> -- <your paths>`. Commit trailer, exactly:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_017dE9KBtjdEVL4shAyv2Ny8`. New files have LF
  line endings.
- **No SDK change** (`thirdparty/rexglue-sdk`) unless spec ruling 8's condition is met in Task 1
  (the application cannot compile or link against `rex/graphics/graphics_system.h` and
  `rex/graphics/command_processor.h` for the ramp). Then, and only after the controller's word:
  one accessor, `bool rex::graphics::GetGuestGammaRamp256(uint32_t out_rgb10[256])`, declared in
  `include/rex/graphics/native_guest_renderer.h`, its decorated name added to
  `src/system/rexruntime.def` (**no `;` comment lines in that file**), and the SDK rebuilt with
  `tools\build_runtime_sdk.cmd C:\Users\Ryan\code\Fable-2-Recomp\out\sdk-install\win-amd64`
  (always with that argument). Any other seeming need for an SDK change: stop, report.
- The game starts only through `tools/drive_game.ps1`, with `-BootSave Hero000`. Agents never
  pass `-UserPresent`: idle gate, or NEEDS_RUN with the exact command lines below. Runs by the
  controller with the user present are labelled `RUN UNCHECKED`. Every run is checked for the
  freeze signature before a number or picture is used.
- A build that changes bindings, targets or shader text is not run on the real GPU before one
  clean 240 s software-adapter run with the debug layer (the controller makes it; line "WARP"
  below). Tasks 5, 6, 8, 13, 14, 15, 16, 17 are such builds.
- Timing only from a quiet machine. Shot brackets for anything that moves with the sun: 3 s.
- Evidence rule (spec criterion 21). Evidence goes to `out/native-evidence/post/`.
- Guest reads are bounded and fail closed; no value from the guest reaches a shader without a
  finiteness check and a bound; an address is followed only for a fetch constant that is its
  pin. `DrawRecord` stays 616 bytes. Nothing is read with the native view off or with the
  phase's switch off; view-off capture median stays under 0.5 ms.
- Nothing assumes the game's resolution: every new target's size is `ClayPass::kWidth` /
  `kHeight` divided by 1, 2 or 4, with a static assert that the division is exact.
- A frame is either a post frame or today's frame, decided once before its constants are
  written (`DecideClayPost`): never the game's scale under the old mapping, never 2.22 under the
  composite.
- Tests first where a pure function can hold the rule; a Python reference, machine-checked
  against the shader dumps, and a C++ mirror pinned to it come before any shader text.
  Antivirus and shell rules: the workspace's `implementer-rules.md`.
- New reason strings: `post-block`, `post-ramp`, `post-objects`, `quad-record`, `quad-texture`,
  `sun-layer`.
- New switches: `--fable2_exposure_readback`, `--fable2_native_post`, `--fable2_native_bloom`,
  `--fable2_native_sun` (each read with the light switch's parser: on, true, 1, off, false, 0);
  the diagnostic `--fable2_native_post_oracle=<prefix>`.

**Run lines** (after `tools\drive_game.ps1 -BootSave Hero000`; the controller adds
`-UserPresent`; `<args>` is appended to `-GameArgs` after a comma):

| Name | Arguments |
|---|---|
| VIEWS | `-Total 80 -Shots "36,42,48,54,74" -Plan "32:VIEW=off,38:VIEW=native,44:VIEW=split,50:VIEW=overlay,56:VIEW=native,60:DISC=8/1" -GameArgs --fable2_native_render=true` |
| BRIDGE | `-Total 90 -Shots "36,39,42,45,48,51,54,57,86" -Plan "32:VIEW=off,37:VIEW=native,40:VIEW=off,43:VIEW=native,46:VIEW=off,49:VIEW=native,52:VIEW=off,55:VIEW=native,60:DISC=8/1" -GameArgs --fable2_native_render=true` |
| THIRD | `-Total 94 -Shots "44,47,50,53,56,59,90" -Plan "33:LS=0/-100/3500,40:VIEW=off,45:VIEW=native,48:VIEW=off,51:VIEW=native,54:VIEW=off,57:VIEW=native,64:DISC=8/1" -GameArgs --fable2_native_render=true` |
| TREES | `-Total 108 -Shots "58,64,70,76,88,92,98" -Plan "33:LS=0/100/3000,37:RS=-60/0/1500,40:LS=0/100/2500,47:RS=-60/0/600,50:RS=60/0/600,54:VIEW=off,60:VIEW=native,66:VIEW=split,72:VIEW=overlay,78:VIEW=native,82:DISC=8/1,86:VIEW=off,90:VIEW=native" -GameArgs --fable2_native_render=true` |
| SUNWARD | `-Total 80 -Shots "44,47,50,76" -Plan "32:VIEW=off,33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/14000,45:VIEW=native,48:VIEW=off,51:VIEW=native,52:DISC=8/1" -GameArgs --fable2_native_render=true` |
| WARP | `-Total 240 -Shots "225" -Plan "32:VIEW=native" -GameArgs --fable2_native_render=true,--d3d12_adapter=-2,--d3d12_debug=true,--fable2_native_d3d_messages=true` |
| WARP-SUN | `-Total 240 -Shots "225" -Plan "33:RS=-100/0/500,36:RS=-100/0/500,41:RS=0/100/14000,45:VIEW=native" -GameArgs --fable2_native_render=true,--d3d12_adapter=-2,--d3d12_debug=true,--fable2_native_d3d_messages=true` |
| READBACK | the suffix `--readback_resolve=full,--native_render_collect_stats=true` |

## Review Focus

1. **A frame without post** (menus, loading, the first frames, the light off, no light block, a
   block that failed a rule or the pairing, a block older than 4 frames, no ramp, post objects
   that could not be made, no frame constant buffer): today's picture exactly, counted by
   reason; never a stale table beyond the bound, never mixed scales (Global Constraints).
2. **Values that are no numbers, and indices.** A NaN, an infinity or a negative value in the
   linear target, `L = 0` in the bright pass, a table texel or register at its bound, a ramp
   entry out of order: every mirror and every shader step gives a number in its target's range,
   and no array index (the table's 512, the ramp's 256) can leave its array, a NaN's included. A
   test feeds each.
3. **Targets and states.** No target is read and written in one step (the front targets'
   exchange, the blurs' two targets, the radial blur's); every target is back in its resting
   state at the end of `Render`; the mapped target is written in every `Render`, post or not;
   objects are recreated with the pass's others (another device, the F6 retry); a post object
   that cannot be made turns post off (`post-objects`), not the renderer; the debug layer is
   clean on the software adapter.
4. **Switches and their combinations**: post off is the fog's picture; bloom off is 10a's; sun
   off is 10b's; each also with the light, the shadows or the fog off; the oracle feed reaches
   nothing without its switch.
5. **Guest reads**: the table's 2,048 bytes and a quad's storage are read whole or not at all,
   after their pins; the frame is rechecked under the scene's mutex before a block or a record
   is taken; a pending quad of a frame that ended is dropped and counted; the cost of looking at
   every `BeginVertices` call outside the main scene.
6. **Sizes**: fractions of the logical size everywhere (shaders included: texel sizes come from
   constants, not literals).
7. **Evidence**: numbers name their files; what a run did not show is said.

---

## Phase 10a: exposure and composite

Tasks 1 and 3 run in parallel (Task 3 needs no game and none of Task 1's files). Task 2 follows
Task 1's run 1. Tasks 4 to 7 follow in order.

### Task 1: Discovery (capture only): resolves, post buffers, the ramp, the adaptation

**Files:** `src/native/capture/capture.cpp`, `src/native/render/texture_decode.h` (with
`tests/native/test_texture_decode.cpp`), `src/native/fable2_native_render.cpp` / `.h`,
`src/native/native_render_state.h` (with `tests/native/test_native_render_state.cpp`),
`src/core/fable_2_app.h`, `tools/xdk_sigmatch/post_tables.py` (new), `tests/test_post_tables.py`
(new), `docs/native-renderer/frame-map.md` (new section 17 "The post chain", before "Pending";
subsection "Discovery").

**Consumes:** the discovery path as it is (`WriteVertsRow`, `WriteIndxRow`, `DumpTexture`,
`render::DumpKeyedByContent`); `xdk::kDeviceCommandWriteOffset`; the SDK's
`rex::graphics::GraphicsSystem::command_processor()` and
`CommandProcessor::gamma_ramp_256_entry_table()`.

**Produces:**
- Discovery rows `{"kind":"resolve","frame":N,"n":K,"args":[r4..r10],"surface":"0x..",
  "cmd_ok":bool,"cmd":"<hex dwords>"}` for every outermost call of `DrawIndx2:821EF988`: the
  dwords the builder wrote between the device's command write pointer at the hook's entry and at
  its return, at most 512; `cmd_ok` false when the pointer did not advance by 1 to 512 dwords.
- `render::PostBufferBase(uint32_t base) -> bool` (pure): the front buffer `0x1ECFF000` and
  bases in [`0x12A00000`, `0x12E00000`); `DumpKeyedByContent` also true for a 2D texture with
  such a base. `kMaxContentTextures` 1024.
- A memory dump for outcome (ii) below: under a capture, at the first frame's end and the
  second's, `[0x12A00000, 0x12E00000)` as `native_tex_<stamp>/mem_12A00000_f<N>.bin` (pages that
  cannot be read as zeros), only when `--readback_resolve` is not `none`.
- `fable2::native::SetGammaRampReader(bool (*reader)(void* user, uint32_t out_rgb10[256][3]),
  void* user)`; the application registers one built on the SDK's accessors. `PostRamp
  MakePostRamp(const uint32_t rgb10[256][3])` (pure, `native_render_state.h`): `float
  rgb[256][3]` = entry / 1023, `valid` iff every entry is at most 1023 and no channel decreases.
  The post-processor callback reads it each composite frame and logs
  `[native] ramp: read at clay frame N: valid V, <256 x rrrgggbbb as hex>` at the first read and
  at every change, and `[native] ramp: changes C` with the 300-frame lines.
- `post_tables.py` with subcommands `resolves` (PM4 type-0 packets of a row's `cmd` to register
  writes; prints each resolve's `RB_COPY_DEST_BASE`, pitch, format, the draw packet's opcode and
  initiator, and the `"verts"` row before it with its pixel shader), `table` (a capture's table
  per frame: `decode_table(bytes) -> 512 floats`, `c20.w`, the composite's registers, `T` at
  scene luminance 0.01 to 3.99 as research section 3 prints them), `ramp` (the log's table
  against `ramp_092959.txt`'s pairs), `find-luminance` (outcome ii), `downsample` (question 6),
  each with tests on hand-made bytes.

- [ ] Read research sections 1, 3, 6, 7 and spec "Known open before discovery". Write the tests
      of `PostBufferBase`, `MakePostRamp`, `decode_table` and the packet decoder; see them fail.
- [ ] First, before anything else is built: a ten-line trial that the application compiles and
      links with the two SDK headers and reads one ramp entry. If it does not: report
      NEEDS_CONTEXT with the compiler's or linker's message (Global Constraints: the accessor);
      go on with everything else.
- [ ] Implement the rows, the content rule, the memory dump and the ramp line inside the
      discovery gate and the log (nothing the pass draws changes; the records are untouched).
      Build; suites green.
- [ ] Runs (gate or NEEDS_RUN), all under full read-back (run 2 differs from the research's
      line in one token, `32:VIEW=native` for `32:VIEW=off`: the ramp is read in the
      post-processor callback, which runs only with a composite view):
      - run 1, bridge, long: `-Total 120 -Shots "36,42,48,54,60,116" -Plan "32:VIEW=off,38:VIEW=native,44:VIEW=off,56:VIEW=native,64:DISC=40/1" -GameArgs --fable2_native_render=true,--readback_resolve=full,--native_render_collect_stats=true`
      - run 2, adaptation: `-Total 100 -Shots "44,50,56,62,96" -Plan "32:VIEW=native,36:DISC=4/1,40:RS=-100/0/500,41:DISC=60/1,43:RS=-100/0/500,70:DISC=4/1" -GameArgs --fable2_native_render=true,--readback_resolve=full,--native_render_collect_stats=true`
        (three `DISC=` tokens: two are known to work; if the third is refused, drop `70:DISC=4/1`)
      - run 5a, third spot: THIRD with READBACK; run 5b, under the trees: TREES with READBACK.
- [ ] Question 1, the luminance resolve's address A. Outcome (i): `resolves` names, in every
      frame of run 1, one resolve after the `"verts"` row of `5821366EE8FDE893` with one
      destination base: that is A; record its pitch, format, the draw packet (opcode, primitive
      type, index count: the SDK forces only `PM4_DRAW_INDX_2`, a rectangle list, 3 indices) and
      every other resolve whose base lies in [A, A + 64 KiB). Outcome (ii), no copy register in
      the rows' dwords: `find-luminance` over the memory dump, page-aligned bases, a plane of
      280 x 180 big-endian floats at pitch 320 (as stored and untiled) that correlates above
      0.99 with `lum` of the 4 x 4 downsample of the same frame's oracle scene: that base is A,
      stated as found by content. Outcome (iii), neither: say so; Task 2 is BLOCKED and 10a goes
      on blind.
- [ ] Questions 2, 4, 5, 6 with files and numbers: `c20.w` and the table frame by frame through
      run 2's two turns (frames from the turn to the first move, change a frame, whether the
      bright pass's `c77.y * c20.w` leaves 1 by more than 1e-3 in any frame: spec ruling 11); the
      ramp's 256 entries, the pairs of criterion 4, and whether it changed in any run; the table
      and `c20.w` under the trees and at the third spot against `MakePostBlock`'s bounds of
      Task 4 (a value outside them is reported to the controller before Task 4 starts); the
      downsample buffer by content against the CPU downsample with the pixel position integral
      and at the pixel's centre (median difference of each; if the buffer's memory holds neither,
      say so: the four-texel form then stands on variant B's fit).
- [ ] The post rows' order in a frame against the frame row (spec "Frame pairing"): in 40 of 40
      frames of run 1 the composite's row precedes the frame row of the same frame.
- [ ] Frame map section 17 "Discovery"; commit "Post discovery: ...".

### Task 2: The luminance read-back switch (the user's first wish; after Task 1's run 1)

**Files:** `src/core/readback_force_list.h` (new, pure), `src/core/fable_2_app.h`,
`src/native/fable2_native_render.cpp` / `.h`, `tests/native/test_readback_force_list.cpp` (new;
a line in `tests/run_native_tests.cmd`), README (the switch), frame map section 17 "Exposure
read-back".

**Consumes:** Task 1's A, the resolve's packet facts and its neighbours within 64 KiB.

**Produces:** `inline constexpr uint32_t fable2::kLuminanceResolveBase = <A>;`
`std::string fable2::ForceReadbackList(bool hero_dog, bool exposure)` (`""`, `"0x12704000"`,
`"0x<A>"`, `"0x12704000,0x<A>"`); the string switch `fable2_exposure_readback` (defined with the
other `fable2_*` switches, default `off` in this task's first commit) and
`bool fable2::native::ExposureReadbackWanted()`; `OnPostSetup` seeds
`readback_resolve_force_addresses` from `ForceReadbackList` under the existing condition (the
switch still at its default source) and logs both inputs.

- [ ] Test of `ForceReadbackList` first (four cases; the list parses with the SDK's rule: comma,
      `0x` hex). Implement; suites; build.
- [ ] Runs (quiet machine for the frame rates): VIEWS with `--fable2_exposure_readback=on`;
      VIEWS with `=off`; `-Total 70 -GameArgs --fable2_native_render=false,--fable2_exposure_readback=on`;
      the same with `=off`.
- [ ] Criterion 2 from the two captures (`post_tables.py table`) and the log's `Forced
      readback-resolve` lines. Rules for each outcome:
      - adapts (`c20.w` below 12, `T(3) / T(0.1)` at least 1.2) and the bridge holds 30.0 fps in
        every steady window of the on-runs: the default becomes `on` (a second commit of this
        task, after the controller's word: ruling 2);
      - adapts, under 30.0 fps: default stays `off`, reported with the windows;
      - `c20.w` stays 16 and the log has no forced line: the SDK's rule does not match this
        resolve; BLOCKED with the packet's facts (the controller decides: an SDK change, or
        `--readback_resolve=fast` for everyone);
      - `c20.w` stays 16 with the forced line present: the CPU reads another place; back to
        Task 1's rows for the float planes that no pass samples, at most two further addresses,
        then report.
- [ ] The emulated picture with the switch on against a full read-back run: the 36 s shot's
      regions against run 1's 36 s shot (sunlit regions within 2%, the sky's hue within 3
      degrees expected; reported whatever it is). What else is read back within 64 KiB of A, per
      frame, from the SDK's lines.
- [ ] Commit "Exposure read-back: ...".

### Task 3: Formulas, checks and the replay (parallel with Tasks 1 and 2; no game)

**Files:** `tools/xdk_sigmatch/light_formulas.py`, `tools/xdk_sigmatch/post_check.py` (new, from
`out/tests/post-research/post_check.py` and `post_formulas.py`), `tools/xdk_sigmatch/post_replay.py`
(new, from `replay.py`), `tools/xdk_sigmatch/post_pins.py` (new, as `fog_pins.py`),
`tools/xdk_sigmatch/ucode_eval.py` only for what a post shader lacks, `tests/test_post_formulas.py`,
`tests/test_post_replay.py` (new), `docs/native-renderer/lighting-formulas.md` (part "The post
chain").

**Produces** (in `light_formulas.py`; scene and table values as floats, colours as 3-tuples):
`POST_LUM = (0.2125, 0.7154, 0.0721)`; `post_lum(c)`; `post_table_texel(u) -> int` (the point
fetch's texel of a 512-texel 1D table for coordinate `u`, with the interpreter's fetch
convention stated in its text and pinned at `u = k / 512 +- 1e-6` for k = 0, 1, 2, 12, 128,
511, 512); `post_composite(scene, bloom, table, c31x, c46x, c47x, c77, c78x)`;
`post_downsample(tex, x, y, bias)` (`bias` 0: the pixel position integral; 0.5: its centre);
`post_bright(d, tf1, tf2, c31x, c46x, c47x, c77)` (`L = 0`: the scene term is 0);
`post_blur13(tex, x, y, weights7, vertical)`; `post_radial(tex, u, v, c31, c46x, c47x)`;
`post_reduce(tex, x, y)`; `post_ramp(level, ramp)`; `post_unlit_scale(c77y, table)` =
`1 / max(c77y * table[128], 1 / 64)`; `POST_SHADERS` (the eight hashes in full with their
roles). `post_replay.py`: `replay <capture> <frame> --variant A|B|N` (N: today's mapping,
`sqrt(sat(scene / c20.w * 2.22))`), `predict` (per region: variant A or B after the ramp over N),
`export <prefix>` (Task 6's two files) and `compare <dump> <picture>` (median, 90th percentile,
share within 1, 2 and 8 levels, by region).

- [ ] Tests first: hand cases of each function (the composite with a flat table is one gain; a
      table of zeros is black; `bloom = 0` leaves `c`; NaN, infinity and negative inputs behave
      as the product will: each clamped as spec Design 5 and 10 say; state it in the text).
- [ ] `post_check.py` in the tracked tools reproduces 2,000 of 2,000 for the eight shaders
      (criterion 1); a unit test runs a reduced count.
- [ ] `post_replay.py` reproduces the research's variant A and B figures on `092959`, `121331`,
      `132250` frame 1 (criteria 5 and 11; the captures are in
      `out/build/win-amd64-release/logs/`); a test holds the arithmetic on a stored 16 x 16
      excerpt (scene, table, expected front values) that the tool writes once.
- [ ] `post_pins.py` writes the pinned numbers Tasks 5 and 8 hold their mirrors to.
- [ ] Document (the eight formulas, the table's addressing, what is inference: the downsample's
      pixel convention, the clamps of the game's targets); commit "Post formulas: ...".

### Task 4: Post block (capture)

**Files:** `src/native/capture/post_block.h` (new, pure), `src/native/capture/capture.cpp`,
`src/native/capture/capture.h`, `src/native/render/frame_scene.h`,
`src/native/native_render_state.h`, `src/native/fable2_native_render.cpp` (the switch
`fable2_native_post`, off; `SetPostWanted` in `PollFrame`'s documented order of stores),
`tests/native/test_post_block.cpp` (new), `tests/native/post_table_fixture.inc` (a real table
from run 1), `tests/native/test_frame_scene.cpp`, `tools/xdk_sigmatch/post_tables.py` (`block`),
README (counters), frame map "Post block".

**Interfaces (produce exactly):**

```cpp
inline constexpr uint64_t kPostCompositePs = 0x37B186F4BD3288F7ull, kPostBrightPs = 0xEE922CFB4426EA83ull,
                          kPostBlurXPs = 0xEA83C7E7E24EC4C8ull, kPostBlurYPs = 0x28DBE18F313350BFull,
                          kPostRadialPs = 0x4BE4344C27016E04ull;
inline constexpr uint32_t kPostTableSlot = 1, kPostTableTexels = 512, kPostBlockMaxAge = 4;
inline constexpr size_t kPostTableBytes = 2048;
inline constexpr uint8_t kPostPartBlur = 1, kPostPartRadial = 2;
struct PostBlock {
  float composite[6];   // 37B1: c31.x, c46.x, c47.x, c77.x, c77.y, c78.x
  float bright[5];      // EE92: c31.x, c46.x, c47.x, c77.x, c77.y
  float blur[2][7];     // EA83 then 28DB: c46.x (centre), c46.y, .z, .w, c47.x, .y, .z
  float radial[4];      // 4BE4 (Task 12): c31.x, c31.y, c46.x, c47.x
  float table[512];     // the composite's table, one big-endian float a texel
  uint8_t parts;        // kPostPartBlur, kPostPartRadial
  bool valid;
};
static_assert(sizeof(PostBlock) == 2168);
struct PostRegisters {
  float composite[5][4];   // 37B1's c31, c46, c47, c77, c78
  float bright[4][4];      // EE92's c31, c46, c47, c77
  float blur[2][2][4];     // EA83's c46, c47; 28DB's c46, c47
  bool have_composite, have_bright, have_blur[2];
};
enum class PostBlockReason : uint8_t;   // none, off, no-draw, unread, register, range, pairing,
                                        // table-<property> as the fog's, table-bytes, table-texel, stale
PostBlock MakePostBlock(const PostRegisters& regs, const uint32_t fetch1[6],
                        const uint8_t* table_bytes, size_t n, PostBlockReason* why);
bool PostBlockPairs(const PostBlock& block, float out_scale);   // |bright c77.y * out_scale - 1| <= 1e-3
struct PostSource { uint32_t base = 0; uint64_t content = 0; };
```

`MakePostBlock`'s rules, in this order: the composite's and the bright pass's registers were
read (`unread`); every component it takes is finite (`register`); `range`: composite `c31.x` in
[0, 64], `c46.x` in (0, 64], `c47.x` in [0, 1e6], `|c77.x|` <= 4, `c77.y` in (0, 64], `c78.x` in
(0, 1]; bright `c31.x` in [0, 64], `c46.x` in [0, 64], `c47.x` in [1e-3, 64], `|c77.x|` <= 4,
`c77.y` in (0, 64]; the fetch constant is the table's pin (a texture constant, 1D, an address,
width 512, format 36 with number format bit 1, endian 2, swizzle `0xB68`, sign fields `0x55`,
exponent adjust 0, clamp to edge, magnification and minification point, highest mip level 0; the
captured constant is `04000956 1FAA00A4 000001FF 010016D1 00000000 00000018`); `n` is 2,048 with
the bytes there; every texel finite and in [0, 4) (`table-texel`). The block carries the texels as
they are stored; the composite reads the nearest one, `floor(u * 512 + 0.5)` (controller ruling
19), which is the reader's rule (Task 5), not the block's. The blur part
(`kPostPartBlur`) is set when both blurs were read, every weight is in [0, 1] and each set's
`w0 + 2 (w1 + .. + w6)` is 1 within 1e-3; without it the block is still valid. An invalid block
is all zero.

`FrameScene` gains `post` (`PostBlock`), `post_reason`, `post_age` (0: the frame's own),
`post_source`, `post_records`. `FrameBuilder` gains `SetPost`, `CountPostTry`, `PostOn` and keeps
the last valid block across frames; `Finish` decides (pure, under test): the frame's own block
if it is valid and `PostBlockPairs` with the frame's light block's `out_scale`; else the kept one
if its age is at most `kPostBlockMaxAge` and it pairs (`post_age` = its age); else none, with
`pairing`, `stale`, the read's reason, or `no-draw`. A frame without a valid light block has no
post block.

**The read** (`ObservePostDraw` in `capture.cpp`): only in a frame with records and with post
wanted (latched at the frame's boundary with the records, as the fog is); at an outermost
`kHook_D3DDevice_BeginVertices` call outside the main scene; the pixel shader's hash through
`ChoosePs` / `LookupPs` as `ReadSkyBlock` does; at the bright pass and the two blurs the
registers go into a thread-local `PostFrame` (reset by the frame counter, as `FogFrame`); at the
composite its registers, the fetch constant of slot 1 and, only for a constant that is the pin,
the 2,048 bytes (copied first; the copy is hashed for `PostSource` and decoded); then the block
is made and handed over under the scene's mutex after the frame recheck. At most 8 tries a
frame. `OnXdkReturn` calls it beside `ObserveShadowDraw` (both, when both are wanted).

- [ ] Tests of `MakePostBlock`, `PostBlockPairs` and `Finish`'s decision first (the valid case
      from the fixture; every rule's rejection; Review Focus 2's inputs; own, kept at ages 1 to
      5, kept and not pairing).
- [ ] Capture code; the periodic line `[native] capture: frame N post: block valid, age A,
      tries T, c20.w .., 37B1 (..), EE92 (..), table 0x<base> <hash> | post-block in F of W
      frames {reason: n} | dropped at a frame's end D`; the discovery frame row's `"post"`.
- [ ] Run: VIEWS with `--fable2_native_post=on`, READBACK (nothing is drawn with the block yet;
      the picture must be unchanged). `post_tables.py block`: criterion 3 on its 8 frames.
      Capture cost, records on and view off, against Task 2's off-run.
- [ ] Commit "Post block: ...".

### Task 5: Native composite and ramp

**Files:** `src/native/fable2_native_shaders.h`, `src/native/render/clay_logic.h`,
`src/native/render/clay_pass.cpp` / `.h`, `src/native/fable2_native_render.cpp`,
`tests/native/test_clay_logic.cpp`, `tests/native/test_native_shaders.cpp`,
`tests/test_post_literals.py` (new, as `test_fog_literals.py`), README, frame map "Native
composite".

**Consumes:** `FrameScene::post`, `post_age`; `PostRamp`; `light_formulas.post_composite`,
`post_table_texel`, `post_ramp`, `post_unlit_scale` and `post_pins.py`'s numbers.

**Produces** (`clay_logic.h`, section "Native post"):
- `enum class ClayPost : uint8_t { kOff, kNoLight, kBlock, kRamp, kObjects, kBuffer, kDrawn, kCount }`
  and `constexpr ClayPostDecision DecideClayPost(const ClayPostInputs&)` (switch, light on, the
  frame lit, block valid, ramp valid, objects made, the frame's constant buffer present).
- `float PostUnlitScale(const PostBlock&)`; `uint32_t PostTableTexel(float u)`;
  `void PostCompositeMirror(const float scene[3], const float bloom[3], const ClayPostConstants&,
  float out[3])`; `float PostRampMirror(float front, const float ramp[256])`; each the
  reference's rule term for term.
- `struct ClayPostConstants` (cbuffer `Post` at b0 of the post layout; `kClayPostFields` holds
  its offsets against the compiled shaders): the composite's six registers, the scene's clamp
  31.875 and the previous-frame factor (0 until Task 15); the bright pass's five registers and
  two flags (bloom on, sun on); `float4 p_blur[4]`; `float4 p_radial`; `float4 p_table[128]`
  (texel i at `[i >> 2][i & 3]`); `float4 p_ramp[256]` (rgb). 6,288 bytes;
  `kClayPostSlots = 25` small slots; `PlanClayFrameSlots` gains `post` between `sky` and
  `object0`, `ClayFrameSmallSlots` 25 more.
- `ClayFrameConstants`: the word at byte 60 (`f_pad0`) becomes `f_post`: the unlit scale of a
  post frame, 1 in every other frame (never 0); `exposure` is the light block's `out_scale` in a
  post frame and `kLitExposure` otherwise.
- `shaders::kClayPostCompositePs`, `kClayPostRampPs` (drawn with `kFullscreenVs`); the unlit colour paths of the
  four scene pixel shaders multiply by `f_post` before their fog.
- `ClayPass`: `EnsurePostObjects` (made on the first post frame; a failure sets
  `post_objects_failed_` and counts `post-objects`): the post layout (parameter 0 constant
  buffer b0, 1 constants b1 of 8 words, 2 a texture table t0 of 4; static samplers s0 point
  clamp, s1 linear clamp; all pixel stage), the front target (`kWidth` x `kHeight`,
  `R8G8B8A8_UNORM`) and its view, the two pipelines; `Render` takes a `const ClayPostInputs&`
  (post, bloom and sun switches, the ramp, the oracle feed of Task 6) and ends with either the
  two post steps or the old mapping; `ClayStats` and `FormatPostText` (the Post line: the
  verdict, the block's age, `c20.w`, the unlit scale, frames without post by reason).

- [ ] Mirrors and pins first; the decision's table test (every input combination); the slot
      plan's test extended; the frame constants' test (`f_post` 1 without post, in a frame
      without light too).
- [ ] Shader text. The composite: scene by `Load`, a value not above 0 is 0, `min(.., 31.875)`;
      the table's index `min(511, uint(512 * saturate(c46.x * L) + 0.5))`: the nearest texel, the
      convention Task 3 pinned (controller ruling 19; not the floor); the power as `exp2(c78.x * log2(|t|))` with 0 for 0; alpha 1. The ramp: `uint(v *
      255 + 0.5)` clamped to 255, each channel through `p_ramp`. Shader-test cases: the fields'
      offsets, the compiled clamps and index bounds, the lit programs of a frame without post
      line for line today's, the unlit programs today's but for the one multiply (criterion 8's
      first half).
- [ ] The pass: the decision before `MakeClayFrameConstants`; barriers (front: resting in
      `kPixelShaderResource`); the Post line every 300 frames and once at the first post frame
      and at every change of verdict.
- [ ] Suites; build; NEEDS_RUN: WARP with `--fable2_native_post=on` (controller); then BRIDGE
      and THIRD, each with `--fable2_native_post=off` and with `=on`, READBACK appended to all
      four.
- [ ] From the runs: the Post line's counts; frame rates and capture medians (criterion 9, as
      measured on that machine); shots viewed and described, what looks worse included (the
      clear colour near black: table texel 1, the placeholders under ruling 9); `post_replay.py predict` against
      the on / off region ratios (criterion 7, first reading); the off-run's regions against the
      fog's final binary's (criterion 8's second half: within the spread of the two runs'
      emulated shots, controller ruling 22; not the 0.002 the criterion first named).
- [ ] Frame map; commit "Native composite: ...".

### Task 6: Oracle feed and target dump (diagnostic)

**Files:** `src/native/render/post_oracle.h` (new, pure), `src/native/render/clay_pass.cpp` /
`.h`, `src/native/fable2_native_render.cpp` (the switch `fable2_native_post_oracle`),
`tests/native/test_post_oracle.cpp` (new), `tools/xdk_sigmatch/post_replay.py` (`export`,
`compare`), frame map "Oracle feed".

**Produces:** the file formats, little-endian: `<prefix>.scene` = `"F2OS"`, u32 width, u32
height, then width x height x 3 float32 (the oracle scene in the game's scale); `<prefix>.block`
= `"F2OB"`, u32 1, float32 `c20.w`, then `PostBlock`'s 541 floats in the struct's order, u32
parts. `OracleScene ParseOracleScene(const uint8_t*, size_t)` (the size must be the logical
size; every value finite, else 0), `PostBlock ParseOracleBlock(...)` (through `MakePostBlock`'s
rules), `uint16_t HalfFromFloat(float)` with a test against `HalfToFloat`. In the pass: with the
switch set and both files valid, the scene is uploaded once as a `kWidth` x `kHeight`
`R16G16B16A16_FLOAT` texture and every post step reads it in place of the linear target, with
the file's block and the live ramp; 30 frames later the mapped target is copied to a read-back
buffer and written once to `logs/native_post_dump_<stamp>.rgba` (`"F2OD"`, width, height, RGBA
bytes) after its submission has completed. The mapped target gains `kTextureUsageCopySource`.
Without the switch nothing of this is reached.

- [ ] Tests of the parsers first (short files, wrong magic, wrong size, NaN, a block that fails
      a rule). Implement; suites; build.
- [ ] `post_replay.py export` for frame 1 of `092959`, `121331`, `132250`, `134312` into
      `out/native-evidence/post/oracle/`.
- [ ] NEEDS_RUN: WARP with `--fable2_native_post=on,--fable2_native_post_oracle=C:\Users\Ryan\code\Fable-2-Recomp\out\native-evidence\post\oracle\092959_f1`;
      then on the real GPU `-Total 60 -Shots "40,50" -Plan "32:VIEW=native"` with the same
      `-GameArgs` less the three software-adapter switches, once for each of the three bridge
      prefixes.
- [ ] `post_replay.py compare` of each dump against the reference's variant A picture through
      the ramp of the same run's log (criterion 6) and against the front buffer through it
      (criterion 5's figures, now from the GPU). A difference beyond criterion 6 is a defect of
      the shader or of the reference's addressing: find which before going on.
- [ ] Commit "Post oracle feed: ...".

### Task 7: Validation and documents (10a)

- [ ] Runs by the controller on a quiet machine: BRIDGE and THIRD with `--fable2_native_post=on`
      and with `=off`, each with READBACK and each without (eight runs; the read-back switch at
      Task 2's default); TREES with `=on`.
- [ ] Criteria 1 to 10 each with its evidence, verdict words exact (met / not met as written /
      by a substituted measure). Regions: the existing three files.
- [ ] The default of `--fable2_native_post`: on if criteria 3, 6, 8, 9 and 10 are met (the
      controller's ruling, recorded in the spec's "Implementation notes"; the last code change
      of the phase).
- [ ] Spec status and "Built differently"; frame map section 17 "Validation (10a)" and
      "Limits"; `lighting-formulas.md`; `docs/native-renderer/user-checks.md` from check 48 on
      (the exposure adapting while walking from shade into sun and back; a brightness option in
      the game's menu against the ramp's change line; night; a town; an interior; a cutscene or
      a loading screen against the Post line's reasons; the switches off); README.
- [ ] Commit "Post validation (10a): ...". Then the phase's review.

## Phase 10b: bloom

Task 11 (10c's formulas) may run in parallel with Tasks 8 and 9: it needs no game and none of
their files.

### Task 8: Native bloom

**Files:** `src/native/fable2_native_shaders.h`, `src/native/render/clay_logic.h`,
`src/native/render/clay_pass.cpp` / `.h`, `src/native/fable2_native_render.cpp` (the switch
`fable2_native_bloom`, off), the three test files of Task 5, README, frame map "Native bloom".

**Consumes:** `PostBlock::bright`, `blur`, `parts`; `light_formulas.post_downsample`,
`post_bright`, `post_blur13`; Task 1's answer to question 6 (`bias`).

**Produces:** `PostDownsampleMirror`, `PostBrightMirror`, `PostBlurMirror` (pinned);
`inline constexpr float kPostDownsampleBias` (0 or 0.5, with the evidence named beside it);
`shaders::kClayPostDownsamplePs`, `kClayPostBrightPs`, `kClayPostBlurPs` (one program, the
direction in the step's words); targets `bloom_d_` (`kWidth / 4` x `kHeight / 4`, `R11G11B10_FLOAT`), `bloom_p_`,
`bloom_q_` (`R10G10B10A2_UNORM`; if the first software-adapter run shows that format cannot be
made a render target: `R11G11B10_FLOAT` with `floor(sat(v) * 1023 + 0.5) / 1023` in the three
programs, spec ruling 14); the composite's `b` and `s` terms on; `ClayPost` gains the bloom's
verdict (`on`, `off`, `no-weights`), counted on the Post line.

- [ ] Mirrors and pins first; criterion 14's test (NaN, infinity, a negative texel, `L = 0`,
      weights at their bounds).
- [ ] Shader text and steps, between the scene and the composite: downsample (four linear
      samples at the reference's coordinates, texel sizes from the step's words; each sample
      taken as 0 unless above 0, at most 31.875), bright pass (tf1 and tf2 unread behind the sun
      flag; the white texture bound there), blur along x (`bloom_p_` to `bloom_q_`), blur along
      y (back), 13 `Load`s with clamped integer coordinates; every step ends in `(v > 0) ?
      min(v, 1) : 0`; the composite samples `bloom_p_` with the linear clamp sampler at the
      scene's interpolated coordinate plus the fetch's half bloom texel (bloom texel coordinate
      `x / 4 + 0.5`: Task 8's finding, spec "Implementation notes", phase 10b; this line first
      said "at the pixel's own normalised coordinate"). Every step into a UNORM target then
      writes the rounded level itself (controller ruling 25).
- [ ] Suites; build; NEEDS_RUN: WARP with `--fable2_native_post=on,--fable2_native_bloom=on`;
      the oracle feed on the software adapter and on the real GPU for the three bridge prefixes
      (Task 6's lines with `--fable2_native_bloom=on` added); BRIDGE with bloom off and on,
      READBACK.
- [ ] Criterion 12 from the dumps; criterion 13's first reading; shots viewed; cost.
- [ ] Frame map; commit "Native bloom: ...".

### Task 9: Validation and documents (10b)

- [ ] Runs on a quiet machine: BRIDGE and THIRD with bloom on and off (post on), each with and
      without READBACK; TREES with bloom on; SUNWARD with bloom on, READBACK (the baseline of
      10c: no beams, no sun).
- [ ] Criteria 11 to 15 with evidence and exact verdicts; the default of
      `--fable2_native_bloom` (on if 12, 14 and 15 are met; the controller's ruling).
- [ ] Spec, frame map "Validation (10b)" and "Limits", `lighting-formulas.md`, user checks (a
      bright sky at dawn or dusk; a torch or a window at night; bloom on against off), README.
- [ ] Commit "Bloom validation (10b): ...". Then the phase's review.

*As done (2026-10-10).* No run was made for this task. The verdicts are read from Task 8's runs
and from the runs of a Task 8b that the ledger added before this one (ruling 29: the base map's
gamma sign; bridge and third spot, bloom on and off, under full read-back, and the bridge with
post off). Not made: the runs with the read-back switch, TREES and a blind run on the binary
with the gamma fix, and SUNWARD (left to 10c's own runs). Criterion 11's figures in the spec are
the research's, with the bloom sampled at aligned texel centres; the reference now samples at
the game's own fetch (ledger ruling 27) and the verdicts are stated with it. The timing runs of
this task and of Task 7 are one list with its decision rules: frame map section 17,
"Outstanding: cost" (every run since log 457 was on a monitor on which timing does not count).
The deferred document fixes of the reviews of Tasks 7, 8 and 8b were made in this task, each as
its own commit.

## Phase 10c: sun layer and sky furniture

Order: what has the fewest unknowns first; each task ends in a working picture. Task 11 is
independent of Task 10.

### Task 10: Discovery (sun layer): the graph, tf1 and tf2, the quads

**Files:** `tools/xdk_sigmatch/post_tables.py` (`graph`, `quads`), `tests/test_post_tables.py`,
`docs/native-renderer/post-regions.json` (new: "sun", "beams left", "beams right", "sky beside
the sun", drawn on run 3's emulated shot), frame map "Sun layer discovery". Capture code only if
a row lacks what a question needs (then inside the discovery gate).

- [ ] Run 3 (controller): SUNWARD with READBACK and `--fable2_native_fog=on`.
- [ ] `graph`: from the resolve rows and the `"verts"` / `"indx"` rows of one frame, every pass
      of research section 1's table with its real destination: which 560 picture each of C1,
      C2, C3 reduces, what B1 and B2 read and write, what the bright pass's tf1 and tf2 hold
      (question 3; by content: the buffers kept at the rows that sample them). If the contents
      cannot tell them apart: by the weights, stated as inference.
- [ ] `quads`: every furniture quad of a frame with its corners (`"indxdata"`), registers,
      textures, blend and depth words, at the bridge (run 1) and towards the sun; the six
      indices' pattern; whether any furniture shader is drawn through another builder.
- [ ] What the three `D1CC` cubes are transformed to (their vertex registers), what the 24 point
      draws write, what the tree occluder draws are (their index counts against the main
      scene's tree draws of the same frame): each identified or said to be not.
- [ ] Commit "Sun layer discovery: ...".

### Task 11: Furniture formulas and the reference sun layer (no game; parallel with Tasks 8 to 10)

**Files:** `tools/xdk_sigmatch/light_formulas.py`, `post_check.py`, `post_replay.py`,
`tools/xdk_sigmatch/gen_post_quad_table.py` (new), `tests/test_post_formulas.py`,
`tests/test_gen_post_quad_table.py` (new), `lighting-formulas.md`.

**Produces:** a function for each of `18D39DCEBEE4C2FC` (moon), `04985A0F7296E130` (glare),
`8E4DBB9E9A63EF67` (beam source), `D72FD0903CCE7DD6` (flare), `4B58837D42F08C89` (moon glow),
`3E77B1AD4BC5B900` (sun disc), `5D6768847B0602B8` (layer depth), `686DE15FC84D8B73`
(previous-frame blend), `61976295517D7015` (clouds), `3FF79524FCDB71C5` (cloud occluder),
`F1EB2497108F159D` (tree occluder), and of the vertex shaders `81BADE63A169E3DC`,
`C304C6A1AF18B46F`, `D1CC6DC28244C55A`; `POST_QUAD_SHADERS` (hash, kind, the pixel registers it
reads, its texture slots, where it is drawn, its blend); `gen_post_quad_table.py` writes
`src/native/capture/post_quad_table.inc` from it; `post_replay.py --variant C` (B with the sun
layer rasterised on the CPU from a capture's quad rows and the oracle's depth).

- [ ] The short shaders first, each machine-checked 2,000 of 2,000 (criterion 16). Then `6197`
      (58 instructions): read in full, written, checked; the interpreter gains what it lacks.
      A shader that does not reach agreement, or reads an input no capture holds: say exactly
      which instructions; it stays out of `POST_QUAD_SHADERS` (spec ruling 18).
- [ ] Variant C on `134312` frame 1 and, when Task 10's capture exists, on its frame 1:
      criterion 18's figures, whatever they are.
- [ ] Document; commit "Furniture formulas: ...".

### Task 12: Quad records and the radial blur's registers (capture)

**Files:** `src/native/capture/quad_record.h` (new, pure), `post_block.h` (`SetPostRadial`),
`post_quad_table.inc` (generated), `capture.cpp`, `frame_scene.h`, `fable2_native_render.cpp`
(the switch `fable2_native_sun`, off; `SetSunWanted`), `tests/native/test_quad_record.cpp` (new),
`tests/native/test_frame_scene.cpp`, `post_tables.py` (`quads --check`), README, frame map "Quad
records".

**Interfaces (produce exactly):**

```cpp
inline constexpr uint32_t kMaxQuadRecords = 24;
enum class PostQuadKind : uint8_t;   // moon, cloud, glare, beam-source, flare, moon-glow, sun-disc, cloud-occluder
struct QuadRecord {
  uint64_t ps_hash, vs_hash;
  PostQuadKind kind;
  uint8_t layer;            // 0 the main scene, 1 the sun layer
  uint16_t sequence;        // its place among the frame's quad records
  float corners[4][5];      // position x y z and u v, as the game filled the storage
  float view_proj[4][4];    // vertex c0..c3 at the call
  float ps[12][4];          // the pixel registers POST_QUAD_SHADERS names for the kind, in its order
  uint32_t fetch[3][6];     // its texture slots' fetch constants, in the kind's order
  uint32_t blend, depth, colour_mask;
};
struct QuadInputs {
  const uint8_t* vertices; size_t vertex_bytes; uint32_t stride;   // the storage as it was read
  const uint16_t* indices; size_t index_count;
  const float (*view_proj)[4];                 // vertex c0..c3
  const float (*ps)[4]; size_t ps_count;       // the kind's pixel registers, in its order
  const uint32_t (*fetch)[6]; size_t fetch_count;
  uint32_t blend, depth, colour_mask;
};
QuadRecord MakeQuadRecord(const PostQuadEntry& entry, const QuadInputs& in, QuadRecordReason* why);
bool SetPostRadial(PostBlock* block, const float c31[4], const float c46[4], const float c47[4]);
```

`MakeQuadRecord`: 4 vertices of 20 bytes and the six indices of Task 10's pattern, or no record;
every float finite and within 1e7; the registers finite. `FrameScene::quads` (a side array
handled as `lights` is, at most `kMaxQuadRecords`; more are counted). The read: at a call of
`DrawIndx:82217EE8` whose pixel shader is in the table, in the scene (`RecordDraw`'s branch
beside `ReadSkyBlock`) or outside it (`ObservePostDraw`), the registers and state at the call;
the storage's pointers are kept in a thread-local pending record, as discovery's
`PendingIndexed`, and the bytes are read when the thread enters its next hooked draw; then the
record is made and handed over under the mutex after the frame recheck. A pending record whose
frame ended is dropped and counted. `SetPostRadial` at the `4BE4` rectangle (`c31.xy` finite and
in [-4, 4], `c46.x` and `c47.x` in (0, 1]). Only with `--fable2_native_sun=on`.

- [ ] Tests first (a valid quad from Task 10's rows as a fixture; every rejection; the bound;
      the dropped pending record).
- [ ] Capture code; the periodic line's quad counts by kind and `quad-record` by reason.
- [ ] Run: SUNWARD and VIEWS, each with `--fable2_native_post=on,--fable2_native_bloom=on,--fable2_native_sun=on`
      and READBACK (nothing is drawn with the records yet). `quads --check`: criterion 17.
      Capture cost against Task 9's runs.
- [ ] Commit "Quad records: ...".

### Task 13: Moon and glare in the scene

**Files:** the shader header, `clay_logic.h`, `clay_pass.cpp` / `.h`, the three test files, frame
map "Scene furniture".

**Produces:** `ClayQuadConstants` (the four corners, their coordinates, the matrix, the kind's
registers, `c20.w`, sampler indices and host texture scales; at most 512 bytes: two small slots
a quad, `PlanClayFrameSlots` gains `quad0` and 48 slots); `shaders::kClayQuadVs` (six vertices
by `SV_VertexID` from the constants) and `kClayQuadPs` (the kind's formula by a word; the moon:
`tex.rgb * c31.rgb * tex.a * c20.w`, alpha `c31.w * tex.a`; the glare: `tex.rgb * c31.rgb`);
`EnsureQuadObjects`: the quad layout (the quad's constants at b0, the frame's block at b2, a
texture table t0 of 3, the four static samplers), its pipelines (alpha blend with the depth test
and no write; additive without a depth test); `QuadMirror` functions pinned to Task 11's
formulas; the textures through the texture cache (`quad-texture` when one cannot be had: the
quad is not drawn). Place: the moon after the sky step, the glare after the last record; both
only in a post frame with a sky.

- [ ] Mirrors, pins and the place's rule first. Shader text; the pass. Suites; build.
- [ ] NEEDS_RUN: WARP with the three switches on; SUNWARD and BRIDGE with sun on and off,
      READBACK.
- [ ] The glare region of the sunward shots against the emulated ones; the moon: whether any
      frame had it above the horizon (if none: said, a user check). Shots viewed. Cost.
- [ ] Commit "Moon and glare: ...".

### Task 14: The sun layer: beams, flares, moon glow, sun disc

> **Amended after the review of Task 10** (2026-10-10; `review-task-10.md`, rulings 33 and 35; where this note and
> the text below disagree, this note holds; the frame map's "Sun layer discovery" and "Sun layer" have the
> findings):
> - tf1 is the beam source, reduced and blurred radially twice; tf2 is layer 2, reduced. C1 (the 8-bit reduction)
>   is not built: two reductions.
> - **The layer's depth is not the scene's end depth in the game.** It is the mid-frame depth copy plus the cloud
>   occluders at their own reference (0.149), the cubes and the late leaves; the scene's own clouds write depth
>   at 0.0196 and are not in it. The native layer takes the native depth after the scene's last record, which
>   no native cloud step may have written, and draws the set's `3FF7` occluders into it.
> - **The `3FF7` cloud occluders are built here**, not in Task 16 (ruling 35): without them the layer is worse
>   than none (61.6% against 63.9%). A layer that lacks an occluder's or the beam source's texture for a frame
>   is not drawn that frame.
> - Before any in-game shot: the oracle feed with its sun part (the beam source's drawn count against the
>   reference's 12,310 on run 3's frame; layer 1, tf1 and tf2 against the reference's).
> - The software-adapter run faces the sun (WARP-SUN stretched: the first execution of any quad program under
>   the debug layer; review of Tasks 12 and 13). Criterion 19 is judged with the occluders in, the beams' root
>   and a far part of a beam apart (regions "beam root", "beam far left", "beam far right").
> - The sun layer's verdict is its own: an undrawable layer record takes the layer away, not the scene's moon
>   and glare.

**Files:** as Task 13; frame map "Sun layer".

**Consumes:** Task 10's graph (tf1, tf2); `PostBlock::radial`; the mirrors of `post_radial`,
`post_reduce` and of Task 11's layer shaders.

**Produces:** targets `sun_depth_` (`kWidth / 2` x `kHeight / 2`, `D32_FLOAT`), `sun_layer_[2]`
(same size, `R10G10B10A2_UNORM`), `sun_small_[3]` (`kWidth / 4` x `kHeight / 4`, same format);
steps before the bloom's bright pass: the layer's depth from the native depth (`5D67`'s rule for
which texel; a depth-only full-screen step writing depth); layer 1 cleared, the beam source
depth-tested; its reduction; two radial blurs between two small targets; layer 2 cleared,
flares (additive, depth-tested), moon glow (alpha blend), sun disc (additive, depth-tested); its
reduction; the bright pass's sun flag on with tf1 and tf2 as Task 10 found. `shaders::kClayPostReducePs`,
`kClayPostRadialPs`, `kClayPostLayerDepthPs`. A frame without the radial part or without its quads:
the sun flag off, counted `sun-layer` by reason.

- [ ] Mirrors and pins first (the radial blur's 14 taps with a centre outside the picture and
      on its edge; the reduction). Shader text; the pass. Suites; build.
- [ ] NEEDS_RUN: WARP-SUN with the three switches on (the camera turned towards the sun; on
      the slow adapter the turn may end elsewhere: the shot says where); then SUNWARD with sun on and off, READBACK; BRIDGE with sun on (the sun behind the camera:
      the picture must be 10b's within criterion 20).
- [ ] Criterion 19 from the sunward shots; the shots viewed and described against the emulated
      ones (beams through leaves, the sun's disc); cost.
- [ ] Commit "Sun layer: ...".

### Task 15: The previous-frame blend (ruling 7)

> **Note after the build** (2026-10-10): the cost was found to be what the first step allows (one more front
> target, the composite's mix, one more texture of the table it had, the constants' float that was reserved),
> so it is built. Additions to the list of frames with `p_prev` 0: the oracle feed (its picture is held
> against a reference without the blend). The switch is `--fable2_native_sun`. The frame map's
> "Previous-frame blend" has the rest.

**Files:** as Task 13.

- [ ] First the cost, in writing: it must be one more front target (`front_[2]`, exchanged each
      `Render`), the composite's `lerp(new, previous, p_prev)` with `p_prev` = 0.102 (0 in the
      first frame after the targets were made, after a frame without post, and with the switch
      off) and one more texture in the composite's table. If the implementer finds it needs
      more than that: stop, write it down as a follow-up, no code.
- [ ] Mirror and its test (the first frame; a frame after a fallback frame; `p_prev` 0 is
      today's value bit for bit). Shader text; the pass; suites; build.
- [ ] NEEDS_RUN: WARP with the three switches on; then a moving camera, sun on and off:
      `-Total 80 -Shots "44,45,46,47,76" -Plan "32:VIEW=native,40:RS=-100/0/8000" -GameArgs --fable2_native_render=true,--fable2_native_post=on,--fable2_native_bloom=on,--fable2_native_sun=on`.
- [ ] A standing frame is 10b's within one level; the moving shots described. Commit
      "Previous-frame blend: ...".

### Task 16: Clouds and their occluders

> **Amended after the review of Task 10** (2026-10-10; rulings 33 and 35): the layer's `3FF7` occluders are
> built in Task 14; this task keeps the scene's clouds (`6197`) alone. **The scene clouds' depth write must not
> reach the depth the sun layer copies**: in the game they write depth after the layer's depth copy, with
> their alpha test at 0.0196, and with that threshold in the layer's depth the beam source keeps 11 pixels of
> 12,310. So the native cloud step either writes no depth, or the layer's copy is taken before it; the task
> shows which, with the beam source's count on the oracle feed unchanged. Validate by hue (blue share 1 to 2
> points lower under most cover), not by a sky region's luminance; the occluders' count against 11,466 /
> 17,167 / 4,525 if the layer's depth is touched at all; the occluders' mip level need not be exact.

Only for the shaders Task 11 checked (`6197`, `3FF7`); otherwise this task is the paragraph in
the frame map's "Limits" and nothing else.

**Files:** as Task 13; frame map "Clouds".

- [ ] Mirror of `6197` and pins first. The cloud quads in the scene after the moon, alpha blend,
      depth-tested, wrap sampling with the textures' mips; in the sun layer the three cloud
      occluder quads into `sun_depth_` (depth only), by `3FF7`'s rule for what it keeps.
- [ ] NEEDS_RUN: WARP-SUN with the three switches on; SUNWARD and
      THIRD with sun on and off, READBACK.
- [ ] Sky regions against the emulated shots and against the oracle (the fog's criterion 4 band
      nearest the sun read 0.75 to 0.80 without clouds); shots viewed; cost. Commit
      "Clouds: ...".

### Task 17: The remaining occluders, or their limits

> **Amended after the review of Task 10** (2026-10-10; rulings 33 and 35). What is known, and what this task
> documents as limits unless it builds it: (1) the cubes (`D1CC`) are the far water; **the far lake is not in
> the native depth** (`2B09` is not drawn natively), so the layer sees the lake's bed, a limit for a sun low
> over open water (the cube's floor is no quad: 8 vertices, 30 indices, `c119.x`); (2) the two cubes at
> 20.0024 are unidentified; (3) **rewritten after the review of Tasks 14 and 15 and rulings 36 and 39: "nothing to build" was wrong.**
> The late leaf draws are faded to half by the game (`c15.w` 0.5: one sample of two), are NOT in its depth copy, and were whole in
> the native depth the layer copied; the layer's depth is now taken before them (frame map, "The layer's depth place"). The
> `F1EB` redraw itself is not built (ruling 39). The first writing: the tree draws (`4325` / `F1EB`) are the scene's leaf draws issued after the
> depth copy, which the native depth holds already: nothing to build, the difference left is `F1EB`'s alpha
> rule against the native leaf pass's; (4) the native picture has no distant trees: beams pass where the game's
> far woods stand; (5) the dog's fur shells (`014F`) write depth after the copy, have no occluder and are not
> drawn natively. The 24 point draws write nothing.

- [ ] From Task 10's findings, for each of the cubes (`D1CC`) and the tree occluder draws
      (`4325` / `F1EB`): build it only if its transform or its draws are identified, its shader
      is checked, and recording it neither grows `DrawRecord` nor costs more than 0.10 ms of
      the records-on capture median (measured in a trial run before the pass code is written).
      Otherwise: the paragraph in "Limits" with the finding, no code. The 24 point draws are
      not drawn.
- [ ] If anything is built: tests first, WARP, SUNWARD with READBACK, the beams' regions with
      and without it. Commit "Sun layer occluders: ...".

> **After Tasks 16 and 17** (2026-10-10; rulings 38 and 39): nothing was built by Task 17 itself. Built before it,
> by ruling: the layer's depth place (the depth taken before the game's late records; the `F1EB` redraw is not
> built), the half-pixel origin of the layer's quads, the reset of the previous-frame share after a stretch
> without a Render. Task 16's clouds write no depth. The limits and follow-ups: frame map, "Limits (10c)" and
> "Follow-ups (10c)". For Task 18: criterion 19 is "met on the oracle feed, not met in the game" (limit 1); the
> feed is to be judged with `post_sun_layer.py layers` as mended (limit 13); the scene's own half pixel (limit 12)
> stands under every pixel-by-pixel figure of native shots.

### Task 18: Validation and documents (10c)

- [ ] Runs on a quiet machine: SUNWARD, BRIDGE and THIRD with sun on and off (post and bloom
      on), each with and without READBACK; TREES with sun on.
- [ ] Criteria 16 to 21 with evidence and exact verdicts; every item of the spec's "What may end
      as a limit" with what became of it; the default of `--fable2_native_sun` (the
      controller's ruling).
- [ ] Spec status, "Implementation notes (deviations)", the criteria table; frame map section 17
      "Validation (10c)", "Limits", "Follow-ups"; `lighting-formulas.md`; user checks (the sun
      through leaves, at dawn and dusk; the moon and clouds at night; a fast turn for the
      previous-frame blend; each switch off); README.
- [ ] Commit "Sun layer validation (10c): ...". Then the whole-branch code review and the
      documents review.

> **After Task 18** (2026-10-10; controller ruling 42). No game run was made for it: its build
> changes no binding, target or shader text. The runs listed above were not made as listed (none
> by the trees with the sun on; none at the third spot without full read-back with it); the
> verdicts are read from the runs of Tasks 10 to 17 and the two timing sessions, and recomputed
> where a tool could. Criteria 16 to 18 met; 19 met on the oracle feed and not met in the game;
> 20's picture by ruling 22, its cost and criterion 9 by ruling 42 (frame rate met, capture-thread
> median not decidable). `--fable2_native_sun` stays default off, as the three other switches do.
> The frame map's "Validation (10c)" has the closing table of all 21 criteria; "Outstanding: cost"
> has become "Cost: the two timing sessions". The task also made the review fixes of Tasks 16 and
> 17 (own commit): the Clouds line on the oracle feed, a Render after a jump of frame numbers,
> `tools/xdk_sigmatch/post_cloud_hue.py`.
