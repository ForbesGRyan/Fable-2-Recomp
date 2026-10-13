# Campfire terrain colour follow-up

## Investigation

Run 763 reproduced the user's campfire view after the missing material definitions were added. The user confirmed restored textures with an incorrect colour. Both paired captures contain 26 main-scene E0996BF4D9539D38/C30A97D946FA2BE4 layers and 13 0981301D645E2517/FB68A7F2301210E1 layers per frame. All 39 report `lin.reason=light-unknown`, `why=no-family`. Geometry/material validation passes; the renderer reports 406/406 material draws, zero flat fallbacks and zero omitted layers.

Both microcode dumps contain seven lights: positions c80..c86, colours c87..c93, first-bank masks c112/c114, second-bank masks c113/c115. The captured sun is c84/c91 (slot 4), matching the frame's sun; c113 is one-hot at its first component. The first four slots are local lights. `PlanTerrain` makes a whole square unlit if a layer cannot be lit, amplifying these missing classifications across otherwise supported layers. Existing `FOG_SHADERS` already includes both hashes; missing fog definitions are not the cause. The native terrain shader applies the established lighting/fog path only to lit terrain.

Material colour and alpha were already checked against the interpreter in 400 trials per shader, with normal maps affecting lighting rather than texture coordinates. This follow-up addresses the lighting classification and preserves the native renderer's established sun/ambient model. Local point-light rendering and pixel normal mapping remain outside that model; do not claim exact emulated shading equivalence.

## Plan

1. Extend the offline light checker to represent seven-light terrain variants, their literal tables, and the second three-slot bank. Add failing tests for seven-light counts, the captured sun slot 4, and formula agreement. Preserve existing two/four-light and mesh/leaf behavior.
2. Prove each new terrain pair's ambient/sun formula against full pixel microcode: random steep and flat inputs, first-slot light isolation, and the game sun in slot 4 with other colours zero. Run 400 trials for each proof. Mine all four captured frames to verify sun placement, literals, constants and vertex pairing. Do not register a pair that fails.
3. Add the proven `terrain-layer-multi` lighting definitions with facing scale 8 to the two material entries; regenerate the capture table. Keep the existing material mappings, fog formulas, validation guards, layouts, shader text and DrawRecord size unchanged. Update fog's offline family metadata only if required for coherence.
4. Run the affected checker/generator/fog tests, inspect the generated diff, and build the normal release executable with no game running. No product shader/resource layout changes are planned; if that scope changes, satisfy the established WARP/debug gate before hardware runs.
5. Run the official driver with the user present, foreground the window, and revisit the campfire. Capture paired scene/discovery evidence and original split screenshots. Audit run health before using images; full read-back is diagnostic, never timing evidence. Confirm both variants now have valid light inputs, no terrain omissions, restored lighting in this view, and the user's colour assessment. Record remaining differences honestly.

## Constraints and evidence

Use the existing bounded readers and checked shader-pair tables. Never infer an unsupported shader's layout or weaken the fail-closed path. Preserve all pre-existing user edits, including wind changes and the manifest; do not stage them. Never push or switch branches. Gameplay uses `tools/drive_game.ps1 -BootSave Hero000`, at most 100 seconds, mouse look off; the user's existing authorization permits root-owned `-UserPresent` runs. Evidence lives under `out/native-evidence/materials/campfire-ground/` and `out/native-evidence/lighting/campfire-colour/`.

Acceptance: offline proofs pass, existing affected tests pass, release build passes, both new shader pairs are lit in a healthy campfire run with materials intact, and the resulting screenshots/user feedback substantiate the improvement. This does not close the separately grey tree trunk.


## Completion

Steps 1-5 completed. Offline first-light/sun-slot proofs400each and affected tests/build passed. Run764 confirmed user-visible colour but did not capture E099/098. Final healthy run767 captured all120 affected terrain rows with valid light inputs, facing8, checked pairs and zero terrain omissions, and passed both full discovery audits. Root viewed all4 originals; the user confirmed ground colour right in the caravan/lantern view. No timing or exact pixel-equivalence claim. Detailed results: docs/native-renderer/campfire-ground-fix.md. The separately grey trunk was fixed under the tree follow-up plan and confirmed in runs766/767.
