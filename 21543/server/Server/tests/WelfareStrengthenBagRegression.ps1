$ErrorActionPreference = 'Stop'

$serverDir = Split-Path -Parent $PSScriptRoot
$welfarePath = Join-Path $serverDir 'WelfareActivity.cpp'
$messagePath = Join-Path $serverDir 'Message.cpp'
$messageHeaderPath = Join-Path $serverDir 'Message.h'

$welfare = Get-Content -LiteralPath $welfarePath -Raw
$message = Get-Content -LiteralPath $messagePath -Raw
$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-SourceSection(
    [string]$source,
    [string]$startMarker,
    [string]$endMarker
) {
    $start = $source.IndexOf($startMarker, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "missing source marker: $startMarker" }
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "missing source marker: $endMarker" }
    return $source.Substring($start, $end - $start)
}

$mainWriter = Get-SourceSection `
    $welfare `
    'static void WF_WriteStrengthenMain(' `
    'static void WF_WriteStrengthenAwardResult('

$mainHandler = Get-SourceSection `
    $welfare `
    'void Request_Beta97StrengthenBagMainDlg(' `
    'void Request_Beta97StrengthenBagRedPoint('

$redPointHandler = Get-SourceSection `
    $welfare `
    'void Request_Beta97StrengthenBagRedPoint(' `
    'void Request_Beta97StrengthenBagGetAward('

$getAwardHandler = $welfare.Substring(
    $welfare.IndexOf('void Request_Beta97StrengthenBagGetAward(', [StringComparison]::Ordinal))

if ($messageHeader -notmatch 'struct\s+StrengthenBagPlayerState\s*\{[\s\S]*AwardSlotCount\s*=\s*5\s*;[\s\S]*std::array<INT,\s*AwardSlotCount>\s+AwardIndexes') {
    $failures.Add('strengthen bag player state is not a typed five-slot structure')
}

if ($mainWriter -match 'WF_WriteEmptyTable\s*\(\s*player\s*,\s*"awardInfo"') {
    $failures.Add('MainDlg still emits an empty awardInfo table')
}
if ($mainWriter -notmatch 'WF_WriteObject\s*\(\s*player\s*,\s*"awardInfo"') {
    $failures.Add('MainDlg does not emit the official awardInfo object')
}
if ($mainWriter -notmatch 'state\.AwardIndexes\.size\(\)') {
    $failures.Add('MainDlg does not serialize every fixed award slot')
}
if ($mainWriter -notmatch 'WF_WriteNumber\s*\(\s*awardInfo\s*,\s*static_cast<int>\(i\s*\+\s*1\)\s*\)') {
    $failures.Add('awardInfo does not use the official 1-based numeric keys')
}
if ($mainHandler -notmatch 'client->StrengthenBagState') {
    $failures.Add('MainDlg ignores the per-client strengthen bag state')
}

if ($redPointHandler -notmatch 'WF_SendNumberResponse\s*\(') {
    $failures.Add('RedPointCheck still omits the required numeric return value')
}
if ($redPointHandler -match 'WF_Send\s*\([^;]*false') {
    $failures.Add('RedPointCheck still sends the old two-value response')
}

if ($messageHeader -notmatch 'void\s+Request_Beta97StrengthenBagGetAward\s*\(') {
    $failures.Add('Message.h does not declare the GetAward handler')
}
if ($message -notmatch 'cmd\s*==\s*"Co\.Beta97_Strengthen_Bag_GetAward"[\s\S]{0,180}Request_Beta97StrengthenBagGetAward') {
    $failures.Add('QuickRPC dispatcher does not route Strengthen_Bag_GetAward')
}
if ($getAwardHandler -notmatch 'WF_WriteEmptyTable\s*\(\s*p\s*,\s*"awardToReturn"\s*\)') {
    $failures.Add('GetAward has no client-safe empty result when there is no pending reward')
}
if ($getAwardHandler -match 'AwardIndexes\s*=\s*\{\s*[1-9]' -or
    $getAwardHandler -match '\brand\s*\(' -or
    $getAwardHandler -match '\brandom\b') {
    $failures.Add('GetAward fabricates captured/random rewards instead of consuming player state')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'WelfareStrengthenBagRegression passed: MainDlg/RedPoint/GetAward match the client-required structure without forced rewards.'
