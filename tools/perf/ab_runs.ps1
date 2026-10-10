# Sequential gated runs from the perf worktree, each with its own game args; prints the
# steady "[frame]" lines of each run's log and a DISTURBED line when another build or the
# foliage session's tests ran during the run (ninja, clang, lld-link, cmake, exes under
# out\tests\runtime-unit-tests, or a changed write time of the shared fable_2.exe).
# Usage: ab_runs.ps1 -Configs @("", "--x=false") -Total 100 -Tag t -ExtraEnv @{K="v"}
param([string[]]$Configs = @(""), [int]$Total = 100, [string]$Tag = "ab", [hashtable]$ExtraEnv = @{})
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
$SharedExe = "C:\Users\Ryan\code\Fable-2-Recomp\out\build\win-amd64-release\fable_2.exe"
Set-Location $W
$logdir = "$W\out\build\win-amd64-release\logs"
$i = 0
foreach ($cfg in $Configs) {
  $i++
  $before = @(Get-ChildItem "$logdir\fable_2_*.log" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
  $envText = "REX_VSYNC='0'; FABLE2_GUEST_WORK_LOG='1'"
  foreach ($k in $ExtraEnv.Keys) { $envText += "; $k='$($ExtraEnv[$k])'" }
  $argText = ""
  if ($cfg) { $argText = " -GameArgs @('" + ($cfg.Split(' ') -join "','") + "')" }
  $drv = "$env:TEMP\perf_${Tag}_run${i}_driver.txt"
  if (Test-Path $drv) { Remove-Item $drv }
  $cmd = "& '$W\tools\drive_game.ps1' -BootSave Hero000 -Total $Total -Env @{$envText}$argText -IdleWaitSeconds 1800"
  Write-Output "=== run $i/$($Configs.Count): args [$cfg] env [$envText]  $(Get-Date -Format HH:mm:ss)"
  $p = Start-Process pwsh -ArgumentList @("-NoProfile", "-NonInteractive", "-Command", $cmd) -RedirectStandardOutput $drv -PassThru -WindowStyle Hidden
  $started = $false; $disturbed = @{}; $exeTime = $null
  while (-not $p.HasExited) {
    Start-Sleep -Seconds 5
    if (-not $started -and (Test-Path $drv) -and (Select-String -Path $drv -Pattern 'started pid' -Quiet)) {
      $started = $true
      $exeTime = (Get-Item $SharedExe -ErrorAction SilentlyContinue).LastWriteTime
    }
    if ($started) {
      foreach ($pr in Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(ninja|clang|clang\+\+|clang-cl|lld-link|cmake|rexglue)$' -or ($_.Path -and $_.Path -like '*\out\tests\runtime-unit-tests\*') }) {
        $disturbed[$pr.ProcessName] = $true
      }
      $t = (Get-Item $SharedExe -ErrorAction SilentlyContinue).LastWriteTime
      if ($exeTime -and $t -and $t -ne $exeTime) { $disturbed["shared fable_2.exe rewritten"] = $true }
    }
  }
  Get-Content $drv | Where-Object { $_ -notmatch 'idle gate: observing' } | Select-Object -Last 3 | Write-Output
  if ($disturbed.Count) { Write-Output ("DISTURBED by: " + ($disturbed.Keys -join ', ')) } else { Write-Output "undisturbed (no build tool or foreign test seen during the run)" }
  $new = Get-ChildItem "$logdir\fable_2_*.log" -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
  if ($new) {
    Write-Output "log: $($new.Name)"
    Select-String -Path $new.FullName -Pattern '\[frame\] (guest|cp) ' | ForEach-Object { $_.Line.Substring(26) } | Select-Object -Skip 6 | Write-Output
    Select-String -Path $new.FullName -Pattern '\[threadplace\]|d3d ERROR|Unhandled|FATAL' | Select-Object -First 8 | ForEach-Object { $_.Line.Substring(26) } | Write-Output
  }
}
