'use strict'

const fs = require('fs')
const path = require('path')

const flowPath = path.join(__dirname, '..', 'flows.json')
const nodes = JSON.parse(fs.readFileSync(flowPath, 'utf8'))
const ids = new Set()
const errors = []

for (const node of nodes) {
    if (!node.id) {
        errors.push(`节点缺少 id: ${JSON.stringify(node)}`)
        continue
    }
    if (ids.has(node.id)) {
        errors.push(`节点 id 重复: ${node.id}`)
    }
    ids.add(node.id)
}

for (const node of nodes) {
    if (node.z && !ids.has(node.z)) {
        errors.push(`${node.id} 引用了不存在的流程 ${node.z}`)
    }
    for (const output of node.wires || []) {
        for (const target of output) {
            if (!ids.has(target)) {
                errors.push(`${node.id} 连接到不存在的节点 ${target}`)
            }
        }
    }
    if (node.type === 'function') {
        try {
            new Function('msg', 'context', 'flow', 'global', 'env', 'node', 'RED', 'Buffer', node.func)
        } catch (error) {
            errors.push(`${node.id} 函数语法错误: ${error.message}`)
        }
    }
}

const requiredTypes = [
    'serial in',
    'function',
    'mqtt out',
    'ui-chart',
    'ui-dropdown',
    'ui-template',
    'ui-text'
]

for (const type of requiredTypes) {
    if (!nodes.some((node) => node.type === type)) {
        errors.push(`流程缺少 ${type} 节点`)
    }
}

if (errors.length > 0) {
    console.error(errors.join('\n'))
    process.exit(1)
}

console.log(`flows.json 校验通过，共 ${nodes.length} 个节点。`)
