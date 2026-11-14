param(
    [string]$Fixture = (Join-Path $PSScriptRoot 'fixtures\msg25116_merged_20448_23592_uin10001_wire_body.bin')
)

$ErrorActionPreference = 'Stop'

$serverDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "vcvars64.bat not found: $vcvars"
}
if (-not (Test-Path -LiteralPath $Fixture)) {
    throw "25116 fixture not found: $Fixture"
}

$runtimeDir = Join-Path $serverDir 'bin'
$config = Join-Path $runtimeDir 'Config\TopUIItemInfo25116.yml'
if (-not (Test-Path -LiteralPath $config)) {
    throw "25116 YAML not found: $config"
}

$buildDir = Join-Path $env:TEMP ("TopUI25116Regression_{0}" -f $PID)
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$exe = Join-Path $buildDir 'TopUIItemInfo25116Regression.exe'

try {
    $configuredEnvironment = & cmd.exe /d /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars64.bat failed with exit code $LASTEXITCODE"
    }
    foreach ($line in $configuredEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }

    $compileArgs = @(
        '/nologo',
        '/std:c++17',
        '/EHsc',
        '/MT',
        '/DYAML_CPP_STATIC_DEFINE',
        '/W0',
        '/D_CRT_SECURE_NO_WARNINGS',
        ("/Fo{0}{1}" -f $buildDir, [System.IO.Path]::DirectorySeparatorChar),
        ("/Fd{0}" -f (Join-Path $buildDir 'TopUIItemInfo25116Regression.pdb')),
        "/I$serverDir",
        "/I$(Join-Path $serverDir '..\include')",
        (Join-Path $serverDir 'TopUIItemInfo25116.cpp'),
        (Join-Path $PSScriptRoot 'TopUIItemInfo25116Regression.cpp'),
        '/link',
        "/OUT:$exe",
        '/NODEFAULTLIB:spdlog.lib',
        ("/LIBPATH:{0}" -f (Join-Path $serverDir '..\lib\x64')),
        'yaml-cpp.lib',
        'ws2_32.lib'
    )
    & cl.exe @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "25116 regression compile failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "25116 regression compiler returned success without producing $exe"
    }

    Push-Location $runtimeDir
    try {
        & $exe ([System.IO.Path]::GetFullPath($Fixture))
        if ($LASTEXITCODE -ne 0) {
            throw "25116 regression failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    Remove-Item -LiteralPath $buildDir -Recurse -Force -ErrorAction SilentlyContinue
}
