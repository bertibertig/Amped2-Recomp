@echo off
rem Amped2-Recomp launcher (Windows): starts amped2_recomp.exe from this folder with the default settings.
rem The console output goes to run.log next to this file (attach it to bug reports).
rem Options: set them before starting, e.g.  set AMPED2_SKIP_INTRO=1  then  run.bat
setlocal
cd /d "%~dp0"
if not exist "game_files\default.xbe" (
  echo game_files\default.xbe missing: put the extracted contents of your Amped 2 Xbox disc ^(PAL^) into
  echo %~dp0game_files\  - see README.txt, section 1.
  pause
  exit /b 1
)
rem Reserve the whole 4 GB guest window up front: without it Wine/Proton cannot map the GPU's tiled memory
rem window (0xF0000000) and the game crashes at the first movie frame.
if not defined RECOMP_WIN_PLACEHOLDER set RECOMP_WIN_PLACEHOLDER=1
if not defined RECOMP_TITLE_KEVENTS set RECOMP_TITLE_KEVENTS=1
if not defined RECOMP_VBLANK set RECOMP_VBLANK=1
if not defined RECOMP_AC97_READY set RECOMP_AC97_READY=1
if not defined RECOMP_HEAP_RECLAIM set RECOMP_HEAP_RECLAIM=1
if not defined RECOMP_FLIP_VSYNC set RECOMP_FLIP_VSYNC=1
if not defined RECOMP_PB_EXEC set RECOMP_PB_EXEC=1
if not defined RECOMP_FB_WINDOW set RECOMP_FB_WINDOW=1
if not defined RECOMP_USB set RECOMP_USB=1
if not defined RECOMP_KEYBOARD set RECOMP_KEYBOARD=1
if not defined AMPED2_SKIP_LOGOS set AMPED2_SKIP_LOGOS=1
echo Starting Amped 2 (output: run.log) ...
"%~dp0amped2_recomp.exe" %* > "%~dp0run.log" 2>&1
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo amped2_recomp.exe ended with exit code %RC% - see run.log
  pause
)
exit /b %RC%
