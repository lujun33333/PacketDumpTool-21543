param(
    [string]$ServerDirectory = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
$serverDir = [System.IO.Path]::GetFullPath($ServerDirectory)
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

$header = Get-Content -LiteralPath (Join-Path $serverDir 'Message.h') -Raw
$player = Get-Content -LiteralPath (Join-Path $serverDir 'Player.cpp') -Raw
$message = Get-Content -LiteralPath (Join-Path $serverDir 'Message.cpp') -Raw
$quickRpc = Get-Content -LiteralPath (Join-Path $serverDir 'MessageQuickRPC.cpp') -Raw
$config = Get-Content -LiteralPath (Join-Path $serverDir 'bin\24251\IllusionSuitCfg.yml') -Raw

if ($header -notmatch 'struct\s+QQSIllusionSuitVisibleInfo' -or
    $header -notmatch 'UINT\s+ItemID\s*=\s*0' -or
    $header -notmatch 'BYTE\s+ShapeIdx\s*=\s*0' -or
    $header -notmatch 'QQS_WriteEquippedIllusionSuitVisibleInfo') {
    Add-Failure 'shared illusion-suit visible wire model is missing'
}

if ($quickRpc -notmatch 'static\s+std::vector<IllusionSuitStatusRow>\s+IS_BuildVisibleStatusRows' -or
    $quickRpc -notmatch 'baseItem\s*&&\s*baseItem->Status' -or
    $quickRpc -notmatch 'IS_IsValidShapeIdx\(suit,\s*shapeIdx\)' -or
    $quickRpc -notmatch 'Write32\(pInfo,\s*row\.ItemID\)' -or
    $quickRpc -notmatch 'Write8\(pInfo,\s*row\.ShapeIdx\)' -or
    $quickRpc -notmatch 'Set16\(p,\s*static_cast<WORD>\(len\)\)') {
    Add-Failure 'visible IllusionSuitInfo is not serialized as len + ItemID + ShapeIdx'
}

if ($quickRpc -notmatch 'CREATE TABLE IF NOT EXISTS IllusionSuitState' -or
    $quickRpc -notmatch 'PRIMARY KEY \(Uin,ItemID\)' -or
    $quickRpc -notmatch 'IS_LoadShapeIdx' -or
    $quickRpc -notmatch 'IS_SaveShapeIdx') {
    Add-Failure 'illusion shape state is not isolated from generic ItemInfo attributes'
}

$setInfoStart = $quickRpc.IndexOf('NOINLINE void Request_Beta84SetIllusionSuitInfo(', [StringComparison]::Ordinal)
$setInfoEnd = $quickRpc.IndexOf('// ---------------------------------------------------------------------------', $setInfoStart + 1, [StringComparison]::Ordinal)
if ($setInfoStart -lt 0 -or $setInfoEnd -lt 0) {
    Add-Failure 'Beta84 SetIllusionSuitInfo section not found'
} else {
    $setInfo = $quickRpc.Substring($setInfoStart, $setInfoEnd - $setInfoStart)
    if ($setInfo -notmatch 'IS_SaveShapeIdx' -or
        $setInfo -notmatch 'QQS_NotifyIllusionSuitShapeChangedToRoom') {
        Add-Failure 'Beta84 shape selection is not persisted and refreshed to room peers'
    }
    if ($setInfo -match 'PlayerDB_UpdateItemOtherAttribute') {
        Add-Failure 'Beta84 still overwrites Item.OtherAttribute used by bag pin/star state'
    }
}

if ($message -notmatch 'ResponseDoBagItemShowOperate' -or
    $message -notmatch 'PlayerDB_UpdateItemOtherAttribute\(Client->Uin,\s*ItemID,\s*1\)') {
    Add-Failure 'bag pin/star state no longer owns Item.OtherAttribute'
}

$visibleWriterCalls = [regex]::Matches($player, 'QQS_WriteEquippedIllusionSuitVisibleInfo\s*\(').Count
if ($visibleWriterCalls -lt 5) {
    Add-Failure "IllusionSuitInfo writer is not connected to all 104/500/122/527 paths (calls=$visibleWriterCalls)"
}
if ($player -match 'Write8\([^;]+,\s*0\);\s*//IllusionSuitNum') {
    Add-Failure 'a player-visible path still hardcodes IllusionSuitNum=0'
}

$notifyStart = $player.IndexOf('void NotifyChangeItemStatus(', [StringComparison]::Ordinal)
$notifyEnd = $player.IndexOf('void RequestChangeItemStatus(', $notifyStart, [StringComparison]::Ordinal)
if ($notifyStart -lt 0 -or $notifyEnd -lt 0) {
    Add-Failure 'NotifyChangeItemStatus section not found'
} else {
    $notify = $player.Substring($notifyStart, $notifyEnd - $notifyStart)
    if ($notify -notmatch 'PlayerDB_SelectItems\(MyClient->Uin,\s*persistedItems\)' -or
        $notify -notmatch 'storedItem->OtherAttribute' -or
        $notify -notmatch 'QQS_WriteEquippedIllusionSuitVisibleInfo\(p,\s*MyClient->Uin\)' -or
        $notify -notmatch 'GetRoomSeatLimit\(Room\)') {
        Add-Failure 'msg527 does not preserve Item.OtherAttribute and send separate IllusionSuitInfo'
    }
}

foreach ($requiredId in @(149982, 149996, 167908, 167909, 167986, 167987)) {
    if ($config -notmatch "(?m)\b$requiredId\b") {
        Add-Failure "IllusionSuitCfg is missing required official mapping ID $requiredId"
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'IllusionSuitVisibleSyncRegression passed: isolated ShapeIdx state, preserved bag pin attributes, 104/500/122 visibility, and msg527 live refresh'
