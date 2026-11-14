$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $root 'qqspeed_clean_hook.vcxproj'
$productionRoot = Join-Path $root 'production'
[xml]$project = Get-Content -LiteralPath $projectPath -Raw

function Get-ActiveSources([string]$configuration) {
    $sources = New-Object 'System.Collections.Generic.List[string]'
    foreach ($item in @($project.Project.ItemGroup.ClCompile)) {
        if (-not $item.Include) { continue }
        $excluded = $false
        foreach ($entry in @($item.ExcludedFromBuild)) {
            if ($entry.Condition -like "*$configuration*" -and
                $entry.InnerText -eq 'true') {
                $excluded = $true
                break
            }
        }
        if (-not $excluded) {
            $sources.Add([string]$item.Include)
        }
    }
    return @($sources)
}

function Assert-ExactSources(
    [string]$configuration,
    [string[]]$expected) {
    $actual = @(Get-ActiveSources $configuration | Sort-Object)
    $wanted = @($expected | Sort-Object)
    if (($actual -join '|') -ne ($wanted -join '|')) {
        throw "$configuration source set mismatch. expected=$($wanted -join ',') actual=$($actual -join ',')"
    }
}

Assert-ExactSources 'ReleasePlain|x64' @(
    'dllmain.cpp',
    'pch.cpp',
    'production\connect_redirect_mfc.cpp',
    'production\hook_runtime_minimal.cpp'
)
Assert-ExactSources 'ReleaseVmp|x64' @(
    'dllmain.cpp',
    'pch.cpp',
    'production\connect_redirect_mfc.cpp',
    'production\hook_runtime_minimal.cpp',
    'production\protected_topkart_profile_minimal.cpp'
)

foreach ($configuration in @('Debug|x64', 'Release|x64')) {
    $sources = @(Get-ActiveSources $configuration)
    foreach ($required in @(
        'hook_runtime.cpp',
        'logging\hook_log.cpp',
        'network\msg100_login_plain.cpp',
        'network\network_hooks.cpp',
        'topkart\topkart_551_feed.cpp'
    )) {
        if ($sources -notcontains $required) {
            throw "$configuration no longer preserves research source: $required"
        }
    }
    if ($sources -contains 'production\hook_runtime_minimal.cpp') {
        throw "$configuration unexpectedly compiles the production runtime."
    }
}

$productionText = (
    Get-ChildItem -LiteralPath $productionRoot -Filter '*.cpp' -File |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
) -join "`n"
foreach ($forbidden in @(
    'logging/hook_log.h',
    'logging\hook_log.h',
    'Logf(',
    'PacketFlowf(',
    'OutputDebugString',
    'MessageBox',
    'qqspeed_clean_hook_logs',
    'qqspeed_clean_hook_',
    'http://',
    'https://'
)) {
    if ($productionText.Contains($forbidden)) {
        throw "Production source contains forbidden logging/status/upload token: $forbidden"
    }
}

$definitionGroups = @($project.Project.ItemDefinitionGroup)
foreach ($configuration in @('ReleasePlain|x64', 'ReleaseVmp|x64')) {
    $group = $definitionGroups | Where-Object { $_.Condition -like "*$configuration*" }
    $definitions = [string]$group.ClCompile.PreprocessorDefinitions
    if (-not $definitions.Contains('QQSPEED_PRODUCTION_MINIMAL')) {
        throw "$configuration is missing QQSPEED_PRODUCTION_MINIMAL."
    }
}

Write-Host 'Production source boundary verification passed: Plain=4 units, VMP=5 units, no DLL logging/status/upload path.'
