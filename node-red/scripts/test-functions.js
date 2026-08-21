'use strict'

const assert = require('assert')
const fs = require('fs')
const path = require('path')
const handlers = require('../lib/dashboard-handlers')
const { HistoryStore, RAW_RETENTION_MS } = require('../lib/history-store')
const serialParser = require('../lib/serial-parser')

const nodes = JSON.parse(
    fs.readFileSync(path.join(__dirname, '..', 'flows.json'), 'utf8')
)

function getFunction(id) {
    const nodeDefinition = nodes.find((node) => node.id === id)
    assert(nodeDefinition, `未找到函数节点 ${id}`)
    return new Function(
        'msg',
        'context',
        'flow',
        'global',
        'env',
        'node',
        'RED',
        'Buffer',
        nodeDefinition.func
    )
}

function createFlowContext() {
    const values = new Map()
    return {
        get: (key) => values.get(key),
        set: (key, value) => values.set(key, value)
    }
}

function execute(fn, msg, flow, environment = {}) {
    return fn(
        msg,
        {},
        flow,
        {},
        { get: (key) => environment[key] },
        {},
        {},
        Buffer
    )
}

function lastMessage(output) {
    assert(Array.isArray(output) && output.length > 0)
    return output[output.length - 1]
}

const parseJson = getFunction('function_parse_json')
const parsed = execute(
    parseJson,
    { payload: '{"type":"ph_report","device_id":"BLE_MESH_PH","ph":7.12}' },
    createFlowContext()
)
assert.strictEqual(parsed[0].payload.ph, 7.12)
assert.strictEqual(parsed[1], null)

const invalid = execute(parseJson, { payload: 'not-json' }, createFlowContext())
assert.strictEqual(invalid[0], null)
assert.strictEqual(invalid[1].payload.type, 'serial_parse_error')

const recoveredDo = serialParser.parse(
    '{\"type\":\"do_report\",\"device_id\":\"BLE_MESH_DO\",\"device_name\":\"DO_Node\",\"mesh_addr\":\"0x000b\",\"dissolved_oxygen_mg_l\":7.02,\"temperature_c\":25.9,\"saturation_pct\":87,\"calibration_status_known\":false,\"air_calibrated\":null,\"zero_calibrated\":null,\"timestamp_ms\":5'
)
assert.strictEqual(recoveredDo.ok, true)
assert.strictEqual(recoveredDo.recovered, true)
assert.strictEqual(recoveredDo.event.dissolved_oxygen_mg_l, 7.02)
assert.strictEqual(recoveredDo.event.temperature_c, 25.9)

const stateContext = createFlowContext()
let currentTime = 100000

const phOutput = handlers.updateSensors({
    payload: {
        type: 'ph_report',
        device_id: 'BLE_MESH_PH',
        device_name: 'PH_Node',
        mesh_addr: '0x0008',
        ph: 7.12,
        temperature_c: 25.4,
        timestamp_ms: 1234
    }
}, stateContext, currentTime)
assert.strictEqual(lastMessage(phOutput[0]).payload.reading, 7.12)
assert.strictEqual(lastMessage(phOutput[0]).payload.temperature, 25.4)
assert.strictEqual(lastMessage(phOutput[0]).payload.statusCode, 'online')
assert.deepStrictEqual(lastMessage(phOutput[1]).payload, { x: currentTime, y: 7.12 })
assert.deepStrictEqual(lastMessage(phOutput[2]).payload, { x: currentTime, y: 25.4 })

currentTime += 2000
const doWithoutTemperature = handlers.updateSensors({
    payload: {
        type: 'do_report',
        device_id: 'BLE_MESH_DO',
        device_name: 'DO_Node',
        dissolved_oxygen_mg_l: 8.35
    }
}, stateContext, currentTime)
assert.strictEqual(lastMessage(doWithoutTemperature[3]).payload.reading, 8.35)
assert.strictEqual(lastMessage(doWithoutTemperature[3]).payload.temperature, null)
assert.strictEqual(lastMessage(doWithoutTemperature[3]).payload.statusCode, 'warning')
assert.strictEqual(lastMessage(doWithoutTemperature[4]).payload.y, 8.35)
assert.strictEqual(lastMessage(doWithoutTemperature[4]).ui_update.chartOptions.xAxis.max, currentTime)
assert.strictEqual(doWithoutTemperature[5], null)
assert.strictEqual(lastMessage(doWithoutTemperature[9]).payload.temperature, null)

currentTime += 2000
const doTemperatureOnly = handlers.updateSensors({
    payload: {
        type: 'do_report',
        device_id: 'BLE_MESH_DO',
        temperature_c: 25.1
    }
}, stateContext, currentTime)
assert.strictEqual(lastMessage(doTemperatureOnly[3]).payload.reading, 8.35)
assert.strictEqual(lastMessage(doTemperatureOnly[3]).payload.temperature, 25.1)
assert.strictEqual(doTemperatureOnly[4], null)
assert.strictEqual(lastMessage(doTemperatureOnly[5]).payload.y, 25.1)

const offlineDo = handlers.updateSensors(
    { topic: 'watchdog' },
    stateContext,
    currentTime + handlers.OFFLINE_TIMEOUT_MS
)
assert.strictEqual(lastMessage(offlineDo[3]).payload.statusCode, 'offline')
assert.strictEqual(lastMessage(offlineDo[3]).payload.reading, null)
assert.strictEqual(lastMessage(offlineDo[3]).payload.temperature, null)

currentTime += 2000
const orpOutput = handlers.updateSensors({
    payload: {
        type: 'orp_report',
        device_id: 'BLE_MESH_ORP',
        device_name: 'ORP_Node',
        orp_mv: 236.5,
        temperature_c: 24.9
    }
}, stateContext, currentTime)
assert.strictEqual(lastMessage(orpOutput[6]).payload.reading, 236.5)
assert.strictEqual(lastMessage(orpOutput[7]).payload.y, 236.5)
assert.strictEqual(lastMessage(orpOutput[8]).payload.y, 24.9)

const errorOutput = handlers.updateSensors({
    payload: {
        type: 'sensor_error',
        device_id: 'BLE_MESH_PH',
        error: 'modbus_read_failed'
    }
}, stateContext, currentTime)
assert.strictEqual(lastMessage(errorOutput[0]).payload.statusCode, 'error')

currentTime += handlers.OFFLINE_TIMEOUT_MS
const offlineOutput = handlers.updateSensors(
    { topic: 'watchdog', payload: currentTime },
    stateContext,
    currentTime
)
assert.strictEqual(lastMessage(offlineOutput[0]).payload.statusCode, 'offline')

const gatewayContext = createFlowContext()
const gatewayOnline = handlers.updateGatewayDiagnostics({
    payload: { type: 'gateway_online', device_id: 'BLE_MESH_GATEWAY' }
}, gatewayContext, 'COM24', 1000)
assert.strictEqual(gatewayOnline.payload.statusCode, 'online')
assert.strictEqual(gatewayOnline.payload.serialPort, 'COM24')
assert.strictEqual(gatewayOnline.payload.validMessages, 1)

const parseError = handlers.updateGatewayDiagnostics({
    payload: { type: 'serial_parse_error' }
}, gatewayContext, 'COM24', 2000)
assert.strictEqual(parseError.payload.parseErrors, 1)

const silentGateway = handlers.updateGatewayDiagnostics(
    { topic: 'watchdog' }, gatewayContext, 'COM24', 17000
)
assert.strictEqual(silentGateway.payload.statusCode, 'silent')

const historyStore = new HistoryStore(':memory:')
assert.strictEqual(historyStore.write(lastMessage(phOutput[9]).payload), true)
assert.strictEqual(historyStore.write(lastMessage(doWithoutTemperature[9]).payload), true)
assert.strictEqual(historyStore.write(lastMessage(doTemperatureOnly[9]).payload), true)

const rawQuery = historyStore.query('BLE_MESH_DO', '15m', currentTime)
assert.strictEqual(rawQuery.rangeLabel, '近 15 分钟')
assert.strictEqual(rawQuery.rows.length, 2)
assert.strictEqual(rawQuery.rows[0].reading, 8.35)
assert.strictEqual(rawQuery.rows[0].temperature, null)

const minuteQuery = historyStore.query('BLE_MESH_DO', '7d', currentTime)
assert.strictEqual(minuteQuery.rangeLabel, '近 1 周')
assert.strictEqual(minuteQuery.rows.length, 1)
assert.strictEqual(minuteQuery.rows[0].reading, 8.35)
assert.strictEqual(minuteQuery.rows[0].temperature, 25.1)

const historyOutput = handlers.formatHistory({
    deviceId: 'BLE_MESH_DO',
    rangeKey: '1h',
    rangeLabel: '近 1 周',
    rangeSince: 1000,
    rangeUntil: 9000,
    payload: [
        { timestamp: 1000, reading: 8.1, temperature: 24.8 },
        { timestamp: 2000, reading: 8.2, temperature: 24.9 }
    ]
})
assert.strictEqual(historyOutput[0], null)
assert.deepStrictEqual(historyOutput[2].payload[1], { x: 2000, y: 8.2 })
assert.deepStrictEqual(historyOutput[3].payload[0], { x: 1000, y: 24.8 })
assert.strictEqual(historyOutput[2].action, 'replace')
assert.deepStrictEqual(historyOutput[2].ui_update.chartOptions.xAxis, {
    min: 1000,
    max: 9000,
    axisLabel: { formatter: '{HH}:{mm}' }
})

const cleanup = historyStore.cleanup(currentTime + RAW_RETENTION_MS + 1)
assert(cleanup.rawDeleted >= 1)
historyStore.close()

const prepareMqtt = getFunction('function_prepare_mqtt')
const mqttDisabled = execute(
    prepareMqtt,
    { payload: { type: 'ph_report', device_id: 'BLE_MESH_PH' } },
    createFlowContext(),
    { MQTT_ENABLED: 'false' }
)
assert.strictEqual(mqttDisabled, null)

const mqttEnabled = execute(
    prepareMqtt,
    {
        payload: {
            type: 'do_report',
            device_id: 'BLE_MESH_DO',
            dissolved_oxygen_mg_l: 8.3,
            temperature_c: 25.4
        },
        receivedAt: 100000
    },
    createFlowContext(),
    { MQTT_ENABLED: 'true', MQTT_TOPIC_PREFIX: 'ble_mesh' }
)
assert.strictEqual(mqttEnabled.topic, 'ble_mesh/BLE_MESH_DO/telemetry')
assert.strictEqual(mqttEnabled.qos, 1)
assert.strictEqual(mqttEnabled.retain, false)
assert.strictEqual(JSON.parse(mqttEnabled.payload).schema_version, 1)

console.log('实时值、温度、DO 缺字段、历史范围、网关诊断和 MQTT 测试通过。')
