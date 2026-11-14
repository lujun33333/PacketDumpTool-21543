$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $PSScriptRoot 'OfficialKartRefitInventoryRegression.cpp'
$builder = Join-Path $serverRoot 'Msg28346Builder.cpp'
$login100Plain = Join-Path $serverRoot 'Msg100LoginPlain.cpp'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$directRaw = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_100578_inner_dispatch_body_msg271_len28538.bin'
$expanded = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_100579_inner_dispatch_out_full_msg271_len135282.bin'
$expanded318 = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_13396 极品号\payloads\network_bin_13396_100455_special28346_after_full_msg318_len135011.bin'

foreach ($path in @($vsDevCmd, $directRaw, $expanded, $expanded318)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required regression input not found: $path"
    }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qqspeed-kart-refit-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $exe = Join-Path $tempRoot 'OfficialKartRefitInventoryRegression.exe'
    $includeRoot = Join-Path $serverRoot 'include'
    $includeJdbc = Join-Path $includeRoot 'jdbc\cppconn'
    $compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /wd4100 /wd4244 /wd4245 /wd4251 /wd4267 /wd4275 /wd4505 /wd4838 /utf-8 /I"{1}" /I"{2}" /I"{3}" "{4}" "{5}" "{6}" /Fe:"{7}" /link /NODEFAULTLIB:spdlog.lib' -f `
        $vsDevCmd, $serverRoot, $includeRoot, $includeJdbc, $cpp, $builder, $login100Plain, $exe
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "Kart-refit regression compile failed with exit code $LASTEXITCODE"
    }

    & $exe $directRaw $expanded $expanded318
    if ($LASTEXITCODE -ne 0) {
        throw "Kart-refit regression failed with exit code $LASTEXITCODE"
    }

    $message = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.cpp') -Raw
    $messageHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.h') -Raw
    $builderSource = Get-Content -LiteralPath $builder -Raw
    $builderHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Msg28346Builder.h') -Raw
    $player = Get-Content -LiteralPath (Join-Path $serverRoot 'Player.cpp') -Raw
    $database = Get-Content -LiteralPath (Join-Path $serverRoot 'Database.cpp') -Raw

    foreach ($removedSymbol in @(
        'BuildOfficialPlayerItems437Direct',
        'BuildOfficialPlayerItems438Direct',
        'QQS_SendOfficialInventoryDirect',
        'RequestOfficialInventory437Direct'
    )) {
        if ($message.Contains($removedSymbol) -or
            $builderSource.Contains($removedSymbol) -or
            $builderHeader.Contains($removedSymbol)) {
            throw "Non-official direct inventory compatibility remains: $removedSymbol"
        }
    }

    $dispatchStart = $message.IndexOf('void OnRecvFromClient')
    $dispatchEnd = $message.IndexOf('void OnFullReceive', $dispatchStart)
    if ($dispatchStart -lt 0 -or $dispatchEnd -le $dispatchStart) {
        throw 'Unable to isolate OnRecvFromClient for source audit'
    }
    $dispatch = $message.Substring($dispatchStart, $dispatchEnd - $dispatchStart)
    if ($dispatch -match '(?m)^\s*case\s+(271|437)\s*:') {
        throw 'Direct client request dispatch for Msg271/437 remains'
    }
    if (-not $message.Contains('static_cast<UINT>(requestProtocolSeq) + 1u') -or
        -not $message.Contains('SendToClientWithWireDstIDAndMsgSeq')) {
        throw 'Official safe inventory response sequence handling is missing'
    }
    if (-not $messageHeader.Contains('RefitLuckyValueMaxWeight = 0') -or
        -not $player.Contains('kOfficialKartRefitLuckyValueByLevel') -or
        -not $database.Contains('ORDER BY KartID ASC')) {
        throw 'Official Msg271 structure or ordering source changes are missing'
    }
    $requestStart = $player.IndexOf('void RequestKartRefitInfo')
    $requestEnd = $player.IndexOf('bool ResponseKartRefitInfo(', $requestStart)
    if ($requestEnd -lt 0) {
        $requestEnd = $player.IndexOf('void ResponseKartRefitInfo(', $requestStart)
    }
    if ($requestStart -lt 0 -or $requestEnd -le $requestStart) {
        throw 'Unable to isolate RequestKartRefitInfo for source audit'
    }
    $requestFn = $player.Substring($requestStart, $requestEnd - $requestStart)
    if ($requestFn -notmatch 'SendOfficialKartRefit318Page') {
        throw 'Kart-refit continuation must use official 28346(Msg318)'
    }
    if ($requestFn -notmatch 'ResponseKartRefitInfo\(' -or
        $requestFn -notmatch 'Safe318' -or
        -not $player.Contains('Client, 28346, Body28346.data()')) {
        throw 'Kart-refit request must send native Msg271 followed by 28346(Msg318)'
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
