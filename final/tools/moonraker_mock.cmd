@echo off
setlocal
if defined MOONRAKER_PYTHON goto run
if defined IDF_PYTHON_ENV_PATH if exist "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" set "MOONRAKER_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"
if defined MOONRAKER_PYTHON goto run
if exist "C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe" set "MOONRAKER_PYTHON=C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe"
if defined MOONRAKER_PYTHON goto run
if exist "D:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe" set "MOONRAKER_PYTHON=D:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe"
if not defined MOONRAKER_PYTHON set "MOONRAKER_PYTHON=python"
:run
"%MOONRAKER_PYTHON%" "%~dp0moonraker_console.py" %*
exit /b %errorlevel%
