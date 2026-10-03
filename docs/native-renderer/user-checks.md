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

## Done
- Sub-project 3 validation (alignment, coverage 0.949, 25-minute stability, view-off cost): frame-map section 10.
