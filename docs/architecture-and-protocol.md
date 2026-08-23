# 通用节点架构与 Mesh 协议

## 1. 拓扑

```text
pH(addr=1) ---+
DO(addr=2) ---+-- RS485 -- XIAO General_Node -- BLE Mesh -- nRF52840 Dongle -- USB CDC JSON
ORP(addr=3) --+
水位(addr=4) -+
```

每块 XIAO 可安装任意子集，同一类型最多一个。所有 XIAO 烧录同一个 `apps/general_node` 固件。网关源码和节点源码保存在同一仓库。

## 2. 线程模型

- Zephyr Bluetooth 和 Mesh 使用系统线程处理收发。
- `app_node` 使用系统工作队列处理 5 秒心跳、上线重试和网关超时。
- `general_modbus` 是唯一允许调用 Zephyr Modbus Client API 的工作线程。
- pH 校准命令通过消息队列交给 `general_modbus`，不会在 Mesh 回调中直接访问 UART。

单总线只使用一个 Modbus 线程是有意设计。为每个传感器创建独立 UART 线程会导致请求帧交叉、DE/RE 竞争和响应错配。

## 3. 传感器状态机

```text
UNKNOWN --首次读取失败--> ABSENT --30 秒探测成功--> ONLINE
UNKNOWN --首次读取成功--> ONLINE
ONLINE --连续 3 次失败--> LOST --30 秒探测成功--> ONLINE
```

- `ABSENT` 表示本节点没有安装该类型传感器，不产生错误状态。
- `LOST` 表示传感器曾正常在线后失联，节点置位传感器错误并闪红灯。
- 任一成功读数都会按传感器类型独立上报；一类设备故障不会阻止其他类型采集。

## 4. 节点与传感器 ID

通用节点上线消息携带 nRF52840 硬件 ID 的低 32 位。网关生成：

- `node_id = BLE_MESH_RS485_<8位硬件ID>`
- `sensor_id = PH_<硬件ID>`、`DO_<硬件ID>`、`ORP_<硬件ID>`、`WATER_LEVEL_<硬件ID>`

因此同一节点的多个传感器共享 `node_id` 和 `mesh_addr`，但 `sensor_id` 与 `rs485_address` 不同。

## 5. Mesh 地址

| 组地址 | 名称 | 发送者 | 接收者 |
| --- | --- | --- | --- |
| `0xC000` | `NODE_STATUS_GROUP` | 通用节点 | 网关 |
| `0xC002` | `GATEWAY_HEARTBEAT_GROUP` | 网关 | 通用节点 |

应用模型为 Vendor Model `0xFFFF:0x0001`。每个载荷第一字节为协议版本 `1`。

## 6. 应用消息

| Opcode | 名称 | 方向 | 载荷 |
| ---: | --- | --- | --- |
| `0x01` | `NODE_HEARTBEAT` | 节点→网关 | `version, device_type, state_flags, sequence` |
| `0x07` | `GATEWAY_HEARTBEAT` | 网关→节点 | `version, sequence` |
| `0x09` | `NODE_ONLINE` | 节点→网关 | `version, device_type, token, hardware_id_le32` |
| `0x0A` | `NODE_ONLINE_ACK` | 网关→节点 | `version, device_type, token` |
| `0x0B` | `PH_REPORT` | 节点→网关 | `version, temperature_x10_le16, ph_x100_le16, mv_x10_le16, sequence` |
| `0x0C` | `PH_CALIBRATE` | 网关→节点 | `version, point, sequence` |
| `0x0D` | `PH_CALIBRATION_RESULT` | 节点→网关 | `version, point, result, sequence` |
| `0x0E` | `DO_REPORT` | 节点→网关 | `version, do_x100_le16, temperature_x10_le16, saturation, calibration_flags, sequence` |
| `0x0F` | `ORP_REPORT` | 节点→网关 | `version, temperature_x10_le16, orp_x10_le16, drift_x10_le16, sequence` |
| `0x10` | `SENSOR_STATUS` | 节点→网关 | `version, sensor_type, rs485_address, status, error, sequence` |
| `0x11` | `WATER_LEVEL_REPORT` | 节点→网关 | `version, raw_value_le16, decimal_places, unit_code, sequence` |

传感器地址未重复放入每个测量载荷，因为同一类型地址由 `app_rs485_config.h` 固定，网关又能通过源单播地址确定节点。这样可保持所有传感器报告不超过 8 字节，避免 Mesh 分段传输。

## 7. 网关接口

网关按 `docs/unified-gateway-dashboard-json-api.md` 输出单层 JSON，核心字段包括：

- `gateway_id`
- `node_id`
- `sensor_id`
- `sensor_type`
- `mesh_addr`
- `rs485_address`
- `sequence`
- `quality`
- `uptime_ms`
- `time_quality`

网关当前最多同时跟踪 8 个通用节点。节点 30 秒无有效消息后输出一次 `device_offline`；后续消息恢复时输出 `device_online`。

## 8. 暂不实现

继电器/水阀控制线程、阀门反馈和自动联动规则不在本版本中实现。后续应通过独立消息队列接入，不能从 Mesh 回调直接操作水阀。
