# Waits for batch 1 to finish, then runs batch 2 (readback and submission cvars) and a
# profile run that samples the GPU thread, the unnamed threads (main/UI) and the D3D12
# background threads with Microsoft symbols.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
$T = $env:TEMP
Set-Location $W
while (-not (Test-Path "$T\perf_batch1.txt") -or -not (Select-String -Path "$T\perf_batch1.txt" -Pattern 'batch1 done' -Quiet)) {
  Start-Sleep -Seconds 20
}
Write-Output "batch1 done seen at $(Get-Date -Format HH:mm:ss); starting batch2"
& "$W\tools\perf\batch2.ps1" 2>&1 | Write-Output

Write-Output "=== profile run 4 (GPU Commands, unnamed, D3D Background) $(Get-Date -Format HH:mm:ss)"
$drv = "$T\perf_prof4_driver.txt"
if (Test-Path $drv) { Remove-Item $drv }
$cmd = "& '$W\tools\drive_game.ps1' -BootSave Hero000 -Total 110 -Env @{REX_VSYNC='0'; FABLE2_GUEST_WORK_LOG='1'} -IdleWaitSeconds 1800"
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
    & "$T\sampler.exe" $gamePid 40 2 "$T\perf_prof4" "GPU Commands" "unnamed" "D3D Background" 2>&1 | Write-Output
  }
}
Get-Content $drv | Where-Object { $_ -notmatch 'idle gate: observing' } | Select-Object -Last 3 | Write-Output
Write-Output "batch23 done $(Get-Date -Format HH:mm:ss)"
