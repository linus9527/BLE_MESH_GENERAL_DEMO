param(
    [ValidatePattern('^(auto|COM\d+)$')]
    [string]$SerialPort = 'auto',

    [string]$MqttHost = '127.0.0.1',
    [ValidateRange(1, 65535)]
    [int]$MqttPort = 1883,
    [switch]$EnableMqtt
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $projectDirectory

$packageManager = if (Get-Command pnpm -ErrorAction SilentlyContinue) {
    'pnpm'
} elseif (Get-Command npm -ErrorAction SilentlyContinue) {
    'npm'
} else {
    throw '未找到 npm 或 pnpm。请先安装 Node.js 24。'
}

if (-not (Test-Path (Join-Path $projectDirectory 'node_modules'))) {
    & $packageManager install
}

$env:SERIAL_PORT = $SerialPort
$env:MQTT_ENABLED = if ($EnableMqtt) { 'true' } else { 'false' }
$env:MQTT_HOST = $MqttHost
$env:MQTT_PORT = [string]$MqttPort
$env:MQTT_CLIENT_ID = 'ble-mesh-gateway-windows'
$env:MQTT_TOPIC_PREFIX = 'ble_mesh'

& $packageManager start
