@echo off
rem Blackwood Hollow - full editor build + relaunch.
rem Close the Unreal Editor first (Live Coding can't add new classes).
setlocal
set ENG=D:\Epic Games\UE_5.8\Engine
set PROJ=C:\Users\Robert Scalice\Documents\Unreal Projects\BlackwoodHollowBeta\BlackwoodHollowBeta.uproject
tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
  echo Unreal Editor is still running - close it and run this again.
  pause
  exit /b 1
)
call "%ENG%\Build\BatchFiles\Build.bat" BlackwoodHollowBetaEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see output above.
  pause
  exit /b 1
)
start "" "%ENG%\Binaries\Win64\UnrealEditor.exe" "%PROJ%"
