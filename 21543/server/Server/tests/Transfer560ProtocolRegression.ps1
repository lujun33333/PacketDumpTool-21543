param(
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$OfficialTrace = ''
)

$ErrorActionPreference = 'Stop'

$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$messageHeaderPath = Join-Path $serverDir 'Message.h'
$messageCppPath = Join-Path $serverDir 'Message.cpp'
$gameServerCppPath = Join-Path $serverDir 'GameServer.cpp'
$luckyMatchCppPath = Join-Path $serverDir 'LuckyMatch.cpp'
$failures = [System.Collections.Generic.List[string]]::new()

if ([string]::IsNullOrWhiteSpace($OfficialTrace)) {
    $captureRoot = 'F:\PacketDumpTool\PacketDumpTool\x64\Release'
    $captureDirectory = Get-ChildItem -LiteralPath $captureRoot -Directory -Filter 'packet_dumps_15588*' |
        Select-Object -First 1
    if ($null -ne $captureDirectory) {
        $OfficialTrace = Join-Path $captureDirectory.FullName 'network_trace_15588.log'
    }
}

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Read-NetworkUInt16([byte[]]$bytes, [int]$offset) {
    return ([int]$bytes[$offset] -shl 8) -bor [int]$bytes[$offset + 1]
}

function Read-NetworkInt32([byte[]]$bytes, [int]$offset) {
    $value = ([uint32]$bytes[$offset] -shl 24) -bor
        ([uint32]$bytes[$offset + 1] -shl 16) -bor
        ([uint32]$bytes[$offset + 2] -shl 8) -bor
        [uint32]$bytes[$offset + 3]
    return [BitConverter]::ToInt32([BitConverter]::GetBytes($value), 0)
}

# Captured official 21487 border/ranked Msg560 plaintext prefix. This is evidence
# used by the regression test only; production code must learn CSHEAD::Ver from
# the client.
$officialSentinelPrefixHex = '0000005F53EFFFFFFFFFFFFF3E4E8D60000002300002FFFFFFFF0100FFFFFFFF003F'
[byte[]]$officialSentinelPrefix = for ($i = 0; $i -lt $officialSentinelPrefixHex.Length; $i += 2) {
    [Convert]::ToByte($officialSentinelPrefixHex.Substring($i, 2), 16)
}
if ((Read-NetworkUInt16 $officialSentinelPrefix 4) -ne 21487 -or
    (Read-NetworkUInt16 $officialSentinelPrefix 6) -ne 0xFFFF -or
    (Read-NetworkInt32 $officialSentinelPrefix 8) -ne -1 -or
    $officialSentinelPrefix[16] -ne 0 -or $officialSentinelPrefix[17] -ne 0 -or
    (Read-NetworkUInt16 $officialSentinelPrefix 18) -ne 560 -or
    (Read-NetworkUInt16 $officialSentinelPrefix 20) -ne 2 -or
    (Read-NetworkInt32 $officialSentinelPrefix 22) -ne -1 -or
    $officialSentinelPrefix[26] -ne 1 -or $officialSentinelPrefix[27] -ne 0 -or
    (Read-NetworkUInt16 $officialSentinelPrefix 28) -ne 0xFFFF -or
    (Read-NetworkUInt16 $officialSentinelPrefix 30) -ne 0xFFFF) {
    Add-Failure 'captured border/ranked Msg560 fixture no longer parses as Ver=21487, DialogID=-1, MsgSeq=-1, BodyFlag=0, OptLength=0, route=-1/-1'
}

# Captured official 21487 normal-room Msg153 -> Msg560 plaintext prefix. The
# packet carries payload Ver=0/Seq=0; unlike border/ranked, its wire route is
# DialogID=0, SrcID=the source PlayerID, DstID=0, and MsgSeq mirrors the
# source-side inbound CSHEAD sequence.
$officialNormalPrefixHex = '0000006753EF0000FFFFFFFF3D3D1BBE00000230000200003B2A010001D100000047'
[byte[]]$officialNormalPrefix = for ($i = 0; $i -lt $officialNormalPrefixHex.Length; $i += 2) {
    [Convert]::ToByte($officialNormalPrefixHex.Substring($i, 2), 16)
}
if ((Read-NetworkUInt16 $officialNormalPrefix 4) -ne 21487 -or
    (Read-NetworkUInt16 $officialNormalPrefix 6) -ne 0 -or
    (Read-NetworkInt32 $officialNormalPrefix 8) -ne -1 -or
    $officialNormalPrefix[16] -ne 0 -or $officialNormalPrefix[17] -ne 0 -or
    (Read-NetworkUInt16 $officialNormalPrefix 18) -ne 560 -or
    (Read-NetworkUInt16 $officialNormalPrefix 20) -ne 2 -or
    (Read-NetworkInt32 $officialNormalPrefix 22) -ne 15146 -or
    $officialNormalPrefix[26] -ne 1 -or $officialNormalPrefix[27] -ne 0 -or
    (Read-NetworkUInt16 $officialNormalPrefix 28) -ne 465 -or
    (Read-NetworkUInt16 $officialNormalPrefix 30) -ne 0) {
    Add-Failure 'captured normal Msg153->560 fixture no longer parses as Ver=21487, DialogID=0, MsgSeq=15146, BodyFlag=0, SrcID=465, DstID=0'
}

if (-not (Test-Path -LiteralPath $OfficialTrace)) {
    throw "official network trace not found: $OfficialTrace"
}

$official560 = @(Select-String -LiteralPath $OfficialTrace -Pattern 'event=unpack msg=560 ')
if ($official560.Count -lt 1) {
    Add-Failure 'official trace contains no decoded Msg560 samples'
} else {
    $nonOfficialRoute = @($official560 | Where-Object {
        $_.Line -notmatch 'type=2 seq=-1 srcFe=1 dstFe=0 srcId=-1 dstId=-1'
    })
    if ($nonOfficialRoute.Count -ne 0) {
        Add-Failure "official Msg560 route invariant changed: $($nonOfficialRoute.Count)/$($official560.Count) samples differ"
    }

    # Msg560 is sent on the official plaintext low-latency path. The raw packet
    # exposes MsgID=560 at CSHEAD(18) + MSGHEAD offset, and for an adjacent
    # inner-dispatch sample the wire size is exactly 18 + 14 + BodyLen.
    $opaque560 = @($official560 | Where-Object { $_.Line -notmatch 'packetMsg=560' })
    if ($opaque560.Count -ne 0) {
        Add-Failure "official Msg560 is no longer directly readable from the raw packet: $($opaque560.Count)/$($official560.Count) samples differ"
    }

    $previousInnerTick = -1L
    $previousInnerBodyLen = -1
    $plainFrameSamples = 0
    $plainFrameViolations = 0
    foreach ($line in [System.IO.File]::ReadLines($OfficialTrace)) {
        if ($line -match '^\[(\d+)\].*event=inner_dispatch msg=560 len=(\d+) ') {
            $previousInnerTick = [long]$Matches[1]
            $previousInnerBodyLen = [int]$Matches[2]
            continue
        }
        if ($line -match '^\[(\d+)\].*event=unpack msg=560 .*rawLen=(\d+) ') {
            $unpackTick = [long]$Matches[1]
            $rawLength = [int]$Matches[2]
            if ($unpackTick -eq $previousInnerTick -and $previousInnerBodyLen -ge 0) {
                $plainFrameSamples++
                if ($rawLength -ne (18 + 14 + $previousInnerBodyLen)) {
                    $plainFrameViolations++
                }
            }
            $previousInnerTick = -1L
            $previousInnerBodyLen = -1
        }
    }
    if ($plainFrameSamples -lt 1) {
        Add-Failure 'official trace contains no pairable Msg560 raw/body length samples'
    } elseif ($plainFrameViolations -ne 0) {
        Add-Failure "official Msg560 plaintext framing invariant changed: $plainFrameViolations/$plainFrameSamples samples differ"
    }
}

$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$messageSource = Get-Content -LiteralPath $messageCppPath -Raw
$gameSource = Get-Content -LiteralPath $gameServerCppPath -Raw
$luckySource = Get-Content -LiteralPath $luckyMatchCppPath -Raw
$requestStart = $gameSource.IndexOf('void RequestTransferByTCP(', [StringComparison]::Ordinal)
$requestEnd = $gameSource.IndexOf('void NotifyAddPropBySkillStone(', $requestStart, [StringComparison]::Ordinal)
if ($requestStart -lt 0 -or $requestEnd -lt 0) {
    Add-Failure 'Msg153/560 source section not found'
} else {
    $relay = $gameSource.Substring($requestStart, $requestEnd - $requestStart)
    $requiredRelayFragments = @(
        'const bool useNormalRaceWire =',
        'Client->BorderRoomID == 0',
        '!Room->HasCustomScheme',
        'Room->SpeFlag != 9',
        '!Room->LuckyMatchInternalRoom',
        'Client->TaskID == 0',
        'TryGetCurrentInboundProtocolSeq(Client, inboundProtocolSeq)',
        'wireSrcID = static_cast<short>(srcConnPlayerID);',
        'wireDstID = static_cast<short>(0);',
        'protocolDialogID = static_cast<short>(0);',
        'wireMsgSeq = inboundProtocolSeq;',
        'SendToClientWithRawWireRouteIDsAndDialog('
    )
    foreach ($fragment in $requiredRelayFragments) {
        if (-not $relay.Contains($fragment)) {
            Add-Failure "Msg153/560 mode-aware route is missing: $fragment"
        }
    }
    if ($relay -notmatch 'SendToClientWithRawWireRouteIDsAndDialog\(\s*RoomClient\s*,\s*560\s*,\s*relayBody\.data\(\)\s*,\s*static_cast<DWORD>\(relayBody\.size\(\)\)\s*,\s*wireSrcID\s*,\s*FE_PLAYER\s*,\s*wireDstID\s*,\s*protocolDialogID\s*,\s*wireMsgSeq\s*,\s*Notify\s*,\s*false\s*\)') {
        Add-Failure 'Msg560 does not send the selected normal/sentinel raw route and inbound-derived MsgSeq'
    }
    if ($relay -match 'SendToClientWithWireDstID\(\s*RoomClient\s*,\s*560') {
        Add-Failure 'Msg560 still normalizes official raw route IDs'
    }
    if ($relay -match 'NotifyTranferByTCPMC\(') {
        Add-Failure 'per-recipient Msg560 compatibility builder remains in the relay path'
    }
}

if ($messageHeader -notmatch 'USHORT\s+ProtocolVersion\s*=\s*0') {
    Add-Failure 'ClientNode does not retain the validated inbound CSHEAD protocol version'
}
if ($messageSource -notmatch 'inboundProtocolVersion\s*=\s*ntohs[\s\S]*?Client->ProtocolVersion\s*=\s*inboundProtocolVersion') {
    Add-Failure 'OnFullReceive does not learn CSHEAD::Ver from validated inbound traffic'
}
if ($messageSource -match 'htons\(\s*(116|21487)\s*\)' -or $luckySource -match 'htons\(\s*(116|21487)\s*\)') {
    Add-Failure 'an outbound game-server path still hardcodes CSHEAD::Ver'
}
if ($messageSource -notmatch 'ProtocolHead->Ver\s*=\s*static_cast<short>\(htons\(Client->ProtocolVersion\)\)' -or
    $luckySource -notmatch 'proto->Ver\s*=\s*static_cast<short>\(htons\(client->ProtocolVersion\)\)') {
    Add-Failure 'outbound game-server paths do not echo the connection protocol version'
}

if ($messageHeader -notmatch 'BOOL\s+SendToClientWithRawWireRouteIDsAndDialog\([^;]*short\s+ProtocolDialogID[^;]*int\s+WireMsgSeq[^;]*\);') {
    Add-Failure 'raw inner-route sender declaration cannot set outer DialogID and MsgSeq independently'
}
if ($messageHeader -match 'SendToClientViaTransport|RankedPVPTransport|GetRankedPVPTransport' -or
    $messageSource -match 'SendToClientViaTransport|RankedPVPTransport|GetRankedPVPTransport' -or
    $gameSource -match 'SendToClientViaTransport|RankedPVPTransport|GetRankedPVPTransport') {
    Add-Failure 'dedicated ranked PVP transport declarations or calls remain'
}

$rawHelperStart = $messageSource.IndexOf('BOOL SendToClientWithRawWireRouteIDsAndDialog(', [StringComparison]::Ordinal)
$rawHelperEnd = if ($rawHelperStart -ge 0) {
    $messageSource.IndexOf('static BOOL SendToClientWithBodyLenOverride(', $rawHelperStart, [StringComparison]::Ordinal)
} else {
    -1
}
if ($rawHelperStart -lt 0 -or $rawHelperEnd -lt 0) {
    Add-Failure 'raw inner-route sender implementation is missing'
} else {
    $rawHelper = $messageSource.Substring($rawHelperStart, $rawHelperEnd - $rawHelperStart)
    if ($rawHelper -notmatch 'SendToClientInternal\(\s*Client\s*,\s*MsgID\s*,\s*Data\s*,\s*Length\s*,\s*WireSrcID\s*,\s*SrcFE\s*,\s*WireDstID\s*,\s*WireDstID\s*,\s*MsgType\s*,\s*Encrypt\s*,\s*-1\s*,\s*WireMsgSeq\s*,\s*true\s*,\s*ProtocolDialogID\s*\)') {
        Add-Failure 'raw inner-route sender does not preserve wire IDs, DialogID, and MsgSeq independently'
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host "Transfer560ProtocolRegression passed: sentinelSamples=$($official560.Count), plainFrameSamples=$plainFrameSamples, normal route=SrcPlayerID/0 DialogID=0, border/ranked route=-1/-1 DialogID=-1, CSHEAD::Ver=connection version"
