# Fable 2 — ReXGlue Recompiled Project
First and for most this project was made to test the capabilities of local and open source AI. This project was made using Qwen3.8 27b. After about an hour it got the game running. Then after a large amount of human trial and error I was able to get enough of the functions mapped to be able to complete the game. I currently do not consider this to be fully complete as I have yet to 100% the game. The sha256 of the iso that started this project is: "685a0d3bea9718812f17bcd155907a5359a548b6d3d8342dd2a6c944f45e35ff" and its the Fable 2 GOTY for USA and Europe.

Recompilation of Fable 2 (Xbox 360, title ID 4D5307F1) using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.10.0. Guest PPC code is statically recompiled to C++ at build time by `rexglue codegen`, driven by `fable_2_manifest.toml`.

# Current and planned features

Original GOTY USA/Europe and German GOTY dumps are supported by the default
`goty-compatible` build profile. Other XEX revisions remain rejected. See
[German GOTY validation and known limits](docs/GERMAN_GOTY_SUPPORT.md).
[x] Can be used to beat the game\
[x] Guild chest fully unlocked\
[x] Uncapped framerate / increased framerate\
[ ] Higher Resolution support\
[ ] Built in Debug Menu
  - [x] Enabling custom lua to run in game

[x] Keyboard / Mouse Support\
[ ] In game Text changed to respect keyboard and mouse
  - [ ] Automatic swapping between text

[ ] Increased performance / framerate\
[x] Hero / Dog Texture bug fix (upstream renderer, verified on German GOTY)\
[ ] Vulkan support\
[ ] Linux Builds\
[ ] Custom commands to aid in debugging\
[ ] Improved Graphics rendering
[ ] Custom menu(s) / modifying menus for extra functionality (like closing the game)

## Windows launcher

The optional WPF launcher detects the original game edition and configures
output resolution, internal render scale, anisotropic filtering, FXAA, VSync,
window/fullscreen mode and a 30/60/120/144/165/240/unlimited FPS limit. It keeps its preferences
in `launcher-settings.toml` and writes `fable_2.toml`, preserving other settings
and a one-time backup. Preferences survive the runtime rewriting its config.
The game-folder selection starts empty and remembers only the user's saved
location. Game files can live separately; the launcher uses `--game_data_root`.
Both applications are named Fable 2 Recompiled and use new project-owned icons.

Build with the .NET 8 SDK using `build.cmd launcher`, or
`build.cmd launcher-self-contained` to bundle the desktop runtime. Place
`out\tests\launcher-build\Fable2Launcher.exe` beside `fable_2.exe` and its generated
`fable2_build.json`. See [launcher details](launcher/README.md).
For the source-built audio fallback, working FPS limit and matched renderer,
use `build.cmd -release fable_2`; see [build instructions, tests and known
limits](docs/RUNTIME_FIXES.md). Remaster assets and save editing are not included.



# Notes for running the game
- **How to extract:** rip the disc to an ISO, then open it with **[XboxImageExtractor](https://github.com/dromex1/XboxImageExtractor)** — a GUI tool for Xbox 360 game images. It lists the image's filesystem; select `default.xex`, `data`, and `$SystemUpdate` and extract them into the project root (`data` and `$SystemUpdate` extract as folders). You can also grab `nxeart` and anything else the tool lists.
- **You must supply the game content yourself** (it is not in the repo): rip the Fable 2 GOTY (USA/EU) disc (the one with SHA-256 above) and put `default.xex`, `data/`, `nxeart/`, and `$SystemUpdate/` in the project root. The build does not copy this into the build directories   
- **Saves live in `<build dir>\saves\`** — back that folder up to keep your progress, and copy it between build trees (Debug/Release) or machines to carry a save over.
- The `--game_data_root <path>` override still points the content root at a different tree (e.g. to run from a shared content copy without staging); saves/cache still land next to the exe.


## Running

To run the game you must:
1) Extract a downloaded copy release of the game 
2) Extract the "data" and "default.xex" from your copy of the game, See "How to extract" for more info on that
3) Run the fable.exe program and if no errors pop up then the game should launch and you are good to go
  - If an error pops up about the hash being wrong try to extract a different version of the game and then try again.


Optional command-line overrides (all normal `--cvar value` args):

| Argument | Effect |
|---|---|
| `--game_data_root <path>` | Use a different content directory (beats the exe-dir-only search) |
| `--update_data_root <path>` | Optional update content root |
| `--window_width` / `--window_height` / `--fullscreen` | Presentation options |
| `--keyboard_gamepad_map <map>` | Host keyboard -> guest gamepad button map (see below) |

### Experimental native renderer

Foundation for a native D3D12 renderer (see
`docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md`).
Start with `--fable2_native_render=true` (or `[native] enabled = true` in `fable2_config.toml`; restart required). Choose a debug view with `--fable2_native_view=overlay|split|native|pattern` (default `off`) or cycle off, overlay, split, native, pattern with F6. Clay geometry is a debug view: the emulated frame keeps rendering underneath (`overlay` blends the native image at 50%, `split` shows it on the right half, `native` shows it full screen, `pattern` replaces the frame with a test pattern). Any native failure falls back to the emulated image (F6 retries). D3D12 only. The clay view draws terrain, static meshes, trees, instanced plants and characters posed by bone skinning on the CPU. The ground has the game's own terrain material without its light: near the eye every 8 x 8 square is the sum of its material layers (grass, dirt, moss, rock, path), each the layer's detail texture weighted by its weight map and blended as the game blends it; farther away it is the game's ready-mixed far texture, with the game's cross-fade between the two from 48 to 64 units. On steep ground the layers that have one show their side texture (rock faces), projected from the side and mixed in by the slope as the game mixes it (seen in the game so far only in runs made with the user at the machine, on a few small rock faces around one bridge: `docs/native-renderer/frame-map.md` section 13, "Validation"). It has no baked light, no shadow and no fog, so the native picture is darker and flatter than the emulated one. Trees and plants stand still (wind is not animated). Grass, ferns, leaves, hair and other cut-out cards have the game's silhouettes: the clay pass applies the game's alpha test and alpha-to-mask with the alpha of the shaders entered in the table (a draw of a shader without an entry is drawn as its whole rectangle and counted `alpha-unknown`), and a tree or branch that the game is fading in or out shows as a grid of dots, more solid than the game's fade. Draws the game blends are left out while their colour is not known or their blend cannot be expressed (`blend-unknown`: the dog's fur shells, so the dog shows its textured body without fur, and, by inference from one shader's constants, a character while the game fades it in), blended draws are not sorted, water is flat clay, and billboards and particles are not drawn; `docs/native-renderer/frame-map.md` section 13, "Validation", lists what was measured, the known limitations and what is still open (section 12 for placement and skinning).

| Setting | Effect |
|---|---|
| `--fable2_native_clay_color=clay\|draw\|shader` | Clay color: one clay color (default), a color per draw, or a color per vertex shader (shows which shader drew what) |
| `--fable2_native_geometry_budget_mb=<MB>` | GPU memory for decoded native geometry (default 256, minimum 16); least recently used buffers are evicted above it |
| `--fable2_native_textures=true\|false` | Albedo textures on clay draws whose material was resolved, and the terrain material on the ground (default true); other draws stay flat clay, with reasons in F3 |
| `--fable2_native_texture_upload_mb=<MB>` | Guest texture bytes decoded and uploaded per frame (default 8, minimum 1); draws whose texture is over it stay clay that frame (`texture-pending`) |
| `--fable2_native_texture_budget_mb=<MB>` | GPU memory for native albedo textures (default 512, minimum 16); least recently used textures are evicted above it |

F3 (in overlay, split and native views) adds five native lines: `Native: captured C, drawn D (deformed N), skipped S (top: ...)` (main-scene guest draws, the ones drawn as clay, the drawn ones whose shader moves the position in a way the clay view leaves out, such as wind sway, and the top three skip reasons: `no-transform`, `unknown-shader`, `unknown-pos-format`, `unsupported-prim`, `bad-memory`, `bad-index`, `no-stream`, `instance-unsupported` and `skin-unsupported` for an instancing or skinning shader whose vertex layout or streams are not what its table entry describes, `depth-only` for a draw the game executes only in its depth pre-pass, `blend-unknown` for a draw the clay pass leaves out because the game blends it and its colour is not known (no albedo, an unknown shader, a texture that is not ready) or because it uses blend factors the renderer cannot express, `terrain-square` for a ground layer that is left out because its square is drawn as flat clay (below), and `render-other`), `Geometry: U uploads, H hits, M MB resident, instanced I, skinned K | hash ... ms, decode ... ms, record ... ms` (geometry cache uploads, hits and resident MB, the draws whose positions were built by the instancing and the bone-skinning paths, and the GPU-thread hash/decode/record times), `State: P pipelines, alpha test A, blended B, invalid I` (each draw uses the game's own blend, depth, cull and colour-mask state: P is the number of pipelines created in the clay pass's cache, one per distinct state in use and at most 64, shown as `P pipelines (overflow N)` when N draws found no pipeline there, because the cache was full or the pipeline could not be created, and used the default pipeline (a ground draw that finds no pipeline for its material is counted there too, once: it loses the material and its square falls back, reason `terrain-inputs`); A the drawn draws whose alpha was tested as the game tests it: the alpha test, then alpha-to-mask under the sample count of the surface the game draws to: the native view has one sample per pixel and keeps a pixel when at least half of the game's samples would be covered, which in the main scene, drawn with two samples per pixel, is an alpha of at least 0.5 minus an eighth of the dither offset of that pixel's position in a 2x2 block (0.5 minus a sixteenth under four samples, 1 minus a quarter without multisampling), so cut-out foliage is closed and a fading tree shows as a stipple of one, two or three pixels in four, more solid than the game's finer fade; B those drawn with a blend; I the draws whose state could not be used as captured: a state that could not be read is replaced as a whole by the default, opaque with depth less and no culling, while for a depth function the renderer cannot express only the depth state falls back to the default and blend, colour mask and cull stay the game's; a ground draw is the exception: with a state that cannot be used it is not drawn with the default but as flat clay with its square, tested less-equal), `Capture: X ms guest time per frame` (time the capture layer spends on the game thread), and `Textures: textured T of D drawn (S%, P% non-terrain), resident N (M MB), uploads U (X MB), decode Y ms | top untextured: ...` (drawn clay draws that sample their albedo or, for the ground, are drawn with the terrain material, as a share of all drawn draws and of the drawn non-terrain draws, the texture cache, and the top three reasons the others stay flat: `terrain-inputs`, a terrain draw whose material inputs could not be read, whose pixel shader is not a terrain shader in the table, or whose render state cannot be drawn as captured (unread, or no pipeline for it), `terrain-square`, a terrain draw that could have its material while another draw of the same ground square could not (a texture not ready, say): the layers of a square only add up as a whole, so such a square is one flat clay draw and its other layers are left out, never half textured (only the square's one flat draw keeps the reason that caused it; the log's `Terrain:` line names the cause of every such draw), `ps-unknown`, `no-albedo`, `uv-unsupported` (also a terrain draw whose far colour tables or, for a layer that shows a rock face on steep ground, whose normal map could not be read), `format-unsupported`, `texture-pending`, `texture-dynamic`, `texture-bad`, and the two state reasons `alpha-unknown`, a drawn draw whose alpha test or blend has no alpha to apply yet, so it is drawn without the test, as its whole rectangle, or with alpha 1, and `blend-unknown`, the draws left out as above; `Textures: off` with `fable2_native_textures=false`, `Textures: off (latched)` after a GPU texture failure turned the texture path off). The game log gets a `[native] capture:` line every 300 frames in every view (captured/drawable counts, skip reasons, `records on|off` and the capture time with its median, p90 and max; while records are on, also `drawable by vs`, `no-transform by vs`, `unsupported by hook` and `untextured by ps` breakdowns, where a vertex shader is tagged `(terrain)`, `(instanced)`, `(skin)` or `(deformed)`), `[native] clay:` lines while a composite view is shown (the first starts `view <name>`, the view at the end of the 300-frame window, followed by ` (changed)` when the view was changed during that window, so a window is never silently attributed to one view; only composite views write clay lines, so a switch to `off` or `pattern` shows on the next composite view's line, and setting the view it is already in is no change: the game logs `[native] autoplay: view <name> (unchanged)` for it and the mark does not appear; with the textured count, the texture line, a `Terrain: material M of T drawn, flat F, left out S, texture sets N` line (the terrain draws with their material and as flat clay, the layers left out with their square, and the distinct sets of colour, weight and side texture bound, the side texture being the rock face a ground layer shows on steep ground; followed by `| without material: <reason> N, ...` when terrain draws could not have their material, each counted under its own reason, which is where the cause of `terrain-square` draws shows; a ground draw whose geometry could not be built makes its square fall back too and is listed there by what was missing, `no-positions` or `no-indices`; it is also in the first line's `skipped_bad_index` or `other`), the state line with the `alpha-unknown` and `blend-unknown` counts and their breakdown `by ps`; `[native] textures: burst` lines show texture uploads spreading over frames after an area load), and a `[frame] guest` line (fps and median frame/work/swap/wait times; also with `FABLE2_GUEST_WORK_LOG=1` when the renderer is off). Draw records are only built while a composite view is shown, so the view `off` costs well under a millisecond per frame.

Light is being added (`docs/native-renderer/frame-map.md` section 14, `docs/native-renderer/lighting-formulas.md`). So far only the capture has it and nothing is drawn with it: while a composite view is shown each frame carries the game's sun and ambient constants (its light block) and, per draw whose pair of shaders has a checked light family, what that family reads (normal and colour streams, a few constants). The capture's log line every 300 frames says how much of a frame that covers: `[native] capture: frame N light: lit L by family {terrain-layer: a, terrain-far: b, object: c, tree-trunk: d, plant: e, plant-lit: f} (multi M), unknown {light-unknown: u, normal-unsupported: v} by ps {...} | block valid, sun (...) colour (...) (tries T) | light-block in F of W frames`. `lit`: draws that have their light inputs; `multi`: those of them whose shader has two or four lights; `light-unknown`: no family for the pair of shaders, or an input that could not be read; `normal-unsupported`: the normal is not where the shader's table entry says, or in a format the renderer does not decode; `light-block`: frames in which the game's light constants were not read (the first after the view comes on, a scene without a sun), to be drawn without light. The renderer's geometry cache also holds what the light needs per vertex, still unused by any shader: vertex normals (of plain, bone-skinned and instanced meshes and of every ground draw) and the vertex colours of the props that have a colour stream; the `Geometry:` figures (uploads, hits, resident MB) include them. With textures on, a `[native] clay: Light inputs: ready R of L lit records | world rotation: rotation a, mirrored b, skewed c, degenerate d` line every 300 frames says for how many of the textured draws with light inputs these buffers could be built (`L` lit, `R` ready) and what the world rotation of the props and trunks among them is (anything but `rotation` is listed by vertex shader); draws without their buffers follow as `light-unknown N by vs {...}` (a stream that could not be read or is too short) and `normal-unsupported N by vs {...}` (a normal the renderer does not decode), and keep their material.

Developer environment variables (set before launching `fable_2.exe`):

| Variable | Effect |
|---|---|
| `FABLE2_NATIVE_DISCOVERY=<frames>` | Write `logs\native_discovery_<timestamp>.jsonl` (raw hook rows, decoded draw rows, frame rows) for that many guest frames; works with the renderer off |
| `FABLE2_NATIVE_DISCOVERY_DELAY=<seconds>` | Start discovery that many seconds after the first frame (e.g. 50 to reach gameplay) |
| `FABLE2_NATIVE_DISCOVERY_EVERY=<n>` | Sample one draw in n (default 64) |
| `FABLE2_AUTOPLAY=<s:Button,...>` | Press gamepad buttons from inside the game at the given seconds since start, e.g. `23:A,27:Down,30:A,35:A` (buttons: A B X Y Up Down Left Right Start Back LB RB L3 R3). A stick entry `s:LS=x/y/ms` or `s:RS=x/y/ms` holds that stick at x/y percent (-100..100, positive y forward, positive x right) for ms (1..60000), e.g. `55:LS=0/100/3000,60:RS=60/0/1500`; a malformed entry rejects the whole plan (the game then logs `[autoplay] invalid FABLE2_AUTOPLAY, bad token ...` and runs without input; an accepted plan logs `[autoplay] armed: N steps`). An entry at second 0 is dispatched at the first poll; the native renderer is installed about 1.5 s after the autoplay thread starts, so a `VIEW=` entry that early is logged and ignored (`[native] SetView(...): native renderer not installed; ignored`) and a `DISC=` entry that early is not to be relied on. Two more entries run inside the game: `s:VIEW=off\|split\|overlay\|native\|pattern` (names case-insensitive) sets the native view at that time, as an F6 press does (needs `--fable2_native_render=true`; logged and ignored otherwise), and `s:DISC=frames/every` (frames 1..100000, every 1..64, as `FABLE2_NATIVE_DISCOVERY` and `FABLE2_NATIVE_DISCOVERY_EVERY`) starts a discovery capture into a new `native_discovery_<stamp>.jsonl` at that time; it is refused with a log line while another capture is running or still waiting for its `FABLE2_NATIVE_DISCOVERY_DELAY`. Example: `23:A,27:Down,30:A,35:A,52:VIEW=off,56:VIEW=native,60:VIEW=split,64:VIEW=overlay,66:DISC=60/4`. These are the game's button names. `tools\drive_game.ps1 -Plan` takes key names instead (`23:E,27:S,30:E,35:E,...`) and translates them (E and SPACE to A, S to Down, W to Up, A to Left, D to Right, ESC to B, ENTER to Start); given to the game as they are, key names such as E or S reject the whole plan, and the key name A would be read as the button A. |
| `FABLE2_AUTOPLAY_HOLD_MS=<ms>` | How long each autoplay press is held (default 100) |
| `FABLE2_GUEST_WORK_LOG=1` | `[frame] guest` log line with the renderer off (A/B baseline) |

`tools\drive_game.ps1` launches the game with autoplay into gameplay (loads the save; world fully up at about 50 s), optionally takes window screenshots, and closes it, without touching the keyboard or the foreground window. From the repo root, e.g. `.\tools\drive_game.ps1 -Total 120 -Shots "95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"` (screenshots in `out\build\win-amd64-release\logs\shots`), or `-Env @{FABLE2_NATIVE_DISCOVERY="300"; FABLE2_NATIVE_DISCOVERY_DELAY="50"}` for a gameplay discovery capture. `-BootSave HeroNNN` makes the game load that save itself, without menu presses and without the logo videos ([docs/boot-save.md](docs/boot-save.md)): the default key plan is dropped, a `-Plan` then holds only what is to happen in the world (the driver prints `plan for the game: ...`), the world draws from about 23 s and captures belong at 33 s or later, e.g. `.\tools\drive_game.ps1 -BootSave Hero000 -Total 60 -Shots "35,50"`. `-Plan` is a list of `seconds:key` entries (keys E, SPACE, S, W, A, D, ESC, ENTER, translated to pad buttons; the stick, `VIEW=` and `DISC=` entries of `FABLE2_AUTOPLAY` above as they are); the default is `23:E,27:S,30:E,35:E`. The driver checks the plan before anything is launched (`tools\autoplay_plan.ps1`), because the game rejects the whole plan on one bad entry and then runs without input: a time that is not plain decimal seconds, anything after a second `:`, a key without a pad button, or a stick, view or capture entry out of form or range is refused (`REFUSED: bad plan: ... Nothing launched.`, exit 2), and so is a `-Shots` time that is not a plain number of seconds. After a run with a plan, the game's own log of that run (`logs\fable_2_<n>.log`) must hold the game's `[autoplay] armed` line; otherwise the run ends `RUN ABORTED: the game did not accept the plan`, never clean. `-PlanShift N` (whole seconds, 0 to 30) adds N to every plan time, every `-Shots` time and `-Total` (screenshots are named by the moved time): it is the remedy for a plan with menu presses when the start is slow; a `-BootSave` start has no menu steps to miss, and there it moves the plan, the shots and the total only. The menu steps of a plan are at fixed times; on a busy machine the title screen comes a few seconds later, the first press is lost and the run stops in the main menu (`docs/native-renderer/frame-map.md` section 13, "Validation": the plans run 6 s later got through). The driver does not detect the menu. What a start that was too slow looks like: the game stands in the main menu with "New Game" selected, or at the dialog "You have already reached the maximum number of save games ...". Do not press A there (A confirms what is selected): press B, close the game and run again, or run with `-PlanShift 6`.

### Rule for automated agents: how the game may be started

**Automated agents start the game only through `tools\drive_game.ps1`, without `-UserPresent`.** Every other launcher is for a person at the keyboard and must not be used by an agent: `tools\fable2.cmd`, `tools\fable2-uncapped.cmd`, `tools\fable2-functrace.cmd`, `tools\StartWithoutAudio.cmd`, `tools\crash_capture.cmd`, the `launcher\` app, and running `fable_2.exe` directly. (Each `.cmd` carries a comment saying so; they do not enforce it.)

**The game runs only while the user is away** (user decision, 2026-10-07), and `drive_game.ps1` enforces it itself. Start it from a PowerShell session or with `pwsh -File`; both work. Outside test mode `-ExeDir` must be this repository's `out\build\win-amd64-release` (the allow-list constant `$AllowedExeDirs` at the top of the script; any other folder is refused with exit 2), and the `started pid <n>: <full path of the executable> ...` line, printed and logged, shows what ran. A strict-mode caller (`Set-StrictMode`) is not supported: the library fails closed and the driver refuses.

- *Gate before launch.* It runs `tools\idle_check.ps1`'s gate, which needs all of these, each over 300 s of observation (fixed: no parameter, environment variable or option lowers it; unknown and positional arguments are refused with exit 2): no keyboard or mouse input (`GetLastInputInfo`); no XInput pad activity, connection or unplugging (a background thread samples the pads every 10 ms and latches the last activity, so short taps are caught; once a pad has been seen the gate needs the full 300 s without activity or change, and every activity, connect or disconnect restarts that observation; activity is any button, a trigger above 30, or a stick more than 8000 from centre); no sound on the default playback device for 300 s (Core Audio peak meter above 0.005; sound or a failing probe restarts that observation); no other program in fullscreen or presentation mode (`SHQueryUserNotificationState` 2, 3, 4 or 7; 1 is "away", 5 and 6 are neutral); and no `fable_2.exe` already running (this check is gate-only). **Every gate therefore takes at least 300 s: `-IdleWaitSeconds` below 300 always refuses** (default 1800; the refusal message says so). Anything unknown (probe failure, a stalled sampler, no audio device, session 0, an unexpected API value, an idle time that is negative, not finite or longer than the uptime) counts as the user being present. While it waits the gate prints once a minute what it has observed so far, for example `idle gate: observing: 61 of 300 s without sound; waited 61 of at most 1800 s`: such a line is the gate still counting, not a sign that someone was seen (a state that holds at that moment is named as one: `another program is fullscreen or presenting now`). If the gate does not pass or throws, nothing is launched: `REFUSED: ...` and exit code 10. The gate loop sleeps 250 ms plus the probe time (a few ms) between decisions; two polls more than 5 s apart (the machine slept, the process stalled) restart every observation, so a clock jump is never counted as absence.
- *The game cannot outlive the driver.* The game is created suspended, assigned to a Win32 job object (kill on close) whose handle the driver holds, and only then resumed, so it never runs outside the job and the OS kills it and its children if the driver is terminated for any reason; if the job cannot be made or assigned, the game is killed before it ran (exit 12). Stopping the game also terminates the job, the `finally` block kills it through its process handle, and `RUN CLEAN` is printed only after the game is confirmed closed. A dead-man switch (armed before the game is resumed) terminates the job if the driver's main loop has not polled the watch for more than 5 s (for example a hung screenshot call); the main thread also aborts when the gap between two polls, or one poll, exceeds 5 s, and the verdict checks the switch after the last poll. Such a run ends `RUN ABORTED: dead-man switch ...` (exit 12), as does a run that lasted more than 5 s longer than `-Total` (`RUN ABORTED: run overran (<n> s for a <Total> s run)`): neither is ever `RUN CLEAN`. The driver starts the game with `SDL_AUDIO_DRIVER=fable-test-unavailable-backend` (the `tools\StartWithoutAudio.cmd` mechanism) so the game's own sound does not trip the audio signal; an `-Env` value for `SDL_AUDIO_DRIVER` wins.
- *Watch during the run.* From launch to the end it polls every signal about every 100 ms plus the probe time (the game's in-process autoplay and the PrintWindow screenshots are not OS input and do not trip it; a fullscreen state while the game window is in front is ignored; a pad or audio blip between two polls is latched and still counts). The signals are `input`, `controller`, `audio`, `fullscreen`, `unknown` (a failed probe), `watch-error` (an exception in the watch itself), `watch-gap` (the clock jumped by more than 5 s between two polls) and `forced-present` (the test hook). On a signal it kills the game at once, prints `RUN DISTURBED: <signal> at <seconds> s`, moves the run's screenshots into `<ShotDir>\disturbed_<timestamp>\`, writes that line, a "do not use this run" warning and the moved files to the marker file `RUN_DISTURBED.txt` (in `-ShotDir` when `-Shots` is given, else in `<ExeDir>\logs`) and exits 11.
- *Contract for whoever reads the output.* A usable run ends with the last line `RUN CLEAN: no user input for <seconds> s` (seconds from launch to the moment the game was closed) and exit 0; there is no `RUN_DISTURBED.txt` from that run (an older one is renamed `RUN_DISTURBED.<timestamp>.previous.txt` before launch). **Require both the exit code and that last line.** The verdict (`RUN CLEAN`, `RUN UNCHECKED`, `RUN DISTURBED`, `RUN ABORTED`, `GAME EXITED EARLY`) or the refusal (`REFUSED: ...`) is always the last line of the output and always one line; in a PowerShell session a script error before the launch leaves `$LASTEXITCODE` as an earlier command set it, so the exit code alone can be a stale 0. Exit codes: 0 clean; 1 script error before launch (ambiguous parameter abbreviation, a missing library script); 2 refused before the gate, nothing ran (`-KeyboardInput` without `-UserPresent`, unknown or positional arguments, an `-ExeDir` that is not allowed, a bad plan, a bad `-Shots` time or `-PlanShift`, a `-BootSave` name that is not `Hero` and three digits, `-BootSave` with `-KeyboardInput`, `-BootSave` without exactly one such save folder holding a `mainsave.bin`); 10 gate refused, nothing ran; 11 `RUN DISTURBED: ...`, discard the run; 12 `RUN ABORTED: <message>`, an error closed the game, or the game did not accept the plan (not clean; its screenshots are moved into `aborted_<timestamp>\`), and also, with nothing launched, when `-BootSave` could not put the pristine save in place or the game's log folder could not be listed for the plan check; 13 `GAME EXITED EARLY at <seconds> s`, the game exited before `-Total` or before the last `-Shots` time when that is later (never clean; screenshots moved into `early_exit_<timestamp>\`). The run log `<ExeDir>\logs\drive_game_runs.log` gets a `started pid <n>` line at launch (so a killed driver leaves a trace) and a final line (time, exit code, gate result and signals); nothing is written into an `-ExeDir` the driver refused.
- *`-UserPresent`* (spelled in full; abbreviations are rejected) skips the gate and the watch and prints a prominent line saying so; the log line says `USER PRESENT OVERRIDE` and the run ends with `RUN UNCHECKED`, never `RUN CLEAN`. It is for the person at the keyboard running the script themselves. **Agents must never pass it.** `-KeyboardInput` (real key presses) implies the user is driving and requires `-UserPresent`.
- *Self-test runs are labelled.* `-TestHarness`, `-TestExe` and `-TestExeArgs` exist only for `tests\test_idle_gate.py` (a scripted probe and a stand-in process). A harness run prints the banner `*** TEST MODE ...`, every verdict line starts with `TEST HARNESS ` (`TEST HARNESS RUN CLEAN: ...`, never a bare `RUN CLEAN`), and so does every run-log line; it needs an explicit `-ExeDir` without a `fable_2.exe` and refuses `-GameArgs`, `-Env`, `-UserPresent`, `-KeyboardInput` and `-BootSave`. It is also refused when the stand-in is named like the game, is the built `fable_2.exe` (same path or bytes) or sits in a game folder. That guards against accidents; the rule above is policy.
- *Running the tests.* Agents may run `python -m unittest discover -s tests -p "test_*.py"`: no test can reach a launch of the game. The test module sets `FABLE2_DRIVER_NO_REAL_GATE=1`, which makes the driver refuse (exit 2) to evaluate the real gate or to launch unless the forced-present hook, the labelled harness, or `-UserPresent` in a folder without a `fable_2.exe` is in play. Two tests pass `-UserPresent` with an empty `-ExeDir` to prove the skip and the aborted path (nothing exists to launch); every other driver test that gets past the gate uses the stand-in process.
- *Limits.* Only XInput pads are seen (not DirectInput or other controllers). A user who is silent and motionless, with no pad and no sound, reads as away after five minutes. Two short stretches are not polled: from the gate's pass to the watch's first poll (well under a second) and from the watch's last poll to the kill (milliseconds); input that falls into one of them is not seen. The plan check knows the game's log by its name (`fable_2_<n>.log`, a file that did not exist before the launch) and reads its first 5000 lines: a run so short that the game's log has not reached the disk when the game is closed ends `RUN ABORTED` although the plan was taken.
- `tools\idle_check.ps1` is also a strict CLI: with no gating parameter it prints usage and exits 2; `-MinIdleSeconds N` (N at least 60 here; the driver always uses 300; optional `-MaxWaitSeconds M`) exits 0 only when the user has been away for N s, 1 when present, 2 for usage errors or unknown state; `-Report` (alone) prints the signals and exits 0 (for humans, not a permission to run); `-WatchSeconds N` watches and exits 1 at the first signal. The environment variable `FABLE2_IDLE_GATE_FORCE_PRESENT=1` is a test hook that can only add a presence signal.

## User config (fable2_config.toml)

The recomp has its own human-readable user config, `fable2_config.toml`, next
to the exe — separate from `fable_2.toml`, which is the ReXGlue SDK's cvar
config. It is staged by the build (once; user edits survive rebuilds) and the
game recreates it with defaults on launch if it is ever missing. Loaded in
`Fable2App::OnPostInitLogging()`; code in `src/core/fable2_config.{h,cpp}`.

- Missing keys -> built-in defaults; wrong types / unknown sections -> logged
  warnings, defaults used; a syntax error -> dialog + defaults (never blocks
  launch).
- To add a setting: add a member to `fable2::config::Values` (with default),
  read it in `Load()`, and document the key in `config/fable2_config.toml`
  **and** the embedded template in `src/core/fable2_config.cpp` (keep both in
  sync).
- Settings that back an SDK cvar (currently the `[input]` section:
  `keyboard_gamepad_map`, `mouse_look`, `mouse_look_scale`) are seeded into
  the cvar at startup — but only when no higher-priority
  source set it: `fable_2.toml` (cvar config), `REX_*` env vars, and the
  command line (e.g. `--keyboard_gamepad_map`) still win, and the F3 console
  can still change the value live.
- The `[patches]` section holds runtime toggles for the recomp-level
  (mid-asm hook) patches, consulted by the hook bodies on every call
  (`src/core/fable2_hooks.cpp`) — flip one and relaunch to A/B a patch with no
  rebuild. Currently: `fps_60` (60 FPS hook; `true` = main loop ~60/s,
  `false` = original 30/s). Guest-image data patches are a different file:
  `fable2_patches.toml` (see above).



## Keyboard controls

The game normally reads a gamepad via the Xbox 360 `XamInputGetState` API. A
synthetic "keyboard gamepad" input driver (`src/input/keyboard_gamepad.h`) is added
on top of the default SDL driver, so host keyboard keys can drive the guest on
top of (OR-merged with) whatever a real gamepad reports. The physical pad
keeps working; the keyboard just adds buttons. It is wired up in
`Fable2App::OnPreSetup` via `config.input_factory`.

The mapping is the `keyboard_gamepad_map` cvar, format `Key:Button,Key:Button,...`. Its default is no longer hardcoded in the binary: it comes from `[input] keyboard_gamepad_map` in `fable2_config.toml` (see User config above), which you can edit to remap permanently. The command line and the F3 console still override it per-launch / live.

- **Key** — a host key name understood by `rex::ui::ParseVirtualKey`
  (`E`, `Space`, `LeftShift`, `F1`, ...).
- **Button** — a guest gamepad input: `A`, `B`, `X`, `Y`, `LB`/`RB` (shoulders),
  `LT`/`RT` (triggers), `Up`/`Down`/`Left`/`Right` (dpad), `Pause` (Start),
  `Select` (Back), `L3`/`R3` (thumb clicks), and `StickUp`/`StickDown`/
  `StickLeft`/`StickRight` (left thumbstick, full deflection while held).

The default layout is:

| Key(s) | Guest input |
|---|---|
| `E` / `2` / `1` / `3` | `A` / `B` / `X` / `Y` |
| `W` `A` `S` `D` | Left stick (up / left / down / right) |
| `Escape` | Pause (Start) |
| `M` | Select (Back) |
| `Q` / `Tab` | Left / Right trigger |
| `F1` `F2` `F3` `F4` | Dpad up / down / left / right |

Remap at launch without recompiling, e.g.

```
fable_2.exe --keyboard_gamepad_map "E:A,B:B,Space:L3,Enter:Start"
```

### Mouse look (right stick)

Mouse movement is mapped to the guest **right stick** for camera control. The
movement since the previous poll is converted into stick deflection, so you
**sweep the mouse to look and stop to stop**. Two cvars control it:

| Argument | Effect |
|---|---|
| `--mouse_look <bool>` | Enable/disable mouse look (default `true`) |
| `--mouse_look_scale <n>` | Sensitivity: right-stick units per pixel of mouse movement (default `256`; larger = more sensitive) |

The defaults come from `[input] mouse_look` / `[input] mouse_look_scale` in
`fable2_config.toml` (edit there to change them permanently), the command
line overrides per launch, and both cvars are hot-reloadable from the in-game
console, so you can dial in the sensitivity live. Example: `fable_2.exe
--mouse_look_scale 512` for a more sensitive camera.

All cvars above are hot-reloadable, so they can also be changed from the in-game console.

## F5 — run an external Lua script

Pressing **F5** (host keyboard) runs an external Lua file in the in-game Lua
state, exactly the way the game's own `RunScript(path)` global does — but
triggered from the host. This lets you drop a plain `.lua` file on disk and run
it against the live game (no recompile of the scripts needed).

- **Default file:** `data/scripts/recomp/F5.lua` (the build stages
  `src/lua/*.lua` into `data/scripts/recomp/` next to the exe). The shipped
  `F5.lua` snapshots the hero's position (`QuestManager.HeroEntity:GetPosition()`)
  and shows `X / Y / Z` in a message box.
- **Path:** set by the `f5_lua_path` cvar (default `scripts/recomp/F5.lua`,
  resolved relative to the VFS root `data/`). Override per-launch with
  `fable_2.exe --f5_lua_path "scripts/other/MyScript.lua"`.
- **How it works:** `src/core/fable2_f5_lua.h` captures the
  `CScriptManager::RunScript` callable the first time the game loads a `.lua`
  script (via a probe on the LuaPlus bound-method dispatcher), then replays that
  call with your path when F5 is pressed. The file is loaded fresh on each press,
  so you can edit it live (the VFS re-reads it).
- **The script runs in the game's global Lua environment**, so it has the full
  game API (`QuestManager`, `Debug`, `GUI`, `Creature`, `Player`, ...). Plain
  text is fine — `RunScript`/`loadfile` compile it for you.

Implementation: F5 edge-detection in `src/input/keyboard_gamepad.h`, a per-frame
replay from the `MainRenderLoop` hook in `src/diagnostics/fps_meter.h`, and the
string-build + `RunScript` call in `src/core/fable2_f5_lua.h`.\
<br>
<br>
<br>
<br>

---


# Notes for dev who want to work on the build:

## Guest-image patches (fable2_patches.toml)

Data patches for the loaded `default.xex` guest image (Xenia game-patches
format), applied before the guest module launches: `Fable2App::OnPostLoadXexImage()`
→ `fable2::patches::Load()` + `ApplyAll()` (code in `src/core/fable2_patches.{h,cpp}`).
Same lifecycle as the user config: staged by the build, recreated with the
built-in defaults if missing, and a broken file falls back to the built-ins
(dialog + log) so it never blocks launch. Each `[[patch]]` has an `enabled`
toggle (default true) — flip it in the file and relaunch to A/B a patch with
no rebuild. **Scope: data patches only** — code-region ops are inert in this
recomp (guest `.text` is never executed); code patches are mid-asm hooks
instead. Full details, the current patch list, and how code patches work:
`docs/patches.md`.

## Guest function-call tracing (fable2_func_trace.log)

To figure out what each recompiled function does, every guest function entry
can be logged by name. Codegen emits `REX_FUNC_PROLOGUE()` at the top of
every function in `generated/default/fable_2_recomp.*.cpp`; the build hooks
that one macro (via `src/core/fable2_func_trace.h`, appended to the recompiled
PCH after the generated pch — no generated files are modified) so each entry
logs its name to `fable2_func_trace.log` next to the exe. Consecutive calls
of the same function are run-length encoded (per thread) to keep the file
small:

```
GetNewGameLoadingGlobal
sub_82189708 x 4821
LoadingScreen_Virtual43
sub_82CC1BC0 x 3
...
```

(`x N` means that function was called N times in a row; a bare name is a run
of one.)

**Session summary:** call counts are accumulated separately and written to
`fable2_func_summary.log` (same folder), one line per function, sorted by
total count:

```
18422331 x MainRenderLoop_82B9CD68
9711204 x __restgprlr_28
54 x Story_FirstChildCombat
```

It's refreshed every 5 s while tracing (and once more on clean exit), so you
can watch it in another window to see at a glance what's being called; the
last write is at most ~5 s stale even if the game exits via ExitProcess.
Counts respect the same on/off + filter as the text log.

Off by default (one atomic load per call when off). Enable with:

```
set FABLE2_FUNC_TRACE=1            rem every guest function entry
set FABLE2_FUNC_TRACE_FILTER=LoadingScreen   rem optional: only names containing this
```

Either output can be turned off independently (default: both on):
`FABLE2_FUNC_TRACE_LOG=0` skips `fable2_func_trace.log` (summary only),
`FABLE2_FUNC_TRACE_SUMMARY=0` skips `fable2_func_summary.log` (trace only).

or at runtime from a named-function override (see `src/diagnostics/fps_probe.h` for the
override pattern): `Fable2FuncTraceSetEnabled(true)` /
`Fable2FuncTraceSetFilter("LoadingScreen")` /
`Fable2FuncTraceSetSubsOnly(true)` /
`Fable2FuncTraceSetLogEnabled(false)` /
`Fable2FuncTraceSetSummaryEnabled(false)` / `Fable2FuncTraceFlush()`
(declared `extern "C"` in `src/core/fable2_func_trace.h`).

**Naming mode** (`FABLE2_FUNC_TRACE_SUBS_ONLY=1`): log only the unnamed
guest functions - names matching `sub_` + hex digits - dropping named
functions, the `__savegprlr_*`/`__restgprlr_*`/`__savevmx_*` register
helpers, and `xstart`. Both the text log and the summary honor it, so the
summary becomes a ranked list of the unnamed hot functions to name next.
Composes with the substring filter (`FABLE2_FUNC_TRACE_FILTER=82B9` narrows
to an address range).

**Launcher:** `fable2-functrace.cmd` (staged next to the exe) does the env
var dance for you and renames the previous session's log to
`fable2_func_trace_prev.log` first:

Arguments are keywords in any order (`subs` enables naming mode,
`trace`/`summary` pick which output file(s) are written, the backend word
picks the GPU path, anything else is the substring filter):

```
fable2-functrace.cmd                  D3D12, trace every call
fable2-functrace.cmd vulkan           Vulkan, trace every call
fable2-functrace.cmd subs             naming mode: only sub_<hex> functions
fable2-functrace.cmd subs LoadingScreen   naming mode + substring filter
fable2-functrace.cmd d3d12 82B9 subs  D3D12, address-range naming mode
fable2-functrace.cmd summary          summary file only, no trace log
fable2-functrace.cmd trace            trace log only, no summary file
```

With no `trace`/`summary` keyword both files are written (the default).
`summary` is the cheap option for long sessions - it avoids the multi-GB
sequential log while still accumulating the call counts.

It writes its own lightweight log (same pattern as `fps_probe.log`) rather
than the SDK spdlog logger, which would be far too slow at Fable 2's call
rate. To remove the feature: delete the `target_precompile_headers` block in
CMakeLists.txt + `src/core/fable2_func_trace.h` and rebuild.



## Remote control (automated input channel)

`fable_2.exe` runs a localhost TCP **remote control server** so an external
automation harness can drive the guest gamepad over JSON-lines messages —
no human at the keyboard. Design doc: `plans/ai-remote-input-control.md`.

**Debug builds only.** This is a debugging/automation channel — it opens a
localhost TCP port and injects guest input — so it must not ship to players.
It is gated on the `FABLE2_REMOTE_CONTROL` macro, defined only for the Debug
build; Release / RelWithDebInfo builds compile out the pad driver + server
entirely (no port is opened and `fable2_control.py` is not staged).

- **Where:** `127.0.0.1:8791` by default (configurable, see below). One JSON
  object per line; every request gets exactly one response line; keep-alive
  connections are supported.
- **How:** a second synthetic pad driver
  (`src/input/remote_gamepad_driver.h`) is registered next to the keyboard driver,
  fed by the server (`src/input/remote_control_server.h`). It OR-merges with the
  human pads and is **not** gated on window focus, so the AI can drive the
  game while it's in the background.

Quick start (from the build dir, game running):

```
python tools\fable2_control.py ping
python tools\fable2_control.py press A --hold 120
python tools\fable2_control.py get-state
python tools\fable2_control.py script --file repro.json
```

Or speak the protocol directly (any language):

```
> {"cmd":"press","input":"RT","hold_ms":900}
< {"ok":true,"input":"RT","release_in_ms":900}
> {"cmd":"script","steps":[
      {"delay_ms":500,"op":"press","input":"LB","hold_ms":2000},
      {"delay_ms":1000,"op":"press","input":"A","hold_ms":80}]}
< {"ok":true,"duration_ms":2500}
```

`hold_ms` ≤ 0 (or omitted) means *hold until released/cleared*; a positive value
auto-releases after that many ms. In a `script`, each step's `delay_ms` is a **gap
since the previous step** on a single clock — the example presses LB at t=500 (held to
t=2500) and A at t=1500 (held to t=1580), so `duration_ms` (when the last input stops
being active) is 2500.

Commands: `ping`, `info`, `auth`, `press` (`input`, `hold_ms`, `value`),
`release`, `stick` (`input` = `StkLx`/`StkLy`/`StkRx`/`StkRy`, `value`,
`hold_ms`), `state` (sticky baseline: `buttons[]`, `triggers{LT,RT}`,
`stk{lx,ly,rx,ry}`), `clear`, `script` (atomic timed sequence), `get_state`,
`game_state` (current boot/menu state, see below), `cvar` (get/set any cvar
by name), `enable`/`disable`. Input names match the
keyboard-gamepad vocabulary (`A`/`B`/`X`/`Y`, `LB`/`RB`, `LT`/`RT`,
`Up`/`Down`/`Left`/`Right`, `Start`/`Back`, `L3`/`R3`, stick direction
shorthands). `StkLy` positive = forward (Fable 2 convention). `script` is a
single atomic message, so a repro sequence runs with no network round-trips
between steps.

### Game state

`game_state` reports which boot/menu screen the game is on so the AI can
navigate and verify its actions:

```
python tools\fable2_control.py game-state
> {"cmd":"game_state"}
< {"ok":true,"state":{"code":2,"name":"PressAScreen"}}
```

The classifier samples once per second on a background thread (independent of
the render loop, so it keeps working during the movie, which renders video with
no UI text) and reports one of:

| State | Meaning |
|---|---|
| `?` (Unknown) | Arena not mapped yet / undetermined (first ~1 s). |
| `PreMainMenu` | Splash / intro, before the "Press A" prompt. |
| `PressAScreen` | The "Press A to start" prompt is on screen. |
| `MainMenuMovie` | The idle movie is playing (no prompt / menu). |
| `MainMenu` | The main menu (reached by pressing A on the prompt). |

This follows the game's own state machine:
`PreMainMenu →(time)→ PressAScreen →(time)→ MainMenuMovie →(time)→
PressAScreen`, with `PressAScreen →(A)→ MainMenu` and
`MainMenuMovie →(A)→ PressAScreen`. The prompt vs. the menu are visually
indistinguishable (same element-list draw rate), so the transition into the
menu is driven by the A-press, observed on the final merged pad state (remote +
keyboard + physical — it works no matter which input drives A) and latched with
its exact timestamp. State changes are logged to the in-game logger
(`REXSYS_INFO`, same channel as the remote control) on transition only, and
every sample is written to `fable2_state_probe.log` when
`FABLE2_STATE_PROBE=1` is set.

Config (`[remote]` in `fable2_config.toml`): `enabled` (default `true`),
`host` (`127.0.0.1`; `0.0.0.0` exposes it on all interfaces), `port`
(`8791`; if busy, `+1..+9` are tried, the bound port is logged at startup),
`token` (empty = no auth; when set, each connection's first line must be
`{"cmd":"auth","token":"..."}`). Every received command is logged (audit
trail) to `logs/`.

## Building

Everything the build needs is either in this repo or auto-fetched — **no
hardcoded paths**. Prerequisites (all standard tools):

- **CMake** ≥ 3.25 (on PATH)
- **LLVM** (clang/clang++/lld) — on PATH or the default `C:\Program Files\LLVM` install
- **Ninja** (on PATH, `%USERPROFILE%\bin`, or the WinGet package dir)
- **Internet** on first build — `build.cmd` auto-downloads the prebuilt
  ReXGlue SDK v0.10.0 (~100 MB) from the
  [official release](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0)
  into `out\tooling\rexglue-sdk-0.10.0\`. A compatible official SDK on PATH
  is used instead if present. Downloads never replace the source submodule.
- **Python and Git** for code generation and source-SDK preparation; **Visual
  Studio Build Tools / Windows SDK** (x64 developer shell) for the native build.

```
build.cmd               # configure + fable_2_codegen (runs rexglue codegen on the manifest)
build.cmd fable_2       # configure + build the full executable
build.cmd <target>      # any other CMake target
build.cmd -release [t]  # build as Release (-O3) instead of Debug (-r is short form)
```

The first `build.cmd` run fetches the official SDK if needed under `out/tooling`.
Both Debug and Release build the pinned, patched SDK from source; see
[runtime build requirements](docs/RUNTIME_FIXES.md). Game content is required
for code generation but is never published with this repository.

Manual/advanced setup (normally not needed):

```
tools\setup_sdk.cmd      # fetch the prebuilt SDK only (what build.cmd auto-runs)
tools\setup_sdk_src.cmd  # fetch the SDK SOURCE for the Vulkan plugin: clones
                         # rexglue/rexglue-sdk at the pinned commit
                         # (f5337cdc), pins its submodules to the SHAs in
                         # thirdparty/rexglue-sdk-submodule-pins.txt, and
                         # applies thirdparty/rexglue-sdk-local.patch. LONG
                         # (several GB).
git                     # only needed for setup_sdk_src.cmd
```

The build also accepts explicit overrides if you keep the SDK elsewhere:
`cmake -DREXGLUE_SDK_ROOT=<prebuilt SDK root> -DREXGLUE_SDK_SOURCE=<SDK source>`
(`build.cmd` sets `REXGLUE_SDK_ROOT`/`REXGLUE_SDK_SOURCE` from its own search).

- Toolchain: the `win-amd64-debug` preset (Ninja + clang) by default. The SDK's public headers use clang builtins (`__builtin_bswap32`, `__VA_OPT__`), so plain MSVC `cl` cannot compile the generated code — a `win-msvc-debug` preset exists but only works for targets that don't compile `generated/`.
- **Release builds:** `build.cmd` defaults to the `win-amd64-debug` preset (`-O0`, `out\build\win-amd64-debug`). Pass the `-release` flag (short form: `-r`) to select the `win-amd64-release` preset (`-O3`, `out\build\win-amd64-release`):

  ```
  build.cmd -release fable_2_codegen
  build.cmd -release fable_2
  ```

  Both configurations build the patched source SDK, with separate SDK build/staging trees. Release stages `rexruntime.dll` and `rexgpu-xenos.dll`; Debug stages `rexruntimed.dll` and `rexgpu-xenosd.dll`. **Debug builds are drastically slower at runtime** — the recompiled guest code, the runtime, and the Xenos GPU emulator all run at `-O0` with assertions enabled — so use Release for any performance-sensitive run. Omit the flag to build Debug again; the two build trees are independent and can coexist. After updating, rerun `build.cmd` to refresh the SDK selection in an existing CMake cache.
- Codegen runs as part of the build and re-runs automatically when `fable_2_manifest.toml` or `default.xex` change (tracked via the generated DEPFILE).
- A full clean build recompiles ~291 generated translation units (60,462 guest functions).

## Vulkan renderer

The goal is a Vulkan rendering path for the game (the SDK's Windows GPU plugin only ships a
D3D12 Xenos backend; the SDK's own Vulkan backend lives in `rexglue-sdk-src/src/graphics/vulkan/`
but is not compiled into the shipped plugin). Per the project constraint, all Vulkan work lives in
this repo. The SDK source itself is built as stock upstream **plus this
project's tracked local patch** (`thirdparty/rexglue-sdk-local.patch` —
see below); the upstream repo is never forked or pushed to.

**Stage 1 — foundation (DONE, verified).** A self-contained Vulkan pipeline that creates a window,
inits Vulkan, and presents a clear color, proving the whole stack on a real GPU:

```
build.cmd -release fable_2_vulkan_smoke
out\build\win-amd64-release\fable_2_vulkan_smoke.exe   # -> stable blue window
```

Pipeline: dynamic `vulkan-1.dll` loader (no import lib needed) → `VkInstance` → Win32 surface →
device (graphics+present queue) → swapchain (3 images) → render pass → per-image framebuffers +
command buffers (clear-color subpass) → semaphores/fences → present loop. Logs to
`vulkan_smoke.log` next to the exe.

Key implementation notes (hard-won):
- **Window via the SDK's UI framework, not raw Win32/SDL.** The window is created with
  `rex::ui::SDLWindowedAppContext` + `rex::ui::Window` from the *shared* `rexruntime.dll` — the
  same mechanism the game uses. Raw `CreateWindowExW` and the *static* SDL build both fail to
  create/init a window here; only the shared-SDL path (the `rex::ui::*` exports) works. The target
  links the `rexruntime.lib` import lib and stages `rexruntime.dll` next to the exe.
- `#define NOMINMAX` before `<windows.h>` is required (the SDK's `math.h` uses `std::min` /
  `std::numeric_limits::max`, which the Windows `min`/`max` macros would break).
- Image views backing the swapchain **must outlive the framebuffers** (destroying them early makes
  the driver fault in `vkCmdBeginRenderPass`).
- Vendored Khronos headers live in `thirdparty/vulkan` + `thirdparty/vk_video` (Vulkan 1.3.282).

**Stage 2 — render the game via Vulkan (in progress).** The SDK's Vulkan backend is a complete,
~1.4 MB renderer compiled into `rexgpu-xenos` only when `REXGLUE_USE_VULKAN=ON` (OFF by default on
Windows). Rather than re-implementing it, we build the SDK source **plus the
project's local patch** (`thirdparty/rexglue-sdk-local.patch`) into a Vulkan GPU
plugin and load it via the `gpu_plugin` mechanism, so the renderer can be **swapped at launch, no
rebuild of the game**:

```
tools\build_sdk_vulkan.cmd     # builds the SDK source with REXGLUE_USE_VULKAN=ON
                               # (auto-fetches it first via tools\setup_sdk_src.cmd:
                               # clone at the pinned commit + submodule pins +
                               # thirdparty\rexglue-sdk-local.patch, into
                               # thirdparty\rexglue-sdk-src) -> the plugin lands
                               # in <SDK source>\out\win-amd64\Release\rexgpu-xenos.dll
```

The SDK source is the stock `rexglue/rexglue-sdk` repo **plus this project's
local modifications** (`thirdparty/rexglue-sdk-local.patch`: the main-menu
crash fix in `exception_handler_win.cpp` + `xmemory.cpp`, and the Vulkan/
FPS instrumentation — see `docs/main_menu_crash_fix.md` and the patch
header). `tools/setup_sdk_src.cmd` reproduces that exact tree from the
tracked patch + submodule pins, so the source never has to live outside
this repo.

Two plugins are staged next to the exe, selected by the `gpu_plugin` argument:

```
out\build\win-amd64-release\fable_2.exe --gpu_plugin=xenos           # D3D12 (default; prebuilt plugin)
out\build\win-amd64-release\fable_2.exe --gpu_plugin=xenos-vulkan    # Vulkan (source-built plugin)
```

- `rexgpu-xenos.dll` is the prebuilt D3D12 plugin (staged by `GPU_PLUGINS xenos`).
- `rexgpu-xenos-vulkan.dll` is the source-built plugin (staged by CMake from the SDK build tree,
  Release-only). `Fable2App::OnPreSetup` loads it with `LoadGpuPlugin("xenos-vulkan", "vulkan")`,
  forcing the Vulkan backend (the plugin is compiled with both; the default `"any"` would pick D3D12).
- The source plugin must match the game's runtime: the Release source plugin imports `rexruntime.dll`,
  so it is staged for the Release game only (the Debug game links `rexruntimed.dll`).
- The SDK source location is resolved relative to this repo (`thirdparty/rexglue-sdk-src`,
  or a sibling `..\rexglue-sdk-src` for pre-existing setups) and can be overridden with
  `-DREXGLUE_SDK_SOURCE=<path>`.

## Manifest highlights (fable_2_manifest.toml)

Migrated from the XenonRecomp config (`XenonRecomp/fable2.toml`):

- 87 manual function boundaries. ReXGlue forbids the overlapping boundaries XenonRecomp allowed: three nested pairs were trimmed to end where the inner function begins, and `0x82C8D3F0` keeps its original full size with the nested `0x82C8D4E8` entry removed (the gap-fill otherwise extended the outer and emitted an illegal cross-function `goto`). Original sizes are recorded in the file's comments and in `XenonRecomp/fable2.toml`.
- 13 "gap seed" function entries (no size — the scanner sizes them) for branch targets the auto-discovery never registered: a chain of 8-byte thunks at `0x82C000F0`–`0x82C00128` plus stragglers (`0x82BEA25C`, `0x82CD7948`, `0x82C14D58`, `0x82F279D8`, `0x82E7E4F8`, ...).
- `setjmp_address = 0x83000200`, `longjmp_address = 0x82CA9260`, and two `[[invalid_instructions]]` data-table skips, carried over verbatim.
- The XenonRecomp register save/restore helper addresses (`restgprlr_14` etc.) have no ReXGlue equivalent and are preserved only as comments.
- Known runtime risk: codegen logs a handful of non-fatal "Unresolved conditional branch" warnings (e.g. `0x82C8D408`, `0x82C99E74`, `0x82C9A0BC`); those paths emit `REX_FATAL` if hit.

## Function naming rule (important)

A manifest `name` becomes a C symbol: codegen emits the function body as
`__imp__<name>` (extern "C"). A name must therefore NOT equal an XAPI export
of `rexruntimed.dll` (the DLL exports `__imp__<XAPI name>` for every
xboxkrnl/xam hook). If it does, the local definition silently shadows the
DLL import at link time (no linker error), and the generated import-thunk
registration (`fable_2_register.cpp`) routes XAPI calls into the guest
function body instead of the SDK kernel hook. That is what broke audio when
the game-internal KeWait re-implementations were named
`KeWaitForSingleObject` / `KeWaitForMultipleObjects`: the xboxkrnl import
thunks (0x832B28AC / 0x832B2CDC) started landing in the guest wrappers,
whose timeout field is milliseconds while kernel callers pass 100ns units
(0 = infinite). They are named `..._Guest` for this reason — keep it that
way. Check before committing name changes:

```
python tools/check_manifest_collisions.py
```
