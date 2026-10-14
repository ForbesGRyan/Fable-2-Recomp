# Bridge water ripple fix (2026-10-12)

The user reported that bridge-view lake colour looked right but the native surface lacked the game's ripples. The normal map was present and scrolling; all twelve supported water patches drew. The difference was filtering: the SDK defaults `anisotropic_override` to 3 (4x), applied to the game's mipmapped linear normal fetch. Native water's four static samplers always used point/linear filtering.

Runs 770/771 isolate the cause. The second run changes only `--anisotropic_override=0`. In the same opaque interior lake patch, six pixels inside its boundary, the high-frequency luminance RMS changes as follows:

| Filtering | Game | Native | Native/game |
|---|---:|---:|---:|
| Default 4x, before fix | 0.048919 | 0.012568 | 0.257 |
| Linear control, before fix | 0.012554 | 0.012578 | 1.002 |

Both are healthy official 100-second bridge diagnostics, with unchanged binaries within each run, all twelve water surfaces drawn and no D3D12 errors or known freeze signature. This is a filtering control, not a timing result. The region and 20% post-fix detail tolerance were selected before the corrected run.

The correction applies the shared override to the existing linear water samplers. It preserves point samplers and wrap/clamp addressing. The mode is chosen per surface; a point-filtered or single-level normal map does not receive the override, matching the SDK eligibility rule. Layout changes rebind the frame constant buffer before drawing. Normal `SampleGrad` uses anisotropic filtering; mask and reflection `SampleLevel(0)` remain isotropic, as specified by the [Direct3D functional specification, section 7.18.11](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm). Shader text, colour formulas, scroll coordinates and SDK source are unchanged. The renderer caches at most six filter layouts and their cull pipelines, allowing a live setting change without accumulating layouts on repeated toggles. The installed runtime exports its registry accessor, so the setting is read and parsed there rather than through unexported typed queries.

The sampler regression fails against the old all-linear setup (`FAIL water sampler setting=1 register=1`), then passes with the correction for all shared modes and the point/address invariants. The native clay logic checks pass. An existing light/fog census assertion was updated for the three previously registered campfire material families; every per-entry semantic check is retained. The affected Python water suites passed 222 tests with two known evidence-dependent skips. Release build passes, executable SHA-256 `7DDD8979668BD1A31E70EC6E360629FB0B0C72CB5CB5457BE8DBD86D6C883011`.

The full native suite passed, with the existing optional real-data skips. The final eligibility regression was then extended to cover point-filtered maps, mip_max=0 and absent mip storage; it and the final release build pass. The final software/debug gate and corrected hardware comparison completed as described below. Evidence: `out/native-evidence/water/ripples/`; plan: [water-ripples](../superpowers/plans/2026-10-12-native-renderer-water-ripples.md). A discarded six-sampler shader candidate exceeded the RHI's four-sampler limit; none of its shader edits remain and its failing shader test is not the final regression.

This follow-up addresses bridge ripple filtering. Historical reflection silhouettes/pools concerns, unseen water bodies, and the default-off decision retain their recorded scope.

## Corrected bridge evidence

Run 774 (`water_ripples_after_20261012_195640`) completed the official 100-second UserPresent driver with exit 0. Root inspected all three original 1942x1136 screenshots and both detail comparisons. Native ripples are visible, distant trees are present, and the lake retains its colour. Both own-frame captures have exactly the same eye and match their native dump's scene frame. All twelve water surfaces and fourteen impostor groups (928 quads) draw; only the initial three texture warm-up frames are refused.

| Scene frame | Game detail RMS | Native detail RMS | Native/game | Preselected ±20% bound |
|---|---:|---:|---:|---|
| 1195 | 0.048263 | 0.046194 | 0.9571 | met |
| 1813 | 0.047625 | 0.046322 | 0.9726 | met |

The measured region has 5,564 interior pixels in each capture. The unchanged all-water colour measure passes on 18,420/18,429 pixels: native/game RGB mean ratios about 0.984/0.983/0.971 and hue difference -0.64/-0.62 degrees. Scroll coordinates change in both captured frame pairs and between endpoints. With exposure removed, native water RGB changes by RMS 0.009916 across the fixed interior patch. This proves changing endpoint water with a stationary eye, not uninterrupted-motion or glint acceptance. The offline isotropic normal-filter replay is not used as an anisotropic colour oracle.

Full discovery checks pass 2408/2408 and 2411/2411 sampled draws, with zero index, scene-frame, mip, vertex-storage or water-row problems. Six post-start guest windows have work medians around 8 ms and positive swap waits; split geometry remains active. There is no known freeze signature and no D3D12 message in the hardware log. All three binary hashes stay unchanged during the run. The 3,856 startup heap-allocation error lines also occur in both before/control runs; they are not new graphics errors. Full read-back supplies no timing acceptance claim.

The final software run 773 (`water_ripples_warp_20261012_195430`) also completed with exit 0, stable binaries, twelve drawn water surfaces and the 4x pipeline ready. Its two original screenshots and frame pair 236/237 were inspected. There are zero D3D12 errors and four existing warning-1328 buffer initial-state notices. Software read-back refuses the fourteen impostor pictures by content; the hardware run separately draws all fourteen. This 100-second gate does not claim the historical 240-second stress test, and it produces too few frames for aggregate guest windows; its health scope is the changing endpoints and successful driver completion.

The report, plan and ledger record this ripple defect as fixed by hardware evidence. A visible foreground split run was offered for the user's check; no response was received, so user confirmation remains pending. All original six user edits and shader-header bytes match their initial hashes. At the original validation session SDK source remained clean and changes were unstaged. The fix was subsequently committed as `3c84229` and is included in master; user visual acceptance remains pending. Raw logs, metadata, captures, geometry/texture folders and original screenshots are preserved in `out/native-evidence/water/ripples/raw/`, with a file-hash manifest. No game remains running.
