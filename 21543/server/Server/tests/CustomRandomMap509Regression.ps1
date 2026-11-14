param(
    [string]$GameServerCpp = (Join-Path $PSScriptRoot '..\GameServer.cpp'),
    [string]$OfficialExpanded509 = 'F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_20376\payloads\network_bin_20376_5440_special28346_after_full_msg509_len43775.bin'
)

$ErrorActionPreference = 'Stop'

$source = Get-Content -LiteralPath $GameServerCpp -Raw
$start = $source.IndexOf('static bool BuildOfficialCustomGameBegin509Body', [StringComparison]::Ordinal)
$end = $source.IndexOf('bool ValidateCustomRaceKartPhysSnapshot', $start, [StringComparison]::Ordinal)
if ($start -lt 0 -or $end -lt 0) {
    throw 'BuildOfficialCustomGameBegin509Body source section not found'
}
$builder = $source.Substring($start, $end - $start)
$failures = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path -LiteralPath $OfficialExpanded509)) {
    throw "official expanded msg509 dump not found: $OfficialExpanded509"
}
$official = [System.IO.File]::ReadAllBytes($OfficialExpanded509)
function Read-U16LE([int]$offset) { [BitConverter]::ToUInt16($official, $offset) }
function Read-U32LE([int]$offset) { [BitConverter]::ToUInt32($official, $offset) }

$officialMap = Read-U32LE 0x0004
$officialShuttleMap = Read-U32LE 0x3451
$officialCurrentMap = Read-U32LE 0x34CE
$officialOuterBase = Read-U16LE 0x8E65
$officialOuterSub = Read-U16LE 0x8E67
$officialCustomBase = Read-U16LE (0xA7C4 + 0x8C)
$officialCustomSub = Read-U16LE (0xA7C4 + 0x8E)
$officialCustomMap = Read-U32LE (0xA7C4 + 0x9F)

if ($officialMap -ne 202 -or $officialShuttleMap -ne 202 -or
    $officialCurrentMap -ne 202 -or $officialCustomMap -ne 202) {
    $failures.Add("official random-map invariant changed: top=$officialMap shuttle=$officialShuttleMap current=$officialCurrentMap custom=$officialCustomMap")
}
if ($officialOuterBase -ne 42 -or $officialOuterSub -ne 27 -or
    $officialCustomBase -ne 1 -or $officialCustomSub -ne 1) {
    $failures.Add("official custom-mode invariant changed: outer=$officialOuterBase/$officialOuterSub custom=$officialCustomBase/$officialCustomSub")
}

if ($builder -match 'ruleMapList|Room->CustomMapIDList') {
    $failures.Add('msg509 builder still carries the lobby map-pool slots into race runtime state')
}
if ($builder -match 'NormalizeCustomSchemeForOfficial|BuildCustomStageMapOrder|GetCustomStageMapID') {
    $failures.Add('msg509 builder still normalizes or re-resolves maps instead of consuming the frozen stage order')
}
if ($builder -notmatch 'UINT\s+mapList\[3\]\s*=\s*\{\s*0\s*,\s*0\s*,\s*0\s*\}') {
    $failures.Add('msg509 resolved map list is still initialized from a fallback map')
}
if ($builder -notmatch 'mapList\[i\]\s*=\s*Room->CustomStageMapOrder\[i\]') {
    $failures.Add('msg509 does not consume the already-resolved custom stage map order')
}
foreach ($slot in 0..2) {
    $offset = 18 + ($slot * 4)
    if ($builder -notmatch "PatchOfficialCustomGameBegin509U32\(wireBody, customTail \+ $offset, mapList\[$slot\]\)") {
        $failures.Add("custom scheme runtime map slot $slot is not patched from the resolved map list")
    }
}
if ($builder -notmatch 'kOfficialCustomOuterBaseMode\s*=\s*42' -or
    $builder -notmatch 'kOfficialCustomOuterSubMode\s*=\s*27') {
    $failures.Add('msg509 outer custom UI discriminator is not the official 42/27 pair')
}
if ($builder -match 'timeShuttleGen|TimeShuttleGen|0x0228') {
    $failures.Add('msg509 builder still contains the residual forced time-shuttle UI override')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host "CustomRandomMap509Regression passed: official map=$officialMap outer=$officialOuterBase/$officialOuterSub custom=$officialCustomBase/$officialCustomSub"
