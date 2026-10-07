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
TOOLS = Path(os.environ.get("FABLE2_TOOLS_DIR") or ROOT / "tools")
IDLE = TOOLS / "idle_check.ps1"
DRIVE = TOOLS / "drive_game.ps1"
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
  $sleep = { param($ms) $t.v += $ms / 1000.0 }.GetNewClosure()
  try {
    $r = Wait-Fable2Away -MinIdleSeconds $sc.min -MaxWaitSeconds $sc.wait -Probe $probe -Clock $clock -Sleep $sleep
    [pscustomobject]@{ name = $sc.name; passed = $r.Passed; at = ($t.v - 1000.0); signals = (@($r.Signals) -join ',') }
  } catch { [pscustomobject]@{ name = $sc.name; passed = $false; at = -1; signals = 'THREW: ' + $_.Exception.Message } }
}
$out | ConvertTo-Json -Compress -AsArray""" % json.dumps(scenarios)
        res = json.loads(run_lib(body).strip().splitlines()[-1])
        return {r["name"]: r for r in res}

    @staticmethod
    def sc(name, **kw):
        base = dict(name=name, min=60, wait=400, input=[], idle_fail=[], pad_connected=[], pad_activity=[],
                    pad_change=[], audio=[], fullscreen=[], fail=[], running=False)
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
        ])
        self.assertTrue(res["clean"]["passed"]); self.assertTrue(9.9 <= res["clean"]["at"] <= 11, res["clean"])
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
        self.assertTrue(10 <= res["input_before"]["at"] <= 20, res["input_before"])  # idle 50 s at the start, 60 s required
        # a failing probe is never read as idle
        for k in ("idle_fail", "probe_fail", "fullscreen"):
            self.assertTrue(res[k]["passed"] and res[k]["at"] >= 50, res[k])
        self.assertFalse(res["running"]["passed"]); self.assertIn("fable2-running", res["running"]["signals"])
        self.assertTrue(res["audio"]["passed"]); self.assertTrue(res["audio"]["at"] >= 22, res["audio"])
        self.assertIn("THREW", res["floor"]["signals"]); self.assertIn("at least 60", res["floor"]["signals"])


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
            ({"AudioQuietSeconds": 9.9}, True, "audio"),
            ({"AudioQuietSeconds": 10}, False, ""),
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

    def test_type_is_versioned(self):
        out = run_lib("[Fable2IdleProbeV2]::ProbeVersion")
        self.assertEqual(out.strip(), "2")


HARNESS = r"""
# Test harness for drive_game.ps1: a scripted probe, a fake gate clock and test hooks.
$scenario = $env:TEST_SCENARIO
$fake = @{ adv = 0.0; sw = [Diagnostics.Stopwatch]::StartNew(); tick = 1000; pad = $false; fgThrow = $false; written = $false }
$n = New-Object psobject
$n | Add-Member ScriptMethod Now { 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds }.GetNewClosure()
$n | Add-Member ScriptMethod LastInputInfo {
  $now = 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds
  $idle = if ($env:TEST_SCENARIO -eq 'recent_input') { 280.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds } else { 100000.0 }
  @([double]$idle, [double]$fake.tick) }.GetNewClosure()
$n | Add-Member ScriptMethod UptimeSeconds { 1000000.0 }
$n | Add-Member ScriptMethod Latch {
  $now = 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds
  $act = if ($fake.pad) { $now } else { -1.0 }
  [double[]]@(0.0, $act, -1.0, -1.0, -1.0, -1.0, $now, 1.0) }.GetNewClosure()
$n | Add-Member ScriptMethod NotificationState { 5 }
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
  if ($elapsed -ge 2) {
    switch ($env:TEST_SCENARIO) {
      'disturb_input' { $fake.tick = 99999 }
      'disturb_pad' { $fake.pad = $true }
      'disturb_probe' { $fake.fgThrow = $true }
      'abort' { throw 'boom (test abort)' }
    }
  }
}.GetNewClosure()
@{ Native = $n
   Clock = { 1000.0 + $fake.adv + $fake.sw.Elapsed.TotalSeconds }.GetNewClosure()
   Sleep = { param($ms) $fake.adv += $ms / 1000.0 }.GetNewClosure()
   Tick = $tick
   NoKill = ($env:TEST_SCENARIO -eq 'unkillable') }
"""


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
        self.pids = []
        self.addCleanup(self._cleanup)

    def _cleanup(self):
        for p in self.pids:
            kill_pid(p)

    def args(self, scenario, sleep=60, total=4, extra=()):
        self.env = {"TEST_SCENARIO": scenario, "TEST_OUT": str(self.out)}
        return [DRIVE, "-ExeDir", self.exe, "-Total", str(total), "-Plan", "", "-IdleWaitSeconds", "1800",
                "-TestHarness", self.harness, "-TestExe", shutil.which("pwsh") or PWSH,
                "-TestExeArgs", "-NoProfile|-Command|Start-Sleep %d" % sleep, *extra]

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

    def test_clean_run(self):
        r, pid = self.drive("clean", total=3)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN CLEAN: no user input for \d+ s$")
        self.assertIn("at least 300 s", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "stand-in must be closed")
        self.assertFalse((self.exe / "logs" / "RUN_DISTURBED.txt").exists())
        self.assertIn("exit=0", self.runlog())

    def test_gate_requires_300_seconds(self):
        # last input 280 s before the start: the gate must wait for 20 more (fake) seconds
        r, pid = self.drive("recent_input", total=2)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertGreaterEqual(float(self.out.read_text()), 20.0, r.stdout)

    def test_disturbed_by_input(self):
        shots = self.tmp / "shots"
        r, pid = self.drive("disturb_input", total=30, extra=("-Shots", "50", "-ShotDir", shots))
        self.assertEqual(r.returncode, 11, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN DISTURBED: input at \d+\.\d s$")
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

    def test_disturbed_by_pad(self):
        r, pid = self.drive("disturb_pad", total=30)
        self.assertEqual(r.returncode, 11, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN DISTURBED: controller at ")
        self.assertTrue((self.exe / "logs" / "RUN_DISTURBED.txt").exists())  # no shots: next to the log
        self.assertTrue(wait_gone(pid, 3))

    def test_watch_error_is_a_disturbance(self):
        r, pid = self.drive("disturb_probe", total=30)
        self.assertEqual(r.returncode, 11, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN DISTURBED: unknown at ")
        self.assertTrue(wait_gone(pid, 3))

    def test_error_during_run_aborts_and_closes_game(self):
        r, pid = self.drive("abort", total=30)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN ABORTED: ")
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertTrue(wait_gone(pid, 3), "the finally block must close the game")

    def test_game_that_survives_the_kill_is_not_clean(self):
        r, pid = self.drive("unkillable", total=3)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertRegex(self.last(r), r"^RUN ABORTED: the game is still running after the kill")
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_early_exit_is_not_clean(self):
        r, pid = self.drive("clean", sleep=1, total=8)
        self.assertEqual(r.returncode, 13, r.stdout + r.stderr)
        self.assertIn("GAME EXITED EARLY at", r.stdout)
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_gate_refusal_launches_nothing(self):
        r, pid = self.drive("present", total=4)
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIsNone(pid)
        self.assertIn("REFUSED", r.stdout)
        self.assertIn("fable2-running", r.stdout)
        self.assertIn("exit=10", self.runlog())

    def test_game_dies_with_the_driver(self):
        """C1: killing the driver process itself must take the game with it (job object)."""
        env = dict(os.environ, **{"TEST_SCENARIO": "clean", "TEST_OUT": str(self.out)})
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


@unittest.skipUnless(PWSH, "PowerShell not available")
class DriverGateTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="drive_gate_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.assertFalse((Path(self.tmp) / "fable_2.exe").exists())

    def drive(self, *extra, force=True):
        env = {"FABLE2_IDLE_GATE_FORCE_PRESENT": "1"} if force else {}
        return run_ps([DRIVE, "-ExeDir", self.tmp, "-Total", "5", *extra], env)

    def test_refuses_when_present(self):
        before = fable_pids()
        r = self.drive("-IdleWaitSeconds", "0")
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIn("REFUSED", r.stdout)
        self.assertIn("forced-present", r.stdout)
        self.assertNotIn("started pid", r.stdout)
        self.assertNotIn("RUN CLEAN", r.stdout)
        self.assertEqual(fable_pids(), before)
        self.assertIn("exit=10", (Path(self.tmp) / "logs" / "drive_game_runs.log").read_text())

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

    def test_user_present_must_be_spelled_in_full(self):
        for abbr in ("-UserP", "-UserPres", "-UserPresen"):
            r = self.drive(abbr, force=False)
            self.assertNotEqual(r.returncode, 0, abbr)
            self.assertNotIn("SKIPPED", r.stdout, abbr)
            self.assertNotIn("started pid", r.stdout, abbr)
        r = self.drive("-UserPresentAbbreviationGuard", force=False)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_user_present_skips_gate_and_reports_aborted_without_exe(self):
        r = self.drive("-UserPresent")
        self.assertIn("SKIPPED", r.stdout)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("RUN ABORTED", r.stdout.strip().splitlines()[-1])
        self.assertIn("USER PRESENT OVERRIDE", (Path(self.tmp) / "logs" / "drive_game_runs.log").read_text())

    def test_gate_failure_is_a_refusal(self):
        d = Path(self.tmp) / "tools"
        d.mkdir()
        shutil.copy(DRIVE, d / "drive_game.ps1")
        r = run_ps([d / "drive_game.ps1", "-ExeDir", self.tmp, "-IdleWaitSeconds", "0"])
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIn("idle gate failed", r.stdout)

    def test_test_mode_cannot_launch_the_game(self):
        pw = shutil.which("pwsh") or PWSH
        h = Path(self.tmp) / "h.ps1"
        h.write_text("@{}", encoding="utf-8")
        cases = [
            ["-TestHarness", h, "-TestExe", Path(self.tmp) / "fable_2.exe"],            # real name
            ["-TestHarness", h, "-TestExe", Path(self.tmp) / "Fable_2.EXE"],
            ["-TestHarness", h, "-TestExe", Path(self.tmp) / "my_fable_copy.exe"],      # looks like the game
            ["-TestHarness", h],                                                      # harness without exe
            ["-TestExe", pw],                                                         # exe without harness
            ["-TestHarness", h, "-TestExe", pw, "-UserPresent"],                      # never with the override
            ["-TestHarness", h, "-TestExe", pw, "-KeyboardInput"],
        ]
        for extra in cases:
            r = self.drive(*extra, force=False)
            self.assertEqual(r.returncode, 2, (extra, r.stdout, r.stderr))
            self.assertNotIn("started pid", r.stdout)

    def test_driver_structure(self):
        src = DRIVE.read_text(encoding="utf-8")
        starts = [m.start() for m in re.finditer(r"Start-Process", src)]
        self.assertEqual(len(starts), 1, "the driver must have exactly one launch path")
        self.assertLess(src.index("Wait-Fable2Away"), starts[0], "the gate must come before the launch")
        self.assertLess(src.index("AssignToJob"), src.index("Invoke-WatchPoll", starts[0]))
        self.assertNotRegex(src, r"\[int\]\$IdleSeconds\b")
        for text in ("RUN CLEAN: no user input for", "RUN DISTURBED:", "RUN_DISTURBED.txt", "GAME EXITED EARLY"):
            self.assertIn(text, src)


@unittest.skipUnless(PWSH, "PowerShell not available")
class SyntaxTest(unittest.TestCase):
    def test_scripts_parse(self):
        for script in (IDLE, DRIVE):
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
