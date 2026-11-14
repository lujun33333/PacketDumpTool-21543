$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runtimePath = Join-Path $root 'hook_runtime.cpp'
$configPath = Join-Path $root 'hook_config.h'
$logPath = Join-Path $root 'logging\hook_log.cpp'
$networkHooksPath = Join-Path $root 'network\network_hooks.cpp'

$runtime = Get-Content -LiteralPath $runtimePath -Raw
$config = Get-Content -LiteralPath $configPath -Raw
$log = Get-Content -LiteralPath $logPath -Raw
$networkHooks = Get-Content -LiteralPath $networkHooksPath -Raw

if ($runtime -match 'LogModuleFingerprint\(config\.networkModuleName') {
    throw 'Runtime still hashes Network.dll for an eager diagnostic before validation.'
}
if ($runtime -match 'LogModuleFingerprint\(config\.topKartModuleName') {
    throw 'Runtime still hashes Top-Kart.dll for an eager diagnostic before validation.'
}
if ($runtime -notmatch 'hashMs=%llu') {
    throw 'Module fingerprint cost is not observable through hashMs.'
}
if ($runtime -notmatch 'hook initialization complete elapsedMs=%llu') {
    throw 'Overall hook initialization time is not logged.'
}
if ($log -notmatch '\+%llums') {
    throw 'Hook log lines do not carry a relative startup timestamp.'
}
if ($networkHooks -notmatch 'expandUs=%llu') {
    throw 'Msg100 expansion latency is not logged in microseconds.'
}
if ($config -notmatch 'enableNetworkFilterProtocolConnIdAlign\s*=\s*false\s*;') {
    throw 'FilterProtocol compatibility hook is still enabled while cached destination alignment is disabled.'
}

$innerDispatchStart = $networkHooks.IndexOf('int __fastcall HookedInnerDispatch(')
$innerDispatchEnd = $networkHooks.IndexOf('#if defined(QQSPEED_VMP_PROTECTION)', $innerDispatchStart)
if ($innerDispatchStart -lt 0 -or $innerDispatchEnd -le $innerDispatchStart) {
    throw 'Could not isolate HookedInnerDispatch.'
}
$innerDispatch = $networkHooks.Substring($innerDispatchStart, $innerDispatchEnd - $innerDispatchStart)
$expandCall = $innerDispatch.IndexOf('TryExpandDirectMsg100LoginPlain(')
$originalCall = $innerDispatch.IndexOf('g_originalInnerDispatch(')
$failureGuard = $innerDispatch.IndexOf('originalResult == 0')
if ($expandCall -lt 0 -or $originalCall -lt 0 -or $originalCall -gt $failureGuard -or
    $failureGuard -gt $expandCall) {
    throw 'Direct Msg100 expansion does not preserve original-dispatch side effects before fallback.'
}

Write-Host 'Hook login fast-path regression verification passed.'
