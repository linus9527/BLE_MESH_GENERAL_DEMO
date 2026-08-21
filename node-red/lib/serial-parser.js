'use strict'

function parseEvent(text) {
    const event = JSON.parse(text)
    if (!event || typeof event !== 'object' || Array.isArray(event) || typeof event.type !== 'string') {
        throw new Error('missing_type')
    }
    return event
}

function recoverTruncatedDoReport(line) {
    const match = line.match(/^(\{"type":"do_report",.*),"timestamp_ms":-?\d*$/)
    if (!match) {
        return null
    }

    return parseEvent(`${match[1]}}`)
}

function parse(payload) {
    const raw = Buffer.isBuffer(payload)
        ? payload.toString('utf8')
        : String(payload ?? '')
    const line = raw.trim()

    if (!line) {
        return { ok: false, error: 'empty_line', raw: line }
    }

    try {
        return { ok: true, event: parseEvent(line), raw: line, recovered: false }
    } catch (error) {
        try {
            const recoveredEvent = recoverTruncatedDoReport(line)
            if (recoveredEvent) {
                return { ok: true, event: recoveredEvent, raw: line, recovered: true }
            }
        } catch (recoveryError) {
            return { ok: false, error: recoveryError.message, raw: line }
        }

        return { ok: false, error: error.message, raw: line }
    }
}

module.exports = {
    parse
}
