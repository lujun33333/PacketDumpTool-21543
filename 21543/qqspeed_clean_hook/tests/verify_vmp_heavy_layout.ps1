$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $root 'qqspeed_clean_hook.vcxproj'
$wrapperPath = Join-Path $root 'vmp_protection.h'
$vmpProjectPath = Join-Path $root 'x64\ReleaseVmp\qqspeed_clean_hook_64.dll.vmp'
$redirectPath = Join-Path $root 'production\connect_redirect_mfc.cpp'
$runtimePath = Join-Path $root 'production\hook_runtime_minimal.cpp'
$profilePath = Join-Path $root 'production\protected_topkart_profile_minimal.cpp'

$projectText = Get-Content -LiteralPath $projectPath -Raw
$wrapper = Get-Content -LiteralPath $wrapperPath -Raw
$redirect = Get-Content -LiteralPath $redirectPath -Raw
$runtime = Get-Content -LiteralPath $runtimePath -Raw
$profile = Get-Content -LiteralPath $profilePath -Raw
$vmpProjectText = Get-Content -LiteralPath $vmpProjectPath -Raw
[xml]$vmpProject = $vmpProjectText

foreach ($required in @(
    'QQSPEED_VMP_BEGIN_ULTRA',
    'QQSPEED_VMP_BEGIN_MUTATION',
    'QQSPEED_VMP_NOINLINE'
)) {
    if (-not $wrapper.Contains($required)) {
        throw "VMP wrapper is missing: $required"
    }
}
if (-not $projectText.Contains('QQSPEED_PRODUCTION_MINIMAL;QQSPEED_VMP_PROTECTION')) {
    throw 'ReleaseVmp does not select the minimal protected build.'
}

$expected = @(
    @{ Name = 'Prod.Connect.RedirectDecision'; Source = $redirect; Macro = 'ULTRA'; Type = '2' },
    @{ Name = 'Prod.AceFeed.Validate'; Source = $runtime; Macro = 'MUTATION'; Type = '0' },
    @{ Name = 'Prod.AceFeed.Observe'; Source = $runtime; Macro = 'MUTATION'; Type = '0' },
    @{ Name = 'Prod.AntiChecker.ExecuteGate'; Source = $runtime; Macro = 'MUTATION'; Type = '0' },
    @{ Name = 'Prod.AntiChecker.ExchangeSlot'; Source = $runtime; Macro = 'ULTRA'; Type = '2' },
    @{ Name = 'Prod.AntiChecker.RestoreSlot'; Source = $runtime; Macro = 'ULTRA'; Type = '2' },
    @{ Name = 'Prod.AntiChecker.VerifySlots'; Source = $runtime; Macro = 'ULTRA'; Type = '2' }
)
foreach ($entry in $expected) {
    $marker = "QQSPEED_VMP_BEGIN_$($entry.Macro)(`"$($entry.Name)`")"
    if (-not $entry.Source.Contains($marker)) {
        throw "Production source is missing marker: $marker"
    }
    $xmlNeedle = "VMProtectMarker &quot;$($entry.Name)&quot;' Options=`"0`" CompilationType=`"$($entry.Type)`""
    if (-not $vmpProjectText.Contains($xmlNeedle)) {
        throw "VMP project marker/type mismatch: $($entry.Name)"
    }
}

$productionText = $redirect + "`n" + $runtime + "`n" + $profile
$sourceMarkers = [regex]::Matches(
    $productionText,
    'QQSPEED_VMP_BEGIN_(?:ULTRA|MUTATION)\("(Prod\.[^"]+)"\)')
if ($sourceMarkers.Count -ne 7) {
    throw "Expected exactly seven production source markers; actual=$($sourceMarkers.Count)"
}

$procedureNodes = @($vmpProject.Document.Protection.Procedures.Procedure)
$complexity = [int]$vmpProject.Document.Protection.VMComplexity
if ($complexity -ne 20) {
    throw "Production VM complexity must stay at 20; actual=$complexity"
}
$markerNodes = @($procedureNodes | Where-Object {
    ([string]$_.MapAddress).StartsWith('VMProtectMarker ')
})
$stringNodes = @($procedureNodes | Where-Object {
    ([string]$_.MapAddress).StartsWith('string ')
})
if ($markerNodes.Count -ne 7) {
    throw "Expected exactly seven VMProtect procedures; actual=$($markerNodes.Count)"
}
if ($stringNodes.Count -ne 1 -or
    -not ([string]$stringNodes[0].MapAddress).Contains('g7wAFC7RDCVRLxi/i69qpQ==')) {
    throw 'Production VMP project must retain only the protected Top-Kart profile string.'
}
if ($vmpProjectText.Contains('Login.') -or
    $vmpProjectText.Contains('Tu7oaFhOoS4ZPwbK6S9H')) {
    throw 'Legacy Login/Msg100/Msg551/Network-profile protection remains active.'
}

foreach ($entry in $expected) {
    $pattern = [regex]::Escape("QQSPEED_VMP_BEGIN_$($entry.Macro)(`"$($entry.Name)`")") +
        '(?s)(.*?)QQSPEED_VMP_END\(\)'
    $region = [regex]::Match($entry.Source, $pattern)
    if (-not $region.Success) {
        throw "Cannot isolate protected region: $($entry.Name)"
    }
    if ([regex]::IsMatch($region.Groups[1].Value, '\breturn\b')) {
        throw "Protected region contains an early return: $($entry.Name)"
    }
}

Write-Host 'VMProtect production layout verification passed: exactly 7 procedures (4 Ultra + 3 Mutation).'
