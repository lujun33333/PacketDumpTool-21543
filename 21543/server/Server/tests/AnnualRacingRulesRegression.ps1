$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $PSScriptRoot 'AnnualRacingRulesRegression.cpp'
$vcvarsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

if (-not (Test-Path -LiteralPath $vcvarsPath)) {
    throw "missing MSVC environment script: $vcvarsPath"
}

$outputRoot = Join-Path $env:TEMP 'QQSpeedServerTests\AnnualRacingRulesRegression'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$exePath = Join-Path $outputRoot 'AnnualRacingRulesRegression.exe'

$compile = 'call "{0}" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /I"{1}" "{2}" /Fe:"{3}"' -f `
    $vcvarsPath, $serverRoot, $sourcePath, $exePath

& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "AnnualRacingRulesRegression compile failed with exit code $LASTEXITCODE"
}

& $exePath
if ($LASTEXITCODE -ne 0) {
    throw "AnnualRacingRulesRegression failed with exit code $LASTEXITCODE"
}
