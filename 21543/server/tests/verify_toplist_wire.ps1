$ErrorActionPreference = 'Stop'

$playerPath = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\Server')) 'Player.cpp'
$source = Get-Content -LiteralPath $playerPath -Raw -Encoding UTF8

function Get-SourceSection([string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [System.StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "Missing source marker: $startMarker"
    }

    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [System.StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "Missing source marker after ${startMarker}: $endMarker"
    }

    return $source.Substring($start, $end - $start)
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        throw $message
    }
}

function Assert-Ordered([string]$text, [string[]]$needles, [string]$message) {
    $cursor = 0
    foreach ($needle in $needles) {
        $next = $text.IndexOf($needle, $cursor, [System.StringComparison]::Ordinal)
        if ($next -lt 0) {
            throw "$message Missing/out-of-order token: $needle"
        }
        $cursor = $next + $needle.Length
    }
}

$resolver = Get-SourceSection 'BOOL IsUserInTopList(' 'void UpdateTopList('
$visible1 = Get-SourceSection 'void WriteVisbleInfo1(' 'void WriteVisbleInfo('
$visible = Get-SourceSection 'void WriteVisbleInfo(' 'void RequestRegister2('
$login = Get-SourceSection 'BOOL ResponseLogin(' 'void RequestLogout('
$engage = Get-SourceSection 'void ResponseEngageRoleInfo(' 'void RequestFindPlayerByQQ('
$findPlayer = Get-SourceSection 'void ResponseFindPlayerByQQ(' 'void RequestGetAchieveList('

Assert-Contains $resolver 'static BYTE QQS_GetTopListWireFlag(UINT Uin)' 'Missing the shared official-style top-list wire resolver.'
Assert-Contains $resolver 'return (Uin != 0 && IsUserInTopList(static_cast<long>(Uin))) ? 1 : 0;' 'Top-list wire resolver must normalize membership to one byte.'

Assert-Contains $visible1 'Write8(pPlayerRoomVisibleInfo, QQS_GetTopListWireFlag(Uin)); //IsInTopList' 'Initial room-visible info does not use the shared top-list flag.'
Assert-Contains $visible 'Write8(pPlayerRoomVisibleInfo, QQS_GetTopListWireFlag(Uin)); //IsInTopList' 'Room-visible info does not use the shared top-list flag.'
Assert-Contains $engage 'Write8(p, QQS_GetTopListWireFlag(DstUin)); //IsInTopList' 'Engage profile does not use the shared top-list flag.'
Assert-Contains $findPlayer 'Write8(p, QQS_GetTopListWireFlag(DstUin)); //IsInTopList' 'Msg122 player profile does not use the shared top-list flag.'

Assert-Ordered $login @(
    'Set16(pTotalPet, totalPet);',
    'Write8(p, QQS_GetTopListWireFlag(Client ? Client->Uin : 0)); //IsInTopList',
    'Write32(p, 0); //LastLoginTime',
    'Write32(p, 0); //MasterPoint'
) 'Msg100 self top-list flag is missing or no longer occupies the official one-byte slot after PetInfo[].'

if ($login.Contains('//iviplevel')) {
    throw 'Msg100 still mislabels IsInTopList as iviplevel.'
}
if ($source.Contains('Write8(p, 0); //IsInTopList')) {
    throw 'A player-profile path still hard-codes IsInTopList to zero.'
}

$resolverUseCount = ([regex]::Matches($source, 'QQS_GetTopListWireFlag\(')).Count
if ($resolverUseCount -ne 6) {
    throw "Expected one resolver definition and five wire uses; found $resolverUseCount occurrences."
}

Write-Output 'TOPLIST_WIRE_SOURCE_TEST_OK'
