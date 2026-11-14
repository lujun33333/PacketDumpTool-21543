param(
    [string]$OfficialPayloadRoot = ''
)

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

function Assert-BytePrefix {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][byte[]]$Expected
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "official fixture not found: $Path"
    }
    [byte[]]$actual = [IO.File]::ReadAllBytes($Path)
    if ($actual.Length -lt $Expected.Length) {
        throw "official fixture is shorter than expected: $Path"
    }
    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($actual[$i] -ne $Expected[$i]) {
            throw ('official fixture mismatch: {0} offset=0x{1:X} expected=0x{2:X2} actual=0x{3:X2}' -f
                $Path, $i, $Expected[$i], $actual[$i])
        }
    }
}

$send514 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static bool SendLeaveGame514(' `
    -End 'struct BorderTeamReturnEntry'
$request424 = Get-SourceRange `
    -Text $gameServer `
    -Begin 'void RequestLeaveGame(' `
    -End 'void ResponseLeaveGame('
$customRequest424 = Get-SourceRange `
    -Text $request424 `
    -Begin 'if (customReturnRoom)' `
    -End '// 官服普通竞速房 ESC 样本：'
$officialRequest424 = Get-SourceRange `
    -Text $request424 `
    -Begin '// 官服普通竞速房 ESC 样本：' `
    -End 'NotifyGameOver(Client'
$request105 = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestLeaveRoom(' `
    -End 'void LeaveRoom('
$official105 = Get-SourceRange `
    -Text $room `
    -Begin 'static bool HandleOfficialEscLeaveRoom105(' `
    -End 'void RequestLeaveRoom('
$normalize12 = Get-SourceRange `
    -Text $room `
    -Begin 'static void NormalizeTwelvePlayerRoomWireState(' `
    -End 'static void NormalizeNormalCustomRoomWireState('
$seatState107 = Get-SourceRange `
    -Text $room `
    -Begin 'void RequestChangeSeatState(' `
    -End 'void ResponseChangeSeatState('
$response104 = Get-SourceRange `
    -Text $room `
    -Begin 'void ResponseEnterRoom(' `
    -End 'void NotifyEnterRoom('
$gameBeginRoster = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static char BuildGameBeginRoster(' `
    -End 'static bool ValidateFrozenRaceStartFor509('

if ($send514 -match 'NormalizeTwelvePlayerReturnRoom|Normalize.*Room|SeatNum|SeatState') {
    throw '514 serializer still mutates room/seat state'
}
if ($send514 -notmatch 'Write8\(p,\s*returnHall\)[\s\S]*?Write8\(p,\s*waitEnterRoom\)[\s\S]*?Write16\(p,\s*LeaveGameType\)[\s\S]*?Write8\(p,\s*1\)[\s\S]*?Write32\(p,\s*mapParam\)') {
    throw '514 serializer field order no longer matches the captured 14-byte body'
}
if ($customRequest424 -notmatch 'leave-game-(?:12p-)?custom-single-room') {
    throw 'custom single ESC no longer selects the direct room-return transaction'
}
if ($customRequest424 -notmatch 'SendLeaveGameEscToRoom514\(Client,\s*Room,\s*LeaveGameType,\s*1,\s*reason\)[\s\S]*?ReturnClientToOriginalRoom\(Client,\s*Room,\s*reason,\s*true\)') {
    throw 'custom single ESC no longer emits 514(wait=1) before the 104 room rebuild'
}
if ($customRequest424 -notmatch 'std::unique_lock<std::shared_mutex>\s+roomStateLock\(Room->mutex\)') {
    throw 'custom single ESC room reset is no longer serialized with the room timer'
}
if ($customRequest424 -notmatch 'leave-game-(?:12p-)?custom-multi-esc-hall' -or
    $customRequest424 -notmatch 'SendLeaveGameToHall514\(Client,\s*Room,\s*LeaveGameType,\s*reason\)[\s\S]*?LeaveRoom\(Client,\s*0\)') {
    throw 'custom multiplayer ESC no longer returns to hall and commits the room leave'
}
if ($customRequest424 -match 'BeginOfficialEscFlow|ResponseLeaveGame\(Client\)') {
    throw 'custom ESC is incorrectly routed through the ordinary 105/102/103 state machine'
}
if ($officialRequest424 -match 'SendLeaveGameToHall514|ReturnLeaveGameToRoom|ReturnClientToOriginalRoom') {
    throw '424 handler still uses a direct hall/104 return shortcut'
}
if ($officialRequest424 -notmatch 'BeginOfficialEscFlow') {
    throw '424 handler does not arm the official ESC state'
}
if ($officialRequest424 -match '\bLeaveRoom\s*\(') {
    throw '424 handler still commits room leave before client 105'
}
if ($request105 -notmatch 'HandleOfficialEscLeaveRoom105') {
    throw '105 handler is not driven by the official ESC transition'
}
if ($request105 -match 'LEAVE-ROOM-DUP105-SKIP') {
    throw '105 handler still silently drops an official duplicate request'
}
if ($official105 -match 'TimeoutMs|WindowMs') {
    throw 'official 105 state machine still depends on an uncaptured timing heuristic'
}
if ($message -notmatch 'case\s+103:[\s\S]*?CompleteOfficialEscFlow\(Client,\s*103\)') {
    throw 'client 103 does not complete the official single ESC state'
}
if ($message -notmatch 'case\s+102:[\s\S]*?CompleteOfficialEscFlow\(Client,\s*102\)') {
    throw 'client 102 does not complete the official multiplayer ESC state'
}
if ($messageHeader -notmatch 'BYTE\s+OfficialEscFlowKind' -or
    $messageHeader -notmatch 'BYTE\s+OfficialEscPhase') {
    throw 'ClientNode does not retain official ESC protocol state'
}
if ($officialRequest424 -notmatch 'SendLeaveGameEscToRoom514\(Client,\s*Room,\s*LeaveGameType,\s*0') {
    throw '424 handler does not emit the captured ReturnHall=0/WaitEnterRoom=0/map 514'
}
if ($officialRequest424 -notmatch 'if\s*\(singleRace\)[\s\S]*?ResponseLeaveGame\(Client\)') {
    throw 'single official flow does not send the captured 424 response'
}
if ([regex]::Matches($officialRequest424, 'ResponseLeaveGame\(Client\)').Count -ne 1) {
    throw 'ordinary official ESC block must contain exactly one single-only 424 response'
}
if ($room -notmatch 'BuildOfficialClearedLeaveRoom105Response') {
    throw 'duplicate 105 does not have the captured cleared-context response'
}
if ($room -notmatch 'BuildOfficialClearedLeaveRoom105Response[\s\S]*?Write16\(p,\s*LeaveType\)[\s\S]*?Write16\(p,\s*0\)[\s\S]*?Write16\(p,\s*0\)[\s\S]*?Write8\(p,\s*0\)') {
    throw 'duplicate 105 response is not the captured Base=0/Sub=0/GVoice=0 shape'
}
if ($room -notmatch 'OfficialEscPhase\s*=\s*QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT[\s\S]*?LeaveRoom\(Client,\s*LeaveType\)') {
    throw 'first client 105 does not commit the deferred room leave'
}
if ($gameServer -match 'static void ReturnLeaveGameToRoom(?:Ex)?\(') {
    throw 'obsolete proactive ESC room-return helper remains in source'
}
if ($room -match 'ESC-OFFICIAL-QUICKBUG') {
    throw 'server-side quickbug shortcut is still mislabeled as the official client ESC flow'
}
if ($normalize12 -match 'Room->SeatNum\s*=\s*ROOM_SEAT_12_MAX' -or
    $normalize12 -match 'Room->SeatState\[[^\]]+\]\s*=\s*1') {
    throw '12P wire normalization still reopens locked seats or forces SeatNum=12'
}
if ($seatState107 -match 'Room->SeatNum\s*=\s*ROOM_SEAT_12_MAX') {
    throw '107 seat-state handler still discards the official open-seat count'
}
if ($room -notmatch 'return\s+GetConfiguredRoomSeatNum\(Room\);') {
    throw '12P 102/103/104 does not emit the configured open-seat count'
}
if ($response104 -notmatch '!isTwelveRoom\s*&&\s*enterWireSeatNum') {
    throw '12P 104 still infers locked indexes from SeatNum instead of explicit SeatState'
}
if ($gameBeginRoster -notmatch 'Players\.assign\(seatLimit,\s*nullptr\)' -or
    $gameBeginRoster -notmatch 'currentPlayers\s*<\s*seatLimit') {
    throw '12P msg509 roster is no longer sized by the explicit 12-seat shell'
}

if ($OfficialPayloadRoot) {
    Assert-BytePrefix `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_5210_inner_dispatch_body_msg514_len26.bin') `
        -Expected ([byte[]](0x00,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x01,0x0B))
    Assert-BytePrefix `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_21506_inner_dispatch_body_msg514_len26.bin') `
        -Expected ([byte[]](0x00,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x01,0x74))
    Assert-BytePrefix `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_5278_inner_dispatch_body_msg105_len26.bin') `
        -Expected ([byte[]](0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x01))
    Assert-BytePrefix `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_5297_inner_dispatch_body_msg105_len26.bin') `
        -Expected ([byte[]](0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))
    Assert-BytePrefix `
        -Path (Join-Path $OfficialPayloadRoot 'network_bin_17072_21586_inner_dispatch_body_msg105_len26.bin') `
        -Expected ([byte[]](0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x01))

    $room104Path = Join-Path $OfficialPayloadRoot 'network_bin_17072_20767_inner_dispatch_body_msg104_len11842.bin'
    [byte[]]$room104 = [IO.File]::ReadAllBytes($room104Path)
    if ($room104[0x20] -ne 9) {
        throw 'official 12P fixture no longer has SeatNum=9'
    }
    [byte[]]$expectedSeatStatus = 2,2,2,2,2,2,2,2,1,0,0,0
    for ($i = 0; $i -lt $expectedSeatStatus.Length; $i++) {
        if ($room104[0x2B + $i] -ne $expectedSeatStatus[$i]) {
            throw "official 12P fixture seat status mismatch at index $i"
        }
    }

    $gameBegin509Path = Join-Path $OfficialPayloadRoot 'network_bin_17072_21350_special28346_after_full_msg509_len43775.bin'
    [byte[]]$gameBegin509 = [IO.File]::ReadAllBytes($gameBegin509Path)
    if ($gameBegin509[0x113] -ne 9) {
        throw 'official 12P fixture no longer has nine msg509 race players'
    }
    Write-Host 'Official packet fixtures passed.'
}

Write-Host 'Official ESC source audit passed.'
