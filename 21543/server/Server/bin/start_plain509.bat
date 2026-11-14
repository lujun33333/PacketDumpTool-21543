@echo off
rem Daily driver: send NotifyGameBegin 509 as a plain top-level message so client 21669
rem can start a race, without the Uin-10001 debug logging that slows the console down.
rem Use start_debug10001_plain509.bat instead when you need Logs\debug10001.txt.
set QQS_GAMEBEGIN509_PLAIN=1
cd /d %~dp0
SpeedServer.exe
pause
