# Batch 5: the on-flush worker (one ExecuteCommandLists a frame) with thread placement, the
# pending forced read-back (dog check by screenshot), and the limiter yield; gated runs.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
$fast = "--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true --d3d12_async_submit_coalesce=64"
$place = "GPU Commands=2;D3D12 Submit=4;3D Engine=6;GameThread=8"
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast --d3d12_async_submit_on_flush=true") -Total 100 -Tag batch5 -Shots "80" -ExtraEnv @{FABLE2_THREAD_PLACE = $place} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast --d3d12_async_submit_on_flush=true") -Total 100 -Tag batch5y -ExtraEnv @{FABLE2_THREAD_PLACE = $place; FABLE2_LIMITER_YIELD = "1"} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast") -Total 100 -Tag batch5c -ExtraEnv @{FABLE2_THREAD_PLACE = $place} 2>&1 | Write-Output
Write-Output "batch5 done $(Get-Date -Format HH:mm:ss)"
