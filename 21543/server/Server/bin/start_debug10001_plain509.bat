@echo off
rem Same as start_debug10001.bat, but sends NotifyGameBegin 509 as a plain top-level
rem message instead of the safe/28346 envelope. Client 21669 rejects our safe stream at
rem its first field, so this is the temporary route until the codec tables are rebuilt.
rem Delete this file or start via start_debug10001.bat to go back to the safe path.
set QQSPEED_DEBUG_UIN10001=1
set QQS_GAMEBEGIN509_PLAIN=1
cd /d %~dp0
SpeedServer.exe
pause
