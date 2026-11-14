$ErrorActionPreference = 'Stop'

$port = 8096
$root = 'F:\PacketDumpTool\21543\server\Server\bin\ChallengeFiles'
$pidFile = Join-Path $PSScriptRoot 'ChallengeHttp.pid'
$stdoutLog = Join-Path $PSScriptRoot 'ChallengeHttp.stdout.log'
$stderrLog = Join-Path $PSScriptRoot 'ChallengeHttp.stderr.log'

New-Item -ItemType Directory -Path $root -Force | Out-Null

$listener = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($listener) {
    Write-Output "already-listening pid=$($listener.OwningProcess) port=$port"
    exit 0
}

$python = (Get-Command python.exe -ErrorAction Stop).Source
$process = Start-Process `
    -FilePath $python `
    -ArgumentList @('-m', 'http.server', "$port", '--bind', '0.0.0.0', '--directory', $root) `
    -WorkingDirectory $root `
    -WindowStyle Hidden `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -PassThru

Set-Content -LiteralPath $pidFile -Value $process.Id -Encoding ascii
Start-Sleep -Milliseconds 750

$listener = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue |
    Where-Object OwningProcess -eq $process.Id |
    Select-Object -First 1
if (-not $listener) {
    throw "challenge HTTP server failed to listen on port $port; see $stderrLog"
}

Write-Output "started pid=$($process.Id) bind=0.0.0.0:$port root=$root"
