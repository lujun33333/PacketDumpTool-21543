$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path $PSScriptRoot -Parent
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$player = Get-Content -LiteralPath (Join-Path $serverRoot 'Player.cpp') -Raw
$message = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.cpp') -Raw
$messageHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.h') -Raw
$database = Get-Content -LiteralPath (Join-Path $serverRoot 'Database.cpp') -Raw
$safeCodec = Get-Content -LiteralPath (Join-Path $serverRoot 'Msg100SafeCodecData.inc') -Raw

function Assert-Match([string]$Text, [string]$Pattern, [string]$Failure) {
    if ($Text -notmatch $Pattern) {
        throw $Failure
    }
}

function Assert-NoMatch([string]$Text, [string]$Pattern, [string]$Failure) {
    if ($Text -match $Pattern) {
        throw $Failure
    }
}

$requestMatch = [regex]::Match(
    $gameServer,
    '(?s)void RequestChangeIdentity\(.*?(?=\r?\nvoid RequestReportCurrentOpt\()'
)
if (-not $requestMatch.Success) {
    throw 'RequestChangeIdentity source block not found'
}
$request = $requestMatch.Value

$responseMatch = [regex]::Match(
    $gameServer,
    '(?s)void ResponseChangeIdentity\(.*?(?=\r?\nvoid RequestChangeIdentity\()'
)
if (-not $responseMatch.Success) {
    throw 'ResponseChangeIdentity source block not found'
}
$response = $responseMatch.Value

$loginFlagMatch = [regex]::Match(
    $player,
    '(?s)//ExRightFlag\[\].*?(?=\r?\n\s*std::vector<ItemInfo> vecItems;)'
)
if (-not $loginFlagMatch.Success) {
    throw 'Msg100 ExRightFlag source block not found'
}
$loginFlags = $loginFlagMatch.Value

$gameModeMatch = [regex]::Match(
    $player,
    '(?s)void getGameMode\(.*?(?=\r?\nvoid NotifyG2CFizzStarCfg\()'
)
if (-not $gameModeMatch.Success) {
    throw 'getGameMode source block not found'
}
$gameMode = $gameModeMatch.Value

# Official 21543 Msg131 has 46 payload bytes. MSGHEAD::BodyLen includes its
# own two-byte field, so the handler must receive 48 and must not skip two
# bytes from the already advanced Body pointer.
Assert-Match $request 'sizeof\(USHORT\)\s*\+\s*4\s*\+\s*4\s*\+\s*1\s*\+\s*1\s*\+\s*4\s*\+\s*4\s*\+\s*4\s*\+\s*QQS_EX_RIGHT_FLAG_BYTES' `
    'Msg131 request length is not the official 48-byte declared layout'
Assert-Match $request 'BodyLen\s*!=\s*kRequestBodyLen' `
    'Msg131 request does not enforce the official fixed layout'
Assert-Match $request 'Read32\(p\);\s*// BeginBit' 'Msg131 BeginBit is not consumed'
Assert-Match $request 'Read32\(p\);\s*// EndBit' 'Msg131 EndBit is not consumed'
Assert-Match $request 'p\s*\+=\s*QQS_EX_RIGHT_FLAG_BYTES;\s*// ExRightFlag\[24\]' `
    'Msg131 ExRightFlag[24] tail is not consumed'
Assert-NoMatch $request 'HasWireLen|WireLen|Body\s*\+\s*2' `
    'Msg131 still treats the first two Uin bytes as an in-payload length'

# Msg131 response is ResultID(2)+mode(1)+isEx(1)+flag(4)+identity(4)+24 bytes.
Assert-Match $response 'BYTE buf\[2 \+ 1 \+ 1 \+ 4 \+ 4 \+ QQS_EX_RIGHT_FLAG_BYTES\]' `
    'Msg131 response is not the official 36-byte payload'
Assert-Match $response 'PlayerDB_UpdateExRightFlagHex\(Client->Uin,\s*Client->ExRightFlags,\s*QQS_EX_RIGHT_FLAG_BYTES\)' `
    'Msg131 does not persist the complete ExRightFlag array'
Assert-Match $response 'memcpy\(p,\s*Client->ExRightFlags,\s*QQS_EX_RIGHT_FLAG_BYTES\)' `
    'Msg131 response does not return the complete ExRightFlag array'

# Login must not replay a captured Msg131 response. The 21543 client copies
# all 24 response bytes into player state, so a static account snapshot here
# would overwrite the flags restored by Msg100.
Assert-NoMatch $gameMode 'SendToClient\s*\(\s*Client\s*,\s*131|pMsg131|000001010000007540D0000E' `
    'getGameMode still sends a captured Msg131 snapshot that overwrites login flags'

# The receive layer must prove that declared BodyLen(48)-2 equals the 46
# available payload bytes before RequestChangeIdentity consumes the tail.
Assert-Match $message 'MsgID == 1 \|\| MsgID == 3 \|\| MsgID == 131 \|\| MsgID == 153' `
    'Msg131 is missing from receive-layer declared-length validation'

# Official 21543 Msg100 always places all 24 ExRightFlag bytes immediately
# before OverdueItemNum; no legacy 16-byte branch is valid for this target.
Assert-Match $messageHeader '#define QQS_EX_RIGHT_FLAG_BYTES 24' `
    'QQS_EX_RIGHT_FLAG_BYTES is not 24'
Assert-Match $loginFlags 'memcpy\(p,\s*Client->ExRightFlags,\s*QQS_EX_RIGHT_FLAG_BYTES\)' `
    'Msg100 does not serialize ExRightFlag[24]'
Assert-NoMatch $loginFlags 'ClientVersion|18457|memcpy\(p,\s*Client->ExRightFlags,\s*16\)|p\s*\+=\s*16' `
    'Msg100 still contains a legacy 16-byte ExRightFlag branch'
Assert-Match $safeCodec 'ExRightFlags\[0\]' 'Msg100 safe codec is missing ExRightFlags[0]'
Assert-Match $safeCodec 'ExRightFlags\[23\]' 'Msg100 safe codec is missing ExRightFlags[23]'

# Persistence storage remains exactly 24 bytes encoded as 48 hex characters.
Assert-Match $database 'ExRightFlagHex VARCHAR\(48\)' 'ExRightFlagHex schema is not VARCHAR(48)'
Assert-Match $database 'SELECT ExRightFlagHex FROM BaseInfo WHERE Uin=\?' `
    'ExRightFlagHex login read is missing'
Assert-Match $database 'UPDATE BaseInfo SET ExRightFlagHex=\? WHERE Uin=\?' `
    'ExRightFlagHex update is missing'
Assert-Match $player 'PlayerDB_LoadExRightFlags\(Client->Uin,\s*Client->ExRightFlags,\s*QQS_EX_RIGHT_FLAG_BYTES\)' `
    'Login does not load all 24 ExRightFlag bytes'

# Pure round-trip model for the two client switches:
# navigation-route disable bit 141 and kart-cockpit personalization bit 153.
[byte[]]$flags = New-Object byte[] 24
foreach ($bit in 141, 153) {
    $flags[$bit -shr 3] = $flags[$bit -shr 3] -bor (1 -shl ($bit -band 7))
}
$hex = -join ($flags | ForEach-Object { $_.ToString('X2') })
if ($hex.Length -ne 48) {
    throw "24-byte ExRightFlag DB encoding produced $($hex.Length), expected 48"
}
[byte[]]$loaded = New-Object byte[] 24
for ($i = 0; $i -lt $loaded.Length; $i++) {
    $loaded[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
}
foreach ($bit in 141, 153) {
    if (($loaded[$bit -shr 3] -band (1 -shl ($bit -band 7))) -eq 0) {
        throw "ExRightFlag bit $bit did not survive the 24-byte DB round trip"
    }
}

if ((2 + 4 + 4 + 1 + 1 + 4 + 4 + 4 + 24) -ne 48) {
    throw 'Msg131 declared request length model is not 48'
}
if ((2 + 1 + 1 + 4 + 4 + 24) -ne 36) {
    throw 'Msg131 response payload length model is not 36'
}

Write-Host 'PASS: Msg131 request/response, login overwrite guard, DB round-trip, and Msg100 ExRightFlag[24] layout'
