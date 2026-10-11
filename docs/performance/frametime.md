# Frame time: from 33 to 100+ fps in the emulated view

Performance sub-project, 2026-10-10, branch `perf-frametime` (worktree `.claude/worktrees/perf-frametime`).
Ledger with every ruling and run: `.superpowers/sdd/2026-10-10-performance/progress.md`.
Measured at the bridge (save Hero000), standing, native view off, `REX_VSYNC=0`, window 1296 wide.

## Result

| configuration | fps (300-frame windows) | frame median | GPU-emulation thread | GPU |
|---|---|---|---|---|
| master as of 2026-10-10 (log 592) | 30 to 36 | 28 to 33 ms | 27 ms busy, 0 idle | not measured |
| async submit + deferred read-backs + coalescing (log 010) | 61 | 16.5 ms | 9.5 ms | |
| + thread placement (log 012) | 74 to 78 | 12.6 ms | 6.2 ms | |
| + one submission a frame (on flush) (logs 014 to 016) | **97 to 115** | **8.5 to 8.7 ms** | 6.2 ms (p90 6.5) | 7.5 ms (p90 7.8) |

The game's own render thread needs 5.1 ms a frame in this configuration; it spends the rest in
the swap waiting for the GPU-emulation thread.

## Where the 27 ms went (profile 3, log 003, `tools/perf/sampler.cpp` with the SDK PDBs)

More than half of the GPU-emulation thread's frame was synchronization with the host GPU, not
emulation work:

- 21% in memexport read-backs: the particle passes write memory from the GPU (memexport); the SDK
  read every such write back to guest memory, draining the GPU queue twice a frame.
- 8% in the forced hero/dog resolve read-back (`readback_resolve_force_addresses`), another
  drain per resolve.
- 42% waiting on the D3D12 runtime's command-queue mutex inside `ExecuteCommandLists`: the main
  thread presents continuously with vsync off and holds that mutex while the NVIDIA driver spins
  for hardware-queue space inside `Present`; every `ExecuteCommandLists` of the emulation thread
  (6 a frame, one per ring-buffer kick) queued behind it, about 2 ms each whatever its size.
- 20% replaying the deferred command list into the real one; 8% register writes; the draws
  themselves (3,800 a frame, 3,300 of them the predicated tiling replay) 15 ms inside IssueDraw
  of which most was the above.

## The levers

All SDK changes are cvars in `tools/perf/sdk-perf.patch` (against the submodule's 0c6d1de),
default off until merged; the app-side ones are environment variables.

1. `--d3d12_async_submit=true`: the deferred command list is replayed and executed on a worker
   thread ("D3D12 Submit"); the emulation thread reserves the fence value at hand-over and flushes
   the worker before a fence wait, before the presenter takes the swap image, and at shutdown.
2. `--readback_memexport_deferred=true`: when the previous double-buffer slot's submission has not
   completed, the copy-back is skipped instead of draining the GPU (the CPU copy of exported
   particle data arrives a draw later).
3. `--readback_resolve_force_delayed=true`: the first forced resolve of a key is read back
   synchronously (the guest reads the dog's face right after its one resolve: without this the dog
   is black), later ones are queued and copied back when their fence has passed.
4. `--d3d12_async_submit_coalesce=64 --d3d12_async_submit_on_flush=true`: the worker records
   arriving submissions into one open command list and calls `ExecuteCommandLists` once a frame,
   at the swap flush, with a full UAV and aliasing barrier between the merged streams.
5. `--d3d12_async_submit_flush_queries=false`: an occlusion-query read reports "not ready" until the
   worker has executed on its own instead of forcing an execute.
6. `FABLE2_THREAD_PLACE=auto` (or `"GPU Commands=2;D3D12 Submit=4;3D Engine=6;GameThread=8"`): each
   of the four long serial threads on its own physical core of the V-cache CCD. About a third off
   every one of them; the biggest single step after the worker.

Experiments that did not pay: `--d3d12_submit_on_primary_buffer_end=false` alone (moves the cost
into the swap), `--d3d12_tiled_shared_memory=false` (slower), `FABLE2_LIMITER_YIELD` (not measured
to a conclusion).

## How to run and measure

```
tools\drive_game.ps1 -BootSave Hero000 -Total 100 -Env @{REX_VSYNC="0"; FABLE2_GUEST_WORK_LOG="1"; FABLE2_THREAD_PLACE="auto"} -GameArgs @("--d3d12_async_submit=true","--readback_memexport_deferred=true","--readback_resolve_force_delayed=true","--d3d12_async_submit_coalesce=64","--d3d12_async_submit_on_flush=true","--d3d12_async_submit_flush_queries=false")
```

The log then carries, every 300 guest frames: `[frame] guest` (fps, frame median/p90/p99/max,
work, swap), `[frame] cp` (emulation thread busy/idle/present, draws, resolves), `[frame] cp
queue` (hand-overs, fence waits, tile mappings, kicks), `[frame] cp tiling`, `[frame] gpu` (GPU
busy from timestamps) and `[frame] hitch` for frames over 20 ms. `tools/perf/ab_runs.ps1` runs
gated A/B batches and marks runs disturbed by builds; `tools/perf/sampler.cpp` samples thread
stacks of a running game with the Microsoft symbol server (no admin); `tools/perf/build_sdk_sym.cmd`
and `tools/perf/stage_sdk_perf.ps1` build a copy of the SDK with PDBs into a private SDK root.

## Also fixed on the way

`src/core/dir_manifest_heal.h` compared forward-slash paths against the manifest's backslashes,
so every start appended all 439 entries again: the staged manifest had 582 copies of each line and
the CMake manifest step ran for minutes.

## Open

- Stability: five of 38 windows in the 200 s run dip to 85 to 99 fps mean with the median
  unchanged (isolated long frames). The hitch line attributes them from the next run on.
- Other places: only the bridge is measured. The third spot and a town have more draws.
- The GPU at 7.5 ms is now within 1.2 ms of the frame: the tile replay (3,300 of 3,780 draws)
  is the lever that cuts the emulation thread, the worker and the GPU at once (`--dump_pm4_frame`
  gives the per-tile structure); a presenter queue of its own would remove the swap flush and
  the Present lock.
- RivaTuner Statistics Server hooks the process (user check below).
- Merging: the SDK patch into the submodule with the cvars default on, the thread placement and
  the uncapped launcher as the default.

## User checks

- RTSS: add a RivaTuner profile for `fable_2.exe` with application detection "None" (or close
  RTSS and Afterburner) and compare the `[frame] gpu` and `present` figures of a bridge run with
  and without; the hook sits in every `Present` and `ExecuteCommandLists` of the game.
