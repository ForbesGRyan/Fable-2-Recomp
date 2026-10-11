# Stage a private SDK root for the perf worktree from the instrumented SDK copy
# (thirdparty/rexglue-sdk, built by tools/perf/build_sdk_sym.cmd): the official
# 0.10.0 package plus the copy's headers, libs, DLLs and PDBs. The worktree build
# is then configured with REXGLUE_SDK_ROOT (and CMAKE_PREFIX_PATH, rexglue_DIR)
# pointing at the output, so the exe sees the new EmulatedFrameStats fields and
# links the matching rexruntime.lib. Nothing in the main checkout is written.
param(
  [string]$Official = "C:\Users\Ryan\code\Fable-2-Recomp\out\tooling\rexglue-sdk-0.10.0\win-amd64",
  [string]$Worktree = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime",
  [string]$Output = "C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime\out\tooling\runtime-sdk-perf\win-amd64"
)
$ErrorActionPreference = "Stop"
$source = Join-Path $Worktree "thirdparty\rexglue-sdk"
$native = Join-Path $source "out\win-amd64"
$build = Join-Path $Worktree "out\build\runtime-sdk-sym"
foreach ($p in @("$native\rexruntime.dll", "$native\rexgpu-xenos.dll", "$native\rexruntime.lib",
                 "$native\rexgpu-xenos.lib", "$build\include\rex\version.h", "$Official\bin\rexglue.exe")) {
  if (-not (Test-Path $p)) { throw "missing build input: $p" }
}
New-Item -ItemType Directory -Force $Output | Out-Null
robocopy $Official $Output /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $source "include\rex") (Join-Path $Output "include\rex") /E /NFL /NDL /NJH /NJS /NP | Out-Null
Copy-Item -Force "$build\include\rex\version.h" (Join-Path $Output "include\rex\version.h")
robocopy (Join-Path $source "thirdparty\sdl3\include\SDL3") (Join-Path $Output "include\SDL3") /E /NFL /NDL /NJH /NJS /NP | Out-Null
Get-ChildItem "$native\*.lib" | ForEach-Object { Copy-Item -Force $_.FullName (Join-Path $Output "lib\$($_.Name)") }
foreach ($n in @("rexruntime", "rexgpu-xenos")) {
  Copy-Item -Force "$native\$n.dll" (Join-Path $Output "bin\$n.dll")
  Copy-Item -Force "$native\$n.pdb" (Join-Path $Output "bin\$n.pdb")
}
New-Item -ItemType Directory -Force (Join-Path $Output "share\rexglue") | Out-Null
foreach ($n in @("rex_app.cpp", "windowed_app_main_sdl.cpp")) {
  Copy-Item -Force (Join-Path $source "src\ui\$n") (Join-Path $Output "share\rexglue\$n")
}
$prov = @{
  configuration = "Release"
  sdk_source = "copy of the submodule at 0c6d1de plus the perf-frametime instrumentation (no git)"
  codegen_tool = "$Official\bin\rexglue.exe"
  runtime_sha256 = (Get-FileHash "$native\rexruntime.dll" -Algorithm SHA256).Hash.ToLower()
  plugin_sha256 = (Get-FileHash "$native\rexgpu-xenos.dll" -Algorithm SHA256).Hash.ToLower()
} | ConvertTo-Json
Set-Content -Path (Join-Path $Output "renderer-sdk-provenance.json") -Value $prov -Encoding utf8
"staged perf SDK at $Output"
Get-Item "$Output\bin\rexruntime.dll", "$Output\bin\rexgpu-xenos.dll", "$Output\bin\rexgpu-xenos.pdb" | Select-Object Name, Length, LastWriteTime
