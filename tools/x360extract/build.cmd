@echo off
rem Build the x360extract CLI as a single-file exe, or pack the reusable library.
rem
rem   build.cmd                 self-contained exe (default; bundles the .NET 8
rem                             runtime, so users need no .NET install)
rem   build.cmd dev             small framework-dependent exe (needs .NET 8)
rem   build.cmd framework-dep.  same as dev
rem   build.cmd pack            pack the X360ExtractCore.dll library as a NuGet
rem                             package into out\tooling\nuget
setlocal
set "ROOT=%~dp0..\.."
set "PROJECT=%~dp0X360Extract.csproj"
set "LIBPROJECT=%~dp0lib\X360Extract.Core.csproj"
set "OUTPUT=%ROOT%\out\tooling\x360extract"
set "NUGET_OUT=%ROOT%\out\tooling\nuget"

rem pack the reusable library and stop
if /i "%~1"=="pack" (
  if exist "%NUGET_OUT%" rmdir /s /q "%NUGET_OUT%"
  dotnet pack "%LIBPROJECT%" -c Release -o "%NUGET_OUT%"
  if errorlevel 1 exit /b 1
  echo x360extract library packed: %NUGET_OUT%
  exit /b 0
)

rem Clean: drop the staged output (hard) and intermediate build artifacts
rem (best effort - editors like VS Code/C# Dev Kit may hold bin\ handles).
if exist "%OUTPUT%" rmdir /s /q "%OUTPUT%"
if exist "%~dp0bin" rmdir /s /q "%~dp0bin" 2>nul
if exist "%~dp0obj" rmdir /s /q "%~dp0obj" 2>nul
if exist "%~dp0lib\bin" rmdir /s /q "%~dp0lib\bin" 2>nul
if exist "%~dp0lib\obj" rmdir /s /q "%~dp0lib\obj" 2>nul

set "SELF_CONTAINED=true"
if /i "%~1"=="dev" set "SELF_CONTAINED=false"
if /i "%~1"=="framework-dependent" set "SELF_CONTAINED=false"

if "%SELF_CONTAINED%"=="true" (
  dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained true ^
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true ^
    -o "%OUTPUT%"
) else (
  dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained false ^
    -p:PublishSingleFile=true -o "%OUTPUT%"
)

if errorlevel 1 exit /b 1
echo x360extract built: %OUTPUT%\x360extract.exe
