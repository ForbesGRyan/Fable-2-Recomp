# Batch 10: the defaults with the idle toast no longer forcing a UI repaint at every tick
# (presents only with guest frames). 160 s with hitch lines, then 200 s.
$W = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
Set-Location $W
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 160 -Tag batch10 -Shots "80" -ExtraEnv @{FABLE2_HITCH_MS = "12"} 2>&1 | Write-Output
& "$W\tools\perf\ab_runs.ps1" -Configs @("") -Total 200 -Tag batch10long -ExtraEnv @{FABLE2_HITCH_MS = "12"} 2>&1 | Write-Output
Write-Output "batch10 done $(Get-Date -Format HH:mm:ss)"
