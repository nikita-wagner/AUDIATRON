@echo off
rem -------------------------------------------------------------------------
rem  build_win98.bat  -  builds build\audiatron_win98.exe for Windows 98
rem
rem  32-bit, WinMM backend only, no SSE, linked against msvcrt.dll (which
rem  Windows 98 ships). Uses the 32-bit MinGW GCC of MSYS2, wherever you start
rem  this from - no need to open the right MSYS2 shell.
rem
rem  One-time setup (any MSYS2 shell):
rem      pacman -S mingw-w64-i686-gcc
rem  NOT "pacman -S gcc": that is a different compiler, for MSYS2 itself.
rem
rem  MSYS2 not in C:\msys64?   set MSYS2_ROOT=D:\somewhere\msys64 first.
rem
rem  Compiles EVERY .c file under src\ except backend\wasapi\, same as build.bat.
rem -------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

rem Some launchers start with a PATH that lacks the Windows system folder (findstr).
where where >nul 2>nul
if errorlevel 1 set "PATH=%SystemRoot%\System32;%SystemRoot%;%PATH%"

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\mingw32\bin\gcc.exe" (
    echo The 32-bit MinGW GCC was not found in %MSYS2_ROOT%\mingw32\bin
    echo Install it from any MSYS2 shell with:  pacman -S mingw-w64-i686-gcc
    exit /b 1
)
if not exist build mkdir build

set MSYSTEM=MINGW32
set CHERE_INVOKING=1
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "gcc -std=gnu11 -O2 -march=pentium -Wall -Wextra -Isrc -DAT_NO_WASAPI -DAT_QUEUE_PORTABLE_ATOMICS $(find src -name '*.c' | grep -v backend/wasapi) -o build/audiatron_win98.exe -lwinmm -luser32"
if errorlevel 1 exit /b 1

echo.
echo Built build\audiatron_win98.exe
"%MSYS2_ROOT%\mingw32\bin\objdump.exe" -p build\audiatron_win98.exe | findstr /c:"Magic" /c:"MajorSubsystemVersion" /c:"DLL Name"
exit /b 0
