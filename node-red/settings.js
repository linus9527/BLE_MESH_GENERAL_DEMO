'use strict'

const path = require('path')

process.env.SERIAL_PORT ||= 'auto'
process.env.MQTT_ENABLED ||= 'false'
process.env.MQTT_HOST ||= '127.0.0.1'
process.env.MQTT_PORT ||= '1883'
process.env.MQTT_CLIENT_ID ||= 'ble-mesh-gateway'
process.env.MQTT_TOPIC_PREFIX ||= 'ble_mesh'
process.env.HISTORY_DATA_DIR ||= path.join(__dirname, 'data')
process.env.HISTORY_DB ||= path.join(process.env.HISTORY_DATA_DIR, 'sensor-history.sqlite')

module.exports = {
    uiHost: process.env.NODE_RED_HOST || '0.0.0.0',
    uiPort: process.env.PORT || 1880,
    flowFile: 'flows.json',
    flowFilePretty: true,
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET,
    functionTimeout: 10,
    functionGlobalContext: {
        dashboardHandlers: require('./lib/dashboard-handlers'),
        serialParser: require('./lib/serial-parser'),
        historyStore: require('./lib/history-store').createHistoryStore(process.env.HISTORY_DB)
    },
    contextStorage: {
        default: {
            module: 'memory'
        }
    },
    editorTheme: {
        projects: {
            enabled: false
        }
    },
    logging: {
        console: {
            level: 'info',
            metrics: false,
            audit: false
        }
    }
}
