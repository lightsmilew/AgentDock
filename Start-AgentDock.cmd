@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0dist\AgentDock-native\AgentDock.exe" (
  start "" "%~dp0dist\AgentDock-native\AgentDock.exe"
  exit /b
)
if exist "%~dp0build\cpp\AgentDock.exe" (
  start "" "%~dp0build\cpp\AgentDock.exe"
  exit /b
)
echo Please build the native app first:
echo powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-cpp.ps1 -Test
pause
