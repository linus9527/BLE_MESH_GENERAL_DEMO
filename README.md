# BLE Mesh Button Demo

基于 nRF Connect SDK 2.9.3 的四板 BLE Mesh 演示项目：

- `DHT11_Node`：每 5 秒上报温湿度，并在阈值状态变化时告警。
- `Button_Node`：上报 `pressed` / `released` 事件并发送在线心跳。
- `Servo_Node`：接收网关下发的连续旋转舵机方向、速度与停止命令。
- `Gateway_Node`：将 Mesh 消息转换为每行一个 JSON 的串口消息，并将串口 JSON 命令转发到舵机节点。

首次配网与模型配置使用 Nordic nRF Mesh App。项目设计、消息格式和串口协议见 `docs/architecture-and-protocol.md`；接线、配网和联调步骤见 `docs/setup-and-test.md`。

目标板：

- 节点：`xiao_ble/nrf52840/sense`
- 网关：`nrf52840_mdk/nrf52840`
