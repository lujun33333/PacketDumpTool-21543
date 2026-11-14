$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\pet.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$playerSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Player.cpp') -Raw
$messageSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Message.cpp') -Raw
$databaseSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Database.cpp') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-SourceSection([string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "missing source marker: $startMarker"
    }
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "missing source marker: $endMarker"
    }
    return $source.Substring($start, $end - $start)
}

$sender = Get-SourceSection 'static BOOL QQS_SendPetStructuredBody' 'static void QQS_SendPetConfigNotify'
$show = Get-SourceSection 'void ResponseGetPetShowInfo' 'void ResponseGetPetShowMoreInfo'
$showMore = Get-SourceSection 'void ResponseGetPetShowMoreInfo' 'static bool QQS_HandlePetLobbyPKCfgRequest'
$pk = Get-SourceSection 'void ResponseGetPKPetCfg' 'void NotifyGetPKPetMoreCfg'
$pkMore = Get-SourceSection 'void NotifyGetPKPetMoreCfg' 'void RequestGetPKPetInfo'
$pkPetLoader = Get-SourceSection 'static std::vector<PetInfo> QQS_LoadPlayerPKPetsFromDB' 'static UCHAR QQS_PKSkillType'
$pkRequest = Get-SourceSection 'void RequestGetPKPetInfo' 'void ResponseGetPKPetInfo'
$pkResponse = Get-SourceSection 'void ResponseGetPKPetInfo' 'void RequestEmbattle'
$embattle = Get-SourceSection 'void RequestEmbattle' 'void RequestPetTrialInfo'
$petComplete = Get-SourceSection 'bool QQS_CompletePetInfo' 'bool QQS_LoadPetInfo'
$petName = Get-SourceSection 'void NotifyPetUpdateName' 'void NotifyBroadcastPetEvolution'
$petStatus = Get-SourceSection 'void RequestChangePetEquipStatus' 'void NotifyBroadcastPetEvolution'
$petBroadcast = Get-SourceSection 'void NotifyBroadcastPetEvolution' 'void NotifyPetStrengCfgInfo'
$petEvolution = Get-SourceSection 'void NotifyPetEvolution' 'static void QQS_ResponseGetPetInfoList'
$petInfo = Get-SourceSection 'static void QQS_ResponseGetPetInfoList' 'void RequestGetPetInfo'

if ($sender -notmatch 'SendToClient\s*\(' -or
    $sender -notmatch 'body\s*\+\s*sizeof\(USHORT\)' -or
    $sender -notmatch 'logicalLen\s*-\s*sizeof\(USHORT\)' -or
    $sender -notmatch 'msgType\s*,\s*1\s*\)') {
    $failures.Add('pet structured packets must use the encrypted MSGHEAD/BodyLen transport')
}
if ($sender -match 'SendToClientOfficial14') {
    $failures.Add('pet structured sender still uses the plaintext 14-byte transport')
}
foreach ($section in @($show, $showMore, $pk, $pkMore)) {
    if ($section -notmatch 'QQS_SendPetStructuredBody') {
        $failures.Add('a pet show/PK response bypasses the structured encrypted sender')
    }
    if ($section -match 'SendToClientOfficial14') {
        $failures.Add('a pet show/PK response still uses the plaintext 14-byte transport')
    }
}
foreach ($section in @($petName, $petBroadcast, $petEvolution, $petInfo)) {
    if ($section -notmatch 'QQS_SendPetStructuredBody') {
        $failures.Add('a dynamic pet response bypasses the structured encrypted sender')
    }
    if ($section -match 'SendToClientOfficial14') {
        $failures.Add('a dynamic pet response still uses the plaintext 14-byte transport')
    }
}
if ($pk -notmatch 'QQS_SendPetStructuredBody[\s\S]*NotifyGetPKPetMoreCfg\s*\(\s*Client\s*\)') {
    $failures.Add('21140 must be followed by the 21149 chunks')
}
if ($pk -match 'if\s*\(\s*QQS_SendPetStructuredBody') {
    $failures.Add('21149 delivery must not depend on a sender return value')
}
if ($pk -notmatch 'QQS_LoadPetPKHexBody\s*\(\s*"Response21140Hex"' -or
    $pk -notmatch 'QQS_PatchPetPKUin') {
    $failures.Add('21140 must use the known-good raw official body and patch the current UIN')
}
if ($pkMore -notmatch 'QQS_LoadPetPKHexBodyList\s*\(\s*"Response21149HexList"') {
    $failures.Add('21149 must use the known-good raw official chunk list')
}
if ($pk -match 'BuildPKGlobalBody' -or $pkMore -match 'PKMoreBodies') {
    $failures.Add('PK responses regressed to the incompatible five-table reserialization')
}
if ($source -notmatch '(?s)static\s+UCHAR\s+QQS_GetPetTopStatusForLevel\s*\([^\)]*\).*?level\s*>=\s*20.*?return\s+3.*?level\s*>=\s*11.*?return\s+2.*?return\s+1' -or
    $petComplete -notmatch 'pet\.TopStatus\s*=\s*QQS_GetPetTopStatusForLevel\s*\(\s*level\s*\)') {
    $failures.Add('dynamic pet info must derive TopStatus from level so the client can expose the form button')
}
if ($pkRequest -notmatch 'BodyLen\s*!=\s*27' -or
    $pkRequest -notmatch 'const\s+USHORT\s+selfLen\s*=\s*Read16\s*\(\s*p\s*\)' -or
    $pkRequest -notmatch 'const\s+UINT\s+requestUin\s*=\s*Read32\s*\(\s*p\s*\)' -or
    $pkRequest -notmatch 'reserved0\s*!=\s*0\s*\|\|\s*reserved1\s*!=\s*0' -or
    $pkRequest -notmatch 'ResponseGetPKPetInfo\s*\(') {
    $failures.Add('21133 request must parse the exact current 27-byte self-length body and reject nonzero reserved fields')
}
if ($pkResponse -match 'not sent' -or
    $pkResponse -notmatch 'QQS_SendPetStructuredBody\s*\(\s*Client\s*,\s*21133' -or
    $pkResponse -notmatch 'QQS_WritePKPetArrayInfo') {
    $failures.Add('21133 must return a structured player PK response including formation data')
}
if ($messageSource -notmatch '(?s)case\s+21120\s*:.*?RequestEmbattle\s*\(\s*Client\s*,\s*Body\s*,\s*BodyLen\s*\)') {
    $failures.Add('21120 pet formation requests are not dispatched')
}
if ($embattle -notmatch 'BodyLen\s*!=\s*58' -or
    $embattle -notmatch 'const\s+USHORT\s+selfLen\s*=\s*Read16\s*\(\s*p\s*\)' -or
    $embattle -notmatch 'QQS_BuildAutoPKFormation' -or
    $embattle -notmatch 'QQS_SendPetStructuredBody\s*\(\s*Client\s*,\s*21120') {
    $failures.Add('21120 must validate the exact current request, apply formation state, and return the structured official response')
}
if ($pkPetLoader -notmatch 'SELECT P\.PetId,P\.Name,P\.Experience,P\.PL,P\.Status,P\.StrengLevel' -or
    $pkPetLoader -match 'QQS_LoadPetInfo\s*\(') {
    $failures.Add('PK pet loading must remain one joined ownership query instead of an N+1 pet lookup')
}
if ($databaseSource -notmatch 'CREATE TABLE IF NOT EXISTS PetPKFormation' -or
    $source -notmatch 'INSERT INTO PetPKFormation' -or
    $source -notmatch 'QQSPKFormationLoadResult::Error') {
    $failures.Add('formation persistence must use the single PetPKFormation table and fail closed on database errors')
}
if ($playerSource -notmatch '(?s)void\s+NotifyUseItemResult\s*\([^\)]*\).*?SendToClientWithWireDstID\s*\(\s*Client\s*,\s*556' -or
    $playerSource -match '(?s)void\s+NotifyUseItemResult\s*\([^\)]*\).*?SendToClientOfficial14\s*\(\s*Client\s*,\s*556') {
    $failures.Add('556 must remain the encrypted 16-byte business-body notification used by pet transform cards')
}
if ($messageSource -notmatch '(?s)case\s+15398\s*:.*?RequestChangePetEquipStatus\s*\(\s*Client\s*,\s*Body\s*,\s*BodyLen\s*\)') {
    $failures.Add('15398 pet form/status requests are not dispatched')
}
if ($source -notmatch '(?s)void\s+RequestChangePetEquipStatus\s*\([^\)]*\).*?UPDATE Pet SET Status=\?') {
    $failures.Add('15398 handler does not persist the selected pet form/status')
}
if ($petStatus -notmatch 'newStatus\s*>\s*5' -or
    $petStatus -notmatch 'QQS_IsPetFormStatusAllowed\s*\(\s*newStatus\s*,\s*pet\.StrengLevel\s*\)') {
    $failures.Add('15398 handler must accept status 5 only through the +10 form guard')
}
if ($messageSource -notmatch '(?s)case\s+20101\s*:.*?RequestPetStrengthen\s*\(\s*Client\s*,\s*Body\s*,\s*BodyLen\s*\)') {
    $failures.Add('20101 pet strengthen requests are not dispatched')
}
if ($messageSource -notmatch '(?s)void\s+RequestPetStrengthen\s*\([^\)]*\).*?PlayerDB_ApplyPetStrengthenAttempt\s*\(' -or
    $messageSource -notmatch '(?s)void\s+ResponsePetStrengthen\s*\([^\)]*\).*?SendToClient\s*\(\s*Client\s*,\s*20101') {
    $failures.Add('20101 strengthen handler/official response chain is incomplete')
}
if ($source -notmatch '(?s)bool\s+GetStrengthLevelRule\s*\([^\)]*\).*?SuccProb.*?ConsCardNum.*?ConsSuperMoney') {
    $failures.Add('runtime PetStrength table is not used for strengthen probability/cost')
}
if ($databaseSource -notmatch '(?s)bool\s+PlayerDB_ApplyPetStrengthenAttempt\s*\([^\)]*\).*?setAutoCommit\s*\(\s*false\s*\).*?FOR UPDATE.*?commit\s*\(') {
    $failures.Add('pet strengthen deductions and level update are not one database transaction')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'PetConfigTransportRegression passed'
