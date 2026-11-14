$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$utilsPath = Join-Path $root 'Utils.h'
$messagePath = Join-Path $root 'Message.cpp'
$messageHeaderPath = Join-Path $root 'Message.h'
$playerPath = Join-Path $root 'Player.cpp'
$gameServerPath = Join-Path $root 'GameServer.cpp'

$utils = Get-Content -LiteralPath $utilsPath -Raw
$message = Get-Content -LiteralPath $messagePath -Raw
$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$player = Get-Content -LiteralPath $playerPath -Raw
$gameServer = Get-Content -LiteralPath $gameServerPath -Raw

if ($utils -notmatch 'QQS_DebugUin10001Enabled') {
    throw 'UIN 10001 diagnostic logging has no runtime gate.'
}
if ($utils -notmatch 'QQSPEED_DEBUG_UIN10001') {
    throw 'UIN 10001 diagnostic logging cannot be opted in explicitly.'
}
if ($message -notmatch 'QQS_DebugUin10001Enabled\(\)\s*&&') {
    throw 'SendToClient still formats and flushes UIN 10001 traces while diagnostics are disabled.'
}
if ($player -notmatch '\[LOGIN-PERF\]') {
    throw 'Server login stages are not timed.'
}
if ($player -notmatch 'ClientVersion\s*>=\s*21932' -or
    $player -notmatch '\[LOGIN100\]\[DIRECT-21932-SEND\]' -or
    $player -notmatch 'SendToClient\(\s*Client,\s*100,\s*buf\.data\(\)') {
    throw '21932 direct Msg100 compatibility response is missing.'
}
if ($player -match 'Write16\(direct|directBody') {
    throw '21932 direct Msg100 still adds a duplicate inner length prefix.'
}
if ($player -match 'NotifyRankedCarGodLoginAnnouncement\(Client\)') {
    throw 'The optional car-god Msg555 announcement is still sent during login.'
}
if ($player -match 'Write21032\(Client\)') {
    throw 'Msg21032 is still sent inside the login handler.'
}
if ($messageHeader -notmatch 'DynamicCodeCfgSent') {
    throw 'Per-connection Msg21032 ready-event gate is missing.'
}
if ($gameServer -notmatch 'QQS_Get21032Payloads') {
    throw 'Static 21032 YAML payloads are still decoded for every login.'
}

$readyHandlerIndex = $message.IndexOf('void RequestReportCommonEvent')
$readySendIndex = $message.IndexOf('Write21032(Client)', $readyHandlerIndex)
if ($readyHandlerIndex -lt 0 -or $readySendIndex -lt 0) {
    throw 'Msg21032 is not triggered by the Msg24028 ready-event handler.'
}
if ($message -notmatch 'EventID\s*==\s*749') {
    throw 'Msg21032 is not gated by official Msg24028 EventID 749.'
}

Write-Host 'Server login fast-path regression verification passed.'
