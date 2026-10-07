# Boot save

Load a named save straight from the front end, with no menu input, to shorten test launches.

## Use

```powershell
.\tools\drive_game.ps1 -BootSave Hero000 -Total 60 -Shots "35,50"
```

- `-BootSave HeroNNN` sets `FABLE2_BOOT_SAVE`, drops the default key plan (give `-Plan` to add steps; times are still seconds since start) and sets `FABLE2_SKIP_LOGOS=1` (`-Env @{FABLE2_SKIP_LOGOS="0"}` keeps the logos).
- The driver prints the plan the game is given (`-BootSave Hero000: the game loads the save itself; plan for the game: none`, or the plan in the game's button names). A run with `-Plan` must show the game's `[autoplay] armed` line in the game log, or the driver ends it `RUN ABORTED: the game did not accept the plan` (exit 12); the line `[boot-save] armed: HeroNNN`, which every boot-save run logs, does not count. `-PlanShift N` then moves the plan, the `-Shots` times and `-Total` only: there are no menu steps to shift.
- `HeroNNN` is the save folder name under `<exe>\saves\<profile>\4D5307F1\00000001\`. The driver refuses (exit 2, before the idle gate) unless exactly one such folder with a `mainsave.bin` exists; a name that is not `Hero` and three digits, `-KeyboardInput` and the driver's test mode are refused the same way.
- If `<exe>\saves-pristine\HeroNNN` exists, the live save folder is first moved to `<exe>\saves-replaced\<timestamp>\HeroNNN` and the pristine copy is put in its place, so every run starts from the same save. Nothing is deleted, and the live save is not touched until the copy is complete. Without a pristine copy the live save is loaded as it is. The swap is made after the plan has been checked and the idle gate has passed (or been skipped with `-UserPresent`), so a refused run moves nothing; if it fails the run ends `RUN ABORTED: could not restore the pristine save ...` (exit 12) with nothing launched.
- Start captures at 33 s or later (see Results).

Environment options read by the game (all unset: nothing changes):

| Option | Effect |
|---|---|
| `FABLE2_BOOT_SAVE=HeroNNN` | Load that save from the front end. Implies tracing. |
| `FABLE2_SKIP_LOGOS=1` | Do not queue the Microsoft and Lionhead logo videos. |
| `FABLE2_BOOT_TRACE=1` | Log front-end state changes as `[boot-save] frame N: ...` lines. |

## How it works

The retail front end's Load Game and Continue handlers (`sub_826C9280`, `sub_826C72A8`, `sub_826C81A0`) do three things, and `src/core/fable2_boot_save.h` runs the same three from the per-frame main loop hook (`MainRenderLoop_82B9CD68` in `src/diagnostics/fps_meter.h`) every 30 frames (about once per second) while the front end is up. It stops for the rest of the run when the load starts, after 60 failed attempts (one `gave up` warning), or when the front end is left another way (the player loaded by hand):

1. Pick the user's storage device if none is chosen yet: `sub_824E54C0(manager, user)`, then `sub_824E5398(manager, user, 0)`.
2. Read the save's info: `sub_824E4E30(info)`, `sub_824EA7A0(manager, info, name, user)`. The info holds the save's level name at `+12`.
3. Start: `sub_8237A2D8(gui, name, levelName)`.

Objects: `root = *0x83496928`; `game = *(root + 12)`; `manager = *(game + 40)`; `gui = *(*(root + 8) + 80)`. The byte at `game + 92` is 0 while the front end runs.

`FABLE2_SKIP_LOGOS` overrides `sub_822F4958`, which queues the two logo videos, to queue nothing.

Addresses are for code group `goty-identical-payload-2026-09-28` (both accepted GOTY versions); `fable2_boot_save.h` asserts it at compile time. The pure parts (name check, attempt pacing, save folder lookup) are in `src/core/boot_save.h`, tested by `tests/native/test_boot_save.cpp`.

## Results (2026-10-07, worktree release build, save Hero000)

Seconds from process start to the first `Forced readback-resolve` log line (the world starting to draw):

| Run | Seconds |
|---|---|
| Default key plan, logos (baseline, `fable_2_276.log`) | 40.2 |
| `-BootSave Hero000`, logos skipped (`fable_2_008.log`) | 22.6 |

Timeline of the boot-save run: logo queue skipped at 2.1 s; game, save manager and GUI objects present at 5.0 s; `loading Hero000` at 5.4 s on the first attempt; front end torn down at about 18 s; world drawing at 22.6 s; screenshots at 25 s and 40 s show the world with the HUD.

The load itself takes about 17 s here against about 6 s after the last key press in the baseline: with the logos gone the game has not finished its background loading when the save starts. The net gain is about 18 s per launch, not the 30 s first estimated.

Runs 277 to 279 of the branch `native-renderer-materials` (2026-10-07, release build of the merge `f7b5f87`, `-UserPresent -BootSave Hero000`, no pristine copy; copies in `out\native-evidence\materials`: `final_bridge_fable_2_277.log`, `final_trees_fable_2_278.log`, `final_off_fable_2_279.log`): the world draws from 23.9 s, 22.6 s and 22.5 s, measured from the log's first line to the first `Forced readback-resolve` line; `loading Hero000` is logged at 6.9 s, 5.4 s and 5.3 s, each on the first attempt.

Open question from those three runs: in the world the guest work median is about 2 to 3 ms above runs 272, 273 and 276 of the same day, which started through the menu (native view at the bridge 10.7 to 11.7 ms against 8.3 to 8.7 ms, by the trees 9.6 to 10.6 ms against 7.5 to 8.0 ms, renderer off 9.9 to 10.1 ms against 7.0 to 7.4 ms; `docs\native-renderer\frame-map.md` section 13, "Final fix wave"). The rise is the same with the renderer on and off. Whether it is the machine's load at that hour or the game's background loading still going on after a boot-save start (the paragraph above) was not separated; a menu start and a boot-save start run back to back with the renderer off, on a machine left alone, would say.

## Not verified

- `-BootSave` with the logos kept (`FABLE2_SKIP_LOGOS=0`) on the final build.
- Other heroes, checked against the save files rather than by a menu load: `-BootSave Hero003` and `-BootSave Hero001` each logged `loading <hero> (attempt 1, ..., valid 1)` (`fable_2_010.log`, `fable_2_012.log`, build with the retry cap) and opened in the opening cutscene, where `Hero000` opens on the bridge. Each save's `chaptersave.bin` is plain XML after a 4-byte length: `Hero000` says chapter 2, level `BowerLake`, quest `QC060_NewBeginning`; `Hero001`-`Hero005` say chapter 1, level `BWSSlums`, quest `QC010_Childhood`, and their `herosave.bin` files are byte-identical. So those five are new-game saves and the opening is what they hold. Loading one from the menu by hand would be the direct proof.
- The German GOTY version (same code group, not run).
- A run under the idle gate (all runs so far were `-UserPresent`).

## Dead ends, for the next reader

- `sub_82692230`, `sub_82692048`, `sub_82695590`, `sub_826962D0` (the `0x8269xxxx` cluster) are a second, non-retail front end. A hook on `sub_82692230` was never called in three runs; from reading the code, its callers skip it when the byte at `0x833194D0` is set.
- The game update `sub_82355460` does not run while the front end is up; a hook there first fires after the menu is passed.
- The save info lookup fails until a storage device is chosen for the user (step 1 above).
- The retail image has no `SetSkipFrontEnd` binding. `SetInitialSaveGameName` exists (global at `0x834973BC`), and `data/scripts/Startup/mystartup.lua` is a loose text script; setting the name from there was not tried, because the start call above writes that global itself.
