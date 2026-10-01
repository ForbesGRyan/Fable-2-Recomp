import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_runtime_sdk", ROOT / "tools" / "prepare_runtime_sdk.py")
prep = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prep)


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def commit(repo, name):
    (repo / name).write_text(name)
    git(repo, "add", name)
    git(repo, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", name)
    return git(repo, "rev-parse", "HEAD")


class CheckRevisionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        git(self.repo, "init", "-q")
        self.pin = commit(self.repo, "pin")
        self.saved_pin = prep.SDK_PIN
        prep.SDK_PIN = self.pin

    def tearDown(self):
        prep.SDK_PIN = self.saved_pin
        self.tmp.cleanup()

    def test_accepts_exact_pin(self):
        prep.check_revision(self.repo)

    def test_accepts_descendant_of_pin(self):
        commit(self.repo, "renderer-work")
        prep.check_revision(self.repo)

    def test_should_apply_patches_on_exact_pin(self):
        self.assertTrue(prep.should_apply_patches(self.repo))

    def test_should_not_apply_patches_on_descendant(self):
        commit(self.repo, "renderer-work")
        self.assertFalse(prep.should_apply_patches(self.repo))

    def test_rejects_unrelated_history(self):
        git(self.repo, "checkout", "-q", "--orphan", "other")
        commit(self.repo, "unrelated")
        with self.assertRaises(SystemExit):
            prep.check_revision(self.repo)

    def test_rejects_ancestor_of_pin(self):
        git(self.repo, "checkout", "-q", "HEAD")
        commit(self.repo, "newer")
        prep.SDK_PIN = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-q", self.pin)
        with self.assertRaises(SystemExit):
            prep.check_revision(self.repo)


if __name__ == "__main__":
    unittest.main()
