$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$roomHeaderPath = Join-Path $root 'Room.h'
$roomCppPath = Join-Path $root 'Room.cpp'
$gameServerPath = Join-Path $root 'GameServer.cpp'
$messageHeaderPath = Join-Path $root 'Message.h'

$roomHeader = Get-Content -LiteralPath $roomHeaderPath -Raw
$roomCpp = Get-Content -LiteralPath $roomCppPath -Raw
$gameServer = Get-Content -LiteralPath $gameServerPath -Raw
$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-Section([string]$source, [string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [StringComparison]::Ordinal)
    if ($start -lt 0) { return '' }
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) { return '' }
    return $source.Substring($start, $end - $start)
}

foreach ($field in @(
    'BorderRaceStartRoster',
    'BorderRaceStartMask',
    'BorderRaceConnectedMask',
    'BorderRaceFinishedMask',
    'BorderRaceQuitMask',
    'BorderRaceSettlementPhase'
)) {
    if ($roomHeader -notmatch [regex]::Escape($field)) {
        $failures.Add("missing immutable border settlement state: $field")
    }
}

$capture = Get-Section $roomCpp 'bool CaptureRoomRaceStartSnapshot' 'static USHORT GetRoomListSvrID'
if ($capture -notmatch 'BorderRaceStartRoster\.push_back') {
    $failures.Add('border start roster is not captured by value at race start')
}
if ($capture -notmatch 'BorderRaceConnectedMask\s*=\s*mask') {
    $failures.Add('border connected-recipient mask is not initialized from the start snapshot')
}

$leave = Get-Section $roomCpp 'void LeaveBorderRoom' 'void ClearFinishedBorderRoom'
$handleIndex = $leave.IndexOf('HandlePlayerLeaveRace(Client, Room', [StringComparison]::Ordinal)
$resetIndex = $leave.IndexOf('RoomClient->TaskID = 0', [StringComparison]::Ordinal)
$removeIndex = $leave.IndexOf('Room->Player[i] = NULL', [StringComparison]::Ordinal)
if ($handleIndex -lt 0 -or $resetIndex -lt 0 -or $removeIndex -lt 0 -or
    $handleIndex -gt $resetIndex -or $handleIndex -gt $removeIndex) {
    $failures.Add('LeaveBorderRoom destroys race state before recording quit/DNF in the immutable snapshot')
}

if ($gameServer -notmatch 'BorderOfficialRaceShowTimeMs\s*=\s*3000') {
    $failures.Add('July official msg515 RaceShowTime=3000 constant is missing')
}
if ($gameServer -notmatch 'BorderOfficialCountDownToRaceShowMs\s*=\s*11000') {
    $failures.Add('July official msg512-to-msg515 observed 11000-ms phase is missing')
}
if ($gameServer -notmatch 'BorderOfficialAwardTimeMs\s*=\s*17000') {
    $failures.Add('July official msg542 AwardTime=17000 constant is missing')
}
if ($gameServer -notmatch 'RankedOfficialRaceShowTimeMs\s*=\s*6000') {
    $failures.Add('ranked official msg515 RaceShowTime=6000 constant is missing')
}
if ($gameServer -notmatch 'RankedOfficialAwardTimeMs\s*=\s*23000') {
    $failures.Add('ranked official msg513 AwardTime=23000 constant is missing')
}
if ($gameServer -notmatch 'BorderRaceScoreWireSize\s*=\s*181') {
    $failures.Add('msg542 RaceScore does not include the current-client m_uiWinStreak field')
}
if ($gameServer -notmatch 'BorderRaceScoreSlotCount\s*=\s*6') {
    $failures.Add('msg542 is not aligned to the current client fixed six-slot RaceScore decoder')
}

$raceShow = Get-Section $gameServer 'void WINAPI OnBorderRaceShow' 'void WINAPI OnBorderCountDown'
if ([string]::IsNullOrEmpty($raceShow)) {
    $failures.Add('border 515 RaceShow phase callback is missing')
} else {
    if ($raceShow -notmatch 'BuildBorderRaceConnectedRoster') {
        $failures.Add('border 515 is not restricted to connected start-roster recipients')
    }
    if ($raceShow -notmatch 'raceShowTime\s*=\s*Room->matchQueueType\s*==\s*0\s*\?\s*BorderOfficialRaceShowTimeMs\s*:\s*RankedOfficialRaceShowTimeMs') {
        $failures.Add('msg515 does not select the official 3000-ms border / 6000-ms ranked phase value')
    }
    if ($raceShow -notmatch 'NotifyRaceShow\s*\([^;]+raceShowTime\s*\)') {
        $failures.Add('msg515 does not serialize the mode-specific official phase value')
    }
    if ($raceShow -notmatch 'CreateRoomTimer\s*\(\s*Room\s*,\s*raceShowTime\s*,\s*OnBorderCountDown\s*\)') {
        $failures.Add('settlement is not scheduled after the mode-specific official 515 duration')
    }
}

$borderResult = Get-Section $gameServer 'void WINAPI OnBorderCountDown' 'void shuffle'
if ($borderResult -notmatch 'CreateRoomTimer\s*\(\s*Room\s*,\s*BorderOfficialAwardTimeMs\s*,\s*OnBorderCountDown2\s*\)') {
    $failures.Add('border msg514 return is not scheduled from the official 17000-ms AwardTime')
}

$finishPath = Get-Section $gameServer 'else if (Client->BorderRoomID != 0' 'else if (Client->TaskID == 0x06a5)'
if ($finishPath -notmatch 'CreateRoomTimer\s*\(\s*BorderRoom\s*,\s*BorderOfficialCountDownToRaceShowMs\s*,\s*OnBorderRaceShow\s*\)') {
    $failures.Add('first border finisher still skips msg515 and schedules msg542 directly')
}
if ($finishPath -notmatch 'UpdateBorderRaceSnapshotFromClient') {
    $failures.Add('border finish does not persist finish state into the start snapshot')
}

$resultWriter = Get-Section $gameServer 'void NotifyBorderRaceOver(std::shared_ptr<ClientNode>' 'void NotifyRaceOverAndNpc'
if ($resultWriter -notmatch 'BuildBorderRaceSettlementRoster') {
    $failures.Add('msg542 rows are still built only from mutable live room slots')
}
if ($resultWriter -notmatch 'Set16\s*\(\s*borderRow\s*,\s*static_cast<WORD>\(BorderRaceScoreWireSize\)\s*\)') {
    $failures.Add('msg542 populated RaceScore rows still advertise the legacy wire length')
}
if ($resultWriter -notmatch 'while\s*\(\s*settlementRowNum\s*<\s*BorderRaceScoreSlotCount\s*\)') {
    $failures.Add('msg542 does not emit all six fixed RaceScore slots')
}
if ($resultWriter -notmatch 'Write16\s*\(\s*p\s*,\s*BorderRaceEmptyScoreWireSize\s*\)') {
    $failures.Add('msg542 empty RaceScore slots are not encoded as a two-byte empty structure')
}
if ($resultWriter -notmatch 'Write32\s*\(\s*p\s*,\s*BorderOfficialAwardTimeMs\s*\)') {
    $failures.Add('msg542 AwardTime is not serialized after all six RaceScore slots')
}

if ($messageHeader -notmatch 'NotifyRaceShow\s*\(\s*std::shared_ptr<ClientNode>\s+Client\s*,\s*UINT\s+RaceShowTime') {
    $failures.Add('NotifyRaceShow cannot serialize an explicit border RaceShowTime')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'Official settlement regression passed: border 512 -> 515(3000) -> 542(17000) -> 514 and ranked 512 -> 515(6000) -> 513(23000) -> 514.'
