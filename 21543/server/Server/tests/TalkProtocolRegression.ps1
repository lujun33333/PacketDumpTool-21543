param(
    [string]$FixtureDirectory = (Join-Path $PSScriptRoot 'fixtures'),
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'

$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$roomCppPath = Join-Path $serverDir 'Room.cpp'
$roomHeaderPath = Join-Path $serverDir 'Room.h'
$playerCppPath = Join-Path $serverDir 'Player.cpp'
$messageCppPath = Join-Path $serverDir 'Message.cpp'
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Read-U16BE([byte[]]$bytes, [int]$offset) {
    if ($offset -lt 0 -or $offset + 2 -gt $bytes.Length) {
        throw "u16 read outside fixture at $offset"
    }
    return ([int]$bytes[$offset] -shl 8) -bor [int]$bytes[$offset + 1]
}

function Read-U32BE([byte[]]$bytes, [int]$offset) {
    if ($offset -lt 0 -or $offset + 4 -gt $bytes.Length) {
        throw "u32 read outside fixture at $offset"
    }
    return ([uint32]$bytes[$offset] -shl 24) -bor
           ([uint32]$bytes[$offset + 1] -shl 16) -bor
           ([uint32]$bytes[$offset + 2] -shl 8) -bor
           [uint32]$bytes[$offset + 3]
}

function Assert-BodyLength([byte[]]$bytes, [string]$label) {
    $declared = Read-U16BE $bytes 0
    if ($declared -ne $bytes.Length) {
        Add-Failure "$label BodyLen mismatch: declared=$declared actual=$($bytes.Length)"
    }
}

function Compare-Bytes([byte[]]$actual, [byte[]]$expected, [string]$label) {
    if ($actual.Length -ne $expected.Length) {
        Add-Failure "$label length mismatch: actual=$($actual.Length) expected=$($expected.Length)"
        return
    }
    for ($i = 0; $i -lt $actual.Length; ++$i) {
        if ($actual[$i] -ne $expected[$i]) {
            Add-Failure "$label first differs at offset $i`: actual=$('{0:X2}' -f $actual[$i]) expected=$('{0:X2}' -f $expected[$i])"
            return
        }
    }
}

function Parse-NotifyTalk([byte[]]$bytes) {
    Assert-BodyLength $bytes 'msg505'
    $offset = 2 + 4 + 17
    $gender = $bytes[$offset]; ++$offset
    $type = $bytes[$offset]; ++$offset
    $clientUseFlag = $bytes[$offset]; ++$offset
    $offset += 4
    $contentLength = Read-U16BE $bytes $offset
    $offset += 2 + $contentLength
    $bugletType = $bytes[$offset]; ++$offset
    $offset += 4 + 2 + 1
    $talkerInfoLength = Read-U16BE $bytes $offset
    if ($talkerInfoLength -lt 2 -or $offset + $talkerInfoLength -gt $bytes.Length) {
        throw "msg505 TalkerInfo length is invalid"
    }
    $offset += $talkerInfoLength
    $offset += 1 + 4
    if ($offset -gt $bytes.Length) {
        throw "msg505 fixed fields exceed fixture"
    }
    return [pscustomobject]@{
        Gender = $gender
        Type = $type
        ClientUseFlag = $clientUseFlag
        BugletType = $bugletType
        TailOffset = $offset
    }
}

function Find-BugletContentIdOffset([byte[]]$bytes) {
    Assert-BodyLength $bytes 'msg555'
    if ($bytes[2] -ne 1) {
        throw 'msg555 fixture does not contain exactly one BugletInfo'
    }
    $offset = 3
    $bugletInfoLength = Read-U16BE $bytes $offset
    if ($offset + $bugletInfoLength -ne $bytes.Length) {
        throw 'msg555 BugletInfo length mismatch'
    }
    $offset += 2 + 4 + 17 + 17 + 1
    $contentLength = Read-U16BE $bytes $offset
    $offset += 2 + $contentLength + 1 + 4 + 2
    if ($offset + 18 -gt $bytes.Length) {
        throw 'msg555 ContentId exceeds fixture'
    }
    return $offset
}

$fixtureNames = @(
    'talk_official_type1_msg505.bin',
    'talk_official_type3_msg505.bin',
    'talk_official_type3_msg111.bin',
    'talk_official_type3_msg555.bin',
    'talk_official_type3_msg556.bin'
)
$fixtures = @{}
foreach ($name in $fixtureNames) {
    $path = Join-Path $FixtureDirectory $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "talk fixture not found: $path"
    }
    $fixtures[$name] = [System.IO.File]::ReadAllBytes($path)
}

$type3View = Parse-NotifyTalk $fixtures['talk_official_type3_msg505.bin']
if ($type3View.Type -ne 3 -or $type3View.ClientUseFlag -ne 2 -or $type3View.BugletType -ne 11) {
    Add-Failure 'official type-3 msg505 discriminator is not 3/2/11'
}
$type1View = Parse-NotifyTalk $fixtures['talk_official_type1_msg505.bin']
if ($type1View.Type -ne 1 -or $type1View.ClientUseFlag -ne 2 -or $type1View.BugletType -ne 255) {
    Add-Failure 'official type-1 msg505 discriminator is not 1/2/255'
}

$expectedTail = [System.Collections.Generic.List[byte]]::new()
$contentId = New-Object byte[] 18
[System.Text.Encoding]::ASCII.GetBytes('1782568436587').CopyTo($contentId, 0)
$expectedTail.AddRange($contentId)
$expectedTail.AddRange([byte[]](0, 0, 0))       # transBufType + transBufLen
$expectedTail.Add(0)                            # MsgSource
$expectedTail.AddRange([byte[]](0, 0))          # DstNickNameLength
$expectedTail.AddRange([byte[]](0, 0, 0, 0))    # Seq
$expectedTail.AddRange([byte[]](0, 0, 0, 0))    # PresentItemID
$expectedTail.Add(0)                            # AutoMsg
$expectedTail.AddRange([byte[]](0, 6))          # TalkExtraData length
$expectedTail.AddRange([byte[]](0, 0, 0, 0))    # AtUin
$officialType3Notify = $fixtures['talk_official_type3_msg505.bin']
$actualTail = [byte[]]$officialType3Notify[$type3View.TailOffset..($officialType3Notify.Length - 1)]
Compare-Bytes $actualTail $expectedTail.ToArray() 'official msg505 protocol tail'

$officialResponse = $fixtures['talk_official_type3_msg111.bin']
Assert-BodyLength $officialResponse 'msg111'
if ($officialResponse.Length -ne 19 -or $officialResponse[4] -ne 3 -or
    $officialResponse[5] -ne 2 -or $officialResponse[11] -ne 11) {
    Add-Failure 'official msg111 response fields/length changed'
}

$useItemReference = $fixtures['talk_official_type3_msg556.bin']
Assert-BodyLength $useItemReference 'msg556 reference'
if ($useItemReference.Length -ne 18 -or
    (Read-U32BE $useItemReference 2) -ne 1045335392 -or
    (Read-U32BE $useItemReference 6) -ne 64294 -or
    (Read-U32BE $useItemReference 10) -ne 1 -or
    (Read-U32BE $useItemReference 14) -ne 1133) {
    Add-Failure 'msg556 reference is not Uin/item/use/current = 1045335392/64294/1/1133'
}

$officialBuglet = $fixtures['talk_official_type3_msg555.bin']
$bugletContentIdOffset = Find-BugletContentIdOffset $officialBuglet
$bugletContentId = [byte[]]$officialBuglet[$bugletContentIdOffset..($bugletContentIdOffset + 17)]
Compare-Bytes $bugletContentId $contentId 'official msg555 ContentId'

$roomSource = Get-Content -LiteralPath $roomCppPath -Raw
$roomHeader = Get-Content -LiteralPath $roomHeaderPath -Raw
$playerSource = Get-Content -LiteralPath $playerCppPath -Raw
$messageSource = Get-Content -LiteralPath $messageCppPath -Raw

$officialWireTotals = [ordered]@{
    'msg505' = @($fixtures['talk_official_type1_msg505.bin'].Length, 185)
    'msg111' = @($officialResponse.Length, 51)
    'msg555' = @($officialBuglet.Length, 157)
}
foreach ($entry in $officialWireTotals.GetEnumerator()) {
    $bodyLength = [int]$entry.Value[0]
    $expectedTotal = [int]$entry.Value[1]
    if (18 + 14 + $bodyLength -ne $expectedTotal) {
        Add-Failure "$($entry.Key) official wire total is not CSHEAD(18) + MSGHEAD(14) + body"
    }
}
if ($messageSource -notmatch 'static_assert\(sizeof\(QQS_MSGHEAD14\)\s*==\s*14' -or
    $messageSource -notmatch 'sizeof\(CSHEAD\)\s*\+\s*sizeof\(QQS_MSGHEAD14\)\s*\+\s*Length' -or
    $messageSource -notmatch 'ProtocolHead->BodyFlag\s*=\s*0' -or
    $messageSource -notmatch 'effectiveWireDstID\s*=\s*\(WireDstID\s*==\s*0\)') {
    Add-Failure 'SendToClientOfficial14 does not preserve the official 18+14+body clear envelope/broadcast route'
}

if (Test-Path -LiteralPath (Join-Path $serverDir 'TalkProtocol.h')) {
    Add-Failure 'TalkProtocol.h must not be added; helpers belong in existing source files'
}

$officialMappings = [ordered]@{
    0 = 10207; 1 = 14727; 2 = 14728; 3 = 18817;
    4 = 23976; 5 = 24923; 7 = 27139; 10 = 31998;
    11 = 64294; 12 = 66641; 13 = 68144; 14 = 68143
}
foreach ($entry in $officialMappings.GetEnumerator()) {
    $pattern = "case\s+$($entry.Key)\s*:\s*return\s+$($entry.Value)\s*;"
    if ($roomSource -notmatch $pattern) {
        Add-Failure "Room.cpp is missing official BugletType $($entry.Key) -> item $($entry.Value)"
    }
}
if ($roomSource -notmatch 'talkType\s*==\s*1\s*\?\s*static_cast<BYTE>\(0xFF\)') {
    Add-Failure 'ordinary msg505 text is not normalized to official BugletType 0xFF'
}
if ($roomSource -notmatch 'std::array<BYTE,\s*18>\s+QQS_MakeTalkContentId' -or
    $roomSource -notmatch 'system_clock::now' -or $roomSource -notmatch 'std::to_chars') {
    Add-Failure 'Room.cpp does not generate the official 18-byte epoch-millisecond ContentId'
}

$type3Start = $roomSource.IndexOf('else if (Type == 3)', [StringComparison]::Ordinal)
$type3End = $roomSource.IndexOf('else if (Type == 1 || Type == 2 || Type == 4)', $type3Start, [StringComparison]::Ordinal)
if ($type3Start -lt 0 -or $type3End -lt 0) {
    Add-Failure 'RequestTalk type-3 branch not found'
} else {
    $type3Branch = $roomSource.Substring($type3Start, $type3End - $type3Start)
    $orderedCalls = @('NotifyTalk(', 'NotifyUseItemResult2(', 'ResponseTalk(', 'NotifyBugletTalkInternal(')
    $lastIndex = -1
    foreach ($call in $orderedCalls) {
        $index = $type3Branch.IndexOf($call, [StringComparison]::Ordinal)
        if ($index -lt 0 -or $index -le $lastIndex) {
            Add-Failure "type-3 response flow is not official 505 -> 556 -> 111 -> 555 at $call"
            break
        }
        $lastIndex = $index
    }
    if ($type3Branch -notmatch 'const\s+auto\s+contentId\s*=\s*QQS_MakeTalkContentId\(\)' -or
        $type3Branch -notmatch 'NotifyTalk\([\s\S]*?contentId\.data\(\)' -or
        $type3Branch -notmatch 'NotifyBugletTalkInternal\([\s\S]*?contentId\)') {
        Add-Failure 'type-3 msg505/msg555 do not share one official ContentId'
    }
}

$responseStart = $roomSource.IndexOf('void ResponseTalk(', [StringComparison]::Ordinal)
$notifyStart = $roomSource.IndexOf('void NotifyTalk(', $responseStart, [StringComparison]::Ordinal)
$notifyEnd = $roomSource.IndexOf('static void NotifyChangeRoomOwnerInternal', $notifyStart, [StringComparison]::Ordinal)
if ($responseStart -lt 0 -or $notifyStart -lt 0 -or $notifyEnd -lt 0) {
    Add-Failure 'ResponseTalk/NotifyTalk source sections not found'
} else {
    $responseSource = $roomSource.Substring($responseStart, $notifyStart - $responseStart)
    if ($responseSource -notmatch 'pBodyLen' -or $responseSource -notmatch 'Set16\(pBodyLen' -or
        $responseSource -notmatch 'SendToClientOfficial14\(Client,\s*111' -or
        $responseSource -notmatch 'Client->ConnID,\s*FE_PLAYER') {
        Add-Failure 'msg111 lacks body self-length, Official14 envelope, or FE_PLAYER route'
    }

    $notifySource = $roomSource.Substring($notifyStart, $notifyEnd - $notifyStart)
    $tailTokens = @('//ContentId[18]', '//transBufType', '//transBufLen', '//MsgSource',
        '//DstNickNameLength', '//Seq', '//PresentItemID', '//AutoMsg', '//TalkExtraData len', '//AtUin')
    $lastIndex = -1
    foreach ($token in $tailTokens) {
        $index = $notifySource.IndexOf($token, [StringComparison]::Ordinal)
        if ($index -lt 0 -or $index -le $lastIndex) {
            Add-Failure "msg505 tail field is missing or out of order: $token"
            break
        }
        $lastIndex = $index
    }
    if ($notifySource -notmatch 'memcpy\(p,\s*ContentId,\s*18\)' -or
        $notifySource -notmatch 'Set16\(p,\s*static_cast<WORD>\(talkExtraDataLen\)\)' -or
        $notifySource -notmatch 'SendToClientOfficial14\(Client,\s*505' -or
        $notifySource -notmatch 'Client->ConnID,\s*FE_PLAYER') {
        Add-Failure 'msg505 ContentId, TalkExtraData, Official14 envelope, or FE_PLAYER route is missing'
    }
    if ($notifySource -notmatch 'RoomClient->Experience' -or
        $notifySource -notmatch 'RoomClient->GuildID') {
        Add-Failure 'msg505 TalkerInfo does not carry available Exp/GuildId values'
    }
}

$bugletStart = $roomSource.IndexOf('static void NotifyBugletTalkInternal(', [StringComparison]::Ordinal)
$bugletEnd = $roomSource.IndexOf('static BYTE GetQuickbugLeaveGameStageIndex', $bugletStart, [StringComparison]::Ordinal)
if ($bugletStart -lt 0 -or $bugletEnd -lt 0) {
    Add-Failure 'NotifyBugletTalk source section not found'
} else {
    $bugletSource = $roomSource.Substring($bugletStart, $bugletEnd - $bugletStart)
    if ($bugletSource -notmatch 'Client->GuildName' -or
        $bugletSource -notmatch 'QQS_MakeTalkContentId\(\)' -or
        $bugletSource -notmatch 'SendToClientOfficial14\(DstClient,\s*555') {
        Add-Failure 'msg555 lacks guild name, ContentId, or Official14 envelope'
    }
}

$useItemStart = $playerSource.IndexOf('void NotifyUseItemResult(std::shared_ptr<ClientNode>', [StringComparison]::Ordinal)
$useItemEnd = $playerSource.IndexOf('void RequestRefitKart(', $useItemStart, [StringComparison]::Ordinal)
if ($useItemStart -lt 0 -or $useItemEnd -lt 0) {
    Add-Failure 'NotifyUseItemResult source section not found'
} else {
    $useItemSource = $playerSource.Substring($useItemStart, $useItemEnd - $useItemStart)
    if ($useItemSource -match 'pBodyLen' -or $useItemSource -match 'Set16\(pBodyLen' -or
        $useItemSource -notmatch 'SendToClientWithWireDstID\(Client,\s*556' -or
        $useItemSource -match 'SendToClientOfficial14\(Client,\s*556' -or
        ([regex]::Matches($useItemSource, 'Write32\(p,').Count -ne 4) -or
        $useItemSource -notmatch 'len\s*=\s*p\s*-\s*buf\.data\(\)') {
        Add-Failure 'msg556 does not preserve the encrypted 16-byte business-body transport'
    }
}

if ($roomHeader -notmatch 'ResponseTalk\([^;]+BYTE\s+BugletType\)' -or
    $roomHeader -notmatch 'NotifyTalk\([^;]+BYTE\s+BugletType,\s*short\s+ContentLength[^;]+const\s+BYTE\*\s+ContentId') {
    Add-Failure 'Room.h talk declarations do not propagate BugletType'
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'TalkProtocolRegression passed: official msg505/111/555 envelope, encrypted msg556 body, routes, response order, ContentId, and 12 horn mappings'
