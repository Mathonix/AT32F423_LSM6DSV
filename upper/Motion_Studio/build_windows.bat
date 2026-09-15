@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1"
set ERR=%ERRORLEVEL%
endlocal & exit /b %ERR%
