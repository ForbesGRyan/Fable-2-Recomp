# Native renderer: roadmap (to do)

The order of work after sub-project 10 (the post chain), agreed with the user on 2026-10-10.
Each item becomes a numbered sub-project (research, spec, plan, execution, review, local merge).
Limits and follow-ups named here are described in `frame-map.md`, section 17, "Limits (10c)" and
"Follow-ups (10c)", and in the earlier sections' own limits.

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
2. **Missing scene content: distant trees**
   - Far wooded ranges are 1.15 to 1.55 of the game's (no distant trees drawn natively); beams
     pass where the game's far woods stand.
   - The cubes at depth 20.0024, unidentified.
3. **Water**
   - The lake is a placeholder; the far lake's cube is missing from the native depth; reflection
     views (`D72F` at pitch 280) and far water (`D1CC`) are not read.
4. **Shadow correctness** (folded into 2 or 3 where it fits)
   - Near shadowed ground possibly too dark (palisade dirt 0.67 to 0.70).
   - Shadowed ground beyond 100 units 11 to 19% over.
5. **MSAA / supersampling**
   - Targets are kept resolution-independent for it. Settles the scene's sampling place on the
     way: the game's depth copy holds its sample a quarter pixel up and left of the pixel's integral
     position, and the native picture is the game's sampled a quarter pixel down and right
     (`frame-map.md` section 18, "The rasterising replay", "The darkness about the sun"); whether that
     is the console's first sample or the emulator's read-back is not known.
6. **Presentation: HUD, menus, loading screens, cutscenes**
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

## Open with the user

- Push of the local commits on `master`.
- Defaults of `--fable2_native_post` and `--fable2_native_bloom` (the timing supports turning
  both on; kept off until the user says so).
- Defaults of `--fable2_native_wind` and `--fable2_native_coverage` (recommended on once user checks 81
  to 88 are done: the cost at the bridge is judged; kept off until the user says so); `--fable2_native_sun` stays off.
- User checks 40 to 92 in `user-checks.md`.
