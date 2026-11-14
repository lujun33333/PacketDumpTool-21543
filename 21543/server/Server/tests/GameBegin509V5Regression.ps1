$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $PSScriptRoot 'GameBegin509V5Regression.cpp'
$builder = Join-Path $serverRoot 'GameBegin509V5Builder.cpp'
$safeBuilder = Join-Path $serverRoot 'Msg28346Builder.cpp'
$login100Plain = Join-Path $serverRoot 'Msg100LoginPlain.cpp'
$pythonTest = Join-Path $PSScriptRoot 'ValidateGameBegin509V5WithLatestClient.py'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$researchNetworkDll = 'F:\QQ飞车 - 21543\Releasephysx27_64\Network.dll'
$networkDependencyDirectory = 'F:\QQ飞车 - 21543\Releasephysx27_64'
$currentNetworkDll = 'F:\QQ飞车\Releasephysx27_64\Network.dll'
$currentSafeValidator = Join-Path $serverRoot 'tools\validate_current_normal_509.py'
$officialSafePairs = @(
    @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_32779_special28346_before_inner_body_at12_msg509_len1015.bin',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_32780_special28346_after_full_msg509_len43775.bin'
    ),
    @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_37991_special28346_before_inner_body_at12_msg509_len1184.bin',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_15588极品号\payloads\network_bin_15588_37992_special28346_after_full_msg509_len43775.bin'
    ),
    @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_17072活动\payloads\network_bin_17072_21349_special28346_before_inner_body_at12_msg509_len1476.bin',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_17072活动\payloads\network_bin_17072_21350_special28346_after_full_msg509_len43775.bin'
    ),
    @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_41440\payloads\network_bin_41440_6445_special28346_before_inner_body_at12_msg509_len701.bin',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_41440\payloads\network_bin_41440_6446_special28346_after_full_msg509_len43775.bin'
    ),
    @(
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_6752\payloads\network_bin_6752_7868_special28346_before_inner_body_at12_msg509_len714.bin',
        'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_6752\payloads\network_bin_6752_7869_special28346_after_full_msg509_len43779.bin'
    )
)

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command file not found: $vsDevCmd"
}
if (-not (Test-Path -LiteralPath $researchNetworkDll)) {
    throw "Research-client 21543 Network.dll not found: $researchNetworkDll"
}
if (-not (Test-Path -LiteralPath $currentNetworkDll)) {
    throw "Current-client Network.dll not found: $currentNetworkDll"
}
foreach ($pair in $officialSafePairs) {
    foreach ($path in $pair) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Official Msg509 safe fixture not found: $path"
        }
    }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qqspeed-509-v5-" + [Guid]::NewGuid().ToString('N'))
$payloadRoot = Join-Path $tempRoot 'payloads'
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null

try {
    $exe = Join-Path $tempRoot 'GameBegin509V5Regression.exe'
    $includeRoot = Join-Path $serverRoot 'include'
    $includeJdbc = Join-Path $includeRoot 'jdbc\cppconn'
    $compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /wd4100 /wd4244 /wd4245 /wd4251 /wd4267 /wd4275 /wd4505 /wd4838 /utf-8 /I"{1}" /I"{2}" /I"{3}" "{4}" "{5}" "{6}" "{7}" /Fe:"{8}" /link /NODEFAULTLIB:spdlog.lib' -f `
        $vsDevCmd, $serverRoot, $includeRoot, $includeJdbc, $cpp, $builder, $safeBuilder, $login100Plain, $exe
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "GameBegin509V5 regression compile failed with exit code $LASTEXITCODE"
    }

    $regressionArguments = @($payloadRoot)
    foreach ($pair in $officialSafePairs) {
        $regressionArguments += $pair[1]
    }
    & $exe @regressionArguments
    if ($LASTEXITCODE -ne 0) {
        throw "GameBegin509V5 C++ regression failed with exit code $LASTEXITCODE"
    }

    $researchArguments = @(
        $pythonTest,
        $payloadRoot,
        '--layout', 'research-21543',
        '--network-dll', $researchNetworkDll,
        '--dependency-directory', $networkDependencyDirectory,
        '--skip-generated-safe'
    )
    & python @researchArguments
    if ($LASTEXITCODE -ne 0) {
        throw "research-client 21543 validation failed with exit code $LASTEXITCODE"
    }

    & python $currentSafeValidator `
        --dll $currentNetworkDll `
        --root $payloadRoot `
        --all
    if ($LASTEXITCODE -ne 0) {
        throw "current-client Msg509 safe validation failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
