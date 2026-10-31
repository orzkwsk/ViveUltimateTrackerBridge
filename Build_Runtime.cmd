@echo off
setlocal EnableExtensions
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_runtime.ps1" %*
exit /b %ERRORLEVEL%
