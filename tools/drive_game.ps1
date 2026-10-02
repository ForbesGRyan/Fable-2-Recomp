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
#
# -KeyboardInput: the old path. Keys are sent with keybd_event (HoldMs default
# 50), which targets whatever window has focus, so each key is sent only if the
# game window is in the foreground; otherwise it is skipped and logged (it never
# types into another window).
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
  [switch]$KeyboardInput                     # send keys with keybd_event instead of in-process autoplay
)
$ErrorActionPreference = "Stop"
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

$saved = @{}
foreach ($name in $Env.Keys) {
  $saved[$name] = [Environment]::GetEnvironmentVariable($name)
  [Environment]::SetEnvironmentVariable($name, [string]$Env[$name])
}
$argList = @("--fullscreen=false") + $GameArgs
$p = Start-Process (Join-Path $ExeDir "fable_2.exe") -ArgumentList $argList -WorkingDirectory $ExeDir -PassThru
foreach ($name in $Env.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
"started pid $($p.Id): $($argList -join ' ')"

$sw = [Diagnostics.Stopwatch]::StartNew()
$shell = if ($KeyboardInput) { New-Object -ComObject WScript.Shell } else { $null }
try {
  foreach ($ev in $events) {
    while ($sw.Elapsed.TotalSeconds -lt $ev.t) { Start-Sleep -Milliseconds 100 }
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
  }
  while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $Total) { Start-Sleep -Milliseconds 500 }
} finally {
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "closed game at $([int]$sw.Elapsed.TotalSeconds) s" }
}
