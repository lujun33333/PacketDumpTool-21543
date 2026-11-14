$ErrorActionPreference = 'Stop'

$serverDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "vcvars64.bat not found: $vcvars"
}

$buildDir = Join-Path $env:TEMP ("UsePropProtocolRegression_{0}" -f $PID)
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$exe = Join-Path $buildDir 'UsePropProtocolRegression.exe'

try {
    $configuredEnvironment = & cmd.exe /d /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars64.bat failed with exit code $LASTEXITCODE"
    }
    foreach ($line in $configuredEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }

    $compileArgs = @(
        '/nologo',
        '/std:c++17',
        '/EHsc',
        '/W4',
        '/D_CRT_SECURE_NO_WARNINGS',
        ("/Fo{0}{1}" -f $buildDir, [System.IO.Path]::DirectorySeparatorChar),
        ("/Fd{0}" -f (Join-Path $buildDir 'UsePropProtocolRegression.pdb')),
        "/I$serverDir",
        (Join-Path $serverDir 'UsePropProtocol.cpp'),
        (Join-Path $PSScriptRoot 'UsePropProtocolRegression.cpp'),
        '/link',
        "/OUT:$exe"
    )
    & cl.exe @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "UseProp protocol regression compile failed with exit code $LASTEXITCODE"
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "UseProp protocol regression failed with exit code $LASTEXITCODE"
    }

    $gameSource = Get-Content -LiteralPath (Join-Path $serverDir 'GameServer.cpp') -Raw
    $messageSource = Get-Content -LiteralPath (Join-Path $serverDir 'Message.cpp') -Raw
    $messageHeader = Get-Content -LiteralPath (Join-Path $serverDir 'Message.h') -Raw
    $playerSource = Get-Content -LiteralPath (Join-Path $serverDir 'Player.cpp') -Raw
    $requestStart = $gameSource.IndexOf('void RequestUseProp2(', [StringComparison]::Ordinal)
    $requestEnd = $gameSource.IndexOf('void NotifyAddPropByxiaolv(', $requestStart, [StringComparison]::Ordinal)
    if ($requestStart -lt 0 -or $requestEnd -lt 0) {
        throw 'RequestUseProp2 source section not found'
    }
    $requestSource = $gameSource.Substring($requestStart, $requestEnd - $requestStart)
    $notify908Start = $gameSource.IndexOf('void NotifyAddPropBySkillStone(', [StringComparison]::Ordinal)
    $notify908End = $gameSource.IndexOf('void NotifyLM2CCooperInc(', $notify908Start, [StringComparison]::Ordinal)
    if ($notify908Start -lt 0 -or $notify908End -lt 0) {
        throw 'NotifyAddPropBySkillStone source section not found'
    }
    $notify908Source = $gameSource.Substring($notify908Start, $notify908End - $notify908Start)
    if ($requestSource -notmatch 'ParseRequest165\(' -or
        $requestSource -notmatch 'SerializeNotify517\(' -or
        $requestSource -notmatch 'SendToClientWithWireDstID\([\s\S]*?517[\s\S]*?FE_GAMELOGIC') {
        throw 'Msg165 handler is not wired to strict parse/serialize and official Msg517 game-logic route'
    }
    if ($notify908Source -notmatch
        'SendToClientWithWireDstIDAndMsgSeq\([\s\S]*?908[\s\S]*?Client->GameID[\s\S]*?FE_GAMELOGIC[\s\S]*?GetClientWirePlayerHeaderID\(Client\)[\s\S]*?\b0,\s*Notify\)' -or
        $notify908Source -match '908[\s\S]*?FE_PLAYER') {
        throw 'Msg908 is not using the captured official game-logic route'
    }
    if ($requestSource -notmatch 'TryActivateServerSkillStone\(' -or
        $requestSource -match 'request\.stoneSkillType\s*>=') {
        throw 'Msg165 is not using the official server-authoritative rebirth path'
    }
    if ($gameSource -match 'IsVisibleSuperJetPropAlias|void\s+NotifyUseProp\(' -or
        $gameSource -match '\bProbTime\b' -or
        $messageHeader -match '\blastUpdateTime\b') {
        throw 'legacy fixed/alias/timed-injection UseProp residue remains'
    }
    if ($messageSource -notmatch 'MsgID\s*==\s*165' -or
        $messageSource -notmatch 'return MsgID == 112[\s\S]*?MsgID == 165[\s\S]*?MsgID == 517') {
        throw 'Msg165 framing or high-frequency 165/517 debug suppression is missing'
    }
    $dispatchStart = $messageSource.LastIndexOf('case 165:', [StringComparison]::Ordinal)
    $dispatchEnd = $messageSource.IndexOf('case 174:', $dispatchStart, [StringComparison]::Ordinal)
    if ($dispatchStart -lt 0 -or $dispatchEnd -lt 0) {
        throw 'Msg165 dispatch section not found'
    }
    $dispatchSource = $messageSource.Substring($dispatchStart, $dispatchEnd - $dispatchStart)
    if ($dispatchSource -notmatch 'RequestUseProp2\(Client, Body, BodyLen\)' -or
        $dispatchSource -match 'StoneOpenFlag|GameID\s*!=\s*0') {
        throw 'Msg165 dispatch still has the legacy respawn-stone/game-id gate'
    }
    if ($gameSource -match '\bStoneOpenFlag\b' -or
        $messageHeader -match '\bStoneOpenFlag\b' -or
        $playerSource -match '\bStoneOpenFlag\b') {
        throw 'unused legacy StoneOpenFlag state remains'
    }
    $flowStart = $gameSource.IndexOf('static void QQS_FlowFileLog(', [StringComparison]::Ordinal)
    $flowEnd = $gameSource.IndexOf('template<typename ByteT>', $flowStart, [StringComparison]::Ordinal)
    if ($flowStart -lt 0 -or $flowEnd -lt 0 -or
        $gameSource.Substring($flowStart, $flowEnd - $flowStart) -match '->flush\(') {
        throw 'QQS_FlowFileLog still forces synchronous disk flushes'
    }
}
finally {
    Remove-Item -LiteralPath $buildDir -Recurse -Force -ErrorAction SilentlyContinue
}
