'use strict'

const fs = require('fs')
const path = require('path')
const { spawn, spawnSync } = require('child_process')

function assertSupportedNodeVersion() {
    const [major, minor] = process.versions.node.split('.').map(Number)
    if (major < 22 || (major === 22 && minor < 13)) {
        throw new Error(`Node.js ${process.versions.node} 不支持内置 SQLite，请安装 Node.js 24。`)
    }
}

assertSupportedNodeVersion()

const { SerialPort } = require('serialport')

const projectDirectory = path.resolve(__dirname, '..')
const explicitPort = String(process.env.SERIAL_PORT || 'auto').trim()
const probeTimeoutMs = Number(process.env.SERIAL_PROBE_TIMEOUT_MS || 3200)

function delay(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds))
}

function portDescription(port) {
    return [port.path, port.manufacturer, port.friendlyName, port.pnpId, port.vendorId, port.productId]
        .filter(Boolean)
        .join(' ')
        .toLowerCase()
}

function portScore(port) {
    const description = portDescription(port)
    let score = 0
    if (/nordic|nrf|zephyr|cdc|dongle/.test(description)) {
        score += 100
    }
    if (port.vendorId || port.productId) {
        score += 20
    }
    if (/bluetooth/.test(description)) {
        score -= 100
    }
    if (/^com\d+$/i.test(port.path) || /ttyacm|ttyusb|serial\/by-id/i.test(port.path)) {
        score += 10
    }
    return score
}

async function linuxStablePorts() {
    if (process.platform === 'win32') {
        return []
    }
    const directory = '/dev/serial/by-id'
    try {
        const entries = await fs.promises.readdir(directory)
        return entries.map((entry) => ({
            path: path.join(directory, entry),
            friendlyName: entry,
            stablePath: true
        }))
    } catch {
        return []
    }
}

async function canonicalPortPath(portPath) {
    try {
        return await fs.promises.realpath(portPath)
    } catch {
        return path.normalize(portPath)
    }
}

async function portIdentity(portPath, canonicalPath) {
    try {
        const stats = await fs.promises.stat(portPath)
        if (stats.rdev !== undefined) {
            return `rdev:${stats.rdev.toString()}`
        }
    } catch {
    }
    return `path:${canonicalPath.toLowerCase()}`
}

async function listCandidates() {
    const listed = await SerialPort.list()
    const stable = await linuxStablePorts()
    const candidates = [...stable, ...listed]
    const unique = new Map()
    for (const candidate of candidates) {
        if (!candidate.path || /bluetooth/i.test(portDescription(candidate))) {
            continue
        }
        const canonicalPath = await canonicalPortPath(candidate.path)
        const key = await portIdentity(candidate.path, canonicalPath)
        const existing = unique.get(key)
        if (!existing) {
            unique.set(key, {
                ...candidate,
                canonicalPath,
                aliases: [candidate.path]
            })
            continue
        }

        const preferCandidate = Boolean(candidate.stablePath) && !existing.stablePath
        unique.set(key, {
            ...existing,
            ...candidate,
            path: preferCandidate ? candidate.path : existing.path,
            stablePath: Boolean(existing.stablePath || candidate.stablePath),
            canonicalPath,
            aliases: [...new Set([...(existing.aliases || []), candidate.path])],
            manufacturer: existing.manufacturer || candidate.manufacturer,
            vendorId: existing.vendorId || candidate.vendorId,
            productId: existing.productId || candidate.productId
        })
    }
    return [...unique.values()].sort((left, right) => portScore(right) - portScore(left))
}

function isGatewayEvent(line) {
    try {
        const event = JSON.parse(line)
        return event && typeof event.type === 'string' && (
            event.type === 'gateway_online' ||
            String(event.device_id || '').startsWith('BLE_MESH_')
        )
    } catch {
        return false
    }
}

async function probePort(candidate) {
    return new Promise((resolve) => {
        const serial = new SerialPort({
            path: candidate.path,
            baudRate: 115200,
            dataBits: 8,
            parity: 'none',
            stopBits: 1,
            autoOpen: false
        })
        let buffer = ''
        let finished = false

        function finish(matched) {
            if (finished) {
                return
            }
            finished = true
            clearTimeout(timer)
            if (serial.isOpen) {
                serial.close(() => resolve(matched))
            } else {
                resolve(matched)
            }
        }

        const timer = setTimeout(() => finish(false), probeTimeoutMs)
        serial.on('data', (chunk) => {
            buffer += chunk.toString('utf8')
            const lines = buffer.split(/\r?\n/)
            buffer = lines.pop() || ''
            if (lines.some((line) => isGatewayEvent(line.trim()))) {
                finish(true)
            }
        })
        serial.on('error', () => finish(false))
        serial.open((error) => {
            if (error) {
                finish(false)
                return
            }
        })
    })
}

async function detectSerialPort() {
    if (explicitPort && explicitPort.toLowerCase() !== 'auto') {
        return explicitPort
    }

    let candidates = []
    for (let attempt = 1; attempt <= 5; attempt += 1) {
        candidates = await listCandidates()
        if (candidates.length) {
            break
        }
        console.log(`[serial] 第 ${attempt}/5 次扫描未发现串口，2 秒后重试。`)
        await delay(2000)
    }

    if (!candidates.length) {
        throw new Error('自动扫描未发现可用串口。请连接网关，或显式设置 SERIAL_PORT。')
    }

    if (candidates.length === 1) {
        console.log(`[serial] 自动选择唯一串口 ${candidates[0].path}`)
        return candidates[0].path
    }

    console.log(`[serial] 发现 ${candidates.length} 个候选串口，正在识别 BLE Mesh 网关。`)
    for (const candidate of candidates.slice(0, 8)) {
        console.log(`[serial] 探测 ${candidate.path}`)
        if (await probePort(candidate)) {
            console.log(`[serial] 已通过网关 JSON 识别 ${candidate.path}`)
            return candidate.path
        }
    }

    const best = candidates[0]
    if (portScore(best) >= 100) {
        console.log(`[serial] 未收到探测数据，按设备描述选择 ${best.path}`)
        return best.path
    }
    throw new Error(`存在多个串口但无法识别网关：${candidates.map((port) => port.path).join(', ')}`)
}

function resolveNodeRedEntry() {
    try {
        return require.resolve('node-red/red.js')
    } catch {
        const dockerEntry = '/usr/src/node-red/node_modules/node-red/red.js'
        if (fs.existsSync(dockerEntry)) {
            return dockerEntry
        }
        throw new Error('未找到 Node-RED 运行入口。')
    }
}

async function main() {
    const serialPort = await detectSerialPort()
    process.env.SERIAL_PORT = serialPort
    fs.mkdirSync(process.env.HISTORY_DATA_DIR || path.join(projectDirectory, 'data'), { recursive: true })

    const child = spawn(process.execPath, [
        resolveNodeRedEntry(),
        '--userDir', projectDirectory,
        '--settings', path.join(projectDirectory, 'settings.js'),
        path.join(projectDirectory, 'flows.json')
    ], {
        cwd: projectDirectory,
        env: process.env,
        stdio: 'inherit'
    })

    let shuttingDown = false
    let childExited = false
    for (const signal of ['SIGINT', 'SIGTERM']) {
        process.on(signal, () => {
            if (shuttingDown) {
                return
            }
            shuttingDown = true
            if (process.platform === 'win32') {
                child.kill()
            } else {
                child.kill(signal)
            }
            setTimeout(() => {
                if (!childExited) {
                    if (process.platform === 'win32') {
                        spawnSync('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'], {
                            stdio: 'ignore',
                            windowsHide: true
                        })
                    } else {
                        child.kill('SIGKILL')
                    }
                }
            }, 3000).unref()
        })
    }
    child.on('exit', (code, signal) => {
        childExited = true
        if (signal) {
            process.kill(process.pid, signal)
        } else {
            process.exit(code ?? 1)
        }
    })
}

main().catch((error) => {
    console.error(`[startup] ${error.message}`)
    process.exit(1)
})
