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
#   controller      an XInput pad (slots 0..3) showed activity (any button, a trigger above 30,
#                   a thumbstick more than 8000 from centre), or connected, or was unplugged.
#                   A background thread samples the pads every 10 ms (empty slots are re-scanned
#                   once a second) and latches the time of the last activity and of the last
#                   connect or disconnect, so a short tap between two polls is not missed. Once a
#                   pad has been seen, the gate needs the full idle time without activity or
#                   change; each activity, connect or disconnect restarts that observation
#   audio           sound is playing on the default playback device (Core Audio peak meter above
#                   0.005, sampled every 50 ms by the same thread). The gate needs the whole idle time (300 s from the driver) without
#                   it; the watch trips on any sample after the gate passed
#   fullscreen      SHQueryUserNotificationState says busy (2), Direct3D fullscreen (3),
#                   presentation mode (4) or a fullscreen app (7: stricter choice)
#   fable2-running  a fable_2.exe is already running (the user may be playing); gate only
#   unknown         a probe failed (including the sampler thread stalling), an API returned
#                   something unexpected (a notification state outside 1..7, an XInput error other
#                   than "no controller", no audio device), an idle time that is negative,
#                   not finite or longer than the uptime, or this is session 0
#   forced-present  the test hook FABLE2_IDLE_GATE_FORCE_PRESENT=1 is set. It can only ADD
#                   this signal; nothing removes a signal
# Notification state 1 (not present: screen saver running or workstation locked) is "away";
# 5 (accepts notifications) and 6 (quiet time) are neutral.
# Limits: only XInput pads are seen (not DirectInput or other controllers); a user who is
# silent and motionless for the whole idle time reads as away.
# Sampling: the gate loop sleeps 250 ms plus the probe time (a few ms) between decisions.
#
# CLI (exit codes: 0 away, 1 present, 2 usage error or unknown state; never 0 by default):
#   .\tools\idle_check.ps1                                  # usage, exit 2
#   .\tools\idle_check.ps1 -MinIdleSeconds 300              # exit 0 only if away for 300 s
#   .\tools\idle_check.ps1 -MinIdleSeconds 300 -MaxWaitSeconds 1800   # wait up to 30 min
#   .\tools\idle_check.ps1 -Report                          # print the signals, exit 0
#                                                           # (for humans; NOT a permission to run)
#   .\tools\idle_check.ps1 -WatchSeconds 20                 # watch; exit 1 at the first signal
# -MinIdleSeconds must be at least 60 here (drive_game.ps1 always uses 300). -Report cannot be
# combined with another mode. Library use: dot-source this file (`. .\tools\idle_check.ps1`);
# the CLI does not run when dot-sourced.
param(
  [int]$MinIdleSeconds = 0,     # required idle time, at least 60; gates
  [int]$MaxWaitSeconds = 0,     # how long the gate may take to see the user away
  [switch]$Report,              # print the current signals and exit 0 (not a permission to run)
  [int]$WatchSeconds = 0        # observe this long, stop at the first presence signal
)
$ErrorActionPreference = "Stop"
$script:GateGiven = $PSBoundParameters.ContainsKey('MinIdleSeconds')

# The type name carries the version: a stale or look-alike type from earlier in the session is
# never reused (and a type of this name without the right ProbeVersion is an error).
if (-not ('Fable2IdleProbeV3' -as [type])) {
  Add-Type -TypeDefinition @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
class Fable2MMDeviceEnumeratorCom { }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IFable2MMDeviceEnumerator {
  [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
  [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IFable2MMDevice device);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IFable2MMDevice {
  [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}

[ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IFable2AudioMeter {
  [PreserveSig] int GetPeakValue(out float peak);
}

public sealed class Fable2IdleProbeV3 {
  public const int ProbeVersion = 3;
  [StructLayout(LayoutKind.Sequential)]
  struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("shell32.dll")] static extern int SHQueryUserNotificationState(out int state);
  [DllImport("kernel32.dll")] static extern ulong GetTickCount64();
  [StructLayout(LayoutKind.Sequential)]
  struct XINPUT_GAMEPAD {
    public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger;
    public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY;
  }
  [StructLayout(LayoutKind.Sequential)]
  struct XINPUT_STATE { public uint dwPacketNumber; public XINPUT_GAMEPAD Gamepad; }
  [DllImport("xinput1_4.dll")] static extern uint XInputGetState(uint index, out XINPUT_STATE state);

  // One pad state is activity: any button, a trigger above 30, a stick more than 8000 from centre.
  public static bool PadActive(int buttons, int lt, int rt, int lx, int ly, int rx, int ry) {
    if (buttons != 0 || lt > 30 || rt > 30) return true;
    long limit = 8000L * 8000L;
    if ((long)lx * lx + (long)ly * ly > limit) return true;
    if ((long)rx * rx + (long)ry * ry > limit) return true;
    return false;
  }

  // Monotonic seconds; the sampler stamps and the callers' baselines use this same clock.
  public double Now() { return (double)Stopwatch.GetTimestamp() / Stopwatch.Frequency; }

  // { idle seconds (-1 on failure), tick of the last input }
  public double[] LastInputInfo() {
    LASTINPUTINFO info = new LASTINPUTINFO();
    info.cbSize = (uint)Marshal.SizeOf(info);
    if (!GetLastInputInfo(ref info)) return new double[] { -1.0, 0.0 };
    double idle = unchecked((uint)Environment.TickCount - info.dwTime) / 1000.0;
    return new double[] { idle, (double)info.dwTime };
  }
  public double UptimeSeconds() { return GetTickCount64() / 1000.0; }
  // SHQueryUserNotificationState value 1..7, or -1 when the call fails.
  public int NotificationState() {
    int s;
    if (SHQueryUserNotificationState(out s) != 0) return -1;
    return s;
  }
  public int ForegroundPid() {
    IntPtr h = GetForegroundWindow();
    if (h == IntPtr.Zero) return 0;
    uint pid;
    GetWindowThreadProcessId(h, out pid);
    return (int)pid;
  }
  public bool Session0() { return Process.GetCurrentProcess().SessionId == 0; }
  public bool Fable2Running() { return Process.GetProcessesByName("fable_2").Length > 0; }

  // ---- background sampler: pads every 10 ms, audio every 50 ms ----
  static readonly object Lk = new object();
  static Thread th;
  static volatile bool stopFlag;
  static double heartbeat = -1;
  static Fable2SamplerCore core;
  static IFable2AudioMeter meterCom;
  static double meterAcquired;

  public void StartSampler() {
    lock (Lk) {
      if (th != null && th.IsAlive) return;
      stopFlag = false;
      core = new Fable2SamplerCore(RealPad, RealMeter);
      heartbeat = NowS();
      th = new Thread(SampleLoop);
      th.IsBackground = true;
      th.Name = "Fable2IdleSampler";
      th.Start();
    }
  }
  public void StopSampler() {
    Thread t;
    lock (Lk) { t = th; stopFlag = true; }
    if (t != null) t.Join(2000);
    lock (Lk) { th = null; heartbeat = -1; }
  }
  // { pad connected 0/1, pad activity stamp, pad change stamp, audio stamp, pad error stamp,
  //   audio error stamp, sampler heartbeat stamp, sampler running 0/1 }; -1 stamps: never
  public double[] Latch() {
    lock (Lk) {
      bool run = th != null && th.IsAlive;
      double[] c = core != null ? core.Latch() : new double[] { 0, -1, -1, -1, -1, -1 };
      return new double[] { c[0], c[1], c[2], c[3], c[4], c[5], heartbeat, run ? 1.0 : 0.0 };
    }
  }

  static double NowS() { return (double)Stopwatch.GetTimestamp() / Stopwatch.Frequency; }

  static int RealPad(int slot, int[] v) {
    XINPUT_STATE st;
    uint r = XInputGetState((uint)slot, out st);
    if (r == 0) {
      XINPUT_GAMEPAD g = st.Gamepad;
      v[0] = g.wButtons; v[1] = g.bLeftTrigger; v[2] = g.bRightTrigger;
      v[3] = g.sThumbLX; v[4] = g.sThumbLY; v[5] = g.sThumbRX; v[6] = g.sThumbRY;
    }
    return (int)r;
  }

  static IFable2AudioMeter AcquireMeter() {
    IFable2MMDeviceEnumerator en = (IFable2MMDeviceEnumerator)new Fable2MMDeviceEnumeratorCom();
    IFable2MMDevice dev;
    int hr = en.GetDefaultAudioEndpoint(0, 0, out dev);   // eRender, eConsole
    if (hr != 0 || dev == null) throw new InvalidOperationException("no default playback device (0x" + hr.ToString("X") + ")");
    Guid iid = new Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064");
    object o;
    hr = dev.Activate(ref iid, 23, IntPtr.Zero, out o);
    if (hr != 0 || o == null) throw new InvalidOperationException("no audio meter (0x" + hr.ToString("X") + ")");
    return (IFable2AudioMeter)o;
  }

  static void ReleaseMeter() {
    if (meterCom != null) { try { Marshal.ReleaseComObject(meterCom); } catch (Exception) { } meterCom = null; }
  }

  // Peak 0..1 of the default playback device; throws when it cannot be read.
  static double RealMeter() {
    double t = NowS();
    try {
      if (meterCom == null || t - meterAcquired > 5.0) { ReleaseMeter(); meterCom = AcquireMeter(); meterAcquired = t; }
      float pk;
      int hr = meterCom.GetPeakValue(out pk);
      if (hr != 0) throw new InvalidOperationException("GetPeakValue 0x" + hr.ToString("X"));
      return pk;
    } catch (Exception) { ReleaseMeter(); throw; }
  }

  static void SampleLoop() {
    Fable2SamplerCore c = core;
    while (!stopFlag) {
      double t = NowS();
      c.Step(t);
      lock (Lk) { heartbeat = t; }
      Thread.Sleep(10);
    }
    ReleaseMeter();
  }
}

// One sampling step of the pads and the audio meter with injectable readers, so the stamping
// (activity, connect or unplug, XInput error, audio level, audio error) is unit-testable.
public sealed class Fable2SamplerCore {
  // XInput result code (0 connected, 1167 no controller, else error); vals: buttons, lt, rt, lx, ly, rx, ry
  public delegate int PadReader(int slot, int[] vals);
  // Peak level 0..1 of the playback device; throws on failure
  public delegate double MeterReader();
  readonly PadReader pads;
  readonly MeterReader meter;
  readonly object lk = new object();
  int mask;
  double nextScan, nextAudio;
  double padActivity = -1, padChange = -1, audioStamp = -1, padErr = -1, audioErr = -1;

  public Fable2SamplerCore(PadReader p, MeterReader m) { pads = p; meter = m; }

  public void Step(double t) {
    int[] v = new int[7];
    try {
      int nm = mask;
      bool rescan = t >= nextScan;
      for (int i = 0; i < 4; i++) {
        if ((mask & (1 << i)) == 0 && !rescan) continue;
        int r = pads(i, v);
        if (r == 0) {
          nm |= (1 << i);
          if (Fable2IdleProbeV3.PadActive(v[0], v[1], v[2], v[3], v[4], v[5], v[6])) lock (lk) { padActivity = t; }
        } else if (r == 1167) {
          nm &= ~(1 << i);
        } else {
          lock (lk) { padErr = t; }
        }
      }
      if (rescan) nextScan = t + 1.0;
      if (nm != mask) { lock (lk) { mask = nm; padChange = t; } }
    } catch (Exception) { lock (lk) { padErr = t; } }
    if (t >= nextAudio) {
      nextAudio = t + 0.05;
      try {
        double pk = meter();
        if (pk > 0.005) lock (lk) { audioStamp = t; }
      } catch (Exception) { lock (lk) { audioErr = t; } }
    }
  }

  public double[] Latch() {
    lock (lk) { return new double[] { mask != 0 ? 1.0 : 0.0, padActivity, padChange, audioStamp, padErr, audioErr }; }
  }
}
"@
}
if ([Fable2IdleProbeV3]::ProbeVersion -ne 3) { throw "Fable2IdleProbeV3 has an unexpected version" }
if ($null -eq $script:Fable2Native) { $script:Fable2Native = New-Object Fable2IdleProbeV3 }

# ---- Pure decision logic (no Win32 calls; tested with synthetic values) ----

# One pad state is "activity": any button, a trigger above 30, a stick beyond 8000 from centre.
function Test-Fable2PadActive {
  param([Parameter(Mandatory)][int]$Buttons, [Parameter(Mandatory)][int]$LeftTrigger,
        [Parameter(Mandatory)][int]$RightTrigger, [Parameter(Mandatory)][int]$ThumbLX,
        [Parameter(Mandatory)][int]$ThumbLY, [Parameter(Mandatory)][int]$ThumbRX,
        [Parameter(Mandatory)][int]$ThumbRY)
  return [Fable2IdleProbeV3]::PadActive($Buttons, $LeftTrigger, $RightTrigger, $ThumbLX, $ThumbLY, $ThumbRX, $ThumbRY)
}

# True when two consecutive polls are more than LimitSeconds apart (or the clock went backwards):
# the time in between was not observed.
function Test-Fable2PollGap {
  param([Parameter(Mandatory)][double]$Previous, [Parameter(Mandatory)][double]$Current, [Parameter(Mandatory)][double]$LimitSeconds)
  $d = $Current - $Previous
  return ($d -gt $LimitSeconds -or $d -lt 0)
}

# Gate decision before a run. Present = any signal. A negative, non-finite or too long idle time
# is unknown. PadObserved: a pad is or was seen during the observation; PadQuietSeconds is the time
# since its last activity, connect or disconnect.
function Get-Fable2GateDecision {
  param([Parameter(Mandatory)][double]$IdleSeconds, [Parameter(Mandatory)][double]$UptimeSeconds,
        [Parameter(Mandatory)][int]$MinIdleSeconds, [Parameter(Mandatory)][bool]$PadObserved,
        [Parameter(Mandatory)][double]$PadQuietSeconds, [Parameter(Mandatory)][double]$AudioQuietSeconds,
        [Parameter(Mandatory)][int]$NotificationState, [Parameter(Mandatory)][bool]$Fable2Running,
        [Parameter(Mandatory)][bool]$ProbeFailed, [Parameter(Mandatory)][bool]$Session0,
        [Parameter(Mandatory)][bool]$ForcePresent)
  $signals = New-Object System.Collections.ArrayList
  $idleBad = ([double]::IsNaN($IdleSeconds) -or [double]::IsInfinity($IdleSeconds) -or $IdleSeconds -lt 0 `
              -or [double]::IsNaN($UptimeSeconds) -or $IdleSeconds -gt ($UptimeSeconds + 1.0))
  if ($ProbeFailed -or $Session0 -or $idleBad) { [void]$signals.Add("unknown") }
  if ($ForcePresent) { [void]$signals.Add("forced-present") }
  if (-not $idleBad -and $IdleSeconds -lt $MinIdleSeconds) { [void]$signals.Add("input") }
  if ($PadObserved -and $PadQuietSeconds -lt $MinIdleSeconds) { [void]$signals.Add("controller") }
  if ($AudioQuietSeconds -lt $MinIdleSeconds) { [void]$signals.Add("audio") }
  switch ($NotificationState) {
    { $_ -in 2, 3, 4, 7 } { [void]$signals.Add("fullscreen") }
    { $_ -in 1, 5, 6 } { }
    default { if (-not $signals.Contains("unknown")) { [void]$signals.Add("unknown") } }
  }
  if ($Fable2Running) { [void]$signals.Add("fable2-running") }
  return [pscustomobject]@{ Present = ($signals.Count -gt 0); Signals = @($signals) }
}

# One progress line of the gate loop. A gate that is still waiting has not seen a user: mostly it is
# still counting. So each open signal is worded as what has been observed: for the three with a
# required time "N of M s without ..." (the count restarts when the thing happens), for the others
# the state that holds now.
function Get-Fable2GateProgressText {
  param([Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Signals, [Parameter(Mandatory)][double]$IdleSeconds,
        [Parameter(Mandatory)][double]$PadQuietSeconds, [Parameter(Mandatory)][double]$AudioQuietSeconds,
        [Parameter(Mandatory)][int]$MinIdleSeconds, [Parameter(Mandatory)][double]$WaitedSeconds,
        [Parameter(Mandatory)][int]$MaxWaitSeconds)
  $whole = { param($v) [long][Math]::Floor([Math]::Max(0.0, [Math]::Min($v, 1e15))) }
  $parts = New-Object System.Collections.ArrayList
  foreach ($s in $Signals) {
    switch ($s) {
      "input" { [void]$parts.Add(("{0} of {1} s without keyboard or mouse input" -f (& $whole $IdleSeconds), $MinIdleSeconds)) }
      "controller" { [void]$parts.Add(("{0} of {1} s without controller activity" -f (& $whole $PadQuietSeconds), $MinIdleSeconds)) }
      "audio" { [void]$parts.Add(("{0} of {1} s without sound" -f (& $whole $AudioQuietSeconds), $MinIdleSeconds)) }
      "fullscreen" { [void]$parts.Add("another program is fullscreen or presenting now") }
      "fable2-running" { [void]$parts.Add("a fable_2.exe is running now") }
      "forced-present" { [void]$parts.Add("the test hook forces present") }
      "unknown" { [void]$parts.Add("the state is unknown now (a probe failed or gave no usable value)") }
      default { [void]$parts.Add("$s now") }
    }
  }
  return ("idle gate: observing: {0}; waited {1} of at most {2} s" -f ($parts -join ", "), (& $whole $WaitedSeconds), $MaxWaitSeconds)
}

# Watch decision during a run: the first presence signal's name, or $null. Input is any change
# (up or down) of the last-input tick from the baseline taken when the gate passed. Pad and audio
# stamps count when later than BaselineStamp (the gate's pass time on the sampler's clock). A
# fullscreen state while the game window is in front is the game's own and is ignored.
function Get-Fable2WatchSignal {
  param([Parameter(Mandatory)][long]$LastInputTick, [Parameter(Mandatory)][long]$BaselineInputTick,
        [Parameter(Mandatory)][double]$PadActivityStamp, [Parameter(Mandatory)][double]$PadChangeStamp,
        [Parameter(Mandatory)][double]$AudioStamp, [Parameter(Mandatory)][double]$BaselineStamp,
        [Parameter(Mandatory)][int]$NotificationState, [Parameter(Mandatory)][int]$ForegroundPid,
        [Parameter(Mandatory)][int]$GamePid, [Parameter(Mandatory)][bool]$ProbeFailed,
        [Parameter(Mandatory)][bool]$Session0, [Parameter(Mandatory)][bool]$ForcePresent)
  if ($ProbeFailed -or $Session0) { return "unknown" }
  if ($ForcePresent) { return "forced-present" }
  if ($LastInputTick -ne $BaselineInputTick) { return "input" }
  if ($PadActivityStamp -gt $BaselineStamp -or $PadChangeStamp -gt $BaselineStamp) { return "controller" }
  if ($AudioStamp -gt $BaselineStamp) { return "audio" }
  switch ($NotificationState) {
    { $_ -in 2, 3, 4, 7 } { if ($GamePid -eq 0 -or $ForegroundPid -ne $GamePid) { return "fullscreen" } }
    { $_ -in 1, 5, 6 } { }
    default { return "unknown" }
  }
  return $null
}

# ---- probing ----

# One reading of every signal through $Native (default: the real probe; tests pass a fake with the
# same methods). Each probe is isolated; a failing one sets ProbeFailed (fail closed).
function Get-Fable2Snapshot {
  param([switch]$CheckProcess, $Native = $null)
  if ($null -eq $Native) { $Native = $script:Fable2Native }
  $failed = @()
  $now = 0.0; $idle = -1.0; $tick = [long]0; $uptime = 0.0
  $padConn = $false; $padAct = -1.0; $padChg = -1.0; $aud = -1.0
  try { $now = [double]$Native.Now() } catch { $failed += "clock: $($_.Exception.Message)" }
  try {
    $li = $Native.LastInputInfo(); $idle = [double]$li[0]; $tick = [long]$li[1]
    if ($idle -lt 0) { $failed += "GetLastInputInfo failed" }
  } catch { $failed += "input: $($_.Exception.Message)"; $idle = -1.0 }
  try { $uptime = [double]$Native.UptimeSeconds() } catch { $failed += "uptime: $($_.Exception.Message)" }
  try {
    $l = $Native.Latch()
    $padConn = ($l[0] -eq 1); $padAct = [double]$l[1]; $padChg = [double]$l[2]; $aud = [double]$l[3]
    if ($l[7] -ne 1 -or $l[6] -lt 0 -or ($now - $l[6]) -gt 2.0) { $failed += "sampler thread not running or stalled" }
    if ($l[4] -ge 0 -and ($now - $l[4]) -lt 2.0) { $failed += "XInput probe failed" }
    if ($l[5] -ge 0 -and ($now - $l[5]) -lt 2.0) { $failed += "audio probe failed" }
  } catch { $failed += "sampler: $($_.Exception.Message)" }
  $ns = -1
  try { $ns = [int]$Native.NotificationState() } catch { $failed += "notification: $($_.Exception.Message)" }
  $fg = 0
  try { $fg = [int]$Native.ForegroundPid() } catch { $failed += "foreground: $($_.Exception.Message)" }
  $s0 = $true
  try { $s0 = [bool]$Native.Session0() } catch { $failed += "session: $($_.Exception.Message)" }
  $running = $false
  if ($CheckProcess) {
    try { $running = [bool]$Native.Fable2Running() } catch { $failed += "process list: $($_.Exception.Message)" }
  }
  return [pscustomobject]@{
    Now = $now; IdleSeconds = $idle; LastInputTick = $tick; UptimeSeconds = $uptime
    PadConnected = $padConn; PadActivityStamp = $padAct; PadChangeStamp = $padChg; AudioStamp = $aud
    NotificationState = $ns; ForegroundPid = $fg; Session0 = $s0
    Fable2Running = $running
    ForcePresent = ($env:FABLE2_IDLE_GATE_FORCE_PRESENT -eq "1")
    ProbeFailed = ($failed.Count -gt 0); Failures = $failed
  }
}

# Waits until the user has been away for MinIdleSeconds (and sound has been off for as long, a failing probe restarting that), at most
# MaxWaitSeconds; a pad observation needs MinIdleSeconds of that. Returns Passed, Signals and the
# last snapshot. -Probe, -Clock and -Sleep are injectable for tests; the 60 s floor is always in force.
function Wait-Fable2Away {
  param([int]$MinIdleSeconds, [int]$MaxWaitSeconds = 0, [scriptblock]$Probe = $null,
        [scriptblock]$Clock = $null, [scriptblock]$Sleep = $null)
  if ($MinIdleSeconds -lt 60) { throw "MinIdleSeconds must be at least 60 (got $MinIdleSeconds)" }
  if ($null -eq $Probe) {
    $script:Fable2Native.StartSampler()
    $Probe = { Get-Fable2Snapshot -CheckProcess }
  }
  if ($null -eq $Clock) { $Clock = { $script:Fable2Native.Now() } }
  if ($null -eq $Sleep) { $Sleep = { param($ms) Start-Sleep -Milliseconds $ms } }
  $t0 = [double](& $Clock)
  $quietSince = $t0; $audioSince = $t0; $lastLog = $t0
  $prevNow = $t0; $prevWall = [DateTime]::UtcNow
  while ($true) {
    $snap = & $Probe
    $now = [double]$snap.Now
    # Continuity: polls more than 5 s apart (by the loop's clock or the wall clock) mean the machine
    # slept or the process stalled; that time was not observed, so every observation restarts.
    $wall = [DateTime]::UtcNow
    $wallGap = ($wall - $prevWall).TotalSeconds
    if ((Test-Fable2PollGap -Previous $prevNow -Current $now -LimitSeconds 5) -or (Test-Fable2PollGap -Previous 0 -Current $wallGap -LimitSeconds 5)) {
      $quietSince = $now; $audioSince = $now
    }
    $prevNow = $now; $prevWall = $wall
    # A pad connecting, unplugged or active (latched between polls) restarts the observation.
    if ($snap.PadChangeStamp -gt $quietSince) { $quietSince = $snap.PadChangeStamp }
    if ($snap.PadActivityStamp -gt $quietSince) { $quietSince = $snap.PadActivityStamp }
    # Sound restarts the audio observation, and so does a failing probe.
    $audioSince = [Math]::Max($audioSince, $snap.AudioStamp)
    if ($snap.ProbeFailed) { $audioSince = $now }
    # Any other instantaneous signal restarts the pad observation too.
    $others = Get-Fable2GateDecision -IdleSeconds $snap.IdleSeconds -UptimeSeconds $snap.UptimeSeconds -MinIdleSeconds 0 `
      -PadObserved $false -PadQuietSeconds 0 -AudioQuietSeconds 1e9 -NotificationState $snap.NotificationState `
      -Fable2Running $snap.Fable2Running -ProbeFailed $snap.ProbeFailed -Session0 $snap.Session0 -ForcePresent $snap.ForcePresent
    if ($others.Present) { $quietSince = $now }
    $d = Get-Fable2GateDecision -IdleSeconds $snap.IdleSeconds -UptimeSeconds $snap.UptimeSeconds -MinIdleSeconds $MinIdleSeconds `
      -PadObserved ($snap.PadConnected -or $snap.PadChangeStamp -ge 0) -PadQuietSeconds ($now - $quietSince) `
      -AudioQuietSeconds ($now - $audioSince) -NotificationState $snap.NotificationState `
      -Fable2Running $snap.Fable2Running -ProbeFailed $snap.ProbeFailed -Session0 $snap.Session0 -ForcePresent $snap.ForcePresent
    if (-not $d.Present) { return [pscustomobject]@{ Passed = $true; Signals = @(); Snapshot = $snap } }
    if (($now - $t0) -ge $MaxWaitSeconds) { return [pscustomobject]@{ Passed = $false; Signals = $d.Signals; Snapshot = $snap } }
    if ($now - $lastLog -ge 60) {
      $lastLog = $now
      Write-Host (Get-Fable2GateProgressText -Signals @($d.Signals) -IdleSeconds $snap.IdleSeconds -PadQuietSeconds ($now - $quietSince) `
        -AudioQuietSeconds ($now - $audioSince) -MinIdleSeconds $MinIdleSeconds -WaitedSeconds ($now - $t0) -MaxWaitSeconds $MaxWaitSeconds)
    }
    & $Sleep ([int][Math]::Min(250, [Math]::Max(1, ($MaxWaitSeconds - ($now - $t0)) * 1000)))
  }
}

# ---- CLI ----

function Invoke-Fable2IdleCli {
  $modes = @($script:GateGiven, [bool]$Report, ($WatchSeconds -gt 0)) | Where-Object { $_ }
  if (@($modes).Count -eq 0) {
    Write-Host "usage: idle_check.ps1 -MinIdleSeconds N [-MaxWaitSeconds M]   gate: exit 0 only if the user has been away N s (N >= 60), 1 if present, 2 unknown or usage"
    Write-Host "       idle_check.ps1 -Report                                  print the signals, exit 0 (NOT a permission to run the game)"
    Write-Host "       idle_check.ps1 -WatchSeconds N                          watch N s, exit 1 at the first presence signal"
    return 2
  }
  if (@($modes).Count -gt 1) { Write-Host "usage error: -MinIdleSeconds, -Report and -WatchSeconds are separate modes; give one"; return 2 }
  $n = $script:Fable2Native
  try {
    if ($Report) {
      $n.StartSampler(); Start-Sleep -Milliseconds 300
      $s = Get-Fable2Snapshot -CheckProcess
      Write-Host ("idle: {0:N0} s since keyboard or mouse input" -f $s.IdleSeconds)
      Write-Host ("controller: connected={0}; last activity {1}; last connect/disconnect {2}" -f $s.PadConnected, $(if ($s.PadActivityStamp -ge 0) { "{0:N1} s ago" -f ($s.Now - $s.PadActivityStamp) } else { "never seen" }), $(if ($s.PadChangeStamp -ge 0) { "{0:N1} s ago" -f ($s.Now - $s.PadChangeStamp) } else { "never seen" }))
      Write-Host ("audio: last sound {0}" -f $(if ($s.AudioStamp -ge 0) { "{0:N1} s ago" -f ($s.Now - $s.AudioStamp) } else { "not heard" }))
      Write-Host ("notification state: {0} (1 away, 2 busy, 3 D3D fullscreen, 4 presentation, 5 accepts, 6 quiet, 7 app, -1 failed)" -f $s.NotificationState)
      Write-Host ("fable_2.exe running: {0}; session 0: {1}; forced present: {2}; probe failed: {3}" -f $s.Fable2Running, $s.Session0, $s.ForcePresent, $s.ProbeFailed)
      foreach ($f in $s.Failures) { Write-Host "probe failure: $f" }
      Write-Host "NOTE: this report is for humans and is not a permission to run the game. Agents run the game only through tools/drive_game.ps1, which gates and watches by itself."
      return 0
    }
    if ($WatchSeconds -gt 0) {
      $n.StartSampler(); Start-Sleep -Milliseconds 300
      $base = Get-Fable2Snapshot
      $sw = [Diagnostics.Stopwatch]::StartNew()
      while ($sw.Elapsed.TotalSeconds -lt $WatchSeconds) {
        $s = Get-Fable2Snapshot
        $sig = Get-Fable2WatchSignal -LastInputTick $s.LastInputTick -BaselineInputTick $base.LastInputTick `
          -PadActivityStamp $s.PadActivityStamp -PadChangeStamp $s.PadChangeStamp -AudioStamp $s.AudioStamp `
          -BaselineStamp $base.Now -NotificationState $s.NotificationState -ForegroundPid $s.ForegroundPid -GamePid 0 `
          -ProbeFailed ($s.ProbeFailed -or $base.ProbeFailed) -Session0 $s.Session0 -ForcePresent $s.ForcePresent
        if ($sig) { Write-Host ("presence signal: {0} at {1:N1} s" -f $sig, $sw.Elapsed.TotalSeconds); return 1 }
        Start-Sleep -Milliseconds 100
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
  } finally {
    try { $n.StopSampler() } catch { }
  }
}

if ($MyInvocation.InvocationName -ne '.') { exit (Invoke-Fable2IdleCli) }
