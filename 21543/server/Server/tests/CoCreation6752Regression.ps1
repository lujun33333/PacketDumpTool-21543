$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $PSScriptRoot 'CoCreation6752Regression.cpp'
$roomBuilder = Join-Path $serverRoot 'RoomOfficialUGC.cpp'
$auroraBuilder = Join-Path $serverRoot 'AuroraOfficialUGC.cpp'
$quickRpcBuilder = Join-Path $serverRoot 'UGCQuickRpc.cpp'
$msg28346Router = Join-Path $serverRoot 'Msg28346Router.cpp'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$dumpRoot = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_6752'
$dump17188Root = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_17188'
$dump20932Root = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_20932'

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command file not found: $vsDevCmd"
}
if (-not (Test-Path -LiteralPath $dumpRoot)) {
    throw "Official packet dump not found: $dumpRoot"
}
if (-not (Test-Path -LiteralPath $dump17188Root)) {
    throw "Official packet dump not found: $dump17188Root"
}
if (-not (Test-Path -LiteralPath $dump20932Root)) {
    throw "Official packet dump not found: $dump20932Root"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qqspeed-cocreation-6752-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $exe = Join-Path $tempRoot 'CoCreation6752Regression.exe'
    $compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /utf-8 /I"{1}" "{2}" "{3}" "{4}" "{5}" "{6}" /Fe:"{7}" /link bcrypt.lib' -f `
        $vsDevCmd, $serverRoot, $cpp, $roomBuilder, $auroraBuilder, $quickRpcBuilder, $msg28346Router, $exe
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "CoCreation6752 regression compile failed with exit code $LASTEXITCODE"
    }

    & $exe $dumpRoot $dump17188Root $dump20932Root
    if ($LASTEXITCODE -ne 0) {
        throw "CoCreation6752 regression failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
