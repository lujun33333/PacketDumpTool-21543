$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $root 'qqspeed_clean_hook.vcxproj'
$runtimePath = Join-Path $root 'production\hook_runtime_minimal.cpp'
$redirectPath = Join-Path $root 'production\connect_redirect_mfc.cpp'
$profilePath = Join-Path $root 'production\protected_topkart_profile_minimal.cpp'
$vmpProjectPath = Join-Path $root 'x64\ReleaseVmp\qqspeed_clean_hook_64.dll.vmp'
$researchProjectPath = Join-Path $root 'vmprotect\qqspeed_clean_hook_64.research-19.vmp'

foreach ($path in @(
    $projectPath,
    $runtimePath,
    $redirectPath,
    $profilePath,
    $vmpProjectPath,
    $researchProjectPath
)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required production/research artifact is missing: $path"
    }
}

$project = Get-Content -LiteralPath $projectPath -Raw
[xml]$projectXml = $project
$runtime = Get-Content -LiteralPath $runtimePath -Raw
$redirect = Get-Content -LiteralPath $redirectPath -Raw
$profile = Get-Content -LiteralPath $profilePath -Raw
$vmpProject = Get-Content -LiteralPath $vmpProjectPath -Raw
$researchProject = Get-Content -LiteralPath $researchProjectPath -Raw
$production = $runtime + "`n" + $redirect + "`n" + $profile

foreach ($required in @(
    'VMProtectIsProtected()',
    'VMProtectIsValidImageCRC()',
    'QQSPEED_VMP_DECRYPT_STRING_A(',
    'SecureZeroMemory'
)) {
    if (-not $profile.Contains($required)) {
        throw "Protected Top-Kart profile is missing: $required"
    }
}
foreach ($plainRva in @('0x342D90', '0x2B55F98', '0x36C2D08')) {
    if ($profile.Contains($plainRva)) {
        throw "VMP Top-Kart profile contains plaintext RVA: $plainRva"
    }
}

foreach ($marker in @(
    'Prod.Connect.RedirectDecision',
    'Prod.AceFeed.Validate',
    'Prod.AceFeed.Observe',
    'Prod.AntiChecker.ExecuteGate',
    'Prod.AntiChecker.ExchangeSlot',
    'Prod.AntiChecker.RestoreSlot',
    'Prod.AntiChecker.VerifySlots'
)) {
    if (-not $production.Contains($marker) -or -not $vmpProject.Contains($marker)) {
        throw "Production marker is not synchronized: $marker"
    }
}
if (([regex]::Matches($vmpProject, 'VMProtectMarker &quot;')).Count -ne 7) {
    throw 'Active VMP project must contain exactly seven procedure markers.'
}
foreach ($legacy in @(
    'Login.Msg100',
    'Login.Msg551',
    'Login.NetworkProfile',
    'Login.TopKartProfile',
    'Login.Redirect'
)) {
    if ($vmpProject.Contains($legacy)) {
        throw "Legacy protection remains active: $legacy"
    }
}
if (-not $researchProject.Contains('Login.Msg100.PlayerBase') -or
    -not $researchProject.Contains('Login.Msg551.CompactFeed') -or
    -not $researchProject.Contains('Login.NetworkProfile.Resolve')) {
    throw 'The preserved 19-region research VMP project is incomplete.'
}

foreach ($forbidden in @(
    'logging/hook_log.h',
    'Logf(',
    'PacketFlowf(',
    'OutputDebugString',
    'hook_config protected_profile',
    'network_preflight',
    'hashMs=',
    'elapsedMs=',
    'http://',
    'https://'
)) {
    if ($production.Contains($forbidden)) {
        throw "Production code contains logging/upload disclosure: $forbidden"
    }
}
$loggerItem = @($projectXml.Project.ItemGroup.ClCompile | Where-Object {
    $_.Include -eq 'logging\hook_log.cpp'
})[0]
if (-not $loggerItem) {
    throw 'Research logger source is no longer preserved in the project.'
}
foreach ($configuration in @('ReleasePlain|x64', 'ReleaseVmp|x64')) {
    $excluded = @($loggerItem.ExcludedFromBuild | Where-Object {
        $_.Condition -like "*$configuration*" -and $_.InnerText -eq 'true'
    })
    if ($excluded.Count -ne 1) {
        throw "Logger is not excluded exactly once from $configuration."
    }
}

Write-Host 'VMP security verification passed: protected endpoint/profile, 7 markers, no production logging or upload path.'
