param(
    [string]$OfficialPayloadRoot = '',
    [string]$CustomReturnPayloadRoot = ''
)

$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$room = Get-Content -LiteralPath (Join-Path $serverRoot 'Room.cpp') -Raw
$player = Get-Content -LiteralPath (Join-Path $serverRoot 'Player.cpp') -Raw
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

$countdown3 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'void WINAPI OnCountDown3(' `
    -End 'void WINAPI OnCountDown2('
$normalTail = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static void ReturnNormalRaceRoomOfficialTail(' `
    -End 'static void SendOfficialReturnRoomChain('
$participantPrepare = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static void PrepareOfficialRoomReturnParticipant(' `
    -End 'static void FinalizeOfficialRoomReturnParticipant('
$returnChain = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static void SendOfficialReturnRoomChain(' `
    -End 'static void ReturnClientToOriginalRoom('
$borderRestore = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static bool RestoreBorderTeamReturnStateOnly(' `
    -End 'static int SendBorderTeamReturnRoomOfficialRoster('
$borderRoster = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static int SendBorderTeamReturnRoomOfficialRoster(' `
    -End 'static void ReturnLeaveGameTo3DRoom('
$twelveReturnNormalize = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static void NormalizeTwelvePlayerReturnRoom(' `
    -End 'static void NormalizeNormalCustomReturnRoom('
$roomSeatModel = Get-SourceRange `
    -Text $room `
    -Begin 'bool IsTwelvePlayerRoom(' `
    -End 'RoomSeatMask GetRoomSeatBit('
$createRoom = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestCreateRoom(' `
    -End 'void ResponseCreateRoom('
$action62Flow = Get-SourceRange `
    -Text $room `
    -Begin 'bool TryHandleOfficialRoomReturnAction62(' `
    -End 'bool CanAcceptOfficialRoomReturnReadyChange('
$returnReadyRoster = Get-SourceRange `
    -Text $room `
    -Begin 'void NotifyOfficialReturnReadyObRoster(' `
    -End 'void NotifyOfficialReturnSelfReadyObState('
$returnSelfReadyState = Get-SourceRange `
    -Text $room `
    -Begin 'void NotifyOfficialReturnSelfReadyObState(' `
    -End 'static bool IsOfficialRoomReturnKind('
$readyGate = Get-SourceRange `
    -Text $room `
    -Begin 'bool CanAcceptOfficialRoomReturnReadyChange(' `
    -End 'void CompleteOfficialRoomReturnReadyChange('
$completeReady = Get-SourceRange `
    -Text $room `
    -Begin 'void CompleteOfficialRoomReturnReadyChange(' `
    -End 'void RequestChangeReadyState('
$readyRequest = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestChangeReadyState(' `
    -End 'void ResponseChangeReadyState('
$readyResponse = Get-SourceRange `
    -Text $room `
    -Begin 'void ResponseChangeReadyState(' `
    -End 'uint32_t splitAndConvert('
$enterInvited = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestEnterInvitedRoom(' `
    -End 'void RequestEnterRoom('
$enterRoom = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestEnterRoom(' `
    -End 'static void WriteOfficialRoomPlayerVisibleInfo('
$startGame = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestStartGame(' `
    -End 'void ResponseStartGame('
$playerAction = Get-SourceRange `
    -Text $message `
    -Begin 'void RequestPlayerActionPara(' `
    -End 'void ResponsePlayerActionPara('

if ($messageHeader -notmatch 'struct\s+OfficialRoomReturnFlowState' -or
    $messageHeader -notmatch 'OfficialRoomReturnFlowState\s+OfficialRoomReturn') {
    throw 'ClientNode does not own an explicit completed-race room-return transaction'
}
if ($messageHeader -notmatch 'QQS_ROOM_READY_STATE_READY\s*=\s*0' -or
    $messageHeader -notmatch 'QQS_ROOM_READY_STATE_NOT_READY\s*=\s*1' -or
    $messageHeader -notmatch 'QQS_PLAYER_VISIBLE_STATUS_NOT_READY\s*=\s*0' -or
    $messageHeader -notmatch 'QQS_PLAYER_VISIBLE_STATUS_READY\s*=\s*1') {
    throw 'room ReadyState and PlayerRoomVisibleInfo Status domains are no longer declared independently'
}
if ($messageHeader -notmatch 'QQS_ToPlayerVisibleStatus[\s\S]*?QQS_ROOM_READY_STATE_READY[\s\S]*?QQS_PLAYER_VISIBLE_STATUS_READY[\s\S]*?QQS_PLAYER_VISIBLE_STATUS_NOT_READY') {
    throw 'PlayerRoomVisibleInfo Status no longer converts from the inverse ReadyState domain'
}
if ([regex]::Matches($player, 'QQS_ToPlayerVisibleStatus\(static_cast<BYTE>\(Client->ReadyState\)\)').Count -ne 2) {
    throw 'both PlayerRoomVisibleInfo writers must convert ReadyState into visible Status'
}
foreach ($phase in @(
    'SERVER_CHAIN_PENDING',
    'WAIT_AT62_SEND_ROSTER',
    'WAIT_AT62_ACK_ONLY',
    'AT62_COMPLETE'
)) {
    if ($messageHeader -notmatch "QQS_OFFICIAL_ROOM_RETURN_PHASE_$phase") {
        throw "missing official room-return phase: $phase"
    }
}

$preparedIndex = $countdown3.IndexOf('[FLOW][ROOM-RETURN-GROUP-PREPARED]', [StringComparison]::Ordinal)
$gameOverIndex = $countdown3.IndexOf('NotifyGameOver(RoomClient', [StringComparison]::Ordinal)
$dispatchIndex = $countdown3.IndexOf('ReturnClientToOriginalRoom(RoomClient', [StringComparison]::Ordinal)
if ($preparedIndex -lt 0 -or $gameOverIndex -le $preparedIndex -or $dispatchIndex -le $gameOverIndex) {
    throw 'OnCountDown3 is not a prepare-all -> 514-all -> return-chain-all transaction'
}
if ([regex]::Matches($countdown3, 'NotifyGameOver\(RoomClient').Count -ne 1) {
    throw 'OnCountDown3 must serialize 514 in exactly one group pass'
}
if ($countdown3 -notmatch 'PrepareOfficialRoomReturnParticipant' -or
    $countdown3 -notmatch 'QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_SEND_ROSTER') {
    throw 'normal race participants are not armed before the first 514'
}
$beginFlowIndex = $participantPrepare.IndexOf('BeginOfficialRoomReturnFlow', [StringComparison]::Ordinal)
$baselineIndex = $participantPrepare.IndexOf('Client->ReadyState = QQS_ROOM_READY_STATE_NOT_READY', [StringComparison]::Ordinal)
if ($beginFlowIndex -lt 0 -or $baselineIndex -le $beginFlowIndex) {
    throw 'participant return flow must be armed before the official not-ready return baseline is restored'
}
if ($participantPrepare -match 'Client->ReadyState\s*=\s*QQS_ROOM_READY_STATE_READY') {
    throw 'participant return preparation still forces ready instead of waiting for the client C109'
}
$prepareCallIndex = $countdown3.LastIndexOf('PrepareOfficialRoomReturnParticipant(', $preparedIndex, [StringComparison]::Ordinal)
$roomIdleIndex = $countdown3.IndexOf('Room->startStatus = 1', [StringComparison]::Ordinal)
if ($prepareCallIndex -lt 0 -or $roomIdleIndex -le $prepareCallIndex -or $roomIdleIndex -ge $preparedIndex) {
    throw 'OnCountDown3 exposes idle room state before every participant transaction is prepared'
}

if ($normalTail -match 'NotifyOfficialReturn(ReadyObRoster|SelfReadyObState)') {
    throw 'normal return still sends 504/609 before client AT=62'
}
if ($normalTail -notmatch 'WAIT_AT62_SEND_ROSTER' -or
    $normalTail -notmatch 'FinalizeOfficialRoomReturnParticipant') {
    throw 'normal return tail does not retain the explicit AT=62 transaction'
}

if ($returnChain -notmatch 'SERVER_CHAIN_PENDING[\s\S]*?isNormalCustomRoom[\s\S]*?WAIT_AT62_SEND_ROSTER[\s\S]*?WAIT_AT62_ACK_ONLY') {
    throw '6P custom return does not wait for AT=62 before sending its self 504/609 state'
}
if ($returnChain -match 'NotifyOfficialReturnSelfReadyObState') {
    throw '6P custom return still sends self 504/609 inside the pre-AT=62 server chain'
}
if ($returnChain -match 'needNormalCustom500Roster' -or
    $returnChain -notmatch 'if\s*\(!isNormalCustomRoom\)[\s\S]*?NotifyEnterRoom\(Client') {
    throw '6P custom return still duplicates the other-player S104 roster with S500'
}
if ($returnChain -match 'readyObSentBeforeRoster|兜底') {
    throw 'custom return chain still contains a fallback packet branch'
}
if ($borderRestore -notmatch 'QQS_OFFICIAL_ROOM_RETURN_KIND_BORDER_TEAM[\s\S]*?SERVER_CHAIN_PENDING') {
    throw 'border team restore is not armed as an explicit return transaction'
}
if ($borderRoster -notmatch 'NotifyOfficialReturnReadyObRoster[\s\S]*?WAIT_AT62_ACK_ONLY') {
    throw 'border team return does not finish 504/609 before the AT=62 ACK phase'
}

if ($roomSeatModel -notmatch 'IsTwelvePlayerRoom\(Room\)\s*\?\s*ROOM_SEAT_12_MAX') {
    throw '12P room capacity is no longer fixed at twelve slots'
}

foreach ($entryFlow in @(
    @{ Name = 'invited room'; Source = $enterInvited },
    @{ Name = 'normal room'; Source = $enterRoom }
)) {
    if ($entryFlow.Source -notmatch 'Client->ReadyState\s*=\s*QQS_ROOM_READY_STATE_NOT_READY') {
        throw "$($entryFlow.Name) does not commit the joining player as not-ready"
    }
    if ($entryFlow.Source -notmatch 'ResponseEnterRoom\(Client[\s\S]*?EnterRoom104RosterMode::OtherVisiblePlayers') {
        throw "$($entryFlow.Name) no longer emits the official other-player-only S104 roster"
    }
    if ($entryFlow.Source -match 'NotifyInitialEnterRoomStateAfter104' -or
        $entryFlow.Source -match 'NotifyChangeReadyState\(Client,\s*Client->Uin') {
        throw "$($entryFlow.Name) sends a non-official self S504 after S104"
    }
    if ($entryFlow.Source -match 'SendToClient\(Client,\s*134' -or
        $entryFlow.Source -match 'NotifyChangeSeatState\(Client') {
        throw "$($entryFlow.Name) sends a non-official self S134/S133 refresh after S104"
    }
}
if ($room -match 'NotifyInitialEnterRoomStateAfter104' -or
    $room -match 'NotifyRoomStateAfterEnter104Custom') {
    throw 'obsolete initial-entry self ready/OB refresh helper remains in source'
}
if ($returnReadyRoster -notmatch 'if\s*\(!RoomClient\s*\|\|\s*RoomClient\s*==\s*Client\)\s*continue;') {
    throw 'return 504/609 roster does not exclude the receiving player'
}
if ($returnReadyRoster -notmatch 'OtherOnly=1') {
    throw 'return ready/OB roster no longer records its official other-player-only contract'
}
if ($returnSelfReadyState -notmatch 'NotifyChangeReadyState\(Client,\s*Client->Uin,\s*Client->ReadyState\)' -or
    $returnSelfReadyState -notmatch 'NotifyChangeOBState\(Client,\s*FALSE,\s*Client->Uin\)') {
    throw 'post-AT=62 return state does not emit S504/S609 for the receiving player'
}
if ($returnSelfReadyState -match 'RoomClient' -or
    $returnSelfReadyState -match 'for\s*\(') {
    throw 'post-AT=62 self state unexpectedly iterates the other-player roster'
}
if ($createRoom -notmatch 'TwelvePlayerCustomRoomShell\s*=\s*\(SeatNum\s*==\s*ROOM_SEAT_12_MAX\)') {
    throw 'SeatNum=12 creation no longer establishes the explicit 12P room shell'
}
if ($createRoom -notmatch 'roomSeatCapacity\s*=\s*GetRoomSeatCapacity\(Room\)[\s\S]*?SeatState\[i\]\s*=\s*\(i\s*<\s*roomSeatCapacity\)\s*\?\s*1\s*:\s*0') {
    throw '12P creation no longer opens every configured seat from the live room request'
}
if ($twelveReturnNormalize -match 'SeatState\s*\[' -or
    $twelveReturnNormalize -match 'SeatNum\s*=\s*9') {
    throw '12P return normalization copies a capture-specific locked-seat layout'
}

if ($playerAction -notmatch 'ActionType\s*==\s*62[\s\S]*?TryHandleOfficialRoomReturnAction62\(Client\)') {
    throw '24640 AT=62 is not routed through the explicit room-return transaction'
}
if ($playerAction -match 'returnAckGuard|Client->LastRoomReturnTick') {
    throw '24640 AT=62 still guesses a return transaction from a timing heuristic'
}

$rosterIndex = $action62Flow.IndexOf('NotifyOfficialReturnSelfReadyObState', [StringComparison]::Ordinal)
$at62CompleteIndex = $action62Flow.IndexOf('OfficialRoomReturn.Phase = QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE', [StringComparison]::Ordinal)
$ackIndex = $action62Flow.IndexOf('ResponsePlayerActionParaAck(Client, 62)', [StringComparison]::Ordinal)
if ($rosterIndex -lt 0 -or $at62CompleteIndex -le $rosterIndex -or $ackIndex -le $at62CompleteIndex) {
    throw 'AT=62 does not send self 504/609 before its minimal 24640 ACK'
}
if ($action62Flow -notmatch 'SERVER_CHAIN_PENDING[\s\S]*?return true') {
    throw 'out-of-order custom/border AT=62 is not consumed while the server chain is pending'
}
if ($readyGate -match 'ROOM-RETURN-109-DEFER' -or
    $readyGate -match 'wait AT62 completion' -or
    $readyGate -match 'return false;') {
    throw 'a valid return-room C109 can still be dropped while AT=62 is pending'
}
if ($readyGate -notmatch 'ROOM-RETURN-109-ACCEPT') {
    throw 'early valid return-room C109 acceptance is no longer explicit'
}
if ($messageHeader -notmatch 'bool\s+ReadyRequestCommitted\s*=\s*false' -or
    $messageHeader -notmatch 'DWORD\s+ReadyRequestTick\s*=\s*0') {
    throw 'C109 commit state is not modeled independently from the AT=62 packet phase'
}
if ($room -match 'QQS_OFFICIAL_ROOM_RETURN_PHASE_READY_CHANGE_COMMITTED') {
    throw 'C109 commit is still encoded by overwriting the AT=62 packet phase'
}
if ($completeReady -match 'OfficialRoomReturn\.Phase\s*=') {
    throw 'C109 completion still overwrites the independent AT=62 packet phase'
}
if ($completeReady -notmatch 'ReadyRequestCommitted\s*=\s*true' -or
    $completeReady -notmatch 'ReadyRequestTick\s*=\s*GetTickCount\(\)') {
    throw 'C109 completion does not record its independent commit dimension'
}

if ($readyRequest -notmatch 'kRequestWireBodyLen\s*=\s*sizeof\(USHORT\)\s*\+\s*kRequestPayloadLen') {
    throw 'C109 does not validate the declared wire body structure'
}
if ($readyRequest -notmatch 'Uin\s*!=\s*Client->Uin' -or
    $readyRequest -notmatch 'ReadyStateValue\s*>\s*QQS_ROOM_READY_STATE_NOT_READY') {
    throw 'C109 does not validate request identity and ready-state domain'
}
$notIdleBegin = $readyRequest.IndexOf('if (Room->startStatus != 1)', [StringComparison]::Ordinal)
$returnGate = $readyRequest.IndexOf('if (!CanAcceptOfficialRoomReturnReadyChange', [StringComparison]::Ordinal)
if ($notIdleBegin -lt 0 -or $returnGate -le $notIdleBegin) {
    throw 'C109 idle/return phase guards are missing or reordered'
}
$notIdleBlock = $readyRequest.Substring($notIdleBegin, $returnGate - $notIdleBegin)
if ($notIdleBlock -match 'ResponseChangeReadyState') {
    throw 'C109 still reports false success while the room is loading/racing'
}
$authoritativeWrite = $readyRequest.IndexOf('AuthoritativeClient->ReadyState = ReadyStateValue', [StringComparison]::Ordinal)
$response109 = $readyRequest.IndexOf('ResponseChangeReadyState(Client', [StringComparison]::Ordinal)
$complete109 = $readyRequest.IndexOf('CompleteOfficialRoomReturnReadyChange', [StringComparison]::Ordinal)
if ($authoritativeWrite -lt 0 -or $response109 -le $authoritativeWrite -or $complete109 -le $response109) {
    throw 'C109 does not commit the authoritative room slot before S109/transaction completion'
}
if ($readyRequest -notmatch 'Room->Player\[authoritativeSeat\]') {
    throw 'C109 still writes only a detached client object'
}
if ($readyResponse -notmatch 'Write16\(p,\s*0\)[\s\S]*?Write8\(p,\s*ReadyState\)[\s\S]*?Write8\(p,\s*0\)[\s\S]*?SendToClient\(Client,\s*109') {
    throw 'S109 field order no longer matches ResultID/ReadyState/ReasonLen'
}
if ($startGame -notmatch 'ReadyState\s*!=\s*QQS_ROOM_READY_STATE_READY') {
    throw 'start-game validation does not read the same ready=0 authoritative state'
}
if ($room -notmatch 'ReadyRequestCommitted\s*=\s*true' -or
    $room -notmatch 'ReadyRequestTick\s*=\s*GetTickCount\(\)') {
    throw 'accepted C109 is not retained independently until start/leave'
}

if ($OfficialPayloadRoot) {
    function Read-U16BE {
        param(
            [Parameter(Mandatory = $true)][byte[]]$Bytes,
            [Parameter(Mandatory = $true)][int]$Offset
        )
        return ([int]$Bytes[$Offset] -shl 8) -bor [int]$Bytes[$Offset + 1]
    }

    function Read-U32BE {
        param(
            [Parameter(Mandatory = $true)][byte[]]$Bytes,
            [Parameter(Mandatory = $true)][int]$Offset
        )
        return ([uint32]$Bytes[$Offset] -shl 24) -bor
            ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
            ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
            [uint32]$Bytes[$Offset + 3]
    }

    function Read-Official104Roster {
        param(
            [Parameter(Mandatory = $true)][string]$Path
        )
        [byte[]]$bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 56) {
            throw "official S104 fixture is truncated: $Path"
        }

        $selfSeat = [int]$bytes[35]
        $playerCount = [int]$bytes[55]
        $occupiedCount = 0
        for ($seat = 0; $seat -lt 12; $seat++) {
            if ($bytes[43 + $seat] -eq 2) {
                $occupiedCount++
            }
        }
        if ($selfSeat -ge 12 -or $bytes[43 + $selfSeat] -ne 2) {
            throw "official S104 self seat is not occupied: $Path"
        }
        if ($occupiedCount -ne ($playerCount + 1)) {
            throw "official S104 does not encode PlayerCount as occupied players minus self: $Path"
        }

        $offset = 56
        $records = @()
        for ($i = 0; $i -lt $playerCount; $i++) {
            if ($offset + 15 -gt $bytes.Length) {
                throw "official S104 visible roster is truncated: $Path record=$i"
            }
            $recordLength = Read-U16BE -Bytes $bytes -Offset $offset
            if ($recordLength -lt 15 -or $offset + $recordLength -gt $bytes.Length) {
                throw "official S104 visible record length is invalid: $Path record=$i len=$recordLength"
            }
            $recordSeat = [int]$bytes[$offset + 10]
            if ($recordSeat -eq $selfSeat) {
                throw "official S104 visible roster unexpectedly contains the receiving player: $Path"
            }
            $records += [pscustomobject]@{
                Uin = Read-U32BE -Bytes $bytes -Offset ($offset + 2)
                Seat = $recordSeat
                VisibleStatus = [byte]$bytes[$offset + 14]
            }
            $offset += $recordLength
        }
        return [pscustomobject]@{
            SelfSeat = $selfSeat
            PlayerCount = $playerCount
            OccupiedCount = $occupiedCount
            Records = $records
        }
    }

    function Assert-WireReadyState {
        param(
            [Parameter(Mandatory = $true)][string]$Path,
            [Parameter(Mandatory = $true)][int]$Offset,
            [Parameter(Mandatory = $true)][byte]$Expected
        )
        [byte[]]$bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -le $Offset -or $bytes[$Offset] -ne $Expected) {
            throw "official ready-state fixture mismatch: $Path offset=$Offset expected=$Expected"
        }
    }

    # S109: BodyLen(2), ResultID(2), ReadyState(1), ReasonLen(1).
    Assert-WireReadyState `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_21259_inner_dispatch_body_msg109_len18.bin') `
        -Offset 4 `
        -Expected 0
    # The same official room contains both states: 0=ready and 1=not-ready.
    # S504: BodyLen(2), Uin(4), ReadyState(1).
    Assert-WireReadyState `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_16242_inner_dispatch_body_msg504_len18.bin') `
        -Offset 6 `
        -Expected 0
    Assert-WireReadyState `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_16246_inner_dispatch_body_msg504_len18.bin') `
        -Offset 6 `
        -Expected 1

    # PlayerRoomVisibleInfo::Status is not the S504 ReadyState enum.
    # The same official entrant first appears in S500 as Status=0 (not ready),
    # then changes through S504 ReadyState=0 (ready).
    [byte[]]$enter500 = [IO.File]::ReadAllBytes(
        (Join-Path $OfficialPayloadRoot 'network_bin_17072_20864_inner_dispatch_body_msg500_len1602.bin'))
    [byte[]]$ready504 = [IO.File]::ReadAllBytes(
        (Join-Path $OfficialPayloadRoot 'network_bin_17072_21140_inner_dispatch_body_msg504_len18.bin'))
    [uint32]$enter500Uin = Read-U32BE -Bytes $enter500 -Offset 6
    [uint32]$ready504Uin = Read-U32BE -Bytes $ready504 -Offset 2
    if ($enter500Uin -ne $ready504Uin) {
        throw 'official S500/S504 transition fixtures do not identify the same player'
    }
    if ($enter500[18] -ne 0 -or $ready504[6] -ne 0) {
        throw 'official visible Status=0 -> ReadyState=0 transition no longer proves the inverse field domains'
    }

    $room104Fixtures = @(
        'network_bin_17072_13868_inner_dispatch_body_msg104_len6514.bin',
        'network_bin_17072_15616_inner_dispatch_body_msg104_len8354.bin',
        'network_bin_17072_17400_inner_dispatch_body_msg104_len4666.bin',
        'network_bin_17072_19079_inner_dispatch_body_msg104_len3274.bin',
        'network_bin_17072_20767_inner_dispatch_body_msg104_len11842.bin'
    )
    foreach ($fixture in $room104Fixtures) {
        $null = Read-Official104Roster -Path (Join-Path $OfficialPayloadRoot $fixture)
    }

    # In the same captured 12P room context, later S504 notifications target players
    # already present in the S104 other-player roster, never the omitted receiver.
    $twelveRoster = Read-Official104Roster `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_20767_inner_dispatch_body_msg104_len11842.bin')
    [uint32[]]$twelveRosterUins = @($twelveRoster.Records | ForEach-Object { $_.Uin })
    foreach ($fixture in @(
        'network_bin_17072_21008_inner_dispatch_body_msg504_len18.bin',
        'network_bin_17072_21271_inner_dispatch_body_msg504_len18.bin'
    )) {
        [byte[]]$notify504 = [IO.File]::ReadAllBytes((Join-Path $OfficialPayloadRoot $fixture))
        [uint32]$notifyUin = Read-U32BE -Bytes $notify504 -Offset 2
        if ($twelveRosterUins -notcontains $notifyUin) {
            throw "official S504 does not target an existing other-player S104 record: $fixture"
        }
    }

    $officialTracePath = Join-Path (Split-Path -Parent $OfficialPayloadRoot) 'network_trace_17072.log'
    if (Test-Path -LiteralPath $officialTracePath) {
        $officialTrace = Get-Content -LiteralPath $officialTracePath -Raw
        $initialEnterBegin = $officialTrace.IndexOf('[101253015]', [StringComparison]::Ordinal)
        $initialEnter104 = $officialTrace.IndexOf('[101255125]', $initialEnterBegin, [StringComparison]::Ordinal)
        $initialEnter109 = $officialTrace.IndexOf('[101262984]', $initialEnter104, [StringComparison]::Ordinal)
        if ($initialEnterBegin -lt 0 -or $initialEnter104 -le $initialEnterBegin -or $initialEnter109 -le $initialEnter104) {
            throw 'official initial-enter C104/S104/C109 trace window is missing'
        }
        $initialEnterWindow = $officialTrace.Substring($initialEnterBegin, $initialEnter109 - $initialEnterBegin)
        if ($initialEnterWindow -notmatch 'event=pack[^\r\n]*cmd=104' -or
            $initialEnterWindow -notmatch 'event=inner_dispatch msg=104') {
            throw 'official initial-enter trace window no longer contains C104 -> S104'
        }
        if ($initialEnterWindow -match 'event=inner_dispatch msg=(133|134|504|609)\b') {
            throw 'official initial-enter trace unexpectedly refreshes the receiving player before C109'
        }
    }
    Write-Host 'Official ready-state packet fixtures passed.'
}

if ($CustomReturnPayloadRoot) {
    $custom514Path = Join-Path $CustomReturnPayloadRoot 'network_bin_41440_12442_inner_dispatch_body_msg514_len26.bin'
    $custom104Path = Join-Path $CustomReturnPayloadRoot 'network_bin_41440_12447_inner_dispatch_body_msg104_len370.bin'
    $custom504Path = Join-Path $CustomReturnPayloadRoot 'network_bin_41440_12560_inner_dispatch_body_msg504_len18.bin'
    $custom609Path = Join-Path $CustomReturnPayloadRoot 'network_bin_41440_12564_inner_dispatch_body_msg24834_len18.bin'
    foreach ($path in @($custom514Path, $custom104Path, $custom504Path, $custom609Path)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "official 6P custom-return fixture is missing: $path"
        }
    }

    [byte[]]$custom514 = [IO.File]::ReadAllBytes($custom514Path)
    [byte[]]$custom104 = [IO.File]::ReadAllBytes($custom104Path)
    [byte[]]$custom504 = [IO.File]::ReadAllBytes($custom504Path)
    [byte[]]$custom609 = [IO.File]::ReadAllBytes($custom609Path)
    if ($custom514.Length -lt 7 -or $custom514[5] -ne 0 -or $custom514[6] -ne 1) {
        throw 'official custom S514 no longer proves ReturnHall=0 and WaitEnterRoom=1'
    }
    if ($custom104.Length -lt 56 -or $custom104[32] -ne 6 -or $custom104[35] -ne 0 -or $custom104[55] -ne 0) {
        throw 'official custom S104 fixture no longer proves the single-player 6-seat return shell'
    }

    [uint32]$readyUin = ([uint32]$custom504[2] * 16777216) +
        ([uint32]$custom504[3] * 65536) + ([uint32]$custom504[4] * 256) + [uint32]$custom504[5]
    [uint32]$obUin = ([uint32]$custom609[2] * 16777216) +
        ([uint32]$custom609[3] * 65536) + ([uint32]$custom609[4] * 256) + [uint32]$custom609[5]
    if ($readyUin -ne 924496061 -or $obUin -ne $readyUin -or $custom504[6] -ne 1 -or $custom609[6] -ne 0) {
        throw 'official custom return S504/S609 do not target the receiving player with not-ready/non-OB state'
    }

    $customTracePath = Join-Path (Split-Path -Parent $CustomReturnPayloadRoot) 'network_trace_41440.log'
    if (-not (Test-Path -LiteralPath $customTracePath)) {
        throw "official 6P custom-return trace is missing: $customTracePath"
    }
    $customTrace = Get-Content -LiteralPath $customTracePath -Raw
    $s514Index = $customTrace.IndexOf('[142831453]', [StringComparison]::Ordinal)
    $s104Index = $customTrace.IndexOf('[142831468]', $s514Index, [StringComparison]::Ordinal)
    $c62Index = $customTrace.IndexOf('[142832171]', $s104Index, [StringComparison]::Ordinal)
    $s504Index = $customTrace.IndexOf('[142832453]', $c62Index, [StringComparison]::Ordinal)
    $s609Index = $customTrace.IndexOf('[142832468]', $s504Index, [StringComparison]::Ordinal)
    $ackIndex = $customTrace.IndexOf('[142832734]', $s609Index, [StringComparison]::Ordinal)
    if ($s514Index -lt 0 -or $s104Index -le $s514Index -or $c62Index -le $s104Index -or
        $s504Index -le $c62Index -or $s609Index -le $s504Index -or $ackIndex -le $s609Index) {
        throw 'official custom return order is no longer S514 -> S104 -> C24640 -> S504 -> S609 -> S24640 ACK'
    }
    $customReturnWindow = $customTrace.Substring($s514Index, $ackIndex - $s514Index)
    if ($customReturnWindow -notmatch 'event=pack[^\r\n]*cmd=24640' -or
        $customReturnWindow -notmatch 'event=inner_dispatch msg=504' -or
        $customReturnWindow -notmatch 'event=inner_dispatch msg=24834[^\r\n]*rawMsg=609') {
        throw 'official custom return trace events no longer identify AT=62/S504/S609'
    }
    Write-Host 'Official 6P custom-return packet fixtures passed.'
}

Write-Host 'Official room-return ready-state regression passed.'
