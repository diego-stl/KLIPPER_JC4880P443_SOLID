param(
    [ValidateRange(1, 65535)]
    [int]$Port = 17125,
    [ValidatePattern('^(\d{1,3}\.){3}\d{1,3}$')]
    [string]$Bind = '0.0.0.0',
    [string]$Python = '',
    [switch]$Background,
    [switch]$Restart
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$fixturePath = Join-Path $projectRoot 'tests\klipper_simulator.py'

if (-not $Python) {
    $candidates = @()
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidates += Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
    }
    $candidates += @(
        'C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe',
        'D:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe'
    )
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand -and $pythonCommand.Source -notmatch '\\WindowsApps\\') {
        $candidates += $pythonCommand.Source
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $Python = $candidate
            break
        }
    }
}
if (-not $Python -or -not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw 'No se encontro Python. Indica su ruta con -Python C:\ruta\python.exe'
}

$healthHost = if ($Bind -eq '0.0.0.0') { '127.0.0.1' } else { $Bind }
$healthUrl = "http://${healthHost}:$Port/__test/identity"
$existingIdentity = $null
try { $existingIdentity = Invoke-RestMethod -Uri $healthUrl -TimeoutSec 1 } catch { }
if ($existingIdentity) {
    if ($existingIdentity.service -ne 'JC4880P443_MOONRAKER_SIMULATOR' -or $existingIdentity.test_only -ne $true) {
        throw "El puerto $Port pertenece a otro servicio; no se detendra."
    }
    if (-not $Restart) {
        $existingStatus = Invoke-RestMethod -Uri "http://${healthHost}:$Port/__test/status" -TimeoutSec 2
        if (-not $existingStatus.interactive) { throw 'Servidor antiguo activo. Usa -Background -Restart para actualizarlo.' }
        Write-Host "Servidor interactivo ya activo en el puerto $Port. Menu: tools\moonraker_mock.cmd"
        exit 0
    }
    $listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction Stop
    foreach ($owner in ($listener.OwningProcess | Select-Object -Unique)) {
        $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId=$owner"
        if (-not $processInfo.CommandLine.Contains($fixturePath) -or
            $processInfo.CommandLine -notmatch "--port\s+$Port(?:\s|$)") {
            throw 'El proceso no coincide con el simulador de este proyecto; no se detendra.'
        }
        Stop-Process -Id $owner -ErrorAction Stop
    }
    Start-Sleep -Milliseconds 400
}

Write-Host "Simulador Moonraker INTERACTIVO: HTTP + WebSocket, puerto $Port"
Write-Host 'En la pantalla: Ajustes > WI-FI Y MOONRAKER > Moonraker.'
if ($Bind -eq '0.0.0.0') {
    foreach ($network in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if ($network.OperationalStatus -ne 'Up') { continue }
        foreach ($address in $network.GetIPProperties().UnicastAddresses) {
            if ($address.Address.AddressFamily -eq 'InterNetwork' -and
                -not [System.Net.IPAddress]::IsLoopback($address.Address)) {
                Write-Host "Host/IP: $($address.Address.IPAddressToString)    Puerto: $Port"
            }
        }
    }
} else {
    Write-Host "Host/IP: $Bind    Puerto: $Port"
}

if (-not $Background) {
    Write-Host 'Ctrl+C para detener. Las ordenes recibidas se consultan en /__test/requests.'
    & $Python -u $fixturePath --interactive --bind $Bind --port $Port
    exit $LASTEXITCODE
}

$logDir = Join-Path $projectRoot '.cache'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$logPath = Join-Path $logDir "moonraker-mock-$Port.log"
$errorPath = Join-Path $logDir "moonraker-mock-$Port.err.log"
$mockProcess = Start-Process -FilePath $Python -WindowStyle Hidden -PassThru `
    -WorkingDirectory $projectRoot `
    -ArgumentList @('-u', ('"{0}"' -f $fixturePath), '--interactive', '--bind', $Bind, '--port', "$Port") `
    -RedirectStandardOutput $logPath -RedirectStandardError $errorPath

for ($attempt = 0; $attempt -lt 20; $attempt++) {
    Start-Sleep -Milliseconds 250
    $mockProcess.Refresh()
    if ($mockProcess.HasExited) {
        throw "El simulador no pudo iniciar. $(Get-Content -LiteralPath $errorPath -Raw)"
    }
    try {
        $identity = Invoke-RestMethod -Uri $healthUrl -TimeoutSec 1
        if ($identity.service -eq 'JC4880P443_MOONRAKER_SIMULATOR' -and $identity.test_only -eq $true) {
            $interactiveStatus = Invoke-RestMethod -Uri "http://${healthHost}:$Port/__test/status" -TimeoutSec 1
            if (-not $interactiveStatus.interactive) { continue }
            Write-Host "Servidor activo. PID: $($mockProcess.Id)"
            Write-Host 'Menu: tools\moonraker_mock.cmd (CMD/PowerShell) o bash tools/moonraker_mock.sh (Bash)'
            Write-Host "Detener: Stop-Process -Id $($mockProcess.Id)"
            Write-Host "Registro: $logPath"
            exit 0
        }
    } catch {
        # The process may still be starting; retry within the bounded startup window.
    }
}
Stop-Process -Id $mockProcess.Id -ErrorAction SilentlyContinue
throw "No se pudo verificar el simulador. Revisa $errorPath"
