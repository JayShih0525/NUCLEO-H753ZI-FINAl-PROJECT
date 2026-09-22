@echo off
setlocal
pushd "%~dp0"
if exist "..\..\..\.venv\Scripts\python.exe" (
  "..\..\..\.venv\Scripts\python.exe" -B -u run_camera.py
) else (
  python -B -u run_camera.py
)
set "PQC_RESULT=%ERRORLEVEL%"
popd
exit /b %PQC_RESULT%
