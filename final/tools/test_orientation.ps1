param([string]$Compiler = 'D:\PROYECTOS\DISPOSITIVOS\ES3C28P ESP32-S3 DISPLAY\PROYECTOS\KLIPPER_ES3C28P_SOLID\output\pantallas\herramientas\tcc\tcc.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Force -Path '.cache' | Out-Null
    & $Compiler -std=c11 -Wall -Werror -include tests\host_c11_compat.h -Icomponents\display_orientation\include tests\orientation_regression.c components\display_orientation\display_orientation.c -o .cache\orientation_regression.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\.cache\orientation_regression.exe
    exit $LASTEXITCODE
}
finally { Pop-Location }
