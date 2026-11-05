param([ValidatePattern('^COM[0-9]+$')][string]$Port='COM8')
$ErrorActionPreference='Stop'
$python='C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe'
$project=Split-Path -Parent $PSScriptRoot
$snapshot=Join-Path $project 'verification\orientation\firmware-0.1.0'
foreach($entry in (Get-Content -LiteralPath (Join-Path $snapshot 'SHA256SUMS.txt'))) {
    if($entry -match '^([a-fA-F0-9]{64})  (.+)$') {
        $expected=$Matches[1];$file=Join-Path $snapshot $Matches[2]
        if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected){throw "Hash no coincide: $file"}
    }
}
Push-Location $snapshot
try {
    & $python -m esptool --chip esp32p4 --port $Port --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 klipper_jc4880p443_solid.bin
    exit $LASTEXITCODE
}
finally {Pop-Location}
