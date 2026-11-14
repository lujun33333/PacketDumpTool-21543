@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0keep_speedladder_reset_other_activities.ps1" -Execute
echo.
pause
