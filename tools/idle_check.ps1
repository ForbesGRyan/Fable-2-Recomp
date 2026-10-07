# Presence gate: decides whether the user is using the machine. Library plus strict CLI.
#
# Game runs made by agents are allowed only while the user is away (user decision,
# 2026-10-07). tools/drive_game.ps1 dot-sources this file, runs the gate before it launches
# the game and polls the watch while the game runs. Read-only: sends no input, touches no
# window. The game's in-process autoplay and drive_game.ps1's PrintWindow screenshots are
# not OS input and do not count.
#
# Presence signals (each means "the user is present"; anything unknown counts as present):
#   input           keyboard or mouse input within the required idle time (GetLastInputInfo)
#   controller      a connected XInput pad (slots 0..3) showed activity: any button, a trigger
#                   above 30, a thumbstick beyond 8000 from centre. Pads have no "last input"
#                   time, so with a pad connected the gate samples every 250 ms for the whole
#                   required idle time; activity restarts that observation
#   fullscreen      SHQueryUserNotificationState says busy (2), Direct3D fullscreen (3),
#                   presentation mode (4) or a fullscreen app (7: stricter choice)
#   fable2-running  a fable_2.exe is already running (the user may be playing); gate only
#   unknown         a probe failed, an API returned something unexpected (including a
#                   notification state outside 1..7 or an XInput error other than "no
#                   controller"), or this is session 0
#   forced-present  the test hook FABLE2_IDLE_GATE_FORCE_PRESENT=1 is set. It can only ADD
#                   this signal; nothing removes a signal
# Notification state 1 (not present: screen saver running or workstation locked) is "away";
# 5 (accepts notifications) and 6 (quiet time) are neutral.
# Only a controller observation spans the whole idle time; the other non-input signals are
# instantaneous, but a gate that sees one restarts the controller observation.
#
# CLI (exit codes: 0 away, 1 present, 2 usage error or unknown state; never 0 by default):
#   .\tools\idle_check.ps1                                  # usage, exit 2
#   .\tools\idle_check.ps1 -MinIdleSeconds 300              # exit 0 only if away for 300 s
#   .\tools\idle_check.ps1 -MinIdleSeconds 300 -MaxWaitSeconds 1800   # wait up to 30 min
#   .\tools\idle_check.ps1 -Report                          # print the signals, exit 0
#                                                           # (for humans; NOT a permission to run)
#   .\tools\idle_check.ps1 -WatchSeconds 20                 # watch; exit 1 at the first signal
# -MinIdleSeconds must be at least 60. Library use: dot-source this file
# (`. .\tools\idle_check.ps1`); the CLI does not run when dot-sourced.
param(
  [int]$MinIdleSeconds = 0,     # required idle time, at least 60; gates
  [int]$MaxWaitSeconds = 0,     # how long the gate may take to see the user away
  [switch]$Report,              # print the current signals and exit 0 (not a permission to run)
  [int]$WatchSeconds = 0        # observe this long, stop at the first presence signal
)
$ErrorActionPreference = "Stop"
$script:GateGiven = $PSBoundParameters.ContainsKey('MinIdleSeconds')

if (-not ('Fable2IdleProbe' -as [type])) {
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Fable2IdleProbe {
  [StructLayout(LayoutKind.Sequential)]
  struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("shell32.dll")] static extern int SHQueryUserNotificationState(out int state);
  [StructLayout(LayoutKind.Sequential)]
  struct XINPUT_GAMEPAD {
    public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger;
    public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY;
  }
  [StructLayout(LayoutKind.Sequential)]
  struct XINPUT_STATE { public uint dwPacketNumber; public XINPUT_GAMEPAD Gamepad; }
  [DllImport("xinput1_4.dll")] static extern uint XInputGetState(uint index, out XINPUT_STATE state);

  // Seconds since the last keyboard or mouse input and that input's tick; idle -1 on failure.
  public static double LastInput(out uint tick) {
    LASTINPUTINFO info = new LASTINPUTINFO();
    info.cbSize = (uint)Marshal.SizeOf(info);
    tick = 0;
    if (!GetLastInputInfo(ref info)) return -1.0;
    tick = info.dwTime;
    return unchecked((uint)Environment.TickCount - info.dwTime) / 1000.0;
  }
  // SHQueryUserNotificationState value 1..7, or -1 when the call fails.
  public static int NotificationState() {
    int s;
    if (SHQueryUserNotificationState(out s) != 0) return -1;
    return s;
  }
  public static int ForegroundPid() {
    IntPtr h = GetForegroundWindow();
    if (h == IntPtr.Zero) return 0;
    uint pid;
    GetWindowThreadProcessId(h, out pid);
    return (int)pid;
  }
  // null: no controller in that slot. Else buttons, left and right trigger, LX, LY, RX, RY.
  // Any other XInput result is an error and throws (the caller treats it as "unknown").
  public static int[] Pad(int index) {
    XINPUT_STATE st;
    uint r = XInputGetState((uint)index, out st);
    if (r == 1167) return null;
    if (r != 0) throw new InvalidOperationException("XInputGetState(" + index + ") returned " + r);
    XINPUT_GAMEPAD g = st.Gamepad;
    return new int[] { g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY };
  }
}
"@
}

# ---- Pure decision logic (no Win32 calls; tested with synthetic values) ----

# One pad state is "activity": any button, a trigger above 30, a stick beyond 8000 from centre.
function Test-Fable2PadActive {
  param([int]$Buttons = 0, [int]$LeftTrigger = 0, [int]$RightTrigger = 0,
        [int]$ThumbLX = 0, [int]$ThumbLY = 0, [int]$ThumbRX = 0, [int]$ThumbRY = 0)
  if ($Buttons -ne 0) { return $true }
  if ($LeftTrigger -gt 30 -or $RightTrigger -gt 30) { return $true }
  foreach ($v in @($ThumbLX, $ThumbLY, $ThumbRX, $ThumbRY)) {
    if ([Math]::Abs($v) -gt 8000) { return $true }
  }
  return $false
}

# Gate decision before a run. Present = any signal. IdleSeconds < 0 means the probe failed.
# ControllerQuietSeconds is how long the caller has observed the connected pads quiet.
function Get-Fable2GateDecision {
  param([double]$IdleSeconds, [int]$MinIdleSeconds,
        [bool]$ControllerConnected = $false, [double]$ControllerQuietSeconds = 0,
        [int]$NotificationState = 5, [bool]$Fable2Running = $false,
        [bool]$ProbeFailed = $false, [bool]$Session0 = $false, [bool]$ForcePresent = $false)
  $signals = New-Object System.Collections.ArrayList
  if ($ProbeFailed -or $Session0 -or $IdleSeconds -lt 0) { [void]$signals.Add("unknown") }
  if ($ForcePresent) { [void]$signals.Add("forced-present") }
  if ($IdleSeconds -ge 0 -and $IdleSeconds -lt $MinIdleSeconds) { [void]$signals.Add("input") }
  if ($ControllerConnected -and $ControllerQuietSeconds -lt $MinIdleSeconds) { [void]$signals.Add("controller") }
  switch ($NotificationState) {
    { $_ -in 2, 3, 4, 7 } { [void]$signals.Add("fullscreen") }
    { $_ -in 1, 5, 6 } { }
    default { if (-not $signals.Contains("unknown")) { [void]$signals.Add("unknown") } }
  }
  if ($Fable2Running) { [void]$signals.Add("fable2-running") }
  return [pscustomobject]@{ Present = ($signals.Count -gt 0); Signals = @($signals) }
}

# Watch decision during a run: the first presence signal's name, or $null. Input is any change
# of the last-input tick from the baseline taken when the gate passed. A fullscreen state while
# the game window is in front is the game's own and is ignored.
function Get-Fable2WatchSignal {
  param([long]$LastInputTick, [long]$BaselineInputTick, [bool]$ControllerActive = $false,
        [int]$NotificationState = 5, [int]$ForegroundPid = 0, [int]$GamePid = 0,
        [bool]$ProbeFailed = $false, [bool]$Session0 = $false, [bool]$ForcePresent = $false)
  if ($ProbeFailed -or $Session0) { return "unknown" }
  if ($ForcePresent) { return "forced-present" }
  if ($LastInputTick -ne $BaselineInputTick) { return "input" }
  if ($ControllerActive) { return "controller" }
  switch ($NotificationState) {
    { $_ -in 2, 3, 4, 7 } { if ($GamePid -eq 0 -or $ForegroundPid -ne $GamePid) { return "fullscreen" } }
    { $_ -in 1, 5, 6 } { }
    default { return "unknown" }
  }
  return $null
}

# ---- Win32 probing ----

# One reading of every signal. Each probe is isolated; a failing one sets ProbeFailed (fail closed).
function Get-Fable2Snapshot {
  param([switch]$CheckProcess)
  $failed = @()
  $idle = -1.0; $tick = [uint32]0
  try { $idle = [Fable2IdleProbe]::LastInput([ref]$tick) } catch { $failed += "input: $($_.Exception.Message)" }
  if ($idle -lt 0) { $failed += "GetLastInputInfo failed" }
  $connected = $false; $active = $false
  try {
    for ($i = 0; $i -lt 4; $i++) {
      $s = [Fable2IdleProbe]::Pad($i)
      if ($null -eq $s) { continue }
      $connected = $true
      if (Test-Fable2PadActive -Buttons $s[0] -LeftTrigger $s[1] -RightTrigger $s[2] `
            -ThumbLX $s[3] -ThumbLY $s[4] -ThumbRX $s[5] -ThumbRY $s[6]) { $active = $true }
    }
  } catch { $failed += "xinput: $($_.Exception.Message)" }
  $ns = -1
  try { $ns = [Fable2IdleProbe]::NotificationState() } catch { $failed += "notification: $($_.Exception.Message)" }
  $fg = 0
  try { $fg = [Fable2IdleProbe]::ForegroundPid() } catch { $failed += "foreground: $($_.Exception.Message)" }
  $s0 = $false
  try { $s0 = ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) } catch { $failed += "session: $($_.Exception.Message)"; $s0 = $true }
  $running = $false
  if ($CheckProcess) {
    try { $running = (@([Diagnostics.Process]::GetProcessesByName("fable_2")).Count -gt 0) }
    catch { $failed += "process list: $($_.Exception.Message)" }
  }
  return [pscustomobject]@{
    IdleSeconds = $idle; LastInputTick = [long]$tick
    ControllerConnected = $connected; ControllerActive = $active
    NotificationState = $ns; ForegroundPid = $fg; Session0 = $s0
    Fable2Running = $running
    ForcePresent = ($env:FABLE2_IDLE_GATE_FORCE_PRESENT -eq "1")
    ProbeFailed = ($failed.Count -gt 0); Failures = $failed
  }
}

# Waits until the user has been away for MinIdleSeconds, at most MaxWaitSeconds (a controller
# observation needs MinIdleSeconds of that). Returns Passed, Signals and the last snapshot.
function Wait-Fable2Away {
  param([int]$MinIdleSeconds, [int]$MaxWaitSeconds = 0)
  if ($MinIdleSeconds -lt 60) { throw "MinIdleSeconds must be at least 60 (got $MinIdleSeconds)" }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $quietSince = 0.0; $wasConnected = $false; $lastLog = 0.0
  while ($true) {
    $snap = Get-Fable2Snapshot -CheckProcess
    $now = $sw.Elapsed.TotalSeconds
    if ($snap.ControllerConnected -and -not $wasConnected) { $quietSince = $now }
    $wasConnected = $snap.ControllerConnected
    # Any signal other than input or a quiet pad restarts the controller observation.
    $others = Get-Fable2GateDecision -IdleSeconds $snap.IdleSeconds -MinIdleSeconds 0 `
      -NotificationState $snap.NotificationState -Fable2Running $snap.Fable2Running `
      -ProbeFailed $snap.ProbeFailed -Session0 $snap.Session0 -ForcePresent $snap.ForcePresent
    if ($snap.ControllerActive -or $others.Present) { $quietSince = $now }
    $d = Get-Fable2GateDecision -IdleSeconds $snap.IdleSeconds -MinIdleSeconds $MinIdleSeconds `
      -ControllerConnected $snap.ControllerConnected -ControllerQuietSeconds ($now - $quietSince) `
      -NotificationState $snap.NotificationState -Fable2Running $snap.Fable2Running `
      -ProbeFailed $snap.ProbeFailed -Session0 $snap.Session0 -ForcePresent $snap.ForcePresent
    if (-not $d.Present) { return [pscustomobject]@{ Passed = $true; Signals = @(); Snapshot = $snap } }
    if ($now -ge $MaxWaitSeconds) { return [pscustomobject]@{ Passed = $false; Signals = $d.Signals; Snapshot = $snap } }
    if ($now - $lastLog -ge 60) { $lastLog = $now; Write-Host ("idle gate: user present ({0}), waiting ({1:N0} of {2} s)" -f ($d.Signals -join ", "), $now, $MaxWaitSeconds) }
    Start-Sleep -Milliseconds ([int][Math]::Min(250, [Math]::Max(1, ($MaxWaitSeconds - $now) * 1000)))
  }
}

# ---- CLI ----

function Invoke-Fable2IdleCli {
  if (-not ($script:GateGiven -or $Report -or $WatchSeconds -gt 0)) {
    Write-Host "usage: idle_check.ps1 -MinIdleSeconds N [-MaxWaitSeconds M]   gate: exit 0 only if the user has been away N s (N >= 60), 1 if present, 2 unknown or usage"
    Write-Host "       idle_check.ps1 -Report                                  print the signals, exit 0 (NOT a permission to run the game)"
    Write-Host "       idle_check.ps1 -WatchSeconds N                          watch N s, exit 1 at the first presence signal"
    return 2
  }
  try {
    if ($Report) {
      $s = Get-Fable2Snapshot -CheckProcess
      Write-Host ("idle: {0:N0} s since keyboard or mouse input" -f $s.IdleSeconds)
      Write-Host ("controller: connected={0} active={1}" -f $s.ControllerConnected, $s.ControllerActive)
      Write-Host ("notification state: {0} (1 away, 2 busy, 3 D3D fullscreen, 4 presentation, 5 accepts, 6 quiet, 7 app, -1 failed)" -f $s.NotificationState)
      Write-Host ("fable_2.exe running: {0}; session 0: {1}; forced present: {2}; probe failed: {3}" -f $s.Fable2Running, $s.Session0, $s.ForcePresent, $s.ProbeFailed)
      foreach ($f in $s.Failures) { Write-Host "probe failure: $f" }
      Write-Host "NOTE: this report is for humans and is not a permission to run the game. Agents run the game only through tools/drive_game.ps1, which gates and watches by itself."
      return 0
    }
    if ($WatchSeconds -gt 0) {
      $base = Get-Fable2Snapshot
      $sw = [Diagnostics.Stopwatch]::StartNew()
      while ($sw.Elapsed.TotalSeconds -lt $WatchSeconds) {
        $s = Get-Fable2Snapshot
        $sig = Get-Fable2WatchSignal -LastInputTick $s.LastInputTick -BaselineInputTick $base.LastInputTick `
          -ControllerActive $s.ControllerActive -NotificationState $s.NotificationState `
          -ForegroundPid $s.ForegroundPid -ProbeFailed ($s.ProbeFailed -or $base.ProbeFailed) `
          -Session0 $s.Session0 -ForcePresent $s.ForcePresent
        if ($sig) { Write-Host ("presence signal: {0} at {1:N1} s" -f $sig, $sw.Elapsed.TotalSeconds); return 1 }
        Start-Sleep -Milliseconds 250
      }
      Write-Host ("no presence signal for {0} s" -f $WatchSeconds)
      return 0
    }
    if ($MinIdleSeconds -lt 60) { Write-Host "refused: -MinIdleSeconds must be at least 60 (got $MinIdleSeconds)"; return 2 }
    $r = Wait-Fable2Away -MinIdleSeconds $MinIdleSeconds -MaxWaitSeconds $MaxWaitSeconds
    if ($r.Passed) { Write-Host ("away for at least {0} s: gate passed" -f $MinIdleSeconds); return 0 }
    Write-Host ("user present or unknown ({0}): do not run" -f ($r.Signals -join ", "))
    if ($r.Signals -contains "unknown") { return 2 }
    return 1
  } catch {
    Write-Host "unknown state: $($_.Exception.Message)"
    return 2
  }
}

if ($MyInvocation.InvocationName -ne '.') { exit (Invoke-Fable2IdleCli) }
