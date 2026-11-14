$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$profileSourcePath = Join-Path $root 'production\protected_topkart_profile_minimal.cpp'
$runtimePath = Join-Path $root 'production\hook_runtime_minimal.cpp'
$vmpProjectPath = Join-Path $root 'x64\ReleaseVmp\qqspeed_clean_hook_64.dll.vmp'

$source = Get-Content -LiteralPath $profileSourcePath -Raw
$runtime = Get-Content -LiteralPath $runtimePath -Raw
$vmpProject = Get-Content -LiteralPath $vmpProjectPath -Raw
$match = [regex]::Match(
    $source,
    'QQSPEED_VMP_DECRYPT_STRING_A\(\s*"([A-Za-z0-9+/=]+)"\s*\)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $match.Success) {
    throw 'Unable to locate the protected Top-Kart profile literal.'
}

$protectedText = $match.Groups[1].Value
if (-not $vmpProject.Contains("string &quot;$protectedText&quot;")) {
    throw 'VMProtect project is not synchronized with the protected Top-Kart profile literal.'
}
if ($vmpProject.Contains('Login.TopKartProfile.Resolve')) {
    throw 'Legacy Top-Kart profile procedure must not consume a fourth production region.'
}

$bytes = [Convert]::FromBase64String($protectedText)
$expected = [uint32[]]@(0x342D90, 0x2B55F98, 0x36C2D08)
if ($bytes.Length -ne (($expected.Length + 1) * 4)) {
    throw "Unexpected protected Top-Kart profile byte length: $($bytes.Length)"
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
    $mask = Convert-ToUInt32 ([uint64]3337565984 + ([uint64]$i * [uint64]2654435761))
    $rotation = (($i * 11) + 9) -band 31
    $added = Rotate-Right32 ([uint32]($mixed -bxor $mask)) $rotation
    $delta = Convert-ToUInt32 ([uint64]3518319157 * [uint64]($i + 1))
    $plain = Convert-ToUInt32 ([uint64]$added + [uint64]4294967296 - [uint64]$delta)
    $decoded.Add($plain)
}

for ($i = 0; $i -lt $expected.Length; ++$i) {
    if ($decoded[$i] -ne $expected[$i]) {
        throw ('Top-Kart profile word {0} mismatch: expected=0x{1:X8} actual=0x{2:X8}' -f
            $i, $expected[$i], $decoded[$i])
    }
}

$hash = [uint32]2166136261
foreach ($word in $expected) {
    foreach ($shift in @(0, 8, 16, 24)) {
        $octet = [uint32](($word -shr $shift) -band 0xFF)
        $hash = Convert-ToUInt32 ([uint64]($hash -bxor $octet) * [uint64]16777619)
    }
}
if ($decoded[$expected.Length] -ne $hash) {
    throw ('Top-Kart profile checksum mismatch: expected=0x{0:X8} actual=0x{1:X8}' -f
        $hash, $decoded[$expected.Length])
}

foreach ($plainRva in @('0x342D90', '0x2B55F98', '0x36C2D08')) {
    if ($source.Contains($plainRva)) {
        throw "Protected Top-Kart source contains plaintext RVA: $plainRva"
    }
}
if ($runtime -notmatch '(?s)#if !defined\(QQSPEED_VMP_PROTECTION\).*?kAntiCheckerExecuteRva.*?kAntiCheckerExecuteSlotRva.*?kAceInterfaceGlobalRva.*?#endif') {
    throw 'Plain Top-Kart AntiChecker RVAs are not isolated to non-VMP builds.'
}
if (-not $runtime.Contains('ResolveProtectedTopKartCoreProfile(') -or
    -not $runtime.Contains('ClearResolvedTopKartCoreProfile(')) {
    throw 'VMP runtime does not resolve and clear the protected Top-Kart profile.'
}
if ($source -match 'QQSPEED_VMP_BEGIN_(?:ULTRA|MUTATION)') {
    throw 'Protected Top-Kart string decoder unexpectedly consumes a procedure region.'
}

Write-Host 'Protected Top-Kart profile decodes exactly and is isolated to VMP builds.'
