@echo off
chcp 65001 >nul
title QQSpeedLauncher 启动诊断
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0QQSpeedLauncher-Startup-Diagnose.ps1" %*
echo.
pause
