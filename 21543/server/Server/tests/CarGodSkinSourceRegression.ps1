$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $root 'GameServer.cpp') -Raw
$begin = $source.IndexOf('void NotifyGameBeginCS(')
$end = $source.IndexOf('static UINT ResolveKartPhysLoadID(', $begin)
if ($begin -lt 0 -or $end -le $begin) { throw 'Challenge 509 source not found' }
$challenge = $source.Substring($begin, $end - $begin)
if ($challenge -notmatch 'player\.kartId\s*=\s*GetVisibleKartForStone\(Client\);' -or
    $challenge -notmatch 'player\.stoneKartId\s*=\s*player\.kartId;' -or
    $challenge -match 'player\.kartId\s*=\s*Client->KartID;') {
    throw 'Challenge 509 does not use the same visible skin selection as normal races'
}
if ($source -notmatch 'return Player->KartSkinID \? Player->KartSkinID : Player->KartID;' -or
    $source -notmatch 'player\.kartId = GetVisibleKartForStone\(source\);') {
    throw 'Shared visible kart selection or normal mode binding changed'
}
if ($source -notmatch 'if \(Player->KartSkinID > 0\)\s+LoadID = Player->KartSkinID;' -or
    $source -notmatch 'TryBuildKartPhysPlainForPlayer\(loadID, srcPlayer, DstClient, playerConfig\)') {
    throw '551 no longer loads the equipped skin profile'
}
foreach ($marker in @('[CHALLENGE-SKIN-509]', '[CHALLENGE-SKIN-551]')) {
    if (-not $source.Contains($marker)) { throw "Missing diagnostic: $marker" }
}
if ($challenge -notmatch 'fields\.gameBeginSwitchFlag\s*=\s*0x009133B0u;' -or
    $challenge -notmatch 'SuperJetDisabled=%u') {
    throw 'CarGod must clear only DisableKartSuperJet (0x40) and log the sent flag'
}
Write-Output 'PASS: CarGod and normal 509 share skin-first selection; no-skin uses base; 551 remains skin-first; CarGod DisableKartSuperJet cleared; diagnostic markers present.'
