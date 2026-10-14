# Source-only tests

From the repository root:

```cmd
python -m pip install -r tests/requirements.txt
python tools/generate_game_versions.py --check
python -m unittest discover -s tests -p test_*.py
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release
tests\run_native_tests.cmd
```

Tests that evaluate captured shader microcode require local files under
`out/shader_dump` and skip explicitly when those files are absent. CI runs
without these game-derived inputs; keep them out of commits and uploads.

Python tests also cover Debug/Release SDK staging with synthetic
files, including failure when Debug DLLs or import libraries are missing.
The native runner needs a VS x64 developer shell with Clang and the patched
SDK source. An optional first argument selects that source tree. It checks
audio pacing, host FPS limiting (including 144), FPS metering and the version
catalogue/content guards in compatible and both single-edition configurations.
Test executables go under `out/tests`; no game files or audio/GPU device are
needed. Original XEX paths can optionally be passed to the native verifier,
and a dump path to the launcher tests, for local checks. Never commit these inputs.

## Running the Python tests on every core

`python -m pytest -q tests/ -n auto` (needs `pip install pytest-xdist`; about 2.5 minutes on
32 cores against roughly 15 minutes serially). Every test that writes uses a temporary
directory and the shared `out/shader_dump` is read only, so the parallel result equals the
serial one (checked 2026-10-13).
