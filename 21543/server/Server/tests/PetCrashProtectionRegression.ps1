$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $PSScriptRoot 'PetCrashProtectionRegression.cpp'
$encoderPath = Join-Path $serverRoot 'KartPhys551PlainEncoder.cpp'
$runtimeYamlPath = Join-Path $serverRoot 'bin\EncryptKart-20884\163043.yml'
$snowRuntimeYamlPath = Join-Path $serverRoot 'bin\EncryptKart-20884\109563.yml'
$vcvarsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path -LiteralPath $vcvarsPath)) {
    throw "missing MSVC environment script: $vcvarsPath"
}

$outputRoot = Join-Path $env:TEMP 'QQSpeedServerTests\PetCrashProtectionRegression'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$exePath = Join-Path $outputRoot 'PetCrashProtectionRegression.exe'

$includeRoot = Join-Path $serverRoot 'include'
$libraryRoot = Join-Path (Split-Path $serverRoot -Parent) 'lib\x64'
$compile = 'call "{0}" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /DYAML_CPP_STATIC_DEFINE /I"{1}" /I"{2}" "{3}" "{4}" /Fe:"{5}" /link /LIBPATH:"{6}" yaml-cpp.lib' -f `
    $vcvarsPath, $serverRoot, $includeRoot, $sourcePath, $encoderPath, $exePath, $libraryRoot

& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "PetCrashProtectionRegression compile failed with exit code $LASTEXITCODE"
}

& $exePath $runtimeYamlPath $snowRuntimeYamlPath
if ($LASTEXITCODE -ne 0) {
    throw "PetCrashProtectionRegression failed with exit code $LASTEXITCODE"
}
