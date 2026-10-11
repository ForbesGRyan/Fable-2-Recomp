param([string]$Tag = "prof1", [int]$Total = 110, [int]$SampleAt = 55, [int]$SampleFor = 40, [string]$ExtraArgs = "")
Set-Location C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime
$out = "$env:TEMP\perf_${Tag}_driver.txt"
$env:FABLE2_PERF_TAG = $Tag
$gameArgs = @()
if ($ExtraArgs) { $gameArgs = $ExtraArgs.Split(' ') }
$job = Start-Job -ScriptBlock {
  param($root, $out, $Total, $gameArgs)
  Set-Location $root
  & .\tools\drive_game.ps1 -BootSave Hero000 -Total $Total -Env @{REX_VSYNC="0"; FABLE2_GUEST_WORK_LOG="1"} -GameArgs $gameArgs -IdleWaitSeconds 1800 2>&1 | Tee-Object -FilePath $out
} -ArgumentList (Get-Location).Path, $out, $Total, $gameArgs
$pid_ = 0; $t0 = $null
while ($job.State -eq 'Running') {
  Start-Sleep -Seconds 2
  if (-not $pid_ -and (Test-Path $out)) {
    $m = Select-String -Path $out -Pattern 'started pid (\d+)' | Select-Object -First 1
    if ($m) { $pid_ = [int]$m.Matches[0].Groups[1].Value; $t0 = Get-Date; Write-Output "game pid $pid_ at $t0" }
  }
  if ($pid_ -and $t0 -and ((Get-Date) - $t0).TotalSeconds -ge $SampleAt) {
    Write-Output "sampling pid $pid_ for $SampleFor s"
    & "$env:TEMP\sampler.exe" $pid_ $SampleFor 2 "$env:TEMP\perf_${Tag}" "GPU Commands" "3D Engine" "GameThread" 2>&1 | Write-Output
    $t0 = $null
  }
}
Receive-Job $job | Out-Null
Get-Content $out -Tail 3
