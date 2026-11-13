param([string]$Compiler = 'D:\PROYECTOS\DISPOSITIVOS\ES3C28P ESP32-S3 DISPLAY\PROYECTOS\KLIPPER_ES3C28P_SOLID\output\pantallas\herramientas\tcc\tcc.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    New-Item -ItemType Directory -Path '.cache' -Force | Out-Null
    foreach ($test in @('performance', 'orientation', 'serial_menu')) {
        $sources = if ($test -eq 'serial_menu') {
            @('components/device_network/tests/serial_menu_regression.c', 'components/device_network/serial_menu.c')
        } else {
            @("tests/${test}_regression.c", 'components/display_orientation/display_orientation.c')
        }
        $binary = ".cache/${test}-regression.exe"
        $arguments = @('-std=c11', '-Wall', '-Werror', '-include', 'tests/host_c11_compat.h',
            '-Icomponents/display_orientation/include', '-Icomponents/device_network/include',
            '-Icomponents/device_network', '-Icomponents/gui_port', '-Itests/portal_host') + $sources + @('-o', $binary)
        & $Compiler @arguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path (Get-Location) $binary)
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}
finally { Pop-Location }
