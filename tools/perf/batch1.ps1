# Batch 1: one profile run (sampler on three threads) then A/B timing runs, all gated,
# from the perf worktree's exe dir. Output: $env:TEMP\perf_batch1_*.txt
param([int]$ProfileTotal = 110, [int]$SampleAt = 55, [int]$SampleFor = 40, [switch]$SkipProfile)
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
$T = $env:TEMP

if (-not $SkipProfile) {
  $drv = "$T\perf_batch1_prof3_driver.txt"
  if (Test-Path $drv) { Remove-Item $drv }
  $cmd = "& '$W\tools\drive_game.ps1' -BootSave Hero000 -Total $ProfileTotal -Env @{REX_VSYNC='0'; FABLE2_GUEST_WORK_LOG='1'} -IdleWaitSeconds 1800"
  $p = Start-Process pwsh -ArgumentList @("-NoProfile", "-NonInteractive", "-Command", $cmd) -RedirectStandardOutput $drv -PassThru -WindowStyle Hidden
  Write-Output "profile driver pid $($p.Id) started $(Get-Date -Format HH:mm:ss)"
  $gamePid = 0; $t0 = $null; $sampled = $false
  while (-not $p.HasExited) {
    Start-Sleep -Seconds 2
    if (-not $gamePid -and (Test-Path $drv)) {
      $m = Select-String -Path $drv -Pattern 'started pid (\d+)' | Select-Object -First 1
      if ($m) { $gamePid = [int]$m.Matches[0].Groups[1].Value; $t0 = Get-Date; Write-Output "game pid $gamePid at $($t0.ToString('HH:mm:ss'))" }
    }
    if ($gamePid -and -not $sampled -and ((Get-Date) - $t0).TotalSeconds -ge $SampleAt) {
      $sampled = $true
      Write-Output "sampling $gamePid for $SampleFor s"
      & "$T\sampler.exe" $gamePid $SampleFor 2 "$T\perf_prof3" "GPU Commands" "3D Engine" "GameThread" 2>&1 | Write-Output
    }
  }
  Get-Content $drv | Where-Object { $_ -notmatch 'idle gate: observing' } | Select-Object -Last 4 | Write-Output
}

$configs = @(
  "",
  "--d3d12_submit_on_primary_buffer_end=false",
  "--d3d12_tiled_shared_memory=false"
)
& "$W\tools\perf\ab_runs.ps1" -Configs $configs -Total 100 -Tag batch1 2>&1 | Write-Output
Write-Output "=== run thread placement $(Get-Date -Format HH:mm:ss)"
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 100 -Tag batch1tp -ExtraEnv @{FABLE2_THREAD_PLACE = "GPU Commands=2;3D Engine=4;GameThread=6"} 2>&1 | Write-Output
Write-Output "batch1 done $(Get-Date -Format HH:mm:ss)"
