# Anisotropic texture filtering for the native world passes (2026-10-13, sub-project 15)

The user saw ground textures slightly blurred at grazing angles in the native view and sharp in the emulated one. The cause is the one the water-ripple fix found for the lake: the emulated path applies the SDK's `anisotropic_override` (default 3, 4x) to every eligible texture fetch, while the native world passes sampled with plain linear samplers. Only the water surface followed the override (`water-ripple-fix.md`). The SDK's rule for an eligible fetch: more than one level, a mip filter that is not base-map, mag and min filters linear, mip filter point or linear.

## Design

- The cvar is read once per frame in `ClayPass::Render` (`ParseFilterSetting`, `FilterMode`; -1, 0 and out of range keep linear, 1..5 mean 1x..16x). The water's helpers are renamed `kFilterModes`, `FilterMode`, `FilterAnisotropy`.
- The main clay layout exists per filter mode (six, cached, `ConfigureClaySamplers` in `src/native/render/clay_samplers.h`, shared with the water step): mode 0 keeps the four static samplers, modes 1..5 make s1 and s3 anisotropic and leave s0 and s2 point. It serves the colour pass, the pre-pass, the shadow casters, the sun layer's tree occluders and the reflection terrain.
- A mode change rebuilds the pass's objects; the layouts and the texture cache survive.
- The sampler word gains bit 2, "level 0 only" (`kSamplerLevel0 = 4`, `SamplerBits`), set from the bound texture's level count (`Entry::levels`) for single-level, base-only and base-map textures. Five shader sites sample level 0 with `SampleLevel(..., 0)` when it is set: object albedo, shadow caster alpha, terrain colour, side map and base map. This makes the cases the SDK leaves isotropic exact without a fifth static sampler.
- Water keeps `SamplerIndex` (no bit 2). Historical shader hashes are kept through the `SamplerSelectBefore` text transform in the shader test.
- The ready line names the filtering; `ClayStats::filter_mode` carries the mode. No new switch: `anisotropic_override` is the SDK's, default 3.

## Numbers

Fetch filters of run 774's world capture (`fetch_filters.json`, 309 unique textures): 156 mipped linear/linear/linear (38 ask for anisotropy 2 or 3 in their own fetch constant), 45 single-level linear, 34 base-map (2 linear, 32 point), 66 mipped point/point/linear (point mag filter: the point samplers, unaffected), 0 with mag linear and min point.

Paired hardware captures at the bridge (Hero000, split view, 100 s, discovery at 45 s and 70 s; before exe `E3D1DEF1...2C45` from 3c84229, after and control exe `0889B3EC...58B4`; the same eye in all six captures; all runs `RUN UNCHECKED`, user present). Judged box: the dirt path at a grazing angle, rows 392..426, cols 106..168 of the 1120 x 720 plane (2108 px). High-pass luminance RMS (the ripple fix's measure):

| Run | Sample | Game | Native | Native/game |
|---|---|---:|---:|---:|
| before (override 3) | 45 s | 0.1322 | 0.0650 | 0.492 |
| before (override 3) | 70 s | 0.1281 | 0.0802 | 0.626 |
| after (override 3) | 45 s | 0.1321 | 0.1219 | 0.923 |
| after (override 3) | 70 s | 0.1335 | 0.1262 | 0.946 |
| control (override 0) | 45 s | 0.0728 | 0.0651 | 0.894 |
| control (override 0) | 70 s | 0.0911 | 0.0858 | 0.942 |

Acceptance 1 (ratio within 0.8..1.2 at override 3, before figure beside it): met. Acceptance 2 (control within 0.8..1.2 with the override off on both sides): met; the game's own detail falls to 0.073 and 0.091 and native follows it, so a before-build control run was not needed. Acceptance 3 (all-colour measure within its bounds: RGB mean ratios 0.92..1.08, hue within 6 degrees, luminance percentile ratios 0.93..1.07): met on all four after and control samples (RGB ratios 0.96..0.99, hue 0.29..0.43 degrees). The bridge deck box (rows 418..462, cols 240..500) is recorded as a secondary sample only: 1.05 and 1.07 before, 1.13 and 1.16 after, 1.16 and 1.18 control; its high-pass is dominated by aliased plank-gap edges, so it does not see texture filtering.

Bounded WARP/D3D debug run (100 s, exe `0889B3EC...58B4`): 0 D3D12 errors, warning 1328 four times, ready line "texture filtering 4x anisotropic, anisotropic_override mode 3". Native suite, Python suite (1851 tests; six errors from a non-ASCII comment fixed in fdf7d25) and the release build pass. Cost was recorded, not gated (ruling 5).

## Limits

- Impostor, furniture-quad, cloud and sun-quad layouts stay linear (ruling 1).
- With `--anisotropic_override=-1` the SDK would honour a fetch's own anisotropy field; native keeps linear (ruling 11). The shader instruction's own filter override is not read, only the fetch constant's.
- Mipped textures with mag linear and min point would be approximated; none was seen.
- A mode change rebuilds the pass (a one-frame hitch); a failed rebuild turns the renderer off until F6, as a failed device change does today.
- The judged box is small (2108 px) and one texture type; the cvar is looked up by name once per frame plus once in the water block.
- User confirmation is pending (user check 112).

## Rulings

1. Scope is `layout_` (colour pass, pre-pass, shadow casters, sun-tree occluders, reflection terrain); impostor, quad, cloud and sun-quad layouts stay linear.
2. No SDK change (no fifth static sampler, no dynamic sampler kind); bit 2 gives exactness for the cases that matter.
3. A mode change rebuilds the pass's objects rather than caching pipelines per mode.
4. The acceptance box is a ground patch at the bridge from the ripple runs' shots.
5. Cost is recorded (the stats line's frame time), not gated.
6. The path is architectural (spec, plan, subagent execution with reviews), not bounded.
7. The water helpers are renamed to neutral names and the sampler configuration moves to `clay_samplers.h`, shared with the water.
8. Textures with mag linear and min point with mips number 0 of 309 (amended: the 66 mipped point/point/linear textures select the point samplers and are not affected).
9. The identical sampler-select block in the textured and the shadow programs stays duplicated text rather than a shared HLSL macro.
10. Task 1 (environment) was reviewed by verifying its artifacts, not a diff.
11. The 38 textures whose fetch constant asks for anisotropy follow the override like the SDK does with override 0..5; with -1 native keeps linear.
12. The aniso worktree's pin includes were rewritten to LF in the working tree only so the Python literal tests read them as main does (nothing committed).
13. The shader test's sample_l check is register-specific so it tells old code from new.
14. The non-ASCII comment that broke the literal tests is fixed as a fix round of the pass task, then the validation resumes.
15. The "before" run swaps the before exe into the worktree build folder and restores the after exe (hash checked), because the driver starts the game only from its allow-listed folder.
16. The judged ground box is the dirt path at rows 392..426, cols 106..168, not the brief's default box (near railing and dog) nor the deck box (edge-dominated, secondary only).

## Evidence

Under `out/native-evidence/aniso/`: `fetch_filters.json`, `warp_result.json`, `results.json` (all runs, the `_verdicts` block), `ground_detail.py`, `run_*.ps1`, `before/` and `after_saved/` exes, `aniso_{before,after,control}_{45,70}_detail.png` and `_frames.png`, the drivers' output `*_driver.txt`. Original screenshots are in the worktree's `out/build/win-amd64-release/logs/shots/aniso_{before,after,control}/`. Spec: `docs/superpowers/specs/2026-10-13-native-renderer-aniso-design.md`; frame-map section 21.
