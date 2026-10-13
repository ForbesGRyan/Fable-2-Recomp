# Campfire tree material follow-up

The user confirmed the ground colour now looks right in run 764, but reported trees still untextured. The four split screenshots show a large grey trunk beside otherwise textured smaller trunks. The capture has two main-scene draws per frame of pixel shader 7928BAA7A6691A5D paired with the existing deformed trunk vertex shader 475EC9F795E5EDBB. They use the same two bark textures as the supported two/four-light trunk variants, but have no material or lighting classification. The pixel dump contains seven lights with the sun in slot 4.

1. Trace texture coordinates, squared colour/alpha division, alpha output, predicates and captured state. Inspect both bark thumbnails and confirm the mesh is the visible trunk from captured transforms/geometry.
2. Extend the offline object driver to use the existing seven-light setup. Prove the new pair against the established trunk formula in 400 first-light trials and 400 sun-slot-4 trials, including randomized alpha and masks. Check captured literals and sun placement. Add regression tests before registering the material.
3. Add only the proven material, alpha and lighting definition; regenerate the table. Retain existing vertex decoding, geometry, fail-closed validation and renderer shaders. Update existing fog family metadata if present, without changing its formula.
4. Run affected light, material generator and fog tests, inspect the diff and build release with no game running. Preserve all pre-existing user edits. No shader/resource changes are planned; if that changes, run the established debug/WARP gate.
5. Use the official driver, BootSave Hero000, root-owned UserPresent authorization, mouse look off, at most 100 seconds. Revisit the campfire with the user, audit health before inspecting originals, check both bark draws have valid material/light inputs, and compare the native trunk with the emulated view. Full read-back is diagnostic and carries no timing claim.

Evidence: out/native-evidence/materials/campfire-trees/. No pushes, branch switches or staging of user edits. Acceptance requires offline proofs, affected tests, build and a healthy hardware comparison. The ground colour confirmation remains recorded separately; run 764's paired captures use the older terrain variants, so hardware exercise of the new seven-light terrain variants remains to be captured.


## Completion

Steps 1-5 completed. The offline colour/sun/alpha and vertex-fog proofs passed, 249 affected tests passed, and release build passed. Run 765 is discarded for a white loading stall. Healthy retry 766 captured both new bark draws with valid material/light inputs and passed both full discovery checks. Root viewed every original screenshot; the user confirmed bark visible. The reported campfire tree material defect is confirmed fixed in this view. Full record: docs/native-renderer/campfire-tree-fix.md.
