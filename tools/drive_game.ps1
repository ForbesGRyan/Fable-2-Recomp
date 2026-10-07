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
#    no active, connecting or unplugged XInput pad, no sound on the default playback device,
#    no other fullscreen or presentation program and no fable_2.exe already running, each for 300 s
#    of observation (fixed: there is no parameter, environment variable or option that lowers it),
#    waiting up to -IdleWaitSeconds (default 1800). Every gate therefore takes at least 300 s;
#    an -IdleWaitSeconds below 300 always refuses. If the gate does not pass, or fails for any
#    reason, the game is not launched: message "REFUSED: ..." and exit code 10.
#  - The game is created suspended, assigned to a Win32 job object (kill on close) held by this
#    script, and only then resumed, so it never runs outside the job and the OS kills it (and its
#    children) if this script dies for any reason. If that fails the game is killed at once
#    (exit 12). Stopping the game also terminates the job. The game runs with
#    SDL_AUDIO_DRIVER=fable-test-unavailable-backend (the tools\StartWithoutAudio.cmd mechanism) so
#    its own sound does not trip the audio signal; an -Env value for SDL_AUDIO_DRIVER wins.
#  - Dead-man switch: if the main loop has not polled the watch for more than 5 s while the game
#    runs (for example a hung screenshot call), a background thread terminates the job; the run
#    ends "RUN ABORTED: dead-man switch ..." (exit 12).
#    The main thread checks itself too: a gap of more than 5 s between polls, or one poll that
#    takes more than 5 s, aborts the run; a trip of the thread after the last poll is checked in
#    the verdict; and a run that lasted more than 5 s longer than -Total (or the last screenshot
#    time) is "RUN ABORTED: run overran (...)", never clean.
#  - Continuity: in the gate, two polls more than 5 s apart (suspend, stall) restart every
#    observation; in the watch a jump of the clock between polls is "RUN DISTURBED: watch-gap".
#  - -ExeDir outside test mode must be one of the folders in $AllowedExeDirs (default: this
#    repository's out\build\win-amd64-release), else exit 2. The "started pid" line (printed and
#    logged) names the full path of the executable. Verdict and mode variables are constants.
#  - Test-suite guard: with FABLE2_DRIVER_NO_REAL_GATE=1 in the environment (set by
#    tests\test_idle_gate.py) the driver refuses (exit 2) to evaluate the real gate or to launch,
#    except with the forced-present hook, the labelled harness, or -UserPresent in a folder
#    without fable_2.exe. It can only restrict.
#  - From launch to the end it polls the same signals (about every 100 ms plus the probe time,
#    between its timing and screenshot steps; the pad and audio latches are sampled every 10 and
#    50 ms by a background thread; the game's own in-process autoplay and the PrintWindow
#    screenshots are not OS input and do not trip it; the running-game check is gate-only). On a
#    signal (input, controller, audio, fullscreen, unknown, forced-present; a watch error is
#    "unknown") it kills the game at once, prints "RUN DISTURBED: <signal> at <seconds> s", moves
#    the run's screenshots into <ShotDir>\disturbed_<timestamp>\, writes that line, a warning and
#    the moved files to the marker file RUN_DISTURBED.txt (in -ShotDir when -Shots is given, else
#    in <ExeDir>\logs) and exits 11. A watch error counts as a disturbance (fail closed).
#  - A run with no disturbance ends with the last output line "RUN CLEAN: no user input for
#    <seconds> s" (seconds from launch to close) and exit 0, printed only after the game is
#    confirmed closed. An error during the run closes the game and ends with
#    "RUN ABORTED: <message>" and exit 12. A game that exits before -Total prints
#    "GAME EXITED EARLY at <seconds> s" and exits 13 (never RUN CLEAN). A stale marker from an
#    earlier run is renamed to RUN_DISTURBED.<timestamp>.previous.txt before launch.
#  - Every run appends lines to <ExeDir>\logs\drive_game_runs.log: "started pid <n>" at launch and
#    at the end time, exit code, gate result and signals ("USER PRESENT OVERRIDE" when
#    -UserPresent was used).
#  Exit codes: 0 clean (or unchecked with -UserPresent), 2 refused option combination (also any
#  unknown or positional argument), 10 gate refused (nothing launched), 11 disturbed, 12 aborted,
#  13 game exited early; 1 is a script error before launch (bad plan, ambiguous abbreviation).
#  -UserPresent (spelled in full; abbreviations are rejected): ONLY for the person at the keyboard
#  running this script themselves. It skips the gate and the watch and prints a prominent line
#  saying so. AGENTS MUST NEVER PASS IT. -KeyboardInput (the user driving with real key presses)
#  requires -UserPresent.
#  -TestHarness/-TestExe/-TestExeArgs: self-test of this script's run path (tests\test_idle_gate.py).
#  They replace the Win32 probes by a scripted probe and the game by a stand-in process. Such a run
#  is labelled everywhere: the banner "*** TEST MODE ...", every verdict line (RUN CLEAN, RUN
#  DISTURBED, RUN ABORTED, GAME EXITED EARLY) starts with "TEST HARNESS " and so does every line
#  of the run log. They are refused (exit 2) unless -TestHarness and -TestExe are given together
#  with an explicit -ExeDir that contains no fable_2.exe, never together with -UserPresent,
#  -KeyboardInput, -GameArgs or -Env, and refused when -TestExe is named like the game (any name
#  containing "fable"), is the built fable_2.exe (same path or same bytes), or sits in a folder
#  with rexruntime.dll or fable_2.toml. This is a guard against accidents, not against someone who
#  deliberately builds a disguised copy of the game: the rule that agents start the game only
#  through this script without test parameters is policy.
#
# Examples (from the repo root, PowerShell):
#   .\tools\drive_game.ps1 -Total 120
#   .\tools\drive_game.ps1 -Total 150 -Env @{FABLE2_NATIVE_DISCOVERY="300"; FABLE2_NATIVE_DISCOVERY_DELAY="50"}
#   .\tools\drive_game.ps1 -GameArgs "--native_render_log_vertex_bindings=true" -Shots "50,80"
[CmdletBinding(PositionalBinding = $false)]
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
  [string]$TestExeArgs = "",                 # self-test only: arguments of the stand-in, separated by |
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest = @()   # catches positional and unknown arguments: refused
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "cmdline_quote.ps1")
$Root = Split-Path -Parent $PSScriptRoot
# Paths are resolved once, against the caller's current location (not the process directory), and used everywhere.
function Resolve-AbsolutePath([string]$Path) {
  if (-not $Path) { return $Path }
  if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $PWD.ProviderPath $Path }
  return [IO.Path]::GetFullPath($Path)
}
$exeDirGiven = $PSBoundParameters.ContainsKey('ExeDir')
if ($HoldMs -le 0) { $HoldMs = if ($KeyboardInput) { 50 } else { 100 } }
if (-not $ExeDir) { $ExeDir = Join-Path $Root "out\build\win-amd64-release" }
$ExeDir = Resolve-AbsolutePath $ExeDir
if (-not $ShotDir) { $ShotDir = Join-Path $ExeDir "logs\shots" }
$ShotDir = Resolve-AbsolutePath $ShotDir
$TestExe = Resolve-AbsolutePath $TestExe
$TestHarness = Resolve-AbsolutePath $TestHarness
# The only folders (relative to the repository root) whose fable_2.exe this script will start outside test mode.
$AllowedExeDirs = @("out\build\win-amd64-release")   # allow-list: add a path here on purpose, never at the command line
# Constants: harness code running inside the driver cannot blank or change them.
New-Variable -Name RequiredAbsenceSeconds -Value 300 -Option Constant   # the idle requirement; nothing lowers it
New-Variable -Name DeadManSeconds -Value 5 -Option Constant             # the main loop must poll the watch at least this often
New-Variable -Name testMode -Value ([bool]($TestHarness -or $TestExe -or $TestExeArgs)) -Option Constant
New-Variable -Name Label -Value $(if ($testMode) { "TEST HARNESS " } else { "" }) -Option Constant   # prefix of every verdict and log line of a harness run
$script:job = $null; $script:hProc = $null; $script:p = $null
$script:GateText = "not evaluated"
$script:sampledNative = $null

# Appends one line to the durable run log (harness runs: labelled, and never into a default folder).
function Add-RunLog([string]$Text) {
  if ($testMode -and -not $exeDirGiven) { return }
  $Text = ($Text -replace '[\x00-\x1f\x7f]+', ' | ')   # one line per run event, whatever the message holds
  try {
    $dir = Join-Path $ExeDir "logs"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Content -LiteralPath (Join-Path $dir "drive_game_runs.log") -Value ("{0}{1:yyyy-MM-dd HH:mm:ss}  {2}" -f $Label, (Get-Date), $Text)
  } catch { }
}
# Closing the job handle kills whatever is still in the job (kill on close), so nothing survives the call even when
# the caller is a session that keeps running; the process and thread handles are closed too.
function Close-RunHandles {
  if ($script:p) { try { $script:p.Dispose() } catch { } }
  if ($null -ne $script:hProc -and $script:hProc -ne [IntPtr]::Zero) { try { [DriveGameNative]::Close($script:hProc) } catch { }; $script:hProc = $null }
  if ($null -ne $script:job -and $script:job -ne [IntPtr]::Zero) { try { [DriveGameNative]::Close($script:job) } catch { }; $script:job = $null }
}
# Ends the script: the final line in the run log, the sampler stopped, the handles closed, then exit.
function Exit-Run([int]$Code) {
  Add-RunLog ("exit={0}  gate={1}" -f $Code, $script:GateText)
  try { if ($script:sampledNative) { $script:sampledNative.StopSampler() } } catch { }
  Close-RunHandles
  exit $Code
}
if ($UserPresentAbbreviationGuard) {
  Write-Output "REFUSED: -UserPresentAbbreviationGuard is not an option. Nothing launched."
  Exit-Run 2
}
if ($Rest.Count) {
  Write-Output "REFUSED: unknown arguments ($($Rest -join ' ')); positional and unknown arguments are never ignored silently. Nothing launched."
  Exit-Run 2
}
if ($KeyboardInput -and -not $UserPresent) {
  Write-Output "REFUSED: -KeyboardInput means the user is driving; it requires -UserPresent. Nothing launched."
  Exit-Run 2
}
if (-not $testMode) {
  $allowedFull = @($AllowedExeDirs | ForEach-Object { (Resolve-AbsolutePath $(if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $Root $_ })).TrimEnd('\') })
  if ($allowedFull -notcontains $ExeDir.TrimEnd('\')) {
    Write-Output "REFUSED: -ExeDir '$ExeDir' is not an allowed build output directory (allowed: $($allowedFull -join '; ')). Nothing launched."
    Exit-Run 2
  }
  # Test-suite guard: tests/test_idle_gate.py sets FABLE2_DRIVER_NO_REAL_GATE=1 so that no test can ever reach the real gate
  # or a real launch, whatever is mutated: only the forced-present hook (the gate can only refuse), or -UserPresent in a
  # folder that has no fable_2.exe, is accepted.
  if ($env:FABLE2_DRIVER_NO_REAL_GATE -eq "1") {
    $hookOk = ($env:FABLE2_IDLE_GATE_FORCE_PRESENT -eq "1")
    $emptyOk = ($UserPresent -and -not (Test-Path -LiteralPath (Join-Path $ExeDir "fable_2.exe")))
    if (-not ($hookOk -or $emptyOk)) {
      Write-Output "REFUSED: test-suite guard (FABLE2_DRIVER_NO_REAL_GATE=1): the real gate and a real launch are not allowed in this process. Nothing launched."
      Exit-Run 2
    }
  }
}
if ($testMode) {
  $why = $null
  if (-not ($TestHarness -and $TestExe)) { $why = "-TestHarness and -TestExe must be given together" }
  elseif ($UserPresent -or $KeyboardInput) { $why = "test parameters cannot be combined with -UserPresent or -KeyboardInput" }
  elseif (-not $exeDirGiven) { $why = "test parameters require an explicit -ExeDir" }
  elseif ($GameArgs.Count -or $PSBoundParameters.ContainsKey('Env')) { $why = "test mode refuses -GameArgs and -Env" }
  elseif ([IO.Path]::GetFileName($TestExe) -match 'fable') { $why = "the stand-in must not be named like the game" }
  elseif (-not (Test-Path -LiteralPath $TestExe -PathType Leaf)) { $why = "the stand-in does not exist" }
  elseif (-not (Test-Path -LiteralPath $TestHarness -PathType Leaf)) { $why = "the harness script does not exist" }
  elseif (Test-Path -LiteralPath (Join-Path $ExeDir "fable_2.exe")) { $why = "-ExeDir contains fable_2.exe" }
  else {
    $standIn = (Resolve-Path -LiteralPath $TestExe).Path
    $real = Join-Path $Root "out\build\win-amd64-release\fable_2.exe"
    if (Test-Path -LiteralPath $real -PathType Leaf) {
      if ((Resolve-Path -LiteralPath $real).Path -ieq $standIn) { $why = "the stand-in is the game" }
      elseif ((Get-Item -LiteralPath $real).Length -eq (Get-Item -LiteralPath $standIn).Length -and
              (Get-FileHash -LiteralPath $real).Hash -eq (Get-FileHash -LiteralPath $standIn).Hash) { $why = "the stand-in has the game's bytes" }
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
using System; using System.Diagnostics; using System.Runtime.InteropServices; using System.Text; using System.Threading;
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
  [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job, uint code);
  [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct STARTUPINFO {
    public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
    public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError; }
  [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
  static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  // Creates the process SUSPENDED (CREATE_SUSPENDED | CREATE_NO_WINDOW): it executes nothing until Resume,
  // so the caller can assign it to the job first. Returns the pid, or 0 on failure.
  public static int StartSuspended(string exe, string args, string cwd, out IntPtr hProcess, out IntPtr hThread) {
    STARTUPINFO si = new STARTUPINFO(); si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
    PROCESS_INFORMATION pi;
    StringBuilder cmd = new StringBuilder("\"" + exe + "\"" + (args.Length > 0 ? " " + args : ""));
    hProcess = IntPtr.Zero; hThread = IntPtr.Zero;
    if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, 0x00000004 | 0x08000000, IntPtr.Zero, cwd, ref si, out pi)) return 0;
    hProcess = pi.hProcess; hThread = pi.hThread;
    return pi.dwProcessId;
  }
  public static void Close(IntPtr h) { CloseHandle(h); }
  public static bool Resume(IntPtr hThread) { return ResumeThread(hThread) != 0xFFFFFFFF; }
  // Kills a process that was created suspended and never ran.
  public static void Abort(IntPtr hProcess, IntPtr hThread) { TerminateProcess(hProcess, 1); CloseHandle(hThread); CloseHandle(hProcess); }
  public static bool TerminateJob(IntPtr job) { return job != IntPtr.Zero && TerminateJobObject(job, 1); }

  // Dead-man switch: terminates the job when PetDeadMan has not been called for more than the given seconds.
  static Thread dmThread; static volatile bool dmStop; static volatile bool dmTripped; static long dmLast;
  public static void StartDeadMan(IntPtr job, double seconds) {
    dmStop = false; dmTripped = false; dmLast = Stopwatch.GetTimestamp();
    long limit = (long)(seconds * Stopwatch.Frequency);
    dmThread = new Thread(delegate() {
      while (!dmStop) {
        if (Stopwatch.GetTimestamp() - Interlocked.Read(ref dmLast) > limit) { TerminateJobObject(job, 1); dmTripped = true; return; }
        Thread.Sleep(100);
      }
    });
    dmThread.IsBackground = true;
    dmThread.Start();
  }
  public static void PetDeadMan() { Interlocked.Exchange(ref dmLast, Stopwatch.GetTimestamp()); }
  public static bool IsDeadManTripped() { return dmTripped; }
  public static void StopDeadMan() { dmStop = true; }
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
    $probe = { Get-Fable2Snapshot -CheckProcess -Native $native }
    $clock = { $native.Now() }
    $sleepSb = if ($harness) { $harness.Sleep } else { $null }
    $gate = Wait-Fable2Away -MinIdleSeconds $RequiredAbsenceSeconds -MaxWaitSeconds $IdleWaitSeconds -Probe $probe -Clock $clock -Sleep $sleepSb
    if ($gate.Passed) {
      $gatePassed = $true
      $baselineTick = $gate.Snapshot.LastInputTick
      $baselineStamp = $gate.Snapshot.Now
      $script:GateText = "passed (no user input for at least $RequiredAbsenceSeconds s)"
    } else {
      $script:GateText = "refused: $($gate.Signals -join ',')"
      Write-Output "REFUSED: user present or state unknown ($($gate.Signals -join ', ')) after waiting up to $IdleWaitSeconds s. Every gate needs $RequiredAbsenceSeconds s of observed absence: use -IdleWaitSeconds $RequiredAbsenceSeconds or more. Nothing launched."
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
$script:job = if ($harness -and $harness.FailJobCreate) { [IntPtr]::Zero } else { [DriveGameNative]::CreateKillOnCloseJob() }
if ($script:job -eq [IntPtr]::Zero) {
  $script:GateText += "; ABORTED"
  Write-Output "${Label}RUN ABORTED: could not create the kill-on-close job object. Nothing launched."
  Exit-Run 12
}

$script:disturbAt = 0.0
$script:lastPollAt = -1.0; $script:lastNativeStamp = $null; $script:lastWall = $null; $script:stalledBetween = $false
$script:shotFiles = New-Object System.Collections.ArrayList
$testCtx = @{ ShotDir = $(if ($Shots) { $ShotDir } else { $null }); ShotFiles = $script:shotFiles; shotMade = $false }
$sw = [Diagnostics.Stopwatch]::StartNew()

# Kills the game through the process handle and waits for it to be gone.
function Stop-Game {
  if ($harness -and $harness.NoKill) { return }   # test mode only: simulates a game that cannot be killed
  $wasAlive = $false
  try { if ($script:p) { $wasAlive = -not $script:p.HasExited } } catch { }
  try { [void][DriveGameNative]::TerminateJob($script:job) } catch { }   # the game and its children, even before the process object exists
  if (-not $script:p) { return }
  try {
    if (-not $script:p.HasExited) { $script:p.Kill() }
    if ($wasAlive) { "closed game at $([int]$sw.Elapsed.TotalSeconds) s" }
  } catch {
    try { Stop-Process -Id $script:p.Id -Force } catch { }
  }
  try { [void]$script:p.WaitForExit(5000) } catch { }
}

# Kills the game and its children at once.
function Stop-RunNow { try { [void][DriveGameNative]::TerminateJob($script:job); if ($script:p -and -not $script:p.HasExited) { $script:p.Kill() } } catch { } }

# Polls the presence signals; on one it kills the game at once and throws. Also the main thread's own dead-man check:
# a gap between polls, or a poll that itself takes longer than the limit, aborts the run. No signals with -UserPresent.
function Invoke-WatchPoll {
  if ([DriveGameNative]::IsDeadManTripped()) {
    throw "dead-man switch: the driver did not poll the watch for more than $DeadManSeconds s; the game was terminated"
  }
  $pollStart = $sw.Elapsed.TotalSeconds
  if ($script:lastPollAt -ge 0 -and ($pollStart - $script:lastPollAt) -gt $DeadManSeconds) {
    Stop-RunNow
    throw ("dead-man switch: the driver did not poll the watch for {0:N1} s (limit {1} s); the game was terminated" -f ($pollStart - $script:lastPollAt), $DeadManSeconds)
  }
  [DriveGameNative]::PetDeadMan()
  if ($harness -and $harness.Tick) { & $harness.Tick $sw.Elapsed.TotalSeconds $testCtx }
  if (($sw.Elapsed.TotalSeconds - $pollStart) -gt $DeadManSeconds) {
    Stop-RunNow
    throw ("dead-man switch: one watch poll took {0:N1} s (limit {1} s); the game was terminated" -f ($sw.Elapsed.TotalSeconds - $pollStart), $DeadManSeconds)
  }
  if (-not $UserPresent) {
    $sig = $null
    try {
      $s = Get-Fable2Snapshot -Native $native
      $wall = [DateTime]::UtcNow
      # Continuity: a jump of the clock (machine suspended) between two polls is a disturbance, never absence.
      $gap = $false
      if ($null -ne $script:lastNativeStamp) {
        $gap = (Test-Fable2PollGap -Previous $script:lastNativeStamp -Current $s.Now -LimitSeconds $DeadManSeconds) -or
               (Test-Fable2PollGap -Previous 0 -Current ($wall - $script:lastWall).TotalSeconds -LimitSeconds $DeadManSeconds)
      }
      $script:lastNativeStamp = $s.Now; $script:lastWall = $wall
      $gamePid = if ($script:p) { $script:p.Id } else { 0 }
      $sig = Get-Fable2WatchSignal -LastInputTick $s.LastInputTick -BaselineInputTick $baselineTick `
        -PadActivityStamp $s.PadActivityStamp -PadChangeStamp $s.PadChangeStamp -AudioStamp $s.AudioStamp `
        -BaselineStamp $baselineStamp -NotificationState $s.NotificationState -ForegroundPid $s.ForegroundPid `
        -GamePid $gamePid -ProbeFailed $s.ProbeFailed -Session0 $s.Session0 -ForcePresent $s.ForcePresent
      if ($gap) { $sig = "watch-gap" }
    } catch { $sig = "watch-error" }
    if ($sig) {
      $script:disturbAt = $sw.Elapsed.TotalSeconds
      Stop-RunNow
      throw "FABLE2-DISTURBED:$sig"
    }
  }
  $script:lastPollAt = $sw.Elapsed.TotalSeconds
}
# The pause between two polls (test mode only: a harness can stall it once, to prove the main thread notices a gap).
function Pause-Loop {
  Start-Sleep -Milliseconds 100
  if ($harness -and $harness.StallBetweenPollsMs -and -not $script:stalledBetween -and $sw.Elapsed.TotalSeconds -ge 1) {
    $script:stalledBetween = $true
    Start-Sleep -Milliseconds $harness.StallBetweenPollsMs
  }
}
function Wait-Until([double]$seconds) {
  while ($sw.Elapsed.TotalSeconds -lt $seconds) {
    if ($script:p -and $script:p.HasExited) { return }
    Invoke-WatchPoll
    Pause-Loop
  }
}

# Moves this run's screenshots into <ShotDir>\<prefix>_<timestamp>\ so a later run in the same folder cannot be mistaken for them.
function Move-RunShots([string]$Prefix) {
  $moved = @()
  try {
    if ($script:shotFiles.Count) {
      $sub = Join-Path $ShotDir ("{0}_{1}" -f $Prefix, (Get-Date -Format "yyyyMMdd_HHmmss"))
      New-Item -ItemType Directory -Force $sub | Out-Null
      foreach ($f in $script:shotFiles) {
        if (Test-Path -LiteralPath $f) { Move-Item -LiteralPath $f -Destination $sub -Force; $moved += (Join-Path $sub (Split-Path -Leaf $f)) }
      }
    }
  } catch { Write-Host "WARNING: could not move the run's screenshots ($($_.Exception.Message))" }
  return $moved
}

$saved = @{}
$disturbSignal = $null; $abortMessage = $null; $earlyExit = $false; $runSeconds = 0.0
try {
  foreach ($name in $Env.Keys) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, [string]$Env[$name])
  }
  $argList = @("--fullscreen=false") + $GameArgs
  $exePath = Join-Path $ExeDir "fable_2.exe"
  $workDir = $ExeDir
  if ($testMode) { $exePath = $TestExe; $workDir = Split-Path -Parent $TestExe; $argList = @($TestExeArgs.Split("|") | Where-Object { $_ }) }
  # The only place the game is started: created suspended, assigned to the job, then resumed.
  if (-not ($UserPresent -or $gatePassed)) { throw "internal error: launch without a passed idle gate" }
  $cmdArgs = (@($argList | ForEach-Object { ConvertTo-CommandLineArgument $_ })) -join ' '
  $hProc = [IntPtr]::Zero; $hThr = [IntPtr]::Zero
  $newPid = [DriveGameNative]::StartSuspended($exePath, $cmdArgs, $workDir, [ref]$hProc, [ref]$hThr)
  if ($newPid -eq 0) { throw "could not start $exePath (Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
  if ($harness -and $harness.AssignDelayMs) { Start-Sleep -Milliseconds $harness.AssignDelayMs }   # test mode only: a game that was not suspended would run now
  $assigned = (-not ($harness -and $harness.FailJobAssign)) -and [DriveGameNative]::AssignToJob($script:job, $hProc)
  if (-not $assigned) {
    [DriveGameNative]::Abort($hProc, $hThr)
    throw "could not assign the game to the kill-on-close job object (the game was killed before it ran)"
  }
  # The dead-man switch is armed before the game runs a single instruction.
  if (-not ($harness -and $harness.NoDeadManThread)) { [DriveGameNative]::StartDeadMan($script:job, $DeadManSeconds) }   # NoDeadManThread: test mode only, to test the main thread's own checks
  if (-not [DriveGameNative]::Resume($hThr)) { [DriveGameNative]::Abort($hProc, $hThr); throw "could not resume the game" }
  $script:hProc = $hProc
  [DriveGameNative]::Close($hThr)
  $script:p = [Diagnostics.Process]::GetProcessById($newPid)
  $p = $script:p
  foreach ($name in $Env.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
  Add-RunLog "started pid $($p.Id) exe=$exePath"   # logged first: a pipeline may stop the script at the next line
  "started pid $($p.Id): $exePath $($argList -join ' ')"
  $testCtx.Pid = $p.Id

  $sw.Restart()
  $script:lastPollAt = 0.0
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
  while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt $Total) { Invoke-WatchPoll; Pause-Loop }
  if ($p.HasExited -and $sw.Elapsed.TotalSeconds -lt $Total) {
    $earlyExit = $true
    "${Label}GAME EXITED EARLY at $([int]$sw.Elapsed.TotalSeconds) s"
  }
  Invoke-WatchPoll
  if ($harness -and $harness.StallAfterLoopMs) { Start-Sleep -Milliseconds $harness.StallAfterLoopMs }   # test mode only: a stall after the last poll
  $runSeconds = $sw.Elapsed.TotalSeconds
} catch {
  if ($_.Exception.Message -like "FABLE2-DISTURBED:*") { $disturbSignal = $_.Exception.Message.Substring("FABLE2-DISTURBED:".Length) }
  else { $abortMessage = $_.Exception.Message }
} finally {
  # Every exit path closes the game; the environment is restored.
  [DriveGameNative]::StopDeadMan()
  Stop-Game
  if ($harness -and $harness.HoldAfterKillMs) { Start-Sleep -Milliseconds $harness.HoldAfterKillMs }   # test mode only: lets a test look at the machine while the driver is still alive
  foreach ($name in $saved.Keys) { try { [Environment]::SetEnvironmentVariable($name, $saved[$name]) } catch { } }
}

$deadManTripped = [DriveGameNative]::IsDeadManTripped()
$gameClosed = $true
if ($script:p) { try { $gameClosed = $script:p.HasExited } catch { $gameClosed = $false } }
$lastEvent = if ($events) { [double](($events | Measure-Object -Property t -Maximum).Maximum) } else { 0.0 }
$expectedSeconds = [Math]::Max([double]$Total, $lastEvent)

if ($disturbSignal) {
  $line = "${Label}RUN DISTURBED: $disturbSignal at {0:N1} s" -f $script:disturbAt
  $script:GateText += "; DISTURBED: $disturbSignal"
  $moved = @(Move-RunShots "disturbed")
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
# A dead-man trip is an abort whatever else happened, and a run that lasted far longer than asked is never clean.
if (-not $abortMessage -and $deadManTripped) { $abortMessage = "dead-man switch: the driver stopped polling the watch for more than $DeadManSeconds s and the game was terminated" }
if (-not $abortMessage -and $gameClosed -and -not $earlyExit -and $runSeconds -gt ($expectedSeconds + 5)) {
  $abortMessage = "run overran ({0} s for a {1} s run)" -f [int]$runSeconds, $Total
}
if ($abortMessage) {
  $script:GateText += "; ABORTED"
  $moved = @(Move-RunShots "aborted")
  if ($moved.Count) { "screenshots of the aborted run moved to $(Split-Path -Parent $moved[0]): do not use them" }
  Write-Output "${Label}RUN ABORTED: $abortMessage"
  Exit-Run 12
}
if (-not $gameClosed) {
  $script:GateText += "; ABORTED"
  Write-Output "${Label}RUN ABORTED: the game is still running after the kill"
  Exit-Run 12
}
if ($earlyExit) {
  $script:GateText += "; EARLY EXIT"
  $moved = @(Move-RunShots "early_exit")
  if ($moved.Count) { "screenshots of the early-exit run moved to $(Split-Path -Parent $moved[0]): do not use them" }
  Exit-Run 13
}
if ($UserPresent) { Write-Output "RUN UNCHECKED: -UserPresent given, no idle gate and no watch" }
else { Write-Output ("${Label}RUN CLEAN: no user input for {0} s" -f [int]$sw.Elapsed.TotalSeconds) }
Exit-Run 0
