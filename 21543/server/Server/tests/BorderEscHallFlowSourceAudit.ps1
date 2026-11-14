$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$room = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.cpp') -Raw
$roomHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.h') -Raw
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

$request424 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'void RequestLeaveGame(' `
    -End 'void ResponseLeaveGame('
$border424 = Get-SourceRange `
    -Text $request424 `
    -Begin 'std::shared_ptr<RoomNode> RaceRoom = BorderRoom ? BorderRoom : Room;' `
    -End 'if (RaceRoom && Room)'
$official105 = Get-SourceRange `
    -Text $room `
    -Begin 'static bool HandleOfficialEscLeaveRoom105(' `
    -End 'void RequestLeaveRoom('
$leaveRoom = Get-SourceRange `
    -Text $room `
    -Begin 'void LeaveRoom(' `
    -End 'void LeaveBorderRoom('
$response105 = Get-SourceRange `
    -Text $room `
    -Begin 'void ResponseLeaveRoom(' `
    -End 'void NotifyLeaveRoom('
$completeEsc = Get-SourceRange `
    -Text $message `
    -Begin 'static void CompleteOfficialEscFlow(' `
    -End 'void OnRecvFromClient('
if ($messageHeader -notmatch 'QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL\s*=\s*3') {
    throw 'border-to-hall official ESC flow kind is missing'
}
if ($gameServer -notmatch 'case GameBegin509V5Profile::Border:\s*[\r\n]+\s*fields\.normalLoadTime[\s\S]*?fields\.gameBeginSwitchFlag\s*=\s*0x81F88EB3u') {
    throw 'border msg509 does not enable the client ENGBSF_EnableLeaveGame bit (0x00200000)'
}
if ($request424 -match 'LEAVE-GAME-BORDER-TEAM-RETURN|SendLeaveGameToRoom514\(Client,\s*Room|ReturnClientToOriginalRoom\(Client,\s*Room,\s*"leave-game-border') {
    throw 'team border ESC still has a return-to-team-room shortcut'
}
if ($border424 -notmatch 'SendLeaveGameEscToRoom514\(Client,\s*RaceRoom,\s*LeaveGameType,\s*0,\s*reason\)') {
    throw 'border ESC does not send the normal-room ReturnHall=0/WaitEnterRoom=0 msg514'
}
if ($border424 -notmatch 'BeginOfficialEscFlow\(Client,\s*RaceRoom,\s*false,\s*true,\s*reason\)') {
    throw 'border ESC does not arm the deferred border-to-hall transaction'
}
if ($border424 -match '\bLeaveBorderRoom\s*\(|\bLeaveRoom\s*\(|\bResponseLeaveGame\s*\(') {
    throw 'border C424 still commits a room leave or sends a normal single-room C424 response early'
}

$responseIndex = $official105.IndexOf('ResponseLeaveRoom(Client, LeaveType);', [StringComparison]::Ordinal)
$borderLeaveIndex = $official105.IndexOf('LeaveBorderRoom(Client, LeaveType, false);', [StringComparison]::Ordinal)
$teamLeaveIndex = $official105.IndexOf('LeaveRoom(Client, LeaveType, false);', [StringComparison]::Ordinal)
if ($official105 -notmatch 'GetBorderRoom\(clientRoomID\)' -or
    $responseIndex -lt 0 -or $borderLeaveIndex -le $responseIndex -or $teamLeaveIndex -le $borderLeaveIndex) {
    throw 'client C105 does not acknowledge the border room before silently clearing border and team-room state'
}
if ($roomHeader -notmatch 'void\s+LeaveRoom\([^;]*bool\s+sendLeaveAck\s*=\s*true\)') {
    throw 'normal-room cleanup cannot suppress the duplicate S105 for a team-border hall exit'
}
if ($leaveRoom -notmatch 'if\s*\(sendLeaveAck\)\s*ResponseLeaveRoom\(Client,\s*LeaveType\)') {
    throw 'LeaveRoom does not honor the S105 suppression flag'
}
if ($response105 -notmatch 'QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL[\s\S]*?Client->OfficialEscRoomID\s*==\s*Client->BorderRoomID' -or
    $response105 -notmatch 'SendCorrelatedResponseToClient\([\s\S]*?Client,\s*105[\s\S]*?leaveRoomID,\s*FE_ROOM') {
    throw 'border S105 is not correlated with the active border room ID'
}
if ($completeEsc -notmatch 'QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL\)[\s\S]*?nextMsgID\s*==\s*102') {
    throw 'client C102 does not complete the border-to-hall ESC transaction'
}

Write-Host 'Border ESC hall-flow source audit passed.'
