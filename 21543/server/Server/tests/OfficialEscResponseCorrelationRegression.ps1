$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$room = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.cpp') -Raw
$message = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.cpp') -Raw
$messageHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.h') -Raw

function Get-SourceRange {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Begin,
        [Parameter(Mandatory = $true)][string]$End
    )

    $beginIndex = $Text.IndexOf($Begin, [StringComparison]::Ordinal)
    if ($beginIndex -lt 0) {
        throw "source range begin marker not found: $Begin"
    }
    $endIndex = $Text.IndexOf($End, $beginIndex, [StringComparison]::Ordinal)
    if ($endIndex -lt 0 -or $endIndex -le $beginIndex) {
        throw "source range end marker not found after begin: $End"
    }
    return $Text.Substring($beginIndex, $endIndex - $beginIndex)
}

$response424 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'void ResponseLeaveGame(' `
    -End 'void NotifyRandRoomNameList('
$response105 = Get-SourceRange `
    -Text $room `
    -Begin 'void ResponseLeaveRoom(' `
    -End 'void NotifyLeaveRoom('
$duplicate105 = Get-SourceRange `
    -Text $room `
    -Begin 'static void BuildOfficialClearedLeaveRoom105Response(' `
    -End 'static bool HandleOfficialEscLeaveRoom105('
$response103 = Get-SourceRange `
    -Text $room `
    -Begin 'void ResponseCreateRoom(' `
    -End 'void ResponseBorderTeamReturnRoom103('

if ($messageHeader -notmatch 'BOOL\s+SendCorrelatedResponseToClient\([\s\S]*?int\s+WireMsgSeq,\s*int\s+ProtocolSeq,\s*char\s+Encrypt\s*=\s*true\)') {
    throw 'correlated response sender is not declared with separate inner and outer sequence values'
}
$correlatedSender = Get-SourceRange `
    -Text $message `
    -Begin 'BOOL SendCorrelatedResponseToClient(' `
    -End 'BOOL SendToClientWithRawWireRouteIDsAndDialog('
if ($message -notmatch 'ProtocolHead->Seq\s*=\s*htonl\(ProtocolSeq\)') {
    throw 'CSHEAD.Seq is still hard-coded instead of using the explicit protocol response sequence'
}
if ($correlatedSender -notmatch 'SendToClientInternal\([\s\S]*?WireMsgSeq,\s*false,\s*WireDstID,\s*ProtocolSeq\)') {
    throw 'correlated response sender does not set CSHEAD.DialogID=WireDstID and CSHEAD.Seq=ProtocolSeq'
}

if ($response424 -notmatch 'TryGetCurrentInboundWireMsgSeq\(Client,\s*inboundWireMsgSeq\)') {
    throw '424 response does not read the inbound request MsgSeq'
}
if ($response424 -notmatch 'TryGetCurrentInboundProtocolSeq\(Client,\s*inboundProtocolSeq\)') {
    throw '424 response does not read the inbound request CSHEAD.Seq'
}
if ($response424 -notmatch 'SendCorrelatedResponseToClient\([\s\S]*?Client,\s*424[\s\S]*?Client->GameID,\s*FE_GAMELOGIC[\s\S]*?GetClientWirePlayerHeaderID\(Client\)[\s\S]*?inboundWireMsgSeq,\s*inboundProtocolSeq') {
    throw '424 response does not preserve the official route and both request sequence values'
}
if ($response424 -match 'SendToClient\(Client,\s*424') {
    throw '424 response still uses the uncorrelated generic sender'
}

if ($response105 -notmatch 'if\s*\(officialEscFirst105\)[\s\S]*?TryGetCurrentInboundWireMsgSeq\(Client,\s*inboundWireMsgSeq\)') {
    throw 'first official ESC 105 response does not read the inbound request MsgSeq'
}
if ($response105 -notmatch 'if\s*\(officialEscFirst105\)[\s\S]*?TryGetCurrentInboundProtocolSeq\(Client,\s*inboundProtocolSeq\)') {
    throw 'first official ESC 105 response does not read the inbound request CSHEAD.Seq'
}
if ($response105 -notmatch 'if\s*\(officialEscFirst105\)[\s\S]*?SendCorrelatedResponseToClient\([\s\S]*?Client,\s*105[\s\S]*?leaveRoomID,\s*FE_ROOM[\s\S]*?GetClientWirePlayerHeaderID\(Client\)[\s\S]*?inboundWireMsgSeq,\s*inboundProtocolSeq') {
    throw 'first official ESC 105 response is not correlated through both protocol headers'
}

if ($duplicate105 -notmatch 'TryGetCurrentInboundWireMsgSeq\(Client,\s*inboundWireMsgSeq\)') {
    throw 'duplicate official ESC 105 response does not read the inbound request MsgSeq'
}
if ($duplicate105 -notmatch 'TryGetCurrentInboundProtocolSeq\(Client,\s*inboundProtocolSeq\)') {
    throw 'duplicate official ESC 105 response does not read the inbound request CSHEAD.Seq'
}
if ($duplicate105 -notmatch 'SendCorrelatedResponseToClient\([\s\S]*?Client,\s*105[\s\S]*?0,\s*FE_ROOM[\s\S]*?GetClientWirePlayerHeaderID\(Client\)[\s\S]*?inboundWireMsgSeq,\s*inboundProtocolSeq') {
    throw 'duplicate official ESC 105 response is not correlated with cleared room context through both headers'
}
if ($duplicate105 -match 'SendToClient\(Client,\s*105') {
    throw 'duplicate official ESC 105 response still uses the uncorrelated generic sender'
}

if ($response103 -notmatch 'TryGetCurrentInboundWireMsgSeq\(Client,\s*inboundWireMsgSeq\)') {
    throw '103 response does not read the inbound request MsgSeq'
}
if ($response103 -notmatch 'TryGetCurrentInboundProtocolSeq\(Client,\s*inboundProtocolSeq\)') {
    throw '103 response does not read the inbound request CSHEAD.Seq'
}
if ($response103 -notmatch 'SendCorrelatedResponseToClient\([\s\S]*?Client,\s*103[\s\S]*?Client->ServerID,\s*FE_GAMESVRD[\s\S]*?createWireDstID[\s\S]*?inboundWireMsgSeq,\s*inboundProtocolSeq') {
    throw '103 response does not reproduce the official double-header request correlation'
}

Write-Host 'Official ESC response correlation regression passed.'
