param([string]$Compiler = 'D:\PROYECTOS\DISPOSITIVOS\ES3C28P ESP32-S3 DISPLAY\PROYECTOS\KLIPPER_ES3C28P_SOLID\output\pantallas\herramientas\tcc\tcc.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    & $Compiler -std=c11 -Wall -Werror -Icomponents\display_orientation\include tests\orientation_regression.c components\display_orientation\display_orientation.c -o verification\orientation\orientation_regression.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\verification\orientation\orientation_regression.exe
    exit $LASTEXITCODE
}
finally { Pop-Location }
