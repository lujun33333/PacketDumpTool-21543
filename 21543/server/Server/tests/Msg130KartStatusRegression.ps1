$ErrorActionPreference = 'Stop'

$serverRoot = Split-Path -Parent $PSScriptRoot
$playerPath = Join-Path $serverRoot 'Player.cpp'
$player = Get-Content -LiteralPath $playerPath -Raw

foreach ($required in @(
    'priorKartAlreadyUnequipped',
    'status.ItemID == priorKartID && status.NewStatus == 0',
    '!priorKartAlreadyUnequipped'
)) {
    if (-not $player.Contains($required)) {
        throw "Msg130 prior-kart deduplication source is missing: $required"
    }
}

function Build-AckStatuses {
    param(
        [Parameter(Mandatory)]
        [object[]]$RequestStatuses,
        [uint32]$PriorKartID,
        [uint32]$NewKartID
    )

    $ack = [System.Collections.Generic.List[object]]::new()
    foreach ($status in $RequestStatuses) {
        $ack.Add($status)
    }

    $priorAlreadyUnequipped = $false
    foreach ($status in $ack) {
        if ($status.ItemID -eq $PriorKartID -and $status.NewStatus -eq 0) {
            $priorAlreadyUnequipped = $true
            break
        }
    }

    if ($NewKartID -ne 0 -and
        $PriorKartID -ne 0 -and
        $PriorKartID -ne $NewKartID -and
        -not $priorAlreadyUnequipped) {
        $ack.Add([pscustomobject]@{ ItemID = $PriorKartID; NewStatus = 0 })
    }

    return @($ack)
}

function Assert-Statuses {
    param(
        [Parameter(Mandatory)]
        [object[]]$Actual,
        [Parameter(Mandatory)]
        [string[]]$Expected,
        [Parameter(Mandatory)]
        [string]$Case
    )

    $encoded = @($Actual | ForEach-Object { '{0}:{1}' -f $_.ItemID, $_.NewStatus })
    if (($encoded -join ',') -ne ($Expected -join ',')) {
        throw "$Case failed: actual=[$($encoded -join ',')] expected=[$($Expected -join ',')]"
    }
}

$newClientRequest = @(
    [pscustomobject]@{ ItemID = 171075; NewStatus = 1 },
    [pscustomobject]@{ ItemID = 163043; NewStatus = 0 }
)
Assert-Statuses `
    -Actual (Build-AckStatuses -RequestStatuses $newClientRequest -PriorKartID 163043 -NewKartID 171075) `
    -Expected @('171075:1', '163043:0') `
    -Case '21622 request already contains prior kart'

$legacyRequest = @(
    [pscustomobject]@{ ItemID = 171075; NewStatus = 1 }
)
Assert-Statuses `
    -Actual (Build-AckStatuses -RequestStatuses $legacyRequest -PriorKartID 163043 -NewKartID 171075) `
    -Expected @('171075:1', '163043:0') `
    -Case 'legacy request omits prior kart'

$retryRequest = @(
    [pscustomobject]@{ ItemID = 163043; NewStatus = 1 },
    [pscustomobject]@{ ItemID = 171075; NewStatus = 0 }
)
Assert-Statuses `
    -Actual (Build-AckStatuses -RequestStatuses $retryRequest -PriorKartID 163043 -NewKartID 163043) `
    -Expected @('163043:1', '171075:0') `
    -Case 'idempotent retry'

Write-Output 'Msg130KartStatusRegression PASS'
