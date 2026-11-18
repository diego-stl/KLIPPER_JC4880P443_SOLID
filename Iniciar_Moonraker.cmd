@echo off
setlocal

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0final\tools\moonraker_mock.ps1" -Background
set "MOONRAKER_EXIT_CODE=%ERRORLEVEL%"

if not "%MOONRAKER_EXIT_CODE%"=="0" echo No se pudo iniciar el simulador. Revisa el mensaje anterior.
if /i not "%~1"=="--no-pause" (
    echo.
    echo Pulsa una tecla para cerrar esta ventana.
    pause >nul
)
exit /b %MOONRAKER_EXIT_CODE%
