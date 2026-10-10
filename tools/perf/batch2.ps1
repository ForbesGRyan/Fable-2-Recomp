# Batch 2: the readback and submission cvars, gated runs from the perf worktree.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
$configs = @(
  "--readback_memexport=false",
  "--readback_memexport=false --d3d12_submit_on_primary_buffer_end=false",
  "--readback_memexport=false --readback_resolve_force_addresses= --d3d12_submit_on_primary_buffer_end=false",
  "--readback_memexport=false --readback_resolve_force_addresses= --d3d12_submit_on_primary_buffer_end=false --d3d12_tiled_shared_memory=false"
)
& "$W\tools\perf\ab_runs.ps1" -Configs $configs -Total 100 -Tag batch2 2>&1 | Write-Output
Write-Output "batch2 done $(Get-Date -Format HH:mm:ss)"
