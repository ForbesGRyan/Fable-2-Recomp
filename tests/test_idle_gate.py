"""Tests for tools/idle_check.ps1 (presence gate) and the gate, watch and kill paths of
tools/drive_game.ps1.

The real game is never launched. Driver tests that get past the gate use the driver's test
harness (-TestHarness/-TestExe/-TestExeArgs): a scripted probe and clock replace the Win32
probes and a harmless stand-in process (pwsh sleeping) replaces fable_2.exe. All other driver
tests run with -ExeDir pointing at an empty temporary folder. FABLE2_TOOLS_DIR points the
tests at a scratch copy of the scripts (used to run mutations of the enforcing code).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suite-level guard: with this set the driver refuses to evaluate the real gate (it allows only the forced-present hook,
# the labelled harness, or -UserPresent in a folder without a fable_2.exe), so no test can reach a real launch.
os.environ["FABLE2_DRIVER_NO_REAL_GATE"] = "1"
TOOLS = Path(os.environ.get("FABLE2_TOOLS_DIR") or ROOT / "tools")
IDLE = TOOLS / "idle_check.ps1"
DRIVE = TOOLS / "drive_game.ps1"
PLAN = TOOLS / "autoplay_plan.ps1"
PWSH = shutil.which("pwsh") or shutil.which("powershell")


def run_ps(args, env_extra=None, timeout=180):
    env = dict(os.environ)
    env.pop("FABLE2_IDLE_GATE_FORCE_PRESENT", None)
    env.update(env_extra or {})
    return subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", *map(str, args)],
                          capture_output=True, text=True, timeout=timeout, env=env)


def run_lib(script_body, timeout=180):
    """Dot-source idle_check.ps1 and run script_body; returns stdout."""
    code = ". '%s'\n%s" % (str(IDLE).replace("\\", "/"), script_body)
    r = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-Command", code],
                       capture_output=True, text=True, timeout=timeout)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


def fable_pids():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq fable_2.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return sorted(re.findall(r'"fable_2\.exe","(\d+)"', out))


def pid_alive(pid):
    out = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return ('"%d"' % pid) in out


def wait_gone(pid, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if not pid_alive(pid):
            return True
        time.sleep(0.2)
    return not pid_alive(pid)


def kill_pid(pid):
    subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)


@unittest.skipUnless(PWSH, "PowerShell not available")
class IdleCliTest(unittest.TestCase):
    def test_no_parameters_is_usage_exit_2(self):
        r = run_ps([IDLE])
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("usage", r.stdout)

    def test_below_floor_is_exit_2(self):
        r = run_ps([IDLE, "-MinIdleSeconds", "30"])
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("at least 60", r.stdout)

    def test_report_exit_0_with_disclaimer(self):
        r = run_ps([IDLE, "-Report"])
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("not a permission to run the game", r.stdout)

    def test_report_with_gating_parameter_is_usage_error(self):
        for extra in (["-MinIdleSeconds", "300"], ["-WatchSeconds", "3"]):
            r = run_ps([IDLE, "-Report", *extra])
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_idle_longer_than_possible_no_wait_is_exit_1(self):
        r = run_ps([IDLE, "-MinIdleSeconds", "99999999", "-MaxWaitSeconds", "0"])
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)

    def test_forced_present_hook_blocks_the_gate(self):
        r = run_ps([IDLE, "-MinIdleSeconds", "60", "-MaxWaitSeconds", "0"],
                   {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("forced-present", r.stdout)

    def test_watch_stops_at_forced_signal(self):
        r = run_ps([IDLE, "-WatchSeconds", "5"], {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("presence signal: forced-present", r.stdout)


PS_GATE_HELPERS = r"""
function LastLE($list, $rel) { $m = [double]::NegativeInfinity; foreach ($x in @($list)) { if ($x -le $rel -and $x -gt $m) { $m = [double]$x } }; $m }
function InAny($ivs, $rel) { foreach ($iv in @($ivs)) { if ($rel -ge $iv[0] -and $rel -lt $iv[1]) { return $true } }; $false }
function Stamp($v) { if ([double]::IsNegativeInfinity($v)) { -1.0 } else { 1000.0 + $v } }
"""


@unittest.skipUnless(PWSH, "PowerShell not available")
class GateLoopTest(unittest.TestCase):
    """Wait-Fable2Away against a scripted probe and a fake clock (the real floor stays in force)."""

    def run_scenarios(self, scenarios):
        body = PS_GATE_HELPERS + r"""
$scenarios = '%s' | ConvertFrom-Json
$out = foreach ($sc in $scenarios) {
  $t = @{ v = 1000.0 }
  $probe = {
    $rel = $t.v - 1000.0
    $li = LastLE $sc.input $rel
    $idle = if ([double]::IsNegativeInfinity($li)) { $rel + 100000.0 } else { $rel - $li }
    if (InAny $sc.idle_fail $rel) { $idle = -1.0 }
    [pscustomobject]@{
      Now = $t.v; IdleSeconds = $idle
      LastInputTick = if ([double]::IsNegativeInfinity($li)) { [long]7 } else { [long]($li * 1000) + 1000000 }
      UptimeSeconds = $rel + 1000000.0
      PadConnected = (InAny $sc.pad_connected $rel)
      PadActivityStamp = (Stamp (LastLE $sc.pad_activity $rel))
      PadChangeStamp = (Stamp (LastLE $sc.pad_change $rel))
      AudioStamp = (Stamp (LastLE $sc.audio $rel))
      NotificationState = if (InAny $sc.fullscreen $rel) { 3 } else { 5 }
      ForegroundPid = 0; Session0 = $false; Fable2Running = [bool]$sc.running; ForcePresent = $false
      ProbeFailed = (InAny $sc.fail $rel); Failures = @()
    }
  }.GetNewClosure()
  $clock = { $t.v }.GetNewClosure()
  $done = @{}
  $sleep = { param($ms) $t.v += $ms / 1000.0
    foreach ($j in @($sc.jump)) { if (-not $done.ContainsKey([string]$j[0]) -and ($t.v - 1000.0) -ge $j[0]) { $done[[string]$j[0]] = $true; $t.v += $j[1] } }
    # real_stall [at, seconds]: the process really stands still once (the loop's own clock does not move)
    if (@($sc.real_stall).Count -eq 2 -and -not $done.ContainsKey("stall") -and ($t.v - 1000.0) -ge $sc.real_stall[0]) { $done["stall"] = $true; Start-Sleep -Milliseconds ([int]($sc.real_stall[1] * 1000)) } }.GetNewClosure()
  try {
    $r = Wait-Fable2Away -MinIdleSeconds $sc.min -MaxWaitSeconds $sc.wait -Probe $probe -Clock $clock -Sleep $sleep
    [pscustomobject]@{ name = $sc.name; passed = $r.Passed; at = ($t.v - 1000.0); signals = (@($r.Signals) -join ',') }
  } catch { [pscustomobject]@{ name = $sc.name; passed = $false; at = -1; signals = 'THREW: ' + $_.Exception.Message } }
}
$out | ConvertTo-Json -Compress -AsArray""" % json.dumps(scenarios)
        self.stdout = run_lib(body)
        res = json.loads(self.stdout.strip().splitlines()[-1])
        return {r["name"]: r for r in res}

    @staticmethod
    def sc(name, **kw):
        base = dict(name=name, min=60, wait=400, input=[], idle_fail=[], pad_connected=[], pad_activity=[],
                    pad_change=[], audio=[], fullscreen=[], fail=[], running=False, jump=[], real_stall=[])
        base.update(kw)
        return base

    def test_scenarios(self):
        sc = self.sc
        FOREVER = 1e9
        res = self.run_scenarios([
            sc("clean"),                                                     # audio window only
            sc("pad_quiet", pad_connected=[[0, FOREVER]], pad_change=[0]),
            sc("pad_activity", pad_connected=[[0, FOREVER]], pad_change=[0], pad_activity=[41.1]),
            sc("pad_connect", pad_connected=[[30, FOREVER]], pad_change=[30], input=[-20]),   # idle 20 s at the start: the gate cannot pass before the connect
            sc("pad_disconnect", pad_connected=[[0, 40]], pad_change=[0, 40]),
            sc("pad_then_fullscreen", pad_connected=[[0, FOREVER]], pad_change=[0], fullscreen=[[20, 30]]),
            sc("pad_too_long", pad_connected=[[0, FOREVER]], pad_change=[0], wait=30),
            sc("input_at_10", input=[10]),
            sc("input_before", input=[-50], min=60),
            sc("idle_fail", idle_fail=[[0, 50]]),
            sc("probe_fail", fail=[[0, 50]]),
            sc("fullscreen", fullscreen=[[0, 50]]),
            sc("running", running=True, wait=30),
            sc("audio", audio=[5, 12]),
            sc("floor", min=30),
            sc("jump", jump=[[10, 3600]], wait=10000),            # the machine sleeps for an hour after 10 s of polling
            sc("jump_small", jump=[[10, 4]], wait=10000),         # a 4 s hiccup is below the 5 s continuity limit
            sc("jump_out_of_wait", jump=[[10, 3600]], wait=100),
            sc("wall_stall", real_stall=[10, 5.6]),               # the process stands still for 5.6 real seconds; the loop's clock does not show it
        ])
        self.assertTrue(res["clean"]["passed"]); self.assertTrue(59.9 <= res["clean"]["at"] <= 61, res["clean"])   # audio needs the whole 60 s
        self.assertTrue(res["pad_quiet"]["passed"]); self.assertTrue(59.9 <= res["pad_quiet"]["at"] <= 61, res["pad_quiet"])
        # activity between two polls (latched stamp) restarts the observation
        self.assertTrue(res["pad_activity"]["passed"]); self.assertTrue(101 <= res["pad_activity"]["at"] <= 102.5, res["pad_activity"])
        # a pad that connects restarts it
        self.assertTrue(res["pad_connect"]["passed"]); self.assertTrue(90 <= res["pad_connect"]["at"] <= 91.5, res["pad_connect"])
        # unplugging a pad restarts it too (a quiet pad unplugged at 40 s must not pass at 40 s)
        self.assertTrue(res["pad_disconnect"]["passed"]); self.assertTrue(100 <= res["pad_disconnect"]["at"] <= 101.5, res["pad_disconnect"])
        # another signal restarts the pad observation too (fullscreen ends at 30 s, so 90 s, not 60 s)
        self.assertTrue(res["pad_then_fullscreen"]["passed"]); self.assertTrue(89.5 <= res["pad_then_fullscreen"]["at"] <= 91.5, res["pad_then_fullscreen"])
        self.assertFalse(res["pad_too_long"]["passed"]); self.assertIn("controller", res["pad_too_long"]["signals"])
        self.assertTrue(70 <= res["input_at_10"]["at"] <= 71.5, res["input_at_10"])
        self.assertTrue(59.9 <= res["input_before"]["at"] <= 61, res["input_before"])  # idle 50 s at the start, audio needs 60 s
        # a failing probe is never read as idle
        for k in ("idle_fail", "fullscreen"):
            self.assertTrue(res[k]["passed"] and res[k]["at"] >= 59.9, res[k])
        # a probe failure restarts the audio observation too: last failing poll at 49.75 s, then 60 s
        self.assertTrue(res["probe_fail"]["passed"] and 109 <= res["probe_fail"]["at"] <= 111, res["probe_fail"])
        self.assertFalse(res["running"]["passed"]); self.assertIn("fable2-running", res["running"]["signals"])
        self.assertTrue(res["audio"]["passed"]); self.assertTrue(71.5 <= res["audio"]["at"] <= 73.5, res["audio"])   # sound at 12 s, then 60 s
        # a clock jump (suspend) is not observed absence: every observation restarts and a full 60 s is needed again
        self.assertTrue(res["jump"]["passed"]); self.assertTrue(3669 <= res["jump"]["at"] <= 3672, res["jump"])
        self.assertTrue(res["jump_small"]["passed"]); self.assertTrue(59.5 <= res["jump_small"]["at"] <= 66, res["jump_small"])
        self.assertFalse(res["jump_out_of_wait"]["passed"])
        # the wall clock is compared too: 5.6 s that the loop's own clock did not count restart every observation (10 s + 60 s)
        self.assertTrue(res["wall_stall"]["passed"]); self.assertTrue(69.5 <= res["wall_stall"]["at"] <= 71.5, res["wall_stall"])
        self.assertIn("THREW", res["floor"]["signals"]); self.assertIn("at least 60", res["floor"]["signals"])

    def test_progress_lines_report_observation(self):
        # Sound at 5 s and 12 s; the gate needs 60 s without it and passes at about 72 s. The line printed
        # after 60 s says what has been observed so far: it does not call a user present who made no sign.
        self.run_scenarios([self.sc("audio", audio=[5, 12])])
        lines = [l for l in self.stdout.splitlines() if l.startswith("idle gate:")]
        self.assertEqual(len(lines), 1, self.stdout)
        self.assertRegex(lines[0], r"^idle gate: observing: 4[78] of 60 s without sound; waited 60 of at most 400 s$")
        self.assertNotIn("user present", self.stdout)
        # Nothing at all happens: the only open point is the audio observation itself.
        self.run_scenarios([self.sc("clean", min=90, wait=400)])
        lines = [l for l in self.stdout.splitlines() if l.startswith("idle gate:")]
        self.assertEqual(lines, ["idle gate: observing: 60 of 90 s without sound; waited 60 of at most 400 s"])
        # A state that holds now is named as such.
        self.run_scenarios([self.sc("running", running=True, wait=70)])
        self.assertIn("a fable_2.exe is running now", self.stdout)
        self.assertNotIn("user present", self.stdout)


@unittest.skipUnless(PWSH, "PowerShell not available")
class SnapshotTest(unittest.TestCase):
    """Get-Fable2Snapshot against a fake native probe object."""

    FAKE = r"""
function New-FakeNative($o) {
  $d = @{ now = 5000.0; idle = 400.0; tick = 123; uptime = 100000.0
          latch = @(0.0, -1.0, -1.0, -1.0, -1.0, -1.0, 4999.9, 1.0)
          ns = 5; fg = 0; s0 = $false; running = $false; throwOn = '' }
  foreach ($k in $o.PSObject.Properties) { $d[$k.Name] = $k.Value }
  $n = New-Object psobject
  $chk = { param($name) if ($d.throwOn -eq $name) { throw "fake failure in $name" } }.GetNewClosure()
  $n | Add-Member ScriptMethod Now { $d.now }.GetNewClosure()
  $n | Add-Member ScriptMethod LastInputInfo { & $chk 'LastInputInfo'; @([double]$d.idle, [double]$d.tick) }.GetNewClosure()
  $n | Add-Member ScriptMethod UptimeSeconds { $d.uptime }.GetNewClosure()
  $n | Add-Member ScriptMethod Latch { & $chk 'Latch'; [double[]]$d.latch }.GetNewClosure()
  $n | Add-Member ScriptMethod NotificationState { & $chk 'NotificationState'; [int]$d.ns }.GetNewClosure()
  $n | Add-Member ScriptMethod ForegroundPid { [int]$d.fg }.GetNewClosure()
  $n | Add-Member ScriptMethod Session0 { [bool]$d.s0 }.GetNewClosure()
  $n | Add-Member ScriptMethod Fable2Running { [bool]$d.running }.GetNewClosure()
  $n
}
"""

    def snap(self, cases):
        body = self.FAKE + r"""
$cases = '%s' | ConvertFrom-Json
$out = foreach ($c in $cases) {
  $n = New-FakeNative $c.fake
  $s = if ($c.check) { Get-Fable2Snapshot -Native $n -CheckProcess } else { Get-Fable2Snapshot -Native $n }
  [pscustomobject]@{ failed = $s.ProbeFailed; idle = $s.IdleSeconds; running = $s.Fable2Running; padConnected = $s.PadConnected
                     padChange = $s.PadChangeStamp; audio = $s.AudioStamp; text = ($s.Failures -join '; ') }
}
$out | ConvertTo-Json -Compress -AsArray""" % json.dumps(cases)
        return json.loads(run_lib(body))

    def test_snapshot_cases(self):
        L = [1.0, 4900.0, 4800.0, 4950.0, -1.0, -1.0, 4999.9, 1.0]
        cases = [
            {"fake": {}},                                                           # 0 clean
            {"fake": {"idle": -1}},                                                 # 1 GetLastInputInfo failed
            {"fake": {"latch": [0, -1, -1, -1, 4999.5, -1, 4999.9, 1]}},            # 2 recent XInput error
            {"fake": {"latch": [0, -1, -1, -1, 4990.0, -1, 4999.9, 1]}},            # 3 old error is not current
            {"fake": {"latch": [0, -1, -1, -1, -1, 4999.5, 4999.9, 1]}},            # 4 recent audio error
            {"fake": {"latch": [0, -1, -1, -1, -1, -1, 4990.0, 1]}},                # 5 sampler stalled
            {"fake": {"latch": [0, -1, -1, -1, -1, -1, 4999.9, 0]}},                # 6 sampler not running
            {"fake": {"running": True}, "check": True},                             # 7
            {"fake": {"running": True}, "check": False},                            # 8 not asked
            {"fake": {"throwOn": "Latch"}},                                         # 9
            {"fake": {"throwOn": "NotificationState"}},                             # 10
            {"fake": {"throwOn": "LastInputInfo"}},                                 # 11
            {"fake": {"latch": L}},                                                 # 12 mapping
            {"fake": {"s0": True}},                                                 # 13 session 0 is not a probe failure here
        ]
        r = self.snap(cases)
        self.assertFalse(r[0]["failed"], r[0])
        self.assertTrue(r[1]["failed"] and r[1]["idle"] == -1, r[1])
        self.assertTrue(r[2]["failed"], r[2])
        self.assertFalse(r[3]["failed"], r[3])
        self.assertTrue(r[4]["failed"], r[4])
        self.assertTrue(r[5]["failed"], r[5])
        self.assertTrue(r[6]["failed"], r[6])
        self.assertTrue(r[7]["running"] and not r[8]["running"])
        for i in (9, 10, 11):
            self.assertTrue(r[i]["failed"], r[i])
        self.assertTrue(r[12]["padConnected"] and r[12]["padChange"] == 4800.0 and r[12]["audio"] == 4950.0, r[12])

    def test_real_snapshot_and_sampler(self):
        out = run_lib("""
$n = $script:Fable2Native
$n.StartSampler(); Start-Sleep -Milliseconds 300
$l = $n.Latch(); $now = $n.Now()
'heartbeat age: ' + [math]::Round($now - $l[6], 2) + ' running: ' + $l[7]
$t = [Diagnostics.Stopwatch]::StartNew(); 1..20 | ForEach-Object { $null = Get-Fable2Snapshot }
'ms per snapshot: ' + [int]($t.ElapsedMilliseconds / 20)
$n.StopSampler(); Start-Sleep -Milliseconds 100
'running after stop: ' + $n.Latch()[7]
""")
        age = float(re.search(r"heartbeat age: ([\d.]+)", out).group(1))
        self.assertLess(age, 0.5, out)
        self.assertIn("running: 1", out)
        self.assertIn("running after stop: 0", out)
        self.assertLess(int(re.search(r"ms per snapshot: (\d+)", out).group(1)), 50, out)


@unittest.skipUnless(PWSH, "PowerShell not available")
class DecisionTest(unittest.TestCase):
    BASE = dict(IdleSeconds=1000, UptimeSeconds=100000, MinIdleSeconds=300, PadObserved=False,
                PadQuietSeconds=0, AudioQuietSeconds=1000, NotificationState=5, Fable2Running=False,
                ProbeFailed=False, Session0=False, ForcePresent=False)

    def decide(self, cases):
        full = [dict(self.BASE, **c) for c in cases]
        body = ("$cases = '%s' | ConvertFrom-Json\n"
                "$out = foreach ($c in $cases) {\n"
                "  $h = @{}; $c.PSObject.Properties | ForEach-Object { $h[$_.Name] = $_.Value }\n"
                "  $d = Get-Fable2GateDecision @h\n"
                "  [pscustomobject]@{ Present = $d.Present; Signals = (@($d.Signals) -join ',') }\n"
                "}\n$out | ConvertTo-Json -Compress -AsArray" % json.dumps(full))
        return json.loads(run_lib(body))

    def test_gate_table(self):
        cases = [
            ({}, False, ""),
            ({"IdleSeconds": 299}, True, "input"),
            ({"IdleSeconds": 300}, False, ""),
            ({"IdleSeconds": -1}, True, "unknown"),
            ({"IdleSeconds": 200000}, True, "unknown"),                  # more than the uptime
            ({"PadObserved": True, "PadQuietSeconds": 299}, True, "controller"),
            ({"PadObserved": True, "PadQuietSeconds": 300}, False, ""),
            ({"PadObserved": False, "PadQuietSeconds": 0}, False, ""),
            ({"AudioQuietSeconds": 299}, True, "audio"),
            ({"AudioQuietSeconds": 300}, False, ""),
            ({"NotificationState": 1}, False, ""),
            ({"NotificationState": 2}, True, "fullscreen"),
            ({"NotificationState": 3}, True, "fullscreen"),
            ({"NotificationState": 4}, True, "fullscreen"),
            ({"NotificationState": 5}, False, ""),
            ({"NotificationState": 6}, False, ""),
            ({"NotificationState": 7}, True, "fullscreen"),
            ({"NotificationState": 0}, True, "unknown"),
            ({"NotificationState": 9}, True, "unknown"),
            ({"NotificationState": -1}, True, "unknown"),
            ({"Fable2Running": True}, True, "fable2-running"),
            ({"ProbeFailed": True}, True, "unknown"),
            ({"Session0": True}, True, "unknown"),
            ({"ForcePresent": True}, True, "forced-present"),
            ({"IdleSeconds": 0, "NotificationState": 3, "Fable2Running": True}, True,
             "input,fullscreen,fable2-running"),
        ]
        got = self.decide([c[0] for c in cases])
        for (case, present, signals), g in zip(cases, got):
            self.assertEqual((g["Present"], g["Signals"]), (present, signals), case)

    def test_non_finite_idle_is_unknown(self):
        out = run_lib("""
foreach ($v in @([double]::NaN, [double]::PositiveInfinity, [double]::NegativeInfinity, -5.0)) {
  $d = Get-Fable2GateDecision -IdleSeconds $v -UptimeSeconds 100000 -MinIdleSeconds 300 -PadObserved $false -PadQuietSeconds 0 `
    -AudioQuietSeconds 1000 -NotificationState 5 -Fable2Running $false -ProbeFailed $false -Session0 $false -ForcePresent $false
  "case $v : $($d.Present) $($d.Signals -join ',')"
}""")
        self.assertEqual(len(re.findall(r": True unknown", out)), 4, out)

    def test_mandatory_parameters(self):
        out = run_lib("""
try { $null = Get-Fable2GateDecision -IdleSeconds 5 -ErrorAction Stop; 'no error' } catch { 'threw' }
""".replace("5 -Error", "5 -MinIdleSeconds 5 -Error"))  # most parameters still missing
        self.assertIn("threw", out)

    def test_watch_table(self):
        base = dict(LastInputTick=500, BaselineInputTick=500, PadActivityStamp=-1, PadChangeStamp=-1,
                    AudioStamp=-1, BaselineStamp=2000, NotificationState=5, ForegroundPid=10, GamePid=99,
                    ProbeFailed=False, Session0=False, ForcePresent=False)
        cases = [
            ({}, ""),
            ({"LastInputTick": 501}, "input"),
            ({"LastInputTick": 499}, "input"),                       # a tick that goes backwards counts
            ({"PadActivityStamp": 2000.5}, "controller"),
            ({"PadActivityStamp": 1999.0}, ""),                      # before the gate passed
            ({"PadChangeStamp": 2001}, "controller"),                # a pad connected or unplugged
            ({"AudioStamp": 2000.2}, "audio"),
            ({"AudioStamp": 1999}, ""),
            ({"NotificationState": 3}, "fullscreen"),
            ({"NotificationState": 3, "ForegroundPid": 99}, ""),     # the game's own fullscreen state
            ({"NotificationState": 2, "ForegroundPid": 99}, ""),
            ({"NotificationState": 1}, ""),
            ({"NotificationState": 8}, "unknown"),
            ({"ProbeFailed": True}, "unknown"),
            ({"Session0": True}, "unknown"),
            ({"ForcePresent": True}, "forced-present"),
            ({"NotificationState": 3, "GamePid": 0, "ForegroundPid": 0}, "fullscreen"),
        ]
        full = [dict(base, **c[0]) for c in cases]
        body = ("$cases = '%s' | ConvertFrom-Json\n"
                "$out = foreach ($c in $cases) {\n"
                "  $h = @{}; $c.PSObject.Properties | ForEach-Object { $h[$_.Name] = $_.Value }\n"
                "  $s = Get-Fable2WatchSignal @h\n  if ($s) { $s } else { '' }\n}\n"
                "$out | ConvertTo-Json -Compress -AsArray" % json.dumps(full))
        got = json.loads(run_lib(body))
        for (case, want), g in zip(cases, got):
            self.assertEqual(g, want, case)

    def test_pad_activity_predicate(self):
        z = dict(Buttons=0, LeftTrigger=0, RightTrigger=0, ThumbLX=0, ThumbLY=0, ThumbRX=0, ThumbRY=0)
        cases = [
            ({}, False), (dict(Buttons=1), True),
            (dict(LeftTrigger=30), False), (dict(LeftTrigger=31), True),
            (dict(RightTrigger=30), False), (dict(RightTrigger=31), True),
            (dict(ThumbLX=8000), False), (dict(ThumbLX=8001), True),
            (dict(ThumbLY=-8000), False), (dict(ThumbLY=-8001), True),
            (dict(ThumbRX=8001), True), (dict(ThumbRY=-32768), True),
            (dict(ThumbLX=5600, ThumbLY=5600), False),               # 7920 from centre
            (dict(ThumbLX=5700, ThumbLY=5700), True),                # 8061 from centre
            (dict(ThumbRX=-5700, ThumbRY=5700), True),
        ]
        body = ("$cases = '%s' | ConvertFrom-Json\n"
                "$out = foreach ($c in $cases) {\n"
                "  $h = @{}; $c.PSObject.Properties | ForEach-Object { $h[$_.Name] = $_.Value }\n"
                "  [bool](Test-Fable2PadActive @h)\n}\n"
                "$out | ConvertTo-Json -Compress -AsArray" % json.dumps([dict(z, **c[0]) for c in cases]))
        got = json.loads(run_lib(body))
        for (case, want), g in zip(cases, got):
            self.assertEqual(g, want, case)

    def test_poll_gap_table(self):
        cases = [(0, 4.9, True, False), (0, 5.0, True, False), (0, 5.1, True, True), (10, 9, True, True), (100, 3700, True, True)]
        body = "$out = @(" + ",".join("(Test-Fable2PollGap -Previous %s -Current %s -LimitSeconds 5)" % (a, b) for a, b, _, _ in cases) + "); $out -join ','"
        got = run_lib(body).strip().split(",")
        for (a, b, _, want), g in zip(cases, got):
            self.assertEqual(g, str(want), (a, b))

    def test_type_is_versioned(self):
        out = run_lib("[Fable2IdleProbeV3]::ProbeVersion")
        self.assertEqual(out.strip(), "3")

    def test_progress_text_table(self):
        # What the gate loop prints while it waits: every open signal, as what has been observed.
        cases = [
            (["audio"], "idle gate: observing: 61 of 300 s without sound; waited 61 of at most 1800 s"),
            (["input", "audio"], "idle gate: observing: 12 of 300 s without keyboard or mouse input, "
                                 "61 of 300 s without sound; waited 61 of at most 1800 s"),
            (["controller"], "idle gate: observing: 200 of 300 s without controller activity; waited 61 of at most 1800 s"),
            (["fullscreen"], "idle gate: observing: another program is fullscreen or presenting now; waited 61 of at most 1800 s"),
            (["fable2-running"], "idle gate: observing: a fable_2.exe is running now; waited 61 of at most 1800 s"),
            (["forced-present"], "idle gate: observing: the test hook forces present; waited 61 of at most 1800 s"),
            (["unknown", "audio"], "idle gate: observing: the state is unknown now (a probe failed or gave no usable value), "
                                   "61 of 300 s without sound; waited 61 of at most 1800 s"),
            (["something-new"], "idle gate: observing: something-new now; waited 61 of at most 1800 s"),
        ]
        body = ("$cases = '%s' | ConvertFrom-Json\n"
                "foreach ($c in $cases) { Get-Fable2GateProgressText -Signals @($c) -IdleSeconds 12.9 -PadQuietSeconds 200.2 "
                "-AudioQuietSeconds 61.7 -MinIdleSeconds 300 -WaitedSeconds 61.9 -MaxWaitSeconds 1800 }" % json.dumps([c[0] for c in cases]))
        got = run_lib(body).strip().splitlines()
        self.assertEqual(got, [c[1] for c in cases])
        for line in got:
            self.assertNotIn("user present", line)


@unittest.skipUnless(PWSH, "PowerShell not available")
class SamplerCoreTest(unittest.TestCase):
    """The sampler's stamping (Fable2SamplerCore) against scripted pad and meter readers."""

    def test_stamps(self):
        out = run_lib(r"""
$st = @{ code = 0; act = $false; meterThrow = $false; level = 0.0 }
$pr = [Fable2SamplerCore+PadReader]{ param([int]$slot, [int[]]$v)
  if ($slot -ne 0) { return 1167 }
  if ($st.code -ne 0) { return $st.code }
  for ($i = 0; $i -lt 7; $i++) { $v[$i] = 0 }
  if ($st.act) { $v[0] = 1 }
  return 0 }.GetNewClosure()
$mr = [Fable2SamplerCore+MeterReader]{ if ($st.meterThrow) { throw 'meter failed' }; return [double]$st.level }.GetNewClosure()
function Show($name, $c) { "$name " + (($c | ForEach-Object { [string]$_ }) -join ',') }
$c = New-Object Fable2SamplerCore $pr, $mr
$c.Step(10.0);  Show 'connect' $c.Latch()
$st.act = $true;  $c.Step(11.0); Show 'activity' $c.Latch()
$st.act = $false; $st.code = 1167; $c.Step(12.0); Show 'unplug' $c.Latch()
$st.code = 5; $c.Step(13.0); Show 'xinput-error' $c.Latch()
$st.code = 1167; $st.meterThrow = $true; $c.Step(14.0); Show 'audio-error' $c.Latch()
$st.meterThrow = $false; $st.level = 0.004; $c.Step(15.0); Show 'quiet' $c.Latch()
$st.level = 0.5; $c.Step(16.0); Show 'sound' $c.Latch()
""")
        rows = {l.split(" ", 1)[0]: [float(x) for x in l.split(" ", 1)[1].split(",")] for l in out.strip().splitlines()}
        # latch: connected, activity, change, audio, pad error, audio error
        self.assertEqual(rows["connect"][0], 1.0); self.assertEqual(rows["connect"][2], 10.0)
        self.assertEqual(rows["activity"][1], 11.0)
        self.assertEqual(rows["unplug"][0], 0.0); self.assertEqual(rows["unplug"][2], 12.0)    # unplug is stamped
        self.assertEqual(rows["xinput-error"][4], 13.0)                                         # XInput error is stamped
        self.assertEqual(rows["audio-error"][5], 14.0)                                          # audio error is stamped
        self.assertEqual(rows["quiet"][3], -1.0)                                                # below the threshold
        self.assertEqual(rows["sound"][3], 16.0)


HARNESS = r"""
# Test harness for drive_game.ps1: a scripted probe, a fake gate clock and test hooks.
$scenario = $env:TEST_SCENARIO
if ($scenario -eq 'multiline') { throw "first line`nsecond line" }
if ($scenario -eq 'blank_at_load') {
  foreach ($scp in 1, 2, 3, 'Script', 'Global') {
    try { Set-Variable -Name Label -Value '' -Scope $scp -Force -ErrorAction SilentlyContinue } catch { }
    try { Set-Variable -Name testMode -Value $false -Scope $scp -Force -ErrorAction SilentlyContinue } catch { }
  }
}
$fake = @{ adv = 0.0; sw = [Diagnostics.Stopwatch]::StartNew(); tick = 1000; pad = $false; padchg = $false; audio = $false
           fs = $false; fgThrow = $false; written = $false; stalled = $false; jumped = $false }
$n = New-Object psobject
$n | Add-Member ScriptMethod Now { 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds }.GetNewClosure()
$n | Add-Member ScriptMethod LastInputInfo {
  $idle = if ($env:TEST_SCENARIO -eq 'recent_input') { 280.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds } else { 100000.0 }
  @([double]$idle, [double]$fake.tick) }.GetNewClosure()
$n | Add-Member ScriptMethod UptimeSeconds { 1000000.0 }
$n | Add-Member ScriptMethod Latch {
  $now = 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds
  $act = if ($fake.pad) { $now } else { -1.0 }
  $chg = if ($fake.padchg) { $now } else { -1.0 }
  $aud = if ($fake.audio) { $now } else { -1.0 }
  [double[]]@(0.0, $act, $chg, $aud, -1.0, -1.0, $now, 1.0) }.GetNewClosure()
$n | Add-Member ScriptMethod NotificationState { if ($fake.fs) { 3 } else { 5 } }.GetNewClosure()
$n | Add-Member ScriptMethod ForegroundPid { if ($fake.fgThrow) { throw 'fake foreground failure' }; 0 }.GetNewClosure()
$n | Add-Member ScriptMethod Session0 { $false }
$n | Add-Member ScriptMethod Fable2Running { $env:TEST_SCENARIO -eq 'present' }
$n | Add-Member ScriptMethod StartSampler { }
$n | Add-Member ScriptMethod StopSampler { }
$tick = {
  param($elapsed, $ctx)
  if (-not $fake.written) {
    $fake.written = $true
    Set-Content -LiteralPath $env:TEST_OUT -Value ([string]$fake.adv)
  }
  if ($elapsed -ge 1 -and $ctx.ShotDir -and -not $ctx.shotMade) {
    $ctx.shotMade = $true
    New-Item -ItemType Directory -Force $ctx.ShotDir | Out-Null
    $f = Join-Path $ctx.ShotDir 'shot_1s.png'
    Set-Content -LiteralPath $f -Value 'x'
    [void]$ctx.ShotFiles.Add($f)
  }
  if ($env:TEST_SCENARIO -eq 'slowpoll' -and $elapsed -ge 1) { Start-Sleep -Seconds 4 }
  if ($elapsed -ge 2) {
    switch ($env:TEST_SCENARIO) {
      'disturb_input' { $fake.tick = 99999 }
      'disturb_pad' { $fake.pad = $true }
      'disturb_padchange' { $fake.padchg = $true }
      'disturb_audio' { $fake.audio = $true }
      'disturb_fullscreen' { $fake.fs = $true }
      'disturb_probe' { $fake.fgThrow = $true }
      'jump_watch' { if (-not $fake.jumped) { $fake.jumped = $true; $fake.adv += 3600 } }
      'blank_label' {
        foreach ($sc in 'Script', 'Global', 0, 1, 2, 3, 4) {
          try { Set-Variable -Name Label -Value '' -Scope $sc -Force -ErrorAction SilentlyContinue } catch { }
          try { Set-Variable -Name testMode -Value $false -Scope $sc -Force -ErrorAction SilentlyContinue } catch { }
        }
      }
      'abort' { throw 'boom (test abort)' }
      'abort_multiline' { throw "boom (test abort)`nits second line" }
      'stall' {
        if (-not $fake.stalled) {
          $fake.stalled = $true
          Start-Sleep -Seconds 7
          $alive = [bool](Get-Process -Id $ctx.Pid -ErrorAction SilentlyContinue)
          Set-Content -LiteralPath $env:TEST_OUT2 -Value $(if ($alive) { 'alive' } else { 'dead' })
        }
      }
    }
  }
}.GetNewClosure()
@{ Native = $n
   Clock = { 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds }.GetNewClosure()
   Sleep = { param($ms) $fake.adv += $ms / 1000.0 }.GetNewClosure()
   Tick = $tick
   NoKill = ($env:TEST_SCENARIO -eq 'unkillable')
   FailJobCreate = ($env:TEST_SCENARIO -eq 'job_create_fail')
   FailJobAssign = ($env:TEST_SCENARIO -eq 'job_assign_fail')
   StallAfterLoopMs = $(if ($env:TEST_SCENARIO -eq 'stall_after') { 7000 } else { 0 })
   NoDeadManThread = ($env:TEST_SCENARIO -eq 'stall_between')
   StallBetweenPollsMs = $(if ($env:TEST_SCENARIO -eq 'stall_between') { 7000 } else { 0 })
   HoldAfterKillMs = $(if ($env:TEST_SCENARIO -eq 'clean_hold') { 4000 } else { 0 })
   AssignDelayMs = $(if ($env:TEST_SCENARIO -eq 'job_assign_fail') { 1500 } else { 0 }) }
"""

SYSTEM32 = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
SORT = SYSTEM32 / "sort.exe"   # a small harmless exe used as file content for guard tests
REAL_GAME = ROOT / "out" / "build" / "win-amd64-release" / "fable_2.exe"


@unittest.skipUnless(PWSH, "PowerShell not available")
class DriverRunPathTest(unittest.TestCase):
    """The driver's post-gate run against a harmless stand-in process (never the game)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="drive_run_"))
        self.addCleanup(shutil.rmtree, str(self.tmp), True)
        self.harness = self.tmp / "harness.ps1"
        self.harness.write_text(HARNESS, encoding="utf-8")
        self.exe = self.tmp / "exe"
        self.exe.mkdir()
        self.out = self.tmp / "gate_adv.txt"
        self.out2 = self.tmp / "stall.txt"
        self.pids = []
        self.addCleanup(self._cleanup)

    def _cleanup(self):
        for p in self.pids:
            kill_pid(p)

    def args(self, scenario, sleep=60, total=4, extra=(), command=None, plan=""):
        self.env = {"TEST_SCENARIO": scenario, "TEST_OUT": str(self.out), "TEST_OUT2": str(self.out2)}
        if command:   # a stand-in script file keeps quoting out of the command line
            script = self.tmp / "standin.ps1"
            script.write_text(command, encoding="utf-8")
            exe_args = "|-NoProfile|-File|%s" % script   # leading | so the value never looks like a parameter (a ":" would)
        else:
            exe_args = "|-NoProfile|-Command|Start-Sleep %d" % sleep
        return [DRIVE, "-ExeDir", self.exe, "-Total", str(total), "-Plan", plan, "-IdleWaitSeconds", "1800",
                "-TestHarness", self.harness, "-TestExe", shutil.which("pwsh") or PWSH,
                "-TestExeArgs", exe_args, *extra]

    def drive(self, scenario, **kw):
        r = run_ps(self.args(scenario, **kw), self.env)
        m = re.search(r"started pid (\d+)", r.stdout)
        pid = int(m.group(1)) if m else None
        if pid:
            self.pids.append(pid)
        return r, pid

    def last(self, r):
        return r.stdout.strip().splitlines()[-1]

    def runlog(self):
        return (self.exe / "logs" / "drive_game_runs.log").read_text()

    def assertLabelled(self, r):
        self.assertIn("*** TEST MODE", r.stdout)                        # the banner
        self.assertTrue(self.last(r).startswith("TEST HARNESS "), self.last(r))
        for line in self.runlog().splitlines():
            self.assertTrue(line.startswith("TEST HARNESS "), line)
        # no unlabelled verdict line anywhere
        for line in r.stdout.splitlines():
            self.assertNotRegex(line, r"^(RUN CLEAN|RUN DISTURBED|RUN ABORTED|GAME EXITED EARLY)")

    def test_clean_run(self):
        r, pid = self.drive("clean", total=3)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN CLEAN: no user input for \d+ s$")
        self.assertIn("at least 300 s", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "stand-in must be closed")
        self.assertFalse((self.exe / "logs" / "RUN_DISTURBED.txt").exists())
        self.assertIn("exit=0", self.runlog())
        self.assertIn("started pid %d" % pid, self.runlog())
        self.assertLabelled(r)

    def test_gate_requires_300_seconds(self):
        # last input 280 s before the start, and 300 s of observed silence for audio: at least 300 fake seconds
        r, pid = self.drive("recent_input", total=2)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertGreaterEqual(float(self.out.read_text()), 295.0, r.stdout)   # 300 s of fake time minus the real seconds counted in the clock

    def test_children_of_the_game_die_too(self):
        """Stopping the game terminates the job: the game's child is gone while the driver is still alive."""
        child = self.tmp / "child.txt"
        pw = (shutil.which("pwsh") or PWSH).replace("'", "''")
        cmd = ("$c = Start-Process -FilePath '%s' -ArgumentList '-NoProfile','-Command','Start-Sleep 60' -PassThru; "
               "Set-Content '%s' $c.Id; Start-Sleep 60" % (pw, str(child).replace("'", "''")))
        argv = self.args("clean_hold", total=7, command=cmd)
        env = dict(os.environ, **self.env)
        env.pop("FABLE2_IDLE_GATE_FORCE_PRESENT", None)
        p = subprocess.Popen([PWSH, "-NoProfile", "-NonInteractive", "-File", *map(str, argv)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        self.addCleanup(lambda: p.poll() is None and p.kill())
        end = time.time() + 120
        closed = False
        while time.time() < end:
            line = p.stdout.readline()
            m = re.search(r"started pid (\d+)", line)
            if m:
                self.pids.append(int(m.group(1)))
            if "closed game at" in line:
                closed = True
                break
            if not line:
                break
        self.assertTrue(closed, "the driver never closed the game")
        cpid = int(child.read_text().strip())
        self.pids.append(cpid)
        self.assertTrue(wait_gone(cpid, 2), "the child of the game must die with the job at once")
        self.assertIsNone(p.poll(), "the driver must still be alive here (it holds after the kill in this scenario)")
        p.communicate(timeout=60)

    def disturbed(self, scenario, signal):
        r, pid = self.drive(scenario, total=30)
        self.assertEqual(r.returncode, 11, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN DISTURBED: %s at \d+\.\d s$" % signal)
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "stand-in must be closed at once")
        self.assertTrue((self.exe / "logs" / "RUN_DISTURBED.txt").exists())   # no shots: next to the log
        self.assertLabelled(r)

    def test_disturbed_by_pad(self):
        self.disturbed("disturb_pad", "controller")

    def test_disturbed_by_pad_change(self):
        self.disturbed("disturb_padchange", "controller")

    def test_disturbed_by_audio(self):
        self.disturbed("disturb_audio", "audio")

    def test_disturbed_by_fullscreen(self):
        self.disturbed("disturb_fullscreen", "fullscreen")

    def test_watch_error_is_a_disturbance(self):
        self.disturbed("disturb_probe", "unknown")

    def test_disturbed_by_input(self):
        shots = self.tmp / "shots"
        r, pid = self.drive("disturb_input", total=30, extra=("-Shots", "50", "-ShotDir", shots))
        self.assertEqual(r.returncode, 11, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN DISTURBED: input at \d+\.\d s$")
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "stand-in must be closed at once")
        marker = shots / "RUN_DISTURBED.txt"
        self.assertTrue(marker.exists())
        text = marker.read_text()
        self.assertEqual(text.splitlines()[0], self.last(r))
        self.assertIn("do NOT use", text)
        # the run's screenshots are moved out of the folder and listed in the marker
        self.assertFalse((shots / "shot_1s.png").exists())
        moved = list(shots.glob("disturbed_*/shot_1s.png"))
        self.assertEqual(len(moved), 1)
        self.assertIn(moved[0].name, text)
        self.assertIn("exit=11", self.runlog())
        self.assertLabelled(r)

    def test_error_during_run_aborts_and_closes_game(self):
        r, pid = self.drive("abort", total=30)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: ")
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "the finally block must close the game")
        self.assertLabelled(r)

    def test_game_that_survives_the_kill_is_not_clean(self):
        r, pid = self.drive("unkillable", total=3)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: the game is still running after the kill")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_early_exit_is_not_clean(self):
        r, pid = self.drive("clean", sleep=1, total=8)
        self.assertEqual(r.returncode, 13, r.stdout + r.stderr)
        self.assertIn("TEST HARNESS GAME EXITED EARLY at", r.stdout)
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertLabelled(r)

    def test_gate_refusal_launches_nothing(self):
        r, pid = self.drive("present", total=4)
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIsNone(pid)
        self.assertIn("REFUSED", r.stdout)
        self.assertIn("fable2-running", r.stdout)
        self.assertIn("exit=10", self.runlog())
        self.assertIn("*** TEST MODE", r.stdout)
        self.assertTrue(self.runlog().startswith("TEST HARNESS "))

    def test_job_creation_failure_launches_nothing(self):
        r, pid = self.drive("job_create_fail", total=4)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIsNone(pid)
        self.assertIn("kill-on-close job object. Nothing launched", r.stdout)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED")

    def test_job_assignment_failure_kills_the_game_before_it_runs(self):
        ran = self.tmp / "ran.txt"
        cmd = "Set-Content '%s' x; Start-Sleep 60" % str(ran).replace("'", "''")
        r, pid = self.drive("job_assign_fail", total=4, command=cmd)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("could not assign the game to the kill-on-close job object", r.stdout)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED")
        time.sleep(2)
        # the game is created suspended and killed unassigned: it never executed a single instruction
        self.assertFalse(ran.exists(), "the stand-in ran outside the job")

    def test_dead_man_switch_terminates_the_game(self):
        r, pid = self.drive("stall", total=40)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: dead-man switch")
        # the stand-in was already dead when the stalled loop woke up (killed by the watchdog thread)
        self.assertEqual(self.out2.read_text().strip(), "dead")
        self.assertTrue(wait_gone(pid, 3))
        self.assertLabelled(r)

    def test_stall_with_short_total_never_ends_clean(self):
        r, pid = self.drive("stall", total=2)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: dead-man switch")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_dead_man_trip_after_the_last_poll_is_not_clean(self):
        # the thread fires during a stall that follows the final poll: only the verdict check can see it
        r, pid = self.drive("stall_after", total=2)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: dead-man switch")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_overrun_is_not_clean(self):
        # every poll takes 4 s (below the dead-man limit) but the run lasts far longer than -Total
        r, pid = self.drive("slowpoll", total=2)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: run overran \(\d+ s for a 2 s run\)")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_clock_jump_during_the_watch_is_a_disturbance(self):
        self.disturbed("jump_watch", "watch-gap")

    def test_aborted_run_moves_its_screenshots(self):
        shots = self.tmp / "shots"
        r, pid = self.drive("abort", total=30, extra=("-Shots", "50", "-ShotDir", shots))
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertFalse((shots / "shot_1s.png").exists())
        self.assertEqual(len(list(shots.glob("aborted_*/shot_1s.png"))), 1)

    def test_early_exit_moves_its_screenshots(self):
        shots = self.tmp / "shots"
        r, pid = self.drive("clean", sleep=3, total=30, extra=("-Shots", "50", "-ShotDir", shots))
        self.assertEqual(r.returncode, 13, r.stdout + r.stderr)
        self.assertFalse((shots / "shot_1s.png").exists())
        self.assertEqual(len(list(shots.glob("early_exit_*/shot_1s.png"))), 1)

    def test_log_lines_are_single_line(self):
        r, pid = self.drive("multiline", total=3)
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        lines = self.runlog().splitlines()
        for line in lines:
            self.assertTrue(line.startswith("TEST HARNESS "), line)
        exit_lines = [l for l in lines if "exit=10" in l]
        self.assertEqual(len(exit_lines), 1)
        self.assertIn("first line", exit_lines[0])
        self.assertIn("second line", exit_lines[0])

    def test_started_line_names_the_executable(self):
        r, pid = self.drive("clean", total=3)
        exe = (shutil.which("pwsh") or PWSH).lower()
        started = [l for l in r.stdout.splitlines() if l.startswith("started pid")]
        self.assertEqual(len(started), 1)
        self.assertIn(exe, started[0].lower())
        logged = [l for l in self.runlog().splitlines() if "started pid" in l]
        self.assertEqual(len(logged), 1)
        self.assertIn(exe, logged[0].lower())

    def test_stall_between_polls_is_caught_without_the_thread(self):
        # no background thread at all: the main thread's own gap check must still abort the run
        r, pid = self.drive("stall_between", total=2)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: dead-man switch: the driver did not poll the watch for")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_harness_code_cannot_blank_the_mode_at_load(self):
        # harness code that runs inside the driver before the launch tries to clear testMode and the label
        r, pid = self.drive("blank_at_load", total=3)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        started = [l for l in r.stdout.splitlines() if l.startswith("started pid")]
        self.assertEqual(len(started), 1)
        self.assertIn((shutil.which("pwsh") or PWSH).lower(), started[0].lower())   # still the stand-in, not ExeDir/fable_2.exe
        self.assertLabelled(r)

    def test_label_cannot_be_blanked_by_harness_code(self):
        r, pid = self.drive("blank_label", total=4)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertLabelled(r)

    # --- the plan: accepted by the game, moved by -PlanShift ---

    def game_script(self, log_lines=None, log_name="fable_2_7.log", sleep=60):
        """A stand-in that behaves like the game at start: it writes its log into <ExeDir>\\logs (when log_lines
        is given) and the plan it was handed into plan.txt, then idles."""
        q = lambda p: str(p).replace("'", "''")
        text = "Set-Content -LiteralPath '%s' -Value $env:FABLE2_AUTOPLAY\n" % q(self.tmp / "plan.txt")
        if log_lines is not None:
            text += "New-Item -ItemType Directory -Force '%s' | Out-Null\n" % q(self.exe / "logs")
            text += "Set-Content -LiteralPath '%s' -Value @(%s)\n" % (
                q(self.exe / "logs" / log_name), ", ".join("'%s'" % l.replace("'", "''") for l in log_lines))
        return text + "Start-Sleep %d\n" % sleep

    ARMED = "[2026-10-07 12:42:20.758] [info] [sys] [t32872] [autoplay] armed: 3 steps, hold 100 ms"
    REJECTED = "[2026-10-07 12:42:20.758] [warning] [sys] [t32872] [autoplay] invalid FABLE2_AUTOPLAY, bad token 'x:A'; autoplay disabled"

    def test_accepted_plan_is_clean_and_reaches_the_game_shifted(self):
        # Keys become pad buttons, times are plain decimals, and -PlanShift moves the plan, the shots and the total.
        r, pid = self.drive("clean", total=3, plan="1:E, 2.50:s,3:LS=0/100/500", command=self.game_script([self.ARMED]),
                            extra=("-PlanShift", "2", "-Shots", "1", "-ShotDir", self.tmp / "shots"))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual((self.tmp / "plan.txt").read_text().strip(), "3:A,4.5:Down,5:LS=0/100/500")
        self.assertIn("plan shift: 2 s", r.stdout)
        self.assertIn("no game window yet at 3 s", r.stdout)                 # the shot of 1 s, at 3 s
        m = re.match(r"^TEST HARNESS RUN CLEAN: no user input for (\d+) s$", self.last(r))
        self.assertTrue(m, self.last(r))
        self.assertGreaterEqual(int(m.group(1)), 5)                          # -Total 3 and the shift
        self.assertLabelled(r)

    def test_plan_the_game_rejected_is_never_clean(self):
        r, pid = self.drive("clean", total=4, plan="1:E", command=self.game_script([self.REJECTED]))
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: the game did not accept the plan")
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertIn("exit=12", self.runlog())
        self.assertTrue(wait_gone(pid, 3))
        self.assertLabelled(r)

    def test_plan_without_a_game_log_is_never_clean(self):
        r, pid = self.drive("clean", total=4, plan="1:E", command=self.game_script(None))
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: the game did not accept the plan")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_armed_line_of_an_earlier_run_does_not_count(self):
        logs = self.exe / "logs"
        logs.mkdir()
        (logs / "fable_2_6.log").write_text(self.ARMED + "\n")               # the log of an earlier run
        r, pid = self.drive("clean", total=4, plan="1:E", command=self.game_script([self.REJECTED]))
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS RUN ABORTED: the game did not accept the plan")
        # ... and a log of that name written again by this run does not either (only a new file is this run's)
        r, pid = self.drive("clean", total=4, plan="1:E", command=self.game_script([self.ARMED], log_name="fable_2_6.log"))
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)

    def test_no_plan_needs_no_armed_line(self):
        r, pid = self.drive("clean", total=3, plan=" , ", command=self.game_script(None))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual((self.tmp / "plan.txt").read_text().strip(), "")

    def test_shifted_shot_after_the_total_keeps_the_game_running(self):
        # m4: a game that exits between -Total and the last screenshot time ended early; it is not a clean run.
        shots = self.tmp / "shots"
        r, pid = self.drive("clean", sleep=4, total=2, extra=("-Shots", "9", "-ShotDir", shots))
        self.assertEqual(r.returncode, 13, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^TEST HARNESS GAME EXITED EARLY at \d+ s$")
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertLabelled(r)

    def test_verdict_is_the_last_line_and_one_line(self):
        # m5: an error message of several lines cannot push the verdict off the last line.
        r, pid = self.drive("abort_multiline", total=30)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertEqual(self.last(r), "TEST HARNESS RUN ABORTED: boom (test abort) | its second line")
        r, pid = self.drive("multiline", total=3)                            # the harness fails while it loads: a gate error
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^REFUSED: idle gate failed \(first line \| second line\); counted as user present\. Nothing launched\.$")

    def test_clean_seconds_are_launch_to_close(self):
        # m8: the driver stays alive 4 s after it closed the game in this scenario; the verdict's time is the game's.
        r, pid = self.drive("clean_hold", total=3)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        m = re.match(r"^TEST HARNESS RUN CLEAN: no user input for (\d+) s$", self.last(r))
        self.assertTrue(m, self.last(r))
        self.assertIn(int(m.group(1)), (3, 4))

    def test_game_dies_with_the_driver(self):
        """C1: killing the driver process itself must take the game with it (job object)."""
        env = dict(os.environ, **{"TEST_SCENARIO": "clean", "TEST_OUT": str(self.out), "TEST_OUT2": str(self.out2)})
        env.pop("FABLE2_IDLE_GATE_FORCE_PRESENT", None)
        p = subprocess.Popen([PWSH, "-NoProfile", "-NonInteractive", "-File", *map(str, self.args("clean", total=120))],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        self.addCleanup(lambda: p.poll() is None and p.kill())
        pid = None
        end = time.time() + 120
        while time.time() < end:
            line = p.stdout.readline()
            if not line:
                break
            m = re.search(r"started pid (\d+)", line)
            if m:
                pid = int(m.group(1))
                break
        self.assertIsNotNone(pid, "driver did not start the stand-in")
        self.pids.append(pid)
        self.assertTrue(pid_alive(pid))
        p.kill()   # TerminateProcess: no finally block runs
        p.wait(10)
        self.assertTrue(wait_gone(pid, 6), "the stand-in survived the death of the driver")
        self.assertIn("started pid %d" % pid, self.runlog())   # a killed driver leaves a trace


def patched_driver(root_dir, allow, with_idle=True):
    """A copy of the driver (and its libraries) whose allow-list constant also names `allow`, so tests can use a
    scratch -ExeDir outside test mode. The unpatched driver refuses any such -ExeDir."""
    d = Path(root_dir) / "ptools"
    d.mkdir(exist_ok=True)
    for name in ("drive_game.ps1", "idle_check.ps1", "cmdline_quote.ps1", "autoplay_plan.ps1"):
        if name == "idle_check.ps1" and not with_idle:
            continue
        src = (TOOLS / name).read_text(encoding="utf-8")
        if name == "drive_game.ps1":
            new = re.sub(r'(?m)^\$AllowedExeDirs = @\(.*?\)[ \t]*(#.*)?$',
                         lambda m: '$AllowedExeDirs = @(%s)' % ",".join('"%s"' % a for a in allow), src, count=1)
            assert new != src, "allow-list constant not found"
            src = new
        (d / name).write_text(src, encoding="utf-8", newline="\n")
    return d / "drive_game.ps1"


@unittest.skipUnless(PWSH, "PowerShell not available")
class DriverGateTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="drive_gate_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.assertFalse((Path(self.tmp) / "fable_2.exe").exists())
        self.drv = patched_driver(self.tmp, [self.tmp])

    def drive(self, *extra, force=True, exedir=True):
        env = {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"} if force else {}
        pre = ["-ExeDir", self.tmp] if exedir else []
        return run_ps([self.drv, *pre, "-Total", "5", *extra], env)

    def test_refuses_when_present(self):
        before = fable_pids()
        r = self.drive("-IdleWaitSeconds", "0")
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIn("REFUSED", r.stdout)
        self.assertIn("forced-present", r.stdout)
        self.assertIn("-IdleWaitSeconds 300", r.stdout)       # the refusal says how long to wait
        self.assertNotIn("started pid", r.stdout)
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertEqual(fable_pids(), before)
        self.assertIn("exit=10", (Path(self.tmp) / "logs" / "drive_game_runs.log").read_text())

    def test_bootsave_bad_name_is_refused(self):
        for bad in ("..\\Hero000", "Hero1", "Hero0003", "hero003x"):
            r = self.drive("-BootSave", bad)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn("-BootSave must be HeroNNN", r.stdout)
            self.assertNotIn("started pid", r.stdout)

    def test_bootsave_restores_the_pristine_save_and_keeps_the_replaced_one(self):
        live = Path(self.tmp) / "saves" / "B13EBABEBABEBABE" / "4D5307F1" / "00000001" / "Hero003"
        live.mkdir(parents=True)
        (live / "mainsave.bin").write_text("drifted")
        (live / "extra.bin").write_text("only in the live folder")
        pristine = Path(self.tmp) / "saves-pristine" / "Hero003"
        pristine.mkdir(parents=True)
        (pristine / "mainsave.bin").write_text("pristine")
        # -UserPresent in a folder without fable_2.exe: the gate is skipped and the launch itself fails.
        r = run_ps([self.drv, "-ExeDir", self.tmp, "-Total", "5", "-UserPresent", "-BootSave", "Hero003"])
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("restored pristine save Hero003", r.stdout)
        self.assertEqual((live / "mainsave.bin").read_text(), "pristine")
        self.assertFalse((live / "extra.bin").exists())
        replaced = list((Path(self.tmp) / "saves-replaced").glob("*/Hero003/mainsave.bin"))
        self.assertEqual(len(replaced), 1)
        self.assertEqual(replaced[0].read_text(), "drifted")

    def test_bootsave_without_that_save_is_refused_before_the_gate(self):
        other = Path(self.tmp) / "saves" / "B13EBABEBABEBABE" / "4D5307F1" / "00000001" / "Hero003"
        other.mkdir(parents=True)
        (other / "mainsave.bin").write_text("x")
        r = self.drive("-BootSave", "Hero004")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("needs exactly one save folder", r.stdout)
        self.assertIn("found 0", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_bootsave_failed_pristine_copy_leaves_the_live_save_in_place(self):
        import ctypes
        live = Path(self.tmp) / "saves" / "B13EBABEBABEBABE" / "4D5307F1" / "00000001" / "Hero003"
        live.mkdir(parents=True)
        (live / "mainsave.bin").write_text("drifted")
        pristine = Path(self.tmp) / "saves-pristine" / "Hero003"
        pristine.mkdir(parents=True)
        (pristine / "mainsave.bin").write_text("pristine")
        # Hold the pristine file open with no sharing, so the driver's copy of it fails.
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateFileW.restype = ctypes.c_void_p
        k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        k32.CloseHandle.argtypes = [ctypes.c_void_p]
        h = k32.CreateFileW(str(pristine / "mainsave.bin"), 0x80000000, 0, None, 3, 0, None)   # GENERIC_READ, OPEN_EXISTING
        self.assertNotIn(h, (None, ctypes.c_void_p(-1).value))
        try:
            r = run_ps([self.drv, "-ExeDir", self.tmp, "-Total", "5", "-UserPresent", "-BootSave", "Hero003"])
        finally:
            k32.CloseHandle(h)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("could not restore the pristine save", r.stdout)
        self.assertNotIn("started pid", r.stdout)
        self.assertEqual((live / "mainsave.bin").read_text(), "drifted")   # the live save was never moved
        self.assertEqual(list((Path(self.tmp) / "saves-replaced").glob("*/Hero003")), [])

    def test_bootsave_without_a_pristine_copy_leaves_the_save_alone(self):
        live = Path(self.tmp) / "saves" / "B13EBABEBABEBABE" / "4D5307F1" / "00000001" / "Hero003"
        live.mkdir(parents=True)
        (live / "mainsave.bin").write_text("live")
        r = run_ps([self.drv, "-ExeDir", self.tmp, "-Total", "5", "-UserPresent", "-BootSave", "Hero003"])
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertNotIn("restored pristine save", r.stdout)
        self.assertEqual((live / "mainsave.bin").read_text(), "live")
        self.assertFalse((Path(self.tmp) / "saves-replaced").exists())

    def test_exedir_outside_the_allow_list_is_refused(self):
        # (-IdleWaitSeconds 0: a driver without the check would end at the gate at once, not after the test's timeout)
        r = run_ps([DRIVE, "-ExeDir", self.tmp, "-Total", "5", "-IdleWaitSeconds", "0"], {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("not an allowed build output directory", r.stdout)
        self.assertNotIn("started pid", r.stdout)
        # m9: nothing is written into a folder the driver refused: no run log, no logs folder
        self.assertFalse((Path(self.tmp) / "logs").exists())
        for extra in (["stray"], ["-KeyboardInput"], ["-Plan", "x:E"]):      # refusals that come before or after it
            r = run_ps([DRIVE, "-ExeDir", self.tmp, "-Total", "5", "-IdleWaitSeconds", "0", *extra],
                       {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
            self.assertEqual(r.returncode, 2, (extra, r.stdout, r.stderr))
            self.assertFalse((Path(self.tmp) / "logs").exists(), extra)

    def test_suite_guard_holds_for_user_present_beside_a_game(self):
        # m7: -UserPresent is let through the suite guard only in a folder WITHOUT fable_2.exe. Here the allowed
        # folder holds one (a harmless stand-in under the game's name): the guard must refuse before any launch.
        shutil.copy(SORT, Path(self.tmp) / "fable_2.exe")
        for extra in ([], ["-KeyboardInput"]):
            r = run_ps([self.drv, "-ExeDir", self.tmp, "-Total", "2", "-Plan", "", "-UserPresent", *extra], {})
            self.assertEqual(r.returncode, 2, (extra, r.stdout, r.stderr))
            self.assertIn("test-suite guard", r.stdout)
            self.assertNotIn("started pid", r.stdout)
            self.assertNotIn("RUN UNCHECKED", r.stdout)

    def bad(self, *extra):
        """A refusal before the gate: exit 2, one REFUSED line last, the gate not evaluated, nothing launched."""
        r = self.drive("-IdleWaitSeconds", "0", *extra)
        self.assertEqual(r.returncode, 2, (extra, r.stdout, r.stderr))
        last = r.stdout.strip().splitlines()[-1]
        self.assertRegex(last, r"^REFUSED: .*Nothing launched\.$", (extra, r.stdout))
        self.assertNotIn("forced-present", r.stdout, extra)                  # refused before the gate ran
        self.assertNotIn("started pid", r.stdout, extra)
        self.assertIn("exit=2", (Path(self.tmp) / "logs" / "drive_game_runs.log").read_text().splitlines()[-1])
        return last

    def test_plan_the_game_would_reject_is_refused_before_the_gate(self):
        # The game rejects the whole plan on one bad token and then runs without input: the driver refuses it.
        self.assertIn("bad plan", self.bad("-Plan", "23:E,x:S"))                       # a time that is no number
        self.assertIn("exactly one ':'", self.bad("-Plan", "23:E:27"))                 # (was: the rest dropped)
        self.assertIn("bad stick entry", self.bad("-Plan", "55:LS=0/101/3000"))
        self.assertIn("23:LS=0000050/0/100", self.bad("-Plan", "23:LS=0000050/0/100")) # seven digits: the game's limit is six
        self.assertIn("no pad button", self.bad("-Plan", "23:Q"))
        # a plan the game takes reaches the gate (which the hook refuses: exit 10)
        r = self.drive("-IdleWaitSeconds", "0", "-Plan", "23:E, 27.5:S,55:LS=0/100/3000,56:VIEW=native,66:DISC=60/4")
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)

    def test_bad_shots_are_refused(self):
        # m3: 'NaN' used to parse as a time and switched the overrun rule off
        for shots in ("NaN", "50,NaN", "-5", "abc", "1e2", "Infinity", "5;6"):
            self.assertIn("-Shots", self.bad("-Shots", shots), shots)
        r = self.drive("-IdleWaitSeconds", "0", "-Shots", " 50 ,,80.5")
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)

    def test_plan_shift_is_a_whole_number_from_0_to_30(self):
        for shift in ("31", "-1", "2.5", "x", "", "100"):
            self.assertIn("-PlanShift", self.bad("-PlanShift", shift), shift)
        for shift in ("0", "6", "30"):
            r = self.drive("-IdleWaitSeconds", "0", "-PlanShift", shift)
            self.assertEqual(r.returncode, 10, (shift, r.stdout, r.stderr))

    def test_suite_guard_refuses_a_real_gate(self):
        # FABLE2_DRIVER_NO_REAL_GATE is set by this module: without the hook the driver refuses to evaluate the real gate
        r = run_ps([self.drv, "-ExeDir", self.tmp, "-IdleWaitSeconds", "0"], {})
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("test-suite guard", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_keyboard_input_requires_user_present(self):
        r = self.drive("-KeyboardInput", force=False)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("requires -UserPresent", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_no_idle_seconds_parameter(self):
        # the 300 s requirement cannot be lowered: unknown arguments are refused, not ignored
        r = self.drive("-IdleSeconds", "60", "-IdleWaitSeconds", "0")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("unknown arguments", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_positional_arguments_are_refused(self):
        r = run_ps([self.drv, "5", "23:E", "", "", "x", "-ExeDir", self.tmp, "-IdleWaitSeconds", "0"],
                   {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("unknown arguments", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_user_present_must_be_spelled_in_full(self):
        for abbr in ("-UserP", "-UserPres", "-UserPresen"):
            r = self.drive(abbr, force=True)
            self.assertNotEqual(r.returncode, 0, abbr)
            self.assertNotIn("SKIPPED", r.stdout, abbr)
            self.assertNotIn("started pid", r.stdout, abbr)
        r = self.drive("-UserPresentAbbreviationGuard", force=True)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_user_present_skips_gate_and_reports_aborted_without_exe(self):
        # an empty folder: the launch cannot happen, so this proves the skip and the aborted path only
        r = self.drive("-UserPresent", force=False)
        self.assertIn("SKIPPED", r.stdout)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("RUN ABORTED", r.stdout.strip().splitlines()[-1])
        self.assertIn("USER PRESENT OVERRIDE", (Path(self.tmp) / "logs" / "drive_game_runs.log").read_text())

    def test_gate_failure_is_a_refusal(self):
        (Path(self.tmp) / "noidle").mkdir()
        d = patched_driver(Path(self.tmp) / "noidle", [self.tmp], with_idle=False)
        r = run_ps([d, "-ExeDir", self.tmp, "-IdleWaitSeconds", "0"], {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIn("idle gate failed", r.stdout)

    def test_test_mode_cannot_launch_the_game(self):
        """Every guard of the test parameters, each with real files so no other guard can mask it."""
        t = Path(self.tmp)
        h = t / "h.ps1"
        h.write_text("@{}", encoding="utf-8")
        exe_ok = t / "exe_ok"; exe_ok.mkdir()
        named = t / "my_fable_copy.exe"; shutil.copy(SORT, named)
        exe_game = t / "exe_game"; exe_game.mkdir(); shutil.copy(SORT, exe_game / "fable_2.exe")
        folder = t / "gamedir"; folder.mkdir(); shutil.copy(SORT, folder / "standin.exe"); (folder / "rexruntime.dll").write_text("x")
        plain = t / "plain.exe"; shutil.copy(SORT, plain)
        cases = [
            ("named like the game", ["-ExeDir", exe_ok, "-TestHarness", h, "-TestExe", named]),
            ("contains fable_2.exe", ["-ExeDir", exe_game, "-TestHarness", h, "-TestExe", plain]),
            ("game folder", ["-ExeDir", exe_ok, "-TestHarness", h, "-TestExe", folder / "standin.exe"]),
            ("explicit -ExeDir", ["-TestHarness", h, "-TestExe", plain]),
            ("-GameArgs", ["-ExeDir", exe_ok, "-TestHarness", h, "-TestExe", plain, "-GameArgs", "x"]),
            ("together", ["-ExeDir", exe_ok, "-TestHarness", h]),
            ("together", ["-ExeDir", exe_ok, "-TestExe", plain]),
            ("-UserPresent", ["-ExeDir", exe_ok, "-TestHarness", h, "-TestExe", plain, "-UserPresent"]),
        ]
        if REAL_GAME.exists():
            copy = t / "standin_bytes.exe"
            shutil.copy(REAL_GAME, copy)   # a copy of the built game under a neutral name
            cases.append(("the game's bytes", ["-ExeDir", exe_ok, "-TestHarness", h, "-TestExe", copy]))
        for want, extra in cases:
            r = run_ps([DRIVE, "-Total", "5", *extra], {})
            self.assertEqual(r.returncode, 2, (want, r.stdout, r.stderr))
            self.assertIn(want, r.stdout)
            self.assertNotIn("started pid", r.stdout)

    def test_driver_structure(self):
        src = DRIVE.read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"Start-Process|Invoke-Item|Diagnostics\.Process\]::Start", src)), 0)
        starts = [m.start() for m in re.finditer(r"\[DriveGameNative\]::StartSuspended\(", src)]
        self.assertEqual(len(starts), 1, "the driver must have exactly one launch path")
        self.assertLess(src.index("Wait-Fable2Away"), starts[0], "the gate must come before the launch")
        self.assertLess(starts[0], src.index("AssignToJob(", starts[0]))
        self.assertLess(src.index("AssignToJob(", starts[0]), src.index("StartDeadMan(", starts[0]))   # armed before the game runs
        self.assertLess(src.index("StartDeadMan(", starts[0]), src.index("Resume(", starts[0]))
        self.assertNotRegex(src, r"\[int\]\$IdleSeconds\b")
        for text in ("RUN CLEAN: no user input for", "RUN DISTURBED:", "RUN_DISTURBED.txt", "GAME EXITED EARLY", "TEST HARNESS "):
            self.assertIn(text, src)


QUOTE_CASES = [
    "plain", "a b", "a b\\", "c:\\path with space\\", "say \"hi\"", "", "x=y", "a,b", "\"", "\\", "\\\\", "a\\\\\"b",
    "tab\there", "--fullscreen=false", "--opt=a b", "end\\\\", "\"quoted\"", "a \"b c\" d", "-NoProfile", "x;y",
]


@unittest.skipUnless(PWSH, "PowerShell not available")
class QuotingTest(unittest.TestCase):
    """ConvertTo-CommandLineArgument round-trips through the real CommandLineToArgvW."""

    def test_round_trip(self):
        q = str(TOOLS / "cmdline_quote.ps1").replace("\\", "/")
        body = (r"""
. '%s'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class Fable2Argv {
  [DllImport("shell32.dll", SetLastError = true)] static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string cl, out int n);
  public static string[] Split(string cl) {
    int n; IntPtr p = CommandLineToArgvW(cl, out n); string[] r = new string[n];
    for (int i = 0; i < n; i++) r[i] = Marshal.PtrToStringUni(Marshal.ReadIntPtr(p, i * IntPtr.Size));
    return r; }
}
'@
$cases = '%s' | ConvertFrom-Json
$bad = @()
foreach ($c in $cases) {
  $line = 'prog ' + (ConvertTo-CommandLineArgument $c)
  $argv = [Fable2Argv]::Split($line)
  if ($argv.Count -ne 2 -or $argv[1] -ne $c) { $bad += "single [$c] -> [$line]" }
}
$all = 'prog ' + (($cases | ForEach-Object { ConvertTo-CommandLineArgument $_ }) -join ' ')
$argv = [Fable2Argv]::Split($all)
if ($argv.Count -ne $cases.Count + 1) { $bad += "table count" } else { for ($i = 0; $i -lt $cases.Count; $i++) { if ($argv[$i + 1] -ne $cases[$i]) { $bad += "table [$($cases[$i])]" } } }
'bad: ' + $bad.Count
$bad
""" % (q, json.dumps(QUOTE_CASES).replace("'", "''")))
        out = run_lib_plain(body)
        self.assertIn("bad: 0", out, out)


def run_lib_plain(body, timeout=120):
    r = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-Command", body], capture_output=True, text=True, timeout=timeout)
    assert r.returncode == 0, r.stdout + r.stderr
    return r.stdout


def run_cmd(shell, command, env_extra=None, timeout=300, cwd=None):
    env = dict(os.environ)
    env.pop("FABLE2_IDLE_GATE_FORCE_PRESENT", None)
    env.update(env_extra or {})
    return subprocess.run([shell, "-NoProfile", "-NonInteractive", "-Command", command],
                          capture_output=True, text=True, timeout=timeout, env=env, cwd=str(cwd or ROOT))


PS51 = shutil.which("powershell")


@unittest.skipUnless(PWSH, "PowerShell not available")
class InSessionTest(unittest.TestCase):
    """The driver started the way the README shows, from a PowerShell session (not `-File`)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="drive_sess_"))
        self.addCleanup(shutil.rmtree, str(self.tmp), True)
        (self.tmp / "exe").mkdir()
        self.harness = self.tmp / "harness.ps1"
        self.harness.write_text(HARNESS, encoding="utf-8")
        self.drv = patched_driver(self.tmp, [self.tmp / "exe"])
        self.env = {"TEST_OUT": str(self.tmp / "o.txt"), "TEST_OUT2": str(self.tmp / "o2.txt")}

    def q(self, p):
        return "'%s'" % str(p).replace("'", "''")

    def refusal(self, shell):
        cmd = ("& %s -ExeDir %s -IdleWaitSeconds 0 -Env @{A='b'}; 'after: ' + $LASTEXITCODE" % (self.q(self.drv), self.q(self.tmp / "exe")))
        r = run_cmd(shell, cmd, {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        self.assertIn("forced-present", r.stdout, r.stdout + r.stderr)       # the hook's signal ...
        self.assertNotIn("not recognized", r.stdout + r.stderr)              # ... not a scope error
        self.assertIn("after: 10", r.stdout)                                  # exit does not kill the session
        self.assertNotIn("started pid", r.stdout)

    def harness_run(self, shell, scenario, total, sleep=30):
        pw = shutil.which("pwsh") or PWSH
        cmd = ("& %s -ExeDir %s -Total %d -Plan '' -TestHarness %s -TestExe %s -TestExeArgs '|-NoProfile|-Command|Start-Sleep %d'; "
               "'after: ' + $LASTEXITCODE" % (self.q(DRIVE), self.q(self.tmp / "exe"), total, self.q(self.harness), self.q(pw), sleep))
        return run_cmd(shell, cmd, dict(self.env, TEST_SCENARIO=scenario))

    def harness_clean(self, shell):
        r = self.harness_run(shell, "clean", 3)
        lines = r.stdout.strip().splitlines()
        self.assertRegex(lines[-2], r"^TEST HARNESS RUN CLEAN: no user input for \d+ s$", r.stdout + r.stderr)
        self.assertEqual(lines[-1], "after: 0")

    def stall_total2(self, shell):
        # the dead-man thread kills the stand-in during the stall; the run must still not end clean
        r = self.harness_run(shell, "stall", 2, sleep=60)
        lines = r.stdout.strip().splitlines()
        self.assertRegex(lines[-2], r"^TEST HARNESS RUN ABORTED: dead-man switch", r.stdout + r.stderr)
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertEqual(lines[-1], "after: 12")

    def bad_plan(self, shell):
        # a bad plan is a refusal with exit code 2 (not a script error that leaves a stale $LASTEXITCODE behind)
        cmd = ("$global:LASTEXITCODE = 0; & %s -ExeDir %s -IdleWaitSeconds 0 -Plan '23:E:27' -Shots '50'; 'after: ' + $LASTEXITCODE"
               % (self.q(self.drv), self.q(self.tmp / "exe")))
        r = run_cmd(shell, cmd, {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"})
        lines = r.stdout.strip().splitlines()
        self.assertRegex(lines[-2], r"^REFUSED: bad plan: .*Nothing launched\.$", r.stdout + r.stderr)
        self.assertEqual(lines[-1], "after: 2")
        self.assertNotIn("forced-present", r.stdout)

    def test_bad_plan_in_session_pwsh(self):
        self.bad_plan(PWSH)

    @unittest.skipUnless(PS51, "Windows PowerShell 5.1 not available")
    def test_bad_plan_in_session_ps51(self):
        self.bad_plan(PS51)

    def test_refusal_in_session_pwsh(self):
        self.refusal(PWSH)

    def test_harness_run_in_session_pwsh(self):
        self.harness_clean(PWSH)

    def test_stall_with_short_total_in_session_pwsh(self):
        self.stall_total2(PWSH)

    @unittest.skipUnless(PS51, "Windows PowerShell 5.1 not available")
    def test_refusal_in_session_ps51(self):
        self.refusal(PS51)

    @unittest.skipUnless(PS51, "Windows PowerShell 5.1 not available")
    def test_harness_run_in_session_ps51(self):
        self.harness_clean(PS51)

    @unittest.skipUnless(PS51, "Windows PowerShell 5.1 not available")
    def test_stall_with_short_total_in_session_ps51(self):
        self.stall_total2(PS51)

    def test_env_is_refused_in_test_mode(self):
        pw = shutil.which("pwsh") or PWSH
        cmd = ("& %s -ExeDir %s -Env @{A='b'} -TestHarness %s -TestExe %s; 'after: ' + $LASTEXITCODE"
               % (self.q(DRIVE), self.q(self.tmp / "exe"), self.q(self.harness), self.q(pw)))
        r = run_cmd(PWSH, cmd)
        self.assertIn("-Env", r.stdout)
        self.assertIn("after: 2", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_surviving_game_is_killed_when_the_driver_returns_in_session(self):
        """m2: the job handle is closed on the way out, so a game that could not be killed does not outlive the call."""
        pw = shutil.which("pwsh") or PWSH
        cmd = ("$o = & %s -ExeDir %s -Total 2 -Plan '' -TestHarness %s -TestExe %s -TestExeArgs '|-NoProfile|-Command|Start-Sleep 60'; "
               "$m = [regex]::Match(($o -join \"`n\"), 'started pid (\\d+)'); Start-Sleep -Milliseconds 800; "
               "'alive: ' + [bool](Get-Process -Id ([int]$m.Groups[1].Value) -ErrorAction SilentlyContinue)"
               % (self.q(DRIVE), self.q(self.tmp / "exe"), self.q(self.harness), self.q(pw)))
        r = run_cmd(PWSH, cmd, dict(self.env, TEST_SCENARIO="unkillable"))
        self.assertIn("alive: False", r.stdout, r.stdout + r.stderr)

    def test_relative_exedir_matches_the_allow_list_after_resolution(self):
        cmd = ("Set-Location %s; & %s -ExeDir .\\exe -IdleWaitSeconds 0; 'after: ' + $LASTEXITCODE" % (self.q(self.tmp), self.q(self.drv)))
        r = run_cmd(PWSH, cmd, {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"}, cwd=ROOT)
        self.assertIn("forced-present", r.stdout, r.stdout + r.stderr)   # reached the gate: the relative path was resolved first
        self.assertIn("after: 10", r.stdout)

    def test_started_line_is_logged_before_it_is_printed(self):
        """m3: a pipeline that stops the script at the started line still leaves the log line."""
        pw = shutil.which("pwsh") or PWSH
        cmd = ("& %s -ExeDir %s -Total 30 -Plan '' -TestHarness %s -TestExe %s -TestExeArgs '|-NoProfile|-Command|Start-Sleep 60' "
               "| Select-Object -First 3; 'done'" % (self.q(DRIVE), self.q(self.tmp / "exe"), self.q(self.harness), self.q(pw)))
        r = run_cmd(PWSH, cmd, dict(self.env, TEST_SCENARIO="clean"))
        self.assertIn("started pid", r.stdout, r.stdout + r.stderr)
        log = (self.tmp / "exe" / "logs" / "drive_game_runs.log").read_text()
        self.assertIn("started pid", log)

    def test_relative_paths_resolve_against_the_session_location(self):
        """m4: -ExeDir, -TestHarness and -TestExe are resolved once, against the caller's location (not the process directory)."""
        shutil.copy(SORT, self.tmp / "standin.exe")
        cmd = ("Set-Location %s; & %s -ExeDir .\\exe -Total 3 -Plan '' -TestHarness .\\harness.ps1 -TestExe .\\standin.exe; "
               "'after: ' + $LASTEXITCODE" % (self.q(self.tmp), self.q(DRIVE)))
        r = run_cmd(PWSH, cmd, dict(self.env, TEST_SCENARIO="clean"), cwd=ROOT)
        self.assertNotIn("REFUSED", r.stdout, r.stdout + r.stderr)
        started = [l for l in r.stdout.splitlines() if l.startswith("started pid")]
        self.assertEqual(len(started), 1, r.stdout + r.stderr)
        self.assertIn(str(self.tmp / "standin.exe").lower(), started[0].lower())      # the absolute path in the started line
        self.assertTrue((self.tmp / "exe" / "logs" / "drive_game_runs.log").exists())  # the log is under the resolved -ExeDir


PLAN_TABLE_PS = r"""
. '%s'
$pad = @{ "E"="A"; "SPACE"="A"; "S"="Down"; "W"="Up"; "A"="Left"; "D"="Right"; "ESC"="B"; "ENTER"="Start" }
$keys = @{ "E"=0x45; "S"=0x53; "F6"=0x75 }
$cases = '%s' | ConvertFrom-Json
$out = foreach ($c in $cases) {
  try {
    if ($c.kind -eq 'plan') {
      $r = ConvertTo-Fable2Plan -Plan $c.text -KeyboardInput ([bool]$c.keyboard) -Shift ([double]$c.shift) -PadButtons $pad -Keys $keys
      $got = if ($c.keyboard) { (@($r.Events | ForEach-Object { "$($_.t)=$($_.k)" }) -join ';') } else { "$($r.Count)|$($r.Autoplay)" }
    } elseif ($c.kind -eq 'shots') {
      $got = (@(ConvertTo-Fable2ShotTimes -Shots $c.text -Shift ([double]$c.shift)) -join ';')
    } else {
      $got = [string](ConvertTo-Fable2PlanShift -Text $c.text)
    }
    [pscustomobject]@{ ok = $true; got = $got }
  } catch { [pscustomobject]@{ ok = $false; got = $_.Exception.Message } }
}
ConvertTo-Json -Compress -InputObject @($out)
"""


@unittest.skipUnless(PWSH, "PowerShell not available")
class PlanTableTest(unittest.TestCase):
    """tools/autoplay_plan.ps1: the driver's reading of -Plan, -Shots and -PlanShift, in both PowerShells.
    The driver accepts what the game's parser accepts (src/input/autoplay.h; its rejected tokens are those of
    tests/native/test_autoplay.cpp) and refuses the rest by name."""

    ACCEPT = [
        ("plan", "23:E,27:S,30:E,35:E", 0, False, "4|23:A,27:Down,30:A,35:A"),
        ("plan", " 23 : e ,, 27.50:s ,", 0, False, "2|23:A,27.5:Down"),              # blanks and empty entries, as the game
        ("plan", "5.:W,.5:D,0:ESC,7:enter,8:Space,9:a", 0, False, "6|5:Up,0.5:Right,0:B,7:Start,8:A,9:Left"),
        ("plan", "55:LS=0/100/3000,59:rs=-60/0/1500,52:VIEW=Off,66:disc=60/4,70:view=PATTERN", 0, False,
         "5|55:LS=0/100/3000,59:rs=-60/0/1500,52:VIEW=Off,66:disc=60/4,70:view=PATTERN"),
        ("plan", "1:LS=-100/100/60000,2:RS=000100/-0/000001,3:DISC=100000/64,4:DISC=1/1", 0, False,
         "4|1:LS=-100/100/60000,2:RS=000100/-0/000001,3:DISC=100000/64,4:DISC=1/1"),
        ("plan", "23:E,27:S,54.25:VIEW=native,55:LS=0/100/3000", 6, False, "4|29:A,33:Down,60.25:VIEW=native,61:LS=0/100/3000"),
        ("plan", "0:E,86400:S", 30, False, "2|30:A,86430:Down"),
        ("plan", "", 0, False, "0|"),
        ("plan", " , ,", 6, False, "0|"),
        ("plan", "1:e,2.5:F6", 2, True, "3=E;4.5=F6"),
        ("shots", "50,80", 0, False, "50;80"),
        ("shots", " 54 ,,58.5,", 6, False, "60;64.5"),
        ("shots", "", 6, False, ""),
        ("shift", "0", 0, False, "0"), ("shift", "6", 0, False, "6"), ("shift", " 30 ", 0, False, "30"), ("shift", "06", 0, False, "6"),
    ]
    REJECT = [
        # the time field: digits with at most one '.', nothing else
        ("plan", "x:E", "not a time in seconds"), ("plan", "-1:E", "not a time in seconds"), ("plan", "+1:E", "not a time in seconds"),
        ("plan", "1e3:E", "not a time in seconds"), ("plan", "NaN:E", "not a time in seconds"), ("plan", "Infinity:E", "not a time in seconds"),
        ("plan", "1.2.3:E", "not a time in seconds"), ("plan", ":E", "not a time in seconds"), ("plan", ".:E", "not a time in seconds"),
        ("plan", "0x10:E", "not a time in seconds"), ("plan", "1 0:E", "not a time in seconds"), ("plan", "100000:E", "more than 86400"),
        # exactly seconds:key
        ("plan", "23", "exactly one ':'"), ("plan", "23:E:extra", "exactly one ':'"), ("plan", "23:E:", "exactly one ':'"),
        ("plan", "23:LS=0/100/3000:1", "exactly one ':'"),
        ("plan", "23:", "no pad button"), ("plan", "23:Q", "no pad button"), ("plan", "23:Down", "no pad button"),
        # one bad entry refuses the plan
        ("plan", "23:E,x:S,30:E", "not a time in seconds"),
        # sticks (the game's rejects, and its digit limit)
        ("plan", "5:LS=0/100", "none of"), ("plan", "5:LS=0/100/0", "bad stick entry"), ("plan", "5:LS=101/0/100", "bad stick entry"),
        ("plan", "5:LS=0/-101/100", "bad stick entry"), ("plan", "5:LS=0/100/60001", "bad stick entry"),
        ("plan", "5:LS=0/100/10/1", "none of"), ("plan", "5:LS=a/0/100", "none of"), ("plan", "5:XS=0/0/100", "none of"),
        ("plan", "5:LS=", "none of"), ("plan", "5:LS=0//100", "none of"), ("plan", "5:LS=0000050/0/100", "none of"),
        ("plan", "5:LS=0/0/0000100", "none of"), ("plan", "5:LS=+5/0/100", "none of"), ("plan", "5:LS=0/0/-100", "none of"),
        ("plan", "5:LS=0 /0/100", "none of"),
        # view and capture
        ("plan", "5:VIEW=", "none of"), ("plan", "5:VIEW=sideways", "none of"), ("plan", "5:VIEW=native/1", "none of"),
        ("plan", "5:VIEWS=off", "none of"), ("plan", "5:DISC=120", "none of"), ("plan", "5:DISC=0/4", "bad capture entry"),
        ("plan", "5:DISC=120/0", "bad capture entry"), ("plan", "5:DISC=120/65", "bad capture entry"),
        ("plan", "5:DISC=100001/4", "bad capture entry"), ("plan", "5:DISC=a/4", "none of"), ("plan", "5:DISC=120/4/1", "none of"),
        ("plan", "5:DISC=", "none of"), ("plan", "5:DISC=/4", "none of"), ("plan", "5:DISC=120/", "none of"),
        ("plan", "5:DISC=1234567/4", "none of"), ("plan", "5:X=1", "none of"),
        ("plan", "5:VIEW", "no pad button"),                                  # (without "=" it is a key name here)
        # shots and the shift
        ("shots", "NaN", "-Shots time"), ("shots", "50,NaN", "-Shots time"), ("shots", "-5", "-Shots time"),
        ("shots", "abc", "-Shots time"), ("shots", "1e2", "-Shots time"), ("shots", "Infinity", "-Shots time"),
        ("shots", "5;6", "-Shots time"), ("shots", "90000", "more than 86400"),
        ("shift", "31", "-PlanShift"), ("shift", "-1", "-PlanShift"), ("shift", "2.5", "-PlanShift"), ("shift", "x", "-PlanShift"),
        ("shift", "", "-PlanShift"), ("shift", "100", "-PlanShift"), ("shift", "1e1", "-PlanShift"), ("shift", "NaN", "-PlanShift"),
    ]
    KEYBOARD_REJECT = [("plan", "1:LS=0/0/100", "unknown key"), ("plan", "1:Q", "unknown key"), ("plan", "x:E", "not a time in seconds")]

    def table(self, shell):
        cases = [dict(kind=k, text=t, shift=s, keyboard=kb) for k, t, s, kb, _ in self.ACCEPT]
        cases += [dict(kind=k, text=t, shift=0, keyboard=False) for k, t, _ in self.REJECT]
        cases += [dict(kind=k, text=t, shift=0, keyboard=True) for k, t, _ in self.KEYBOARD_REJECT]
        body = PLAN_TABLE_PS % (str(PLAN).replace("\\", "/"), json.dumps(cases).replace("'", "''"))
        r = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-Command", body], capture_output=True, text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        got = json.loads(r.stdout.strip().splitlines()[-1])
        self.assertEqual(len(got), len(cases))
        n = len(self.ACCEPT)
        for (kind, text, shift, kb, want), g in zip(self.ACCEPT, got[:n]):
            self.assertTrue(g["ok"], (kind, text, g))
            self.assertEqual(g["got"], want, (kind, text, shift))
        for (kind, text, why), g in zip(self.REJECT + self.KEYBOARD_REJECT, got[n:]):
            self.assertFalse(g["ok"], (kind, text, g))
            self.assertIn(why, g["got"], (kind, text))
            self.assertNotIn("\n", g["got"])

    def test_table_pwsh(self):
        self.table(PWSH)

    @unittest.skipUnless(PS51, "Windows PowerShell 5.1 not available")
    def test_table_ps51(self):
        self.table(PS51)

    def test_the_games_rejected_tokens_are_in_the_table(self):
        # Every token tests/native/test_autoplay.cpp expects the game to reject is refused here too.
        src = (ROOT / "tests" / "native" / "test_autoplay.cpp").read_text(encoding="utf-8")
        tokens = set()
        for block in re.findall(r"for \(const char\* bad : \{(.*?)\}\)", src, re.S):
            tokens |= set(re.findall(r'"([^"]*)"', block))
        self.assertGreaterEqual(len(tokens), 20)
        mine = {t for _, t, _ in self.REJECT}
        self.assertEqual(sorted(t for t in tokens if t not in mine), [])


@unittest.skipUnless(PWSH, "PowerShell not available")
class SyntaxTest(unittest.TestCase):
    def test_scripts_parse(self):
        for script in (IDLE, DRIVE, PLAN):
            code = ("$e = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('%s', [ref]$null, [ref]$e); "
                    "$e.Count" % str(script).replace("\\", "/"))
            r = subprocess.run([PWSH, "-NoProfile", "-Command", code], capture_output=True, text=True)
            self.assertEqual(r.stdout.strip(), "0", (script, r.stdout, r.stderr))


@unittest.skipUnless(PWSH, "PowerShell not available")
class LauncherCommentTest(unittest.TestCase):
    def test_other_launchers_carry_the_agent_rule(self):
        for name in ("fable2", "fable2-uncapped", "fable2-functrace", "StartWithoutAudio", "crash_capture"):
            head = (ROOT / "tools" / (name + ".cmd")).read_text(encoding="utf-8", errors="replace").splitlines()[:3]
            self.assertTrue(any(l.lower().startswith("rem ") and "drive_game.ps1" in l and "agent" in l.lower() for l in head), name)


if __name__ == "__main__":
    unittest.main()
