@echo off
rem -------------------------------------------------------------------------
rem  build.bat [debug]  -  builds build\audiatron.exe with MSVC
rem  Finds Visual Studio via vswhere if cl.exe isn't already on PATH.
rem -------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto have_cl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - install Visual Studio with the "Desktop development with C++" workload.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No MSVC x64 toolset found.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

:have_cl
if not exist build mkdir build

set "CFLAGS=/nologo /std:c11 /W4 /Zi /MP /Isrc /D_CRT_SECURE_NO_WARNINGS"
if /i "%~1"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /RTC1 /D_DEBUG"
) else (
    set "CFLAGS=%CFLAGS% /O2 /fp:fast /DNDEBUG"
)

cl %CFLAGS% src\*.c /Fobuild\ /Fdbuild\ /Febuild\audiatron.exe /link /INCREMENTAL:NO ole32.lib avrt.lib
if errorlevel 1 exit /b 1
echo.
echo Built build\audiatron.exe
