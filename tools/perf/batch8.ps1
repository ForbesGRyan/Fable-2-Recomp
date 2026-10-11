# Batch 8: the defaults. A plain launch (no cvars, no placement variable) with the hitch
# threshold at 12 ms and a screenshot; then the same with the placement switched off.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 160 -Tag batch8 -Shots "80" -ExtraEnv @{FABLE2_HITCH_MS = "12"} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 100 -Tag batch8off -ExtraEnv @{FABLE2_HITCH_MS = "12"; FABLE2_THREAD_PLACE = "off"} 2>&1 | Write-Output
Write-Output "batch8 done $(Get-Date -Format HH:mm:ss)"
