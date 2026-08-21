param(
    [string]$Version = '',
    [string]$GatewayBuildDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path
$repoRoot = (Resolve-Path (Join-Path $scriptRoot '../..')).Path
$versionFile = Join-Path $scriptRoot 'VERSION'
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -Raw $versionFile).Trim()
}
$packageName = "ble-mesh-gateway-rpi-v$Version"
$outputRoot = Join-Path $repoRoot 'output'
$stageRoot = Join-Path $outputRoot $packageName
$archivePath = Join-Path $outputRoot "$packageName.tar.gz"

function Convert-ToLinuxTextFile([string]$Path) {
    $content = [System.IO.File]::ReadAllText($Path)
    $normalized = $content.Replace("`r`n", "`n").Replace("`r", "`n")
    [System.IO.File]::WriteAllText($Path, $normalized, [System.Text.UTF8Encoding]::new($false))
}

if ([string]::IsNullOrWhiteSpace($GatewayBuildDirectory)) {
    $gatewayCandidates = @(
        (Join-Path $repoRoot 'apps/gateway/build_2/gateway'),
        (Join-Path $repoRoot 'apps/gateway/build_1/gateway'),
        (Join-Path $repoRoot 'apps/gateway/build/gateway'),
        (Join-Path $repoRoot 'build/gateway')
    )
    $GatewayBuildDirectory = $gatewayCandidates | Where-Object { Test-Path (Join-Path $_ 'zephyr/zephyr.uf2') } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($GatewayBuildDirectory)) {
    throw '没有找到网关构建目录，请使用 -GatewayBuildDirectory 指定。'
}
$GatewayBuildDirectory = (Resolve-Path $GatewayBuildDirectory).Path
$gatewayZephyr = Join-Path $GatewayBuildDirectory 'zephyr'
$gatewayUf2 = Join-Path $gatewayZephyr 'zephyr.uf2'
$gatewayHex = Join-Path $GatewayBuildDirectory 'merged.hex'
if (-not (Test-Path $gatewayHex)) {
    $gatewayHex = Join-Path $gatewayZephyr 'zephyr.hex'
}
if (-not (Test-Path $gatewayUf2)) { throw "网关 UF2 不存在：$gatewayUf2" }
if (-not (Test-Path $gatewayHex)) { throw "网关 HEX 不存在：$gatewayHex" }

if (Test-Path $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
if (Test-Path $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
New-Item -ItemType Directory -Path $stageRoot | Out-Null

$packageFiles = @('main', 'install.sh', 'update.sh', 'uninstall.sh', 'VERSION', 'README.md')
foreach ($fileName in $packageFiles) {
    Copy-Item -LiteralPath (Join-Path $scriptRoot $fileName) -Destination $stageRoot
}
foreach ($directoryName in @('config', 'systemd')) {
    Copy-Item -LiteralPath (Join-Path $scriptRoot $directoryName) -Destination $stageRoot -Recurse
}
$releaseReadme = Join-Path $stageRoot 'README.md'
$releaseReadmeContent = [System.IO.File]::ReadAllText($releaseReadme).Replace('v1.0.0', "v$Version")
[System.IO.File]::WriteAllText($releaseReadme, $releaseReadmeContent, [System.Text.UTF8Encoding]::new($false))

$appSource = Join-Path $repoRoot 'node-red'
$appDestination = Join-Path $stageRoot 'app'
New-Item -ItemType Directory -Path $appDestination | Out-Null
$excludedNames = @('node_modules', 'data', '.env', 'flows_cred.json', '.config.nodes.json', '.config.runtime.json', '.config.runtime.json.backup', '.config.users.json', '.config.users.json.backup', 'runtime.stdout.log', 'runtime.stderr.log')
Get-ChildItem -LiteralPath $appSource -Force | Where-Object { $excludedNames -notcontains $_.Name } | Copy-Item -Destination $appDestination -Recurse -Force

$firmwareDestination = Join-Path $stageRoot 'firmware'
New-Item -ItemType Directory -Path $firmwareDestination | Out-Null
Copy-Item -LiteralPath $gatewayUf2 -Destination (Join-Path $firmwareDestination 'gateway.uf2')
Copy-Item -LiteralPath $gatewayHex -Destination (Join-Path $firmwareDestination 'gateway-merged.hex')
$gatewayBin = Join-Path $gatewayZephyr 'zephyr.bin'
if (Test-Path $gatewayBin) {
    Copy-Item -LiteralPath $gatewayBin -Destination (Join-Path $firmwareDestination 'gateway.bin')
}

$textExtensions = @('.env', '.js', '.json', '.md', '.ps1', '.service', '.sh', '.yaml', '.yml')
$textFileNames = @('Dockerfile', 'main', 'VERSION')
Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object {
    $textExtensions -contains $_.Extension -or $textFileNames -contains $_.Name
} | ForEach-Object {
    Convert-ToLinuxTextFile $_.FullName
}

$metadata = [ordered]@{
    package = $packageName
    version = $Version
    built_at = (Get-Date).ToUniversalTime().ToString('o')
    gateway_build_directory = $GatewayBuildDirectory
    node_red_version = '5.0.4'
    node_version = '24.19.0'
    pnpm_version = '9.15.9'
    target = 'Raspberry Pi OS/Debian headless, aarch64'
}
$metadata | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $stageRoot 'RELEASE_METADATA.json')

$hashLines = foreach ($file in (Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Sort-Object FullName)) {
    $relativePath = $file.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relativePath"
}
$hashLines | Set-Content -Encoding ASCII (Join-Path $stageRoot 'SHA256SUMS.txt')

if (-not (Get-Command tar.exe -ErrorAction SilentlyContinue)) {
    throw '未找到 tar.exe；请使用 Windows 10/11 自带 tar，或安装 bsdtar。'
}
& tar.exe -czf $archivePath -C $outputRoot $packageName
if ($LASTEXITCODE -ne 0) { throw "tar 打包失败，退出码：$LASTEXITCODE" }

Write-Output "发行包已生成：$archivePath"
Write-Output "暂存目录：$stageRoot"
Write-Output "网关固件来源：$GatewayBuildDirectory"
