# Batch 4: the async worker with coalescing. Gated runs from the perf worktree; the first run
# takes a screenshot at 80 s for the correctness check of the deferred readbacks; then the
# placement variant; then profile run 4 (GPU Commands, the submit worker, unnamed threads,
# the D3D12 background threads) with Microsoft symbols.
param([switch]$SkipProfile)
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
$T = $env:TEMP
Set-Location $W
$fast = "--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true"
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast --d3d12_async_submit_coalesce=16") -Total 100 -Tag batch4 -Shots "80" 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("--d3d12_async_submit_coalesce=16") -Total 100 -Tag batch4ref -Shots "80" 2>&1 | Write-Output
Write-Output "=== run thread placement $(Get-Date -Format HH:mm:ss)"
& "$W\tools\perf\ab_runs.ps1" -Configs @("$fast --d3d12_async_submit_coalesce=16") -Total 100 -Tag batch4tp -ExtraEnv @{FABLE2_THREAD_PLACE = "GPU Commands=2;D3D12 Submit=4;3D Engine=6;GameThread=8"} 2>&1 | Write-Output

if (-not $SkipProfile) {
  Write-Output "=== profile run 4 (async + coalesce) $(Get-Date -Format HH:mm:ss)"
  $drv = "$T\perf_prof4_driver.txt"
  if (Test-Path $drv) { Remove-Item $drv }
  $cmd = "& '$W\tools\drive_game.ps1' -BootSave Hero000 -Total 110 -Env @{REX_VSYNC='0'; FABLE2_GUEST_WORK_LOG='1'} -GameArgs @('--d3d12_async_submit=true','--readback_memexport_deferred=true','--readback_resolve_force_delayed=true','--d3d12_async_submit_coalesce=16') -IdleWaitSeconds 1800"
  $p = Start-Process pwsh -ArgumentList @("-NoProfile", "-NonInteractive", "-Command", $cmd) -RedirectStandardOutput $drv -PassThru -WindowStyle Hidden
  $gamePid = 0; $t0 = $null; $sampled = $false
  while (-not $p.HasExited) {
    Start-Sleep -Seconds 2
    if (-not $gamePid -and (Test-Path $drv)) {
      $m = Select-String -Path $drv -Pattern 'started pid (\d+)' | Select-Object -First 1
      if ($m) { $gamePid = [int]$m.Matches[0].Groups[1].Value; $t0 = Get-Date; Write-Output "game pid $gamePid at $($t0.ToString('HH:mm:ss'))" }
    }
    if ($gamePid -and -not $sampled -and ((Get-Date) - $t0).TotalSeconds -ge 55) {
      $sampled = $true
      & "$T\sampler.exe" $gamePid 40 2 "$T\perf_prof4" "GPU Commands" "D3D12 Submit" "unnamed" "D3D Background" 2>&1 | Write-Output
    }
  }
  Get-Content $drv | Where-Object { $_ -notmatch 'idle gate: observing' } | Select-Object -Last 3 | Write-Output
}
Write-Output "batch4 done $(Get-Date -Format HH:mm:ss)"
