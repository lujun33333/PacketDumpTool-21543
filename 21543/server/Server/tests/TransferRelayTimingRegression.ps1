param(
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$OfficialBorder548 = ''
)

$ErrorActionPreference = 'Stop'
$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$game = Get-Content -LiteralPath (Join-Path $serverDir 'GameServer.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $serverDir 'Message.h') -Raw
$room = Get-Content -LiteralPath (Join-Path $serverDir 'Room.cpp') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Read-NetworkUInt16([byte[]]$bytes, [int]$offset) {
    return ([int]$bytes[$offset] -shl 8) -bor [int]$bytes[$offset + 1]
}

if ([string]::IsNullOrWhiteSpace($OfficialBorder548)) {
    $captureRoot = 'F:\PacketDumpTool\PacketDumpTool\x64\Release'
    $captureDirectory = Get-ChildItem -LiteralPath $captureRoot -Directory -Filter 'packet_dumps_15588*' |
        Select-Object -First 1
    if ($null -ne $captureDirectory) {
        $OfficialBorder548 = Join-Path $captureDirectory.FullName 'payloads\network_bin_15588_32769_inner_dispatch_body_msg548_len9274.bin'
    }
}

# The official border roster deliberately publishes PlayerID=0 for every
# participant. This is the precondition under which the client emits its first
# Msg153, before any peer Msg560 has arrived and could teach it an assigned ID.
if (-not (Test-Path -LiteralPath $OfficialBorder548)) {
    Add-Failure "official border Msg548 fixture not found: $OfficialBorder548"
} else {
    [byte[]]$official548 = [System.IO.File]::ReadAllBytes($OfficialBorder548)
    if ($official548.Length -lt 11 -or
        (Read-NetworkUInt16 $official548 0) -ne ($official548.Length - 15) -or
        $official548[10] -ne 6) {
        Add-Failure 'official border Msg548 fixture framing/player count changed'
    } else {
        $visibleOffset = 11
        for ($playerIndex = 0; $playerIndex -lt $official548[10]; $playerIndex++) {
            if ($visibleOffset + 15 -gt $official548.Length) {
                Add-Failure "official border Msg548 visible row $playerIndex is truncated"
                break
            }
            $visibleLength = Read-NetworkUInt16 $official548 $visibleOffset
            if ($visibleLength -lt 15 -or $visibleOffset + $visibleLength -gt $official548.Length) {
                Add-Failure "official border Msg548 visible row $playerIndex has invalid length $visibleLength"
                break
            }
            $officialPlayerID = Read-NetworkUInt16 $official548 ($visibleOffset + 12)
            if ($officialPlayerID -ne 0) {
                Add-Failure "official border Msg548 player $playerIndex no longer uses unresolved PlayerID=0"
            }
            $visibleOffset += $visibleLength
        }
    }
}

$borderRosterStart = $room.IndexOf('bool NotifyBorderPlayerInfo(', [StringComparison]::Ordinal)
$borderRosterEnd = $room.IndexOf('static bool NotifyRankedMatchBanMapInfo(', $borderRosterStart, [StringComparison]::Ordinal)
if ($borderRosterStart -lt 0 -or $borderRosterEnd -lt 0) {
    Add-Failure 'server Msg548 serializer section not found'
} else {
    $borderRoster = $room.Substring($borderRosterStart, $borderRosterEnd - $borderRosterStart)
    if ($borderRoster -notmatch 'WriteVisbleInfo1\(RoomClient,\s*p,\s*RoomClient->Uin,\s*0,\s*0\)') {
        Add-Failure 'server Msg548 no longer preserves the official unresolved PlayerID=0 roster'
    }
    if ($borderRoster -notmatch 'SendToClient\(\s*Client,\s*548,[\s\S]*?BorderRoom->RaceGameID\),\s*FE_GAMESVRD,[\s\S]*?GetClientWirePlayerHeaderID\(Client\),\s*Notify\)') {
        Add-Failure 'ranked Msg548 is not sent on the authenticated main connection with the official RaceGameID route'
    }
}

$requestStart = $game.IndexOf('void RequestTransferByTCP(', [StringComparison]::Ordinal)
$requestEnd = $game.IndexOf('void NotifyAddPropBySkillStone(', $requestStart, [StringComparison]::Ordinal)
if ($requestStart -lt 0 -or $requestEnd -lt 0) {
    Add-Failure 'Msg153/560 relay section not found'
} else {
    $relay = $game.Substring($requestStart, $requestEnd - $requestStart)
    if ($relay -notmatch 'BodyLen\s*>=\s*sizeof\(USHORT\).+BodyLen\s*-\s*sizeof\(USHORT\)' -or
        $relay -notmatch 'BYTE\*\s+end\s*=\s*Body\s*\+\s*payloadLen') {
        Add-Failure 'Msg153 parser does not use the exact available payload range'
    }
    # Live 2026-07-22 two-client capture: payload=81, bytes before movement=24,
    # BuffLen=56, leaving one request-only extension byte. Msg560 carries only
    # the length-delimited movement buffer, so the parser must reject truncation
    # but must not require the extension area to be empty.
    if ((81 - 24 - 56) -ne 1) {
        Add-Failure 'captured Msg153 extension fixture is inconsistent'
    }
    if ($relay -notmatch 'remaining\(\)\s*<\s*BuffLen') {
        Add-Failure 'Msg153 does not reject an under-declared movement buffer'
    }
    if ($relay -match 'remaining\(\)\s*!=\s*BuffLen') {
        Add-Failure 'Msg153 incorrectly rejects the request-only extension bytes after BuffLen'
    }
    if ($relay -notmatch 'packetUin\s*==\s*0\s*\|\|\s*packetUin\s*!=\s*Client->Uin') {
        Add-Failure 'Msg153 does not strictly bind packet Uin to the authenticated connection'
    }
    if ($relay -match 'Client->Uin\s*\?\s*Client->Uin\s*:\s*packetUin' -or
        $relay -match 'srcConnPlayerID\s*=\s*GetClientWirePlayerID16') {
        Add-Failure 'Msg153 source identity fallback remains'
    }
    if ($relay -match 'forwardedUins|alreadyForwarded') {
        Add-Failure 'Msg153 still silently deduplicates malformed destinations'
    }
    if ($relay -notmatch 'info\.Uin\s*==\s*0\s*\|\|\s*info\.PlayerID\s*==\s*0xFFFF' -or
        $relay -notmatch 'existing\.Uin\s*==\s*info\.Uin[\s\S]*?return') {
        Add-Failure 'Msg153 does not reject an invalid Uin/0xFFFF PlayerID or duplicate destination Uin'
    }
    # Compatibility A/B: the 21043 relay parsed PlayerID but selected recipients
    # only by destination Uin. Keep PlayerID validation for the wire sentinel, but
    # do not let stale/duplicated assigned IDs suppress an otherwise valid Uin.
    if ($relay -match 'info\.PlayerID\s*!=\s*0[\s\S]*?existing\.PlayerID\s*==\s*info\.PlayerID') {
        Add-Failure 'Msg153 still rejects a destination list because assigned PlayerIDs are duplicated'
    }
    if ($relay -notmatch 'RoomClient->Uin\s*!=\s*dstInfo\.Uin') {
        Add-Failure 'Msg153 does not select the relay recipient by destination Uin'
    }
    if ($relay -match 'receiverPlayerID' -or
        $relay -match 'dstInfo\.PlayerID\s*!=\s*0\s*&&') {
        Add-Failure 'Msg153 still gates Uin routing on the destination PlayerID'
    }
    if ($relay -notmatch 'const bool useNormalRaceWire\s*=' -or
        $relay -notmatch 'Client->BorderRoomID\s*==\s*0' -or
        $relay -notmatch '!Room->HasCustomScheme' -or
        $relay -notmatch 'Room->SpeFlag\s*!=\s*9') {
        Add-Failure 'Msg153 relay does not separate captured normal-room and sentinel wire profiles'
    }
    if ($relay -notmatch 'const std::shared_ptr<RoomNode> Room = GetActiveRaceRoom\(Client\)' -or
        $relay -notmatch 'SendToClientWithRawWireRouteIDsAndDialog\(\s*RoomClient,\s*560') {
        Add-Failure 'Msg153/560 does not use the authenticated main session and recipient connection'
    }
    if ($relay -match 'useRankedPVPTransport|GetRankedPVPTransport|SendToClientViaTransport') {
        Add-Failure 'dedicated ranked PVP transport remains in the Msg153/560 relay path'
    }
    if ($relay -match 'GetBorderRoom\(|GetRoom\(Client->RoomID\)|Client->CurrentRoom') {
        Add-Failure 'Msg153 contains a second room lookup or raw CurrentRoom fallback'
    }

    $buildIndex = $relay.IndexOf('std::vector<BYTE> relayBody', [StringComparison]::Ordinal)
    $loopIndex = $relay.IndexOf('for (const DstInfo& dstInfo', [StringComparison]::Ordinal)
    if ($buildIndex -lt 0 -or $loopIndex -lt 0 -or $buildIndex -gt $loopIndex) {
        Add-Failure 'Msg560 body is not built once before recipient iteration'
    }

    if ($relay -match 'GetPendingDataLength|QQS_FlowFileLog|Transfer(Max|Last|Window|Packet|Interval|Forward|Send|Seq|Out)|std::chrono') {
        Add-Failure 'blocking queue probes or relay telemetry remain in the per-packet hot path'
    }
}

$enterStart = $room.IndexOf('void RequestEnterMatchRoom(', [StringComparison]::Ordinal)
$enterEnd = $room.IndexOf('void RequestAddToTeamMatchQueue(', $enterStart, [StringComparison]::Ordinal)
if ($enterStart -lt 0 -or $enterEnd -lt 0) {
    Add-Failure 'match-room entry section not found'
} else {
    $matchEntry = $room.Substring($enterStart, $enterEnd - $enterStart)
    if ($matchEntry -match 'NotifyMuchPlayerInfo\(Client\)') {
        Add-Failure 'border/ranked match entry still injects the non-official Msg1718 PlayerID roster'
    }
}

$customLoadingStart = $room.IndexOf('void WINAPI OnCustomEnterLoading(', [StringComparison]::Ordinal)
$customLoadingEnd = $room.IndexOf('void RequestStartGame(', $customLoadingStart, [StringComparison]::Ordinal)
if ($customLoadingStart -lt 0 -or $customLoadingEnd -lt 0 -or
    $room.Substring($customLoadingStart, $customLoadingEnd - $customLoadingStart) -notmatch 'NotifyMuchPlayerInfo\(RoomClient\)') {
    Add-Failure 'custom-room Msg1718 roster path was removed instead of isolating official match flow'
}

$activeRoomStart = $room.IndexOf('std::shared_ptr<RoomNode> GetActiveRaceRoom(', [StringComparison]::Ordinal)
$activeRoomEnd = $room.IndexOf('void PrintRoom(', $activeRoomStart, [StringComparison]::Ordinal)
if ($activeRoomStart -lt 0 -or $activeRoomEnd -lt 0) {
    Add-Failure 'authoritative active race room resolver is missing'
} else {
    $activeRoom = $room.Substring($activeRoomStart, $activeRoomEnd - $activeRoomStart)
    $borderIndex = $activeRoom.IndexOf('Client->BorderRoomID != 0', [StringComparison]::Ordinal)
    $normalIndex = $activeRoom.IndexOf('else if (Client->RoomID != 0)', [StringComparison]::Ordinal)
    if ($borderIndex -lt 0 -or $normalIndex -lt 0 -or $borderIndex -gt $normalIndex) {
        Add-Failure 'active room precedence is not BorderRoomID first, then RoomID'
    }
    if ($activeRoom -notmatch 'Client->CurrentRoom\s*!=\s*Room\.get\(\)') {
        Add-Failure 'active room resolver does not verify CurrentRoom identity'
    }
    if ($activeRoom -match 'if\s*\(\s*!Room\s*\)[\s\S]*?Get(Border)?Room') {
        Add-Failure 'active room resolver falls back across room namespaces'
    }
}

if ($header -match 'Transfer(WindowStartMs|LastRecvMs|IntervalTotalMs|MaxHandlerUs|LastSeq|PacketCount|IntervalSamples|MinIntervalMs|MaxIntervalMs|ForwardCount|SendFailCount|SeqGapCount|OutOfOrderCount|MaxPendingBytes|LastSeqValid)' -or
    $header -match 'NotifyTranferByTCPMC') {
    Add-Failure 'obsolete Msg153/560 telemetry or compatibility declarations remain in ClientNode'
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'TransferRelayTimingRegression passed: official zero-ID fixture, Uin-only recipient routing, authoritative room, strict bounds, one relay build, captured normal/sentinel wire split'
