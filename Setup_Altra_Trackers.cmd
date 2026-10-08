@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_altra_trackers.ps1"
if errorlevel 1 pause
