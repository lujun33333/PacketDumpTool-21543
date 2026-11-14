param(
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$ReleaseCaptureRoot = 'F:\PacketDumpTool\PacketDumpTool\x64\Release'
)

$ErrorActionPreference = 'Stop'
$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Find-CaptureDirectory([string]$pattern) {
    return Get-ChildItem -LiteralPath $ReleaseCaptureRoot -Directory -Filter $pattern |
        Select-Object -First 1
}

function Read-Expanded509([string]$capturePattern, [string]$fileName) {
    $capture = Find-CaptureDirectory $capturePattern
    if ($null -eq $capture) {
        throw "capture directory not found: $capturePattern"
    }
    $path = Join-Path (Join-Path $capture.FullName 'payloads') $fileName
    if (-not (Test-Path -LiteralPath $path)) {
        throw "expanded msg509 fixture not found: $path"
    }
    return [System.IO.File]::ReadAllBytes($path)
}

function Get-TrackView([byte[]]$bytes) {
    if ($bytes.Length -ne 43775) {
        throw "expanded msg509 length $($bytes.Length) != 43775"
    }
    $orders = [uint32[]]::new(20)
    $occupied = [System.Collections.Generic.List[int]]::new()
    $seen = [System.Collections.Generic.HashSet[uint32]]::new()
    for ($slot = 0; $slot -lt 20; ++$slot) {
        $uin = [BitConverter]::ToUInt32($bytes, 0x31 + $slot * 4)
        $orders[$slot] = $uin
        if ($uin -eq 0) {
            continue
        }
        $occupied.Add($slot)
        if ($uin -le 10000) {
            Add-Failure "official fixture has invalid TrackOrders[$slot]=$uin"
        }
        if (-not $seen.Add($uin)) {
            Add-Failure "official fixture has duplicate TrackOrders UIN=$uin"
        }
    }
    return [pscustomobject]@{
        CurrentPlayers = [int]$bytes[0x30]
        Orders = $orders
        Occupied = $occupied.ToArray()
        NonZeroCount = $seen.Count
    }
}

function Assert-TrackShape(
    [byte[]]$bytes,
    [int]$expectedPlayers,
    [int[]]$expectedOccupied,
    [string]$label
) {
    $view = Get-TrackView $bytes
    if ($view.CurrentPlayers -ne $expectedPlayers) {
        Add-Failure "$label CurrentPlayers=$($view.CurrentPlayers) expected=$expectedPlayers"
    }
    if ($view.NonZeroCount -ne $expectedPlayers) {
        Add-Failure "$label non-zero TrackOrders=$($view.NonZeroCount) expected=$expectedPlayers"
    }
    if (($view.Occupied -join ',') -ne ($expectedOccupied -join ',')) {
        Add-Failure "$label occupied=$($view.Occupied -join ',') expected=$($expectedOccupied -join ',')"
    }
}

$onePlayer = Read-Expanded509 'packet_dumps_15588*' 'network_bin_15588_11871_special28346_after_full_msg509_len43775.bin'
$fourPlayer = Read-Expanded509 'packet_dumps_15588*' 'network_bin_15588_37992_special28346_after_full_msg509_len43775.bin'
$sixPlayer = Read-Expanded509 'packet_dumps_15588*' 'network_bin_15588_32780_special28346_after_full_msg509_len43775.bin'
$twoPlayerA = Read-Expanded509 'packet_dumps_20448' 'network_bin_20448_6942_special28346_after_full_msg509_len43775.bin'
$twoPlayerB = Read-Expanded509 'packet_dumps_31148' 'network_bin_31148_8282_special28346_after_full_msg509_len43775.bin'

Assert-TrackShape $onePlayer 1 @(2) 'official 1P'
Assert-TrackShape $twoPlayerA 2 @(2, 3) 'official 2P'
Assert-TrackShape $fourPlayer 4 @(1, 2, 3, 4) 'official 4P'
Assert-TrackShape $sixPlayer 6 @(0, 1, 2, 3, 4, 5) 'official 6P'

$twoA = Get-TrackView $twoPlayerA
$twoB = Get-TrackView $twoPlayerB
if (($twoA.Orders -join ',') -ne ($twoB.Orders -join ',')) {
    Add-Failure 'same-race two-client official captures do not share one TrackOrders snapshot'
}

$roomHeader = Get-Content -LiteralPath (Join-Path $serverDir 'Room.h') -Raw
$roomSource = Get-Content -LiteralPath (Join-Path $serverDir 'Room.cpp') -Raw
$gameSource = Get-Content -LiteralPath (Join-Path $serverDir 'GameServer.cpp') -Raw

if ($roomHeader -notmatch 'std::vector<UINT>\s+RaceStartUins' -or
    $roomHeader -notmatch 'std::array<UINT,\s*20>\s+RaceTrackOrders' -or
    $roomHeader -notmatch 'bool\s+RaceTrackOrdersValid' -or
    $roomHeader -notmatch 'UINT\s+RaceSeed' -or
    $roomHeader -notmatch 'UINT\s+RaceGameSeq') {
    Add-Failure 'RoomNode does not contain the complete frozen race-start snapshot'
}

$captureStart = $roomSource.IndexOf('static bool BuildFrozenRoomRaceTrackOrders(', [StringComparison]::Ordinal)
$captureEnd = $roomSource.IndexOf('static USHORT GetRoomListSvrID(', $captureStart, [StringComparison]::Ordinal)
if ($captureStart -lt 0 -or $captureEnd -lt 0) {
    Add-Failure 'CaptureRoomRaceStartSnapshot section not found'
} else {
    $captureSource = $roomSource.Substring($captureStart, $captureEnd - $captureStart)
    if ($captureSource -notmatch 'kOfficialSixPlayerLaneOrder' -or
        $captureSource -notmatch 'std::shuffle' -or
        $captureSource -notmatch 'RaceTrackOrdersValid\s*=\s*true') {
        Add-Failure 'room start does not generate one frozen official TrackOrders permutation'
    }
    if ($captureSource -match '\bsrand\s*\(' -or $captureSource -match '\brand\s*\(') {
        Add-Failure 'room race snapshot still uses process-global srand/rand'
    }
}

$notifyStart = $gameSource.IndexOf('bool NotifyGameBegin4(', [StringComparison]::Ordinal)
$notifyEnd = $gameSource.IndexOf('void NotifyGameBeginCS(', $notifyStart, [StringComparison]::Ordinal)
if ($notifyStart -lt 0 -or $notifyEnd -lt 0) {
    Add-Failure 'NotifyGameBegin4 section not found'
} else {
    $notifySource = $gameSource.Substring($notifyStart, $notifyEnd - $notifyStart)
    if ($notifySource -notmatch 'RaceTrackOrdersValid' -or
        $notifySource -notmatch 'RaceTrackOrders\[' -or
        $notifySource -match 'shuffle\(index,\s*CurrentPlayers\)' -or
        $notifySource -match '\bsrand\s*\(') {
        Add-Failure 'normal msg509 does not reuse the frozen room TrackOrders'
    }
    if ($notifySource -notmatch 'Write16\(p,\s*10\);\s*//ReportAntiCollisionDataTime') {
        Add-Failure 'normal msg509 ReportAntiCollisionDataTime is not official 10'
    }
    if ($notifySource -notmatch 'CurrentPlayers\s*>=\s*4\s*\?\s*15u\s*:\s*30u' -or
        $notifySource -notmatch 'Write32\(p,\s*moveFreq509\);\s*//MoveFreq') {
        Add-Failure 'normal msg509 MoveFreq does not follow official 1P/2P=30 and 4P/6P=15 fixtures'
    }
    if ($notifySource -notmatch 'Write32\(p,\s*0x01F08EB3\);\s*//GameBeginSwitchFlag') {
        Add-Failure 'normal msg509 GameBeginSwitchFlag is not official 0x01F08EB3'
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'GameBegin509SyncRegression passed: official 1P/2P/4P/6P fixtures, same-race dual-client identity, frozen server snapshot, official constants'
