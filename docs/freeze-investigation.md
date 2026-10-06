# The world freeze: what is known and what to try next

Status: cause not established (2026-10-06). This file collects the evidence, the
candidate causes and the next experiments so a later investigation does not start
from zero. Nothing here has been fixed.

Each statement is marked **observed** (seen in a log, a screenshot or the code) or
**inferred**. File and line references come from a read-only investigation on
2026-10-06 and were not re-checked afterwards; confirm them before relying on them.

## Symptom

The game world stops: the same picture every frame, no animation, no reaction to
input. The window keeps presenting and nothing on screen explains it. It has
only been seen after the world has loaded.

- User, 2026-10-06: "the game does occasionally freeze when alt-tabbing".
- Autoplay runs (`tools\drive_game.ps1`) froze in six of seven runs made while the
  machine was in use with another window in front, and in none made with the
  game window in front and the machine left alone.
- First seen on 2026-10-01 (`fable_2_133.log`): "the world froze behind what
  looked like a menu". Later frozen runs showed no menu.

## How to recognise it in a log

Compare the `[frame] guest` lines and the `[native] capture:` lines of a run.

| | Live | Frozen |
|---|---|---|
| Guest frame rate | 30.0 fps | 23 to 26 fps |
| Guest frame "work" | about 7 ms, plus a swap wait | 39 to 41 ms, swap wait about 0.01 ms |
| Draw counts per 300-frame window | change | identical every window |
| Texture and geometry uploads | some | 0 |
| Log lines at onset | — | none of any class |

Any autoplay measurement must be checked against this before its numbers are used.
Task 8's comparison figures of sub-project 5 (`fable_2_172.log`, 981 captured /
949 drawable) turned out to be a frozen frame.

## Evidence logs

Copies are kept in `out\native-evidence\freeze-logs\` (git-ignored, like all of
`out`); the originals are in `out\build\win-amd64-release\logs\`.

| Log | Run | Freeze |
|---|---|---|
| `fable_2_133.log` | manual play, 2026-10-01 | frozen for about 2 minutes, to the end |
| `fable_2_172.log` | autoplay, split view | from about 61 s |
| `fable_2_179.log` | autoplay, native renderer off | from about 50 s |
| `fable_2_180.log` | autoplay, native renderer off | from about 77 s; the 95 s screenshot shows the F3 overlay open and the camera moved, so input reached the game |
| `fable_2_181.log` | autoplay, native renderer off | from about 72 s |
| `fable_2_182.log` | autoplay, view off | from about 91 s |
| `fable_2_183.log` | autoplay, split view | from about 47 s; watched by a foreground/input sampler |
| `fable_2_185.log` | autoplay, native renderer off | from about 50 s; watched by the sampler |
| `fable_2_186.log` | user session | from about 119 s; window closed 40 to 55 s later |
| `fable_2_188.log` | user session | from about 59 s; window closed 40 to 55 s later |
| `fable_2_184.log`, `177`, `187`, `189` | controls | never freeze |

Not examined: `fable_2_173.log` to `176`, `178`.

## What is established

**It never recovers (observed).** All ten frozen logs stay frozen until the log
ends. Whether input, or a wait longer than two minutes, would unfreeze it has not
been tried.

**It is not a hang (observed).** The scene is drawn again every frozen frame, not
re-presented: the capture cost stays at about 1.15 ms per frame and the F3 overlay
in the `fable_2_180` screenshot shows 3,952 draws.

**The "40 ms of work" is live work plus a fixed delay of about 33 ms (observed).**
Frozen frame median minus the same run's live work median is 33.0 ms in `172`,
`180` and `181` and 32 to 33 ms in most others. The meter counts the delay as work
because it subtracts only the swap and one limiter function
(`src\diagnostics\guest_frame_rate.h:104`). The F3 overlay in the `180` screenshot
reads "guest-CPU-bound", GPU idle 26.9 ms, guest pacing 0.0 ms.

**The same pacing exists in every run before the first button press (observed).**
Live runs included: 12 to 23 s shows a 39.2 ms frame with a 0.02 ms swap, and
`fable_2_188.log` shows 34.6 ms at 23 to 33 s while waiting at the start screen.
The same host thread presents those frames, the menu, the live world and the
frozen world.

**So the frozen state is a state of the game's own main loop (inferred).** The
game is running its "not in gameplay" loop over the loaded world: rendering every
tick, not updating the world. The loop can do this by design:
`ProcessGameFrame_82276C30` is a fixed-step loop on the guest timebase in which the
world update runs only when a tick counter crosses a multiple derived from two
global rate doubles, while the render callbacks run every tick
(`src\core\hotfunc\frame\ProcessGameFrame_82276C30.cpp:281-310, 619-653, 842-907`).

**Every onset coincides with a disturbance in presentation (observed).** The
`vsync present gate` lines show a dip or gap of up to 1.9 s and then a burst of 35
to 68 in one second (30 is normal). The live control `fable_2_184.log` has none.
The bursts are extra host paint requests, not guest frames (inferred: in `182` a
39/68/39 burst at 68 to 70 s leaves the 300-frame windows exactly 10.0 s apart).
Something touched the game window at onset.

**Being out of the foreground is at most an enabling condition (inferred).**
`183` and `185` ran unfocused through boot, the menus and the load before freezing.

**It is independent of the native renderer (observed).** Four frozen runs had
`--fable2_native_render=false`.

## Ruled out

| Hypothesis | Why not |
|---|---|
| The controller is reported as disconnected when focus is lost, and the game pauses for it | The SDL pad returns a neutral state with success (`thirdparty\rexglue-sdk\src\input\sdl\sdl_input_driver.cpp:219-224`); the keyboard driver returns zero buttons with success (`src\input\keyboard_gamepad.h:227-265, 333`); a NOP pad is always on user 0, so `GetState` cannot fail there (`input_system.cpp:351-353`, `:245`). The guest sees a connected, idle pad. |
| The host or the SDK pauses something on focus loss | `OnWindowFocusChanged` is an empty hook that nothing overrides (`rex_app.cpp:563-571`). No notification is broadcast on a focus change, nothing calls the audio or GPU `Pause`, and the overlays do not pause. |
| Autoplay input arrives half-applied (a button held, a pause toggled) | Autoplay is not gated on focus and cannot leave a button held (`src\input\remote_gamepad_driver.h:67-91`, `src\input\autoplay.h:93-98`, `src\core\fable_2_app.h:437-441`). |
| A visible system dialog | Window screenshots of frozen `183` and `185` show no dialog or menu; the same capture method does show the F3 overlay in `180`. |
| The dirty-disc UI | It logs an error; none appears. |
| The XDK re-swap worker (`sub_82B9F598`, 30 ms timeout) | It does not issue draws again, and the frozen frames are drawn again. |
| Windows timer throttling of a background process | The delay is a clean 33 ms, not a multiple of 15.6 ms. |
| The native debug renderer | See above. |

Input-side fixes (feed neutral input while unfocused, report connected-but-idle)
would therefore not help: that is already what the guest sees.

## Candidate causes

None of these is confirmed, and they are not ranked.

### A. "System UI is showing" stuck on

The game's pause inputs from the OS are two notifications
(`src\core\hotfunc\os_notifications\DispatchOsNotifications_82185080.cpp:82-93, 125-137`):

- `XN_SYS_UI` (id 9) sets a "system UI showing" byte;
- `XN_SYS_INPUTDEVICESCHANGED` (id 18) re-checks the controller.

In this SDK id 18 is sent only at startup (`kernel_state.cpp:1103-1104`) and id 9
only at startup and around `XamShow*UI` calls (`xam_ui.cpp:79-104, 160-171`).

Mechanism: the game calls a `XamShow*UI` function, the SDK broadcasts id 9 with
data 1, and the matching data 0 never arrives. The game then waits for a system UI
that is not there: world paused, nothing drawn over it. That is exactly the
symptom.

- For: explains the pause state and the absence of anything on screen.
- Against: nothing in the code ties it to focus or to machine load.
- Confirms: `BroadcastNotification(id=0x9, data=1)` at onset with no later
  `data=0`; the `XamShow...` line before it names the call.
- Refutes: no such line at onset in a debug-level log of a frozen run.

### B. A timing disturbance that leaves the loop not updating

`UpdateGuestClock` samples the host tick before `try_lock`, so a stale sample
moves the stored host tick backwards and guest time jumps forward by the stale
interval (`thirdparty\rexglue-sdk\src\core\clock.cpp:66-89`). Every `mftb` goes
through it (`generated\default\fable_2_pch.h:293`), and so does the vblank worker.

Mechanism: the game runs in the background on a busy machine, a hitch and a guest
clock jump occur, and the fixed-step loop ends up in a state where the world
update's tick condition is not met again.

- For: fits the trigger conditions (background, machine in use) and the hitch
  seen at every onset.
- Against: no path was found from a forward jump of guest time to a permanent
  freeze; a fixed-step loop would normally catch up or clamp.
- Confirms: the freezes stop under the same provocation with
  `--clock_no_scaling=true` (the stateless clock path); or a dump of the two rate
  globals (`0x83319510`, `0x83319518`) and the tick counter shows them stuck in a
  frozen run.
- Refutes: it still freezes with `--clock_no_scaling=true`.

### C. A key reaching the game (manual play only)

Escape is mapped to Start and Tab to RT. Alt-Tab sends Tab. A pause opened by a
stray key would explain "behind what looked like a menu" in `fable_2_133.log`.
This cannot explain the autoplay freezes with nothing on screen, so it would be a
second, separate effect.

- Check: reproduce by alt-tabbing and note whether the pause menu is visible when
  returning; try alt-tabbing with a different key path (clicking another window
  with the mouse) and compare.

### D. The F5 Lua hook

F5 is polled without a focus check (`src\core\fable2_f5_lua.h:133`).
`fable2_f5_lua.log` has no timestamps, so it could not be matched to the onsets.

- Check: add timestamps to that log, or look for F5 presses in the user's own
  sessions; for the autoplay runs, whether the user pressed F5 in another program
  at the onset times.

### Open conflict in the evidence

In `fable_2_183.log` the sampler puts the last input event at 46.2 s, while the
paint burst is at 46.4 to 48.4 s. Either the two clocks differ by about a second,
or the burst has a source other than input.

## Experiments, cheapest first

1. **Debug-level log of a frozen run** (separates A from the rest). While someone
   types and moves the mouse in another window, as in the frozen runs:
   ```powershell
   .\tools\drive_game.ps1 -Total 120 -GameArgs "--fable2_native_render=false","--log_level=debug" -Env @{FABLE2_GUEST_WORK_LOG="1"}
   Select-String -Path out\build\win-amd64-release\logs\fable_2_<n>.log -Pattern "BroadcastNotification|XamShow|Deferred overlapped"
   ```
   Debug logging changes timing, so a run that does not freeze proves nothing;
   repeat until one freezes.
2. **The same with `--clock_no_scaling=true`** added to `-GameArgs` (tests B).
   Several runs each way are needed, because the freeze is occasional.
3. **Does it recover?** In a frozen manual session: press Start, press B, wait five
   minutes, alt-tab away and back. Any of these unfreezing it points at a pause
   state with an exit condition (A or C) rather than a stuck loop (B).
4. **Read the pause state from a frozen process**: the "system UI showing" byte and
   the controller byte the notification handler writes, the two rate doubles at
   `0x83319510` and `0x83319518`, and the loop's tick counter. The project's
   memory-dump tooling can do this once the addresses of the two bytes are taken
   from `DispatchOsNotifications_82185080.cpp`.
5. **Provoke it on purpose.** If a minimal trigger is found (a window message, a
   focus change at a given moment, a burst of paints), it can be scripted and the
   investigation stops depending on chance.

## Not looked at yet

- Audio and XMA (a stalled audio stream could stall a guest thread that waits on
  it).
- The guest code that reads the two pause bytes or writes the rate globals, and
  `sub_82352AC8`.
- `XNotifyListener` queue internals and the sign-in path at `xam_user.cpp:484`.
- SDL internals, the command processor's vblank wait, Windows scheduling of a
  background process.
- What produces the paint bursts at onset.

## Practical notes until it is fixed

- Keep the game window focused while playing or testing; once frozen, restart.
- Autoplay measurements: leave the machine alone for the run, and check the
  `[frame] guest` lines for the freeze signature before using any figure.
- Related records: `docs\native-renderer\frame-map.md` section 12 "Validation"
  (the runs and the sampler), `docs\native-renderer\user-checks.md` checks 5 and 15.
