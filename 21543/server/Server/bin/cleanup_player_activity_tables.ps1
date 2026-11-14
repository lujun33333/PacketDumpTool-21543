[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$MySqlBin = ""
)

$ErrorActionPreference = "Stop"

# Activity business tables that are now owned by the activity database.
$MigratedTables = @(
    "SpeedPassState",
    "SpeedPassTaskState",
    "SpeedPassClaim",
    "SpeedPassExchangeState",
    "FourBeautiesState",
    "FourBeautiesBuff",
    "FourBeautiesGotItem",
    "FourBeautiesTempItem",
    "WastelandState",
    "WastelandTempItem",
    "WastelandExchange",
    "CharmVipPlayerState",
    "CharmVipClaimState",
    "CarTreasureSelectedGroup",
    "RetComItemTable",
    "SummerCarTreasureState",
    "GodWarBoxState",
    "GodWarBoxTempItem",
    "GodWarBoxDrawLog",
    "SupremeTreasureState",
    "SecretCompassState",
    "SpeedLadderPlayerMap",
    "TarotState",
    "TarotTempItem",
    "TarotDrawLog",
    "FinalMagicHouseState",
    "FinalMagicHouseSelectedGroup",
    "HuimengDreamState",
    "HuimengDreamTempItem",
    "ZhiYaoGodsState",
    "GodHorseState",
    "GodHorseExchange",
    "WelfareClaimState",
    "AnnualRacingState",
    "AnnualRacingTempItem",
    "AnnualRacingCounter",
    "NorthdoorInvisibleState",
    "NorthdoorInvisibleDrawLog",
    "NorthdoorInvisibleSpecialOfferPurchase",
    "RockPartyState",
    "RockPartyDrawLog",
    "RockPartyPurchaseLog"
)

# Confirmed obsolete tables. They have no activity counterpart and are backed up
# before removal.
$LegacyTables = @(
    "SummerCarTreasureShopState",
    "RockLegendPersonalization"
)

function Read-IniSection {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Section
    )

    $result = @{}
    $currentSection = ""
    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ($line -match '^\[(.+)\]$') {
            $currentSection = $Matches[1]
            continue
        }
        if ($currentSection -ne $Section -or $line.Length -eq 0 -or
            $line.StartsWith("#") -or $line.StartsWith(";")) {
            continue
        }
        if ($line -match '^([^=]+)=(.*)$') {
            $result[$Matches[1].Trim()] = $Matches[2].Trim()
        }
    }
    return $result
}

function Resolve-MySqlTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($MySqlBin) {
        $candidate = Join-Path $MySqlBin $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $known = @(
        (Join-Path $env:ProgramFiles "MySQL\MySQL Server 8.0\bin\$Name"),
        (Join-Path ${env:ProgramFiles(x86)} "MySQL\MySQL Server 8.0\bin\$Name")
    )
    foreach ($candidate in $known) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Cannot find $Name. Pass -MySqlBin with the MySQL bin directory."
}

function Quote-SqlIdentifier {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "``$($Value.Replace('`', '``'))``"
}

function Invoke-MySqlQuery {
    param([Parameter(Mandatory = $true)][string]$Sql)

    $arguments = @(
        "--protocol=tcp",
        "--host=$script:DbHost",
        "--port=$script:DbPort",
        "--user=$script:DbUser",
        "--batch",
        "--skip-column-names",
        "--execute=$Sql"
    )
    $output = @(& $script:MySqlExe @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "mysql.exe failed: $($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { "$_" })
}

function Get-TableExists {
    param(
        [Parameter(Mandatory = $true)][string]$Schema,
        [Parameter(Mandatory = $true)][string]$Table
    )
    $sql = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$Schema' AND table_name='$Table'"
    $rows = @(Invoke-MySqlQuery $sql)
    return [int]::Parse(($rows[-1]).Trim())
}

function Get-ExactRowCount {
    param(
        [Parameter(Mandatory = $true)][string]$Schema,
        [Parameter(Mandatory = $true)][string]$Table
    )
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $Table)"
    $rows = @(Invoke-MySqlQuery "SELECT COUNT(*) FROM $qualified")
    return [Int64]::Parse(($rows[-1]).Trim())
}

function Get-TableChecksum {
    param(
        [Parameter(Mandatory = $true)][string]$Schema,
        [Parameter(Mandatory = $true)][string]$Table
    )
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $Table)"
    $lines = @(Invoke-MySqlQuery "CHECKSUM TABLE $qualified")
    $line = $lines[-1]
    return ($line -split "`t")[-1].Trim()
}

function Get-NormalizedCreateTable {
    param(
        [Parameter(Mandatory = $true)][string]$Schema,
        [Parameter(Mandatory = $true)][string]$Table
    )
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $Table)"
    $lines = @(Invoke-MySqlQuery "SHOW CREATE TABLE $qualified")
    $line = $lines[-1]
    $parts = $line -split "`t", 2
    if ($parts.Count -ne 2) {
        throw "Unexpected SHOW CREATE TABLE output for $Schema.$Table"
    }
    return ($parts[1] -replace 'AUTO_INCREMENT=\d+\s*', '')
}

$iniPath = Join-Path $PSScriptRoot "mysql.ini"
if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "Missing mysql.ini next to this script: $iniPath"
}

$playerConfig = Read-IniSection -Path $iniPath -Section "database_player"
$activityConfig = Read-IniSection -Path $iniPath -Section "database_activity"
foreach ($requiredKey in @("host", "user", "password", "database")) {
    if (-not $playerConfig.ContainsKey($requiredKey)) {
        throw "mysql.ini [database_player] is missing '$requiredKey'."
    }
}
if ($playerConfig["database"] -ne "player") {
    throw "Expected [database_player] database=player."
}
if (-not $activityConfig.ContainsKey("database") -or $activityConfig["database"] -ne "activity") {
    throw "Expected [database_activity] database=activity."
}
if ($activityConfig.ContainsKey("host") -and $activityConfig["host"] -ne $playerConfig["host"]) {
    throw "player and activity point to different MySQL hosts; this cleanup cannot verify them safely."
}

$script:DbHost = $playerConfig["host"]
$script:DbPort = if ($playerConfig.ContainsKey("port")) { $playerConfig["port"] } else { "3306" }
$script:DbUser = $playerConfig["user"]
$script:MySqlExe = Resolve-MySqlTool "mysql.exe"
$mySqlDumpExe = Resolve-MySqlTool "mysqldump.exe"

$runningServer = @(Get-Process -Name "SpeedServer" -ErrorAction SilentlyContinue)
if ($runningServer.Count -gt 0) {
    throw "SpeedServer.exe is still running (PID: $($runningServer.Id -join ', ')). Stop it before cleanup."
}

$oldMySqlPwd = $env:MYSQL_PWD
$env:MYSQL_PWD = $playerConfig["password"]
try {
    [void](Invoke-MySqlQuery "SELECT 1")

    $verified = @()
    $emptySources = @()
    $alreadyAbsent = @()
    $blockers = @()
    $speedLadderPending = $null

    Write-Host "Checking 42 migrated activity tables..."
    foreach ($table in $MigratedTables) {
        $sourceExists = Get-TableExists -Schema "player" -Table $table
        if (-not $sourceExists) {
            $alreadyAbsent += $table
            continue
        }

        $sourceRows = Get-ExactRowCount -Schema "player" -Table $table
        $sourceChecksum = Get-TableChecksum -Schema "player" -Table $table
        if ($sourceRows -eq 0) {
            # Empty old tables do not carry player data. The new binary creates
            # their activity counterparts when it starts.
            $emptySources += [PSCustomObject]@{
                Table = $table
                Rows = $sourceRows
                Checksum = $sourceChecksum
            }
            continue
        }

        $destinationExists = Get-TableExists -Schema "activity" -Table $table
        if (-not $destinationExists) {
            if ($table -eq "SpeedLadderPlayerMap") {
                $speedLadderPending = [PSCustomObject]@{
                    Table = $table
                    Rows = $sourceRows
                    Checksum = $sourceChecksum
                    CreateDestination = $true
                }
            }
            else {
                $blockers += "$table : missing activity destination"
            }
            continue
        }

        $sourceCreate = Get-NormalizedCreateTable -Schema "player" -Table $table
        $destinationCreate = Get-NormalizedCreateTable -Schema "activity" -Table $table
        if ($sourceCreate -cne $destinationCreate) {
            $blockers += "$table : schema mismatch"
            continue
        }

        $destinationRows = Get-ExactRowCount -Schema "activity" -Table $table
        if ($sourceRows -ne $destinationRows) {
            if ($table -eq "SpeedLadderPlayerMap" -and $destinationRows -eq 0) {
                $speedLadderPending = [PSCustomObject]@{
                    Table = $table
                    Rows = $sourceRows
                    Checksum = $sourceChecksum
                    CreateDestination = $false
                }
            }
            else {
                $blockers += "$table : row count mismatch (player=$sourceRows, activity=$destinationRows)"
            }
            continue
        }

        $destinationChecksum = Get-TableChecksum -Schema "activity" -Table $table
        if ($sourceChecksum -ne $destinationChecksum) {
            $blockers += "$table : checksum mismatch (player=$sourceChecksum, activity=$destinationChecksum)"
            continue
        }

        $verified += [PSCustomObject]@{
            Table = $table
            Rows = $sourceRows
            Checksum = $sourceChecksum
        }
    }

    $legacyExisting = @()
    foreach ($table in $LegacyTables) {
        if (Get-TableExists -Schema "player" -Table $table) {
            $legacyExisting += [PSCustomObject]@{
                Table = $table
                Rows = Get-ExactRowCount -Schema "player" -Table $table
                Checksum = Get-TableChecksum -Schema "player" -Table $table
            }
        }
    }

    Write-Host "Verified migrated source tables: $($verified.Count)"
    Write-Host "Empty source tables:             $($emptySources.Count)"
    Write-Host "Already absent from player:       $($alreadyAbsent.Count)"
    Write-Host "Obsolete source tables found:     $($legacyExisting.Count)"
    if ($null -ne $speedLadderPending) {
        Write-Host "SpeedLadder rows to migrate:      $($speedLadderPending.Rows)"
    }

    if ($blockers.Count -gt 0) {
        Write-Host ""
        Write-Host "Cleanup blocked:" -ForegroundColor Red
        $blockers | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        throw "Verification failed. No tables were deleted."
    }

    $pendingTables = if ($null -ne $speedLadderPending) { @($speedLadderPending.Table) } else { @() }
    $tablesToDelete = @($verified.Table) + @($emptySources.Table) + $pendingTables + @($legacyExisting.Table)
    if ($tablesToDelete.Count -eq 0) {
        Write-Host "Nothing to delete from player."
        exit 0
    }

    Write-Host ""
    Write-Host "Tables ready for deletion from player ($($tablesToDelete.Count)):"
    $tablesToDelete | ForEach-Object { Write-Host "  $_" }

    if (-not $Execute) {
        Write-Host ""
        Write-Host "Preview only. Run with -Execute (or use cleanup_player_activity_tables.cmd) to back up and delete."
        exit 0
    }

    Write-Host ""
    $confirmation = Read-Host "Type DELETE to create a backup and drop these player tables"
    if ($confirmation -cne "DELETE") {
        Write-Host "Cancelled. No tables were deleted."
        exit 0
    }

    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\_db_backups"))
    $backupDir = Join-Path $backupRoot "player_activity_cleanup_$stamp"
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null

    $manifestPath = Join-Path $backupDir "verification.tsv"
    $manifestRows = @($verified) + @($emptySources) + @($legacyExisting)
    if ($null -ne $speedLadderPending) {
        $manifestRows += $speedLadderPending
    }
    $manifestRows |
        Select-Object Table, Rows, Checksum |
        Export-Csv -LiteralPath $manifestPath -Delimiter "`t" -NoTypeInformation -Encoding UTF8

    $dumpPath = Join-Path $backupDir "player_activity_tables.sql"
    $dumpArguments = @(
        "--protocol=tcp",
        "--host=$script:DbHost",
        "--port=$script:DbPort",
        "--user=$script:DbUser",
        "--single-transaction",
        "--set-gtid-purged=OFF",
        "--column-statistics=0",
        "--hex-blob",
        "--default-character-set=utf8mb4",
        "--result-file=$dumpPath",
        "player"
    ) + $tablesToDelete

    $dumpOutput = @(& $mySqlDumpExe @dumpArguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "mysqldump.exe failed: $($dumpOutput -join [Environment]::NewLine)"
    }
    if (-not (Test-Path -LiteralPath $dumpPath -PathType Leaf) -or
        (Get-Item -LiteralPath $dumpPath).Length -eq 0) {
        throw "Backup dump was not created. No tables were deleted."
    }

    if ($null -ne $speedLadderPending) {
        $sourceTable = "$(Quote-SqlIdentifier 'player').$(Quote-SqlIdentifier $speedLadderPending.Table)"
        $destinationTable = "$(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $speedLadderPending.Table)"

        if ($speedLadderPending.CreateDestination) {
            Invoke-MySqlQuery "CREATE TABLE $destinationTable LIKE $sourceTable" | Out-Null
        }

        $destinationRowsBefore = Get-ExactRowCount -Schema "activity" -Table $speedLadderPending.Table
        if ($destinationRowsBefore -ne 0) {
            throw "activity.SpeedLadderPlayerMap is no longer empty. Migration stopped; no player tables were deleted."
        }

        Invoke-MySqlQuery "INSERT INTO $destinationTable SELECT * FROM $sourceTable" | Out-Null

        $sourceCreate = Get-NormalizedCreateTable -Schema "player" -Table $speedLadderPending.Table
        $destinationCreate = Get-NormalizedCreateTable -Schema "activity" -Table $speedLadderPending.Table
        $destinationRows = Get-ExactRowCount -Schema "activity" -Table $speedLadderPending.Table
        $destinationChecksum = Get-TableChecksum -Schema "activity" -Table $speedLadderPending.Table
        if ($sourceCreate -cne $destinationCreate -or
            $destinationRows -ne $speedLadderPending.Rows -or
            $destinationChecksum -ne $speedLadderPending.Checksum) {
            throw "SpeedLadderPlayerMap copy verification failed. No player tables were deleted."
        }
        Write-Host "SpeedLadderPlayerMap migrated and verified: $destinationRows row(s)" -ForegroundColor Green
    }

    $qualifiedTables = $tablesToDelete | ForEach-Object {
        "$(Quote-SqlIdentifier 'player').$(Quote-SqlIdentifier $_)"
    }
    Invoke-MySqlQuery ("DROP TABLE IF EXISTS " + ($qualifiedTables -join ", ")) | Out-Null

    $remaining = @()
    foreach ($table in $tablesToDelete) {
        if (Get-TableExists -Schema "player" -Table $table) {
            $remaining += $table
        }
    }
    if ($remaining.Count -gt 0) {
        throw "DROP returned but these tables still exist: $($remaining -join ', ')"
    }

    Write-Host ""
    Write-Host "Cleanup completed." -ForegroundColor Green
    Write-Host "Deleted from player: $($tablesToDelete.Count) table(s)"
    Write-Host "Backup: $dumpPath"
}
finally {
    if ($null -eq $oldMySqlPwd) {
        Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue
    }
    else {
        $env:MYSQL_PWD = $oldMySqlPwd
    }
}
