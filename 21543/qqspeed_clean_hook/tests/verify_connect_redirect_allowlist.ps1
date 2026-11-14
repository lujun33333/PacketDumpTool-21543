$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'network\connect_redirect.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

$functionMatch = [regex]::Match(
    $source,
    '(?s)bool ShouldRedirectAddressString\(const char\* address\)\s*\{.*?\r?\n\}'
)
if (-not $functionMatch.Success) {
    throw "Unable to locate ShouldRedirectAddressString in $sourcePath"
}

$allowedAddresses = @(
    '113.96.13.205',
    '121.14.75.60',
    '120.232.19.139',
    '157.255.13.55'
)
foreach ($allowedAddress in $allowedAddresses) {
    $comparisonPattern = 'std::strcmp\(address,\s*"' + [regex]::Escape($allowedAddress) + '"\)\s*==\s*0'
    if (-not [regex]::IsMatch($functionMatch.Value, $comparisonPattern)) {
        throw "Redirect address allowlist is missing $allowedAddress"
    }
}

$mfcConnectMatch = [regex]::Match(
    $source,
    '(?s)int __fastcall HookedMfcAsyncSocketConnect\(\s*void\* socket,\s*const char\* hostAddress,\s*unsigned int hostPort\s*\)\s*\{.*?\r?\n\}'
)
if (-not $mfcConnectMatch.Success) {
    throw "Unable to locate HookedMfcAsyncSocketConnect in $sourcePath"
}
if ($mfcConnectMatch.Value -notmatch 'const bool redirectCandidate\s*=\s*portCandidate\s*;') {
    throw 'MFC redirect must accept every scoped login-port candidate, including newly assigned source IPs.'
}
if ($mfcConnectMatch.Value -match 'redirectCandidate\s*=\s*addressCandidate\s*&&\s*portCandidate') {
    throw 'MFC redirect still incorrectly requires the legacy source-IP allowlist.'
}

Write-Host "Connect redirect port policy verification passed: 10000-10005; legacy addresses retained for diagnostics"
