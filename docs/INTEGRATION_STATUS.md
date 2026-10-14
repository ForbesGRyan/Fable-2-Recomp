# Renderer integration status

Updated 2026-10-14. This records the combined source and its validation, while
the [renderer roadmap](native-renderer/roadmap.md) and
[user checks](native-renderer/user-checks.md) retain the outstanding acceptance.

## Integrated source

The integration starts from master `39e26da`, which already includes the
water/campfire/ripple fixes and local lights. It combines:

| Work | Source | Integration |
|---|---|---|
| HUD, menus/masks, video, particles, snow and fades | `native-renderer-presentation`, `16db251` | Included with master's local lights |
| Opening-character lighting | `234bced` | Included with the shared multi-light table |
| World anisotropic filtering and level-0 sampling | `native-renderer-aniso`, `b9011ca` | Merge `ac05fb7` |
| Frame-meter compatibility with both SDK layouts | `perf-frametime`, `9869594` | Merge `3ce56d7`; identical code already existed on master as `a227629` |

The filtering merge preserves both shader-test transformations: the historical
sampler sites are reconstructed after the presentation effect transformation,
and the local-light-era byte pins remain intact. The native runner includes
all presentation tests and the renamed clay-sampler test. Acceptance numbering
is 107-110 for local lights, 111-117 for presentation and 118 for filtering.

The integrated worktree is `out/integration/renderers`. The original checkout
remains on `native-renderer-water`; its pre-existing tracked/untracked files
are preserved, including the separate user edits enabling wind. Its ignored
`TODO.md` is updated to this integration state. No branch has been pushed.

## Validation

Validation uses the existing matched Release SDK at
`out/tooling/runtime-sdk/win-amd64`, SDK source `0c6d1de`, and the existing
separate codegen tool. No SDK source was modified. Local shader dumps are
available read-only to dump-backed tests; generated sources and saves are
copied into the isolated worktree rather than shared for mutation.

| Check | Result |
|---|---|
| Release configure/build of `fable_2` | Passed, exit 0 |
| Full `tests/run_native_tests.cmd` | Passed, exit 0, 59 PASS lines; includes shader/binding, mask GPU, sampler and renderer-record checks |
| `python -m pytest tests -n 8 -q --tb=short` | 1,911 passed, 33 skipped, 281.46 s |
| `light_check.py --multi-lanterns --trials 400` | 43 pairs, 400/400 per pair, zero disagreements |
| Existing SDK performance patch, apply check only | Passed on the clean current SDK; patch not applied |
| Bounded integrated WARP/D3D12 game check | Refused by the unchanged idle gate after 300 s (input/audio), exit 10; nothing launched |

The native suite's optional real-normal-stream and real-picture-level checks
remain skipped without `FABLE2_NORMALS_BIN` and `FABLE2_1555_BIN`. Python skips
are reported by the suite; a passing synthetic
or dump-backed check does not establish moving-picture acceptance.

Build binary SHA-256:
`E814591EF6531CC24497A3459CDD68CA023B099B99614C8743A44B31ECDF0977`.

Local raw evidence is in the controlling checkout's `out/integration/`:
`build-release.log`, `native-tests.log`, `python-tests.log`,
`integrated-lanterns.log`, `warp-driver.log` and `root-state-before.json`.
The initial runtime preflight found no copied save and launched nothing; that
refusal is retained in `warp-save-preflight.log`. A corrected attempt uses a
copy of the profile/save tree and the unchanged idle gate. That attempt was
refused due to input/audio after 300 s; no integrated game-runtime acceptance
is claimed. A fresh bounded run remains required when the machine is idle.

## Defaults and remaining acceptance

Committed native UI, water, wind, coverage, impostors, post, bloom and sun layers
remain off by default. Local lights, ordinary textures, shadows and fog default
on; the shared anisotropic override defaults to 3 (4x). The development checkout's
user wind edits are separate from these committed defaults.

- Quiet UI-off timing remains pending. Run 869 established the historical UI-on
  floor of 30.0 fps; the last UI-off attempt was refused at 22:35 on 2026-10-13
  after 1800 s of gate waiting. An old waiter is not a currently running job.
  Repeat the pair on the integrated binary before accepting its cost.
- Moving snow, menu selections, movies, subtitles, fades, guiding-trail flicker
  and the unexpected close from run 837 remain user/runtime checks.
- Native half-resolution effects and distortion producers, some material
  variants, and an exercised F51 DOF route remain incomplete. Unsupported
  distortion composites explicitly retain current guest output.
- Filtering's live F3 rebuild/toggle, other texture types/regions and documented
  filter overrides remain acceptance or implementation limits.
- Water remains deferred WITH_CONCERNS. Integration does not resolve its
  reflection-coverage/light-pool misses, shoreline/glint views, motion, other
  regions/times, device replacement or default decision.

## Performance SDK boundary

Normal `build.cmd` prepares/stages the matched runtime SDK through
`tools/prepare_runtime_sdk.py`; it does not apply `tools/perf/sdk-perf.patch`.
The historical 100+ fps measurements used a separately patched/private SDK.
App thread placement and uncapped configuration already exist in master, and
the merged frame meter tolerates SDKs without queue/GPU counters.

Adopting the async/deferred/coalescing SDK patch into the normal source/build
path, validating the matched DLLs/import libraries and choosing its defaults
remain separate work. See [performance notes](performance/frametime.md).
