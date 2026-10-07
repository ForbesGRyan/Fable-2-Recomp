# Launch fable_2.exe and drive it into gameplay with a fixed key sequence.
#
# Default sequence (from the user): E at 23 s skips the intro, S at 27 s moves
# to Continue, E at 30 s and 35 s load the save. Verified 2026-10-02: the world
# is fading in at 40 s and fully up by 50 s, so start captures at >= 50 s.
# Default (autoplay) mode: the keys are translated to gamepad buttons (E/SPACE->A,
# S->Down, W->Up, A->Left, D->Right, ESC->B, ENTER->Start) and handed to the game
# in FABLE2_AUTOPLAY / FABLE2_AUTOPLAY_HOLD_MS, so it presses them itself from
# inside the process. No focus is needed and this script never touches the
# foreground window or the keyboard; screenshots use PrintWindow on the game
# window handle. Buttons are held HoldMs (default 100); longer holds auto-repeat
# in the menus.
# A plan entry "55:LS=0/100/3000" (or RS=...) holds a stick at x/y percent (-100..100,
# positive y forward, positive x right) for that many ms (x, y -100..100, ms 1..60000; a bad
# entry throws here instead of the game rejecting the whole plan). "56:VIEW=native"
# (off|split|overlay|native|pattern) switches the native view and "66:DISC=60/4" arms a
# discovery capture of 60 frames, every 4th draw (frames 1..100000, every 1..64); both
# are run by the game's autoplay thread and passed through unchanged.
#
# -KeyboardInput: the old path. Keys are sent with keybd_event (HoldMs default
# 50), which targets whatever window has focus, so each key is sent only if the
# game window is in the foreground; otherwise it is skipped and logged (it never
# types into another window).
#
# GAME RUNS ONLY WHILE THE USER IS AWAY (user decision, 2026-10-07). This script enforces it.
# Automated agents start the game ONLY through this script, WITHOUT -UserPresent; every other
# launcher (tools\*.cmd, the launcher\ app, running fable_2.exe directly) is for a person at the keyboard.
#  - Before launching it runs the idle gate of tools/idle_check.ps1: no keyboard or mouse input,
#    no active, connecting or unplugged XInput pad, no sound on the default playback device (10 s),
#    no other fullscreen or presentation program and no fable_2.exe already running, for 300 s
#    (fixed: there is no parameter, environment variable or option that lowers it), waiting up to
#    -IdleWaitSeconds (default 1800; the gate needs at least 10 s of observation, a pad 300 s).
#    If the gate does not pass, or fails for any reason, the game is not launched:
#    message "REFUSED: ..." and exit code 10.
#  - The game is assigned to a Win32 job object (kill on close) held by this script, so the OS
#    kills the game if this script dies for any reason. If that fails the game is killed at once
#    (exit 12). The game also runs with SDL_AUDIO_DRIVER=fable-test-unavailable-backend (the
#    tools\StartWithoutAudio.cmd mechanism) so its own sound does not trip the audio signal;
#    an -Env value for SDL_AUDIO_DRIVER wins.
#  - From launch to the end it polls the same signals (about every 100 ms plus the probe time,
#    between its timing and screenshot steps; the pad and audio latches are sampled every 10 and
#    50 ms by a background thread; the game's own in-process autoplay and the PrintWindow
#    screenshots are not OS input and do not trip it; the running-game check is gate-only). On a
#    signal it kills the game at once, prints "RUN DISTURBED: <signal> at <seconds> s", moves the
#    run's screenshots into <ShotDir>\disturbed_<timestamp>\, writes that line, a warning and the
#    moved files to the marker file RUN_DISTURBED.txt (in -ShotDir when -Shots is given, else in
#    <ExeDir>\logs) and exits 11. A watch error counts as a disturbance (fail closed).
#  - A run with no disturbance ends with the last output line "RUN CLEAN: no user input for
#    <seconds> s" (seconds from launch to close) and exit 0, printed only after the game is
#    confirmed closed. An error during the run closes the game and ends with
#    "RUN ABORTED: <message>" and exit 12. A game that exits before -Total prints
#    "GAME EXITED EARLY at <seconds> s" and exits 13 (never RUN CLEAN). A stale marker from an
#    earlier run is renamed to RUN_DISTURBED.<timestamp>.previous.txt before launch.
#  - Every run appends one line to <ExeDir>\logs\drive_game_runs.log: time, exit code, gate
#    result and signals ("USER PRESENT OVERRIDE" when -UserPresent was used).
#  Exit codes: 0 clean (or unchecked with -UserPresent), 2 refused option combination,
#  10 gate refused (nothing launched), 11 disturbed, 12 aborted, 13 game exited early;
#  1 is a script error before launch (bad plan, unknown parameter, ambiguous abbreviation).
#  -UserPresent (spelled in full; abbreviations are rejected): ONLY for the person at the keyboard
#  running this script themselves. It skips the gate and the watch and prints a prominent line
#  saying so. AGENTS MUST NEVER PASS IT. -KeyboardInput (the user driving with real key presses)
#  requires -UserPresent.
#  -TestHarness/-TestExe/-TestExeArgs: self-test of this script's run path (tests\test_idle_gate.py).
#  They replace the Win32 probes by a scripted probe and the game by a stand-in process. They are
#  refused unless all of -TestHarness and -TestExe are given, never together with -UserPresent, and
#  refused when -TestExe is named like the game (any name containing "fable"), is the built
#  fable_2.exe (same path or same bytes), or sits in a folder with rexruntime.dll or fable_2.toml.
#  This is a guard against accidents, not against someone who deliberately builds a disguised
#  copy of the game: the rule that agents start the game only through this script without
#  test parameters is policy.
#
# Examples (from the repo root, PowerShell):
#   .\tools\drive_game.ps1 -Total 120
#   .\tools\drive_game.ps1 -Total 150 -Env @{FABLE2_NATIVE_DISCOVERY="300"; FABLE2_NATIVE_DISCOVERY_DELAY="50"}
#   .\tools\drive_game.ps1 -GameArgs "--native_render_log_vertex_bindings=true" -Shots "50,80"
param(
  [int]$Total = 120,                         # seconds before the game is closed
  [string]$Plan = "23:E,27:S,30:E,35:E",     # second:key list
  [string]$Shots = "",                       # seconds for window screenshots, e.g. "50,80"
  [string]$ShotDir = "",                     # default: <exe folder>\logs\shots
  [string[]]$GameArgs = @(),                 # extra fable_2.exe arguments
  [hashtable]$Env = @{},                     # environment variables for the game process
  [string]$ExeDir = "",                      # default: out\build\win-amd64-release
  [int]$HoldMs = 0,                          # press time; default 100 (autoplay) / 50 (-KeyboardInput)
  [switch]$KeyboardInput,                    # send keys with keybd_event instead of in-process autoplay; needs -UserPresent
  [int]$IdleWaitSeconds = 1800,              # how long the gate may wait for the 300 s of absence
  [switch]$UserPresent,                      # the person at the keyboard runs this script themselves: skips gate and watch. AGENTS MUST NOT PASS IT
  [switch]$UserPresentAbbreviationGuard,     # makes every abbreviation of -UserPresent ambiguous (an error); refused if given
  [string]$TestHarness = "",                 # self-test only, see the header
  [string]$TestExe = "",                     # self-test only
  [string]$TestExeArgs = ""                  # self-test only: arguments of the stand-in, separated by |
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if ($HoldMs -le 0) { $HoldMs = if ($KeyboardInput) { 50 } else { 100 } }
if (-not $ExeDir) { $ExeDir = Join-Path $Root "out\build\win-amd64-release" }
if (-not $ShotDir) { $ShotDir = Join-Path $ExeDir "logs\shots" }
$RequiredAbsenceSeconds = 300   # the idle requirement; nothing lowers it
$script:GateText = "not evaluated"
$script:sampledNative = $null

# Ends the script: one line in the durable run log, the sampler stopped, then exit.
function Exit-Run([int]$Code) {
  try {
    $dir = Join-Path $ExeDir "logs"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Content -LiteralPath (Join-Path $dir "drive_game_runs.log") -Value ("{0:yyyy-MM-dd HH:mm:ss}  exit={1}  gate={2}" -f (Get-Date), $Code, $script:GateText)
  } catch { }
  try { if ($script:sampledNative) { $script:sampledNative.StopSampler() } } catch { }
  exit $Code
}
if ($UserPresentAbbreviationGuard) {
  Write-Output "REFUSED: -UserPresentAbbreviationGuard is not an option. Nothing launched."
  Exit-Run 2
}
if ($KeyboardInput -and -not $UserPresent) {
  Write-Output "REFUSED: -KeyboardInput means the user is driving; it requires -UserPresent. Nothing launched."
  Exit-Run 2
}
if ($args.Count) {
  Write-Output "REFUSED: unknown arguments ($($args -join ' ')); nothing is ignored silently. Nothing launched."
  Exit-Run 2
}
$testMode = [bool]($TestHarness -or $TestExe -or $TestExeArgs)
if ($testMode) {
  $why = $null
  if (-not ($TestHarness -and $TestExe)) { $why = "-TestHarness and -TestExe must be given together" }
  elseif ($UserPresent -or $KeyboardInput) { $why = "test parameters cannot be combined with -UserPresent or -KeyboardInput" }
  elseif ([IO.Path]::GetFileName($TestExe) -match 'fable') { $why = "the stand-in must not be named like the game" }
  elseif (-not (Test-Path -LiteralPath $TestExe -PathType Leaf)) { $why = "the stand-in does not exist" }
  elseif (-not (Test-Path -LiteralPath $TestHarness -PathType Leaf)) { $why = "the harness script does not exist" }
  else {
    $standIn = (Resolve-Path -LiteralPath $TestExe).Path
    foreach ($dir in @($ExeDir, (Join-Path $Root "out\build\win-amd64-release"))) {
      $real = Join-Path $dir "fable_2.exe"
      if (-not (Test-Path -LiteralPath $real -PathType Leaf)) { continue }
      if ((Resolve-Path -LiteralPath $real).Path -ieq $standIn) { $why = "the stand-in is the game"; break }
      if ((Get-Item -LiteralPath $real).Length -eq (Get-Item -LiteralPath $standIn).Length -and
          (Get-FileHash -LiteralPath $real).Hash -eq (Get-FileHash -LiteralPath $standIn).Hash) { $why = "the stand-in has the game's bytes"; break }
    }
    $sd = Split-Path -Parent $standIn
    if (-not $why -and ((Test-Path (Join-Path $sd "rexruntime.dll")) -or (Test-Path (Join-Path $sd "fable_2.toml")))) { $why = "the stand-in sits in a game folder" }
  }
  if ($why) {
    Write-Output "REFUSED: test mode: $why. Nothing launched."
    Exit-Run 2
  }
  Write-Output "*** TEST MODE: scripted probe and stand-in process '$TestExe', not the game ***"
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class DriveGameNative {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attrs, string name);
  [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint len);
  [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [StructLayout(LayoutKind.Sequential)] struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
    public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
    public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
    public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass; }
  [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong a, b, c, d, e, f; }
  [StructLayout(LayoutKind.Sequential)] struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
    public JOBOBJECT_BASIC_LIMIT_INFORMATION Basic; public IO_COUNTERS Io;
    public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit; public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed; }
  // A job object that kills every process in it when its last handle closes (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE).
  // The caller keeps the handle for its whole life, so the OS closes it, and kills the game, when the caller dies.
  public static IntPtr CreateKillOnCloseJob() {
    IntPtr job = CreateJobObject(IntPtr.Zero, null);
    if (job == IntPtr.Zero) return IntPtr.Zero;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
    info.Basic.LimitFlags = 0x2000;
    int len = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
    IntPtr mem = Marshal.AllocHGlobal(len);
    try {
      Marshal.StructureToPtr(info, mem, false);
      if (!SetInformationJobObject(job, 9, mem, (uint)len)) return IntPtr.Zero;   // JobObjectExtendedLimitInformation
    } finally { Marshal.FreeHGlobal(mem); }
    return job;
  }
  public static bool AssignToJob(IntPtr job, IntPtr process) { return job != IntPtr.Zero && AssignProcessToJobObject(job, process); }
}
"@
# Without this, GetWindowRect returns DPI-virtualised (scaled-down) sizes on a scaled display
# and the PrintWindow bitmap crops the window (e.g. to its left 75% at 133%).
[void][DriveGameNative]::SetProcessDPIAware()
$vk = @{
  "E"=0x45; "S"=0x53; "W"=0x57; "A"=0x41; "D"=0x44; "ESC"=0x1B; "ENTER"=0x0D; "SPACE"=0x20;
  "UP"=0x26; "DOWN"=0x28; "LEFT"=0x25; "RIGHT"=0x27; "F1"=0x70; "F2"=0x71; "F3"=0x72; "F6"=0x75
}

$padButton = @{
  "E"="A"; "SPACE"="A"; "S"="Down"; "W"="Up"; "A"="Left"; "D"="Right"; "ESC"="B"; "ENTER"="Start"
}

$events = @()
$autoplaySteps = @()
foreach ($e in $Plan.Split(",")) {
  if (-not $e) { continue }
  $t, $k = $e.Split(":")
  if (-not $KeyboardInput -and $k -match '^(?i:LS|RS)=(-?\d+)/(-?\d+)/(\d+)$') {
    # Same ranges as src/input/autoplay.h: the game rejects the whole plan on a bad one.
    $sx = [long]$Matches[1]; $sy = [long]$Matches[2]; $sms = [long]$Matches[3]
    if ($sx -lt -100 -or $sx -gt 100 -or $sy -lt -100 -or $sy -gt 100 -or $sms -lt 1 -or $sms -gt 60000) {
      throw "bad stick entry '$k': x and y must be -100..100 and ms 1..60000"
    }
    $autoplaySteps += "${t}:$k"
    continue
  }
  # View switch and timed discovery capture, run by the game's autoplay thread.
  if (-not $KeyboardInput -and $k -match '^(?i:VIEW)=(?i:off|split|overlay|native|pattern)$') {
    $autoplaySteps += "${t}:$k"
    continue
  }
  if (-not $KeyboardInput -and $k -match '^(?i:DISC)=(\d+)/(\d+)$') {
    $df = [long]$Matches[1]; $de = [long]$Matches[2]
    if ($df -lt 1 -or $df -gt 100000 -or $de -lt 1 -or $de -gt 64) {
      throw "bad capture entry '$k': frames must be 1..100000 and every 1..64"
    }
    $autoplaySteps += "${t}:$k"
    continue
  }
  if ($KeyboardInput) {
    if (-not $vk.ContainsKey($k.ToUpper())) { throw "unknown key '$k' (known: $($vk.Keys -join ', '))" }
    $events += [pscustomobject]@{ t = [double]$t; k = $k.ToUpper(); shot = $false }
  } else {
    if (-not $padButton.ContainsKey($k.ToUpper())) { throw "key '$k' has no pad button in autoplay mode (known: $($padButton.Keys -join ', '))" }
    $autoplaySteps += "${t}:$($padButton[$k.ToUpper()])"
  }
}
if (-not $KeyboardInput) {
  $Env = @{} + $Env
  $Env["FABLE2_AUTOPLAY"] = $autoplaySteps -join ","
  $Env["FABLE2_AUTOPLAY_HOLD_MS"] = [string]$HoldMs
}
foreach ($t in $Shots.Split(",")) {
  if ($t) { $events += [pscustomobject]@{ t = [double]$t; k = ""; shot = $true } }
}
$events = $events | Sort-Object t

# ---- Idle gate: no path below launches the game unless this passed or -UserPresent was given ----
$gatePassed = $false
$baselineTick = [long]0
$baselineStamp = [double]0
$native = $null
$harness = $null
if ($UserPresent) {
  $script:GateText = "USER PRESENT OVERRIDE (gate and watch skipped)"
  Write-Output "*** -UserPresent: the user runs this script; the idle gate and the watch are SKIPPED ***"
} else {
  try {
    . (Join-Path $PSScriptRoot "idle_check.ps1")
    if ($testMode) {
      $harness = & $TestHarness
      $native = $harness.Native
      if ($null -eq $native -or $null -eq $harness.Clock -or $null -eq $harness.Sleep) { throw "the test harness must return Native, Clock and Sleep" }
    } else {
      $native = $script:Fable2Native
    }
    $script:sampledNative = $native
    $native.StartSampler()
    $probe = { Get-Fable2Snapshot -CheckProcess -Native $native }.GetNewClosure()
    $clock = { $native.Now() }.GetNewClosure()
    $sleepSb = if ($harness) { $harness.Sleep } else { $null }
    $gate = Wait-Fable2Away -MinIdleSeconds $RequiredAbsenceSeconds -MaxWaitSeconds $IdleWaitSeconds -Probe $probe -Clock $clock -Sleep $sleepSb
    if ($gate.Passed) {
      $gatePassed = $true
      $baselineTick = $gate.Snapshot.LastInputTick
      $baselineStamp = $gate.Snapshot.Now
      $script:GateText = "passed (no user input for at least $RequiredAbsenceSeconds s)"
    } else {
      $script:GateText = "refused: $($gate.Signals -join ',')"
      Write-Output "REFUSED: user present or state unknown ($($gate.Signals -join ', ')) after waiting up to $IdleWaitSeconds s. Nothing launched."
    }
  } catch {
    $script:GateText = "refused: gate error ($($_.Exception.Message))"
    Write-Output "REFUSED: idle gate failed ($($_.Exception.Message)); counted as user present. Nothing launched."
  }
  if (-not $gatePassed) { Exit-Run 10 }
  Write-Output "idle gate passed: no user input for at least $RequiredAbsenceSeconds s"
  # Silence the game so its own sound does not trip the audio signal (an explicit -Env value wins).
  $Env = @{} + $Env
  if (-not $Env.ContainsKey("SDL_AUDIO_DRIVER")) { $Env["SDL_AUDIO_DRIVER"] = "fable-test-unavailable-backend" }
}

# Marker file location: next to the screenshots, or next to the log when none are requested.
$markerDir = if ($Shots) { $ShotDir } else { Join-Path $ExeDir "logs" }
$markerFile = Join-Path $markerDir "RUN_DISTURBED.txt"
if (-not $UserPresent -and (Test-Path -LiteralPath $markerFile)) {
  Move-Item -LiteralPath $markerFile -Destination (Join-Path $markerDir ("RUN_DISTURBED.{0}.previous.txt" -f (Get-Date -Format "yyyyMMdd_HHmmss"))) -Force
}

# The OS kills the game when this process dies for any reason: the job handle is held for the
# life of the script and is never closed explicitly.
$script:job = [DriveGameNative]::CreateKillOnCloseJob()
if ($script:job -eq [IntPtr]::Zero) {
  Write-Output "RUN ABORTED: could not create the kill-on-close job object. Nothing launched."
  Exit-Run 12
}

$script:p = $null
$script:disturbAt = 0.0
$script:shotFiles = New-Object System.Collections.ArrayList
$testCtx = @{ ShotDir = $(if ($Shots) { $ShotDir } else { $null }); ShotFiles = $script:shotFiles; shotMade = $false }
$sw = [Diagnostics.Stopwatch]::StartNew()

# Kills the game through the process handle and waits for it to be gone.
function Stop-Game {
  if (-not $script:p) { return }
  if ($harness -and $harness.NoKill) { return }   # test mode only: simulates a game that cannot be killed
  try {
    if (-not $script:p.HasExited) {
      $script:p.Kill()
      "closed game at $([int]$sw.Elapsed.TotalSeconds) s"
    }
  } catch {
    try { Stop-Process -Id $script:p.Id -Force } catch { }
  }
  try { [void]$script:p.WaitForExit(5000) } catch { }
}

# Polls the presence signals; on one it kills the game at once and throws. No-op with -UserPresent.
function Invoke-WatchPoll {
  if ($harness -and $harness.Tick) { & $harness.Tick $sw.Elapsed.TotalSeconds $testCtx }
  if ($UserPresent) { return }
  $sig = $null
  try {
    $s = Get-Fable2Snapshot -Native $native
    $gamePid = if ($script:p) { $script:p.Id } else { 0 }
    $sig = Get-Fable2WatchSignal -LastInputTick $s.LastInputTick -BaselineInputTick $baselineTick `
      -PadActivityStamp $s.PadActivityStamp -PadChangeStamp $s.PadChangeStamp -AudioStamp $s.AudioStamp `
      -BaselineStamp $baselineStamp -NotificationState $s.NotificationState -ForegroundPid $s.ForegroundPid `
      -GamePid $gamePid -ProbeFailed $s.ProbeFailed -Session0 $s.Session0 -ForcePresent $s.ForcePresent
  } catch { $sig = "watch-error" }
  if ($sig) {
    $script:disturbAt = $sw.Elapsed.TotalSeconds
    try { if ($script:p -and -not $script:p.HasExited) { $script:p.Kill() } } catch { }
    throw "FABLE2-DISTURBED:$sig"
  }
}
function Wait-Until([double]$seconds) {
  while ($sw.Elapsed.TotalSeconds -lt $seconds) { Invoke-WatchPoll; Start-Sleep -Milliseconds 100 }
}

$saved = @{}
$disturbSignal = $null; $abortMessage = $null; $earlyExit = $false
try {
  foreach ($name in $Env.Keys) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, [string]$Env[$name])
  }
  $argList = @("--fullscreen=false") + $GameArgs
  $exePath = Join-Path $ExeDir "fable_2.exe"
  $workDir = $ExeDir
  if ($testMode) { $exePath = $TestExe; $workDir = Split-Path -Parent $TestExe; $argList = @($TestExeArgs.Split("|") | Where-Object { $_ }) }
  # The only place the game is started.
  if (-not ($UserPresent -or $gatePassed)) { throw "internal error: launch without a passed idle gate" }
  $spawn = @{ FilePath = $exePath; WorkingDirectory = $workDir; PassThru = $true }
  if ($argList.Count) { $spawn.ArgumentList = $argList }
  $script:p = Start-Process @spawn
  $p = $script:p
  foreach ($name in $Env.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
  "started pid $($p.Id): $($argList -join ' ')"
  if (-not [DriveGameNative]::AssignToJob($script:job, $p.Handle)) {
    throw "could not assign the game to the kill-on-close job object (the game is being closed)"
  }

  $sw.Restart()
  $shell = if ($KeyboardInput) { New-Object -ComObject WScript.Shell } else { $null }
  foreach ($ev in $events) {
    Wait-Until $ev.t
    if ($p.HasExited) { break }
    $p.Refresh()
    $hwnd = $p.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { "no game window yet at $($ev.t) s; skipped"; continue }
    if ($KeyboardInput) {
      [void]$shell.AppActivate($p.Id)
      [void][DriveGameNative]::SetForegroundWindow($hwnd)
      Start-Sleep -Milliseconds 150
    }
    if ($ev.shot) {
      New-Item -ItemType Directory -Force $ShotDir | Out-Null
      $r = New-Object DriveGameNative+RECT
      [void][DriveGameNative]::GetWindowRect($hwnd, [ref]$r)
      $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $hdc = $g.GetHdc(); [void][DriveGameNative]::PrintWindow($hwnd, $hdc, 2); $g.ReleaseHdc($hdc)
      $file = Join-Path $ShotDir ("shot_{0}s.png" -f [int]$ev.t)
      $bmp.Save($file); $g.Dispose(); $bmp.Dispose()
      [void]$script:shotFiles.Add($file)
      "shot $($ev.t) s -> $file"
    } elseif ([DriveGameNative]::GetForegroundWindow() -ne $hwnd) {
      "key $($ev.k) at $($ev.t) s SKIPPED: game window not in foreground"
    } else {
      $code = [byte]$vk[$ev.k]
      [DriveGameNative]::keybd_event($code, 0, 0, [UIntPtr]::Zero)
      Start-Sleep -Milliseconds $HoldMs
      [DriveGameNative]::keybd_event($code, 0, 2, [UIntPtr]::Zero)
      "key $($ev.k) at $($ev.t) s"
    }
    Invoke-WatchPoll
  }
  while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $Total) { Invoke-WatchPoll; Start-Sleep -Milliseconds 100 }
  if ($p.HasExited -and $sw.Elapsed.TotalSeconds -lt $Total) {
    $earlyExit = $true
    "GAME EXITED EARLY at $([int]$sw.Elapsed.TotalSeconds) s"
  }
  Invoke-WatchPoll
} catch {
  if ($_.Exception.Message -like "FABLE2-DISTURBED:*") { $disturbSignal = $_.Exception.Message.Substring("FABLE2-DISTURBED:".Length) }
  else { $abortMessage = $_.Exception.Message }
} finally {
  # Every exit path closes the game; the environment is restored.
  Stop-Game
  foreach ($name in $saved.Keys) { try { [Environment]::SetEnvironmentVariable($name, $saved[$name]) } catch { } }
}

$gameClosed = $true
if ($script:p) { try { $gameClosed = $script:p.HasExited } catch { $gameClosed = $false } }

if ($disturbSignal) {
  $line = "RUN DISTURBED: $disturbSignal at {0:N1} s" -f $script:disturbAt
  $script:GateText += "; DISTURBED: $disturbSignal"
  $moved = @()
  try {
    if ($script:shotFiles.Count) {
      $sub = Join-Path $ShotDir ("disturbed_{0}" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
      New-Item -ItemType Directory -Force $sub | Out-Null
      foreach ($f in $script:shotFiles) {
        if (Test-Path -LiteralPath $f) { Move-Item -LiteralPath $f -Destination $sub -Force; $moved += (Join-Path $sub (Split-Path -Leaf $f)) }
      }
    }
  } catch { "WARNING: could not move the run's screenshots ($($_.Exception.Message))" }
  try {
    New-Item -ItemType Directory -Force $markerDir | Out-Null
    $text = @(
      $line,
      "The user was present during this run: do NOT use its screenshots, logs or numbers as evidence.",
      "Screenshots taken before the disturbance were moved out of the folder and are kept only for reference:")
    if ($moved.Count) { $text += ($moved | ForEach-Object { "  $_" }) } else { $text += "  (none)" }
    $text += ("written {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
    Set-Content -LiteralPath $markerFile -Encoding ASCII -Value $text
    "marker written: $markerFile"
  } catch { "WARNING: could not write the marker file $markerFile ($($_.Exception.Message))" }
  if (-not $gameClosed) { "WARNING: the game process is still running after the kill" }
  Write-Output $line
  Exit-Run 11
}
if ($abortMessage) {
  $script:GateText += "; ABORTED"
  Write-Output "RUN ABORTED: $abortMessage"
  Exit-Run 12
}
if (-not $gameClosed) {
  $script:GateText += "; ABORTED"
  Write-Output "RUN ABORTED: the game is still running after the kill"
  Exit-Run 12
}
if ($earlyExit) { $script:GateText += "; EARLY EXIT"; Exit-Run 13 }
if ($UserPresent) { Write-Output "RUN UNCHECKED: -UserPresent given, no idle gate and no watch" }
else { Write-Output ("RUN CLEAN: no user input for {0} s" -f [int]$sw.Elapsed.TotalSeconds) }
Exit-Run 0
