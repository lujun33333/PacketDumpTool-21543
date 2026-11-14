param(
    [string]$DllPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'x64\ReleasePlain\qqspeed_clean_hook_64.dll')
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Assert-Contains([string]$Text, [string]$Expected, [string]$Path) {
    if (-not $Text.Contains($Expected)) {
        throw "Missing manual-map TLS compatibility marker in ${Path}: $Expected"
    }
}

$sourceFiles = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.h', '.hpp', '.cpp', '.cxx') -and
    $_.FullName -notlike "$root\tests\*" -and
    $_.FullName -notlike "$root\x64\*" -and
    $_.FullName -notlike "$root\.codex-backups\*"
}

$compilerTlsPattern = '(?m)\bthread_local\b|__declspec\s*\(\s*thread\s*\)'
foreach ($sourceFile in $sourceFiles) {
    $source = Get-Content -LiteralPath $sourceFile.FullName -Raw
    if ([regex]::IsMatch($source, $compilerTlsPattern)) {
        throw "Compiler TLS remains in source: $($sourceFile.FullName)"
    }
}

$flsHeaderPath = Join-Path $root 'manual_fls_slot.h'
if (-not (Test-Path -LiteralPath $flsHeaderPath)) {
    throw "Explicit FLS storage header is missing: $flsHeaderPath"
}
$flsHeader = Get-Content -LiteralPath $flsHeaderPath -Raw
foreach ($marker in @('class ManualFlsSlot', 'FlsAlloc', 'FlsGetValue', 'FlsSetValue', 'FlsFree')) {
    Assert-Contains $flsHeader $marker $flsHeaderPath
}

$diagnosticPath = Join-Path $root 'diagnostics\process_exception_trace.cpp'
$networkHooksPath = Join-Path $root 'network\network_hooks.cpp'
$mapProbePath = Join-Path $root 'topkart\topkart_mapid_probe.cpp'
$feedPath = Join-Path $root 'topkart\topkart_551_feed.cpp'
$msg100Path = Join-Path $root 'network\msg100_login_plain.cpp'
$diagnostic = Get-Content -LiteralPath $diagnosticPath -Raw
$networkHooks = Get-Content -LiteralPath $networkHooksPath -Raw
$mapProbe = Get-Content -LiteralPath $mapProbePath -Raw
$feed = Get-Content -LiteralPath $feedPath -Raw
$msg100 = Get-Content -LiteralPath $msg100Path -Raw

Assert-Contains $diagnostic 'ManualFlsSlot<VehThreadState>' $diagnosticPath
Assert-Contains $networkHooks 'INIT_ONCE g_performanceFrequencyOnce' $networkHooksPath
if ([regex]::IsMatch($networkHooks, 'static\s+const\s+LARGE_INTEGER\s+frequency')) {
    throw 'ElapsedMicrosSince still uses a dynamically initialized function-local static and implicitly pulls in _tls_index.'
}
Assert-Contains $mapProbe 'ManualFlsSlot<MapIdThreadState>' $mapProbePath
Assert-Contains $feed 'struct RouteProbeCounterSnapshot' $feedPath
Assert-Contains $feed 'const RouteProbeCounterSnapshot& baseline' $feedPath

if (-not [regex]::IsMatch($msg100, '(?m)^\s*std::vector<unsigned char>\s+bodyCopy\s*;') -or
    -not [regex]::IsMatch($msg100, '(?m)^\s*std::vector<unsigned char>\s+plain\s*;')) {
    throw 'Msg100 scratch vectors are not call-stack-owned locals.'
}

if (-not (Test-Path -LiteralPath $DllPath)) {
    throw "ReleasePlain output is missing: $DllPath"
}
$dllItem = Get-Item -LiteralPath $DllPath
$latestSourceWrite = ($sourceFiles | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
if ($dllItem.LastWriteTimeUtc -lt $latestSourceWrite) {
    throw "ReleasePlain output is stale relative to its source files: $DllPath"
}

$bytes = [IO.File]::ReadAllBytes($DllPath)
if ($bytes.Length -lt 512 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Not a valid PE image: $DllPath"
}
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 0 -or $peOffset + 0x108 -gt $bytes.Length -or
    $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) {
    throw "Invalid PE header: $DllPath"
}
$optionalOffset = $peOffset + 24
$magic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
$dataDirectoryOffset = switch ($magic) {
    0x20B { $optionalOffset + 112 }
    0x10B { $optionalOffset + 96 }
    default { throw ('Unsupported PE optional-header magic: 0x{0:X}' -f $magic) }
}
$tlsDirectoryOffset = $dataDirectoryOffset + (9 * 8)
$tlsRva = [BitConverter]::ToUInt32($bytes, $tlsDirectoryOffset)
$tlsSize = [BitConverter]::ToUInt32($bytes, $tlsDirectoryOffset + 4)
if ($tlsRva -ne 0 -or $tlsSize -ne 0) {
    throw ('ReleasePlain still has a PE TLS directory: rva=0x{0:X} size=0x{1:X}' -f $tlsRva, $tlsSize)
}

Write-Host "Manual-map TLS compatibility verification passed: no compiler TLS and PE TLS directory is zero ($DllPath)."
