$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\GameServer.cpp'
$roomPath = Join-Path $PSScriptRoot '..\Room.cpp'
$encoderPath = Join-Path $PSScriptRoot '..\KartPhys551PlainEncoder.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$roomSource = Get-Content -LiteralPath $roomPath -Raw
$encoderSource = Get-Content -LiteralPath $encoderPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-SourceSection([string]$text, [string]$startMarker, [string]$endMarker) {
    $start = $text.IndexOf($startMarker, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "missing source marker: $startMarker"
    }
    $end = $text.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "missing source marker: $endMarker"
    }
    return $text.Substring($start, $end - $start)
}

$loader = Get-SourceSection $source 'static bool TryLoadKartPhysPlainYaml' 'void Reloadracingdata()'
$resolver = Get-SourceSection $source 'static CustomRaceKartPhysChoice ResolveCustomRaceKartPhysChoice' 'static const BYTE kOfficialCustomGameBegin509TemplateWireBody'
$rosterBuilder = Get-SourceSection $source 'static bool BuildOfficialKartPhys551RosterData' 'static bool SendOfficialKartPhys551Roster'
$sender = Get-SourceSection $source 'static bool SendOfficialKartPhys551Roster' 'void NotifyEncryptedMsg('
$notify = Get-SourceSection $source 'bool NotifyKartPhysParam' 'bool NotifyOtherKartStoneInfo'

if ($loader -notmatch 'HasPlain551' -or $loader -match 'config\s*\[\s*"Data"\s*\]') {
    $failures.Add('551 loader must require exact Plain551 and must not use legacy Data[]')
}
if ($loader -notmatch 'OwnerClient[\s\S]*SessionClient') {
    $failures.Add('551 loader must carry separate source-player and destination-session contexts')
}
if ($loader -notmatch 'Group1Elements"\]\s*=\s*YAML::Node\(YAML::NodeType::Sequence\)') {
    $failures.Add('per-player load must discard captured Group1 companions before rebuilding the live roster')
}
if ($source -match 'shouldApplyRefitToGroup' -or $source -match 'not-official-refit-group-kart') {
    $failures.Add('Group0/Group2 refit must not use a KartID whitelist')
}
if ($source -notmatch 'applyOfficialDeltaToGroup\("Group0Elements"\)' -or
    $source -notmatch 'applyOfficialDeltaToGroup\("Group1Elements"\)' -or
    $source -notmatch 'applyOfficialDeltaToGroup\("Group2Elements"\)') {
    $failures.Add('all three form groups must receive the same official refit overlay as the main profile')
}
if ($resolver -match 'candidates|PushCustomKartCandidate|TryBuildKartPhysDataForPlayer\s*\(\s*baseKartID') {
    $failures.Add('custom 551 resolver still contains a substitute/base-kart fallback')
}
if ($resolver -notmatch 'TryBuildKartPhysDataForPlayer\s*\(\s*preferredID\s*,\s*Player\s*,\s*Player') {
    $failures.Add('custom 551 resolver does not validate the exact selected kart/skin profile')
}
if ($source -match 'QQS_CUSTOM_551_ALL_ROSTER|Custom551AllRoster|NotifyEncryptedMsgForPlayer|Mode=ALL|Mode=SELF') {
    $failures.Add('legacy SELF/ALL 551 control or per-player packet path remains')
}
if ($rosterBuilder -notmatch 'group1Extra\.push_back' -or
    $rosterBuilder -notmatch 'Group0Elements' -or
    $rosterBuilder -notmatch 'Group2Elements' -or
    $rosterBuilder -notmatch 'exact-Plain551-missing[\s\S]*return false') {
    $failures.Add('official 551 builder must aggregate Group0/Group1/Group2 and abort on any missing exact profile')
}
if ($sender -notmatch 'Write32\s*\(\s*p\s*,\s*DstClient->Uin\s*\)' -or
    $sender -notmatch 'Write16\s*\(\s*p\s*,\s*551\s*\)' -or
    $sender -notmatch 'SendToClientWithWireDstIDAndMsgSeq[\s\S]*28346[\s\S]*,\s*2\s*,\s*Notify') {
    $failures.Add('official 551 sender does not use the captured 28346 destination/session framing')
}
if ($sender -notmatch 'if\s*\(\s*!sent\s*\)[\s\S]*return false[\s\S]*CustomLastSentMsgID\s*=\s*551') {
    $failures.Add('551 state marker must only advance after a successful send')
}
if ($notify -notmatch 'BuildGameBegin509V5Roster' -or
    $notify -notmatch 'ValidateFrozenRaceStartFor509' -or
    $notify -notmatch 'return SendOfficialKartPhys551Roster\s*\(\s*Client\s*,\s*Room\s*,\s*roster\s*\)') {
    $failures.Add('551 must use the same frozen roster/order as 509 and send one aggregated packet')
}
if ($encoderSource -notmatch 'Group1Elements' -or
    $encoderSource -notmatch 'group1Count\s*=\s*1\s*\+\s*p\.m_group1Elements\.size\(\)' -or
    $encoderSource -notmatch 'for\s*\(\s*const auto& elem\s*:\s*p\.m_group1Elements\s*\)') {
    $failures.Add('native 551 encoder does not emit the full official Group1 roster')
}
if ($roomSource -notmatch 'if\s*\(\s*!NotifyKartPhysParam\s*\(\s*RoomClient\s*,\s*Room->Player\s*\)\s*\)' -or
    $roomSource -match 'CustomLastSentMsgID\s*=\s*551') {
    $failures.Add('custom loading must abort on 551 failure and must not force the 551 state marker')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'KartPhysPlayerContextRegression passed'
