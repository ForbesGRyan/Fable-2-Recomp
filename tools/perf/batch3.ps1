# Batch 3 (binary with the async submit worker and the deferred readback cvars): gated runs
# from the perf worktree, then a profile run with the submit worker sampled.
param([switch]$SkipProfile)
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
$T = $env:TEMP
Set-Location $W
$configs = @(
  "",
  "--d3d12_async_submit=true",
  "--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true",
  "--readback_memexport=false"
)
& "$W\tools\perf\ab_runs.ps1" -Configs $configs -Total 100 -Tag batch3 2>&1 | Write-Output
Write-Output "=== run thread placement + async $(Get-Date -Format HH:mm:ss)"
& "$W\tools\perf\ab_runs.ps1" -Configs @("--d3d12_async_submit=true --readback_memexport_deferred=true --readback_resolve_force_delayed=true") -Total 100 -Tag batch3tp -ExtraEnv @{FABLE2_THREAD_PLACE = "GPU Commands=2;D3D12 Submit=4;3D Engine=6;GameThread=8"} 2>&1 | Write-Output

if (-not $SkipProfile) {
  Write-Output "=== profile run 4 (async submit on) $(Get-Date -Format HH:mm:ss)"
  $drv = "$T\perf_prof4_driver.txt"
  if (Test-Path $drv) { Remove-Item $drv }
  $cmd = "& '$W\tools\drive_game.ps1' -BootSave Hero000 -Total 110 -Env @{REX_VSYNC='0'; FABLE2_GUEST_WORK_LOG='1'} -GameArgs @('--d3d12_async_submit=true','--readback_memexport_deferred=true','--readback_resolve_force_delayed=true') -IdleWaitSeconds 1800"
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
Write-Output "batch3 done $(Get-Date -Format HH:mm:ss)"
