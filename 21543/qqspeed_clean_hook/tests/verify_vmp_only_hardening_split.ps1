$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$plainDllPath = Join-Path $root 'x64\ReleasePlain\qqspeed_clean_hook_64.dll'
$vmpDllPath = Join-Path $root 'x64\ReleaseVmp\qqspeed_clean_hook_64.dll'

foreach ($dllPath in @($plainDllPath, $vmpDllPath)) {
    if (-not (Test-Path -LiteralPath $dllPath)) {
        throw "Build output missing: $dllPath"
    }
}

function Contains-Bytes([byte[]]$haystack, [byte[]]$needle) {
    for ($i = 0; $i -le $haystack.Length - $needle.Length; ++$i) {
        $same = $true
        for ($j = 0; $j -lt $needle.Length; ++$j) {
            if ($haystack[$i + $j] -ne $needle[$j]) {
                $same = $false
                break
            }
        }
        if ($same) { return $true }
    }
    return $false
}

function Assert-TextAbsent([byte[]]$bytes, [string]$text, [string]$imageName) {
    if ((Contains-Bytes $bytes ([Text.Encoding]::ASCII.GetBytes($text))) -or
        (Contains-Bytes $bytes ([Text.Encoding]::Unicode.GetBytes($text)))) {
        throw "$imageName contains forbidden residual: $text"
    }
}

$plainBytes = [IO.File]::ReadAllBytes($plainDllPath)
$vmpBytes = [IO.File]::ReadAllBytes($vmpDllPath)
$networkProfilePrefix = [Text.Encoding]::ASCII.GetBytes('Tu7oaFhOoS4ZPwbK6S9H')
$topKartProfile = [Text.Encoding]::ASCII.GetBytes('g7wAFC7RDCVRLxi/i69qpQ==')

if ((Contains-Bytes $plainBytes $networkProfilePrefix) -or
    (Contains-Bytes $vmpBytes $networkProfilePrefix)) {
    throw 'A production image still embeds the unused Network profile blob.'
}
if (Contains-Bytes $plainBytes $topKartProfile) {
    throw 'ReleasePlain embeds the protected Top-Kart profile blob.'
}
if (-not (Contains-Bytes $vmpBytes $topKartProfile)) {
    throw 'ReleaseVmp does not embed the protected Top-Kart profile blob.'
}

$rvas = @(
    @{ Name = 'execute'; Value = [uint32]0x342D90 },
    @{ Name = 'execute-slot'; Value = [uint32]0x2B55F98 },
    @{ Name = 'ACE-interface-global'; Value = [uint32]0x36C2D08 }
)
foreach ($rva in $rvas) {
    $bytes = [BitConverter]::GetBytes($rva.Value)
    if (-not (Contains-Bytes $plainBytes $bytes)) {
        throw "ReleasePlain no longer contains local $($rva.Name) RVA."
    }
    if (Contains-Bytes $vmpBytes $bytes) {
        throw "ReleaseVmp contains plaintext $($rva.Name) RVA."
    }
}

foreach ($marker in @(
    'Prod.Connect.RedirectDecision',
    'Prod.AceFeed.Validate',
    'Prod.AceFeed.Observe',
    'Prod.AntiChecker.ExecuteGate',
    'Prod.AntiChecker.ExchangeSlot',
    'Prod.AntiChecker.RestoreSlot',
    'Prod.AntiChecker.VerifySlots'
)) {
    $markerBytes = [Text.Encoding]::ASCII.GetBytes($marker)
    if (Contains-Bytes $plainBytes $markerBytes) {
        throw "ReleasePlain embeds a VMProtect marker: $marker"
    }
    if (-not (Contains-Bytes $vmpBytes $markerBytes)) {
        throw "ReleaseVmp is missing a VMProtect marker: $marker"
    }
}

foreach ($image in @(
    @{ Name = 'ReleasePlain'; Bytes = $plainBytes },
    @{ Name = 'ReleaseVmp'; Bytes = $vmpBytes }
)) {
    foreach ($residual in @(
        'qqspeed_clean_hook_',
        'qqspeed_packet_flow_',
        'OutputDebugStringA',
        'network_preflight',
        'hook initialization',
        'knownExcludedIds',
        'Login.Msg100',
        'Login.Msg551',
        'Msg100',
        'Msg551',
        '28346',
        'PacketFlow',
        'topkart_change_item',
        'topkart_worldbound',
        'process_exception_trace',
        'http://',
        'https://'
    )) {
        Assert-TextAbsent $image.Bytes $residual $image.Name
    }
}

Write-Host 'Plain/VMP production binary split verification passed: no logger/probe/Msg100/551/28346 residuals.'
