$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $PSScriptRoot 'PetFormStatusRegression.cpp'
$vcvarsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path -LiteralPath $vcvarsPath)) {
    throw "missing MSVC environment script: $vcvarsPath"
}

$outputRoot = Join-Path $env:TEMP 'QQSpeedServerTests\PetFormStatusRegression'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$exePath = Join-Path $outputRoot 'PetFormStatusRegression.exe'

$includeRoot = Join-Path $serverRoot 'include'
$libraryRoot = Join-Path (Split-Path $serverRoot -Parent) 'lib\x64'
$compile = 'call "{0}" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /DYAML_CPP_STATIC_DEFINE /I"{1}" /I"{2}" "{3}" /Fe:"{4}" /link /LIBPATH:"{5}" yaml-cpp.lib' -f `
    $vcvarsPath, $serverRoot, $includeRoot, $sourcePath, $exePath, $libraryRoot

& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "PetFormStatusRegression compile failed with exit code $LASTEXITCODE"
}

& $exePath
if ($LASTEXITCODE -ne 0) {
    throw "PetFormStatusRegression failed with exit code $LASTEXITCODE"
}
