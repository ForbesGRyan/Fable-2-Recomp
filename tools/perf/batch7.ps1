# Batch 7: automatic thread placement (FABLE2_THREAD_PLACE=auto) with a PM4 dump of frame 1500,
# then a 200 s stability run with the frame-time percentiles.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
$fast = "--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true --d3d12_async_submit_coalesce=64 --d3d12_async_submit_on_flush=true --d3d12_async_submit_flush_queries=false"
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast --dump_pm4_frame=1500") -Total 100 -Tag batch7 -ExtraEnv @{FABLE2_THREAD_PLACE = "auto"} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast") -Total 200 -Tag batch7long -ExtraEnv @{FABLE2_THREAD_PLACE = "auto"} 2>&1 | Write-Output
Write-Output "batch7 done $(Get-Date -Format HH:mm:ss)"
