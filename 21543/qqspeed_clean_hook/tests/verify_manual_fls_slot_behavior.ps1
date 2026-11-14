$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'manual_fls_slot_test.cpp'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual Studio x64 environment script is missing: $vcvars"
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('qqspeed-fls-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
    $exe = Join-Path $tempRoot 'manual_fls_slot_test.exe'
    $obj = Join-Path $tempRoot 'manual_fls_slot_test.obj'
    $compile = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /MT /I"{1}" "{2}" /Fe:"{3}" /Fo:"{4}"' -f `
        $vcvars, $root, $source, $exe, $obj
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) {
        throw "manual_fls_slot behavior test compilation failed with exit code $LASTEXITCODE"
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "manual_fls_slot behavior test failed with exit code $LASTEXITCODE"
    }
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Manual FLS slot multithread isolation/destructor verification passed.'
