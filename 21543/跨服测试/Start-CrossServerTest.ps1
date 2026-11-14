$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Run = Join-Path $Root 'run'
$Coordinator = Join-Path $Root 'Coordinator'
$LobbyA = Join-Path $Root 'LobbyA'
$LobbyB = Join-Path $Root 'LobbyB'
$Worker = Join-Path $Root 'CrossWorker'
$StopScript = Join-Path $Root 'Stop-CrossServerTest.ps1'
New-Item -ItemType Directory -Force -Path $Run | Out-Null

$requiredPorts = 28980, 28981, 28982, 19527, 19528, 19529, 19600
foreach ($port in $requiredPorts) {
    if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) {
        throw "TCP port $port is already in use."
    }
}

function Wait-Listener {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [int] $Port,
        [Parameter(Mandatory)] [string] $Name,
        [int] $TimeoutSeconds = 120
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 500
        if ($Process.HasExited) {
            throw "$Name exited early with code $($Process.ExitCode)."
        }
        $ready = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
    } until ($ready -or (Get-Date) -gt $deadline)
    if (-not $ready) {
        throw "$Name did not listen on TCP $Port within $TimeoutSeconds seconds."
    }
}

function Start-Server {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Directory,
        [Parameter(Mandatory)] [int] $Port
    )
    $exe = Join-Path $Directory 'SpeedServer.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Missing server executable: $exe"
    }
    $process = Start-Process -FilePath $exe -WorkingDirectory $Directory `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $Run "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $Run "$Name.stderr.log")
    $process.Id | Set-Content -LiteralPath (Join-Path $Run "$Name.pid") -Encoding ascii
    Wait-Listener -Process $process -Port $Port -Name $Name
    return $process
}

try {
    $env:PYTHONUNBUFFERED = '1'
    $env:QQSPEED_DEBUG_UIN10001 = '1'

    $coordinatorProcess = Start-Process -FilePath 'python' `
        -ArgumentList @((Join-Path $Coordinator 'coordinator.py'), '--config', (Join-Path $Coordinator 'coordinator.json')) `
        -WorkingDirectory $Coordinator -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $Run 'coordinator.stdout.log') `
        -RedirectStandardError (Join-Path $Run 'coordinator.stderr.log')
    $coordinatorProcess.Id | Set-Content -LiteralPath (Join-Path $Run 'coordinator.pid') -Encoding ascii
    Wait-Listener -Process $coordinatorProcess -Port 19600 -Name 'coordinator' -TimeoutSeconds 15

    $workerProcess = Start-Server -Name 'worker' -Directory $Worker -Port 28981
    $lobbyAProcess = Start-Server -Name 'lobby-a' -Directory $LobbyA -Port 28980
    $lobbyBProcess = Start-Server -Name 'lobby-b' -Directory $LobbyB -Port 28982

    Write-Host "Coordinator PID=$($coordinatorProcess.Id) TCP=19600"
    Write-Host "Lobby A (测试1区) PID=$($lobbyAProcess.Id) TCP=28980 Delivery=19527"
    Write-Host "Lobby B (测试2区) PID=$($lobbyBProcess.Id) TCP=28982 Delivery=19528"
    Write-Host "Cross Worker PID=$($workerProcess.Id) TCP=28981 Delivery=19529"
}
catch {
    & $StopScript
    throw
}
