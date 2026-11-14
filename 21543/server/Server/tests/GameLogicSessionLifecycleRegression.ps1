param(
    [string]$OfficialTrace = ''
)

$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$messageHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.h') -Raw
$roomHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.h') -Raw
$message = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.cpp') -Raw
$room = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.cpp') -Raw
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$luckyMatch = Get-Content -LiteralPath (Join-Path $serverRoot 'LuckyMatch.cpp') -Raw
$safeBuilderHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Msg28346Builder.h') -Raw
$safeBuilder = Get-Content -LiteralPath (Join-Path $serverRoot 'Msg28346Builder.cpp') -Raw

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

$production = $message + "`n" + $room + "`n" + $gameServer + "`n" + $luckyMatch
$allocator = Get-SourceRange `
    -Text $message `
    -Begin 'USHORT AllocateGameLogicSessionID()' `
    -End 'void ResetNormalRaceWireState('
$startGame = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestStartGame(' `
    -End 'void ResponseStartGame('
$raceBegin = Get-SourceRange `
    -Text $gameServer `
    -Begin 'bool NotifyRaceBegin(' `
    -End 'void NotifyPlayerFinishRace('
$leave514 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static bool SendLeaveGame514(' `
    -End 'static void SendLeaveGameToHall514('
$sendInternal = Get-SourceRange `
    -Text $message `
    -Begin 'static BOOL SendToClientInternal(' `
    -End 'BOOL SendToClient('

if ($production -match '\bGameID\s*=\s*2\s*;' -or
    $production -match '\bGameID\s*[!=]=\s*2\b') {
    throw 'a fixed GameID=2 assignment/sentinel remains in a race path'
}
if ($messageHeader -notmatch 'USHORT\s+GameID\s*=\s*0\s*;') {
    throw 'ClientNode GameID is not zero-initialized'
}
if ($messageHeader -notmatch 'int\s+NormalRaceWireLastMsgSeq\s*=\s*0\s*;') {
    throw 'normal-race wire state does not retain the last official sequence'
}
if ($messageHeader -notmatch 'USHORT\s+AllocateGameLogicSessionID\s*\(\s*\)\s*;') {
    throw 'the process-wide game-logic session allocator is not declared'
}
if ($roomHeader -notmatch 'USHORT\s+RaceGameID\s*=\s*0\s*;') {
    throw 'RoomNode does not retain one GameID shared by the whole race'
}
if ($allocator -match '\bSleep\s*\(|sleep_for|GetTickCount|time\s*\(|\brand\s*\(|BCryptGenRandom') {
    throw 'GameID allocation depends on delay, clock, PRNG, or CSPRNG'
}
if ($allocator -notmatch 'Client->GameID\s*==\s*candidate') {
    throw 'GameID allocator does not reject IDs held by a live client'
}
if ($allocator -notmatch '0x7FFF') {
    throw 'GameID allocator does not stay in the positive signed 16-bit route range'
}
if ($startGame -notmatch 'BeginRoomGameLogicSession\(Room,\s*"start-game"\)') {
    throw 'RequestStartGame does not allocate one new room-scoped GameID'
}
if ($startGame -notmatch 'RoomClient->GameID\s*=\s*Room->RaceGameID\s*;') {
    throw 'RequestStartGame does not assign the shared room GameID to every racer'
}
if ($startGame -notmatch 'ResetNormalRaceWireState\(RoomClient\)[\s\S]*?RoomClient->GameID\s*=\s*Room->RaceGameID') {
    throw 'new race assignment does not clear the previous normal wire session first'
}
if ($raceBegin -match 'NormalRaceWireActive\s*=\s*false|NormalRaceWireRouteID\s*=\s*0') {
    throw 'msg511 still destroys the route required by the official msg514'
}
if ($safeBuilderHeader -notmatch 'struct\s+CNotifyRaceBeginPara' -or
    $safeBuilderHeader -notmatch 'static_assert\s*\(\s*sizeof\(CNotifyRaceBeginPara\)\s*==\s*0x30\s*\)' -or
    $safeBuilderHeader -notmatch 'DWORD\s+RaceStartTime' -or
    $safeBuilderHeader -notmatch 'DWORD\s+RaceStartMicroSecond' -or
    $safeBuilderHeader -notmatch 'DWORD\s+RaceBeginSecond' -or
    $safeBuilderHeader -notmatch 'DWORD\s+RaceBeginMicroSecond' -or
    $safeBuilderHeader -notmatch 'DWORD\s+Para1') {
    throw 'the complete official 0x30-byte CNotifyRaceBeginPara layout is missing'
}
if ($safeBuilderHeader -notmatch 'BuildOfficialSafeRaceBegin511\s*\(' -or
    $safeBuilder -notmatch 'CSSafeCNotifyRaceBeginPara::ReadFromStream' -or
    $safeBuilder -notmatch 'FinishOfficial28346Body\s*\(\s*outerUin\s*,\s*context\s*,\s*511') {
    throw 'the official 28346(inner511) safe builder is missing'
}
if ($raceBegin -notmatch 'BuildOfficialSafeRaceBegin511\s*\(' -or
    $raceBegin -notmatch 'SendToClientWithWireDstIDAndMsgSeq\s*\(\s*Client\s*,\s*28346' -or
    $raceBegin -notmatch 'SendToClient\s*\(\s*Client\s*,\s*28346') {
    throw 'NotifyRaceBegin does not route both game paths through outer Msg28346'
}
if ($raceBegin -match 'SendToClient(?:WithWireDstIDAndMsgSeq)?\s*\(\s*Client\s*,\s*511') {
    throw 'NotifyRaceBegin still contains a direct Msg511 send path'
}
if ($raceBegin -notmatch 'outerContext\s*=\s*static_cast<DWORD>\(time\(nullptr\)\)' -or
    $raceBegin -notmatch 'BuildOfficialSafeRaceBegin511\s*\(\s*Client->Uin\s*,\s*outerContext' -or
    $raceBegin -notmatch 'ServerMicroSecond\s*=\s*[\s\S]*?epochMicroseconds\s*%\s*1000000' -or
    $raceBegin -notmatch 'Para1\s*=\s*[\s\S]*?LoginClientPID[\s\S]*?kClientPidCheckXor') {
    throw 'NotifyRaceBegin does not populate the official context/microsecond/PID-bound fields'
}
if ($sendInternal -notmatch 'NormalRaceWireActive' -or
    $sendInternal -notmatch 'NormalRaceWireRouteID' -or
    $sendInternal -notmatch 'NormalRaceWireLastMsgSeq\s*\+\s*1' -or
    $sendInternal -notmatch 'effectiveWireMsgSeq') {
    throw 'the shared send path does not continue the official game-route sequence after msg511'
}
if ($leave514 -notmatch 'NormalRaceWireLastMsgSeq\s*<\s*3') {
    throw 'msg514 does not accept the next live sequence after the initial 509/551/907 chain'
}
if ($leave514 -notmatch 'SendToClientWithWireDstIDAndMsgSeq\s*\(') {
    throw 'normal msg514 is not sent on the preserved official game route'
}
if ($leave514 -notmatch 'ResetNormalRaceWireState\(Client\)') {
    throw 'successful msg514 does not close and clear the normal wire session'
}

if ($OfficialTrace) {
    if (-not (Test-Path -LiteralPath $OfficialTrace -PathType Leaf)) {
        throw "official trace not found: $OfficialTrace"
    }
    $trace = Get-Content -LiteralPath $OfficialTrace -Raw
    foreach ($pattern in @(
        'event=unpack msg=509\b[^\r\n]*\bseq=1\b[^\r\n]*\bsrcId=1366\b',
        'event=unpack msg=511\b[^\r\n]*\bseq=4\b[^\r\n]*\bsrcId=1366\b',
        'event=unpack msg=514\b[^\r\n]*\bseq=5\b[^\r\n]*\bsrcId=1366\b',
        'event=unpack msg=514\b[^\r\n]*\bseq=38\b[^\r\n]*\bsrcId=621\b',
        'event=unpack msg=509\b[^\r\n]*\bseq=1\b[^\r\n]*\bsrcId=112\b'
    )) {
        if ($trace -notmatch $pattern) {
            throw "official game-session trace evidence missing: $pattern"
        }
    }
    Write-Host 'Official game-route sequence fixtures passed (including post-511 seq38 -> 514).'
}

Write-Host 'Game-logic session lifecycle regression passed.'
