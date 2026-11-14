$ErrorActionPreference = 'Stop'

$serverDir = Split-Path -Parent $PSScriptRoot
$welfarePath = Join-Path $serverDir 'WelfareActivity.cpp'
$messagePath = Join-Path $serverDir 'Message.cpp'
$messageHeaderPath = Join-Path $serverDir 'Message.h'

$welfare = Get-Content -LiteralPath $welfarePath -Raw
$message = Get-Content -LiteralPath $messagePath -Raw
$messageHeader = Get-Content -LiteralPath $messageHeaderPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-SourceSection(
    [string]$source,
    [string]$startMarker,
    [string]$endMarker
) {
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

$structuredResponse = Get-SourceSection `
    $welfare `
    'static void WF_Send(std::shared_ptr<ClientNode>' `
    'static void WF_SendNumberResponse(std::shared_ptr<ClientNode>'

$numberResponse = Get-SourceSection `
    $welfare `
    'static void WF_SendNumberResponse(std::shared_ptr<ClientNode>' `
    'static void WF_SendError(std::shared_ptr<ClientNode>'

$commonWrapper = Get-SourceSection `
    $message `
    'void MyResponseQuickRPC(std::shared_ptr<ClientNode>' `
    'void NotifySpeedLadderBreakMapRecordQuickRPC(std::shared_ptr<ClientNode>'

foreach ($entry in @(
    @{ Name = 'WF_Send'; Source = $structuredResponse },
    @{ Name = 'WF_SendNumberResponse'; Source = $numberResponse }
)) {
    if ($entry.Source -notmatch 'MyResponseQuickRPC\s*\(') {
        $failures.Add("$($entry.Name) bypasses the shared Msg24251 response wrapper")
    }
    if ($entry.Source -match 'SendToClient\s*\([^;]*24251') {
        $failures.Add("$($entry.Name) still assembles and sends a private Msg24251 envelope")
    }
    if ($entry.Source -match 'Write16\s*\([^;]*\)\s*;[\s\S]*Set16\s*\(') {
        $failures.Add("$($entry.Name) still owns outer Head/Body length fields")
    }
}

if ($messageHeader -notmatch 'void\s+MyResponseQuickRPC\s*\(\s*std::shared_ptr<ClientNode>\s+Client\s*,\s*BYTE\*\s*Msg\s*,\s*size_t\s+MsgLen\s*\)\s*;') {
    $failures.Add('Message.h does not expose the shared Msg24251 response wrapper')
}

if ($commonWrapper -notmatch 'Write16\s*\(\s*pBody\s*,\s*MsgLen\s*\)') {
    $failures.Add('shared Msg24251 wrapper no longer writes the exact inner MsgLen')
}
if ($commonWrapper -match 'memcpy\s*\(\s*pBody\s*,\s*Msg\s*,\s*MsgLen\s*\)\s*;[\s\S]*Write16\s*\(\s*pBody\s*,\s*0\s*\)') {
    $failures.Add('shared Msg24251 wrapper appends a non-official two-byte body trailer')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'Welfare24251WrapperRegression passed: welfare responses reuse the canonical Msg24251 envelope.'
