# BLE Mesh Gateway Raspberry Pi Release

这个目录生成的是面向工业树莓派的“固件式发行包”，不是绑定某一个型号的 SD 卡镜像。包内包含：

- `main`：Linux 启动入口，负责启动 Node-RED 和串口自动识别。
- `app/`：可继续编辑的 Node-RED 工程。
- `systemd/`：开机自动启动服务。
- `install.sh`、`update.sh`、`uninstall.sh`：安装、升级和卸载。
- `firmware/`：当前 nRF52840-Dongle 网关的 UF2、HEX 和 BIN。
- `config/`：树莓派环境变量模板。

`main` 是树莓派上的启动程序；`firmware/gateway-merged.hex` 和 `firmware/gateway.uf2` 是烧录到 nRF52840-Dongle 的固件，二者不是同一个设备上的文件。

## 1. 构建发行包

在 Windows PowerShell 的仓库根目录执行：

```powershell
.\deploy\raspberry-pi\build-release.ps1
```

生成文件：

```text
output/ble-mesh-gateway-rpi-v1.0.2.tar.gz
```

构建脚本默认使用最新网关构建目录：`apps/gateway/build_2/gateway`。如果需要指定其他固件构建目录：

```powershell
.\deploy\raspberry-pi\build-release.ps1 -GatewayBuildDirectory .\build\gateway
```

## 2. 目标环境

推荐使用联网的 64 位 Raspberry Pi OS Lite 或 Debian 系统，并通过 SSH 操作。检查架构：

```bash
uname -m
cat /etc/os-release
```

正常应看到 `aarch64`。本包默认拒绝 32 位 `armv7l`；如果设备确实只能运行 32 位系统，可以在配置文件中设置 `ALLOW_ARMV7L=1` 后自行验证。

树莓派需要：

- 已启动的 systemd 系统。
- 首次安装时可访问互联网，用于下载 ARM Node.js 和安装依赖。
- nRF52840-Dongle 通过 USB 接入树莓派。
- 树莓派与 Windows 电脑处于同一局域网。

## 3. Windows 传包到树莓派

在 Windows PowerShell 中，将生成的压缩包复制到树莓派：

```powershell
scp .\output\ble-mesh-gateway-rpi-v1.0.2.tar.gz pi@192.168.50.2:/home/pi/
```

将 `pi` 和 `192.168.50.2` 换成实际用户名和 IP。首次使用时可以先确认 SSH：

```powershell
ssh pi@192.168.50.2
```

首次连接时输入 `yes` 确认主机指纹，再输入树莓派用户密码。输入密码时终端不显示字符是正常现象。不要使用 `ssh -vvv` 作为日常连接命令，否则会持续打印 `debug3` 调试信息。

如果可以 `ping` 通树莓派但 SSH 无法连接，先在 Windows 检查 SSH 端口：

```powershell
Test-NetConnection 192.168.50.2 -Port 22
```

当 `TcpTestSucceeded` 为 `True` 时，网络与 SSH 服务可达；应改用正确的树莓派用户名或密码继续登录。

## 4. 安装

SSH 登录树莓派后执行：

```bash
cd /tmp
mkdir -p ble-mesh-release
tar -xzf /home/pi/ble-mesh-gateway-rpi-v1.0.2.tar.gz -C ble-mesh-release --strip-components=1
cd ble-mesh-release
sudo bash ./install.sh
```

如已部署过旧版本，使用升级流程，不要重复执行首次安装：

```bash
cd /home/pi
mkdir -p ble-mesh-update
tar -xzf ble-mesh-gateway-rpi-v1.0.2.tar.gz -C ble-mesh-update --strip-components=1
cd ble-mesh-update
sudo bash ./update.sh
```

升级脚本会备份现场的 Node-RED Flow、环境配置和历史数据，不会覆盖现场流程。

安装脚本会：

1. 创建 `blemesh` 系统用户，并加入 `dialout` 组。
2. 安装或复用 Node.js `24.19.0`。
3. 安装锁定版本的 pnpm 和 Node-RED 生产依赖。
4. 将工程放到 `/opt/ble-mesh-gateway/app`。
5. 将历史数据库放到 `/var/lib/ble-mesh-gateway/data`。
6. 创建 `/etc/ble-mesh-gateway/ble-mesh-gateway.env`。
7. 注册并启用 `ble-mesh-gateway.service`。

安装完成后查看服务：

```bash
sudo systemctl status ble-mesh-gateway --no-pager
sudo journalctl -u ble-mesh-gateway -f
```

确认开机自启动和当前运行状态：

```bash
systemctl is-enabled ble-mesh-gateway.service
systemctl is-active ble-mesh-gateway.service
```

预期分别为 `enabled` 和 `active`。若服务此前启动失败，可执行：

```bash
sudo systemctl reset-failed ble-mesh-gateway.service
sudo systemctl restart ble-mesh-gateway.service
```

## 5. 串口设置

默认 `SERIAL_PORT=auto`。启动器会扫描串口，自动合并 `/dev/ttyUSB0` 与 `/dev/serial/by-id/...` 这类同一物理设备的不同路径；存在多个物理串口时会尝试识别网关 JSON。先查看设备：

```bash
ls -l /dev/serial/by-id/
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

现场设备较多时，建议编辑配置文件，使用稳定的 by-id 路径：

```bash
sudo nano /etc/ble-mesh-gateway/ble-mesh-gateway.env
```

例如：

```dotenv
SERIAL_PORT=/dev/serial/by-id/usb-SEGGER_J-Link_000000000000-if00
```

保存后重启：

```bash
sudo systemctl restart ble-mesh-gateway
```

如果权限不足：

```bash
id blemesh
getent group dialout
sudo usermod -aG dialout blemesh
sudo systemctl restart ble-mesh-gateway
```

## 6. Windows 访问 Dashboard

在树莓派上查看 IP：

```bash
hostname -I
```

在 Windows 浏览器访问：

```text
http://树莓派IP:1880/dashboard/sensors
```

例如：

```text
http://192.168.50.2:1880/dashboard/sensors
```

Node-RED 编辑器地址为：

```text
http://192.168.50.2:1880
```

无图形界面的树莓派不需要显示器或桌面环境；浏览器运行在 Windows 上，Dashboard 页面通过局域网访问。

Windows 端可以先测试端口：

```powershell
Test-NetConnection 192.168.50.2 -Port 1880
```

树莓派本机可以测试 Node-RED：

```bash
curl -I http://127.0.0.1:1880/dashboard/sensors
```

## 7. 联调顺序

1. 先确认 `gateway_online` 出现在 `journalctl` 日志中。
2. 再给 Mesh 传感器节点上电，观察 `device_online`、`device_heartbeat` 和各类 `*_report`。
3. 打开 Dashboard，确认 pH、DO、ORP 三组的当前值、温度、状态和趋势图更新。
4. 暂时断开一个传感器，等待离线超时，确认只清空该传感器当前读数，其他设备不受影响。
5. 恢复传感器，确认它重新显示在线和最新读数。

日志过滤示例：

```bash
sudo journalctl -u ble-mesh-gateway -f | grep --line-buffered -E 'gateway_online|device_online|device_offline|ph_report|do_report|orp_report'
```

## 8. 在树莓派上继续开发

部署后仍可直接打开 Node-RED 编辑器修改流程、增加节点并点击 Deploy。工程目录是：

```text
/opt/ble-mesh-gateway/app
```

历史数据目录是：

```text
/var/lib/ble-mesh-gateway/data
```

也可以 SSH 后安装额外节点：

```bash
cd /opt/ble-mesh-gateway/app
sudo -u blemesh /opt/ble-mesh-gateway/runtime/bin/pnpm add <node-package>
sudo systemctl restart ble-mesh-gateway
```

升级发行包前，脚本会备份 `flows.json`、`settings.js`、`package.json`、`pnpm-lock.yaml` 和环境文件；升级不会覆盖现场流程和配置。

执行升级：

```bash
sudo bash ./update.sh
```

备份位置：

```text
/var/lib/ble-mesh-gateway/backups/
```

## 9. 网关固件烧录

本包不会在树莓派上自动烧录 nRF52840-Dongle。按照项目现有流程使用外部 J-Link/Programmer 烧录：

```text
firmware/gateway-merged.hex
```

`firmware/gateway.uf2` 作为备用文件保留。烧录前确认网关固件版本和 Mesh 配网数据策略；执行全片擦除会清除配网信息。烧录完成后把 Dongle 插回树莓派，再重启服务。

## 10. 常见问题

### Dashboard 打不开

```bash
sudo systemctl status ble-mesh-gateway --no-pager
sudo ss -ltnp | grep 1880
sudo journalctl -u ble-mesh-gateway -n 100 --no-pager
```

确认访问的是树莓派 IP，不是 `127.0.0.1`；确认 Windows 和树莓派在同一局域网。

### 只有 gateway_online，没有传感器数据

确认 nRF Mesh App 中各节点的 AppKey 绑定、Vendor Model 和订阅地址正确，再检查节点是否上电。Node-RED 只负责解析网关已经输出的 JSON。

### 服务显示 `activating` 或 Dashboard 打不开

服务会先识别网关串口，再启动 Node-RED；因此串口识别失败时 Dashboard 端口 `1880` 不会监听。按下面顺序检查：

```bash
sudo journalctl -u ble-mesh-gateway.service -n 100 --no-pager
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
ls -l /dev/serial/by-id/ 2>/dev/null
```

如果日志出现“存在多个串口但无法识别网关”，优先升级到 `v1.0.2`；该版本修复了同一 USB 串口被重复计数的问题。若现场仍有多个真实串口，按下一节固定网关串口路径。

### 自动扫描无法确定网关串口

将 `SERIAL_PORT` 改成 `/dev/serial/by-id/...`，不要使用会变化的 `/dev/ttyACM0`。

### `ERR_PNPM_MISSING_PACKAGE_NAME`

这是旧版 `v1.0.0` 中 workspace 配置不完整导致的。请重新上传并安装 `v1.0.2`，不要继续使用旧包。

### 首次安装没有网络

可以先在另一台同架构树莓派上准备 Node.js 压缩包，并同时设置 `NODE_RUNTIME_ARCHIVE=/path/to/node-v24.19.0-linux-arm64.tar.xz` 与对应的 `NODE_RUNTIME_SHA256`，再将包复制到目标机；Node-RED npm 依赖仍需要预先准备网络或 pnpm store。

## 11. 安全提示

当前工程面向局域网测试，Node-RED 编辑器和 Dashboard 尚未配置登录认证。不要将 1880 端口直接暴露到公网；正式部署前应增加认证、局域网隔离或 VPN。
