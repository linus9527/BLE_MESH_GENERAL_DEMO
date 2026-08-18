# 接线、配网与联调

## 1. 接线

所有 XIAO 都是 `xiao_ble/nrf52840/sense`。外设信号均使用 3.3 V GPIO 逻辑。

| 节点 | 外设 | XIAO 引脚 | 连接 |
| --- | --- | --- | --- |
| `DHT11_Node` | DHT11 三针模块 | `D0` (`P0.02`) | `VCC -> 3.3V`、`GND -> GND`、`DATA -> D0`。 |
| `Button_Node` | 按键模块 | `D1` (`P0.03`) | `VCC -> 3.3V`、`GND -> GND`、`SIG -> D1`。低电平为按下。 |
| `Servo_Node` | SG90 控制信号 | `D2` (`P0.28`) | `SIG -> D2`、舵机 `VCC -> 独立 5V`、舵机 `GND` 与 XIAO `GND` 共地。 |
| `PH_Node` | MIK-PH-8001 + XIAO-RS485 | 扩展板固定使用 `D4/RX`、`D5/TX`、`D2/DE-RE` | 电极绿线接 `A`、白线接 `B`；红线接独立 `12V+`、黑线接 `12V-`；电极电源地、扩展板 GND 与 XIAO GND 共地。 |
| `DO_Node` | MIK-DO-7019 + XIAO-RS485 | 固定使用 `D4/P0.04 TX`、`D5/P0.05 RX`、`D2/P0.28 DE-RE` | 电极 RS485 `A/B` 接扩展板同名端子；电极接独立 `12V DC`；电极电源地、扩展板 GND 与 XIAO GND 共地。 |
| `ORP_Node` | MIK-ORP-8001 + XIAO-RS485 | 固定使用 `D4/P0.04 TX`、`D5/P0.05 RX`、`D2/P0.28 DE-RE` | 电极 RS485 `A/B` 接扩展板同名端子；电极使用独立 `6–30V DC` 供电，推荐 `12V DC`；电极电源地、扩展板 GND 与 XIAO GND 共地。 |

SG90 电源应至少提供 1 A。不得将 DHT11 或按键模块的 5 V 数据输出直接接到 XIAO GPIO。

pH、DO 和 ORP 电极不能由 XIAO-RS485 的 `5V` 端子供电。XIAO 使用 USB 或扩展板稳定的 `5 V` 输出供电，电极使用各自规定的独立电源；只连接公共地和 RS485 的 `A/B`。单传感器、短距离测试可先关闭扩展板 `120R` 终端电阻；线路较长时仅在总线两端启用终端电阻。若一直出现 `modbus_read_failed`，先断电交换 `A/B` 后重试。

pH、DO 和 ORP 固件使用 Zephyr 官方 Modbus RTU Client：设备树节点为 `zephyr,modbus-serial`，`D2` 作为高有效 `DE`，驱动负责等待 UART 发送完成后切回接收状态，并自动处理 RTU 帧间隔、CRC 和响应超时。DO 和 ORP 固件禁用 UART 控制台，UART0 只用于 RS485。

网关使用 `nrf52840dongle/nrf52840`。运行固件通过开发板自带的 USB 接口枚举 CDC ACM 虚拟串口，不需要连接 UART 引脚，也不需要外置 USB-TTL 模块。网关状态使用 Dongle 的 RGB LED1：红色 `P0.08`、绿色 `P1.09`、蓝色 `P0.12`。

## 2. 构建

在已激活 nRF Connect SDK 2.9.3 工具链的终端中执行：

```powershell
west build -p always -b xiao_ble/nrf52840/sense apps/dht11 -d build/dht11
west build -p always -b xiao_ble/nrf52840/sense apps/button -d build/button
west build -p always -b xiao_ble/nrf52840/sense apps/servo -d build/servo
west build -p always -b xiao_ble/nrf52840/sense apps/ph -d build/ph
west build -p always -b xiao_ble/nrf52840/sense apps/do -d build/do
west build -p always -b xiao_ble/nrf52840/sense apps/orp -d build/orp
west build -p always -b nrf52840dongle/nrf52840 apps/gateway -d build/gateway
```

七个构建目录都会生成 `zephyr/zephyr.uf2`，同时保留 `zephyr/zephyr.hex`：

- 六块 XIAO nRF52840 Sense 双击 Reset 进入 `XIAO BLE` U 盘模式，然后将各自构建目录中的 `zephyr.uf2` 拖入该 U 盘。
- nRF52840 Dongle 网关使用外部 J-Link/Programmer 烧录。网关配置已关闭原厂 nRF5 Bootloader 偏移，`build/gateway/merged.hex` 从 `0x0000` 直接启动，允许 Programmer 执行全片擦除后写入。

J-Link 必须连接 Dongle 的专用 `SWDIO`、`SWDCLK`、`VDD_nRF` 和 `GND`，不能连接 `P1.07`、`P1.01`、`P1.04`、`P1.02` 等普通 GPIO。在 nRF Connect Programmer 中添加 `build/gateway/merged.hex` 后执行 **Erase & write**，也可以使用：

```powershell
west flash -d build/gateway --runner nrfjprog --erase
```

全片擦除会同时清除 Mesh 配网信息，因此烧录后需要重新配网。烧录完成后拔掉 J-Link，仅将 Dongle USB 插入电脑；运行时会枚举新的 CDC ACM COM 口。

标准扩展名是 `.uf2`，不是 `.u2f`。所有节点会将 Mesh 设置保存到 Flash；同一版模型固件重复烧录且未擦除设置区时无需再次配网。由于旧版自定义 SIG 模型已改为 Vendor Model，现有四个设备升级时必须清除旧 Mesh 设置并重新配网；新增 pH 节点按新设备正常配网。

## 3. nRF Mesh App 配置

本版固件使用 **Vendor Model：Company ID `0xFFFF`、Model ID `0x0001`**。旧固件显示的 `Unknown (SIG Model ID: 0xFFFF)` 使用了与 Mesh Foundation 冲突的 SIG 操作码，不能继续沿用旧的模型绑定和订阅。所有绑定、订阅操作都必须在上述 Vendor Model 上完成，不能在 `Configuration Server (0x0000)` 上操作。`0xFFFF` 仅用于当前原型；产品化时必须替换为正式分配的 Bluetooth SIG Company ID。

### 3.1 建立或核对网络

1. 可以保留现有 Mesh 网络、NetKey、AppKey 和三个组，但要先在 App 中对旧的四个设备执行 `Reset Node` 并删除旧节点记录；若节点无法连接 App，则使用调试器完整擦除芯片。只重新烧录 `.uf2`/`.hex` 通常不会清除 Mesh 设置区。
2. 烧录本版固件后添加七个设备，名称分别设为 `BLE_MESH_GATEWAY`、`BLE_MESH_DHT11`、`BLE_MESH_BUTTON`、`BLE_MESH_SERVO`、`BLE_MESH_PH`、`BLE_MESH_DO`、`BLE_MESH_ORP`。每个设备只有一个 Element，并包含 `Configuration Server` 和 Vendor Model `0xFFFF:0x0001`。
3. 在 `Groups` 页面建立或核对以下三个地址：`Gateway_Rx` = `0xC000`、`Servo_CTRL` = `0xC001`、`Common_CTRL` = `0xC002`。组名可以不同，地址必须完全一致。

### 3.2 逐节点绑定 AppKey

对七个设备逐一进入 `Node Configuration`，展开唯一的 Element，点击 Company ID `0xFFFF`、Model ID `0x0001` 的 Vendor Model，进入 `Bound App Keys`，点击 `BIND KEY`，选择同一个 `Application Key 1`。完成后该页面必须显示已绑定的 AppKey。

### 3.3 设置订阅地址

仍在同一个 Vendor Model `0xFFFF:0x0001` 页面的 `Subscriptions` 中，点击 `SUBSCRIBE` 并按下表设置：

| 节点 | 必须订阅的组地址 | 作用 |
| --- | --- | --- |
| `BLE_MESH_GATEWAY` | `Gateway_Rx` (`0xC000`) | 接收六类节点的状态、传感器数据和执行结果。 |
| `BLE_MESH_DHT11` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |
| `BLE_MESH_BUTTON` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |
| `BLE_MESH_SERVO` | `Servo_CTRL` (`0xC001`)、`Common_CTRL` (`0xC002`) | 接收舵机命令与网关心跳。 |
| `BLE_MESH_PH` | `Common_CTRL` (`0xC002`) | 接收网关心跳；校准命令由网关按节点单播地址发送。 |
| `BLE_MESH_DO` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |
| `BLE_MESH_ORP` | `Common_CTRL` (`0xC002`) | 接收网关心跳。 |

`Publish Address` 保持 `None`，这是正确的：固件通过 `bt_mesh_model_send()` 直接发送到固定组地址，不使用手机 App 的 Publication 配置。每个节点启用 Relay，TTL 保持 `7`。

### 3.4 上电验证顺序

1. 将 nRF52840 Dongle 插入电脑，等待固件枚举出 CDC ACM COM 口，再用上位机或串口终端以 `115200 8N1` 打开并置位 DTR；网关已配网时会输出一行 `gateway_online` JSON。
2. 再给 DHT11、按键、舵机、pH、DO 和 ORP 节点上电。节点在等待网关心跳和 `NODE_ONLINE_ACK` 时蓝色慢闪；网关确认上线后绿色常亮；超过 15 秒未收到网关心跳则红色闪烁。DO 节点通信正常但未完成空气校准时显示黄色。
3. 网关收到每个节点的 `NODE_ONLINE` 后先输出 `device_online`，之后继续输出 `device_heartbeat` 或业务数据。若未出现这些 JSON，先检查 AppKey 是否绑定在 Vendor Model `0xFFFF:0x0001` 上，以及订阅地址是否正确。
   上线阶段采用确认机制：节点每 2 秒重发 `NODE_ONLINE`，直到网关向该节点单播 `NODE_ONLINE_ACK`；确认完成后传感器每 2 秒采集并上报，按键和舵机节点每 5 秒发送心跳。
## 4. USB CDC 串口测试

网关通过板载 USB 枚举 CDC ACM 虚拟串口。上位机按 `115200 8N1` 打开该 COM 口并置位 DTR，端口只传输一行一个 UTF-8 JSON。当前 `timestamp_ms` 是网关自启动以来的单调毫秒数；接入真实云端或 RTC 后可替换为 Unix 时间戳。

不再使用 MDK 的 UART 引脚或外置 USB-TTL。若打开端口后没有 `gateway_online`，先确认选择的是固件运行时的 COM 口而不是 Bootloader COM 口，并确认上位机已置位 DTR。

将以下命令逐行输入网关串口：

```json
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"forward","speed_pct":60}
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"reverse","speed_pct":40}
{"type":"servo_command","device_id":"BLE_MESH_SERVO","direction":"stop","speed_pct":0}
{"type":"servo_calibrate_stop","device_id":"BLE_MESH_SERVO","stop_pulse_us":1500}
{"type":"ph_calibrate","device_id":"BLE_MESH_PH","point":"7.00"}
```

预期现象：

- DHT11 每 2 秒输出 `dht_report`；越界及恢复时各输出一次 `dht_alert`。
- 按键输入连续稳定 `50 ms` 后才确认变化；一次完整点击正常输出两条 `button_event`，分别为 `pressed` 和 `released`，并每 5 秒输出 `device_heartbeat`。若未按下时轻触导线仍能维持低电平超过 50 ms，应缩短信号线，并在 `SIG` 与 `3.3V` 之间增加约 `10 kΩ` 外部上拉。
- 舵机命令会先得到 `servo_command_accepted`，随后得到 Mesh 返回的 `servo_result`。
- pH 节点每 2 秒输出 `ph_report`，包含 `temperature_c`、`ph` 和 `ph_mv`，不产生阈值告警；通信失败时输出一次 `sensor_error`，恢复后输出一次 `sensor_recovered`。
- DO 节点每 2 秒输出 `do_report`，包含 `dissolved_oxygen_mg_l`、`temperature_c`、`saturation_pct`、`calibration_status_known`、`air_calibrated` 和 `zero_calibrated`；仅校准状态读取失败时两个校准字段为 `null`，测量数据仍正常上报。测量通信失败时输出一次 `sensor_error`，恢复后输出一次 `sensor_recovered`。
- ORP 节点每 2 秒输出 `orp_report`，包含 `temperature_c`、`orp_mv` 和 `orp_drift_mv`；通信失败时输出一次 `sensor_error`，恢复后输出一次 `sensor_recovered`。
- 任一节点连续 30 秒没有任何有效消息时，网关只为该节点输出一次 `device_offline` 并显示黄色；其他节点继续正常工作。离线节点再次发送有效消息后，网关输出 `device_online`，全部已知节点恢复在线后重新显示绿色。
- 舵机超过 15 秒未收到网关心跳后停止，随后上报 `safe_stopped`。

## 5. pH 校准

电极默认参数为 Modbus 地址 `1`、`9600 8N1`。校准前准备与命令中的 `point` 完全一致的标准缓冲液，清洗电极后放入缓冲液并等待至少 1 分钟，读数稳定后再从网关串口发送 `ph_calibrate`。可选点为 `4.00`、`6.86`、`7.00`、`9.18`、`10.00`、`10.01`；应按电极当前使用的标准体系选择，不要混用两套缓冲液体系。

成功流程会依次出现 `ph_calibration_accepted` 和 `ph_calibration_result`，后者的 `result` 为 `success`。`communication_error` 表示 Modbus 无响应或 CRC 错误，优先检查 12 V 供电、公共地、`A/B` 和从站参数；`rejected` 表示校准点无效或电极返回内容不符合写单寄存器回显格式。

## 6. 溶解氧节点

MIK-DO-7019 默认使用地址 `1`、`9600 8N1`。DO 节点通过功能码 `0x03` 读取 `0x2001–0x2006`，再尝试读取 `0x200F` 校准状态；第二次读取失败不会丢弃已经取得的测量值。Zephyr Modbus 驱动自动生成请求帧和 CRC。串口工具等价的测量请求为 `01 03 20 01 00 06 9F C8`。

读数范围按手册检查为 `0–20.00 mg/L`、`0–200%` 和 `0–50.0°C`。未完成空气校准时仍会向网关上报读数，但 `air_calibrated` 为 `false` 且节点显示黄色；Modbus 无响应、状态寄存器读取失败或数据越界时节点闪红灯并发送带传感器故障位的心跳。

## 7. ORP 节点

MIK-ORP-8001 默认使用地址 `1`、`9600 8N1`。ORP 节点先读取温度寄存器 `0`，再连续读取 ORP 和漂移寄存器 `9–10`；对应原始 Modbus 请求分别为 `01 03 00 00 00 01 84 0A` 和 `01 03 00 09 00 02 14 09`。Zephyr 官方 Modbus 驱动负责生成请求、CRC、超时处理和 `DE/RE` 切换。

温度、ORP 和漂移均按有符号 16 位解析，换算公式为 `实际值 = 有符号原始值 / 10.0`。例如响应数据 `07 26` 为原始值 `1830`，对应 `183.0 mV`；`FF 9C` 为原始值 `-100`，对应 `-10.0 mV`。固件接受温度 `-20.0–60.0°C`、ORP `-1000.0–1000.0 mV`；读取失败或数值越界时节点闪红灯并发送带传感器故障位的心跳。手册没有给出明确的 ORP 校准执行命令，本版不实现远程 ORP 校准，避免误写寄存器。
