param(
    [ValidateSet('build', 'reconfigure', 'menuconfig', 'size', 'flash', 'monitor', 'flash-monitor', 'version')]
    [string]$Action = 'build',
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port = 'COM8'
)

$ErrorActionPreference = 'Stop'
$eimExecutable = 'D:\Espressif\installers\eim-cli-windows-x64-v0.20.1.exe'
if (-not (Test-Path -LiteralPath $eimExecutable)) {
    throw "No se encontro EIM en $eimExecutable"
}

# EIM activa la instalacion existente y su Python; no instala herramientas.
# -B build es relativo a ESTE proyecto, nunca al diagnostico de la raiz.
$idfCommand = switch ($Action) {
    'flash'         { "idf.py -B build -p $Port -b 460800 flash" }
    'monitor'       { "idf.py -B build -p $Port monitor" }
    'flash-monitor' { "idf.py -B build -p $Port -b 460800 flash monitor" }
    'version'       { 'idf.py --version' }
    default         { "idf.py -B build $Action" }
}

Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    # EIM 0.20.1 puede devolver 0 aunque falle idf.py. Recuperar su codigo real.
    New-Item -ItemType Directory -Force -Path '.cache' | Out-Null
    $exitCodeFile = Join-Path (Get-Location) '.cache\idf-exit-code.txt'
    if (Test-Path -LiteralPath $exitCodeFile) {
        Remove-Item -LiteralPath $exitCodeFile
    }
    $idfCommand += '; $LASTEXITCODE | Set-Content -LiteralPath ''.cache/idf-exit-code.txt'''
    & $eimExecutable --do-not-track true run $idfCommand v5.5.6
    if (-not (Test-Path -LiteralPath $exitCodeFile)) {
        throw 'EIM no devolvio el estado de idf.py; revisar la salida anterior'
    }
    $idfExitCode = [int](Get-Content -LiteralPath $exitCodeFile -Raw).Trim()
}
finally {
    Pop-Location
}
exit $idfExitCode
