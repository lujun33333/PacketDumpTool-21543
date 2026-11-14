$ErrorActionPreference = 'Stop'

$serverDir = Split-Path $PSScriptRoot -Parent
$roomPath = Join-Path $serverDir 'Room.cpp'
$roomHeaderPath = Join-Path $serverDir 'Room.h'

$room = Get-Content -LiteralPath $roomPath -Raw
$roomHeader = Get-Content -LiteralPath $roomHeaderPath -Raw

function Get-FunctionSection([string]$source, [string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [StringComparison]::Ordinal)
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($start -lt 0 -or $end -le $start) {
        throw "matchmaking source section not found: $startMarker"
    }
    return $source.Substring($start, $end - $start)
}

if ($roomHeader -notmatch 'ROOM_SEAT_NORMAL_MAX\s*=\s*6\s*;') {
    throw 'border match room capacity must remain exactly 6 players'
}

$teamBuilder = Get-FunctionSection $room `
    'static bool TryBuildBorderTeamMixedRoster_NoLock(' `
    'static bool TryBuildBorderSingleRoster_NoLock('
$singleBuilder = Get-FunctionSection $room `
    'static bool TryBuildBorderSingleRoster_NoLock(' `
    'static bool TryBuildBorderMatchRoster_NoLock('

if ($teamBuilder -match '\btargetPlayers\b') {
    throw 'team matchmaking still treats the configured minimum as an exact target'
}
if ($teamBuilder -notmatch 'outRoster\.size\(\)\s*>=\s*ROOM_SEAT_NORMAL_MAX') {
    throw 'team matchmaking does not fill available solo players up to the 6-player room cap'
}
if ($teamBuilder -notmatch 'outRoster\.size\(\)\s*>=\s*requiredPlayers') {
    throw 'team matchmaking must use borderset.number only as the minimum start threshold'
}

if ($singleBuilder -notmatch 'outRoster\.size\(\)\s*>=\s*ROOM_SEAT_NORMAL_MAX') {
    throw 'solo matchmaking still stops at the configured minimum instead of filling up to 6 players'
}
if ($singleBuilder -notmatch 'outRoster\.size\(\)\s*<\s*requiredPlayers') {
    throw 'solo matchmaking must retain borderset.number as the minimum start threshold'
}

Write-Host 'BorderMatchDynamicRosterRegression passed: current queue population fills 2..6 slots, and team rooms can be completed by solo players without splitting the team.'
