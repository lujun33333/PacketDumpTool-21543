$ErrorActionPreference = 'Stop'

$serverRoot = Resolve-Path (Join-Path $PSScriptRoot '..\Server')
$userServer = Get-Content -LiteralPath (Join-Path $serverRoot 'UserServer.cpp') -Raw
$runtime = Get-Content -LiteralPath (Join-Path $serverRoot 'LauncherRuntimeMaterial.cpp') -Raw
$project = Get-Content -LiteralPath (Join-Path $serverRoot 'SpeedServer.vcxproj') -Raw

foreach ($forbidden in @(
    'LauncherRuntimeKeyring.bin',
    'RuntimeMaterialKeyringPath',
    'ConfigureFromDpapiFile',
    'CryptUnprotectData',
    'kKeyringMagic'
)) {
    if ($userServer.Contains($forbidden) -or $runtime.Contains($forbidden)) {
        throw "Runtime keyring dependency remains: $forbidden"
    }
}

if (-not $userServer.Contains('ConfigureFromEmbeddedRelease(error)')) {
    throw 'UserServer does not initialize runtime material from the embedded release.'
}

foreach ($required in @(
    'QQSReleaseMaterialIncludeDir',
    'QQS_EMBEDDED_RELEASE_MATERIAL',
    'LauncherReleaseMaterial.generated.inc'
)) {
    if (-not ($runtime.Contains($required) -or $project.Contains($required))) {
        throw "Embedded release build contract is missing: $required"
    }
}

Write-Output 'NO_RUNTIME_KEYRING_SOURCE_TEST_OK'
