@echo off
setlocal
set "ROOT=%~dp0.."
set "PROJECT=%~dp0Fable2.Launcher\Fable2.Launcher.csproj"
set "OUTPUT=%ROOT%\out\tests\launcher-build"

rem Default is self-contained: the exe bundles the .NET 8 Desktop Runtime, so
rem users need no .NET install. Pass dev or framework-dependent for a small,
rem fast build that requires the .NET 8 Desktop Runtime on the target machine.
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
echo Launcher built: %OUTPUT%\Fable2Launcher.exe
