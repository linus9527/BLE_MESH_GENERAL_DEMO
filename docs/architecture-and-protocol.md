# BLE Mesh 架构与通信协议

## 1. 范围

本项目使用 nRF Connect SDK 2.9.3 与 Zephyr Bluetooth Mesh，在七块 nRF52840 开发板上实现本地物联演示。电脑上位机通过 USB CDC 虚拟串口暂时模拟云端；网关是 Mesh 和云端接口之间的唯一边界。

首次配网、AppKey 绑定、发布地址与订阅地址配置由 Nordic nRF Mesh App 完成。网关不承担 Provisioner 角色。

## 2. 节点角色

| 逻辑 ID | 设备名称 | 板级目标 | 角色 |
| --- | --- | --- | --- |
| `BLE_MESH_DHT11` | `DHT11_Node` | `xiao_ble/nrf52840/sense` | 每 5 秒采集并上报温湿度；阈值状态变化时告警。 |
| `BLE_MESH_BUTTON` | `Button_Node` | `xiao_ble/nrf52840/sense` | 即时上报按下、松开事件；每 5 秒发送在线心跳。 |
| `BLE_MESH_SERVO` | `Servo_Node` | `xiao_ble/nrf52840/sense` | 控制连续旋转 SG90 的方向、速度、停止；每 5 秒发送在线心跳。 |
| `BLE_MESH_PH` | `PH_Node` | `xiao_ble/nrf52840/sense` | 使用 Zephyr 官方 Modbus RTU Client API，每 5 秒读取并上报温度、pH 和 pH 毫伏值；支持远程校准。 |
| `BLE_MESH_DO` | `DO_Node` | `xiao_ble/nrf52840/sense` | 使用 Zephyr 官方 Modbus RTU Client API，每 5 秒读取并上报溶解氧、温度、饱和度和校准状态。 |
| `BLE_MESH_ORP` | `ORP_Node` | `xiao_ble/nrf52840/sense` | 使用 Zephyr 官方 Modbus RTU Client API，每 5 秒读取并上报温度、ORP 和 ORP 漂移值。 |
| `BLE_MESH_GATEWAY` | `Gateway_Node` | `nrf52840dongle/nrf52840` | 管理节点在线状态，通过板载 USB CDC ACM 转换 Mesh 与上位机 JSON，并每 5 秒发送网关心跳。 |

所有设备均开启 Relay。设备数量与距离适用于近距离演示网络，不以低功耗或高吞吐为目标。

## 3. 网络拓扑

```text
nRF Mesh App -- 首次配网和模型配置 --> 全部七个设备

DHT11_Node --- 状态、温湿度、告警 ---> Gateway_Node --- USB CDC JSON ---> 上位机
Button_Node -- 状态、按键事件、告警 --> Gateway_Node --- USB CDC JSON ---> 上位机
Servo_Node --- 状态、执行结果、告警 ---> Gateway_Node --- USB CDC JSON ---> 上位机
PH_Node ------ 状态、pH/温度/mV ------> Gateway_Node --- USB CDC JSON ---> 上位机
DO_Node ------ 状态、DO/温度/饱和度 ---> Gateway_Node --- USB CDC JSON ---> 上位机
ORP_Node ----- 状态、ORP/温度/漂移 ----> Gateway_Node --- USB CDC JSON ---> 上位机
上位机 -------- 舵机 JSON 控制命令 ----> Gateway_Node --- Mesh ---> Servo_Node
上位机 -------- pH JSON 校准命令 -----> Gateway_Node --- Mesh ---> PH_Node
```

每个应用消息使用 Vendor Model：Company ID `0xFFFF`、Model ID `0x0001`。旧版曾把 `0xFFFF` 当成自定义 SIG Model，并使用 `0x8001–0x800A` 操作码；这些值与 Configuration Foundation 消息冲突，会导致上线通知、确认和校准等消息被配置服务器截获。本版改用三字节 Vendor opcode。`0xFFFF` 仅用于原型，产品化时必须替换为正式分配的 Bluetooth SIG Company ID。

## 4. 组地址

| 地址 | 名称 | 发布者 | 订阅者 | 用途 |
| --- | --- | --- | --- | --- |
| `0xC000` | `NODE_STATUS_GROUP` | 六个功能节点 | 网关 | 节点状态、传感器、按键、舵机执行结果和 pH 校准结果。 |
| `0xC001` | `SERVO_CONTROL_GROUP` | 网关 | 舵机节点 | 舵机运行、停止、校准命令。 |
| `0xC002` | `GATEWAY_HEARTBEAT_GROUP` | 网关 | 六个功能节点 | 网关在线心跳和舵机安全停止判定。 |

源单播地址由 Mesh 网络头提供。网关将它格式化为 JSON 中的 `mesh_addr`。

## 5. 应用模型消息

每个应用操作码通过 `BT_MESH_MODEL_OP_3(code, 0xFFFF)` 编码为三字节 Vendor opcode，其中 `code` 见下表。所有载荷第一字节均为协议版本，当前为 `1`；版本不匹配的消息会被丢弃。

| 操作码 | 名称 | 方向 | 载荷 |
| --- | --- | --- | --- |
| `0x01` | `NODE_HEARTBEAT` | 节点 -> 网关 | `version`, `device_type`, `state_flags`, `sequence` |
| `0x02` | `DHT_REPORT` | DHT11 节点 -> 网关 | `version`, `temperature_c`, `humidity_pct`, `sequence` |
| `0x03` | `DHT_ALERT` | DHT11 节点 -> 网关 | `version`, `metric`, `state`, `value`, `sequence` |
| `0x04` | `BUTTON_EVENT` | 按键节点 -> 网关 | `version`, `state`, `sequence` |
| `0x05` | `SERVO_COMMAND` | 网关 -> 舵机节点 | `version`, `command`, `direction`, `speed_pct`, `sequence` |
| `0x06` | `SERVO_RESULT` | 舵机节点 -> 网关 | `version`, `result`, `direction`, `speed_pct`, `sequence` |
| `0x07` | `GATEWAY_HEARTBEAT` | 网关 -> 节点 | `version`, `sequence` |
| `0x08` | `SERVO_CALIBRATE_STOP` | 网关 -> 舵机节点 | `version`, `stop_pulse_us`, `sequence` |
| `0x09` | `NODE_ONLINE` | 节点 -> 网关 | `version`, `device_type`, `token` |
| `0x0A` | `NODE_ONLINE_ACK` | 网关 -> 节点单播 | `version`, `device_type`, `token` |
| `0x0B` | `PH_REPORT` | pH 节点 -> 网关 | `version`, `temperature_x10`, `ph_x100`, `ph_mv_x10`, `sequence` |
| `0x0C` | `PH_CALIBRATE` | 网关 -> pH 节点单播 | `version`, `point`, `sequence` |
| `0x0D` | `PH_CALIBRATION_RESULT` | pH 节点 -> 网关 | `version`, `point`, `result`, `sequence` |
| `0x0E` | `DO_REPORT` | DO 节点 -> 网关 | `version`, `dissolved_oxygen_x100`, `temperature_x10`, `saturation_pct`, `calibration_flags`, `sequence` |
| `0x0F` | `ORP_REPORT` | ORP 节点 -> 网关 | `version`, `temperature_x10`, `orp_x10`, `drift_x10`, `sequence` |

ORP 数值解析规则：

- Modbus 寄存器返回高字节在前，先组合为 16 位原始值，再按有符号 `int16_t` 解释。
- 温度、ORP 和 ORP 漂移的权重均为 `0.1`，即 `实际值 = 有符号原始值 / 10.0`。
- 例如数据字节 `07 26` 组合为 `0x0726 = 1830`，ORP 为 `183.0 mV`；数据字节 `FF 9C` 解释为 `-100`，ORP 为 `-10.0 mV`。
- Mesh 中的 `temperature_x10`、`orp_x10` 和 `drift_x10` 使用有符号 16 位小端格式传输，网关换算后输出带一位小数的 JSON。

枚举值：

- `device_type`：`1` DHT11，`2` 按键，`3` 舵机，`4` pH，`5` 溶解氧，`6` ORP。
- `state_flags`：位 `0` 已配网，位 `1` 网关可达，位 `2` 设备告警，位 `3` 传感器读取故障。
- `metric`：`1` 温度，`2` 湿度。
- `state`：`1` 进入异常，`2` 恢复正常。
- `command`：`1` 运行，`2` 停止。
- `direction`：`0` 停止，`1` 正转，`2` 反转。
- `result`：`1` 已执行，`2` 已停止，`3` 因网关超时安全停止，`4` 参数无效。

### 5.1 上报规则

- 节点完成配网或从 Flash 恢复 Mesh 配置后，立即向 `NODE_STATUS_GROUP` 发送 `NODE_ONLINE`；未收到网关确认时每 2 秒重试。
- 网关收到 `NODE_ONLINE` 后登记节点地址、输出 `device_online` JSON，并向该节点的单播地址回复 `NODE_ONLINE_ACK`。节点收到匹配的 `token` 后停止重试。
- DHT11 节点每 5 秒发送一次 `DHT_REPORT`。该消息同时视为该节点在线心跳。
- 按键节点只在输入连续稳定 `50 ms` 后确认状态变化并发送 `BUTTON_EVENT`；一次完整点击正常产生一条 `pressed` 和一条 `released`。节点每 5 秒发送 `NODE_HEARTBEAT`。
- 舵机节点每 5 秒发送 `NODE_HEARTBEAT`。收到命令后发送 `SERVO_RESULT`。
- pH 节点每 5 秒读取 Modbus 保持寄存器 `0–2` 并发送 `PH_REPORT`。读取失败时改发带传感器故障位的 `NODE_HEARTBEAT`；恢复后继续上报数据。pH 节点不做阈值告警。
- DO 节点每 5 秒读取 Modbus 保持寄存器 `0x2001–0x2006`，并尝试读取校准状态寄存器 `0x200F` 后发送 `DO_REPORT`。测量寄存器读取失败或数据超出手册量程时改发带传感器故障位的 `NODE_HEARTBEAT`；仅校准状态读取失败时仍上报测量数据，并将校准状态标记为未知。DO 节点不做阈值告警。
- ORP 节点每 5 秒读取温度寄存器 `0` 以及 ORP/漂移寄存器 `9–10` 并发送 `ORP_REPORT`。读取失败或数据超出手册量程时改发带传感器故障位的 `NODE_HEARTBEAT`；恢复后继续上报。ORP 节点不做阈值告警。
- 网关每 5 秒向 `GATEWAY_HEARTBEAT_GROUP` 发送 `GATEWAY_HEARTBEAT`。
- 网关连续 15 秒未收到某功能节点的有效上报时，标记该节点离线。
- 舵机节点连续 15 秒未收到有效网关心跳时，立即输出校准后的停止脉宽并上报安全停止结果；Mesh 恢复后等待新的控制命令。
- 温度在 `30–35°C`（含边界）以及湿度在 `60–80%`（含边界）时为正常。DHT11 只在进入异常和恢复正常时发送 `DHT_ALERT`。

## 6. USB CDC JSON 协议

nRF52840 Dongle 通过板载 USB 枚举为 CDC ACM 虚拟串口，上位机按 `115200 8N1` 打开对应 COM 口。每条输入和输出均是单行 UTF-8 JSON；网关不会在该端口输出 Zephyr 调试日志。上位机打开端口并置位 DTR 后，网关输出 `gateway_online`，因此不需要额外的 USB-TTL 转换器。`timestamp_ms` 由网关在输出时添加；当前实现使用网关自启动以来的单调毫秒数，接入真实云端或 RTC 后可替换为 Unix 时间戳。

### 6.1 网关输出

节点重启后首次有效上报示例：

```json
{"type":"device_online","device_id":"BLE_MESH_DHT11","device_name":"DHT11_Node","mesh_addr":"0x0005","reason":"boot","timestamp_ms":1730000000000}
```

温湿度定时上报：

```json
{"type":"dht_report","device_id":"BLE_MESH_DHT11","device_name":"DHT11_Node","mesh_addr":"0x0005","temperature_c":31,"humidity_pct":70,"timestamp_ms":1730000005000}
```

阈值告警与恢复：

```json
{"type":"dht_alert","device_id":"BLE_MESH_DHT11","device_name":"DHT11_Node","mesh_addr":"0x0005","metric":"temperature","state":"entered","value":29,"timestamp_ms":1730000010000}
{"type":"dht_alert","device_id":"BLE_MESH_DHT11","device_name":"DHT11_Node","mesh_addr":"0x0005","metric":"temperature","state":"cleared","value":30,"timestamp_ms":1730000015000}
```

按键事件：

```json
{"type":"button_event","device_id":"BLE_MESH_BUTTON","device_name":"Button_Node","mesh_addr":"0x0006","state":"pressed","timestamp_ms":1730000020000}
{"type":"button_event","device_id":"BLE_MESH_BUTTON","device_name":"Button_Node","mesh_addr":"0x0006","state":"released","timestamp_ms":1730000020120}
```

pH 定时上报：

```json
{"type":"ph_report","device_id":"BLE_MESH_PH","device_name":"PH_Node","mesh_addr":"0x0008","temperature_c":25.0,"ph":7.00,"ph_mv":0.6,"timestamp_ms":1730000025000}
```

溶解氧定时上报：

```json
{"type":"do_report","device_id":"BLE_MESH_DO","device_name":"DO_Node","mesh_addr":"0x0009","dissolved_oxygen_mg_l":8.35,"temperature_c":25.0,"saturation_pct":96,"air_calibrated":true,"zero_calibrated":false,"timestamp_ms":1730000025000}
```

ORP 定时上报：

```json
{"type":"orp_report","device_id":"BLE_MESH_ORP","device_name":"ORP_Node","mesh_addr":"0x000a","temperature_c":25.0,"orp_mv":235.6,"orp_drift_mv":1.2,"timestamp_ms":1730000025000}
```

设备离线、读取故障与舵机执行结果：

```json
{"type":"device_offline","device_id":"BLE_MESH_SERVO","device_name":"Servo_Node","mesh_addr":"0x0007","timeout_ms":15000,"timestamp_ms":1730000035000}
{"type":"sensor_error","device_id":"BLE_MESH_DHT11","device_name":"DHT11_Node","mesh_addr":"0x0005","error":"read_failed","timestamp_ms":1730000040000}
{"type":"sensor_error","device_id":"BLE_MESH_PH","device_name":"PH_Node","mesh_addr":"0x0008","error":"modbus_read_failed","timestamp_ms":1730000041000}
{"type":"sensor_error","device_id":"BLE_MESH_DO","device_name":"DO_Node","mesh_addr":"0x0009","error":"modbus_read_failed","timestamp_ms":1730000042000}
{"type":"sensor_error","device_id":"BLE_MESH_ORP","device_name":"ORP_Node","mesh_addr":"0x000a","error":"modbus_read_failed","timestamp_ms":1730000043000}
{"type":"servo_result","device_id":"BLE_MESH_SERVO","device_name":"Servo_Node","mesh_addr":"0x0007","result":"executed","direction":"forward","speed_pct":60,"timestamp_ms":1730000045000}
```

### 6.2 串口输入

连续旋转：

```json
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"forward","speed_pct":60}
```

停止：

```json
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"stop","speed_pct":0}
```

停止 PWM 校准：

```json
{"type":"servo_calibrate_stop","device_id":"BLE_MESH_SERVO","stop_pulse_us":1500}
```

pH 校准：

```json
{"type":"ph_calibrate","device_id":"BLE_MESH_PH","point":"7.00"}
```

`point` 可取 `4.00`、`6.86`、`7.00`、`9.18`、`10.00` 或 `10.01`。网关先输出 `ph_calibration_accepted`；电极返回结果后输出：

```json
{"type":"ph_calibration_result","device_id":"BLE_MESH_PH","device_name":"PH_Node","mesh_addr":"0x0008","point":"7.00","result":"success","timestamp_ms":1730000050000}
```

网关收到格式正确的控制命令时立即输出 `servo_command_accepted`。舵机返回 `SERVO_RESULT` 后输出最终 `servo_result`。当舵机离线、JSON 无效或 Mesh 发送失败时，网关输出相应错误类型：`node_offline`、`invalid_command` 或 `mesh_send_failed`。

## 7. LED 状态

默认 LED 规则如下，可在实际硬件测试中微调：

| 状态 | 显示 |
| --- | --- |
| 未配网 | 蓝色慢闪 |
| 配网进行中 | 蓝色快闪 |
| 已配网、网关可达且上线确认完成 | 绿色常亮 |
| 已配网，等待网关心跳或上线确认 | 蓝色慢闪，最长 15 秒 |
| 消息发送或接收 | 绿色短闪 |
| DHT11 阈值告警 | 黄色慢闪 |
| 传感器读取失败、网关超时或 Mesh 故障 | 红色闪烁 |
| 舵机因网关超时停止 | 红色快闪 |

网关在任一节点离线时显示红色告警，并在该节点恢复上报后恢复绿色在线状态。

## 8. 硬件约束

- DHT11 三针模块使用 `3.3 V` 供电，避免 `5 V` DATA 信号损伤 XIAO 的 GPIO。
- 按键模块使用 `3.3 V` 供电。信号为低有效；若模块上拉不足，启用 XIAO 内部上拉。
- SG90 使用独立、至少 `1 A` 的 `5 V` 电源。舵机电源地和 XIAO GND 必须共地。
- 连续旋转 SG90 无角度反馈，只提供正转、反转、速度和停止控制。默认停止脉宽为 `1500 us`，可通过串口命令校准并持久化保存。
- MIK-PH-8001 电极使用独立 `12 V DC` 电源，默认 Modbus 地址 `1`、`9600 8N1`。电极电源地、XIAO GND 和 RS485 GND 必须共地；不得把 `12 V` 接到 XIAO-RS485 的 `5V` 端子。
- MIK-DO-7019 电极使用独立 `12 V DC` 电源，默认 Modbus 地址 `1`、`9600 8N1`。读取范围按手册限制为 `0–20.00 mg/L`、`0–200%` 和 `0–50.0°C`；未校准时允许读数，但空气校准状态未置位会显示黄色警告。
- MIK-ORP-8001 电极使用 `6–30 V DC` 供电，典型为 `12 V DC`，默认 Modbus 地址 `1`、`9600 8N1`。读取范围按手册限制为 `-1000.0–1000.0 mV`，温度范围为 `0–60.0°C`。
- DO 和 ORP 节点的 XIAO-RS485 使用 `D4/P0.04` 发送、`D5/P0.05` 接收、`D2/P0.28` 控制 `DE/RE`；高电平发送、低电平接收。
- pH、DO 和 ORP 节点通过 Zephyr `zephyr,modbus-serial` 驱动管理 RTU 帧间隔、CRC、接收超时及 `DE` 收发切换，业务代码调用官方 Modbus API，不自行拼接 Modbus 帧。DO 和 ORP 节点关闭 UART 控制台，确保 UART0 只用于 RS485。
- 所有 Mesh 配网状态和舵机停止脉宽均持久化到 Flash。设备和网关重启后无需重新配网。

## 9. 后续实现顺序

1. 为网关和六种节点创建共用 Vendor Model 库。
2. 先实现配网、持久化、LED、心跳和 USB CDC JSON。
3. 分别接入 DHT11、按键、SG90、RS485 pH、RS485 DO 和 RS485 ORP 电极驱动。
4. 使用 nRF Mesh App 完成七设备配网、AppKey 绑定与组地址配置。
5. 按 USB CDC JSON 用例完成七设备联调。
