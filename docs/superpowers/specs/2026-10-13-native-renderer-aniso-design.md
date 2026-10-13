# Native renderer: anisotropic texture filtering for the world passes (sub-project 15)

Date: 2026-10-13. Branch `native-renderer-aniso`, from `native-renderer-water` at 3c84229.
Research: `.superpowers/sdd/aniso-research.md`. Ledger:
`.superpowers/sdd/2026-10-13-native-renderer-aniso/progress.md`.

## Why

The user sees ground textures slightly blurred at grazing angles in the native view, sharp in the
emulated one. The cause is the one the water-ripple fix found for the lake
(`docs/native-renderer/water-ripple-fix.md`): the emulated path applies the SDK's
`anisotropic_override` (default 3, 4x) to every eligible texture fetch, while the native world passes
sample with plain linear samplers. Only the native water surface follows the override today.

Isotropic trilinear filtering picks the mip level from the longer screen derivative, so a ground
texel stretched along the view direction drops to a coarse level; anisotropic filtering takes several
taps along that axis at a finer level. The visible difference is sharpness along the view direction
at grazing angles, on ground first, then on walls, trunks and props.

## Goal and acceptance

Goal: the native colour pass (and the passes that share its layout) filter textures as the SDK does
under `anisotropic_override`, so the native ground at grazing angles carries the game's detail.

Acceptance:

1. **Detail restored.** In paired captures at the bridge (Hero000, split view, the existing run
   recipe of the ripple fix), the native ground patch's high-frequency luminance RMS is within
   0.8 to 1.2 of the game's at the default override 3, measured by the ripple fix's method on a fixed
   ground box in the lower third of the picture (grazing). The "before" figure from the same box on
   the 3c84229 build is recorded beside it.
2. **Control.** With `--anisotropic_override=0` on both sides the same measure reads within 0.8 to 1.2
   as well (native linear is the game's linear), and the native picture of the "after" build at
   override 0 is within 1% RMS of the "before" build's at override 0 (the change is inert without the
   override).
3. **No colour change.** The all-colour measure (native/game RGB mean ratios, hue difference) of the
   ripple fix stays within its existing bounds in the "after" run.
4. **Exactness where the sampler cannot be.** A texture the SDK would not make anisotropic (single
   level, base-level-only mip filter) is sampled at level 0 isotropically, pinned by tests.
5. **Bounded software/debug run** (WARP, D3D debug layer): zero D3D errors, the known 1328/1008
   warnings only.
6. **Suites.** Full native suite, full Python suite, release build pass.

Not a goal: a frame-time budget (the override is a setting the emulated path already pays for);
MSAA or supersampling ([[native-renderer-aa-later]] stays later); impostor cards, furniture quads,
clouds and the sun layer's quads (view-facing or screen-aligned: little anisotropy to gain; listed as
a limit); the presentation and particle passes (sub-project 14's).

## Design

### 1. One filter mode a frame, from the SDK's cvar

`anisotropic_override` is read once per `ClayPass::Render` through `rex::cvar::GetFlagByName` and
parsed as the water step does today (`-1`, `0` and out-of-range keep linear; 1..5 mean 1x, 2x, 4x,
8x, 16x). The water helpers `kWaterFilterModes`, `WaterFilterMode` and `WaterFilterAnisotropy` in
`clay_logic.h` are renamed `kFilterModes`, `FilterMode` and `FilterAnisotropy` (the water step and
its tests follow; test 867 keeps its numbers). The cvar is hot-reloadable in the SDK (F3 console,
remote-control `cvar` command), so the mode can change at run time.

### 2. The main layout per mode, a rebuild on change

`layout_` (clay_pass.cpp, `Ensure`) is shared by every world draw: the colour pass's terrain,
objects, plants, leaves and trunks, the depth pre-pass, the shadow casters, the sun layer's tree
occluders and the reflection terrain. Its four static samplers are s0 point/wrap, s1 linear/wrap,
s2 point/clamp, s3 linear/clamp (`SamplerIndex`: bit 0 linear, bit 1 clamp). The RHI allows four
static samplers (`kMaxStaticSamplers = 4`), has no dynamic sampler binding and cannot destroy a
layout.

So: `layouts_[kFilterModes]`, created lazily, one per mode, by a shared
`ConfigureClaySamplers(desc, mode)` (the water's `ConfigureWaterSamplers` generalised, in
`src/native/render/clay_samplers.h`; the water step calls the same function): in mode 0 the four
samplers above; in mode m > 0, s1 and s3 become `Filter::kAnisotropic` with
`max_anisotropy = FilterAnisotropy(m)`, s0 and s2 stay point. `layout_` is the current mode's.

Every pipeline cache built on `layout_` (`pipelines_`, `variant_pipelines_`, `prepass_pipelines_`,
`shadow_pipelines_`, `shadow_wind_pipelines_`, `sun_tree_pipelines_`, the water-terrain pipelines)
holds the CURRENT mode's pipelines only. When the mode read at the top of `Render` differs from the
one the objects were built with (`filter_mode_`), the pass destroys its objects and rebuilds them
(`DestroyObjects()`, then `Ensure(dev)` with the new mode): the path a device change takes today.
The six layouts survive the rebuild (cached), so repeated toggles allocate nothing new. A rebuild
is a one-frame hitch on a cvar change; nothing is done per frame otherwise.

Not changed: `sky_layout_` (its fetches are `SampleLevel(0)`), `post_layout_`, `quad_layout_`,
`impostor_layout_`, `water_sky_layout_`, `water_blur_layout_` (limits: see above).

### 3. Eligibility, and exactness without a fifth sampler

The SDK makes a fetch anisotropic when the override is 0..5 (0 forces it off), the texture has more
than one level, the mip filter is not "base map", mag and min filters are linear and the mip filter
is point or linear; it then forces trilinear. Native decodes only the mag filter today and has no
"has mips" bit in the sampler index.

`TextureFetch` gains `min_linear` and `mip_filter` (fc[3] bits 21..22 and 23..24; test pins in
`test_texture_decode.cpp`). `TextureCache::Resolve` reports, with the sampler index, whether the
upload holds more than one level (`plan.levels > 1`, false with `base_only`).

The sampler index gains **bit 2: level 0 only**. It is set when the texture is single-level
(`LevelCount == 1`, DXT3A, or `base_only`) or its mip filter is base-map. The shaders' sampler
selection reads `index & 3` for the register and, with bit 2 set, samples with
`SampleLevel(s, uv, 0)` in place of `Sample`/`SampleGrad`: for a single-level texture that is the
same picture as today's linear sample and the SDK's (bilinear at level 0, no anisotropy); for a
base-map texture it is what the SDK does (MaxLOD = MinLOD) and better than today's mipped sample.
A mipped texture whose min filter is point (mag linear) stays an approximation, as today, now
anisotropic rather than trilinear; the research found no such fetch in the captures in hand and
plan Task 2 counts them in a discovery capture.

The fetches that change under the anisotropic samplers: the object albedo (`Sample`), the terrain
colour (`Sample`), the terrain side and base maps (`SampleGrad`), the shadow caster's alpha
(`Sample`). The ones that do not: weights, the shadow mask, the sky, the reflection picture, the
post chain (`SampleLevel(0)` or `Load`).

Shader sites changed: `FABLE2_CLAY_TEXTURED_HLSL` sampler select and `albedo.Sample`;
`kClayTerrainLayerPs`/`kClayTerrainFarPs` `sample_colour`, `sample_side`, `sample_base`;
`kClayShadowPs` select and sample. The sampler fields (`sampler_index` in the Draw root constants,
`t_colour_sampler`, `t_side_sampler`, `t_base_sampler` in the terrain block) keep their places;
only their value range grows (0..7). `kClayStaticSamplers` stays 4 and the shader bind-point test
holds.

### 4. What the pass says

The clay "ready" log line names the filtering ("texture filtering linear" or "4x anisotropic,
anisotropic_override 3"); a rebuild for a mode change logs one line. `ClayStats` carries the mode
for the F3 Geometry line as `filter: linear|4x`.

### 5. Switches

None new. `anisotropic_override` is the SDK's; the README's native section notes that it now governs
the native world passes and the water as it does the emulated path. Default 3 (4x) as the SDK's.

### 6. Tests (before code; TDD)

- `test_texture_decode.cpp`: min/mip filter decode; `SamplerIndex` bit 2 for single-level, base-only
  and base-map fetches, clear for a mipped linear one.
- `test_clay_logic.cpp`: the renamed mode helpers (867 kept), constants packing with bit 2 (80..82,
  251..257, 303, 460 extended, not changed in place), `ClayStats` filter field and line text.
- `test_water_sampler.cpp` becomes the shared sampler test: every mode's four samplers, s0/s2 point
  always, s1/s3 anisotropic with 1/2/4/8/16 in modes 1..5.
- `test_native_shaders.cpp`: the changed programs compile; the listing has `sample_l` on the
  level-0 branch and `sample`/`sample_d` on the other for each changed site; the historical
  "program of before" hashes for untouched programs hold; the Draw/Terrain cbuffer layout pins hold.
- Python: `tests/test_*literals*.py` pins that quote the changed HLSL lines are updated with them.

### 7. Evidence

Runs reuse `out/native-evidence/water/ripples/` scripts (`prepare*.py`, `run_*.ps1`, `analyse.py`)
with a ground box instead of the lake box, in `out/native-evidence/aniso/`:
`run_before` (3c84229 build, override 3), `run_after` (this build, override 3), `run_control`
(this build, override 0; and the before build at override 0 if the first pair is not conclusive),
`run_warp` (software/debug). `ground_detail.py` applies the ripple fix's high-pass luminance RMS to
the ground box of each paired capture and writes the ratios; acceptance 1 to 3 read from its JSON.

## Rulings

- Ruling 1: scope is `layout_` (colour pass, pre-pass, shadow casters, sun-tree occluders,
  reflection terrain); impostor, quad, cloud and sun-quad layouts are left linear - why: those draws
  are view-facing or screen-aligned, the gain is small and each is its own layout cache - cost if
  wrong: far woods and clouds a little softer than the game's at an angle; a follow-up of the same
  shape.
- Ruling 2: no SDK change (no fifth static sampler, no dynamic sampler kind) - why: the standing
  rule that SDK changes are the user's, and bit 2 gives exactness for the cases that matter - cost
  if wrong: mipped textures with a point min filter are approximated.
- Ruling 3: a mode change rebuilds the pass's objects rather than caching pipelines per mode - why:
  seven pipeline caches would otherwise grow sixfold for a debug cvar that changes rarely - cost if
  wrong: a one-frame hitch and a texture re-upload on a toggle.
- Ruling 4: the acceptance box is a ground patch at the bridge in the lower third of the frame, from
  the same shots as the ripple runs - why: those runs, scripts and the game's frame replay exist -
  cost if wrong: another view costs one more run.
- Ruling 5: cost is recorded (the stats line's frame time in the "before" and "after" runs), not
  gated - why: the emulated path pays the same filtering; the user's AA work later is where cost is
  decided - cost if wrong: none now.
