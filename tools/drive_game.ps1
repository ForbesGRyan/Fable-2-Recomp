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
# GAME RUNS ONLY WHILE THE USER IS AWAY (user decision, 2026-10-07). This script enforces it:
#  - Before launching it runs the idle gate of tools/idle_check.ps1: no keyboard or mouse input,
#    no active XInput controller, no other fullscreen or presentation program and no fable_2.exe
#    already running, for -IdleSeconds (default 300, at least 60), waiting up to
#    -IdleWaitSeconds (default 1800). If the gate does not pass, or fails for any reason, the
#    game is not launched: message "REFUSED: ..." and exit code 10.
#  - From launch to the end it polls the same signals (every 100 ms, between its timing and
#    screenshot steps; the game's own in-process autoplay and the PrintWindow screenshots are
#    not OS input and do not trip it). On a signal it kills the game at once, prints
#    "RUN DISTURBED: <signal> at <seconds> s", writes that line plus a warning to the marker
#    file RUN_DISTURBED.txt (in -ShotDir when -Shots is given, else in <ExeDir>\logs) and exits 11.
#    Screenshots taken before the disturbance stay on disk but must not be used. A watch
#    error counts as a disturbance (fail closed).
#  - A run with no disturbance ends with the last output line "RUN CLEAN: no user input for
#    <seconds> s" (seconds from launch to close) and exit 0. An error during the run closes the
#    game and ends with "RUN ABORTED: <message>" and exit 12 (not clean). A stale marker from
#    an earlier run is renamed to RUN_DISTURBED.<timestamp>.previous.txt before launch.
#  Exit codes: 0 clean, 2 refused option combination, 10 gate refused (nothing launched),
#  11 disturbed, 12 aborted; 1 is a script error before launch (bad plan etc.).
#  -UserPresent: ONLY for the person at the keyboard running this script themselves. It skips
#  the gate and the watch and prints a prominent line saying so. AGENTS MUST NEVER PASS IT.
#  -KeyboardInput (the user driving with real key presses) requires -UserPresent.
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
  [int]$IdleSeconds = 300,                   # the user must have been away this long before launch (>= 60)
  [int]$IdleWaitSeconds = 1800,              # how long the gate may wait for that
  [switch]$UserPresent                       # the person at the keyboard runs this script themselves: skips gate and watch. AGENTS MUST NOT PASS IT
)
$ErrorActionPreference = "Stop"
if ($KeyboardInput -and -not $UserPresent) {
  Write-Output "REFUSED: -KeyboardInput means the user is driving; it requires -UserPresent. Nothing launched."
  exit 2
}
if (-not $UserPresent -and $IdleSeconds -lt 60) {
  Write-Output "REFUSED: -IdleSeconds must be at least 60 (got $IdleSeconds). Nothing launched."
  exit 2
}
$Root = Split-Path -Parent $PSScriptRoot
if ($HoldMs -le 0) { $HoldMs = if ($KeyboardInput) { 50 } else { 100 } }
if (-not $ExeDir) { $ExeDir = Join-Path $Root "out\build\win-amd64-release" }
if (-not $ShotDir) { $ShotDir = Join-Path $ExeDir "logs\shots" }

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
if ($UserPresent) {
  Write-Output "*** -UserPresent: the user runs this script; the idle gate and the watch are SKIPPED ***"
} else {
  try {
    . (Join-Path $PSScriptRoot "idle_check.ps1")
    $gate = Wait-Fable2Away -MinIdleSeconds $IdleSeconds -MaxWaitSeconds $IdleWaitSeconds
    if ($gate.Passed) { $gatePassed = $true; $baselineTick = $gate.Snapshot.LastInputTick }
    else { Write-Output "REFUSED: user present or state unknown ($($gate.Signals -join ', ')) after waiting up to $IdleWaitSeconds s. Nothing launched." }
  } catch {
    Write-Output "REFUSED: idle gate failed ($($_.Exception.Message)); counted as user present. Nothing launched."
  }
  if (-not $gatePassed) { exit 10 }
  Write-Output "idle gate passed: no user input for at least $IdleSeconds s"
}

# Marker file location: next to the screenshots, or next to the log when none are requested.
$markerDir = if ($Shots) { $ShotDir } else { Join-Path $ExeDir "logs" }
$markerFile = Join-Path $markerDir "RUN_DISTURBED.txt"
if (-not $UserPresent -and (Test-Path -LiteralPath $markerFile)) {
  Move-Item -LiteralPath $markerFile -Destination (Join-Path $markerDir ("RUN_DISTURBED.{0}.previous.txt" -f (Get-Date -Format "yyyyMMdd_HHmmss"))) -Force
}

$script:p = $null
$script:disturbAt = 0.0
$sw = [Diagnostics.Stopwatch]::StartNew()
# Polls the presence signals; on one it kills the game at once and throws. No-op with -UserPresent.
function Invoke-WatchPoll {
  if ($UserPresent) { return }
  $sig = $null
  try {
    $s = Get-Fable2Snapshot
    $gamePid = if ($script:p) { $script:p.Id } else { 0 }
    $sig = Get-Fable2WatchSignal -LastInputTick $s.LastInputTick -BaselineInputTick $baselineTick `
      -ControllerActive $s.ControllerActive -NotificationState $s.NotificationState `
      -ForegroundPid $s.ForegroundPid -GamePid $gamePid -ProbeFailed $s.ProbeFailed `
      -Session0 $s.Session0 -ForcePresent $s.ForcePresent
  } catch { $sig = "watch-error" }
  if ($sig) {
    $script:disturbAt = $sw.Elapsed.TotalSeconds
    try { if ($script:p -and -not $script:p.HasExited) { Stop-Process -Id $script:p.Id -Force } } catch { }
    throw "FABLE2-DISTURBED:$sig"
  }
}
function Wait-Until([double]$seconds) {
  while ($sw.Elapsed.TotalSeconds -lt $seconds) { Invoke-WatchPoll; Start-Sleep -Milliseconds 100 }
}

$saved = @{}
$disturbSignal = $null; $abortMessage = $null
try {
  foreach ($name in $Env.Keys) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, [string]$Env[$name])
  }
  $argList = @("--fullscreen=false") + $GameArgs
  # The only place the game is started.
  if (-not ($UserPresent -or $gatePassed)) { throw "internal error: launch without a passed idle gate" }
  $script:p = Start-Process (Join-Path $ExeDir "fable_2.exe") -ArgumentList $argList -WorkingDirectory $ExeDir -PassThru
  $p = $script:p
  foreach ($name in $Env.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
  "started pid $($p.Id): $($argList -join ' ')"

  $sw.Restart()
  $shell = if ($KeyboardInput) { New-Object -ComObject WScript.Shell } else { $null }
  foreach ($ev in $events) {
    Wait-Until $ev.t
    if ($p.HasExited) { "game exited at $([int]$sw.Elapsed.TotalSeconds) s"; break }
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
  Invoke-WatchPoll
} catch {
  if ($_.Exception.Message -like "FABLE2-DISTURBED:*") { $disturbSignal = $_.Exception.Message.Substring("FABLE2-DISTURBED:".Length) }
  else { $abortMessage = $_.Exception.Message }
} finally {
  # Every exit path closes the game the same way as at the end of a normal run.
  try { if ($script:p -and -not $script:p.HasExited) { Stop-Process -Id $script:p.Id -Force; "closed game at $([int]$sw.Elapsed.TotalSeconds) s" } } catch { Start-Sleep -Milliseconds 200; try { if (-not $script:p.HasExited) { Stop-Process -Id $script:p.Id -Force } } catch { } }
}

if ($disturbSignal) {
  $line = "RUN DISTURBED: $disturbSignal at {0:N1} s" -f $script:disturbAt
  try {
    New-Item -ItemType Directory -Force $markerDir | Out-Null
    Set-Content -LiteralPath $markerFile -Encoding ASCII -Value @(
      $line,
      "The user was present during this run: do NOT use its screenshots, logs or numbers as evidence.",
      "Screenshots in this folder taken before the disturbance are kept only for reference.",
      ("written {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")))
    "marker written: $markerFile"
  } catch { "WARNING: could not write the marker file $markerFile ($($_.Exception.Message))" }
  Write-Output $line
  exit 11
}
if ($abortMessage) { Write-Output "RUN ABORTED: $abortMessage"; exit 12 }
if ($UserPresent) { Write-Output "RUN UNCHECKED: -UserPresent given, no idle gate and no watch" }
else { Write-Output ("RUN CLEAN: no user input for {0} s" -f [int]$sw.Elapsed.TotalSeconds) }
exit 0
