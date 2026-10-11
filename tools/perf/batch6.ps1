# Batch 6: the fast configuration with non-flushing occlusion queries and the first-resolve
# synchronous read-back (dog check by screenshot), the GPU busy line, then a long stability run.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
$fast = "--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true --d3d12_async_submit_coalesce=64 --d3d12_async_submit_on_flush=true --d3d12_async_submit_flush_queries=false"
$place = "GPU Commands=2;D3D12 Submit=4;3D Engine=6;GameThread=8"
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast") -Total 100 -Tag batch6 -Shots "80" -ExtraEnv @{FABLE2_THREAD_PLACE = $place} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast") -Total 200 -Tag batch6long -ExtraEnv @{FABLE2_THREAD_PLACE = $place} 2>&1 | Write-Output
Write-Output "batch6 done $(Get-Date -Format HH:mm:ss)"
