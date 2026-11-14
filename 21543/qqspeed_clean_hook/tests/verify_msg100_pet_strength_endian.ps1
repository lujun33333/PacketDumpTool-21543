$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'network\msg100_login_plain.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

$functionMatch = [regex]::Match(
    $source,
    '(?s)bool WritePetRecordsPlain\(.*?\n\}\r?\n\r?\n(?:QQSPEED_VMP_NOINLINE )?bool WriteOverdueRecordsPlain\('
)
if (-not $functionMatch.Success) {
    throw "Unable to locate WritePetRecordsPlain in $sourcePath"
}

$body = $functionMatch.Value
$rawCopyPattern = '(?s)ReadBytesFromStream\(stream,\s*streamLen,\s*pos,\s*bytes,\s*5\).*?memcpy\(out \+ 179,\s*bytes,\s*5\)'
if ([regex]::IsMatch($body, $rawCopyPattern)) {
    throw 'StrengthLevel is still copied as raw big-endian bytes into little-endian client memory.'
}

$decodedPattern = '(?s)ReadBE32FromStream\(stream,\s*streamLen,\s*pos,\s*value32\).*?WriteLE32Raw\(out \+ 179,\s*value32\).*?ReadU8FromStream\(stream,\s*streamLen,\s*pos,\s*value8\).*?out\[183\]\s*=\s*value8'
if (-not [regex]::IsMatch($body, $decodedPattern)) {
    throw 'StrengthLevel does not have the required BE32-to-LE32 conversion.'
}

$wireBytes = [byte[]](0x00, 0x00, 0x00, 0x0A)
$rawClientValue = [BitConverter]::ToUInt32($wireBytes, 0)
if ($rawClientValue -ne 167772160) {
    throw "Unexpected raw-copy reproduction value: $rawClientValue"
}

$fixedClientBytes = [byte[]](0x0A, 0x00, 0x00, 0x00)
$fixedClientValue = [BitConverter]::ToUInt32($fixedClientBytes, 0)
if ($fixedClientValue -ne 10) {
    throw "Unexpected converted value: $fixedClientValue"
}

Write-Host 'Msg100 pet StrengthLevel endian regression verification passed.'
