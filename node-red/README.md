# BLE Mesh 网关 Node-RED 看板

该工程从 nRF52840 Dongle 网关的 USB CDC 串口读取单行 JSON，并提供：

- pH、溶解氧、ORP 三组独立看板。
- 每组分别显示当前主读数、当前温度、设备状态、主读数趋势和温度趋势。
- 每组可独立切换近 15 分钟、近 1 小时、近 1 天、近 1 周和近 1 个月。
- 主读数和温度独立更新；任一字段有效时，另一字段缺失不会阻止数据显示。
- SQLite 保存历史：2 秒原始数据保留 1 天，分钟聚合保留 90 天。
- 显示当前串口、有效消息数、JSON 解析错误数和最后接收时间。
- 将所有解析成功的网关 JSON 分类转发到预留 MQTT 接口。

Dashboard 页面：

```text
http://<Node-RED主机>:1880/dashboard/sensors
```

Node-RED 编辑器：

```text
http://<Node-RED主机>:1880
```

## Windows 运行

安装 Node.js 24，关闭占用网关串口的串口工具，然后执行：

```powershell
cd node-red
.\start-windows.ps1
```

默认使用 `auto`：

1. 枚举本机串口并排除蓝牙虚拟串口。
2. 只有一个候选串口时直接选择。
3. 有多个候选串口时，以 `115200 8N1`、DTR 高电平逐个探测。
4. 收到网关单行 JSON 后锁定对应串口。

需要固定串口时仍可显式指定：

```powershell
.\start-windows.ps1 -SerialPort COM24
```

启用 MQTT：

```powershell
.\start-windows.ps1 -EnableMqtt -MqttHost 192.168.1.20 -MqttPort 1883
```

## 历史数据

数据库默认位于：

```text
node-red/data/sensor-history.sqlite
```

保存策略：

- `sensor_raw`：保存 2 秒原始值，保留 24 小时。
- `sensor_minute`：持续生成分钟平均值，保留 90 天。
- 近 15 分钟、近 1 小时读取原始表。
- 近 1 天使用 1 分钟平均值，近 1 周使用 5 分钟平均值，近 1 个月使用 30 分钟平均值。

`node-red/data/` 已加入忽略规则，不会把现场数据库提交到仓库。

## 树莓派打包部署

工业树莓派无图形界面建议使用仓库内的原生发行包，而不是在目标机手工配置 Node-RED。完整步骤见 `../deploy/raspberry-pi/README.md`；Windows PowerShell 构建命令为：

```powershell
.\deploy\raspberry-pi\build-release.ps1
```

该发行包使用 systemd 开机启动，保留 `/opt/ble-mesh-gateway/app` 作为可编辑 Node-RED 工程，并通过 `http://树莓派IP:1880/dashboard/sensors` 提供局域网 Dashboard。

Docker 容器启动前必须先映射一个宿主机串口。优先使用稳定的设备路径：

```bash
ls -l /dev/serial/by-id/
```

复制配置并修改 `SERIAL_PORT`：

```bash
cd node-red
cp .env.example .env
docker compose up -d --build
```

历史数据库通过 `./data:/data/data` 持久化。容器无法访问串口时，查询宿主机 `dialout` 组编号并更新 `.env`：

```bash
getent group dialout
```

## MQTT 接口

默认 `MQTT_ENABLED=false`。启用后使用 QoS 1：

```text
ble_mesh/<device_id>/telemetry
ble_mesh/<device_id>/status
ble_mesh/<device_id>/events
```

- `telemetry`：`dht_report`、`ph_report`、`do_report`、`orp_report`。
- `status`：设备上线、离线、心跳、传感器故障和恢复，使用 retained 消息。
- `events`：按键、舵机结果、校准结果以及其他网关 JSON。

消息保留原始网关字段，并增加：

```json
{
  "schema_version": 1,
  "received_at": "2026-08-18T10:00:00.000Z"
}
```

MQTT 用户名和密码不写入仓库。需要鉴权时，在 Node-RED 编辑器中打开 `Gateway MQTT` 配置节点填写凭据。

## 本地校验

安装依赖后执行：

```bash
npm test
```

校验包括流程引用、Function 语法、DO 缺失温度时继续显示、温度独立更新、离线判断、历史范围、网关诊断和 MQTT 分流。
