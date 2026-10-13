# Function Manifest Sorting — TODO / Registry

This file tracks the effort to split `fable_2_manifest.toml` (5059 `[entrypoint.functions]`
entries, ~49k lines) into navigable per-theme TOML files under `config/functions/`.

The main manifest keeps only its `[entrypoint]` config (file_path, includes, longjmp/setjmp
addresses, invalid_instructions, register settings, ...) and pulls every function back in via
`includes = [...]`. The SDK merges `[functions."0xADDR"]` tables by address (last-wins), so an
address defined in two places is not a conflict — the parent manifest wins.

## The scheme (what to do for each function)

For each function, look at its **name** and its **description comment** (the comment block
immediately above the `[entrypoint.functions."0xADDR"]` entry). Then place it in the best-fitting
file below. If no existing file fits, create a new one and add a row to the Registry (name,
location, purpose).

Decision order:
1. **Named theme** — if the name (or a strong signal in the comment) matches one of the specific
   themes (loading screen, story, melee action, UI, camera, texture morph, guild chest, player
   input), put it in that file.
2. **Mechanism theme** — otherwise use the leading word of the name (the primary action): dispatch
   (Switch/Vcall/Vtable/Tail/Indirect/Thunk), memory (Allocate/Free/Pool/RefCount/MemCpy/
   Construct/Destruct/Copy), sync (Critical/Sync/Atomic/Thread/Lock), strings, collections
   (Array/List/Tree/Hash/Node), math (Interpolate/Multiply/Compute/Timebase), comparison
   (Compare/Search/Find/Check/Validate), query (Get/Read/Lookup/Access), exceptions.
3. **Generic fallback** — the auto-generated "Process…"/"Func…" data-manipulation functions that
   have no specific theme go into `data_processing/`, grouped by their 256 KB guest-address region
   (file named `0x<region-base>.toml`). Find one by its address.

New files follow the same root-level format as the other fragments (this is an included file):
`[functions."0xADDR"]` / `name = "..."` / `size = "..."` — NOT `[entrypoint.functions.…]`.

## Registry (files created)

| File | Location | Purpose | # funcs | Status |
|---|---|---|---|---|
| dispatch.toml | config/functions/dispatch.toml | Switch/state/vcall/vtable/tail-call/indirect dispatchers & thunks | 299 | done |
| memory.toml | config/functions/memory.toml | Allocation, free, pool, refcount, memcpy/memset, construct/destruct | 229 | done |
| comparison.toml | config/functions/comparison.toml | Compare, search, find, check, validate, match | 87 | done |
| query.toml | config/functions/query.toml | Getters, field readers, lookups, singletons | 85 | done |
| sync.toml | config/functions/sync.toml | Critical sections, atomics, locks, threads | 55 | done |
| collections.toml | config/functions/collections.toml | Arrays, lists, trees, hashes, search structures | 41 | done |
| strings.toml | config/functions/strings.toml | String & character operations | 41 | done |
| math.toml | config/functions/math.toml | Float/int compute, interpolation, vector math, timebase | 33 | done |
| ui.toml | config/functions/ui.toml | UI text, font, elements, menu | 23 | done |
| melee_action.toml | config/functions/melee_action.toml | Melee attack trigger chain | 14 | done |
| loading_screen.toml | config/functions/loading_screen.toml | Loading screen & new-game setup | 9 | done |
| story.toml | config/functions/story.toml | Story cutscene / chapter markers | 8 | done |
| exceptions.toml | config/functions/exceptions.toml | Exceptions, bug checks, fatal errors | 7 | done |
| player_input.toml | config/functions/player_input.toml | Player input, active player, input-event dispatch | 5 | done |
| texture_morph.toml | config/functions/texture_morph.toml | Texture morph, compress, mips, readback | 5 | done |
| camera.toml | config/functions/camera.toml | Camera system | 4 | done |
| guild_chest.toml | config/functions/guild_chest.toml | Guild chest / website / CE content unlocks | 4 | done |
| data_processing/*.toml | config/functions/data_processing/ | Generic data-processing functions, one file per 256 KB guest-address region (67 files: 0x82140000 … 0x832C0000) | 4110 | done |

**Total: 5059 functions in 84 files (17 themed + 67 data_processing regions).**

## Progress

- [x] Analyze naming landscape (auto vs semantic, mechanism taxonomy)
- [x] Fix the auto-vs-human classifier (comment signature, line-wrap-safe)
- [x] Generate the themed files + data_processing/ region files (5059, lossless)
- [x] Update main manifest: drop inlined functions, extend `includes` (86 entries)
- [x] Fill in the Registry `# funcs` column
- [x] Validate: tomllib parse every file + codegen (551 files, EXIT=0, all named functions resolve)
- [x] Full `build.cmd fable_2` (compile + link) — 300/300 TUs, fable_2.exe (183 MB), no link errors

## Result

All 5059 functions sorted into `config/functions/` (17 themed files + 67 data_processing
region files), the main manifest reduced to a clean 172-line config file, and the full
build (`build.cmd fable_2`) passes end-to-end. Done.

## Notes / known misclassifications

- Classification is name-driven, so a few functions land in a reasonable-but-not-perfect file
  (e.g. `Story_AfterNewGameVideo` → loading_screen because its name contains "newgame"; some
  `ThunkTo_UIText_*` → ui because the name carries the UIText target). These are safe to move
  between files any time — the SDK keys entries by address, so location is purely for humans.
- The `data_processing/` region boundaries are 256 KB (address >> 18). To re-granularize, re-run
  the split with a different `DP_SHIFT` and regenerate `includes`.
- Renamed/probe functions stay discoverable: `GetUser1InputState_822B2D60`,
  `DispatchPlayerAction_82748840`, `ResolvePlayerAction_823E8528`, the 12 `MeleeAction_*`,
  `InputEvent_*`, `QueryActivePlayerMethod_821EC8A0` all live in player_input.toml /
  melee_action.toml.
