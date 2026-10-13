# Campfire ground material fix (2026-10-12)

The native half showed a large grey ground area beside the campfire reached by turning right from the bridge, descending the hill, and turning right around the corner. Run 761 reproduced it while the emulated half retained the ground textures.

The two paired discovery captures contain 26 main-scene terrain layers per frame using pixel shader `E0996BF4D9539D38` and 13 using `0981301D645E2517`. Neither had a terrain material definition. Their 39 failures match the logged `terrain-inputs` count: the whole-square fallback drew eight flat squares and omitted 31 layers. The fix adds these two verified definitions and regenerates `ps_albedo_table.inc`.

- `E0996BF4D9539D38`, paired with vertex shader `C30A97D946FA2BE4`, uses detail texture slot 0, baked-light slot 3 and the existing steep-ground side projection from slot 13. Instructions 19/26 fetch and square the detail colour; 21/22 select the steep block; 27/28 fetch the side colour; 43/44/46 mix it. Captured literals give threshold 0.99609375 and epsilon 0.0001. Instructions 189–192 supply the existing distance fade and layer weight.
- `0981301D645E2517`, paired with vertex shader `FB68A7F2301210E1`, uses detail slot 0 and baked-light slot 3 without the steep block. Instructions 18/19 fetch the base and detail; 22 squares the detail; 159–162 supply distance fade and layer weight.

The interpreter matched material colour and alpha in 400 random trials for each shader, including shadow boolean b135 on/off and steep/non-steep inputs for the first shader. The largest absolute error was 2.22e-16. The proof isolates material by using a white baked-light sample, ambient floor 1, zero light colours and disabled fog. It does not establish lighting or fog equivalence. At the material-only stage lighting remained unclassified; the fog definitions already existed (see correction below). No shader text, resource layout, renderer validation or geometry decoding changes.

Decoded detail thumbnails (`17200052.png`, `15420052.png`) and side-rock thumbnail (`170A0052.png`) were inspected. The generator and terrain replay suites passed: 90 tests, two skips. The release build passed.

Evidence is local and ignored under `out/native-evidence/materials/campfire-ground/`: run 761's full log, both discovery captures and geometry folders, paired scene files, original screenshots, both shader dumps, texture thumbnails, and `campfire_material_proof.py` / `.json`. This is a materials follow-up after water closure; it does not revise the water project's historical validation or claim that the separate grey tree trunk is fixed.

Run 762 exercised the rebuilt executable for 100 seconds (RUN UNCHECKED, user-present mode, full read-back, no timing claim). The foreground window was verified, the logs show no known freeze signature or D3D12 errors, and all four original screenshots were viewed at 1942 by 1136 pixels. At the bridge, every stats snapshot reported 564 material draws, zero flat fallbacks and zero omitted terrain layers. All captures remained at the bridge; neither new shader occurred there, so this run establishes only the bridge smoke check. The campfire after-fix visual check remains pending.

Built executable SHA-256: AE5C440859B27FF5F9BA553E5E678C94FD0243F285A6E85F80D9AA901BBE82E1. The run packet records the two DLL hashes and verifies all three binaries stayed unchanged during the run.


## Campfire confirmation

Run 763 (`campfire_fixed_20261012_181420`) reproduced the campfire view with the fixed build, after the user reported standing beside the affected ground. All four original screenshots (60, 75, 90 and 96 seconds, 1942 by 1136) were viewed: the native ground is textured throughout the formerly grey area. The user confirmed: "Yes, the ground is textured, but it's slightly off colored". The missing-ground-texture defect is confirmed fixed in this view. Colour matching remains open; the new shaders still have no lighting family; their fog definitions already exist. The large grey tree trunk is also still visible and is a separate material issue.

Both paired captures (`native_discovery_20261012_181536.jsonl`, `native_discovery_20261012_181551.jsonl`) contain exactly 26 E099 and 13 098 main-scene terrain layers per frame, exercising both new definitions. Every terrain stats snapshot reports 406 of 406 material draws, zero flat fallbacks and zero omitted layers. Full discovery checks using the vertex-binding log passed for both captures: 2822/2822 and 2800/2800 sampled mesh draws matched, with zero index, scene-frame, texture-mip, vertex-storage or water-row problems. These binding counts cover mesh draw rows, not the tessellation geometry itself.

The official driver completed its 100-second RUN UNCHECKED/user-present run with exit 0. All three binary hashes match run 762 and stayed unchanged within run 763. The audit found no D3D12 errors or known 40-ms-work freeze signature; this diagnostic makes no timing acceptance claim. Evidence is copied beside the earlier packet, including both captures and their full geometry/texture folders, scene files, run log, originals under `after-campfire-shots/`, run metadata and audit.


## Colour investigation and implementation

The seven-light terrain variants were absent from the lighting family table. Every affected row in run 763 had no-family light inputs. The whole-square lighting rule propagated these failures to other layers, producing the broad unlit colour mismatch. The prior statement that fog definitions were also missing was incorrect: both hashes already appear in FOG_SHADERS and the generated fog table. Lighting registration also restores the existing lit-terrain fog path.

The plan is docs/superpowers/plans/2026-10-12-native-renderer-campfire-colour.md. The checker now supports the seven-light second bank and registers both existing vertex pairs as terrain-layer-multi. Captured sun placement is slot 4 (c84/c91, c113 one-hot), matching the frame sun in all 156 affected rows across four captured frames. Each pair passed 400 first-light and 400 game-sun trials against microcode, with relative error below 1e-15. Flat pixel normal maps and zero other light colours preserve the established native sun/ambient model. Local point lights and pixel normal mapping remain limitations of that model.

Both material entries now carry the proven lighting definitions and facing scale 8, and the capture table was regenerated. Fog family metadata was updated without changing the generated fog table. Renderer shaders, resource layouts, geometry, DrawRecord and validation guards are unchanged. The new regression tests reject incorrect sun masks, cone/position semantics and light placement, while the pre-existing two/four-light tests still pass. Affected suites: 317 tests, two skips. Release build passed; executable SHA-256 70199BF8B6CD4351B18B98174E9453E87CD44CD9E1A0C3A82B3A5AEFDC4AC10B. Offline proof and test summary are in out/native-evidence/lighting/campfire-colour/. The hardware campfire comparison is running; outcome not yet claimed.


## Ground colour user confirmation and run 764

The user confirmed "Colour now looks right", then reported that the trees remain untextured. Root viewed all four original 1942x1136 split screenshots: the campfire ground carries colour and texture, while the large tree trunk remains grey. Run 764 completed the official 100-second driver with exit 0, unchanged binary hashes, no D3D12 errors and no known 40-ms-work freeze signature. Full read-back diagnostic: no timing claim. Logged campfire terrain is 515/515 material draws, zero flat and zero omitted; all lit records have ready light inputs. Full discovery checks passed 3000/3000 and 2954/2954 mesh bindings, all structural checks zero problems.

The paired captures at 75 and 90 seconds contain the previously supported one/two/four-light terrain variants, not E099/098. Thus this run confirms the user's visible ground-colour assessment, but does not establish hardware exercise of the newly registered seven-light terrain pairs; that acceptance item remains pending. The follow-up tree run will seek that evidence as well. The full run packet, log, paired captures, geometry/texture folders, scene files and originals are under out/native-evidence/lighting/campfire-colour/.


## Final hardware confirmation: run 767

The user revisited the caravan/lantern view and confirmed "At that view; ground colour looks right". Root viewed all four original 1942x1136 split screenshots: the former grey/off-colour ground is textured and lit, and the formerly grey trunks carry bark. The view changes between screenshots, so no identical-camera pixel-error or timing comparison is claimed. The first paired capture has identical eye position; the second differs by 0.000496 vertically, with x/y unchanged.

The final paired captures exercise both newly registered terrain pairs with valid light inputs: at 75 seconds, 26 E099 plus 13 098 layers per frame; at 90 seconds, 14 plus 7. All 120 affected terrain rows have lin.reason=none, family=terrain-layer, multi=1, facing=8 and the checked vertex pairing. The new trunk shader is also lit in all 16 captured main-scene rows. Terrain stats throughout report every layer drawn as material, zero flat fallbacks and zero omitted layers (480/480 before the first capture, 467/467 around the second).

Full discovery checks passed 3041/3041 and 3202/3202 sampled mesh bindings, with zero index, scene-frame, texture-mip, vertex-storage or water-row problems. The official 100-second UserPresent driver completed with exit 0. No D3D12 errors or known 40-ms-work freeze signature; diagnostic full read-back, no timing acceptance claim. EXE SHA-256 45CA103D53DCA6814525291BC41FF2AC86FD8559BEC3FEC57A262BCB956BEC00; both DLL hashes are recorded in the run packet, and all three hashes match run 766 and remain unchanged within the run.

The full log, both scene/discovery captures, geometry/texture folders, run metadata/audit and originals are preserved under out/native-evidence/lighting/campfire-colour/ and out/native-evidence/materials/campfire-trees/. The reported campfire ground texture/colour and trunk bark defects are confirmed fixed in these views. Ground plan steps 1-5 are complete; tree plan steps 1-5 were completed in run 766 and corroborated here. Existing native-model limits remain: local point lights and pixel normal mapping are outside the sun/ambient implementation. No coverage claim is made for unseen world materials. Historical water closure remains unchanged.
