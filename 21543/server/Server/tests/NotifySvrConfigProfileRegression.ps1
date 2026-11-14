$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $PSScriptRoot 'NotifySvrConfigProfileRegression.cpp'
$profileCpp = Join-Path $serverRoot 'NotifySvrConfigProfile.cpp'
$config = Join-Path $serverRoot 'bin\Config\NotifySvrConfig.yml'
$dumpValidator = Join-Path $PSScriptRoot 'ValidateNotifySvrConfig4WithOfficialDump.py'
$officialDump = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_25880\network_dump_25880.log'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$yamlLibDir = Join-Path (Split-Path -Parent $serverRoot) 'lib\x64'

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command file not found: $vsDevCmd"
}
if (-not (Test-Path -LiteralPath $officialDump)) {
    throw "Latest official 25880 network dump not found: $officialDump"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qqspeed-notify-config-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $exe = Join-Path $tempRoot 'NotifySvrConfigProfileRegression.exe'
    $compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /MT /DYAML_CPP_STATIC_DEFINE /I"{1}" /I"{1}\include" "{2}" "{3}" /Fe:"{4}" /link /LIBPATH:"{5}" yaml-cpp.lib' -f `
        $vsDevCmd, $serverRoot, $cpp, $profileCpp, $exe, $yamlLibDir
    & cmd.exe /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "NotifySvrConfig regression compile failed with exit code $LASTEXITCODE"
    }

    & $exe $config $tempRoot
    if ($LASTEXITCODE -ne 0) {
        throw "NotifySvrConfig regression failed with exit code $LASTEXITCODE"
    }

    & python $dumpValidator $officialDump (Join-Path $tempRoot 'config4-tail.bin')
    if ($LASTEXITCODE -ne 0) {
        throw "official msg25066 comparison failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
