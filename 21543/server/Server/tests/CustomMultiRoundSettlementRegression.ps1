param(
    [string]$GameServerCpp = (Join-Path $PSScriptRoot '..\GameServer.cpp')
)

$ErrorActionPreference = 'Stop'

$source = Get-Content -LiteralPath $GameServerCpp -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$gateStart = $source.IndexOf('std::shared_ptr<RoomNode> CustomFinishRoomGate', [StringComparison]::Ordinal)
$gateEnd = $source.IndexOf('if (DistanceToEnd != 0)', $gateStart, [StringComparison]::Ordinal)
if ($gateStart -lt 0 -or $gateEnd -lt 0) {
    throw 'custom finish gate source section not found'
}
$finishGate = $source.Substring($gateStart, $gateEnd - $gateStart)

# Official custom 2/3-round captures emit one 1727 as soon as each stage reaches
# DistanceToEnd=0. CustomRoundNum is the number of shuttle-map stages, not a
# number of finish-line hits to wait for inside the current stage.
if ($finishGate -match 'totalRoundGate|Client->Round|CUSTOM-LAP-NOT-FINAL') {
    $failures.Add('custom finish gate still treats CustomRoundNum/TotalRound as repeated laps before emitting 1727')
}

$notifyStart = $source.IndexOf('void NotifyCustomStageEvent1727', [StringComparison]::Ordinal)
$notifyEnd = $source.IndexOf('void NotifyCustomMatchResult', $notifyStart, [StringComparison]::Ordinal)
if ($notifyStart -lt 0 -or $notifyEnd -lt 0) {
    throw 'NotifyCustomStageEvent1727 source section not found'
}
$notify1727 = $source.Substring($notifyStart, $notifyEnd - $notifyStart)

# The official 38-byte 1727 payload has a fixed byte 3 at +17. Its stage,
# next-stage and stage-rank DWORDs at +21/+25/+29 are little-endian, unlike the
# normal network-order Set32 helper used elsewhere in this server.
if ($notify1727 -notmatch 'Set8\s*\(\s*payload\s*\+\s*17\s*,\s*3\s*\)') {
    $failures.Add('1727 payload byte +17 is not the official fixed value 3')
}
foreach ($offset in 21, 25, 29) {
    if ($notify1727 -notmatch "SetCustomStageU32LE\s*\(\s*payload\s*\+\s*$offset\s*,") {
        $failures.Add("1727 DWORD at +$offset is not encoded with the required little-endian helper")
    }
    if ($notify1727 -match "Set32\s*\(\s*payload\s*\+\s*$offset\s*,") {
        $failures.Add("1727 DWORD at +$offset still uses network-order Set32")
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'CustomMultiRoundSettlementRegression passed'
