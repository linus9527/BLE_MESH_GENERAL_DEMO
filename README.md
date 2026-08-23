# BLE_MESH_GENERAL_DEMO

基于 nRF Connect SDK 2.9.3 和 Zephyr Bluetooth Mesh 的通用 RS485 水质传感器节点项目。

同一份 `apps/general_node` 固件可烧录到任意数量的 XIAO nRF52840 Sense。每个节点可连接 pH、溶解氧、ORP、水位/压力变送器中的任意组合，同一类型最多一个。四类设备共用一条 RS485 总线，由一个 Modbus 工作线程串行访问，不会出现多个线程同时抢占 UART 的问题。

## 应用

- `apps/general_node`：XIAO nRF52840 Sense + Seeed Studio XIAO-RS485 通用节点。
- `apps/gateway`：nRF52840 Dongle 网关，通过板载 USB CDC ACM 与上位机通信。

## 默认参数

所有传感器统一使用 `9600 8N1`，无校验。地址集中定义在 `common/include/app_rs485_config.h`：

| 传感器 | Modbus 地址 |
| --- | ---: |
| pH | `1` |
| 溶解氧 DO | `2` |
| ORP | `3` |
| 水位/压力变送器 | `4` |

请在接入同一条总线前，使用串口工具将实物地址改成上表数值。固件不会自动修改传感器参数。

## 运行规则

- 启动时依次探测四个固定地址；未安装的传感器不会被当作故障，也不会导致红灯。
- 在线传感器每 5 秒采集并通过 Mesh 上报。
- 连续 3 次读取失败后才报告 `sensor_error`；离线设备每 30 秒低频重探测，恢复后报告 `sensor_recovered`。
- 节点每 5 秒发送心跳；网关 30 秒未收到任何有效消息后判定节点离线。
- 节点 ID 来自 nRF52840 硬件 ID，同一节点上的多个传感器共享 `node_id`，但拥有独立 `sensor_id`。
- 继电器/水阀线程按当前需求暂不实现，后续可在同一工作架构中增加。

## 构建

在已激活 NCS 2.9.3 的终端中执行：

```powershell
west build -p always -b xiao_ble/nrf52840/sense apps/general_node -d apps/general_node/build
west build -p always -b nrf52840dongle/nrf52840 apps/gateway -d apps/gateway/build
```

主要输出：

- XIAO UF2：`apps/general_node/build/general_node/zephyr/zephyr.uf2`
- 通用节点 HEX：`apps/general_node/build/merged.hex`
- Dongle UF2：`apps/gateway/build/gateway/zephyr/zephyr.uf2`
- 网关 HEX：`apps/gateway/build/merged.hex`

详细接线、配网和串口测试见 `docs/setup-and-test.md`；线程与 Mesh 协议见 `docs/architecture-and-protocol.md`；上位机 JSON 字段规范见 `docs/unified-gateway-dashboard-json-api.md`。
