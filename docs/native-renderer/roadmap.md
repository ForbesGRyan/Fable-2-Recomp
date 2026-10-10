# Native renderer: roadmap (to do)

The order of work after sub-project 10 (the post chain), agreed with the user on 2026-10-10.
Each item becomes a numbered sub-project (research, spec, plan, execution, review, local merge).
Limits and follow-ups named here are described in `frame-map.md`, section 17, "Limits (10c)" and
"Follow-ups (10c)", and in the earlier sections' own limits.

## Order

1. **Foliage: wind and coverage** (sub-project 11, in progress)
   - Wind: trees and bushes stand still in the native view. The game moves them in the vertex
     shader (`frame-map.md`, "Wind and displacement"); the native programs take the wind terms as
     zero.
   - Coverage: foliage the game shows part-covered (alpha-to-mask while fading) is drawn solid
     natively. Leading candidate for the native scene being 0.30 of the game's near the sun.
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
   - Targets are kept resolution-independent for it. Settles the scene's half-pixel offset
     (the game samples at integral pixel centres; unmeasured natively) on the way.
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

## Open with the user

- Push of the local commits on `master`.
- Defaults of `--fable2_native_post` and `--fable2_native_bloom` (the timing supports turning
  both on; kept off until the user says so).
- User checks 40 to 78 in `user-checks.md`.
