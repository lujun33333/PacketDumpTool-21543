[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$MySqlBin = ""
)

$ErrorActionPreference = "Stop"
$ScriptVersion = "2026-07-31.4"

$KeepTable = "SpeedLadderPlayerMap"
$ActivityTables = @(
    "SpeedPassState", "SpeedPassTaskState", "SpeedPassClaim", "SpeedPassExchangeState",
    "FourBeautiesState", "FourBeautiesBuff", "FourBeautiesGotItem", "FourBeautiesTempItem",
    "WastelandState", "WastelandTempItem", "WastelandExchange",
    "CharmVipPlayerState", "CharmVipClaimState",
    "CarTreasureSelectedGroup", "RetComItemTable", "SummerCarTreasureState",
    "GodWarBoxState", "GodWarBoxTempItem", "GodWarBoxDrawLog",
    "SupremeTreasureState", "SecretCompassState", "SpeedLadderPlayerMap",
    "TarotState", "TarotTempItem", "TarotDrawLog",
    "FinalMagicHouseState", "FinalMagicHouseSelectedGroup",
    "HuimengDreamState", "HuimengDreamTempItem", "ZhiYaoGodsState",
    "GodHorseState", "GodHorseExchange", "WelfareClaimState",
    "AnnualRacingState", "AnnualRacingTempItem", "AnnualRacingCounter",
    "NorthdoorInvisibleState", "NorthdoorInvisibleDrawLog", "NorthdoorInvisibleSpecialOfferPurchase",
    "RockPartyState", "RockPartyDrawLog", "RockPartyPurchaseLog"
)
$ResetTables = @($ActivityTables | Where-Object { $_ -ne $KeepTable })
$LegacyPlayerTables = @("SummerCarTreasureShopState", "RockLegendPersonalization")

function Read-IniSection {
    param([string]$Path, [string]$Section)
    $result = @{}
    $current = ""
    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ($line -match '^\[(.+)\]$') { $current = $Matches[1]; continue }
        if ($current -ne $Section -or $line.Length -eq 0 -or
            $line.StartsWith("#") -or $line.StartsWith(";")) { continue }
        if ($line -match '^([^=]+)=(.*)$') {
            $result[$Matches[1].Trim()] = $Matches[2].Trim()
        }
    }
    return $result
}

function Resolve-MySqlTool {
    param([string]$Name)
    if ($MySqlBin) {
        $candidate = Join-Path $MySqlBin $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles "MySQL\MySQL Server 8.0\bin\$Name"),
        (Join-Path ${env:ProgramFiles(x86)} "MySQL\MySQL Server 8.0\bin\$Name")
    )) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Cannot find $Name. Pass -MySqlBin with the MySQL bin directory."
}

function Quote-SqlIdentifier {
    param([string]$Value)
    return "``$($Value.Replace('`', '``'))``"
}

function Invoke-MySqlQuery {
    param([string]$Sql)
    $arguments = @(
        "--protocol=tcp", "--host=$script:DbHost", "--port=$script:DbPort",
        "--user=$script:DbUser", "--batch", "--skip-column-names", "--execute=$Sql"
    )
    $output = @(& $script:MySqlExe @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "mysql.exe failed: $($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { "$_" })
}

function Get-ActualTableName {
    param([string]$Schema, [string]$Table)
    $rows = @(Invoke-MySqlQuery "SELECT table_name FROM information_schema.tables WHERE table_schema='$Schema' AND LOWER(table_name)=LOWER('$Table') ORDER BY (BINARY table_name=BINARY '$Table') DESC LIMIT 1")
    if ($rows.Count -eq 0) { return $null }
    return $rows[-1].Trim()
}

function Get-TableExists {
    param([string]$Schema, [string]$Table)
    return $null -ne (Get-ActualTableName -Schema $Schema -Table $Table)
}

function Get-RowCount {
    param([string]$Schema, [string]$Table)
    $actualTable = Get-ActualTableName -Schema $Schema -Table $Table
    if ($null -eq $actualTable) { throw "Table $Schema.$Table no longer exists." }
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $actualTable)"
    $rows = @(Invoke-MySqlQuery "SELECT COUNT(*) FROM $qualified")
    return [Int64]::Parse(($rows[-1]).Trim())
}

function Get-Checksum {
    param([string]$Schema, [string]$Table)
    $actualTable = Get-ActualTableName -Schema $Schema -Table $Table
    if ($null -eq $actualTable) { throw "Table $Schema.$Table no longer exists." }
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $actualTable)"
    $lines = @(Invoke-MySqlQuery "CHECKSUM TABLE $qualified")
    $line = $lines[-1]
    return ($line -split "`t")[-1].Trim()
}

function Get-NormalizedCreateTable {
    param([string]$Schema, [string]$Table)
    $actualTable = Get-ActualTableName -Schema $Schema -Table $Table
    if ($null -eq $actualTable) { throw "Table $Schema.$Table no longer exists." }
    $qualified = "$(Quote-SqlIdentifier $Schema).$(Quote-SqlIdentifier $actualTable)"
    $lines = @(Invoke-MySqlQuery "SHOW CREATE TABLE $qualified")
    $line = $lines[-1]
    $parts = $line -split "`t", 2
    if ($parts.Count -ne 2) { throw "Unexpected SHOW CREATE TABLE output for $Schema.$Table" }
    return ($parts[1] -replace 'AUTO_INCREMENT=\d+\s*', '')
}

function Get-InventoryRows {
    param([string]$Schema, [string[]]$Tables)
    $rows = @()
    foreach ($table in $Tables) {
        $actualTable = Get-ActualTableName -Schema $Schema -Table $table
        if ($null -ne $actualTable) {
            $rows += [PSCustomObject]@{
                Schema = $Schema
                Table = $table
                ActualTable = $actualTable
                Rows = Get-RowCount -Schema $Schema -Table $actualTable
                Checksum = Get-Checksum -Schema $Schema -Table $actualTable
            }
        }
    }
    return $rows
}

function Backup-Tables {
    param([string]$Schema, [string[]]$Tables, [string]$OutputPath)
    if ($Tables.Count -eq 0) { return }
    $arguments = @(
        "--protocol=tcp", "--host=$script:DbHost", "--port=$script:DbPort",
        "--user=$script:DbUser", "--single-transaction", "--set-gtid-purged=OFF",
        "--column-statistics=0", "--hex-blob", "--default-character-set=utf8mb4",
        "--result-file=$OutputPath", $Schema
    ) + $Tables
    $output = @(& $script:MySqlDumpExe @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "mysqldump.exe failed for ${Schema}: $($output -join [Environment]::NewLine)"
    }
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf) -or
        (Get-Item -LiteralPath $OutputPath).Length -eq 0) {
        throw "Backup was not created for $Schema. No data was reset."
    }
}

$iniPath = Join-Path $PSScriptRoot "mysql.ini"
if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "Missing mysql.ini next to this script: $iniPath"
}
$playerConfig = Read-IniSection -Path $iniPath -Section "database_player"
$activityConfig = Read-IniSection -Path $iniPath -Section "database_activity"
foreach ($key in @("host", "user", "password", "database")) {
    if (-not $playerConfig.ContainsKey($key)) { throw "[database_player] is missing '$key'." }
}
if ($playerConfig.database -ne "player" -or $activityConfig.database -ne "activity") {
    throw "mysql.ini must contain database=player and database=activity."
}
if ($activityConfig.host -ne $playerConfig.host) {
    throw "player and activity must be on the same MySQL host."
}

$script:DbHost = $playerConfig.host
$script:DbPort = if ($playerConfig.ContainsKey("port")) { $playerConfig.port } else { "3306" }
$script:DbUser = $playerConfig.user
$script:MySqlExe = Resolve-MySqlTool "mysql.exe"
$script:MySqlDumpExe = Resolve-MySqlTool "mysqldump.exe"

$runningServer = @(Get-Process -Name "SpeedServer" -ErrorAction SilentlyContinue)
if ($runningServer.Count -gt 0) {
    throw "SpeedServer.exe is still running (PID: $($runningServer.Id -join ', ')). Stop it first."
}

$oldMySqlPwd = $env:MYSQL_PWD
$env:MYSQL_PWD = $playerConfig.password
try {
    [void](Invoke-MySqlQuery "SELECT 1")

    $playerInventory = @(Get-InventoryRows -Schema "player" -Tables ($ActivityTables + $LegacyPlayerTables))
    $activityInventory = @(Get-InventoryRows -Schema "activity" -Tables $ActivityTables)
    $playerKeep = @($playerInventory | Where-Object { $_.Table -eq $KeepTable })
    $activityKeep = @($activityInventory | Where-Object { $_.Table -eq $KeepTable })

    if ($playerKeep.Count -eq 0 -and $activityKeep.Count -eq 0) {
        throw "SpeedLadderPlayerMap does not exist in player or activity. Reset refused."
    }
    if ($playerKeep.Count -gt 0 -and $activityKeep.Count -gt 0) {
        $sourceCreate = Get-NormalizedCreateTable -Schema "player" -Table $KeepTable
        $destinationCreate = Get-NormalizedCreateTable -Schema "activity" -Table $KeepTable
        if ($sourceCreate -cne $destinationCreate) {
            throw "player.SpeedLadderPlayerMap and activity.SpeedLadderPlayerMap schemas differ. Reset refused."
        }
    }

    $discardRows = [Int64]0
    @($playerInventory + $activityInventory) |
        Where-Object { $_.Table -ne $KeepTable } |
        ForEach-Object { $discardRows += [Int64]$_.Rows }

    Write-Host "Script version: $ScriptVersion"
    Write-Host "Keep and merge: SpeedLadderPlayerMap"
    Write-Host "  player rows:   $(if ($playerKeep.Count) { $playerKeep[0].Rows } else { 0 })"
    Write-Host "  activity rows: $(if ($activityKeep.Count) { $activityKeep[0].Rows } else { 0 })"
    Write-Host "Reset to empty:  $($ResetTables.Count) other activity tables"
    Write-Host "Rows to discard across player/activity: $discardRows" -ForegroundColor Yellow
    Write-Host "Aurora infrastructure tables are not touched."

    if (-not $Execute) {
        Write-Host "Preview only. Use keep_speedladder_reset_other_activities.cmd to execute."
        exit 0
    }

    $confirmation = Read-Host "Type KEEP SPEEDLADDER to back up, keep SpeedLadder data, and reset all other activity data"
    if ($confirmation -cne "KEEP SPEEDLADDER") {
        Write-Host "Cancelled. No database changes were made."
        exit 0
    }

    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\_db_backups"))
    $backupDir = Join-Path $backupRoot "keep_speedladder_reset_activities_$stamp"
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    @($playerInventory + $activityInventory) |
        Export-Csv -LiteralPath (Join-Path $backupDir "inventory.tsv") -Delimiter "`t" -NoTypeInformation -Encoding UTF8

    $playerBackupTables = @($playerInventory.ActualTable)
    $activityBackupTables = @($activityInventory.ActualTable)
    Backup-Tables -Schema "player" -Tables $playerBackupTables -OutputPath (Join-Path $backupDir "player_before.sql")
    Backup-Tables -Schema "activity" -Tables $activityBackupTables -OutputPath (Join-Path $backupDir "activity_before.sql")

    $sourceActualTable = Get-ActualTableName -Schema "player" -Table $KeepTable
    $destinationActualTable = Get-ActualTableName -Schema "activity" -Table $KeepTable
    $sourceTable = if ($null -ne $sourceActualTable) {
        "$(Quote-SqlIdentifier 'player').$(Quote-SqlIdentifier $sourceActualTable)"
    } else { $null }
    $destinationTable = if ($null -ne $destinationActualTable) {
        "$(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $destinationActualTable)"
    } else { $null }
    if ($activityKeep.Count -eq 0) {
        if ($null -eq $sourceTable) { throw "Cannot create activity.SpeedLadderPlayerMap without a player source table." }
        $destinationTable = "$(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $KeepTable)"
        Invoke-MySqlQuery "CREATE TABLE $destinationTable LIKE $sourceTable" | Out-Null
        $destinationActualTable = Get-ActualTableName -Schema "activity" -Table $KeepTable
        if ($null -eq $destinationActualTable) { throw "activity.SpeedLadderPlayerMap was not created." }
        $destinationTable = "$(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $destinationActualTable)"
        $activityKeep = @([PSCustomObject]@{ Rows = 0 })
    }

    if ($playerKeep.Count -gt 0 -and $playerKeep[0].Rows -gt 0) {
        $copiesAlreadyEqual =
            $activityKeep[0].Rows -eq $playerKeep[0].Rows -and
            $null -ne $activityKeep[0].Checksum -and
            $activityKeep[0].Checksum -eq $playerKeep[0].Checksum

        if ($copiesAlreadyEqual) {
            Write-Host "SpeedLadderPlayerMap copies are already identical; merge skipped."
        }
        elseif ($activityKeep[0].Rows -eq 0) {
            Invoke-MySqlQuery "INSERT INTO $destinationTable SELECT * FROM $sourceTable" | Out-Null
        }
        else {
            $dBestTime = "$destinationTable.$(Quote-SqlIdentifier 'BestTime')"
            $dReplayMd5 = "$destinationTable.$(Quote-SqlIdentifier 'ReplayMd5')"
            $dReplayPath = "$destinationTable.$(Quote-SqlIdentifier 'ReplayPath')"
            $dTrophyLevel = "$destinationTable.$(Quote-SqlIdentifier 'TrophyLevel')"
            $dTrophyCount = "$destinationTable.$(Quote-SqlIdentifier 'TrophyCount')"
            $dStar1Done = "$destinationTable.$(Quote-SqlIdentifier 'Star1Done')"
            $dStar2Done = "$destinationTable.$(Quote-SqlIdentifier 'Star2Done')"
            $dStar3Done = "$destinationTable.$(Quote-SqlIdentifier 'Star3Done')"
            $dAssessDone = "$destinationTable.$(Quote-SqlIdentifier 'AssessDone')"
            $dUpdatedAt = "$destinationTable.$(Quote-SqlIdentifier 'UpdatedAt')"
            $mergeSql = @"
INSERT INTO $destinationTable
(Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt)
SELECT Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt
FROM $sourceTable WHERE TRUE
ON DUPLICATE KEY UPDATE
ReplayMd5=CASE WHEN (($dBestTime=0 AND VALUES(BestTime)>0) OR (VALUES(BestTime)>0 AND VALUES(BestTime)<$dBestTime)) AND VALUES(ReplayMd5)<>'' THEN VALUES(ReplayMd5) WHEN $dReplayMd5='' THEN VALUES(ReplayMd5) ELSE $dReplayMd5 END,
ReplayPath=CASE WHEN (($dBestTime=0 AND VALUES(BestTime)>0) OR (VALUES(BestTime)>0 AND VALUES(BestTime)<$dBestTime)) AND VALUES(ReplayPath)<>'' THEN VALUES(ReplayPath) WHEN $dReplayPath='' THEN VALUES(ReplayPath) ELSE $dReplayPath END,
TrophyLevel=GREATEST($dTrophyLevel,VALUES(TrophyLevel)),
TrophyCount=GREATEST($dTrophyCount,VALUES(TrophyCount)),
Star1Done=GREATEST($dStar1Done,VALUES(Star1Done)),
Star2Done=GREATEST($dStar2Done,VALUES(Star2Done)),
Star3Done=GREATEST($dStar3Done,VALUES(Star3Done)),
AssessDone=GREATEST($dAssessDone,VALUES(AssessDone)),
UpdatedAt=GREATEST($dUpdatedAt,VALUES(UpdatedAt)),
BestTime=CASE WHEN $dBestTime=0 THEN VALUES(BestTime) WHEN VALUES(BestTime)=0 THEN $dBestTime ELSE LEAST($dBestTime,VALUES(BestTime)) END
"@
            Invoke-MySqlQuery $mergeSql | Out-Null
        }

        $missingResult = @(Invoke-MySqlQuery @"
SELECT COUNT(*) FROM $sourceTable s
LEFT JOIN $destinationTable a
ON a.Uin=s.Uin AND a.SeasonID=s.SeasonID AND a.MapID=s.MapID AND a.RecordSource=s.RecordSource
WHERE a.Uin IS NULL
"@)
        $missingKeys = [Int64]::Parse(($missingResult[-1]).Trim())
        $weakerResult = @(Invoke-MySqlQuery @"
SELECT COUNT(*) FROM $sourceTable s
JOIN $destinationTable a
ON a.Uin=s.Uin AND a.SeasonID=s.SeasonID AND a.MapID=s.MapID AND a.RecordSource=s.RecordSource
WHERE (s.BestTime>0 AND (a.BestTime=0 OR a.BestTime>s.BestTime))
   OR a.TrophyLevel<s.TrophyLevel OR a.TrophyCount<s.TrophyCount
   OR a.Star1Done<s.Star1Done OR a.Star2Done<s.Star2Done OR a.Star3Done<s.Star3Done
   OR a.AssessDone<s.AssessDone
"@)
        $weakerRows = [Int64]::Parse(($weakerResult[-1]).Trim())
        if ($missingKeys -ne 0 -or $weakerRows -ne 0) {
            throw "SpeedLadder migration verification failed (missing=$missingKeys, weaker=$weakerRows). Player tables were not dropped."
        }
    }

    # Ensure an empty activity table remains for every reset table that currently
    # exists only in player. Missing-in-both tables will be created by SpeedServer.
    foreach ($table in $ResetTables) {
        $activityActual = Get-ActualTableName -Schema "activity" -Table $table
        $playerActual = Get-ActualTableName -Schema "player" -Table $table
        if ($null -eq $activityActual -and $null -ne $playerActual) {
            $src = "$(Quote-SqlIdentifier 'player').$(Quote-SqlIdentifier $playerActual)"
            $dst = "$(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $table)"
            Invoke-MySqlQuery "CREATE TABLE $dst LIKE $src" | Out-Null
        }
    }

    $deleteStatements = @()
    foreach ($table in $ResetTables) {
        $activityActual = Get-ActualTableName -Schema "activity" -Table $table
        if ($null -ne $activityActual) {
            $deleteStatements += "DELETE FROM $(Quote-SqlIdentifier 'activity').$(Quote-SqlIdentifier $activityActual)"
        }
    }
    if ($deleteStatements.Count -gt 0) {
        Invoke-MySqlQuery ("START TRANSACTION;" + ($deleteStatements -join ";") + ";COMMIT;") | Out-Null
    }

    foreach ($table in $ResetTables) {
        if ((Get-TableExists -Schema "activity" -Table $table) -and
            (Get-RowCount -Schema "activity" -Table $table) -ne 0) {
            throw "activity.$table is not empty after reset. Player tables were not dropped."
        }
    }

    $playerTablesToDrop = @()
    foreach ($table in ($ActivityTables + $LegacyPlayerTables)) {
        $actualTable = Get-ActualTableName -Schema "player" -Table $table
        if ($null -ne $actualTable) { $playerTablesToDrop += $actualTable }
    }
    if ($playerTablesToDrop.Count -gt 0) {
        $qualified = $playerTablesToDrop | ForEach-Object {
            "$(Quote-SqlIdentifier 'player').$(Quote-SqlIdentifier $_)"
        }
        Invoke-MySqlQuery ("DROP TABLE IF EXISTS " + ($qualified -join ",")) | Out-Null
    }

    $remaining = @()
    foreach ($table in ($ActivityTables + $LegacyPlayerTables)) {
        $actualTable = Get-ActualTableName -Schema "player" -Table $table
        if ($null -ne $actualTable) { $remaining += $actualTable }
    }
    if ($remaining.Count -gt 0) {
        throw "These player tables still exist: $($remaining -join ', ')"
    }

    $keptRows = Get-RowCount -Schema "activity" -Table $KeepTable
    Write-Host ""
    Write-Host "Completed: only SpeedLadderPlayerMap activity data was retained." -ForegroundColor Green
    Write-Host "activity.SpeedLadderPlayerMap rows: $keptRows"
    Write-Host "Other activity tables reset to empty: $($ResetTables.Count)"
    Write-Host "Backup directory: $backupDir"
}
finally {
    if ($null -eq $oldMySqlPwd) { Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue }
    else { $env:MYSQL_PWD = $oldMySqlPwd }
}
