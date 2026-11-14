$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$profileSourcePath = Join-Path $root 'network\protected_network_profile.cpp'
if (-not (Test-Path -LiteralPath $profileSourcePath)) {
    throw "Protected profile source is missing: $profileSourcePath"
}

$source = Get-Content -LiteralPath $profileSourcePath -Raw
$match = [regex]::Match(
    $source,
    'QQSPEED_VMP_DECRYPT_STRING_A\(\s*"([A-Za-z0-9+/=]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $match.Success) {
    throw 'Unable to locate the protected base64 profile literal.'
}

$bytes = [Convert]::FromBase64String($match.Groups[1].Value)
$expected = [uint32[]]@(
    0x31DF40, 0x31C780, 0x3160A0, 0x34AE80, 0x36F300,
    0x21DA80, 0x3DCA10, 0x3E288D, 0x31E2D5, 0xCA998,
    0x31E550, 0x31E558, 0x31E0CB,
    0x3E288D,
    0x213C72, 0x3DF23E, 0x3DF49B, 0x3E3C19, 0x3E3E19, 0x3E433D, 0x41DFCE, 0x420125,
    0x3E2903, 0x31E2D5, 0x31CA08, 0x31C9C4, 0x31E349
)

if ($bytes.Length -ne (($expected.Length + 1) * 4)) {
    throw "Unexpected protected profile byte length: $($bytes.Length)"
}

function Convert-ToUInt32([uint64]$value) {
    return [uint32]($value -band [uint64]4294967295)
}

function Rotate-Right32([uint32]$value, [int]$count) {
    $count = $count -band 31
    if ($count -eq 0) {
        return $value
    }
    $right = [uint32]($value -shr $count)
    $left = Convert-ToUInt32 ([uint64]$value -shl (32 - $count))
    return [uint32]($right -bor $left)
}

$decoded = New-Object 'System.Collections.Generic.List[uint32]'
for ($i = 0; $i -lt ($expected.Length + 1); ++$i) {
    $mixed = [BitConverter]::ToUInt32($bytes, $i * 4)
    $mask = Convert-ToUInt32 (([uint64]2781081981 + ([uint64]$i * [uint64]2135587861)))
    $rotation = (($i * 7) + 5) -band 31
    $added = Rotate-Right32 ([uint32]($mixed -bxor $mask)) $rotation
    $delta = Convert-ToUInt32 ([uint64]2654435769 * [uint64]($i + 1))
    $plain = Convert-ToUInt32 (([uint64]$added + [uint64]4294967296 - [uint64]$delta))
    $decoded.Add($plain)
}

for ($i = 0; $i -lt $expected.Length; ++$i) {
    if ($decoded[$i] -ne $expected[$i]) {
        throw ('Profile word {0} mismatch: expected=0x{1:X8} actual=0x{2:X8}' -f $i, $expected[$i], $decoded[$i])
    }
}

$hash = [uint32]2166136261
foreach ($word in $expected) {
    foreach ($shift in @(0, 8, 16, 24)) {
        $octet = [uint32](($word -shr $shift) -band 0xFF)
        $hash = Convert-ToUInt32 (([uint64]($hash -bxor $octet) * [uint64]16777619))
    }
}
if ($decoded[$expected.Length] -ne $hash) {
    throw ('Profile checksum mismatch: expected=0x{0:X8} actual=0x{1:X8}' -f $hash, $decoded[$expected.Length])
}

Write-Host 'Protected Network profile blob decodes to the exact 21482 RVA set and checksum.'
