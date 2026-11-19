@echo off
rem Star Studio launcher: uses the project's Python environment (.venv) when present.
setlocal
set "ROOT=%~dp0.."
if exist "%ROOT%\.venv\Scripts\python.exe" (
  "%ROOT%\.venv\Scripts\python.exe" "%~dp0star_studio.py" %*
) else (
  python "%~dp0star_studio.py" %*
)
