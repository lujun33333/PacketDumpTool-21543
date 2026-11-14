@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cleanup_player_activity_tables.ps1" -Execute
echo.
pause
