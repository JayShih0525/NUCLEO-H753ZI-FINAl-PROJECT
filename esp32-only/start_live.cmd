@echo off
cd /d "%~dp0"
"..\.venv\Scripts\python.exe" -B -u -m host.live_camera
if errorlevel 1 pause
