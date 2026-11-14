$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$gameServerPath = Join-Path $serverRoot 'GameServer.cpp'
$builderHeaderPath = Join-Path $serverRoot 'GameBegin509V5Builder.h'
$builderSourcePath = Join-Path $serverRoot 'GameBegin509V5Builder.cpp'
$messageHeaderPath = Join-Path $serverRoot 'Message.h'
$roomSourcePath = Join-Path $serverRoot 'Room.cpp'
$luckySourcePath = Join-Path $serverRoot 'LuckyMatch.cpp'
$configPath = Join-Path $serverRoot 'bin\Config\ASpeedConfig.yml'

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

$gameServer = Get-Content -LiteralPath $gameServerPath -Raw
$builderHeader = Get-Content -LiteralPath $builderHeaderPath -Raw
$builderSource = Get-Content -LiteralPath $builderSourcePath -Raw
$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$roomSource = Get-Content -LiteralPath $roomSourcePath -Raw
$luckySource = Get-Content -LiteralPath $luckySourcePath -Raw
$config = Get-Content -LiteralPath $configPath -Raw

$v5Source = Get-SourceRange `
    -Text $gameServer `
    -Begin 'enum class GameBegin509V5Profile' `
    -End 'bool NotifyGameBegin4('
$send509Source = Get-SourceRange `
    -Text $gameServer `
    -Begin 'static bool SendOfficialSafeGameBegin509(' `
    -End 'bool NotifyGameBegin('
$authoritySource = Get-SourceRange `
    -Text $roomSource `
    -Begin 'static bool BuildRoomRaceAuthority(' `
    -End 'static BYTE FindFrozenRoomRaceTrackSlot('
$requestStartSource = Get-SourceRange `
    -Text $roomSource `
    -Begin 'void RequestStartGame(' `
    -End 'void ResponseStartGame('
$auditedSource = $v5Source + "`n" + $builderHeader + "`n" + $builderSource + "`n" + $authoritySource

$forbidden = [ordered]@{
    '\bNotifyGameBegin4\b' = 'V5 references the V4 implementation'
    '\b28346\b' = 'V5 references the safe-envelope message'
    '\bBuildOfficialCustomGameBegin509Body\b' = 'V5 references the old custom builder'
    '\bkOfficialCustomGameBegin509Template\b' = 'V5 references a captured packet template'
    '\bWriteHex\s*\(' = 'V5 contains a hex-template writer'
    '\bSleep\s*\(' = 'V5 contains a blocking delay'
    '\bsleep_for\s*\(' = 'V5 contains a blocking delay'
    '\bWaitForSingleObject\s*\(' = 'V5 contains a blocking wait'
    '\bGetTickCount(?:64)?\s*\(' = 'V5 authority uses a tick-derived substitute'
    '\b(?:std::)?rand\s*\(' = 'V5 authority uses the process PRNG'
    'high_resolution_clock' = 'V5 authority uses a clock-derived substitute'
    '0xFFFC|0xFFFD' = 'V5 infers its race profile from a TaskID sentinel'
    '(?i)\bfallback\b' = 'V5 contains fallback behavior'
    '保底|强制|停留' = 'V5 contains a fallback/forced-stay branch'
}
foreach ($entry in $forbidden.GetEnumerator()) {
    if ([regex]::IsMatch($auditedSource, $entry.Key)) {
        throw $entry.Value
    }
}

if ($gameServer -notmatch 'else\s+if\s*\(beiginmode\s*==\s*5\)\s*\{\s*return\s+NotifyGameBegin5\(Client,\s*Player\);') {
    throw 'BeiginMode 5 does not dispatch directly to NotifyGameBegin5'
}
if ($gameServer -notmatch 'else\s+if\s*\(beiginmode\s*==\s*4\)\s*\{\s*return\s+NotifyGameBegin4\(Client,\s*Player\);') {
    throw 'BeiginMode 4 does not remain an explicit YML-selected rollback path'
}
if (($roomSource + "`n" + $luckySource) -match '\bNotifyGameBegin(?:4|5)\s*\(') {
    throw 'A race-mode flow bypasses the YML-selected NotifyGameBegin dispatcher'
}
if ($v5Source -notmatch 'SendOfficialSafeGameBegin509\s*\(' -or
    $send509Source -notmatch 'SendToClientWithWireDstIDAndMsgSeq\s*\(\s*Client,\s*28346\b' -or
    $send509Source -notmatch 'SendToClient\s*\(\s*Client,\s*28346\b') {
    throw 'NotifyGameBegin5 does not use both verified Msg28346 safe-envelope routes'
}
if ($messageHeader -notmatch 'bool\s+NotifyGameBegin5\s*\(') {
    throw 'NotifyGameBegin5 is not declared in Message.h'
}
if ($builderSource -notmatch 'writer\.U8\(fields\.p2pMode\);\s*// P2PMode' -or
    $builderSource -notmatch 'writer\.U8\(fields\.tcpFrequency\);\s*// TcpFrequence') {
    throw 'V5 builder does not serialize P2PMode/TcpFrequence from Fields'
}
if ($builderSource -match 'writer\.U8\(1\);\s*// P2PMode' -or
    $v5Source -notmatch 'fields\.p2pMode\s*=\s*static_cast<BYTE>\(P2PMode\)' -or
    $v5Source -notmatch 'fields\.tcpFrequency\s*=\s*static_cast<BYTE>\(TcpFrequence\)') {
    throw 'V5 transport mode remains hard-coded instead of using ASpeedConfig.yml'
}

# Every racer must have its authoritative game/map state before the first 509 is
# built. Interleaving state preparation and dispatch makes a two-player room
# fail V5 roster consistency while processing the first recipient.
$preparedClientsDeclaration = 'std::vector<std::shared_ptr<ClientNode>> raceStartClients;'
$preparedClientAppend = 'raceStartClients.push_back(RoomClient);'
$preparedClientsDispatch = 'for (const auto& RoomClient : raceStartClients)'
$declarationIndex = $requestStartSource.IndexOf($preparedClientsDeclaration, [StringComparison]::Ordinal)
$appendIndex = $requestStartSource.IndexOf($preparedClientAppend, [StringComparison]::Ordinal)
$dispatchIndex = $requestStartSource.IndexOf($preparedClientsDispatch, [StringComparison]::Ordinal)
$mapAssignmentIndex = $requestStartSource.IndexOf('RoomClient->MapID = MapID;', [StringComparison]::Ordinal)
$gameSessionBeginIndex = $requestStartSource.IndexOf('BeginRoomGameLogicSession(Room, "start-game")', [StringComparison]::Ordinal)
$gameAssignmentIndex = $requestStartSource.IndexOf('RoomClient->GameID = Room->RaceGameID;', [StringComparison]::Ordinal)
$gameBeginIndex = $requestStartSource.IndexOf('NotifyGameBegin(RoomClient, Room->Player)', [StringComparison]::Ordinal)
if ($declarationIndex -lt 0 -or $appendIndex -lt 0 -or $dispatchIndex -lt 0) {
    throw 'RequestStartGame does not use separate all-player preparation and dispatch phases'
}
if ($gameSessionBeginIndex -lt 0 -or $gameSessionBeginIndex -gt $declarationIndex) {
    throw 'RequestStartGame does not allocate one room-scoped game session before player preparation'
}
if ($mapAssignmentIndex -lt $declarationIndex -or $mapAssignmentIndex -gt $appendIndex -or
    $gameAssignmentIndex -lt $declarationIndex -or $gameAssignmentIndex -gt $appendIndex) {
    throw 'RequestStartGame does not prepare GameID/MapID before publishing each prepared racer'
}
if ($gameBeginIndex -lt $dispatchIndex) {
    throw 'RequestStartGame sends 509 before every racer has completed state preparation'
}
if ($authoritySource -notmatch '\bBCryptGenRandom\s*\(') {
    throw 'Room race authority is not generated by the OS CSPRNG'
}
foreach ($binding in @(
    'fields.seed = Room->RaceSeed;',
    'fields.multiInfo.assign(Room->RaceMultiInfo.begin(), Room->RaceMultiInfo.end());',
    'fields.gameSequence = Room->RaceGameSeq;',
    'fields.gameSequenceIdHigh = Room->RaceMatchSeqHigh;',
    'fields.gameSequenceIdLow = Room->RaceMatchSeqLow;',
    'Write32(p, Room->RaceMatchSeqHigh);',
    'Write32(p, Room->RaceMatchSeqLow);'
)) {
    if (-not $gameServer.Contains($binding)) {
        throw "room-scoped authority is not shared by 509 and 20081: $binding"
    }
}
if ($config -notmatch '(?m)^\s*BeiginMode:\s*5(?:\s|$)') {
    throw 'ASpeedConfig.yml is not selecting BeiginMode 5'
}

$requiredProfileLiterals = @(
    '0x01F08EB3u', # normal
    '0x07D88EB3u', # ranked
    '0x01D10EB3u', # pinnacle
    '0x81F88EB3u', # border (ENGBSF_EnableLeaveGame)
    '0x00D13EB3u', # lucky grand prix
    '0x00B1BBB1u', # custom
    '0x46B93BB0u'  # Aurora
)
foreach ($literal in $requiredProfileLiterals) {
    if (-not $v5Source.Contains($literal)) {
        throw "official profile literal missing from V5 source: $literal"
    }
}

foreach ($projectName in @('Server.vcxproj', 'SpeedServer.vcxproj')) {
    $project = Get-Content -LiteralPath (Join-Path $serverRoot $projectName) -Raw
    if ($project -notmatch '<ClCompile Include="GameBegin509V5Builder\.cpp"\s*/>' -or
        $project -notmatch '<ClInclude Include="GameBegin509V5Builder\.h"\s*/>') {
        throw "$projectName does not compile the V5 builder"
    }
}

Write-Host 'GameBegin509V5 source audit passed: V5 builder plus verified Msg28346 safe envelope, no V4/template/fallback/wait path.'
