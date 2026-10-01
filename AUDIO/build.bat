@echo off
rem -------------------------------------------------------------------------
rem  build.bat [debug] [winmm]  -  builds with MSVC
rem
rem    build.bat          build\audiatron.exe        both backends; picks WASAPI
rem                                                  or WinMM when it starts
rem    build.bat winmm    build\audiatron_winmm.exe  WinMM backend only: no
rem                                                  WASAPI code, no COM
rem    add "debug" for an unoptimized build with runtime checks
rem
rem  Compiles EVERY .c file under src\, in all subfolders. To add a module,
rem  create its folder and files - nothing to change here.
rem
rem  Rule: .c file names must be unique across all folders, because every
rem  object file lands in build\obj\. The script stops if two collide.
rem
rem  Finds Visual Studio via vswhere if cl.exe isn't already on PATH.
rem -------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

rem --- options -------------------------------------------------------------
set "DEBUG="
set "WINMM_ONLY="
:next_option
if "%~1"=="" goto options_done
if /i "%~1"=="debug" set "DEBUG=1" & shift & goto next_option
if /i "%~1"=="winmm" set "WINMM_ONLY=1" & shift & goto next_option
echo unknown option "%~1" - use: build.bat [debug] [winmm]
exit /b 1
:options_done

rem Some launchers start with a PATH that lacks the Windows system folder;
rem the Visual Studio setup script silently fails without it.
where where >nul 2>nul
if errorlevel 1 set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0;%PATH%"

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
if not exist build\obj mkdir build\obj

rem --- collect every .c file under src\ into a response file ---------------
if exist build\sources.rsp del build\sources.rsp
set "DUPLICATE="
set "COUNT=0"
for /r src %%f in (*.c) do call :add_source "%%f"
if defined DUPLICATE exit /b 1

set "CFLAGS=/nologo /std:c11 /W4 /Zi /MP /Isrc /D_CRT_SECURE_NO_WARNINGS"
if defined DEBUG (
    set "CFLAGS=%CFLAGS% /Od /RTC1 /D_DEBUG"
) else (
    set "CFLAGS=%CFLAGS% /O2 /fp:fast /DNDEBUG"
)

rem WinMM-only: the WASAPI folder is left out (see :add_source) and the
rem backend selector is told so. Nothing from ole32 is needed then.
set "OUTPUT=build\audiatron.exe"
set "LIBS=winmm.lib user32.lib ole32.lib"
if defined WINMM_ONLY (
    set "CFLAGS=%CFLAGS% /DAT_NO_WASAPI"
    set "OUTPUT=build\audiatron_winmm.exe"
    set "LIBS=winmm.lib user32.lib"
)

cl %CFLAGS% @build\sources.rsp /Fobuild\obj\ /Fdbuild\ /Fe%OUTPUT% /link /INCREMENTAL:NO %LIBS%
if errorlevel 1 exit /b 1
echo.
echo Built %OUTPUT% from %COUNT% source files
exit /b 0

rem --- :add_source "full\path\file.c" ---------------------------------------
:add_source
set "FULL=%~1"
if defined WINMM_ONLY if not "%FULL:\backend\wasapi\=%"=="%FULL%" exit /b 0
if defined SEEN_%~n1 goto duplicate_source
set "SEEN_%~n1=%~1"
set /a COUNT+=1
set "REL=%~1"
call set "REL=%%REL:%CD%\=%%"
>>build\sources.rsp echo "%REL%"
exit /b 0

:duplicate_source
echo ERROR: two source files are named %~nx1 - file names must be unique:
call echo   %%SEEN_%~n1%%
echo   %~1
set "DUPLICATE=1"
exit /b 0
