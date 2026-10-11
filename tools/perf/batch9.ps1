# Batch 9: the defaults with thread isolation and priority (160 s, hitch lines at 12 ms), then
# the same with isolation off for comparison.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 160 -Tag batch9 -ExtraEnv @{FABLE2_HITCH_MS = "12"} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 160 -Tag batch9noiso -ExtraEnv @{FABLE2_HITCH_MS = "12"; FABLE2_THREAD_PLACE_ISOLATE = "0"; FABLE2_THREAD_PLACE_PRIORITY = "0"} 2>&1 | Write-Output
Write-Output "batch9 done $(Get-Date -Format HH:mm:ss)"
