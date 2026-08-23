# 接线、配网与测试

## 1. RS485 接线

通用节点使用 `xiao_ble/nrf52840/sense` 和 Seeed Studio XIAO-RS485 扩展板。

| 功能 | XIAO 引脚 | nRF52840 GPIO |
| --- | --- | --- |
| UART TX | `D4` | `P0.04` |
| UART RX | `D5` | `P0.05` |
| RS485 DE/RE | `D2` | 由设备树别名 `xiao_d 2` 提供 |

传感器的 `A` 接扩展板 `A`，`B` 接扩展板 `B`。如果所有参数正确但始终超时，断电后交换 `A/B` 再测试。总线两端按实际线长决定是否启用 `120 Ω` 终端电阻。

各传感器使用其手册规定的独立电源，典型为 `12 V DC`。传感器电源地、XIAO GND 和 RS485 扩展板 GND 必须共地。不得把传感器的 `12 V` 接入 XIAO 的 `5 V` 引脚。

## 2. Modbus 参数

四类设备统一使用：

- 波特率：`9600`
- 数据位：`8`
- 校验：`None`
- 停止位：`1`

在接入同一条 RS485 总线前，用串口工具设置以下从站地址：

| 地址 | 类型 | 主要读取寄存器 |
| ---: | --- | --- |
| `1` | pH | `0x0000–0x0002`：温度、pH、mV |
| `2` | DO | `0x2001–0x2006`，附加读取 `0x200F` 校准状态 |
| `3` | ORP | `0x0000` 温度，`0x0009–0x000A` ORP/漂移 |
| `4` | 水位/压力变送器 | `0x0002–0x0004`：单位、小数位、测量值 |

如需更换地址，只修改 `common/include/app_rs485_config.h`，并确保实物地址与代码一致。水位/压力变送器的单位码按协议顺序解释为 `MPa`、`kPa`、`Pa`、`bar`、`mbar`、`kg/cm2`、`psi`、`mH2O`、`mmH2O`。

## 3. 构建与烧录

```powershell
west build -p always -b xiao_ble/nrf52840/sense apps/general_node -d apps/general_node/build
west build -p always -b nrf52840dongle/nrf52840 apps/gateway -d apps/gateway/build
```

XIAO 双击 Reset 进入 UF2 磁盘模式，将 `apps/general_node/build/general_node/zephyr/zephyr.uf2` 拖入磁盘。所有 XIAO 都烧录同一个文件。

nRF52840 Dongle 网关当前按外部 J-Link/Programmer 从 `0x0000` 烧录设计。使用 Programmer 对 `apps/gateway/build/merged.hex` 执行 **Erase & write**。全片擦除会删除 Mesh 配网信息。

## 4. nRF Mesh 配置

固件使用 Vendor Model：Company ID `0xFFFF`、Model ID `0x0001`。`0xFFFF` 只适合当前原型，正式产品必须换成分配的 Company ID。

1. 配置一块 `BLE_MESH_GATEWAY` 和所需数量的 `BLE_MESH_GENERAL` 节点。
2. 为所有设备的 Vendor Model 绑定同一个 AppKey。
3. 创建 `Gateway_Rx = 0xC000` 和 `Common_CTRL = 0xC002` 两个组地址。
4. 网关 Vendor Model 订阅 `0xC000`。
5. 每个通用节点 Vendor Model 订阅 `0xC002`。
6. Publication 保持 `None`；固件直接调用 `bt_mesh_model_send()` 发送固定组地址。
7. Relay 保持开启，TTL 可保持 `7`。

通用节点完成配网后会每 2 秒重发上线通知，直到收到网关单播确认。手机中多个节点都显示 `BLE_MESH_GENERAL` 是正常现象，可按物理编号重命名为 `GENERAL_01`、`GENERAL_02` 等。

## 5. LED 状态

| 状态 | LED |
| --- | --- |
| 未配网 | 蓝色慢闪 |
| 已配网但等待网关确认/心跳 | 蓝色慢闪 |
| 网关可达，已检测设备运行正常 | 绿色常亮 |
| 曾经在线的传感器连续 3 次读取失败 | 红色闪烁 |
| 未连接某类传感器 | 不报警，节点保持正常状态 |
| 网关存在已知离线节点 | 网关黄色 |

## 6. USB CDC 串口

Dongle 运行固件后通过板载 USB 枚举 CDC ACM。串口参数为 `115200 8N1`，上位机必须置位 DTR；DTR 未置位时网关不会输出 JSON。

每条消息占一行。例如：

```json
{"schema_version":1,"type":"ph_report","category":"telemetry","gateway_id":"GATEWAY_01","node_id":"BLE_MESH_RS485_1234ABCD","sensor_id":"PH_1234ABCD","sensor_type":"ph","mesh_addr":"0x0010","rs485_address":1,"sequence":42,"ph":7.24,"temperature_c":24.8,"ph_mv":6.2,"quality":"good","uptime_ms":25000,"time_quality":"unsynchronized"}
{"schema_version":1,"type":"water_level_report","category":"telemetry","gateway_id":"GATEWAY_01","node_id":"BLE_MESH_RS485_1234ABCD","sensor_id":"WATER_LEVEL_1234ABCD","sensor_type":"water_level","mesh_addr":"0x0010","rs485_address":4,"sequence":45,"value":0.86,"unit":"mH2O","unit_code":7,"unit_code_namespace":"generic_modbus_transmitter_v1","raw_value":86,"decimal_places":2,"unit_valid":true,"quality":"good","uptime_ms":29000,"time_quality":"unsynchronized"}
```

pH 校准命令使用实际上报中的 `sensor_id`：

```json
{"schema_version":1,"type":"ph_calibrate","category":"command","request_id":"01K3PHCAL0001","gateway_id":"GATEWAY_01","sensor_id":"PH_1234ABCD","calibration_point":"7.00","source":"manual"}
```

## 7. 排障顺序

1. 确认四个传感器没有地址冲突，并全部为 `9600 8N1`。
2. 确认传感器供电正常、三方共地、`A/B` 极性正确。
3. 确认 XIAO 节点订阅 `0xC002`，网关订阅 `0xC000`，双方 Vendor Model 已绑定同一 AppKey。
4. 确认打开的是 Dongle 运行时 COM 口，且串口工具已置位 DTR。
5. 新接入但未识别的传感器最长等待 30 秒自动重探测，或重启节点立即重新探测。
