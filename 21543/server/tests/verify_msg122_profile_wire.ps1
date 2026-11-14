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

$engage = Get-SourceSection 'void ResponseEngageRoleInfo(' 'void RequestFindPlayerByQQ('
$findPlayer = Get-SourceSection 'void ResponseFindPlayerByQQ(' 'void RequestGetAchieveList('

foreach ($section in @($engage, $findPlayer)) {
    Assert-Ordered $section @(
        'Write16(pPlayerInfo, static_cast<USHORT>(VipFlag & 0xFFFF)); //VipFlag',
        'Write16(pPlayerInfo, QQS_GetPurpleVipGrowRate(VipFlag)); //VipGrowRate',
        'Write8(pPlayerInfo, cvState.VipLevel > 0 ? 1 : 0); //isConsumeVip',
        'Write32(pPlayerInfo, cvState.VipLevel); //ConsumeVipLevel',
        'Write32(pPlayerInfo, cvState.SearchTreasureNums); //SearchTreasureNums',
        'Write32(pPlayerInfo, cvState.GetTreasureNums); //GetTreasureNums',
        'Write32(pPlayerInfo, cvState.CharmValueOfMonth); //CharmValueOfMonth',
        'Write8(pPlayerInfo, 0); //HasKartColorInfo',
        'Write32(pPlayerInfo, cvState.CharmCareerLevel); //CharmCareerLevel',
        'Write8(pPlayerInfo, cvState.ShowConsumeVip); //ShowConsumeVip',
        'Write8(pPlayerInfo, 0); //ZhizhenKartStage',
        'Write32(pPlayerInfo, 0); //RoleID high',
        'Write32(pPlayerInfo, 0); //RoleID low',
        'Write32(pPlayerInfo, 0); //KartSkinModel',
        'Write8(pPlayerInfo, 0); //MiniMechNum',
        'Write32(pPlayerInfo, 0); //RUGCArtistLevel',
        'Write32(pPlayerInfo, 0); //AuroraCraftsmanLevel',
        'QQS_WriteMsg122PositionInfo21487(p,'
    ) 'Msg122 PlayerInfo field order does not match the 21487 dump.'

    if ($section.Contains('Write32(pPlayerInfo, QQS_GetPurpleVipLevel(VipFlag))')) {
        throw 'Legacy 32-bit vipLevel still occupies the 21487 VipFlag/VipGrowRate wire slot.'
    }
}

Assert-Contains $findPlayer 'Write32(pPlayerSeasonInfos, 0); //RMTopRaceScore' 'PlayerSeasonInfo is four bytes short: RMTopRaceScore is missing.'

Assert-Contains $source 'static constexpr size_t kQQSMsg122RoomInfoLen21487 = 112;' 'Msg122 RoomInfo must be the 112-byte 21487 record.'
Assert-Contains $source 'Write8(pRoomInfo, Room ? static_cast<BYTE>(Room->Flag & 0xFF) : 0); //Flag' 'Msg122 RoomInfo Flag must be one byte.'
Assert-Contains $source 'struct QQSMsg122Presence21487' 'Msg122 is missing its explicit presence-state snapshot.'
Assert-Contains $source 'QQS_GetMsg122PresenceClient21487' 'Msg122 is missing the game/relax presence lookup.'
Assert-Contains $source 'GetRelaxClient(Uin)' 'Msg122 must also find players connected to the relax server.'
Assert-Contains $source 'GetBorderRoom(Dst->BorderRoomID)' 'Msg122 must resolve the temporary border/ranked room.'
Assert-Contains $source 'Dst->TaskID == 0xFFFD' 'Msg122 is missing border presence detection.'
Assert-Contains $source 'Dst->TaskID == 0xFFFC' 'Msg122 is missing ranked presence detection.'
Assert-Contains $source 'Dst->TaskID == 0xFFFB' 'Msg122 is missing quick-match presence detection.'
$borderStatusText = '"' + [char]0x8DE8 + [char]0x670D + [char]0x8FB9 + [char]0x5883 + [char]0x7ADE + [char]0x901F + [char]0x4E2D + '"'
$rankedStatusText = '"' + [char]0x6392 + [char]0x4F4D + [char]0x8D5B + [char]0x4E2D + '"'
$multiplayerStatusText = '"' + [char]0x591A + [char]0x4EBA + [char]0x6E38 + [char]0x620F + [char]0x4E2D + '"'
Assert-Contains $source $borderStatusText 'Msg122 border presence text does not match the reference dump.'
Assert-Contains $source $rankedStatusText 'Msg122 ranked presence text does not match the reference dump.'
Assert-Contains $source $multiplayerStatusText 'Msg122 quick-match presence text does not match the reference dump.'
Assert-Contains $source 'state.Status = 12;' 'Msg122 normal online/lobby status must use the dump-backed status 12.'
Assert-Contains $source 'state.Status = 16;' 'Msg122 room-waiting status must use the dump-backed status 16.'
Assert-Contains $source 'state.Status = 17;' 'Msg122 racing/activity status must use the dump-backed status 17.'
Assert-Contains $source 'Room->BaseGameMode == 4 ? 28 : FE_GAMESVRD' 'Msg122 dance rooms must use the dump-backed server type 28.'
Assert-Contains $source 'Write16(pPositionInfo, state.Status); //Status' 'Msg122 PositionInfo must serialize the resolved status.'
Assert-Contains $source 'Write8(pPositionInfo, 0); //CanJoin' 'Msg122 CanJoin must remain zero like every reference dump.'
Assert-Contains $source 'Write32(pPositionInfo, state.GuildID); //GuildID' 'Msg122 PositionInfo must serialize the resolved GuildID.'
Assert-Contains $source 'kartRefitItemInfo->SkinSuitID > 0 ? kartRefitItemInfo->SkinSuitID : kartRefitItemInfo->ShapeSuitID' 'Msg122 does not select the effective persisted kart skin.'

$resolver = Get-SourceSection 'static QQSMsg122Presence21487 QQS_ResolveMsg122Presence21487(' 'static void QQS_WriteMsg122PositionInfo21487('
Assert-Ordered $resolver @(
    'Dst->TaskID == 0xFFFD',
    'Dst->TaskID == 0xFFFC',
    'Dst->TaskID == 0xFFFB',
    'GetRoom(Dst->RoomID)'
) 'Msg122 must classify border/ranked/quick-match state before the retained normal RoomID.'
if ($resolver.Contains('state.Status = 15;')) {
    throw 'Status 15 has no closed UI meaning in the available dumps and must not be guessed.'
}

$positionCallCount = ([regex]::Matches($source, 'QQS_WriteMsg122PositionInfo21487\(p,')).Count
if ($positionCallCount -ne 2) {
    throw "Expected both Msg122 response paths to use the aligned PositionInfo writer; found $positionCallCount calls."
}

$presenceLookupCount = ([regex]::Matches($source, 'QQS_GetMsg122PresenceClient21487\(DstUin\)')).Count
if ($presenceLookupCount -ne 3) {
    throw "Expected all live Msg122/engage/pet lookups to include relax-server presence; found $presenceLookupCount calls."
}

Write-Output 'MSG122_PROFILE_WIRE_SOURCE_TEST_OK'
