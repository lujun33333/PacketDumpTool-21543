$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runtimePath = Join-Path $root 'hook_runtime.cpp'
$redirectPath = Join-Path $root 'network\connect_redirect.cpp'
$redirectHeaderPath = Join-Path $root 'network\connect_redirect.h'
$logPath = Join-Path $root 'logging\hook_log.cpp'
$msg100Path = Join-Path $root 'network\msg100_login_plain.cpp'

$runtime = Get-Content -LiteralPath $runtimePath -Raw
$redirect = Get-Content -LiteralPath $redirectPath -Raw
$redirectHeader = Get-Content -LiteralPath $redirectHeaderPath -Raw
$log = Get-Content -LiteralPath $logPath -Raw
$msg100 = Get-Content -LiteralPath $msg100Path -Raw

$flowStart = $runtime.IndexOf('bool WaitForNetworkAndInstallHooks')
$flowEnd = $runtime.IndexOf('DWORD WINAPI RuntimeThread', $flowStart)
if ($flowStart -lt 0 -or $flowEnd -le $flowStart) {
    throw 'Could not isolate WaitForNetworkAndInstallHooks.'
}
$flow = $runtime.Substring($flowStart, $flowEnd - $flowStart)
$redirectIndex = $flow.IndexOf('InstallConnectRedirectIfEnabled(networkModule, config)')
$topKartIndex = $flow.IndexOf('WaitForTopKartAndInstallConsumers(config)')
if ($redirectIndex -lt 0 -or $topKartIndex -lt 0 -or $redirectIndex -gt $topKartIndex) {
    throw 'Connect redirect is not made ready before the optional Top-Kart wait/hash path.'
}
if ($redirectHeader -notmatch 'UninstallConnectRedirectHook') {
    throw 'Redirect install has no rollback API.'
}
if ($flow -notmatch 'UninstallConnectRedirectHook') {
    throw 'Redirect is not rolled back when the later Top-Kart gate fails.'
}
if ($redirect -notmatch 'elapsedUs=%llu' -or $redirect -notmatch 'asyncPending=%d') {
    throw 'Redirect call latency / WSAEWOULDBLOCK state is not observable.'
}
if ($log -notmatch 'HANDLE\s+g_logFile' -or $log -notmatch 'HANDLE\s+g_packetFlowLogFile') {
    throw 'Hook logger still lacks persistent per-channel file handles.'
}
if ($msg100 -match '\bthread_local\b') {
    throw 'Msg100 scratch buffers still use compiler TLS and block loader-independent manual mapping.'
}
if ($msg100 -notmatch '(?m)^\s*std::vector<unsigned char>\s+bodyCopy\s*;' -or
    $msg100 -notmatch '(?m)^\s*std::vector<unsigned char>\s+plain\s*;') {
    throw 'Msg100 scratch vectors are not call-stack-owned per invocation.'
}
if ($msg100 -match 'std::unique_ptr<unsigned char\[\]>\s+plain') {
    throw 'Msg100 hot path still uses a per-response heap allocation.'
}

Write-Host 'Redirect/hot-path regression verification passed.'
