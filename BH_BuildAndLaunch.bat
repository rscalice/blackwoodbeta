@echo off
rem Blackwood Hollow - full editor build + relaunch.
rem Waits for the Unreal Editor to close (Live Coding can't add new classes), then builds and relaunches.
setlocal
set ENG=D:\Epic Games\UE_5.8\Engine
set PROJ=C:\Users\Robert Scalice\Documents\Unreal Projects\blackwoodbeta\BlackwoodHollowBeta.uproject
tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe" >nul
if errorlevel 1 goto build
echo Waiting for the Unreal Editor to close... (Ctrl+C to cancel)
:wait
timeout /t 3 /nobreak >nul
tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 goto wait
rem Give the editor a moment to release file locks after the process exits.
timeout /t 3 /nobreak >nul
echo Editor closed.
:build
echo Building...
call "%ENG%\Build\BatchFiles\Build.bat" BlackwoodHollowBetaEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see output above.
  pause
  exit /b 1
)
start "" "%ENG%\Binaries\Win64\UnrealEditor.exe" "%PROJ%"
