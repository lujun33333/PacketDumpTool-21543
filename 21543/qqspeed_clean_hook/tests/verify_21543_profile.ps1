$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $root 'hook_config.h'
$runtimePath = Join-Path $root 'hook_runtime.cpp'
$kartRefitPath = Join-Path $root 'network\kart_refit_shadow.cpp'
$minimalPath = Join-Path $root 'production\hook_runtime_minimal.cpp'
$topKartProfilePath = Join-Path $root 'topkart\protected_topkart_profile.cpp'
$config = Get-Content -LiteralPath $configPath -Raw
$runtime = Get-Content -LiteralPath $runtimePath -Raw
$kartRefit = Get-Content -LiteralPath $kartRefitPath -Raw
$minimal = Get-Content -LiteralPath $minimalPath -Raw

function Assert-Contains {
	param(
		[string]$Text,
		[string]$Expected,
		[string]$Path
	)

	if (-not $Text.Contains($Expected)) {
		throw "Missing expected current profile marker in ${Path}: $Expected"
	}
}

function Assert-NotContains {
	param(
		[string]$Text,
		[string]$Unexpected,
		[string]$Path
	)

	if ($Text.Contains($Unexpected)) {
		throw "Stale profile marker remains in ${Path}: $Unexpected"
	}
}

$requiredConfigMarkers = @(
	'struct NetworkIdentity',
	'std::array<NetworkFingerprint, 1> acceptedFingerprints',
	'{ "official-auto-97d25c1c", 0x968000, "97d25c1c4cf7730f26c226cdc6e8bb3c" }',
	'std::size_t maxKnownImageSize = 0x968000;',
	'#if !defined(QQSPEED_VMP_PROTECTION)',
	'struct NetworkOffsets : NetworkIdentity',
	'std::uintptr_t unpack = 0x42D8C0;',
	'std::uintptr_t innerDispatch = 0x421670;',
	'std::uintptr_t gameSocketOnMyReceiveUnpackCall = 0x42D77B;',
	'std::uintptr_t directInnerDispatchCall = 0x42DF50;',
	'RvaList<8> unpackCallSites = { { 0x42D77B }, 1 };',
	'RvaList<4> innerDispatchCallSites = { { 0x42DF50 }, 1 };',
	'NetworkIdentity network;',
	'NetworkOffsets network;',
	'bool enableConnectRedirect = true;',
	'bool enableNetworkHooks = true;',
	'bool enableNetworkUnpackMsg100ConnIdPatch = true;',
	'bool enableNetworkInnerDispatchPlainExpand = true;',
	'bool enablePlainExpandMsg100 = true;',
	'bool enableNetworkPackSerializerBodyRepair = false;',
	'bool enableNetworkSerializeBodyRepair = false;',
	'bool enableNetworkSpecial28346DecodeHook = false;',
	'bool enablePacketFlowLog = false;',
	'bool enableTopKartAntiCheckerConditional = true;'
)
foreach ($marker in $requiredConfigMarkers) {
	Assert-Contains -Text $config -Expected $marker -Path $configPath
}

$requiredRuntimeMarkers = @(
	'kTopKartCurrentImageSize = 0x3D63000;',
	'kTopKartCurrentMd5[] = "26b621a8dc3eaf027486081f9b67cec2";',
	'kTopKartAntiCheckerExecuteRva = 0x342D90;',
	'kTopKartAntiCheckerExecuteSlotRva = 0x2B55F98;',
	'kTopKartAceInterfaceGlobalRva = 0x36C2D08;',
	'kTopKartAntiCheckerOriginal = { 0xE9, 0x24, 0xAE, 0x62, 0x03 };',
	'profile=current',
	'candidates=official-auto-97d25c1c:0x968000/97d25c1c4cf7730f26c226cdc6e8bb3c',
	'ValidateTopKartModule(',
	'topkart_preflight accepted=%d',
	'ResolveProtectedNetworkProfile(',
	'ClearResolvedNetworkProfile(',
	'ResolveProtectedTopKartCoreProfile(',
	'ClearResolvedTopKartCoreProfile(',
	'PatchTopKartAntiCheckerConditionalGate(',
	'WaitForTopKartAndInstallConsumers(',
	'patchKind=two_pointer_slots',
	'validFeedRequired=1'
)
foreach ($marker in $requiredRuntimeMarkers) {
	Assert-Contains -Text $runtime -Expected $marker -Path $runtimePath
}

$requiredKartRefitMarkers = @(
	'kNetworkMergeRva = 0x4E28D0;',
	'0x4E4930,',
	'0x4E540A,',
	'0x4E5E75',
	'kTopKartHandlerRva = 0x12F1800;',
	'kTopKartHandlerSlotRva = 0x2C56F08;',
	'kTopKartInsertRva = 0x12E4BC0;',
	'kTopKartInsertCallRva = 0x12F18AE;'
)
foreach ($marker in $requiredKartRefitMarkers) {
	Assert-Contains -Text $kartRefit -Expected $marker -Path $kartRefitPath
}

Assert-Contains `
	-Text $minimal `
	-Expected 'constexpr std::size_t kNetworkImageSize = 0x968000;' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected '0x97, 0xD2, 0x5C, 0x1C, 0x4C, 0xF7, 0x73, 0x0F,' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected '0x26, 0xC2, 0x26, 0xCD, 0xC6, 0xE8, 0xBB, 0x3C' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected 'constexpr std::size_t kTopKartImageSize = 0x3D63000;' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected '0x26, 0xB6, 0x21, 0xA8, 0xDC, 0x3E, 0xAF, 0x02,' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected '0x74, 0x86, 0x08, 0x1F, 0x9B, 0x67, 0xCE, 0xC2' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected 'kAntiCheckerOriginal = { 0xE9, 0x24, 0xAE, 0x62, 0x03 };' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected 'constexpr bool kEnableAntiCheckerConditional = true;' `
	-Path $minimalPath
Assert-Contains `
	-Text $minimal `
	-Expected 'if constexpr (kEnableAntiCheckerConditional)' `
	-Path $minimalPath

foreach ($staleMarker in @(
	'0xEAC030',
	'0x238DB88',
	'0xEA19B0',
	'0xEAC0DE',
	'0xEABB40',
	'0x238CB88',
	'0xEA14C0',
	'0xEABBEE',
	'0x406C30',
	'0x408922',
	'0x409462',
	'0x409F43',
	'0xEB90F0',
	'0x23F4DB8',
	'0xEAEA70',
	'0xEB919E'
)) {
	Assert-NotContains -Text $kartRefit -Unexpected $staleMarker -Path $kartRefitPath
}

foreach ($staleMarker in @(
	'official-auto-0c0c733d',
	'0c0c733d4b171e27aff47da15a6ec764',
	'0x843000',
	'official-auto-498b8c58',
	'498b8c5821f7ff5e41daed6ffb3430b6',
	'official-auto-0679c6a3',
	'0679c6a324679bbe967e7e8771cf2dc3',
	'0x846000',
	'official-auto-b38091f6',
	'b38091f6900d6805a5ce5941ccec8f28',
	'0x840000',
	'official-auto-458ab2c6',
	'458ab2c6052c61d3dec3f0435c057549',
	'0x841000'
)) {
	Assert-NotContains -Text $config -Unexpected $staleMarker -Path $configPath
}
foreach ($staleMarker in @(
	'kTopKart21482ImageSize',
	'kTopKart21482Md5',
	'kTopKart21622ImageSize',
	'kTopKart21622Md5',
	'23263a5accab594ad4d8467f44ac38f1',
	'0x3149000',
	'TopKartAntiCheckerNoop',
	'enableTopKartAntiCheckerDisable',
	'expected=E9753EBB02',
	'2a98d65282ad8d96f4a4b83732340fe5',
	'0x3407000',
	'0xE9, 0x40, 0xC5, 0xE4, 0x02',
	'719621d741ca1e29d5d3b870afd71f47',
	'0x340A000',
	'0xE9, 0x0B, 0xE2, 0xE4, 0x02',
	'c4f5cf8a0089a387d552ab31bb49fbcb',
	'0x3405000',
	'0xE9, 0x64, 0xE1, 0xE4, 0x02'
)) {
	Assert-NotContains -Text $runtime -Unexpected $staleMarker -Path $runtimePath
}

if (-not (Test-Path -LiteralPath $topKartProfilePath)) {
	throw "Protected current Top-Kart profile source is missing: $topKartProfilePath"
}

Write-Host 'QQ Speed current Network/Top-Kart conditional profile verification passed.'
