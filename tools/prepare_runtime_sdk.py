"""Apply the source-only runtime fixes to the tested SDK revision (or a descendant).

No game content is read. Conflicting SDK edits are rejected, not overwritten.
Use --skip-dependencies for patch validation without network/dependency setup.

Accepted SDK states:
  * HEAD at the base pin - a clean SDK checkout; the patches under
    thirdparty/ are applied on top of it, and
  * a descendant of the base pin, where runtime fixes are committed and
    can evolve alongside SDK changes without reapplying the old patches.
"""
import argparse
from pathlib import Path
import subprocess
import sys

# Clean base revision the patches were generated against.
SDK_PIN = "babc769a94be5618010abfd075ed84f3c2bc09f5"
# Public libmspack commit the SDK's broken pin is repointed at.
MSPACK_PIN = "305907723a4e7ab2018e58040059ffb5e77db837"
PATCHES = (
    "rexglue-sdk-runtime-fixes.patch",
    "rexglue-sdk-debug-exports.patch",
)


def fail(message):
    print(f"prepare_runtime_sdk: error: {message}", file=sys.stderr)
    raise SystemExit(1)


def git(source, *args, check=True):
    result = subprocess.run(["git", "-C", str(source), *args],
                            capture_output=True, text=True)
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        fail(f"git {' '.join(args)} failed (exit {result.returncode}):\n{detail}")
    return result


def patch_state(source, patch):
    """Classify the worktree as 'applied', 'clean', or 'conflict' for a patch."""
    if git(source, "apply", "--reverse", "--check", str(patch), check=False).returncode == 0:
        return "applied"
    if git(source, "apply", "--check", str(patch), check=False).returncode == 0:
        return "clean"
    return "conflict"


def libmspack_mode(source):
    """Return the mode of thirdparty/libmspack at HEAD ('160000' submodule
    gitlink, '040000' vendored plain files), or None if absent."""
    result = git(source, "ls-tree", "HEAD", "--", "thirdparty/libmspack", check=False)
    if result.returncode != 0 or not result.stdout.strip():
        return None
    return result.stdout.split()[0]


def check_revision(source):
    """Accept the pinned SDK commit or any commit that descends from it."""
    head = git(source, "rev-parse", "HEAD").stdout.strip()
    if head == SDK_PIN:
        return
    rc = git(source, "merge-base", "--is-ancestor", SDK_PIN, "HEAD", check=False).returncode
    if rc == 0:
        return
    if rc == 128:
        raise SystemExit(f"Cannot check SDK revision: pin {SDK_PIN} is not available "
                         f"(shallow clone?). Run: git -C {source} fetch --unshallow")
    raise SystemExit(f"Expected SDK commit {SDK_PIN} or a descendant; got {head}.")


def should_apply_patches(source):
    """Patches are applied only at the exact pin; descendants carry them as commits."""
    return git(source, "rev-parse", "HEAD").stdout.strip() == SDK_PIN


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--skip-dependencies", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    patch_directory = Path(__file__).resolve().parents[1] / "thirdparty"
    if not source.is_dir():
        fail(f"SDK source directory does not exist: {source}")
    if git(source, "rev-parse", "--is-inside-work-tree", check=False).returncode != 0:
        fail(f"{source} is not a git work tree; run "
             "'git submodule update --init thirdparty/rexglue-sdk' from the repo root.")
    check_revision(source)
    if should_apply_patches(source):
        # Keep follow-up fixes separate so existing patched SDK checkouts can upgrade.
        for name in PATCHES:
            patch = patch_directory / name
            if not patch.is_file():
                fail(f"patch file is missing: {patch}")
            state = patch_state(source, patch)
            if state == "applied":
                print(f"Already applied: {name}")
                continue
            result = git(source, "apply", "--check", str(patch), check=False)
            if result.returncode:
                raise SystemExit(f"SDK patch {name} conflicts with local edits:\n" + result.stderr)
            git(source, "apply", str(patch))
            print(f"Applied: {name}")
    else:
        print("SDK HEAD descends from the pin; runtime patches are committed on the branch - skipping patch application")
    if not args.skip_dependencies:
        # libmspack is either a submodule gitlink (base pin and older fork
        # commits) or plain vendored files (newer fork commits). Only the
        # gitlink form needs the index repin + submodule fetch + Windows
        # symlink materialization; the original SDK pin for it is unavailable
        # on its public remote.
        mode = libmspack_mode(source)
        if mode == "160000":
            # git submodule update reads the index, not the patched worktree gitlink.
            git(source, "update-index", "--cacheinfo",
                f"160000,{MSPACK_PIN},thirdparty/libmspack")
        elif mode != "040000":
            fail("thirdparty/libmspack is missing from the SDK tree at HEAD "
                 "(expected a submodule gitlink or vendored files).")
        result = subprocess.run(["git", "-C", str(source), "submodule", "update",
                                 "--init", "--recursive"])
        if result.returncode != 0:
            fail("SDK submodule update failed; a fresh clone needs network "
                 "access to fetch the SDK submodules.")
        if mode == "160000":
            result = subprocess.run([sys.executable,
                                     str(Path(__file__).with_name("prepare_renderer_mspack.py")),
                                     str(source / "thirdparty" / "libmspack")])
            if result.returncode != 0:
                fail("prepare_renderer_mspack.py failed.")
        else:
            # The SDK's thirdparty/CMakeLists.txt treats a dependency as
            # initialized when <dep>/.git exists. Vendored libmspack has no
            # .git, so drop an empty marker directory (invisible to git
            # status) to satisfy that check.
            marker = source / "thirdparty" / "libmspack" / ".git"
            if not marker.exists():
                marker.mkdir(parents=True, exist_ok=True)


if __name__ == "__main__":
    main()
