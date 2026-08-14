# 接线、配网与联调

## 1. 接线

所有 XIAO 都是 `xiao_ble/nrf52840/sense`。外设信号均使用 3.3 V GPIO 逻辑。

| 节点 | 外设 | XIAO 引脚 | 连接 |
| --- | --- | --- | --- |
| `DHT11_Node` | DHT11 三针模块 | `D0` (`P0.02`) | `VCC -> 3.3V`、`GND -> GND`、`DATA -> D0`。 |
| `Button_Node` | 按键模块 | `D1` (`P0.03`) | `VCC -> 3.3V`、`GND -> GND`、`SIG -> D1`。低电平为按下。 |
| `Servo_Node` | SG90 控制信号 | `D2` (`P0.28`) | `SIG -> D2`、舵机 `VCC -> 独立 5V`、舵机 `GND` 与 XIAO `GND` 共地。 |
| `PH_Node` | MIK-PH-8001 + XIAO-RS485 | 扩展板固定使用 `D4/TX`、`D5/RX`、`D2/DE-RE` | 电极绿线接 `A`、白线接 `B`；红线接独立 `12V+`、黑线接 `12V-`；电极电源地、扩展板 GND 与 XIAO GND 共地。 |

SG90 电源应至少提供 1 A。不得将 DHT11 或按键模块的 5 V 数据输出直接接到 XIAO GPIO。

pH 电极不能由 XIAO-RS485 的 `5V` 端子供电。XIAO 使用 USB 供电，电极单独使用 `12 V DC`；只连接公共地和 RS485 的 `A/B`。单传感器、短距离测试可先关闭扩展板 `120R` 终端电阻；线路较长时仅在总线两端启用终端电阻。若一直出现 `modbus_read_failed`，先断电交换 `A/B` 后重试。

pH 固件使用 Zephyr 官方 Modbus RTU Client：设备树节点为 `zephyr,modbus-serial`，`D2` 作为高有效 `DE`，驱动负责等待 UART 发送完成后切回接收状态，并自动处理 RTU 帧间隔、CRC 和响应超时。

网关使用 `nrf52840_mdk/nrf52840` 的 UART0：`P0.20` 是 TX，`P0.19` 是 RX。连接 3.3 V TTL USB 转串口时交叉连接 TX/RX，并连接 GND。

## 2. 构建

在已激活 nRF Connect SDK 2.9.3 工具链的终端中执行：

```powershell
west build -p always -b xiao_ble/nrf52840/sense apps/dht11 -d build/dht11
west build -p always -b xiao_ble/nrf52840/sense apps/button -d build/button
west build -p always -b xiao_ble/nrf52840/sense apps/servo -d build/servo
west build -p always -b xiao_ble/nrf52840/sense apps/ph -d build/ph
west build -p always -b nrf52840_mdk/nrf52840 apps/gateway -d build/gateway
```

五个构建目录都会生成 `zephyr/zephyr.uf2`，同时保留 `zephyr/zephyr.hex`：

- 四块 XIAO nRF52840 Sense 双击 Reset 进入 `XIAO BLE` U 盘模式，然后将各自构建目录中的 `zephyr.uf2` 拖入该 U 盘。
- nRF52840-MDK 网关也会生成 `zephyr.uf2`，但原厂 DAPLink 优先使用 `zephyr.hex` 拖放或执行 `west flash`；只有更换为兼容 UF2 的 Bootloader 后才使用 UF2 烧录。

标准扩展名是 `.uf2`，不是 `.u2f`。所有节点会将 Mesh 设置保存到 Flash；同一版模型固件重复烧录且未擦除设置区时无需再次配网。由于旧版自定义 SIG 模型已改为 Vendor Model，现有四个设备升级时必须清除旧 Mesh 设置并重新配网；新增 pH 节点按新设备正常配网。

## 3. nRF Mesh App 配置

本版固件使用 **Vendor Model：Company ID `0xFFFF`、Model ID `0x0001`**。旧固件显示的 `Unknown (SIG Model ID: 0xFFFF)` 使用了与 Mesh Foundation 冲突的 SIG 操作码，不能继续沿用旧的模型绑定和订阅。所有绑定、订阅操作都必须在上述 Vendor Model 上完成，不能在 `Configuration Server (0x0000)` 上操作。`0xFFFF` 仅用于当前原型；产品化时必须替换为正式分配的 Bluetooth SIG Company ID。

### 3.1 建立或核对网络

1. 可以保留现有 Mesh 网络、NetKey、AppKey 和三个组，但要先在 App 中对旧的四个设备执行 `Reset Node` 并删除旧节点记录；若节点无法连接 App，则使用调试器完整擦除芯片。只重新烧录 `.uf2`/`.hex` 通常不会清除 Mesh 设置区。
2. 烧录本版固件后添加五个设备，名称分别设为 `BLE_MESH_GATEWAY`、`BLE_MESH_DHT11`、`BLE_MESH_BUTTON`、`BLE_MESH_SERVO`、`BLE_MESH_PH`。每个设备只有一个 Element，并包含 `Configuration Server` 和 Vendor Model `0xFFFF:0x0001`。
3. 在 `Groups` 页面建立或核对以下三个地址：`Gateway_Rx` = `0xC000`、`Servo_CTRL` = `0xC001`、`Common_CTRL` = `0xC002`。组名可以不同，地址必须完全一致。

### 3.2 逐节点绑定 AppKey

对五个设备逐一进入 `Node Configuration`，展开唯一的 Element，点击 Company ID `0xFFFF`、Model ID `0x0001` 的 Vendor Model，进入 `Bound App Keys`，点击 `BIND KEY`，选择同一个 `Application Key 1`。完成后该页面必须显示已绑定的 AppKey。

### 3.3 设置订阅地址

仍在同一个 Vendor Model `0xFFFF:0x0001` 页面的 `Subscriptions` 中，点击 `SUBSCRIBE` 并按下表设置：

| 节点 | 必须订阅的组地址 | 作用 |
| --- | --- | --- |
| `BLE_MESH_GATEWAY` | `Gateway_Rx` (`0xC000`) | 接收四类节点的状态、传感器数据和执行结果。 |
| `BLE_MESH_DHT11` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |
| `BLE_MESH_BUTTON` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |
| `BLE_MESH_SERVO` | `Servo_CTRL` (`0xC001`)、`Common_CTRL` (`0xC002`) | 接收舵机命令与网关心跳。 |
| `BLE_MESH_PH` | `Common_CTRL` (`0xC002`) | 接收网关心跳；校准命令由网关按节点单播地址发送。 |

`Publish Address` 保持 `None`，这是正确的：固件通过 `bt_mesh_model_send()` 直接发送到固定组地址，不使用手机 App 的 Publication 配置。每个节点启用 Relay，TTL 保持 `7`。

### 3.4 上电验证顺序

1. 先打开电脑串口终端，再给网关上电；网关已配网时会先输出一行 `gateway_online` JSON。
2. 再给 DHT11、按键、舵机和 pH 节点上电。节点在等待网关心跳和 `NODE_ONLINE_ACK` 时蓝色慢闪；网关确认上线后绿色常亮；超过 15 秒未收到网关心跳则红色闪烁。
3. 网关收到每个节点的 `NODE_ONLINE` 后先输出 `device_online`，之后继续输出 `device_heartbeat` 或业务数据。若未出现这些 JSON，先检查 AppKey 是否绑定在 Vendor Model `0xFFFF:0x0001` 上，以及订阅地址是否正确。
   上线阶段采用确认机制：节点每 2 秒重发 `NODE_ONLINE`，直到网关向该节点单播 `NODE_ONLINE_ACK`；确认完成后只保留正常数据上报和 5 秒心跳。
## 4. 串口测试

网关串口是 `115200 8N1`，只传输一行一个 UTF-8 JSON。当前 `timestamp_ms` 是网关自启动以来的单调毫秒数；接入真实云端或 RTC 后可替换为 Unix 时间戳。

串口必须使用 MDK 的 `P0.20 (TX)`、`P0.19 (RX)` 和 GND 连接 3.3 V USB-TTL；仅插开发板 USB 不等于已连接此 UART。

将以下命令逐行输入网关串口：

```json
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"forward","speed_pct":60}
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"reverse","speed_pct":40}
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"stop","speed_pct":0}
{"type":"servo_calibrate_stop","device_id":"BLE_MESH_SERVO","stop_pulse_us":1500}
{"type":"ph_calibrate","device_id":"BLE_MESH_PH","point":"7.00"}
```

预期现象：

- DHT11 每 5 秒输出 `dht_report`；越界及恢复时各输出一次 `dht_alert`。
- 按键输入连续稳定 `50 ms` 后才确认变化；一次完整点击正常输出两条 `button_event`，分别为 `pressed` 和 `released`，并每 5 秒输出 `device_heartbeat`。若未按下时轻触导线仍能维持低电平超过 50 ms，应缩短信号线，并在 `SIG` 与 `3.3V` 之间增加约 `10 kΩ` 外部上拉。
- 舵机命令会先得到 `servo_command_accepted`，随后得到 Mesh 返回的 `servo_result`。
- pH 节点每 5 秒输出 `ph_report`，包含 `temperature_c`、`ph` 和 `ph_mv`，不产生阈值告警；通信失败时输出一次 `sensor_error`，恢复后输出一次 `sensor_recovered`。
- 任一节点超过 15 秒未上报，网关输出 `device_offline` 并亮红色告警灯。
- 舵机超过 15 秒未收到网关心跳后停止，随后上报 `safe_stopped`。

## 5. pH 校准

电极默认参数为 Modbus 地址 `1`、`9600 8N1`。校准前准备与命令中的 `point` 完全一致的标准缓冲液，清洗电极后放入缓冲液并等待至少 1 分钟，读数稳定后再从网关串口发送 `ph_calibrate`。可选点为 `4.00`、`6.86`、`7.00`、`9.18`、`10.00`、`10.01`；应按电极当前使用的标准体系选择，不要混用两套缓冲液体系。

成功流程会依次出现 `ph_calibration_accepted` 和 `ph_calibration_result`，后者的 `result` 为 `success`。`communication_error` 表示 Modbus 无响应或 CRC 错误，优先检查 12 V 供电、公共地、`A/B` 和从站参数；`rejected` 表示校准点无效或电极返回内容不符合写单寄存器回显格式。
