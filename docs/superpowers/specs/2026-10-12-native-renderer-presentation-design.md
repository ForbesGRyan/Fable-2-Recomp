# Native renderer presentation - sub-project 14

The native view replaces world rendering but currently loses gameplay HUD,
menus and video. Capture the game's presentation draws as immutable frame-owned
inputs and replay them natively, including frames with no world scene. Preserve
input/navigation in the existing game; this is faithful rendering, not a UI redesign.

## Evidence and routes

Run 775 (HUD/pause/return, 100 seconds) has stable binaries, eleven guest windows,
no known freeze signature and no D3D12 messages. All four original 1942x1136
screenshots were inspected. Gameplay HUD: ten sprite and two text draws/frame
after the world composite. Pause: 126 sprite and 30 text draws/frame inside the
tiling bracket, then menu colour mapping and history copy. These counts are
specific to the captured views. Menu UI is not an opaque borrowed guest texture.

Run 776 (Hero003, 100 seconds) has stable binaries, seven guest windows, no known
freeze signature and no D3D12 messages. Five originals inspected: moving opening
video on the emulated half, blank native half. One indexed quad/frame uses three
CPU-written K8 planes and a YUV shader. This establishes video, not realtime
cutscene or subtitle coverage. Both runs are RUN UNCHECKED, with no timing claim.
Evidence: `out/native-evidence/presentation/`; manifests and copied logs confirm
775, 776 and title/menu run 777. Each has stable binary identities and a health audit.

Sprite/text formulas are checked against the independent microcode interpreter:
512 cases each for both vertex and both pixel programs, including optional second
texture, colour subtraction, mask and outline branches. Text outline extends to
zero centre alpha and preserves fully covered pixels. Movie conversion uses the
captured literal coefficients, not an assumed generic YUV matrix.

## Design

1. Presentation capture is controlled independently of post/sun and keyed by
   exact vertex/pixel shader pairs, target/state and frame identity. Tiling alone
   does not decide whether a draw is UI or world content.
2. Copy used geometry after the immediate builder's caller has filled it, before
   storage reuse; validate counts, indices, constants and viewport. Publish the
   completed set at Swap. A failed read, unsupported state or exhausted bound
   refuses the whole presentation frame, with explicit reasons/counters.
3. UI frame records do not grow DrawRecord and do not require world draw records.
   Initially bound a frame to 1,024 draws, 65,536 expanded triangle vertices,
   16,384 vertices/draw and four MiB per video plane. No unvalidated float or guest
   pointer reaches a shader. Dynamic video planes are copied into frame-owned data.
4. Render sprites and text from guest asset/font textures and captured arithmetic,
   clipping, blend/depth state and ordering. Upload mutable video planes natively
   and apply the game's own conversion. Keep movie, UI and world lifetimes distinct.
5. Compose before the scanout ramp. Preserve the observed menu mapping and previous
   frame share; prevent stale history across a mode change, off/on or failed frame.
   Apply gamma once. Native/split/overlay use the same completed native picture.
6. Unsupported frames retain the guest picture through an explicit fallback. This
   maintains usability but cannot count as native acceptance. Disabled capture
   does not decode/upload presentation resources. No SDK source changes are planned.

## Acceptance criteria

All criteria are pending unless individually backed by recorded evidence.

1. Sprite/text/video arithmetic agrees with independent shader evaluation on
   captured inputs and made inputs spanning every implemented branch.
2. Geometry, indices, constants, texture selectors and clipping match own-frame
   discovery; invalid/overflow/partial/late inputs fail closed without stale reuse.
3. Gameplay HUD, title, pause and an accessed submenu render all identified UI
   families natively; changing labels/icons and selection are visible. For static
   UI-only reference regions, mean absolute RGB error <=2/255 and at least 99%
   coverage within one pixel. Disclose quantization/filter/sampling differences.
4. Loading/title/menu/gameplay transitions never leave stale world or UI content;
   native results and fallback are counted separately.
5. Hero003 opening video renders natively, including changed frame contents, the
   captured colour conversion and correct aspect/letterbox. Mean RGB error <=2/255
   on corresponding video pixels from the same decoded frame; endpoint pictures
   alone do not establish uninterrupted motion.
6. Dialogue/subtitles and at least one realtime cutscene route are exercised;
   their overlays, fade/letterbox and return to gameplay work. A video run cannot
   substitute for this criterion. An inaccessible route remains explicitly open.
7. Off/native/split/overlay, missing world/post data and render-resource failure
   have defined current-frame behavior. Retry/device changes release or recreate
   resources, and caches/uploads remain bounded.
8. Affected meaningful tests and release build pass. Resource/shader changes pass
   a bounded software/debug run before hardware use; record its actual duration
   and pre-existing warnings. Inspect original screenshots and run health.
9. Matched quiet timing on the normal display: presentation adds <=1.0 ms/frame
   and no steady window drops below 30 fps in the measured scenes. User-present
   discovery runs supply no timing acceptance.
10. Final source/diff review, evidence identities, supported/unsupported census,
    defaults and user verification are recorded. Full completion requires every
    applicable criterion; preserve concerns rather than silently deferring scope.
