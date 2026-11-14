$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$redirectPath = Join-Path $root 'production\connect_redirect_mfc.cpp'
$vmpHeaderPath = Join-Path $root 'vmp_protection.h'
$projectPath = Join-Path $root 'qqspeed_clean_hook.vcxproj'
$vmpProjectPath = Join-Path $root 'x64\ReleaseVmp\qqspeed_clean_hook_64.dll.vmp'

$redirect = Get-Content -LiteralPath $redirectPath -Raw
$vmpHeader = Get-Content -LiteralPath $vmpHeaderPath -Raw
$vmpProject = Get-Content -LiteralPath $vmpProjectPath -Raw
[xml]$project = Get-Content -LiteralPath $projectPath -Raw

foreach ($needle in @(
    '#define QQSPEED_VMP_SERVER_LOCK_ENABLED 1',
    '#define QQSPEED_VMP_SERVER_LOCK_ENABLED 0'
)) {
    if (-not $vmpHeader.Contains($needle)) {
        throw "VMP endpoint-lock gate is missing: $needle"
    }
}

$definitionGroups = @($project.Project.ItemDefinitionGroup)
$releaseVmp = $definitionGroups | Where-Object {
    $_.Condition -like '*ReleaseVmp|x64*'
}
$definitions = [string]$releaseVmp.ClCompile.PreprocessorDefinitions
foreach ($definition in @('QQSPEED_PRODUCTION_MINIMAL', 'QQSPEED_VMP_PROTECTION')) {
    if (-not $definitions.Contains($definition)) {
        throw "ReleaseVmp is missing definition: $definition"
    }
}

foreach ($needle in @(
    'kLockedHostOctets',
    '127, 0, 0, 1',
    'kLockedPortEncoded',
    '28880u ^ kLockedPortMask',
    'AcceptRedirectTarget(',
    'VMProtectIsProtected()',
    'VMProtectIsValidImageCRC()',
    'WSASetLastError(WSAEACCES)',
    '#if QQSPEED_VMP_SERVER_LOCK_ENABLED',
    'QQSPEED_VMP_BEGIN_ULTRA("Prod.Connect.RedirectDecision")'
)) {
    if (-not $redirect.Contains($needle)) {
        throw "Protected endpoint source is missing: $needle"
    }
}
if (-not $vmpProject.Contains(
    'VMProtectMarker &quot;Prod.Connect.RedirectDecision&quot;')) {
    throw 'Production VMP project is missing the redirect decision procedure.'
}
$redirectMarkers = [regex]::Matches(
    $redirect,
    'QQSPEED_VMP_BEGIN_(?:ULTRA|MUTATION)\("Prod\.Connect\.[^"]+"\)')
if ($redirectMarkers.Count -ne 1) {
    throw "Redirect production source must contain one protected region; actual=$($redirectMarkers.Count)"
}
if ($redirect.Contains('constexpr char kLockedHost[] = "127.0.0.1"')) {
    throw 'Canonical endpoint must not be stored as a replaceable plaintext authority string.'
}

Write-Host 'ReleaseVmp endpoint-lock verification passed: one protected redirect region.'
