"""Tests for tools/idle_check.ps1 (presence gate) and the gate in tools/drive_game.ps1.

Nothing here launches the game: the driver is always run with -ExeDir pointing at an empty
temporary folder (so even a broken gate could not start fable_2.exe) and the test hook
FABLE2_IDLE_GATE_FORCE_PRESENT=1, which can only add a presence signal.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IDLE = ROOT / "tools" / "idle_check.ps1"
DRIVE = ROOT / "tools" / "drive_game.ps1"
PWSH = shutil.which("pwsh") or shutil.which("powershell")


def run_ps(args, env_extra=None, timeout=120):
    env = dict(os.environ)
    env.pop("FABLE2_IDLE_GATE_FORCE_PRESENT", None)
    env.update(env_extra or {})
    return subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", *map(str, args)],
                          capture_output=True, text=True, timeout=timeout, env=env)


def run_lib(script_body, timeout=120):
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


@unittest.skipUnless(PWSH, "PowerShell not available")
class DecisionTest(unittest.TestCase):
    BASE = dict(IdleSeconds=1000, MinIdleSeconds=300, ControllerConnected=False,
                ControllerQuietSeconds=0, NotificationState=5, Fable2Running=False,
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
            ({"ControllerConnected": True, "ControllerQuietSeconds": 299}, True, "controller"),
            ({"ControllerConnected": True, "ControllerQuietSeconds": 300}, False, ""),
            ({"ControllerConnected": False, "ControllerQuietSeconds": 0}, False, ""),
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

    def test_watch_table(self):
        base = dict(LastInputTick=500, BaselineInputTick=500, ControllerActive=False,
                    NotificationState=5, ForegroundPid=10, GamePid=99, ProbeFailed=False,
                    Session0=False, ForcePresent=False)
        cases = [
            ({}, ""),
            ({"LastInputTick": 501}, "input"),
            ({"ControllerActive": True}, "controller"),
            ({"NotificationState": 3}, "fullscreen"),
            ({"NotificationState": 3, "ForegroundPid": 99}, ""),   # the game's own fullscreen state
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
        cases = [
            (dict(), False),
            (dict(Buttons=1), True),
            (dict(LeftTrigger=30), False), (dict(LeftTrigger=31), True),
            (dict(RightTrigger=30), False), (dict(RightTrigger=31), True),
            (dict(ThumbLX=8000), False), (dict(ThumbLX=8001), True),
            (dict(ThumbLY=-8000), False), (dict(ThumbLY=-8001), True),
            (dict(ThumbRX=8001), True), (dict(ThumbRY=-32768), True),
        ]
        body = ("$cases = '%s' | ConvertFrom-Json\n"
                "$out = foreach ($c in $cases) {\n"
                "  $h = @{}; $c.PSObject.Properties | ForEach-Object { $h[$_.Name] = $_.Value }\n"
                "  [bool](Test-Fable2PadActive @h)\n}\n"
                "$out | ConvertTo-Json -Compress -AsArray" % json.dumps([c[0] or {"Buttons": 0} for c in cases]))
        got = json.loads(run_lib(body))
        for (case, want), g in zip(cases, got):
            self.assertEqual(g, want, case)

    def test_snapshot_probes_do_not_throw_and_are_fast(self):
        out = run_lib("$t = [Diagnostics.Stopwatch]::StartNew(); 1..20 | ForEach-Object { $null = Get-Fable2Snapshot }; "
                      "'ms per snapshot: ' + [int]($t.ElapsedMilliseconds / 20)")
        ms = int(re.search(r"ms per snapshot: (\d+)", out).group(1))
        self.assertLess(ms, 50, out)  # polled every 100 ms inside the driver's timing loop


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
        self.assertEqual(fable_pids(), before)       # no fable_2.exe was started
        self.assertEqual(os.listdir(self.tmp), [])   # and no marker or log was written

    def test_keyboard_input_requires_user_present(self):
        r = self.drive("-KeyboardInput", force=False)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("requires -UserPresent", r.stdout)
        self.assertNotIn("started pid", r.stdout)

    def test_idle_seconds_floor(self):
        r = self.drive("-IdleSeconds", "30")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("at least 60", r.stdout)

    def test_user_present_skips_gate_and_reports_unchecked(self):
        # The exe folder is empty, so nothing can start: the run aborts, never "clean".
        r = self.drive("-UserPresent")
        self.assertIn("-UserPresent", r.stdout)
        self.assertIn("SKIPPED", r.stdout)
        self.assertEqual(r.returncode, 12, r.stdout + r.stderr)
        self.assertIn("RUN ABORTED", r.stdout.strip().splitlines()[-1])
        self.assertNotIn("RUN CLEAN", r.stdout)

    def test_gate_failure_is_a_refusal(self):
        # idle_check.ps1 missing next to the driver: the gate throws, the game must not start.
        d = Path(self.tmp) / "tools"
        d.mkdir()
        shutil.copy(DRIVE, d / "drive_game.ps1")
        r = run_ps([d / "drive_game.ps1", "-ExeDir", self.tmp, "-IdleWaitSeconds", "0"])
        self.assertEqual(r.returncode, 10, r.stdout + r.stderr)
        self.assertIn("idle gate failed", r.stdout)

    def test_driver_structure(self):
        src = DRIVE.read_text(encoding="utf-8")
        starts = [m.start() for m in re.finditer(r"Start-Process", src)]
        self.assertEqual(len(starts), 1, "the driver must have exactly one launch path")
        gate = src.index("Wait-Fable2Away")
        self.assertLess(gate, starts[0], "the gate must come before the launch")
        self.assertIn("RUN CLEAN: no user input for", src)
        self.assertIn("RUN DISTURBED:", src)
        self.assertIn("RUN_DISTURBED.txt", src)


@unittest.skipUnless(PWSH, "PowerShell not available")
class SyntaxTest(unittest.TestCase):
    def test_scripts_parse(self):
        for script in (IDLE, DRIVE):
            code = ("$e = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('%s', [ref]$null, [ref]$e); "
                    "$e.Count" % str(script).replace("\\", "/"))
            r = subprocess.run([PWSH, "-NoProfile", "-Command", code], capture_output=True, text=True)
            self.assertEqual(r.stdout.strip(), "0", (script, r.stdout, r.stderr))


if __name__ == "__main__":
    unittest.main()
