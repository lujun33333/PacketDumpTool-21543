$ErrorActionPreference = 'Continue'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Run = Join-Path $Root 'run'
$expectedExecutables = @{
    'lobby-a' = Join-Path $Root 'LobbyA\SpeedServer.exe'
    'lobby-b' = Join-Path $Root 'LobbyB\SpeedServer.exe'
    'worker' = Join-Path $Root 'CrossWorker\SpeedServer.exe'
}

foreach ($name in 'lobby-b', 'lobby-a', 'worker', 'coordinator') {
    $pidFile = Join-Path $Run "$name.pid"
    if (-not (Test-Path -LiteralPath $pidFile -PathType Leaf)) { continue }
    $processId = 0
    if (-not [int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(), [ref]$processId)) {
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
        continue
    }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$processId" -ErrorAction SilentlyContinue
    if ($process) {
        $isExpected = $false
        if ($name -eq 'coordinator') {
            $isExpected = $process.Name -eq 'python.exe' -and
                $process.CommandLine -like '*Coordinator*coordinator.py*'
        }
        else {
            $expected = [IO.Path]::GetFullPath($expectedExecutables[$name])
            $actual = if ($process.ExecutablePath) { [IO.Path]::GetFullPath($process.ExecutablePath) } else { '' }
            $isExpected = $actual -eq $expected
        }
        if ($isExpected) {
            Stop-Process -Id $processId
            Wait-Process -Id $processId -Timeout 10 -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
}
