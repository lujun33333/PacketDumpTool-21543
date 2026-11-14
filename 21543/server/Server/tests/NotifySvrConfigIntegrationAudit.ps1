$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$gameServer = Get-Content -LiteralPath (Join-Path $serverRoot 'GameServer.cpp') -Raw
$player = Get-Content -LiteralPath (Join-Path $serverRoot 'Player.cpp') -Raw
$main = Get-Content -LiteralPath (Join-Path $serverRoot 'main.cpp') -Raw
$messageHeader = Get-Content -LiteralPath (Join-Path $serverRoot 'Message.h') -Raw
$project = Get-Content -LiteralPath (Join-Path $serverRoot 'SpeedServer.vcxproj') -Raw
$config = Get-Content -LiteralPath (Join-Path $serverRoot 'bin\Config\NotifySvrConfig.yml') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Get-Section([string]$source, [string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [StringComparison]::Ordinal)
    if ($start -lt 0) { return '' }
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length, [StringComparison]::Ordinal)
    if ($end -lt 0) { return '' }
    return $source.Substring($start, $end - $start)
}

$notifySection = Get-Section $gameServer 'static void NotifySvrConfigWithProfile' 'void NotifySpeed2Cfg'
if ([string]::IsNullOrEmpty($notifySection)) {
    $failures.Add('structured NotifySvrConfig implementation is missing')
} else {
    foreach ($forbidden in @('WriteHex(', 'Uinew', 'memcpy(p, "\x', '0x0331BC24', 'KartRefitCardTypeNum', 'catch (...)')) {
        if ($notifySection.IndexOf($forbidden, [StringComparison]::Ordinal) -ge 0) {
            $failures.Add("legacy hardcoded/dead implementation remains in NotifySvrConfig section: $forbidden")
        }
    }
    if ($notifySection -notmatch 'QQSpeedNotifySvrConfig::SerializeTail\s*\(\s*profile\s*,\s*version\s*\)') {
        $failures.Add('NotifySvrConfig does not use the validated structured serializer')
    }
    if ($notifySection -notmatch 'case\s+3:[\s\S]*settings->config3') {
        $failures.Add('selector does not preserve Config3')
    }
    if ($notifySection -notmatch 'case\s+4:[\s\S]*settings->config4') {
        $failures.Add('selector does not expose Config4')
    }
}

if ($player -notmatch 'NotifySelectedSvrConfig\s*\(\s*Client\s*\)') {
    $failures.Add('login path does not call the YAML selector')
}
if ($player -match 'NotifySvrConfig3\s*\(\s*Client\s*\)\s*;\s*//NPC') {
    $failures.Add('login path still forces Config3')
}
if ($messageHeader -notmatch 'void\s+NotifySvrConfig4\s*\(' -or
    $messageHeader -notmatch 'void\s+NotifySelectedSvrConfig\s*\(') {
    $failures.Add('Config4/selector declarations are missing')
}
if ($main -notmatch 'InstallFile\s*\(\s*"\.\\\\Config\\\\NotifySvrConfig\.yml"\s*\)') {
    $failures.Add('startup does not strictly load NotifySvrConfig.yml')
}
if ($main -notmatch 'Load NotifySvrConfig Error:[\s\S]*return HR_ERROR') {
    $failures.Add('invalid initial YAML does not fail the listener startup')
}
if ($project -notmatch 'ClCompile Include="NotifySvrConfigProfile\.cpp"') {
    $failures.Add('SpeedServer project does not compile the structured profile module')
}
if ($config -notmatch '(?m)^SelectedVersion:\s+4\s*$') {
    $failures.Add('NotifySvrConfig.yml does not explicitly select Config4')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}

Write-Host 'NotifySvrConfig integration audit passed: YAML selects one profile, Config3 remains available, Config4 is structured, and legacy blobs/forced branches are absent.'
