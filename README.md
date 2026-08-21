# AquaMesh Gateway

基于 nRF Connect SDK 2.9.3 的七设备 BLE Mesh 演示项目：

- `DHT11_Node`：每 2 秒上报温湿度，并在阈值状态变化时告警。
- `Button_Node`：上报 `pressed` / `released` 事件并发送在线心跳。
- `Servo_Node`：接收网关下发的连续旋转舵机方向、速度与停止命令。
- `PH_Node`：通过 XIAO-RS485 扩展板读取 MIK-PH-8001，每 2 秒上报温度、pH 和 pH 毫伏值，并支持远程校准。
- `DO_Node`：通过 XIAO-RS485 扩展板读取 MIK-DO-7019，每 2 秒上报溶解氧、温度、饱和度和校准状态。
- `ORP_Node`：通过 XIAO-RS485 扩展板读取 MIK-ORP-8001，每 2 秒上报温度、ORP 和 ORP 漂移值。
- `Gateway_Node`：使用 nRF52840 Dongle 的板载 USB CDC ACM 虚拟串口，将 Mesh 消息转换为每行一个 JSON，并转发舵机控制和 pH 校准命令；固件通过外部 J-Link/Programmer 从 `0x0000` 直接烧录。

首次配网与模型配置使用 Nordic nRF Mesh App。项目设计、消息格式和 USB CDC JSON 协议见 `docs/architecture-and-protocol.md`；接线、配网和联调步骤见 `docs/setup-and-test.md`。

目标板：

- 节点：`xiao_ble/nrf52840/sense`
- 网关：`nrf52840dongle/nrf52840`
- 上位机：`node-red/`，提供 Windows 本地运行、树莓派容器部署、Dashboard 2.0 和 MQTT 预留接口。

树莓派无图形界面的原生发行包构建、首次部署、升级、SSH/串口排障和 Dashboard 测试步骤见 `deploy/raspberry-pi/README.md`；在 Windows PowerShell 中执行 `deploy/raspberry-pi/build-release.ps1` 可生成 `output/ble-mesh-gateway-rpi-v1.0.2.tar.gz`。
