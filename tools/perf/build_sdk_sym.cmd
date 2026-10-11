@echo off
setlocal
set "ROOT=C:\Users\Ryan\code\Fable-2-Recomp\.claude\worktrees\perf-frametime"
set "SOURCE=%ROOT%\thirdparty\rexglue-sdk"
set "BUILD=%ROOT%\out\build\runtime-sdk-sym"
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
where ninja >nul 2>nul || set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  "-DCMAKE_C_FLAGS=-march=x86-64-v2 -g -gcodeview" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2 -g -gcodeview" ^
  "-DCMAKE_SHARED_LINKER_FLAGS=-g -Xlinker /DEBUG:FULL" "-DCMAKE_EXE_LINKER_FLAGS=-g -Xlinker /DEBUG:FULL" ^
  -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=OFF ^
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF ^
  -DREXGLUE_BUILD_TESTS=OFF || exit /b 1
cmake --build "%BUILD%" --target rexruntime rexgpu-xenos || exit /b 1
dir /s /b "%BUILD%\*.pdb" "%BUILD%\rexruntime.dll" "%BUILD%\rexgpu-xenos.dll"
exit /b 0
