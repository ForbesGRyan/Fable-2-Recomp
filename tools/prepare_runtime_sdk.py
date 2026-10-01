"""Apply the source-only runtime fixes to the tested SDK revision (or a descendant).

No game content is read. Conflicting SDK edits are rejected, not overwritten.
Use --skip-dependencies for patch validation without network/dependency setup.
"""
import argparse
from pathlib import Path
import subprocess
import sys

SDK_PIN = "babc769a94be5618010abfd075ed84f3c2bc09f5"
MSPACK_PIN = "305907723a4e7ab2018e58040059ffb5e77db837"


def git(source, *args, check=True):
    return subprocess.run(["git", "-C", str(source), *args], check=check,
                          capture_output=True, text=True)


def check_revision(source):
    """Accept the pinned SDK commit or any commit that descends from it."""
    head = git(source, "rev-parse", "HEAD").stdout.strip()
    if head == SDK_PIN:
        return
    if git(source, "merge-base", "--is-ancestor", SDK_PIN, "HEAD", check=False).returncode == 0:
        return
    raise SystemExit(f"Expected SDK commit {SDK_PIN} or a descendant; got {head}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--skip-dependencies", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    patch_directory = Path(__file__).resolve().parents[1] / "thirdparty"
    check_revision(source)
    # Keep follow-up fixes separate so existing patched SDK checkouts can upgrade.
    for name in ("rexglue-sdk-runtime-fixes.patch", "rexglue-sdk-debug-exports.patch"):
        patch = patch_directory / name
        if git(source, "apply", "--reverse", "--check", str(patch), check=False).returncode == 0:
            print(f"Already applied: {name}")
            continue
        result = git(source, "apply", "--check", str(patch), check=False)
        if result.returncode:
            raise SystemExit(f"SDK patch {name} conflicts with local edits:\n" + result.stderr)
        git(source, "apply", str(patch))
        print(f"Applied: {name}")
    if not args.skip_dependencies:
        # git submodule update reads the index, not the patched worktree gitlink.
        # The original SDK pin for libmspack is unavailable on its public remote.
        git(source, "update-index", "--cacheinfo", f"160000,{MSPACK_PIN},thirdparty/libmspack")
        subprocess.run(["git", "-C", str(source), "submodule", "update", "--init", "--recursive"], check=True)
        subprocess.run([sys.executable, str(Path(__file__).with_name("prepare_renderer_mspack.py")),
                        str(source / "thirdparty/libmspack")], check=True)


if __name__ == "__main__":
    main()
