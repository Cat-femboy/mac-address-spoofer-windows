@echo off
cd /d "%~dp0"
if exist "%~dp0MAC-Spoofer.exe" (
    start "" "%~dp0MAC-Spoofer.exe"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Spoof-Mac.ps1" %*
)
