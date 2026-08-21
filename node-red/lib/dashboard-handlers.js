'use strict'

const SENSOR_DEFINITIONS = {
    BLE_MESH_PH: {
        label: 'pH',
        unit: 'pH',
        reportType: 'ph_report',
        readingField: 'ph',
        stateOutput: 0,
        readingOutput: 1,
        temperatureOutput: 2,
        historyReadingOutput: 0,
        historyTemperatureOutput: 1
    },
    BLE_MESH_DO: {
        label: '溶解氧',
        unit: 'mg/L',
        reportType: 'do_report',
        readingField: 'dissolved_oxygen_mg_l',
        stateOutput: 3,
        readingOutput: 4,
        temperatureOutput: 5,
        historyReadingOutput: 2,
        historyTemperatureOutput: 3
    },
    BLE_MESH_ORP: {
        label: 'ORP',
        unit: 'mV',
        reportType: 'orp_report',
        readingField: 'orp_mv',
        stateOutput: 6,
        readingOutput: 7,
        temperatureOutput: 8,
        historyReadingOutput: 4,
        historyTemperatureOutput: 5
    }
}

const HISTORY_OUTPUT = 9
const SENSOR_OUTPUT_COUNT = 10
const HISTORY_OUTPUT_COUNT = 6
const OFFLINE_TIMEOUT_MS = 35000
const GATEWAY_SILENCE_MS = 15000

function finiteNumber(value) {
    if (value === null || value === undefined || value === '') {
        return null
    }
    const number = Number(value)
    return Number.isFinite(number) ? number : null
}

function createSensorStates() {
    const states = {}
    for (const [deviceId, definition] of Object.entries(SENSOR_DEFINITIONS)) {
        states[deviceId] = {
            deviceId,
            deviceName: '',
            label: definition.label,
            unit: definition.unit,
            seen: false,
            online: false,
            sensorError: false,
            error: '',
            dataWarning: '',
            reading: null,
            temperature: null,
            meshAddress: '',
            gatewayTimestampMs: null,
            lastSeenAt: null,
            lastReportAt: null
        }
    }
    return states
}

function sensorStatus(state) {
    if (!state.seen) {
        return { code: 'waiting', text: '等待数据', color: '#78909c' }
    }
    if (!state.online) {
        return { code: 'offline', text: '设备离线', color: '#e53935' }
    }
    if (state.sensorError) {
        const detail = state.error ? `：${state.error}` : ''
        return { code: 'error', text: `传感器故障${detail}`, color: '#e53935' }
    }
    if (state.dataWarning) {
        return { code: 'warning', text: state.dataWarning, color: '#fb8c00' }
    }
    return { code: 'online', text: '在线 / 数据正常', color: '#43a047' }
}

function displayTime(timestamp) {
    if (!timestamp) {
        return '暂无'
    }
    return new Date(timestamp).toLocaleString('zh-CN', { hour12: false })
}

function normaliseOutputs(outputs) {
    return outputs.map((messages) => messages.length ? messages : null)
}

function emitSensorState(outputs, states, deviceId, now) {
    const state = states[deviceId]
    const definition = SENSOR_DEFINITIONS[deviceId]
    const status = sensorStatus(state)
    const reading = state.online ? state.reading : null
    const temperature = state.online ? state.temperature : null
    outputs[definition.stateOutput].push({
        payload: {
            deviceId: state.deviceId,
            deviceName: state.deviceName,
            meshAddress: state.meshAddress,
            reading,
            temperature,
            unit: state.unit,
            statusCode: status.code,
            statusText: status.text,
            statusColor: status.color,
            lastReportText: displayTime(state.lastReportAt),
            lastSeenText: displayTime(state.lastSeenAt)
        },
        timestamp: now
    })
}

function emitChartPoint(outputs, outputIndex, value, topic, now) {
    if (value === null) {
        return
    }
    outputs[outputIndex].push({
        payload: { x: now, y: value },
        topic,
        timestamp: now,
        action: 'append',
        ui_update: {
            chartOptions: {
                xAxis: { max: now }
            }
        }
    })
}

function updateSensors(msg, flow, now = Date.now()) {
    let states = flow.get('sensorStates')
    const firstRun = !states
    if (!states) {
        states = createSensorStates()
    }

    const outputs = Array.from({ length: SENSOR_OUTPUT_COUNT }, () => [])

    if (firstRun) {
        for (const deviceId of Object.keys(SENSOR_DEFINITIONS)) {
            emitSensorState(outputs, states, deviceId, now)
        }
    }

    if (msg.topic === 'watchdog') {
        for (const deviceId of Object.keys(SENSOR_DEFINITIONS)) {
            const state = states[deviceId]
            if (state.online && state.lastSeenAt && now - state.lastSeenAt >= OFFLINE_TIMEOUT_MS) {
                state.online = false
                emitSensorState(outputs, states, deviceId, now)
            }
        }
        flow.set('sensorStates', states)
        return normaliseOutputs(outputs)
    }

    const event = msg.payload
    if (!event || typeof event !== 'object') {
        flow.set('sensorStates', states)
        return normaliseOutputs(outputs)
    }

    const deviceId = event.device_id
    const definition = SENSOR_DEFINITIONS[deviceId]
    if (!definition) {
        flow.set('sensorStates', states)
        return normaliseOutputs(outputs)
    }

    const state = states[deviceId]
    state.deviceName = event.device_name || state.deviceName
    state.meshAddress = event.mesh_addr || state.meshAddress
    state.gatewayTimestampMs = finiteNumber(event.timestamp_ms) ?? state.gatewayTimestampMs
    state.lastSeenAt = now
    state.seen = true

    let updateDashboard = false

    if (event.type === definition.reportType) {
        const reading = finiteNumber(event[definition.readingField])
        const temperature = finiteNumber(event.temperature_c)

        if (reading !== null) {
            state.reading = reading
            emitChartPoint(outputs, definition.readingOutput, reading, definition.label, now)
        }
        if (temperature !== null) {
            state.temperature = temperature
            emitChartPoint(outputs, definition.temperatureOutput, temperature, '温度', now)
        }

        if (reading !== null || temperature !== null) {
            state.lastReportAt = now
            state.online = true
            state.sensorError = false
            state.error = ''
            const missing = []
            if (reading === null) {
                missing.push('主读数')
            }
            if (temperature === null) {
                missing.push('温度')
            }
            state.dataWarning = missing.length ? `数据不完整：缺少${missing.join('、')}` : ''
            outputs[HISTORY_OUTPUT].push({
                payload: {
                    deviceId,
                    reading,
                    temperature,
                    receivedAt: now
                }
            })
            updateDashboard = true
        } else {
            state.online = true
            state.dataWarning = '数据格式异常：主读数和温度均无效'
            updateDashboard = true
        }
    } else if (event.type === 'device_online') {
        state.online = true
        updateDashboard = true
    } else if (event.type === 'device_offline') {
        state.online = false
        updateDashboard = true
    } else if (event.type === 'sensor_error') {
        state.online = true
        state.sensorError = true
        state.error = event.error || 'read_failed'
        updateDashboard = true
    } else if (event.type === 'sensor_recovered') {
        state.online = true
        state.sensorError = false
        state.error = ''
        updateDashboard = true
    } else if (event.type === 'device_heartbeat') {
        state.online = true
        state.sensorError = (Number(event.state_flags) & 8) !== 0
        if (!state.sensorError) {
            state.error = ''
        }
        updateDashboard = true
    }

    if (updateDashboard) {
        emitSensorState(outputs, states, deviceId, now)
    }

    flow.set('sensorStates', states)
    return normaliseOutputs(outputs)
}

function updateGatewayDiagnostics(msg, flow, serialPort, now = Date.now()) {
    const diagnostics = flow.get('gatewayDiagnostics') || {
        validMessages: 0,
        parseErrors: 0,
        lastValidAt: null,
        lastParseErrorAt: null,
        gatewayOnlineAt: null,
        lastType: ''
    }

    if (msg.topic !== 'watchdog') {
        const event = msg.payload
        if (event?.type === 'serial_parse_error') {
            diagnostics.parseErrors += 1
            diagnostics.lastParseErrorAt = now
        } else if (event && typeof event.type === 'string') {
            diagnostics.validMessages += 1
            diagnostics.lastValidAt = now
            diagnostics.lastType = event.type
            if (event.type === 'gateway_online') {
                diagnostics.gatewayOnlineAt = now
            }
        }
    }

    let statusCode = 'waiting'
    let statusText = '等待网关串口数据'
    let statusColor = '#78909c'
    if (diagnostics.lastValidAt && now - diagnostics.lastValidAt < GATEWAY_SILENCE_MS) {
        statusCode = 'online'
        statusText = '网关数据接收正常'
        statusColor = '#43a047'
    } else if (diagnostics.lastValidAt) {
        statusCode = 'silent'
        statusText = '网关串口超过 15 秒无数据'
        statusColor = '#e53935'
    }

    flow.set('gatewayDiagnostics', diagnostics)
    return {
        payload: {
            serialPort: serialPort || '未选择',
            statusCode,
            statusText,
            statusColor,
            validMessages: diagnostics.validMessages,
            parseErrors: diagnostics.parseErrors,
            lastType: diagnostics.lastType || '暂无',
            lastValidText: displayTime(diagnostics.lastValidAt),
            gatewayOnlineText: displayTime(diagnostics.gatewayOnlineAt)
        }
    }
}

function formatHistory(msg) {
    const definition = SENSOR_DEFINITIONS[msg.deviceId]
    const outputs = Array.from({ length: HISTORY_OUTPUT_COUNT }, () => null)
    if (!definition) {
        return outputs
    }

    const rows = Array.isArray(msg.payload) ? msg.payload : []
    const readingPoints = []
    const temperaturePoints = []
    for (const row of rows) {
        const timestamp = finiteNumber(row.timestamp)
        const reading = finiteNumber(row.reading)
        const temperature = finiteNumber(row.temperature)
        if (timestamp !== null && reading !== null) {
            readingPoints.push({ x: timestamp, y: reading })
        }
        if (timestamp !== null && temperature !== null) {
            temperaturePoints.push({ x: timestamp, y: temperature })
        }
    }

    const timeFormat = msg.rangeKey === '15m' || msg.rangeKey === '1h'
        ? '{HH}:{mm}'
        : msg.rangeKey === '1d'
            ? '{MM}-{dd} {HH}:{mm}'
            : '{MM}-{dd}'
    const chartOptions = {
        xAxis: {
            min: msg.rangeSince,
            max: msg.rangeUntil,
            axisLabel: { formatter: timeFormat }
        }
    }
    outputs[definition.historyReadingOutput] = {
        payload: readingPoints,
        topic: `${definition.label} · ${msg.rangeLabel}`,
        action: 'replace',
        ui_update: { chartOptions }
    }
    outputs[definition.historyTemperatureOutput] = {
        payload: temperaturePoints,
        topic: `温度 · ${msg.rangeLabel}`,
        action: 'replace',
        ui_update: { chartOptions }
    }
    return outputs
}

module.exports = {
    OFFLINE_TIMEOUT_MS,
    SENSOR_DEFINITIONS,
    formatHistory,
    updateGatewayDiagnostics,
    updateSensors
}
