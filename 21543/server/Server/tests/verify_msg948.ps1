param(
    [string]$PlayerCpp = (Join-Path $PSScriptRoot '..\Player.cpp')
)

$source = Get-Content -LiteralPath $PlayerCpp -Raw
$match = [regex]::Match(
    $source,
    'void\s+NotifyClientVipFlag\s*\([^)]*\)\s*\{(?<body>.*?)\r?\n\}\r?\n\r?\nvoid\s+RequestInlaySkillStone',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

if (-not $match.Success) {
    throw "NotifyClientVipFlag not found in $PlayerCpp"
}

$writes = [regex]::Matches(
    $match.Groups['body'].Value,
    'Write(?:16|32)\(p,\s*[^;]+\);'
) | ForEach-Object { ($_.Value -replace '\s+', ' ').Trim() }

$expectedWrites = @(
    'Write32(p, Client->Uin);',
    'Write16(p, vipFlag);',
    'Write16(p, 0);',
    'Write16(p, vipGrowRate);'
)

if (($writes -join "`n") -ne ($expectedWrites -join "`n")) {
    throw "Msg948 serializer mismatch.`nExpected:`n$($expectedWrites -join "`n")`nActual:`n$($writes -join "`n")"
}

$sample = [byte[]](
    0x00, 0x00, 0x27, 0x11, # Uin = 10001
    0x01, 0x07,             # VipFlag = 0x0107
    0x00, 0x00,             # MsgLen = 0
    0x00, 0x14              # VipGrowRate = 20
)
$hex = ($sample | ForEach-Object { $_.ToString('X2') }) -join ''

if ($sample.Length -ne 10 -or $hex -ne '00002711010700000014') {
    throw "Unexpected Msg948 reference body: len=$($sample.Length), hex=$hex"
}

Write-Output "Msg948 regression check passed: bodyLen=10 hex=$hex"
