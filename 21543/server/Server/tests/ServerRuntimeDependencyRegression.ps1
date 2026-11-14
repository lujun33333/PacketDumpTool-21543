param(
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$VcpkgRoot = 'C:\vcpkg',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$projectPath = Join-Path $serverDir 'Server.vcxproj'
$outputDir = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $serverDir 'bin'
} else {
    [System.IO.Path]::GetFullPath($OutputDirectory)
}
$connectorPath = Join-Path $outputDir 'mysqlcppconn-10-vs14.dll'
$projectSource = Get-Content -LiteralPath $projectPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()
$vcRedistBase = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'
$vcRedistVersion = Get-ChildItem -LiteralPath $vcRedistBase -Directory |
    Where-Object Name -Match '^\d+(\.\d+)+$' |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -eq $vcRedistVersion) {
    throw "MSVC redistributable directory not found: $vcRedistBase"
}
$vcRuntimeDir = Join-Path $vcRedistVersion.FullName 'x64\Microsoft.VC143.CRT'

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

$dumpbin = Get-ChildItem -LiteralPath `
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC' `
    -Filter dumpbin.exe -Recurse |
    Where-Object FullName -Match 'Hostx64\\x64' |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($dumpbin)) {
    throw 'x64 dumpbin.exe not found'
}
if (-not (Test-Path -LiteralPath $connectorPath)) {
    throw "runtime connector not found: $connectorPath"
}

$connectorImports = (& $dumpbin /dependents $connectorPath) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed with exit code $LASTEXITCODE"
}

$directRuntime = @(
    [pscustomobject]@{ Name = 'HPSocket.dll'; Source = Join-Path $serverDir '..\lib\x64\HPSocket.dll' },
    [pscustomobject]@{ Name = 'mysqlcppconn-10-vs14.dll'; Source = Join-Path $serverDir '..\lib\x64\mysqlcppconn-10-vs14.dll' }
)
$connectorRuntime = @(
    [pscustomobject]@{ Name = 'libcrypto-3-x64.dll'; Source = Join-Path $VcpkgRoot 'installed\x64-windows\bin\libcrypto-3-x64.dll' },
    [pscustomobject]@{ Name = 'libssl-3-x64.dll'; Source = Join-Path $VcpkgRoot 'installed\x64-windows\bin\libssl-3-x64.dll' },
    [pscustomobject]@{ Name = 'msvcp140.dll'; Source = Join-Path $vcRuntimeDir 'msvcp140.dll' },
    [pscustomobject]@{ Name = 'vcruntime140.dll'; Source = Join-Path $vcRuntimeDir 'vcruntime140.dll' },
    [pscustomobject]@{ Name = 'vcruntime140_1.dll'; Source = Join-Path $vcRuntimeDir 'vcruntime140_1.dll' }
)
$requiredRuntime = @($directRuntime) + @($connectorRuntime)

foreach ($runtime in $connectorRuntime) {
    $name = $runtime.Name
    if ($connectorImports -notmatch [regex]::Escape($name)) {
        Add-Failure "connector no longer imports expected runtime: $name"
    }
}

foreach ($runtime in $requiredRuntime) {
    $name = $runtime.Name

    $sourcePath = $runtime.Source
    $outputPath = Join-Path $outputDir $name
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        Add-Failure "vcpkg runtime source is missing: $sourcePath"
        continue
    }
    if (-not (Test-Path -LiteralPath $outputPath)) {
        Add-Failure "server output is missing transitive runtime: $outputPath"
        continue
    }

    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $outputHash = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash
    if ($sourceHash -ne $outputHash) {
        Add-Failure "server output runtime hash differs from vcpkg source: $name"
    }
}

$targetStart = $projectSource.IndexOf('<Target Name="CopyServerRuntimeDependencies"', [StringComparison]::Ordinal)
$targetEnd = if ($targetStart -ge 0) {
    $projectSource.IndexOf('</Target>', $targetStart, [StringComparison]::Ordinal)
} else {
    -1
}
if ($targetStart -lt 0 -or $targetEnd -lt 0) {
    Add-Failure 'Server.vcxproj lacks a build-always runtime dependency target'
} else {
    $targetSource = $projectSource.Substring($targetStart, $targetEnd + 9 - $targetStart)
    if ($targetSource -notmatch 'AfterTargets="Build"') {
        Add-Failure 'runtime dependency target is not attached after every Build invocation'
    }
    foreach ($runtime in $requiredRuntime) {
        $name = $runtime.Name
        if ($targetSource -notmatch [regex]::Escape($name)) {
            Add-Failure "Server.vcxproj does not declare runtime dependency: $name"
        }
    }
    if ($targetSource -notmatch '<Copy\s+SourceFiles="@\(ServerRuntimeDependency\)"\s+DestinationFolder="\$\(OutDir\)"') {
        Add-Failure 'runtime dependency target does not copy into the executable output directory'
    }
    foreach ($runtime in $requiredRuntime) {
        $name = $runtime.Name
        if ($targetSource -notmatch ('<Error\s+Condition="!Exists\([^>]*' + [regex]::Escape($name))) {
            Add-Failure "runtime dependency target does not fail when source is unavailable: $name"
        }
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host "ServerRuntimeDependencyRegression passed: complete Release x64 package contains $(($requiredRuntime.Name) -join ', ')"
