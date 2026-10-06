# Native renderer: checks waiting for the user

Checks only a person at the game can do. Agents add a line when a sub-project needs one, and move it to "Done" with the result once the user reports it. Run everything from `out\build\win-amd64-release`.

## Pending

### Sub-project 4: albedo textures (merged 2026-10-03)
Spec: `docs/superpowers/specs/2026-10-02-native-renderer-textures-design.md` (success criteria 1, 2, 4). Results go in `docs/native-renderer/frame-map.md` section 11.

1. **Alignment.** `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=split`, then F6 through overlay and native in town, an open field and an interior. Expected: clay textures match the emulated image (same picture, orientation and scale, no scrambled tiles).
2. **Coverage.** F3 `Textures:` line in Bowerstone and at Bower Lake. Expected: the `% non-terrain` figure is at least 80%.
3. **Stability.** 10 minutes including an area transition. Expected: no crash, no `[native] textures: ... texture path off` line in the log, resident MB on F3 under the 512 MB budget, no textures that stay black or stale after an area load.
4. **Bower Lake capture (Task 9b, optional but needed for the lake's coverage).** Before launching:
   ```powershell
   $env:FABLE2_NATIVE_DISCOVERY="300"; $env:FABLE2_NATIVE_DISCOVERY_DELAY="<seconds until you stand at the lake>"; $env:FABLE2_NATIVE_DISCOVERY_EVERY="8"
   ```
   add `--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump` to the command in check 1, play to the lake and stay a minute, then `Remove-Item Env:FABLE2_NATIVE_DISCOVERY*`. Tell the agent; it adds the lake's shaders to `ps-albedo.json`.

### Sub-project 3: clay pass (carried over)
5. **Unexplained freeze seen once** (`fable_2_133.log`, frame-map section 10): the world froze behind what looked like a menu. If it recurs, take a screenshot with F3 hidden and note what you pressed.

### Other open items
6. **DebugView with `--d3d12_debug=true`.** Watch for D3D12 debug-layer errors (texture copies for mips smaller than 4 texels are the newest risk).

### Sub-project 5: coverage and skinning
Spec: `docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md`. Results go in `docs/native-renderer/frame-map.md` section 12.

7. **Capture containing vertex shader `0x2D40B53C926109BE`** (wind; same layout as `0xA584...`). It was skipped once per frame in `fable_2_135.log` during play outside Bowerstone and is absent from the autoplay bridge scene; where it is drawn is not known. Before launching:
   ```powershell
   $env:FABLE2_NATIVE_DISCOVERY="120"; $env:FABLE2_NATIVE_DISCOVERY_DELAY="<seconds until you stand where it is drawn>"; $env:FABLE2_NATIVE_DISCOVERY_EVERY="4"
   ```
   run `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=split` and stand still from the delay on for a minute, then `Remove-Item Env:FABLE2_NATIVE_DISCOVERY*`. To find the place first, play with the same command and no variables and look for `0x2D40B53C926109BE` in the log's `[native] capture: ... no-transform by vs` lines (one every 300 frames); the Bower Lake capture of check 4 may already contain it. Expected: `Select-String 2D40B53C926109BE logs\native_discovery_<stamp>.jsonl` finds rows, and `logs\native_geo_<stamp>` exists. Tell the agent; it runs `position_check.py` on the capture.
8. **Trees in overlay view** (vertex shaders `0x475EC9F795E5EDBB`, trunks and branches, and `0xA5846836C90E1192`, leaf clumps; both are in the table as exceptions to the offline in-clip rule, frame-map section 12). `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=overlay`, load the save (the bridge scene), walk off the bridge to the birches on the left and around them, and look at trees near and far; F6 to native and back for comparison. Expected: every clay trunk lies on its emulated trunk and every clay leaf clump on its emulated crown, from all sides and while the camera turns; the clay tree stands still while the emulated one sways in the wind, so a small offset at the branch tips is normal. Leaf clumps are flat opaque cards, larger than the leaves. Wrong component order would look like this: trunks lying sideways (horizontal bars through the tree or across the ground) or leaf clumps collapsed to dots at the tree's foot. Also report: whether small distant trees show at all in native view (in the autoplay shot the small tree beside the fence post behind the hero could not be told from the terrain), and whether any clump sits clearly away from its branch (the leaf shader rebuilds positions about a pivot; the table draws the stored position).
9. **Instanced plants in overlay view, and their cost while moving** (the seven instancing vertex shaders, frame-map section 12 "Instancing"; six are in the table as exceptions to the offline in-clip rule). `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=overlay`, load the save (the bridge scene), walk off the bridge onto the grass on the left, through the ferns and the tall grass, and look at plants near and far; F6 to native and back; then the same in Bowerstone, where the Bowerstone capture has 5,047 instanced rows. Keep F3 open. Expected: every clay fern, grass tuft and low plant stands on its emulated one, upright, at the same size, also the ones around your feet (the four near-field shaders `0x48D3...`, `0xFC4F...`, `0x33C0...`, `0x36B5...` could not be judged on screen from the bridge). The clay plants are opaque textured rectangles and stand still while the emulated ones sway and bend away from the hero; a small offset at the tips is normal. The game stops drawing these plants at a distance set per batch (27 to 66 units in the bridge scene) and since Task 7b the clay view cuts them at the same distance, so plants should appear and vanish in clay where they do in the emulated image as you walk; a clay plant card on ground that is bare in the emulated image, or an emulated plant with no clay card, is wrong. Wrong would look like this: plants lying on their side or upside down, plants mirrored, a whole patch shifted by a few units, cards stretched into long spikes, or texture smeared across the cards. On F3 while walking: the frame rate stays at 30 and the `Geometry:` line's `decode` stays under about 2 ms; tell the agent the highest `decode` and `hash` you see and whether `instanced` or `skipped` jumps (`bad-index` or `instance-unsupported` in the log's `[native] capture:` lines would mean an instance stream the checks did not cover).
10. **Capture with the other skinned shaders inside the main scene** (`0x3A0F9098B839DDBC`, `0x82F6433A69263C75`, `0x9ED0BA440DBD51D4`, `0x5F4416192E87005F`; frame-map section 12 "Skinning"). In the autoplay bridge scene they redraw the hero and the dog only in passes outside the main scene, so their skin readings rest on the shader dumps and could not be replayed on captured bytes. An older capture (`native_discovery_20261002_155634`, the earlier save in Bowerstone with the child hero and Rose) had `0x3A0F...` and `0x82F6...` inside the main scene, 3 and 2 draws per frame; what makes the game draw them there is not known (the plan calls it the quest-glow pass). Before launching:
   ```powershell
   $env:FABLE2_NATIVE_DISCOVERY="60"; $env:FABLE2_NATIVE_DISCOVERY_DELAY="<seconds until you stand there>"; $env:FABLE2_NATIVE_DISCOVERY_EVERY="2"
   ```
   run `.\fable_2.exe --fullscreen=false --fable2_native_render=true --fable2_native_view=split` in a place with characters that glow or are highlighted (a quest character, a character you have targeted) and stand still from the delay on for half a minute, then `Remove-Item Env:FABLE2_NATIVE_DISCOVERY*`. Expected: `logs\native_discovery_<stamp>.jsonl` and `logs\native_geo_<stamp>` exist. Tell the agent; it runs `position_check.py` on the capture, which lists these shaders only if they have rows inside the main scene.

## Done
- Sub-project 3 validation (alignment, coverage 0.949, 25-minute stability, view-off cost): frame-map section 10.
