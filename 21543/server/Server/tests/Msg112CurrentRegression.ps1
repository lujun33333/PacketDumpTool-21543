param(
    [string[]]$DumpPaths = @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_16480\network_packets_16480.pdump',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_32312\network_packets_32312.pdump',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_21256\network_packets_21256.pdump',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_25156\network_packets_25156.pdump'
    )
)

$ErrorActionPreference = 'Stop'
$serverRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $PSScriptRoot 'Msg112CurrentRegression.cpp'
$router = Join-Path $serverRoot 'Msg28346Router.cpp'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command file not found: $vsDevCmd"
}
foreach ($dump in $DumpPaths) {
    if (-not (Test-Path -LiteralPath $dump)) {
        throw "Official packet dump not found: $dump"
    }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'qqspeed-msg112-current-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $exe = Join-Path $tempRoot 'Msg112CurrentRegression.exe'
    $compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /utf-8 /I"{1}" "{2}" "{3}" /Fe:"{4}" /link bcrypt.lib' -f `
        $vsDevCmd, $serverRoot, $cpp, $router, $exe
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "Msg112 current regression compile failed with exit code $LASTEXITCODE"
    }

    & $exe @DumpPaths
    if ($LASTEXITCODE -ne 0) {
        throw "Msg112 current regression failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
