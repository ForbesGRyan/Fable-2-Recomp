# Native renderer presentation: HUD, UI and cutscenes

Sub-project 14, selected by the user on 2026-10-12. The remaining water
acceptance work is deferred in [TODO.md](../../../TODO.md) and the
[roadmap](../../native-renderer/roadmap.md). Shadow correctness and MSAA remain
queued. This is an investigation and phased implementation plan; a design with
measurable acceptance criteria follows the presentation captures.

## Goal and initial findings

Reproduce the game's existing HUD, menus, loading screens, dialogue/subtitles
and cutscene presentation in the native view. Preserve the game's appearance,
layout, animation and input behavior. Begin with gameplay HUD; determine the
other routes from evidence rather than assuming they share the same shaders.

The current native composite replaces the guest image with a world render.
Main-scene records are selected by the guest's tiling bracket. Existing quad
collection skips outside-scene draws after the post composite. Records-enabled
frames can publish an empty world scene, which replaces UI-only frames with
black. A null publication retains the guest picture; neither establishes native UI.

Existing bounded UI render/input/text probes and gameplay discovery captures
provide a starting point. Outside-scene rows also include reflection and shadow
passes, so their presence alone is not evidence of captured HUD. Some immediate
outside-scene vertex paths omit microcode/register/fetch information needed for
native replay. Research details and source anchors live in
`.superpowers/sdd/presentation-research.md`.

## Work sequence

1. **Research and discovery.** Inventory existing hooks and captures.
   Identify HUD draws by their position in the frame and controlled UI changes;
   establish shader pairs, vertex/index storage, texture/font atlas ownership,
   constants, viewport/scissor, blend/depth/alpha state and colour/gamma order.
   Record gaps before changing capture. Collect equivalent evidence for title,
   pause/menu navigation, loading transitions, dialogue/subtitles and at least
   one accessible cutscene. Determine whether each cutscene is realtime or video.
2. **Design and acceptance criteria.** Choose a route consistent with eventually
   leaving the emulated GPU. Specify frame-owned UI records independent of a
   world scene, bounded storage/cache lifetimes and exact placement relative to
   post and scanout. Define unsupported-frame behavior and named counters.
   Separate temporary guest fallback from native acceptance. Establish image,
   clipping, ordering, animation, transition and cost bounds from paired captures.
3. **Gameplay HUD.** Add only evidence-backed capture fields and shader families;
   render sprites/text in observed order. Verify texture and glyph changes,
   clipping, transparent overlaps and UI placement in native/split/overlay views.
   Test meaningful pure rules and run affected checks, release build and the
   software/debug gate before hardware evidence for resource/shader changes.
4. **Menus and loading.** Support UI-only frames and transitions without a world
   scene or post data. Verify navigation, selection, aspect/resolution handling,
   fade order and returning to gameplay without a stale native frame.
5. **Dialogue and cutscenes.** Preserve subtitles, dialogue choices, letterboxing,
   fades, skip/pause behavior and return to gameplay. Implement any video route
   only after identifying its surfaces and ownership; do not treat video and
   realtime scenes as interchangeable.
6. **Review and closure.** Audit supported and unsupported families and all
   transition paths. Record native-versus-fallback results separately, evidence
   identities, remaining user checks and defaults. Update the roadmap and ledger.

## Execution constraints

Preserve the existing working-tree edits and water follow-up evidence. No
staging, commits, branch changes or SDK edits are part of this handoff. New
runtime capture must be bounded, validate guest addresses/constants, publish
immutable per-frame data and have negligible disabled-path work. Avoid a second
gamma/exposure application and never reuse stale world/UI data on a transition.

Use the established `tools/drive_game.ps1` harness for game runs, at most 100
seconds, mouse look off. Use Hero000 for gameplay; establish and document a
separate title/menu/cutscene route when the boot-save path bypasses it. Existing
controller UserPresent authorization applies; label those runs RUN UNCHECKED.
Check run health and inspect original screenshots before using measurements.
Build/test while the game is closed. Do not change display settings or resize
evidence. A bounded software/debug run is not the historical 240-second stress
gate. Store raw evidence and scripts in `out/native-evidence/presentation/`.

## Current status (2026-10-13, build 73)

Verified on hardware (user-present runs, RUN UNCHECKED, zero D3D messages,
stable binaries) with paired native/guest readbacks of the same GPU callback:

| View | Readbacks | Mean RGB error (/255) | Coverage within 1 px, 2 levels |
|---|---|---|---|
| Gameplay HUD, opaque UI pixels (coverage plane) | hud-menu 194508: 1547, 2993 | 0.23 | 99.96% / 100% |
| Pause root | hud-menu 194508: 2102; hud-submenu 192810: 3407, 3739 | 0.24 | 99.7% |
| Quests/Maps (four native masks) | hud-submenu 192810: 5284, 5957 | 0.24 | 99.2% |
| Title and save menu | title 193934: 4428, 5815 (2665 at 0.41 / 97.7%, animated particles) | 0.27 | 100% |
| Opening movie (four frames of one run, mutually 57-62/255 apart) | cutscene 194956: 1039, 1489, 1939, 2389 | 0.26-0.28 | 100% |
| "Scene paused" box over the k_5_6_5 scene backdrop | cutscene-skip 195937: 1608, 2197 | 0.25 | 99.9% |

Transitions (pause enter/leave, title to save menu, movie to realtime) compose
natively with no stale frame; the skip's two frames without draws and the
opening's 577CB/15675 distortion composite frames retain the guest picture
through the explicit fallback (counted, not native acceptance). Snow and
soft-depth billboards draw on hardware; the black fade is native. The full
standalone suite (49 PASS lines, 16 Maps and 8 pause/hardware captured mask
copy spans, 16 HLSL entries) and the 100 s WARP debug-layer run (four known
1328 and one known 1008 warnings, zero errors) pass on the final sources.

Timing (user-present, RUN UNCHECKED, Bowerstone Market, uncapped): the native
UI adds 0.15 ms of frame time and 0.65 ms of capture-thread guest time per
HUD frame (23.3 versus 23.7 fps; the scene is GPU-emulation bound at 23 fps
with or without it, 28.8 ms busy for 5,500 draws); the native pause menu runs
at 75 fps. Criterion 9's cost bound holds; its 30 fps floor is the scene's
own baseline on this branch, not a presentation effect.

Open: criterion 9's away-gated measurement (user check 116:
`out/native-evidence/presentation/timing_ab.ps1`); the opening dialogue's
clothing/skin material 44B567635EB10620 is a further lighting family (its
microcode reads c36) and stays clay natively; F51's native DOF is proven but
no exercised route executes it; half-resolution effects and the distortion
producers remain unimplemented (guest retained). `fable2_native_ui` defaults off.

## Findings of the closing session (builds 56-73)

- Builds 52-55 never observed the mask bin reset: the 822A6318 override lived
  only in `fps_probe.h`, which `fable_2_app.h` does not include, and the hook
  generator skipped the symbol for it. Hooks come from the generated table
  only; `tests/test_gen_census_hooks.py` guards the real files.
- The pause menu clears its mask target in a band (rows 352-360) under each
  mask, the later clears under the copy quad's SrcAlpha blend with alpha 0; the
  four masks share one EDRAM target in sequence. The native producer renders
  them in order into one scratch target (clear rectangle under the captured
  blend, quad, texel-exact resolve) instead of a full per-target clear.
- The copy span after a mask quad can cross a ring kick: two EVENT_WRITE_SHD
  fences, then a 7-11 word segment descriptor that is not PM4, then packets
  resuming at `C0004600 00000019`. The parser resumes once at that marker; the
  Maps fixtures' two "whole-gap refusals" were this boundary.
- The copy rectangle has two forms: pixel edges {0,0,640,0,0,360} (Maps) and
  pixel centres {-0.5,-0.5,639.5,-0.5,639.5,359.5} (pause); both are pinned.
- k_5_6_5 textures: the texel's low field is red; host B5G6R5 holds red high.
  `Exchange565Fields` swaps the fields after the endian swap (the 1555 rule).
- HUD readbacks are judged on the UI's own opaque coverage (an R8 plane the
  pass renders while dumping; F2UI v2), because translucent panels show the
  native world, which differs from the guest's.

## Realtime route material and effect gaps (run 802)

The user confirms that the black town background is fixed, and reports missing
building/eye textures and falling snow. These defects remain part of step 5.
Trace the unsupported FC0922A3, 8C512607 and F96A4494 materials to their own
texture coordinates, alpha and light formulas. Register only proved variants;
retain the existing world lighting limits. Identify eye textures from captured
assets before attributing a shader to Rose. Check snow's particle geometry,
dynamic curve constants and compositing position, including its relationship
to title particles. Validate resource changes with software/debug before a fresh
hardware scene comparison. Preserve the opening-dialogue route and its evidence.

Building FC0922A3 and eye F96A4494 are proved and registered; the user confirms
both textures in build 16. The billboard vertex and three particle pixel formulas
pass independent checks. Native capture separates full-size title particles,
main-scene billboards and half-resolution effects by their actual targets.
Title particles enter the UI draw order; main-scene billboards use native depth
before post. Soft-depth particles are implemented using an owned native depth
snapshot, but their world resource and visual gates remain open. Half-resolution
effects remain unimplemented. Window clipping follows bounded CPU-emitted
commands. The stitched native world uses the same-frame logical window before
XDK tile intersection; tiled UI now uses that same fresh logical window, while
untiled UI uses emitted physical state. Software title run
812 renders native labels/text, and town run 816 actually draws five billboard
records against native world depth. Soft-depth execution remains unverified.

Snow is procedural point-list VS 19A01C01290E20A7 / PS 718C12FF9E1B888B,
not the smoke billboard. Its closed grid/hash, flutter, eight capsule forces,
plane rejection, point size, distance fade and rotated atlas formulas now match
512 independent microcode trials. The actual own HLSL matches those trials too;
pixel coordinates/colour match another 512 trials. Native records own validated
constants and texture slot 13, bounded to 64 weather draws/131,072 points per
frame. GPU quad expansion uses native depth and preserves observed effect order.
Builds 31-33 and focused bounds/shader tests pass. Software runs 818/820 execute
native snow without D3D errors; hardware 819 shows snow on both halves. User
moving-snow and colour/occlusion verification remain pending. Build 33 also
owns the observed black cutscene fade and composes valid world frames with no
HUD. Its software fade gate passes; other fade colours and the no-HUD transition
need separate acceptance. The 577CB5267A8C92C3 post/distortion variant remains
unimplemented. Fresh static HUD/menu comparisons and full realtime route remain.

Hero003's START held at 40s and 55s establishes a repeatable opening-dialogue
route by 95s. Runs are at the display-sleep resolution (1296x759 screenshots),
so they do not establish normal-display timing. All spec criteria remain required.

## Menu history precision follow-up (runs 823-828)

Captured menu draws use an R16-float UI target followed by R10 mapping and
history blend. Native history now owns that R10 destination and the nearest
scan-out table lookup. Run 828 is stable with no D3D errors, but still fails
99% pixel coverage (pause 98.8%, submenu 97.0%). The copy shader's tf13 is
RGBA8, so the previous R10 frame is resolved to eight bits before sampling.
Reproduce this conversion before native history blending; check actual own HLSL
against independent copy/mapping evaluation and rerun software/debug, then the
same hardware menu/submenu pixel gate. Do not infer image acceptance from the
formula proof. The user is exercising the Rose-to-Murgo route in build 36;
do not build or run tests while that local game session is active.

## Murgo route defects (run 829)

The user confirms dialogue movement but reports missing crowd hair and a
flickering guiding light. Captured main-scene F2F103F16764043B draws use the
inspected hair/beard atlases 14200054 and 14090054, with existing skinned UV
entries. Their albedo is tf0(c8.xy*uv+c8.zw), squared for lighting; alpha is
tf0.w*c26.w. The four-light formula matches the independent shader with flat
normal maps and controlled near/far shadow maps. Prove both shadow endpoints,
first-light and actual sun slots, then register the material/alpha and only
checked lighting pairs. A22982E86122DA41 is a second four-light iris variant
paired with 79EAC49585797037, also to prove and register. This does not extend
the existing native normal-map/cascade-shadow approximation. Build and software
gate the changes before a fresh local Murgo comparison. Trace guiding-light
records and producer/composite dependencies separately; do not infer its shader
from an untextured-draw count or call a still screenshot a flicker proof.

## Guiding light: half-resolution producer and B863 composition

Run 829's off-scene 560x360 DBFD/C300 particles use spark textures 15000054 and
14D60054. Their owned vertex outputs project along the golden trail and carry
yellow/orange RGB. The unsupported 775C spline uses flame atlas 14BE0054 instead.
Implement the guide's separate RGBA8 accumulation, native depth snapshot and
captured producer gate. B863 samples this texture linearly at a half-texel bias,
adds c47.x times its RGB to the scene, then maps tone with c77.x contrast,
c78.xy saturation/gain and c79.x exponent. Preserve frame ownership and clear
colour every exercised frame; do not reuse a target after refusal. Prove the
actual tone HLSL and exposure coordinate, retain legacy shader checks with the
new effect disabled, build, software/debug gate, then compare the moving guide
in a local Murgo session. Exact depth sample alignment and target switching need
hardware evidence. Other DOF/distortion composites still require separate work.

Build 43's moving comparison fails: the user sees native-only flicker before
the alley. Correct the separately identified half-depth pixel offset by owning
the completed producer quad and its UV/viewport, rather than synthesizing UV
from host centres (build 44). Validate shape, bounds and late input, compile
actual shaders, rebuild and run a software/debug gate. Keep moving acceptance
open. The next capture must show gameplay with the trail, rather than remaining
in Murgo dialogue. Inspect the actual active composite and record counts in
that view before attributing the remaining defect.

The opening route uses another DOF/distortion composite, 15675EB73FB39170.
Its native post failures need their own fix: own the depth, blurred-scene and
distortion producer dependencies and normalize the captured tone registers.
Separate the two EA83/28DB blur chains by source identity; the DOF weights must
not overwrite the bloom weights. Validate all implemented branches against
independent microcode. Retaining the legacy tone while silently dropping DOF
or distortion is not full native acceptance of these variants.

## Menu source resolve (run 831 follow-up)

All captured pause/Maps UI draws target format 12 (unsigned 7e3 represented by
R16-float on the host). SDK Resolve calls DumpRenderTargets, including its
current direct-resolve path, and clamps/rounds this image to 7e3 before resolving
to the F16 input of 93E7. Reproduce that stage after native UI blending and
before menu pow/colour mapping; do not clamp sprite output before blending.
Validate the captured F16 source and native draw target formats. Check actual
HLSL on WARP against independently enumerated representable values for every
stored half-float and midpoint neighbour. Rebuild, software/debug gate and
repeat the same pause/Quests/Maps paired image comparison; formula correctness
alone does not close the 99% coverage failure.

## Unexpected local exit and opening composite references

The user reports a crash during build 44 run 837, before reaching the guide
comparison view. The log starts window shutdown before guest execution ends;
no Windows fault/hang report or dump was found in the corresponding interval.
The close trigger remains unresolved. Record the window-close-request and
guest-thread-exit hooks in the project app, without altering lifetime or SDK
sources. Verify the release build and use the next permitted local/away run
to inspect their ordering. Preserve this as an open defect until reproduced
or attributed; a shutdown log alone is not evidence of a voluntary close.

Closed 577CB/15675 post references now pass 1,024 independent microcode cases,
including displaced scene/bloom/table coordinates and DOF branch endpoints.
Native support still needs its own distortion/depth/blur images and actual
HLSL proof. Identify those producer chains from their captured fetch sources
before extending capture; keep bloom and DOF blur weights separate.

Build 47 separates the current-frame bloom kernels from the later format-29
DOF kernels (bloom sources are format 7/54). Regression and release checks pass;
fresh game evidence remains pending. Next establish F51AF7DB's non-distorting
DOF composite, including its captured infinite near slope/zero cap. Prove the
actual native formula, refuse singular or unsupported depth/producer inputs,
and produce its HDR blur from native scene/depth before enabling it. Preserve
the existing PostBlock size and legacy post path. Distortion families still
need their separate owned distortion producer; they cannot inherit this result.

F51's capture/native producer/composite and explicit fallback are implemented.
Amount/tone passes 768 independent cases; HDR downsample and each blur axis
pass 512; actual HDR resolve passes 163,836 WARP cases after correcting half
storage rounding. Build 48 passes, build 49 adds directly evaluated used kernel
helpers. Before hardware acceptance, complete a bounded software/debug game
run and establish F51 execution from logs and captured inputs. Keep the native
R11G11B10 world approximation disclosed. Then repeat menu pixel gates and the
moving-guide route; source proofs do not close those visual or timing gates.

Distortion follow-up identifies a separate 8D2C/00D85 half-size RGBA8 producer
and 0A96 quarter-size reduction feeding 577CB TF3 in two run-837 frames. Its
unlit billboard and squared-texture formulas now pass 512 cases each; the
bounded native vertex helper passes 512 interpreter fixtures and refusal
checks. Keep this helper unwired until preceding depth and indirect command
dependencies are closed. Implement a separate owned distortion target and
reduction; do not mix its formula or image into guiding-light effects. Tie the
new same-frame producer to each supported consumer, then prove the used HLSL
and complete software resource execution before visual acceptance.

Known unsupported distortion composites now carry an own-frame marker and
retain the guest world explicitly, rather than inheriting an ordinary kept
tone block. This fallback preserves independent menu/movie rendering and does
not complete distortion support. The UI mask census identifies four 0654/81BA
half-size atlas-copy producers; own their bounded geometry, clears, targets
and resolve associations before replacing the current mask snapshot path.

Build 52 implements eight bounded native 640x360 RGBA8 mask targets. Active
sprite/text mask consumers bind their own-frame producer index. Capture owns
the atlas-copy matrix/UVs, filled indexed geometry, clipping and pinned blend,
the full rectangle/colour clear, and draw-time copy destination/pitch/format.
The existing FPS bin-reset override supplies observed untiled mask/select
packets; no assumed initial GPU state establishes a predicated clear. Observe
the resolve before the next tiled target's command reservation, and refuse
unproved spans, cropped copies or pending producers at publication. Keep
static atlas snapshots in the existing budgets; retain no guest mask snapshot.

Vertex and actual pixel HLSL each pass 512 independent cases, the native vertex
helper passes interpreter fixtures, and 16 captured complete/prefix copy cases
include two deliberate whole-gap refusals. All 23 presentation/particle/DOF
HLSL entries, frame tests, eight presentation Python tests and release build
52 pass. Complete the software/debug resource run, establish actual native
mask execution/ownership from its logs, then repeat paired menu pixel gates.
Formula and release checks alone do not establish a correctly rendered mask.

Build 54 tightens the early boundary to pending masks with committed direct
copies, preserving ordinary UI/video capture timing. Clears require full
clipping and opaque full-channel state; conditional execution/memory writes
and fetched state refuse ownership. Standalone actual mask HLSL/raster/blend
passes on D3D11 WARP over 230,400 pixels, within one level for constant alpha
and two for varying alpha (host filtering/blend precision disclosed). Release
and focused captured-span/vertex checks pass. Build 52's away attempt refused
before launch; the build-54 retry retains the full absence gate and watch.
Live native mask execution and paired Maps accuracy remain required.

The build-54 retry refused before launch after 600s of input/audio resets;
preserve its stable identities and empty game-file inventory. Subsequent source
review also invalidates window ownership across conditional/fetched/memory
operations, and refuses a bin-mask/select change within a mask draw. Focused
frame/mask checks pass. Retry live validation only through the existing gate
and watch; no game acceptance can be inferred from these refusals.
