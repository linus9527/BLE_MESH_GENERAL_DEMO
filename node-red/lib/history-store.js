'use strict'

const fs = require('fs')
const path = require('path')
const { DatabaseSync } = require('node:sqlite')

const RAW_RETENTION_MS = 24 * 60 * 60 * 1000
const AGGREGATE_RETENTION_MS = 90 * 24 * 60 * 60 * 1000
const RANGES = {
    '15m': { label: '近 15 分钟', milliseconds: 15 * 60 * 1000, source: 'raw' },
    '1h': { label: '近 1 小时', milliseconds: 60 * 60 * 1000, source: 'raw' },
    '1d': { label: '近 1 天', milliseconds: 24 * 60 * 60 * 1000, source: 'minute', bucketMs: 60000 },
    '7d': { label: '近 1 周', milliseconds: 7 * 24 * 60 * 60 * 1000, source: 'minute', bucketMs: 5 * 60000 },
    '30d': { label: '近 1 个月', milliseconds: 30 * 24 * 60 * 60 * 1000, source: 'minute', bucketMs: 30 * 60000 }
}

function finiteNumber(value) {
    if (value === null || value === undefined || value === '') {
        return null
    }
    const number = Number(value)
    return Number.isFinite(number) ? number : null
}

class HistoryStore {
    constructor(databasePath) {
        if (databasePath !== ':memory:') {
            fs.mkdirSync(path.dirname(databasePath), { recursive: true })
        }
        this.database = new DatabaseSync(databasePath, { timeout: 5000 })
        this.database.exec(`
            PRAGMA journal_mode = WAL;
            PRAGMA synchronous = NORMAL;
            CREATE TABLE IF NOT EXISTS sensor_raw (
                device_id TEXT NOT NULL,
                received_at INTEGER NOT NULL,
                reading REAL,
                temperature REAL,
                PRIMARY KEY (device_id, received_at)
            );
            CREATE TABLE IF NOT EXISTS sensor_minute (
                device_id TEXT NOT NULL,
                bucket_ms INTEGER NOT NULL,
                reading_sum REAL NOT NULL DEFAULT 0,
                reading_count INTEGER NOT NULL DEFAULT 0,
                temperature_sum REAL NOT NULL DEFAULT 0,
                temperature_count INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (device_id, bucket_ms)
            );
        `)
        this.insertRaw = this.database.prepare(`
            INSERT OR REPLACE INTO sensor_raw
                (device_id, received_at, reading, temperature)
            VALUES (?, ?, ?, ?)
        `)
        this.upsertMinute = this.database.prepare(`
            INSERT INTO sensor_minute
                (device_id, bucket_ms, reading_sum, reading_count, temperature_sum, temperature_count)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(device_id, bucket_ms) DO UPDATE SET
                reading_sum = reading_sum + excluded.reading_sum,
                reading_count = reading_count + excluded.reading_count,
                temperature_sum = temperature_sum + excluded.temperature_sum,
                temperature_count = temperature_count + excluded.temperature_count
        `)
        this.queryRaw = this.database.prepare(`
            SELECT received_at AS timestamp, reading, temperature
            FROM sensor_raw
            WHERE device_id = ? AND received_at >= ?
            ORDER BY received_at
        `)
        this.queryMinute = this.database.prepare(`
            SELECT
                CAST(bucket_ms / ? AS INTEGER) * ? AS timestamp,
                CASE WHEN SUM(reading_count) > 0 THEN SUM(reading_sum) / SUM(reading_count) END AS reading,
                CASE WHEN SUM(temperature_count) > 0 THEN SUM(temperature_sum) / SUM(temperature_count) END AS temperature
            FROM sensor_minute
            WHERE device_id = ? AND bucket_ms >= ?
            GROUP BY CAST(bucket_ms / ? AS INTEGER)
            ORDER BY timestamp
        `)
        this.deleteRaw = this.database.prepare('DELETE FROM sensor_raw WHERE received_at < ?')
        this.deleteMinute = this.database.prepare('DELETE FROM sensor_minute WHERE bucket_ms < ?')
    }

    write(record) {
        if (!record || typeof record.deviceId !== 'string') {
            return false
        }
        const receivedAt = finiteNumber(record.receivedAt)
        const reading = finiteNumber(record.reading)
        const temperature = finiteNumber(record.temperature)
        if (receivedAt === null || (reading === null && temperature === null)) {
            return false
        }

        this.insertRaw.run(record.deviceId, receivedAt, reading, temperature)
        this.upsertMinute.run(
            record.deviceId,
            Math.floor(receivedAt / 60000) * 60000,
            reading ?? 0,
            reading === null ? 0 : 1,
            temperature ?? 0,
            temperature === null ? 0 : 1
        )
        return true
    }

    query(deviceId, rangeKey = '1h', now = Date.now()) {
        const range = RANGES[rangeKey] || RANGES['1h']
        const since = now - range.milliseconds
        const rows = range.source === 'raw'
            ? this.queryRaw.all(deviceId, since)
            : this.queryMinute.all(range.bucketMs, range.bucketMs, deviceId, since, range.bucketMs)
        return {
            deviceId,
            rangeKey,
            rangeLabel: range.label,
            since,
            until: now,
            rows
        }
    }

    cleanup(now = Date.now()) {
        const raw = this.deleteRaw.run(now - RAW_RETENTION_MS)
        const minute = this.deleteMinute.run(now - AGGREGATE_RETENTION_MS)
        return {
            rawDeleted: Number(raw.changes),
            minuteDeleted: Number(minute.changes)
        }
    }

    close() {
        this.database.close()
    }
}

function createHistoryStore(databasePath) {
    return new HistoryStore(databasePath)
}

module.exports = {
    AGGREGATE_RETENTION_MS,
    HistoryStore,
    RAW_RETENTION_MS,
    RANGES,
    createHistoryStore
}
