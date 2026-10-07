# Reports how long the machine has had no keyboard or mouse input, or waits until it
# has been idle long enough. Read-only: it sends no input and touches no window.
#
# Game runs made by agents are allowed only while the user is away (user decision,
# 2026-10-07): check before a run that the machine has been idle, and after the run that
# it stayed idle for the whole run. The game's in-process autoplay and the screenshots
# taken by drive_game.ps1 (PrintWindow) are not user input and do not reset the idle time.
#
# Examples (from the repo root, PowerShell):
#   .\tools\idle_check.ps1                                  # prints the idle seconds, exit 0
#   .\tools\idle_check.ps1 -MinIdleSeconds 300              # exit 0 if idle >= 300 s, else 1
#   .\tools\idle_check.ps1 -MinIdleSeconds 300 -MaxWaitSeconds 1800   # wait up to 30 min for it
param(
  [int]$MinIdleSeconds = 0,     # required idle time; 0 = only report
  [int]$MaxWaitSeconds = 0,     # how long to wait for the machine to become idle enough
  [int]$PollSeconds = 5
)
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Fable2IdleProbe {
  [StructLayout(LayoutKind.Sequential)]
  struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")]
  static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
  public static double IdleSeconds() {
    LASTINPUTINFO info = new LASTINPUTINFO();
    info.cbSize = (uint)Marshal.SizeOf(info);
    if (!GetLastInputInfo(ref info)) return -1.0;
    return unchecked((uint)Environment.TickCount - info.dwTime) / 1000.0;
  }
}
"@

function Get-Idle { [Fable2IdleProbe]::IdleSeconds() }

$idle = Get-Idle
if ($idle -lt 0) { Write-Output "idle: unknown (GetLastInputInfo failed)"; exit 2 }
if ($MinIdleSeconds -le 0) { Write-Output ("idle: {0:N0} s" -f $idle); exit 0 }

$deadline = (Get-Date).AddSeconds($MaxWaitSeconds)
while ($idle -lt $MinIdleSeconds -and (Get-Date) -lt $deadline) {
  Start-Sleep -Seconds $PollSeconds
  $idle = Get-Idle
}
if ($idle -ge $MinIdleSeconds) {
  Write-Output ("idle: {0:N0} s (>= {1} s): ok to run" -f $idle, $MinIdleSeconds)
  exit 0
}
Write-Output ("idle: {0:N0} s (< {1} s): the user is at the machine, do not run" -f $idle, $MinIdleSeconds)
exit 1
