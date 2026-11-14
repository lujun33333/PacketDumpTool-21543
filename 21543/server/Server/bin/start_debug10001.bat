@echo off
rem Start SpeedServer with Uin-10001 debug logging enabled.
rem [SEND-LOWLEVEL-FAIL] / [KART-REFIT] lines will appear on this console
rem and in Logs\debug10001.txt.
set QQSPEED_DEBUG_UIN10001=1
cd /d %~dp0
SpeedServer.exe
pause
