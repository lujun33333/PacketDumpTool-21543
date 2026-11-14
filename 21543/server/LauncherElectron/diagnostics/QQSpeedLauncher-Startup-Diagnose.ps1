[CmdletBinding()]
param(
    [string]$LauncherPath,
    [ValidateRange(3, 120)]
    [int]$ObserveSeconds = 25,
    [switch]$NoLaunch,
    [string]$OutputDirectory
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$startedAt = Get-Date
$stamp = $startedAt.ToString('yyyyMMdd-HHmmss')
$runId = "diagnostic-$stamp-$PID"
$localAppData = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:TEMP }
$runRoot = Join-Path $localAppData "QQSpeedLauncher\diagnostics\$stamp-$PID"
$stableLogPath = Join-Path $localAppData 'QQSpeedLauncher\logs\startup.log'
$summaryPath = Join-Path $runRoot 'summary.txt'
$processPath = Join-Path $runRoot 'processes.jsonl'
$eventPath = Join-Path $runRoot 'events.txt'

New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

function Protect-Text {
    param([AllowNull()][object]$Value, [int]$MaximumLength = 2000)
    $text = [string]$Value
    if ($env:USERPROFILE) {
        $text = [regex]::Replace(
            $text,
            [regex]::Escape($env:USERPROFILE),
            '%USERPROFILE%',
            [Text.RegularExpressions.RegexOptions]::IgnoreCase
        )
    }
    $text = [regex]::Replace(
        $text,
        '(?i)\b(password|passwd|pwd|token|ticket|cookie|authorization|secret)\b\s*[:=]\s*(?:"[^"]*"|''[^'']*''|[^\s,;]+)',
        '$1=[redacted]'
    )
    if ($text.Length -gt $MaximumLength) {
        return $text.Substring(0, $MaximumLength)
    }
    return $text
}

function Add-SummaryLine {
    param([string]$Name, [AllowNull()][object]$Value)
    $safeValue = Protect-Text $Value 1000
    Add-Content -LiteralPath $summaryPath -Encoding UTF8 -Value ("{0}={1}" -f $Name, $safeValue)
}

function Add-JsonLine {
    param([string]$Path, [object]$Record)
    $Record | ConvertTo-Json -Compress -Depth 5 | Add-Content -LiteralPath $Path -Encoding UTF8
}

function Get-PeMachine {
    param([string]$Path)
    $stream = $null
    $reader = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            return 'NOT_PE'
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) {
            return 'INVALID_PE'
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            return 'INVALID_PE'
        }
        $machine = $reader.ReadUInt16()
        if ($machine -eq 0x014C) { return 'x86' }
        if ($machine -eq 0x8664) { return 'x64' }
        if ($machine -eq 0xAA64) { return 'arm64' }
        return ('0x{0:X4}' -f $machine)
    } catch {
        return 'UNAVAILABLE'
    } finally {
        if ($reader) { $reader.Dispose() }
        elseif ($stream) { $stream.Dispose() }
    }
}

function Find-Launcher {
    param([string]$RequestedPath)
    if ($RequestedPath) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }
    $roots = @(
        $PSScriptRoot,
        (Split-Path -Parent $PSScriptRoot),
        (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
    ) | Select-Object -Unique
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        $candidate = Get-ChildItem -LiteralPath $root -Filter 'QQSpeedLauncher-Portable-*.exe' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    return $null
}

function Export-RelatedEvents {
    param(
        [string]$LogName,
        [int[]]$Ids,
        [datetime]$StartTime
    )
    $count = 0
    try {
        $events = Get-WinEvent -FilterHashtable @{ LogName = $LogName; Id = $Ids; StartTime = $StartTime } -ErrorAction Stop
        foreach ($event in $events) {
            $message = Protect-Text $event.Message
            if ($message -notmatch '(?i)QQSpeedLauncher|QQSpeedNativeBridge|qqspeed_clean_hook') {
                continue
            }
            Add-Content -LiteralPath $eventPath -Encoding UTF8 -Value (
                "[{0:o}] log={1} id={2} provider={3} level={4} message={5}" -f
                $event.TimeCreated, $LogName, $event.Id, $event.ProviderName, $event.LevelDisplayName, $message
            )
            $count++
        }
    } catch {
        $errorText = Protect-Text $_.Exception.Message 500
        if ($errorText -match '(?i)No events were found|specified selection criteria') {
            Add-Content -LiteralPath $eventPath -Encoding UTF8 -Value "NONE log=$LogName"
        } else {
            Add-Content -LiteralPath $eventPath -Encoding UTF8 -Value "UNAVAILABLE log=$LogName error=$errorText"
        }
    }
    return $count
}

Set-Content -LiteralPath $summaryPath -Encoding UTF8 -Value 'QQSpeedLauncher startup diagnostics'
Set-Content -LiteralPath $eventPath -Encoding UTF8 -Value 'QQSpeedLauncher related Windows events'
Set-Content -LiteralPath $processPath -Encoding UTF8 -Value ''
Add-SummaryLine 'RunId' $runId
Add-SummaryLine 'StartedAt' $startedAt.ToString('o')
Add-SummaryLine 'PowerShell' $PSVersionTable.PSVersion.ToString()
Add-SummaryLine 'ProcessArchitecture' $env:PROCESSOR_ARCHITECTURE

$operatingSystemIs64Bit = [Environment]::Is64BitOperatingSystem
Add-SummaryLine 'OperatingSystemIs64Bit' $operatingSystemIs64Bit
$wow64CmdPath = Join-Path $env:WINDIR 'SysWOW64\cmd.exe'
$wow64CmdExists = Test-Path -LiteralPath $wow64CmdPath -PathType Leaf
$wow64CmdLaunch = $false
Add-SummaryLine 'Wow64CmdExists' $wow64CmdExists
if ($operatingSystemIs64Bit -and $wow64CmdExists) {
    try {
        $wow64Probe = Start-Process -FilePath $wow64CmdPath -ArgumentList '/d', '/c', 'exit 0' -WindowStyle Hidden -Wait -PassThru -ErrorAction Stop
        $wow64CmdLaunch = $wow64Probe.ExitCode -eq 0
        Add-SummaryLine 'Wow64CmdExitCode' $wow64Probe.ExitCode
    } catch {
        Add-SummaryLine 'Wow64CmdError' $_.Exception.Message
    }
}
Add-SummaryLine 'Wow64CmdLaunch' $wow64CmdLaunch

$uacPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
try {
    $uac = Get-ItemProperty -LiteralPath $uacPath -ErrorAction Stop
    foreach ($setting in @('EnableLUA', 'ConsentPromptBehaviorAdmin', 'PromptOnSecureDesktop')) {
        $property = $uac.PSObject.Properties[$setting]
        Add-SummaryLine ("Uac{0}" -f $setting) $(if ($property) { $property.Value } else { 'MISSING' })
    }
} catch {
    Add-SummaryLine 'UacEnableLUA' 'UNAVAILABLE'
    Add-SummaryLine 'UacConsentPromptBehaviorAdmin' 'UNAVAILABLE'
    Add-SummaryLine 'UacPromptOnSecureDesktop' 'UNAVAILABLE'
    Add-SummaryLine 'UacReadError' $_.Exception.Message
}

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    Add-SummaryLine 'CollectorIsAdministrator' $isAdmin
} catch {
    Add-SummaryLine 'CollectorIsAdministrator' 'UNAVAILABLE'
}

try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    Add-SummaryLine 'WindowsVersion' $os.Version
    Add-SummaryLine 'WindowsBuild' $os.BuildNumber
    Add-SummaryLine 'WindowsArchitecture' $os.OSArchitecture
} catch {
    Add-SummaryLine 'WindowsVersion' ([Environment]::OSVersion.VersionString)
    Add-SummaryLine 'WindowsBuild' 'UNAVAILABLE'
    Add-SummaryLine 'WindowsArchitecture' 'UNAVAILABLE'
}

$tempWritable = $false
try {
    $probePath = Join-Path $env:TEMP "qqspeed-diagnostic-$PID.tmp"
    [IO.File]::WriteAllText($probePath, 'probe')
    Remove-Item -LiteralPath $probePath -Force
    $tempWritable = $true
} catch {
    Add-SummaryLine 'TempProbeError' $_.Exception.Message
}
Add-SummaryLine 'TempWritable' $tempWritable
try {
    $tempItem = Get-Item -LiteralPath $env:TEMP
    $tempDrive = Get-PSDrive -Name $tempItem.PSDrive.Name
    Add-SummaryLine 'TempFreeBytes' $tempDrive.Free
} catch {
    Add-SummaryLine 'TempFreeBytes' 'UNAVAILABLE'
}

$resolvedLauncher = $null
$launcherMachine = 'UNKNOWN'
try {
    $resolvedLauncher = Find-Launcher $LauncherPath
} catch {
    Add-SummaryLine 'LauncherResolveError' $_.Exception.Message
}

if ($resolvedLauncher) {
    $launcherItem = Get-Item -LiteralPath $resolvedLauncher
    $launcherMachine = Get-PeMachine $resolvedLauncher
    Add-SummaryLine 'LauncherFile' $launcherItem.Name
    Add-SummaryLine 'LauncherMachine' $launcherMachine
    Add-SummaryLine 'LauncherSize' $launcherItem.Length
    Add-SummaryLine 'LauncherSHA256' (Get-FileHash -LiteralPath $resolvedLauncher -Algorithm SHA256).Hash
    try {
        $signature = Get-AuthenticodeSignature -LiteralPath $resolvedLauncher
        Add-SummaryLine 'LauncherSignature' $signature.Status
    } catch {
        Add-SummaryLine 'LauncherSignature' 'UNAVAILABLE'
    }
    try {
        $zoneText = Get-Content -LiteralPath $resolvedLauncher -Stream Zone.Identifier -ErrorAction Stop
        $zoneId = ($zoneText | Select-String -Pattern '^ZoneId=(\d+)$').Matches.Groups[1].Value
        Add-SummaryLine 'LauncherZoneId' $(if ($zoneId) { $zoneId } else { 'PRESENT_UNKNOWN' })
    } catch {
        Add-SummaryLine 'LauncherZoneId' 'NONE_OR_UNAVAILABLE'
    }
} else {
    Add-SummaryLine 'LauncherFile' 'NOT_FOUND'
}

$launchErrorCode = 0
$outerProcess = $null
$outerStarted = $false
$seenProcesses = @{}
$seenProcessIds = @{}
$env:QQSPEED_LAUNCHER_RUN_ID = $runId
$env:ELECTRON_ENABLE_LOGGING = '1'
$env:ELECTRON_LOG_FILE = Join-Path $runRoot 'electron.log'

if (-not $NoLaunch) {
    if (-not $resolvedLauncher) {
        Add-SummaryLine 'LaunchStatus' 'NOT_STARTED_LAUNCHER_NOT_FOUND'
    } elseif (-not $tempWritable) {
        Add-SummaryLine 'LaunchStatus' 'NOT_STARTED_TEMP_NOT_WRITABLE'
    } else {
        try {
            $outerProcess = Start-Process -FilePath $resolvedLauncher -PassThru -ErrorAction Stop
            $outerStarted = $true
            Add-SummaryLine 'LaunchStatus' 'STARTED'
            Add-SummaryLine 'OuterPid' $outerProcess.Id
        } catch {
            $nativeErrorProperty = $_.Exception.PSObject.Properties['NativeErrorCode']
            $launchErrorCode = if ($nativeErrorProperty) { [int]$nativeErrorProperty.Value } else { -1 }
            Add-SummaryLine 'LaunchStatus' 'START_FAILED'
            Add-SummaryLine 'LaunchErrorCode' $launchErrorCode
            Add-SummaryLine 'LaunchError' $_.Exception.Message
        }

        if ($outerStarted) {
            $deadline = (Get-Date).AddSeconds($ObserveSeconds)
            $outerExitObservedAt = $null
            while ((Get-Date) -lt $deadline) {
                $now = Get-Date
                $processes = Get-Process -Name 'QQSpeedLauncher*' -ErrorAction SilentlyContinue
                foreach ($process in $processes) {
                    $key = "{0}:{1}" -f $process.Id, $process.ProcessName
                    if ($seenProcesses.ContainsKey($key)) {
                        continue
                    }
                    $seenProcesses[$key] = $true
                    $seenProcessIds[[int]$process.Id] = $true
                    $fileName = 'UNAVAILABLE'
                    $windowHandle = 0
                    try { $fileName = [IO.Path]::GetFileName($process.Path) } catch {}
                    try { $windowHandle = $process.MainWindowHandle.ToInt64() } catch {}
                    Add-JsonLine $processPath ([ordered]@{
                        timestamp = $now.ToString('o')
                        pid = $process.Id
                        process = $process.ProcessName
                        file = $fileName
                        has_window = ($windowHandle -ne 0)
                    })
                }

                try {
                    if ($outerProcess.HasExited -and -not $outerExitObservedAt) {
                        $outerExitObservedAt = Get-Date
                        Add-SummaryLine 'OuterExitCode' $outerProcess.ExitCode
                        Add-SummaryLine 'OuterLifetimeMs' ([int](($outerExitObservedAt - $startedAt).TotalMilliseconds))
                    }
                } catch {}

                if ($outerExitObservedAt -and ((Get-Date) - $outerExitObservedAt).TotalSeconds -ge 2) {
                    break
                }
                Start-Sleep -Milliseconds 250
            }
        }
    }
} else {
    Add-SummaryLine 'LaunchStatus' 'NO_LAUNCH_SELF_TEST'
}

$startupRecords = @()
$correlationStartUtc = $startedAt.ToUniversalTime().AddSeconds(-3)
$correlationEndUtc = (Get-Date).ToUniversalTime().AddSeconds(3)
if (Test-Path -LiteralPath $stableLogPath) {
    foreach ($line in Get-Content -LiteralPath $stableLogPath -ErrorAction SilentlyContinue) {
        try {
            $record = $line | ConvertFrom-Json -ErrorAction Stop
            $runIdMatches = $record.PSObject.Properties['run_id'] -and $record.run_id -eq $runId
            $processMatches = $false
            if ($record.PSObject.Properties['pid']) {
                $processMatches = $seenProcessIds.ContainsKey([int]$record.pid)
            }
            $timestampMatches = $false
            if ($record.PSObject.Properties['timestamp']) {
                $recordTime = [datetime]::Parse([string]$record.timestamp).ToUniversalTime()
                $timestampMatches = $recordTime -ge $correlationStartUtc -and $recordTime -le $correlationEndUtc
            }
            if ($runIdMatches -or ($processMatches -and $timestampMatches)) {
                $startupRecords += $record
                Add-Content -LiteralPath (Join-Path $runRoot 'startup.log') -Encoding UTF8 -Value $line
            }
        } catch {}
    }
}

$eventStart = $startedAt.AddSeconds(-5)
$applicationEventCount = Export-RelatedEvents 'Application' @(1000, 1001, 1026) $eventStart
$securityEventCount = 0
$securityEventCount += Export-RelatedEvents 'Microsoft-Windows-Windows Defender/Operational' @(1116, 1117, 1121, 1122) $eventStart
$securityEventCount += Export-RelatedEvents 'Microsoft-Windows-CodeIntegrity/Operational' @(3076, 3077, 3089) $eventStart
Add-SummaryLine 'ApplicationErrorEventCount' $applicationEventCount
Add-SummaryLine 'SecurityEventCount' $securityEventCount
Add-SummaryLine 'RelatedEventCount' ($applicationEventCount + $securityEventCount)

$classification = 'UNKNOWN'
if ($NoLaunch) {
    $classification = 'COLLECTOR_SELF_TEST'
} elseif ($launcherMachine -eq 'x86' -and $operatingSystemIs64Bit -and -not $wow64CmdLaunch) {
    $classification = 'PORTABLE_X86_WOW64_UNAVAILABLE'
} elseif ($launchErrorCode -eq 1223) {
    $classification = 'UAC_CANCELLED'
} elseif (-not $outerStarted) {
    $classification = 'OUTER_START_FAILED'
} elseif ($startupRecords.Count -eq 0) {
    $classification = if ($securityEventCount -gt 0) { 'SECURITY_BLOCK_BEFORE_ELECTRON' } else { 'OUTER_OR_EARLY_INNER_FAILURE' }
} else {
    $eventNames = @($startupRecords | ForEach-Object { [string]$_.event })
    $fatalChildFailure = $false
    $processExitFailure = $false
    foreach ($record in $startupRecords) {
        $recordEvent = [string]$record.event
        if ($recordEvent -eq 'RENDERER_PROCESS_GONE' -or $recordEvent -eq 'CHILD_PROCESS_GONE') {
            $recordReason = if ($record.PSObject.Properties['reason']) { [string]$record.reason } else { '' }
            if ($recordReason -ne 'clean-exit') {
                $fatalChildFailure = $true
            }
        }
        if ($recordEvent -eq 'APP_PROCESS_EXIT' -and $record.PSObject.Properties['exitCode'] -and [int]$record.exitCode -ne 0) {
            $processExitFailure = $true
        }
    }
    if ($eventNames -contains 'MAIN_IMPORT_FAILED') {
        $classification = 'ELECTRON_IMPORT_FAILURE'
    } elseif ($fatalChildFailure) {
        $classification = 'ELECTRON_RENDERER_OR_GPU_FAILURE'
    } elseif (($eventNames -contains 'NATIVE_STAGE_FAILED') -or ($eventNames -contains 'NATIVE_BINDINGS_FAILED')) {
        $classification = 'NATIVE_BOUNDARY_FAILURE'
    } elseif ($processExitFailure) {
        $classification = 'ELECTRON_PROCESS_EXIT_FAILURE'
    } elseif ($eventNames -contains 'WINDOW_SHOWN') {
        $classification = 'WINDOW_SHOWN'
    } else {
        $lastEvent = [string]$startupRecords[-1].event
        $classification = "ELECTRON_REACHED_$lastEvent"
    }
}
Add-SummaryLine 'Classification' $classification
Add-SummaryLine 'StartupRecordCount' $startupRecords.Count
Add-SummaryLine 'FinishedAt' (Get-Date).ToString('o')

if (-not $OutputDirectory) {
    $OutputDirectory = [Environment]::GetFolderPath('Desktop')
}
if (-not $OutputDirectory) {
    $OutputDirectory = $PSScriptRoot
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$zipPath = Join-Path $OutputDirectory "QQSpeedLauncher-Diagnostics-$stamp.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $runRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host ''
Write-Host "诊断完成：$zipPath" -ForegroundColor Green
Write-Host "自动判定：$classification"
