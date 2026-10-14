# Native renderer: roadmap (to do)

The order of work after sub-project 10 (the post chain), agreed with the user on 2026-10-10.
Each item becomes a numbered sub-project (research, spec, plan, execution, review, local merge).
On 2026-10-12 the user deferred water's remaining acceptance and selected HUD/UI/cutscenes
as the next active subject (sub-project 14). Shadow correctness and MSAA remain queued.
Deferred water work is saved in [TODO.md](../../TODO.md); its WITH_CONCERNS closure stands.
Limits and follow-ups named here are described in `frame-map.md`, section 17, "Limits (10c)" and
"Follow-ups (10c)", and in the earlier sections' own limits.

Integration snapshot (2026-10-14): local lights, presentation through `234bced`
and anisotropic filtering through `b9011ca` are combined with the current
master history. See [integration status](../INTEGRATION_STATUS.md) for validation
and [user checks](user-checks.md) for acceptance. Canonical check numbers are
107-110 for local lights, 111-117 for presentation and 118 for filtering.

## Order

1. **Foliage: wind and coverage** (sub-project 11, **done 2026-10-11**; `frame-map.md` section 18,
   "Validation (11a)", "(11b)", "(11c)", "Limits (11c)")
   - *Achieved*: the game's wind on trunks, leaves, bushes and grass in the colour pass, the pre-pass and
     the shadow maps (`--fable2_native_wind`); part-covered foliage, a see-through tree between the eye and
     the hero and trees dissolving by distance (`--fable2_native_coverage`); the tree occluder's redraw in
     the sun layer (under `--fable2_native_sun`); the darkness about the sun traced in its larger part to the
     late tree drawn whole; that part mended (the scene is still at about 0.79 there, an estimate). Towards the
     sun with all three on: 0.67 to 0.78 of the game's at the sun (0.59 to 0.76 before), 0.87 to 1.10 at
     "beams left" (1.23 to 1.35 before), 0.95 to 1.15 at "beams right" and 1.35, 1.77 at one second of three.
     (One run, log 643, shots 1296 wide, `RUN UNCHECKED`; the figures "before" are another session's, binary
     and window width: `frame-map.md`, "Validation (11c)".)
   - *Not achieved*: sub-project 10's criterion 19 is still not met in the game (the sun region under in
     half its figures; a shaft of the native layer several times the game's: below); the cost of the three switches
     is measured at the bridge alone, after the fix wave (no steady window below 37.4 fps uncapped with
     all on; no cost outside one run's own spread: `frame-map.md`, "After the fix wave"), not at the
     third spot and for no switch alone; nobody has judged the moving picture; all switches stay off by
     default.
   - What follows is the item as it was written before the work:
   - Wind: trees and bushes stand still in the native view. The game moves them in the vertex
     shader (`frame-map.md`, "Wind and displacement"); the native programs take the wind terms as
     zero.
   - Coverage: foliage the game shows part-covered (alpha-to-mask while fading) is drawn solid
     natively. Leading candidate for the native scene being 0.30 of the game's near the sun. (As written;
     the research corrected the figure: not like for like, about 0.50.)
   - The sun layer's tree-occluder redraw (`F1EB`), not built in sub-project 10.
   - Afterwards: judge spec criterion 19 of sub-project 10 again in the game, and with it the
     default of `--fable2_native_sun`.
2. **Missing scene content: distant trees** (sub-project 12, **done 2026-10-12**, behind
   `--fable2_native_impostors=on`, default off; `frame-map.md` section 19, "Validation (12), closing",
   "Limits (12)", "Follow-ups (12)")
   - *Achieved*: the far woods stand in the native view, one camera-turned quad a tree by the game's own
     vertex formula, cut out and coloured by the game's own picture of each tree model (read back from the
     emulated GPU), dissolved, lit and fogged as the game does, written into the native depth. In the linear
     scene before post, native over game, with the pictures, the coverage and the wind on: the far ranges from
     1.33 / 1.15 / 1.12 / 1.11 to 0.989 / 0.992 / 0.986 / 0.986 of the game's; the tree pixels from 1.43 to
     1.009; the far pixels where the game is nearer than the native depth from 48,584 to 6,406 (158 with one
     pixel of tolerance); the sun layer's depth closed on 99.3% of the far-wood texels (88.4% without). (One
     pair at the bridge, logs 695 and 696, shots 1942 wide, `RUN UNCHECKED`.) 0 errors under the graphics debug
     layer (the software adapter, the pictures by a diagnostic feed); every program of before at its bytes
     with the switch off.
   - *Not achieved*: **the cost is not judged on the closing binary** (criterion 15: +2.327 ms a frame at the
     bridge against a bound of 2.5, measured two binaries earlier on the usual monitor; with the window on the
     other monitor the same switch costs about 61 ms a frame, 27 to 10 fps); the pictures are still the
     emulated GPU's, by read-back, and **one region's** (fourteen addresses: elsewhere the far woods are
     missing, counted `picture: base`); no half part (a pixel the game covers by half is drawn whole);
     silhouettes differ from the game's by about a pixel (criterion 7 not met as written; so are 9's 0.005
     and 16; the references of 7, 9, 12, 13 and 16 turned out wrong); the bridge after post, the dissolve
     band and the beams across far woods are not decidable on this save (criteria 10, 11, 12's second part);
     nobody has judged the moving picture; the switch stays off by default. `flat` (filled rectangles) and
     the pictures' feed are diagnostics.
   - *The cubes at depth 20.0024* are identified and leave this item: the lake's level in the sun layer's
     depth; nothing to build while the lake's meshes are drawn with depth (ledger ruling 2).
   - **2026-10-12 water correction (ledger ruling 26):** nothing is built for these
     cubes. The live `A17D` surface writes the lake's depth before the unchanged
     native layer-depth place; the dead opaque twin is removed only with a whole
     replacement surface. The game obtains that level after its earlier bed copy
     by drawing the cubes. These are separate depth planes, measured separately
     by water criteria 7 and 13.
   - What follows is the item as it was written before the work:
   - Far wooded ranges are 1.15 to 1.55 of the game's (no distant trees drawn natively); beams
     pass where the game's far woods stand.
3. [x] **Water** (sub-project13, **implementation and validation done2026-10-12**;
   initial closing commit `ac6fd20` and fresh documents approval complete;
   product/runtime and whole-branch code approval224c92b complete; C18evidence met;
   final fresh whole-branch documents reassessment APPROVED/all findings closed;
   closing prose wave finalized with concerns;
   `frame-map.md` section20, "Validation (13c)", "Limits (13)", "Follow-ups (13)")
   - *Achieved*: a native blended/depth-writing lake, with dead opaque twins removed only
     for a whole replacement surface; native mirrored sky plus the guest's own far-terrain
     views and seven-tap blur, with own cull/clip/light/eye. No reflection readback list or
     SDK change. Final own water92/92 and reflection214/214records are bit equal.
     Cover misses beyond1pixel are.36%/.14%bridge,.74%/.05%third; before-water bed
     agrees with the game's copy on95.4%/94.9%. Off placeholder final-depth shares4.4%/4.3%
     use a different plane/set. Sun-layer lake depth passes99.9787% of4705texels (13b).
   - *Before/after the terrain reflection*, separate own-frame linear-scene sets:13b
     whole-footprint RGB1.150/1.141/1.107bridge and1.154/1.146/1.113third,hue-1.5degrees
     both;13c all-kept RGB.984900/.983597/.971809,hue-.629bridge and
     .981218/.979685/.968927,hue-.562third.13c luminance P5/50/95 ratios
     .948298/.995727/.992886bridge,.968218/.995434/.996679third meet the bounds.
     Sets18428/20830before versus18433/20824after are not a common-set delta.
     `t13_*_colour_given_place.txt`, `t18_*_colour_all.txt`; stills do not judge motion.
   - *Cost after the review fix wave*:new DD2D executable/source224c92b, R61 logs750–753,
     alone on the usual display inventoried every run7680x2160/144DPI150%, uncapped,
     all agents idle; all20shots1942x1136 viewed.69steady windows18/17/17/17,
     off17.798088/on17.515294ms, delta-.282794ms; repeats.766176/.017647ms,
     minimum43.6fps. Frame-cost/floor bounds met; negative delta is no acceleration
     proof. Capture+.1645ms not decidable below1/3ms; CPUmedian.078ms separate.
     `t19_timing.json`; all69raw windows independently recomputed. Initial driver
     BOM-decoding rejection retained; decoding alone fixed, raw/windows/rules unchanged.
   - *Historical cost on4554*:13c alone on the restored usual monitor, all20shots1942wide, uncapped,
     off/on/on/off,68steady windows, minimum47.6fps; frame delta-.004117647ms meets+.5ms,
     repetitions.42/.118824ms meet1.0ms. This tiny negative nominal delta is not acceleration
     proof. Capture+.1675ms is not decidable below1/3ms resolution; Water CPUmedian.077ms
     is separate from GPU/frame cost.13b floor40.6met but frame cost not decidable from
     off repetitions2.0214ms apart. Ruling57 supersedes54 after display sleep/restoration;
     timing precedes third/off runs to use the restored display. `t18_timing.json`.
   - *Not achieved as written*:C10 all eight game-to-native target cover shares
     36.5709–84.6931% miss85%, despite reverse97.2272–100% and common RGB passing;
     objects/flares are absent, without assigning every missed texel to that cause.
     C14 pools miss.97–1.03 on at least one paired side (bridge1.029566/1.055547,
     1.036011/1.041345;third1.043455/1.048152,1.214655/1.162564).
     C4's original tiny deciding base is not decidable; ruling35's disclosed whole-lake
     substitute passes. C5 colour passes16/16 but six horizons miss by2rows. C8's sky subset
     lacks5000pixels; C11near/C12glint lack qualifying views after bounded searches.
     C15old image is not decidable from EXPO/config/width mismatch; C17capture increment
     is not decidable. All these remain named alongside the met measures.
   - *Current closing gates*:source/test fix224c92b, native41PASS/releaseDD2D exit0,
     Python1899tests570.771sOK7skips; fresh documents approval complete. The epoch
     callsite/help/one-fifth near/far/fog floor fixes leave formulas/SDK source/
     shaders/targets/bindings unchanged. Final paired/default-off confirmation complete:
     92water/214reflection rows bit equal,12/12bridge and11/11third with4terrain targets/
     no fallback;750–7580errors/warnings/known freeze NOT SEEN and39viewed1942shots.
     No new colour/silhouette/pool acceptance or motion proof. Final whole-branch code
     review APPROVED at224c92b/all three findings resolved; C18evidence rule MET.
     Final fresh whole-branch documents reassessment APPROVED/all findings closed;
     closing prose wave finalized with concerns. Release includes original
     user wind edits outside the water commits; quantitative runs explicitly select windoff.
   - *Historical evidence limits*:fresh Task18 source/runtime review approved; runtime4554F15C,
     source/test9e6096f. WARP debug gates have0errors/drawn/viewed lake;1328warnings and
     unattributed dump1008warning remain. Bridge/WARP lack aggregate guest windows,
     whole-run freeze NOT EXCLUDED; later third/timing/defaults have actual guest windows
     and no known signature seen. Every driver remains RUN UNCHECKED; continuous motion,
     glint/near-shore fade and other regions/times remain user checks101–106. Water default off.
   - What follows is the item as it was written before the work:
   - **First, at its start: decide the route by which the native view gets pictures the game renders,
     without read-back** (follow-up of sub-project 12, below): the reflection views have the same problem
     as the far woods' pictures.
   - The lake is a placeholder; the far lake's cube is missing from the native depth; reflection
     views (`D72F` at pitch 280) and far water (`D1CC`) are not read.
4. **Shadow correctness** (folded into 2 or 3 where it fits; not folded into 2: sub-project 12 found
   nothing of it that the impostors share)
   - Near shadowed ground possibly too dark (palisade dirt 0.67 to 0.70).
   - Shadowed ground beyond 100 units 11 to 19% over.
5. **MSAA / supersampling**
   - Targets are kept resolution-independent for it. Settles the scene's sampling place on the
     way: the game's depth copy holds its sample a quarter pixel up and left of the pixel's integral
     position, and the native picture is the game's sampled a quarter pixel down and right
     (`frame-map.md` section 18, "The rasterising replay", "The darkness about the sun"); whether that
     is the console's first sample or the emulator's read-back is not known.
6. **Presentation: HUD, menus, loading screens, cutscenes** (sub-project 14, **verified 2026-10-13; away-gated timing pending**)
   - Native sprite/text/video/particle/fade presentation with frame-owned UI records,
     native mask producers (sequential scratch target, kick-aware copy parser), menu
     history/7e3 resolve, title particles, snow and soft-depth billboards, black fade,
     F51 DOF (proven, unexercised). Hardware paired readbacks on build 73: HUD opaque UI
     0.23/255 at 99.96%, pause 0.24 at 99.7%, Quests/Maps 0.24 at 99.2%, title 0.27 at
     100%, movie 0.26-0.28 at 100% over four changing frames, "Scene paused" 0.25 at
     99.9%. Transitions compose natively; the skip and the opening's 577CB/15675
     distortion frames retain the guest picture explicitly. Full suite and the WARP
     debug-layer run pass. User-present market profiling: +0.15 ms/frame, menu at 75 fps,
     scene itself 23 fps (emulation bound); away-gated, native UI on holds 30.0 fps in every
     window at the market (run 869, 2026-10-13 20:36). The opening characters' materials
     (44B56763 two lights, 401A01FD one, 20055F97 seven: lit_object with the cascade maps
     sampled directly) and the 79EAC495/D4D558DA pairings of 789266C3, FC0922A3 and F2F103F1
     are object-multi since the same evening (light_check 400 trials each, sun-slot and lantern
     checks; run 870: Rose and the hero textured and lit at the town dialogue). Open: the
     away-gated UI-off half of the timing (user check 116), half-resolution effects and
     distortion producers (guest retained), the fur shells A17D8AEC/014F8A02 and the flat-ambient
     variants 8C512607/F98433EC (their vertex shaders have no light entry). `fable2_native_ui`
     defaults off; details in the plan's "Current status" and "Findings".
   - [Plan](../superpowers/plans/2026-10-12-native-renderer-presentation.md);
     research `.superpowers/sdd/presentation-research.md`;
     ledger `.superpowers/sdd/2026-10-12-native-renderer-presentation/progress.md`.
   - The long-term goal: leave the emulated GPU. The sun switch has never run in a town, an
     interior, a menu, a loading screen or a cutscene.

## Any time

- **Tests for the three files no test compiles**: `capture.cpp`, `clay_pass.cpp`,
  `fable2_native_render.cpp` are held by game logs alone.
- Bloom's downsample target widened from `R11G11B10_FLOAT` (bias a tenth of a level).
- A public gamma-ramp accessor upstream in the SDK (today read through a derived class).
- The two levels of the capture thread's time per run (about 2.04 or 2.33 ms at the bridge),
  cause unknown.
- The one startup crash with `--fable2_exposure_readback=on` (log 575; points at the boot-save
  trigger, inference).
- Why the reference's sun-layer factor is 1.4 to 1.7 times the game's at the beam root.
- The four copies of one switch reader in `fable2_native_render.cpp` (`CurrentSun`, `CurrentWind`,
  `CurrentCoverage`, `CurrentSceneDump`): one function taking the value and the name (final code review of
  sub-project 11, Minor 8).
- The mip dump repeats the texture cache's level layout loop (`capture.cpp`, `DumpTextureMips`, against
  `texture_cache.cpp`): one function that returns the level rows, used by both, so that a change of the
  cache's rule for packed levels reaches the dump (Minor 9).
- **The fern's look at rest** (and likely other lit plants under alpha-to-mask): natively broad and light,
  in the game dark and toothed. Not the wind, not the geometry, and not part coverage (on its stored mip
  levels a quarter of its pixels have one sample of two, its cover already is the game's first-sample
  rule, and at half it would be lighter: `frame-map.md`, "The fern on its stored mip levels"). Left:
  the lit plant's light, and the level the game's own fetch takes (needs a capture with read-back and
  the mip files, at the bridge).

### Follow-ups of sub-project 11 (`frame-map.md` section 18, "Limits (11a)", "Limits (11b)", "Limits (11c)", "Follow-ups (11)")

- **The cost of wind, coverage and the redraw: measured at the bridge, not elsewhere** (logs 654 to 656,
  width 1942, made alone, uncapped: with all on every steady native window at 37.4 fps or more, mean frame
  19.41 ms against 19.36 with post and bloom alone, capture median +0.13 ms). Left: the third spot's pair
  (user check 91), each switch alone, a capped pair, and how much two runs of one configuration differ
  uncapped (a window's rate moves by 20 fps inside one run, so the criterion's 30 fps bound no longer
  says "no cost"). The run with the sun alone had a frame of 76 to 93 ms in every window, in its emulated
  view too: not explained.
- **The dog is drawn flat red on the software adapter** in every textured run since sub-project 10 at
  least (not on the real GPU): never looked into.
- **The native sun layer's sweeping shaft** (ledger ruling 64; an open item of sub-project 10's layer, with
  "why the reference's sun-layer factor is 1.4 to 1.7 times the game's" above): a shaft across the bridge's
  planks 2 to 4.6 times the game's at "beam far right" and 1.35 to 1.77 at "beams right" at moments, moving
  with the sun's place; in runs with coverage, the wind or neither. Cause open; an occluder missing on that
  side is not supported (573 texels in the one frame that can show it). Needs the game's layer of a frame
  beside the native one. It keeps `--fable2_native_sun` off by default. User checks 76, 89.
- **Leaves at 0.88 of the game's** in the linear scene (0.79 to 0.81 at the beams' root and "beams right").
  No fault of an existing formula was found: the leaf formula's check passes on made inputs; **the frame's
  own leaf rows were not checked** (`light_check.py` has no mode for them). Candidate: the leaf's normal map,
  read flat.
- **Foliage edges against the sky at 0.95** (candidate: the native pixel centre against the game's first
  sample a quarter pixel away; belongs with item 5).
- **The beam at "beams right"**: the same thing as the sweeping shaft above (first written as one region at
  one second).
- **The console's level bias of +1.0 on the tree occluder's fetch**, which the emulator does not apply and
  the native redraw does not have. The twin's alpha scale is the scene shader's, equal to the occluder's
  while `c26.w` is 1.
- **Two different coverage records' half parts at one pixel** blend twice; in the tail, all depth steps and
  then all colour steps would make two early records blend once when there is no pre-pass (a small change:
  test case 765 widened to two records, and a run).
- **The flicker check behind a late tree**: a blended effect that writes no depth, behind a see-through
  tree, is shown at half or hidden by the game's draw order, which changes from frame to frame; 0 pixels in
  the one view measured; only a person can find it (user check 88).
- **The casters' wind is unshown**: the shadow maps' casters sway by the same program (exact by the
  identities), but no measurement in the game separates it (the sun moves); user check 83.
- **A software-adapter run with the wind off**: the redraw's and the half part's rest pipelines have run on
  the real adapter alone. And the two steps' depth equality is measured on one graphics card.
- **The default configuration of the wind and the coverage has not been drawn** (post and bloom off; the
  shadows off; the fog off; the textures off): the fix wave's five software-adapter runs (ledger ruling 67;
  `frame-map.md`, "The fix wave", "Limits (11c)" 13). Not to be made the default before the ledger has them.
- **A long session with the wind and the coverage on**, through several regions: the colour pass's two
  pipeline caches are bounded (64 ordinary keys, 448 of the wind and the coverage) and never evicted; no
  run has reached a bound, and what happens there is held by tests of the pure rules alone. The log's
  `pipelines changed` lines show the use. With it: one F6 retry and one device loss with both switches on
  (complete by reading, never exercised).
- **The fern's look at rest**: the entry above.

### Follow-ups of sub-project 12 (`frame-map.md` section 19, "Limits (12)", "Follow-ups (12)")

- **Criterion 15 on the closing binary**: the timing session at width 1942, four runs, each alone (user
  check 99). The last judged figure, +2.327 ms against a bound of 2.5, is two binaries old, and its off-runs
  stood at the display's 60 Hz.
- **The pictures by a route without read-back.** The forced read-back costs about 1.8 ms a frame plus
  about 1 ms of decode, levels and upload on the usual monitor, and about 61 ms a frame on the other one
  (cause in the emulator's presentation or read-back path, not found). Two routes: share the emulator's GPU
  texture with the native view, or render the pictures natively (the research's option B; with item 6).
  **To be decided at the start of the water sub-project (item 3)**, where the reflection views have the same
  problem. The small step before either: decode and make the levels on the GPU.
- **The half part for impostors** (ledger ruling 48): a pixel the game covers in one sample of two is drawn
  whole (about one kept pixel in five of a far tree; a tree steps in at once between 52.02 and 54 units).
  The rule and its tests exist; the draw needs the coverage tail's order and a software-adapter run.
- **The silhouettes' displacement of about a pixel** (criterion 7): with item 5, the scene's sampling place.
- **Other regions' picture addresses**: the fourteen bases were measured in the starting valley; where the
  pool lies elsewhere, how many models, towns, times of day (user check 97).
- **The content rule**: its ring bound (944 of 1,020 texels clear) comes from fourteen tree models at one
  place; a model that touches its picture's edge is refused, and a whole texture of another kind at one of
  the bases would be drawn.
- **The picture-edge address mode**: the game clamps to a border colour, the native sampler to the edge
  texel; needs a border sampler mode in the SDK's native RHI.
- **The picture hash's lane step and its test**: certain pairs of bits in neighbouring blocks of one lane
  cancel (at worst a kept picture that differs in low bits of two texels); a second rotation and product in
  the step, with the pair (34, 63) as a test case, at the next build of `impostor_picture.h` (ledger ruling
  57). The present test tries only the same bit in both words and does not hold what it names.
- **Refusal paths and the reuse never exercised on a graphics card**: but for `picture: mode` in the one
  run made for it (log 708), no refusal reason of the capture or of the pictures has fired there; the reuse of an unchanged picture ran on the software adapter with a
  one-set feed alone; one F6 retry and one device loss with the switch on (complete by reading).
- **`fast` and a hand-given list unrun** with `on` (`--readback_resolve=fast`; a
  `readback_resolve_force_addresses` given by hand that holds the fourteen bases); a mode changed in a
  running game.
- **The 9,000-line test `main`**: `test_clay_logic`'s `main` is one function with a frame of 995,768 bytes
  and runs on a 16 MB stack since the closing fix; its body moved into functions by section.
- **The two unidentified families** `43ED` / `7F65` and `7D1C` / `983E`: undrawn.
- **The lake placeholder**: item 3.
- **The software adapter's empty forced read-back, and its flat red dog** (the entry of sub-project 11
  above now has a named neighbour: on that adapter every tree picture reads as zeros, so `on` draws no far
  tree there and the pictures' path runs under the debug layer only through the diagnostic feed).
- Kept: the ground-fog block of the impostor's pixel shader (`b129`; a draw with it set is refused); a
  timing run of `flat` and the third spot's pair; the register bounds the spec left narrow (the dissolve's
  rate, the pixel distances); a discovery capture keeps the pictures of its first 32 frames only.

### Follow-ups of sub-project13 (`frame-map.md` section20, "Limits (13)", "Follow-ups (13)")

- Far lake's geometry and165-instruction shader remain unbuilt; D1CC cubes are not drawn
  under ruling26, because the live surface supplies the native layer's lake depth.
- Reflected objects and flares remain omitted;6954/A4A9 is factor memory export,0%colour,
  so omitting its execution is not an omitted picture draw.
- Reflection views are still issued by the guest; move scene/culling to native with item6.
- Other water bodies/regions/times, a live opaque form and the `b137` shadow term.
- Slot14 meaning and a reachable near-water/shoreline view; check102.
- In-scene resolve to0x13401000; native targets do not consume it.
- Sun-layer copy place against other depth-writing blends; keep original depth sources explicit.
- Normal map BC5/all stored levels is implemented; remaining host/console LOD, filtering and
  sample-position differences stay with MSAA/supersampling item5.
- Identify43ED/7F65 independently; it remains undrawn.
- SDK copy interface B2 remains the user's separate decision (ruling2); no SDK edit.
- Monitor experiments remain separate (ruling8); user display-sleep explanation and restored
  timing ruling57 do not license changing display settings or resizing evidence.
- Warning1008 resource attribution and real hot-device replacement remain unverified.
- Moving water/glint, near-shore fade, omitted reflection objects, beams and other water/time:
  checks101–105; exact off control/check106, and a comparable old-image baseline.

## Open with the user

- Push of the local commits on `master`.
- Defaults of `--fable2_native_post` and `--fable2_native_bloom` (the timing supports turning
  both on; kept off until the user says so).
- Defaults of `--fable2_native_wind` and `--fable2_native_coverage` (recommended on once user checks 81
  to 88 are done: the cost at the bridge is judged; kept off until the user says so); `--fable2_native_sun` stays off.
- Default of `--fable2_native_impostors` (off; not recommended on before criterion 15 is judged on the
  closing binary and user checks 93 to 98 are done; the value `on` adds thirteen entries to a start-up list
  of the SDK and its cost depends on the monitor).
- Default of `--fable2_native_water` (off; final colour/cost measures pass, target silhouettes
  and pools miss, motion/near branch/other regions unseen; user checks101–106 first).
- SDK B2 copy interface and further monitor experiments remain separate user decisions.
- Default of `--fable2_native_local_lights` (on, ruling 7 of the local lights spec: the user asked for the light; `off` restores the picture of before; user checks 107 to 110 judge it at night, indoors and at the campfire).
- The earlier branch-order handoff is superseded by the 2026-10-14 integration: local lights were already on master, presentation includes them, and filtering merges over presentation. Integration preserves the original dirty water checkout. Local publication remains pending.
- User checks 40 to 118 in `user-checks.md`.


## Post-closure campfire follow-ups (2026-10-12)

User verification found missing ground texture, then a colour mismatch and an untextured large trunk near the campfire. Three previously unsupported seven-light material variants are now registered with checked shader pairs: two terrain shaders and one bark shader. Offline proofs, affected tests and release build passed. Runs 766/767 confirm bark and ground colour with user feedback, original split screenshots, valid light inputs and full discovery audits. Both follow-up plans are complete. Reports: [ground](campfire-ground-fix.md), [tree](campfire-tree-fix.md). The water project's historical WITH_CONCERNS closure and pending water checks remain as recorded; these follow-ups address the reported campfire material/lighting defects. The campfire fixes are committed as `2f1001b` and included in master.


## Post-closure bridge water ripples (2026-10-12)

The bridge lake's missing fine ripples were caused by native linear filtering while the game used its default 4x anisotropic override. A one-setting linear control reproduces the native smoothness. Native water now follows the shared setting per eligible normal map; point/single-level maps retain their original filters. Run 774 restores measured detail from 26% to 96–97% of the game's, with both existing colour measures passing. Full native checks, targeted water checks, release build and bounded software/debug check pass. Original screenshots and own-frame captures confirm twelve water surfaces and fourteen distant-tree groups drawn. Report: [water ripple fix](water-ripple-fix.md). This defect is fixed by captured hardware evidence; the offered user visual confirmation is pending. Historical reflection/default-off decisions retain their scope. The ripple fix is committed as `3c84229` and included in master; moving acceptance remains pending.

## Post-closure local lights (2026-10-13)

The user found that "the lanterns and torches aren't illuminating nearby areas": the native pass drew every two-, four- and seven-light draw with the frame's sun alone (lighting ruling 5), so the pool of light near a lantern or torch was missing in the native half. Built on branch `native-renderer-lanterns` (worktree `.claude/worktrees/lanterns`, commits `f5fbcac` to `9ac83e7` over `3c84229`, plus the closing documents), a follow-up of sub-project 7 (spec `docs/superpowers/specs/2026-10-13-native-renderer-local-lights-design.md`, ruling 1): the per-slot light form and its checker (`light_check.py --multi-lanterns`); the light set of each multi-light draw read once per upload into `FrameScene::light_sets` (`MakeLightSet`, the lanterns before the sun); the world position of every lit object and trunk in the vertex shader; the frame's lanterns as a 64-lantern tail of the frame constant buffer with each draw's range packed in its `light_mode` word; `LocalLights` in the lit pixel paths of the terrain layers, objects, trunks and both plant forms and the lanterns' colour sums in the leaf vertex path; the switch `--fable2_native_local_lights` (default on). Record: frame-map section 14, "Native light", "Local lights"; `lighting-formulas.md`, "Every slot before the sun"; ruling 5 of the lighting spec superseded. Every game run was `RUN UNCHECKED` with the user at the machine, on their leave "continue".

Criteria (evidence in the worktree's `out/native-evidence/lighting/lanterns/`, git-ignored):

1. Offline proof: **met**. 24 of 24 pairs (the six multi families, the campfire's seven-light layers and bark among them) agree in 400 of 400 trials at every lantern slot, largest relative difference 2.26e-15 (`light_check_multi_lanterns.txt`); `MultiLanterns` fails on a wrong colour, a moved position or a dropped term.
2. Bridge capture: **met in part**. Lantern 2's disc, native over game, 0.866 off to 0.968 on, inside [0.8, 1.25]; lantern 1 is 1.00 in both runs, its pool not measurable by day (neutral, not a success); the frame outside the discs 1.0136 on against 1.0152 off, unchanged (`r2_on_lantern_regions.txt`, `r3_off_lantern_regions.txt`).
3. Debug layer and suites: **met, with the byte pin replaced**. 0 debug-layer errors with the switch on (run r4, `fable_2_004.log`); the native and Python suites pass; the programs' bytes with the switch off are not those of before by design, and ledger ruling 4 replaces that: with the bit clear no program reads the lantern tail (`test_native_shaders.cpp` code 168) and the off-run's picture is that of before.
4. Cost: **met**. GPU busy median 30.38 to 30.64 ms on against 30.43 to 30.62 ms off, frame median 37.47 to 37.54 against 37.45 to 37.61 ms, about 0.0 ms against the +0.5 ms bound (`timing.txt`; both runs at about 26.5 fps with the machine in use).
5. Documents: **met**. What is drawn and what is not are in frame-map section 14; user checks 107 to 110 added and checks 32a, 38 and 59 re-read.

Limits: no lanterns in the water's reflection (spec ruling 6); the lantern vector at the vertex at rest, not the swayed position; no specular per light; no per-light shadow map (slot 9) and no `b135` path, so a post's shadow fanning from a lamp is missing; slots after the sun are not read; a set whose sun is not the frame's, or past 64 lanterns a frame, is refused and counted; presentation's registered crowd/building/character pairs are now included in the merged table; visual acceptance of their lanterns remains pending; night, interiors and the campfire were never seen by the agent (user checks 107 to 110).

Follow-ups:
- The integrated crowd, building and opening-character pairs use the shared multi-light table. Recheck their lanterns in the moving town/interior views; the integrated offline proof is recorded in the integration report.
- Deferred minors worth doing (`.superpowers/sdd/2026-10-13-native-renderer-local-lights/progress.md`): a shared helper (`LanternSpot`) for the leaf vertex path, which repeats `LocalLight`'s distance, range and cone statements without a mutation control; hoisting the two plant calls above the ambient branch (the root-word textured program grew from 24 KB to 42 KB); `lanterns_draws` for the early coverage records drawn in the tail (lit, but not counted in the `Light:` line); and the set slot a skipped record can take (`AddLightSet` is called before the record is known to be kept: call it only when the record is not skipped).

## Sub-project 15: anisotropic texture filtering (2026-10-13)

The native world passes sampled with plain linear samplers while the emulated path applies the SDK's `anisotropic_override` (default 3, 4x); the ground at grazing angles was softer in the native half. The main clay layout now exists per filter mode (six, cached) with anisotropic s1/s3 in modes 1..5, the cvar is read once a frame, and a sampler-word bit 2 samples level 0 only for single-level, base-only and base-map textures (five shader sites), so the cases the SDK leaves isotropic stay exact. Result on the bridge dirt box (2108 px, grazing): native/game high-pass detail 0.49 and 0.63 before, 0.92 and 0.95 after, 0.89 and 0.94 with the override off on both sides; the colour measure is met on all after and control samples; the WARP/D3D debug run has 0 D3D12 errors (warning 1328 only). All runs `RUN UNCHECKED`. Report: [aniso filtering](aniso-filtering.md); frame-map section 21; user check 118 is pending.

Limits (15): impostor, furniture-quad, cloud and sun-quad layouts stay linear; `--anisotropic_override=-1` does not honour a fetch's own anisotropy field; mipped textures with mag linear and min point are approximated (none seen in 309); a mode change rebuilds the pass (one-frame hitch); the judged box is small and one texture type; the shader instruction's own filter override is not read.
