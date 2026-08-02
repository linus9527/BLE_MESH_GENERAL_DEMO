---
title: "Seeed Studio XIAO nRF52840"
aliases: ["XIAO nRF52840", "XIAO BLE"]
category: "外设"
peripheral_type: "无线开发板"
manufacturer: "Seeed Studio"
model: "XIAO nRF52840"
variant: "标准版，D0-D10，无板载 IMU/麦克风"
module_or_component: "module"
interfaces: ["USB", "GPIO", "ADC", "UART", "I2C", "SPI", "NFC", "SWD", "Bluetooth LE"]
knowledge_status: "conditional"
hardware_validation: "untested"
source_grade: "A"
created: "2026-08-02"
updated: "2026-08-02"
tags: ["外设", "开发板", "Seeed", "nRF52840", "BLE", "NFC"]
---

# Seeed Studio XIAO nRF52840

## 0. 结论卡

> 21 mm × 17.8 mm 的 nRF52840 无线开发板，适合 BLE/NFC、电池供电和可贴装产品；当前资料足以指导原型接线，但充电电流控制说明存在官方冲突，因此状态为 `conditional`。[S2 §Features][S3 p.18]

| 项目 | 结论 |
| --- | --- |
| 典型用途 | BLE 传感节点、可穿戴、无线控制、TinyML 原型 |
| 关键接口 | USB、D0-D10、6×ADC、UART、I2C、SPI、NFC、SWD |
| 供电/逻辑 | USB-C/VBUS 5 V；BAT 标称 3.7 V 锂电；系统与 GPIO 为 3.3 V 域 |
| 最重要限制 | 不要把 VBUS 5 V 接入 GPIO；电池 ADC 必须正确控制 `READ_BAT_ENABLE` |
| 主来源 | [S3] Seeed XIAO Series SoM Datasheet、[S4] 官方原理图 v1.1 |
| 硬件验证 | untested |

### 必看警告

- `3V3_OUT` 是 3.3 V 输出，不能视作 5 V 容忍 GPIO 的证据；外接 5 V 外设需要核对电平或增加转换。[S2 §Hardware overview][S4 p.3]
- 读取电池电压时将 `P0.14/READ_BAT_ENABLE` 配置为仅下拉/输出低以启用分压，再读取 `P0.31/AIN7_BAT`；不要在充电期间把 P0.14 置高。[S2 §FAQ Q3][S4 p.3]
- 外部天线不能直接接在普通 NFC/GPIO 上；NFC1/NFC2 是专用差分天线接口。[S2 §Hardware overview]

## 1. 识别信息

| 项目 | 内容 | 证据 |
| --- | --- | --- |
| 制造商 | Seeed Studio | [S2 页面标题] |
| 完整型号 | Seeed Studio XIAO nRF52840 | [S2 §Specifications comparison] |
| 变体/封装/板版本 | 标准版；本次官方原理图 Rev V1.1，2025-12-19 | [S4 p.1-p.2] |
| 裸器件或模块 | 可独立编程、可表贴的开发板/模块 | [S3 p.6-p.7] |
| 识别特征 | USB-C、板载天线、复位键、RGB 用户 LED、充电 LED；无 IMU/麦克风 | [S2 §Hardware overview] |

### 相近型号与兼容性

Sense 版增加 IMU 和 PDM 麦克风；Plus 版增加 D11-D19 并改变可贴装焊盘布局。扩展板机械兼容不能代替引脚/底部焊盘兼容确认，参见 [[Seeed-Studio-XIAO-nRF52840-系列索引]]。[S2 §Specifications comparison]

## 2. 关键参数

### 电气参数

| 参数 | 最小 | 典型 | 最大 | 单位 | 条件/备注 | 证据 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| USB/VBUS 输入 |  | 5 |  | V | Type-C 供电 | [S3 p.18 §4.1.4] |
| BAT 输入 |  | 3.7 |  | V | Seeed 指定锂离子电池标称值；不是绝对最大额定 | [S3 p.18 §4.1.4] |
| 系统/GPIO 电压域 |  | 3.3 |  | V | 原理图 `System 3.3V` | [S4 p.3] |
| 正常功耗 |  | 10.38 |  | mA | SoM 手册给出的板级“Normal”值，负载/无线状态未细分 | [S3 p.18 §4.1.3] |
| 低功耗模式 |  | <5 |  | µA | 需使用适配 BSP 并让外部 QSPI 进入深度掉电 | [S3 p.18 §4.1.3][S2 §Power Consumption Verification] |
| 充电电流 | 45 | 50 | 55 | mA | SoM 手册默认规格；Wiki 另有 50/100 mA 可选说法 | [S3 p.18 §4.1.5][S2 §Battery Charging current] |
| 3.3 V 电源能力 |  | 200 | 250 | mA | 200 mA 来自板级工业数据；250 mA 是原理图稳压器器件标注，实际可供外设电流需扣除板耗 | [S5 p.5][S4 p.3] |

### 性能参数

| 参数 | 数值/范围 | 条件 | 证据 |
| --- | --- | --- | --- |
| CPU | Arm Cortex-M4F，最高 64 MHz | nRF52840 | [S2 §Specifications comparison] |
| 存储 | 1 MB 片上 Flash、256 KB RAM、2 MB 板载 QSPI Flash | 区分片上与板载 | [S2 §Specifications comparison][S5 p.5] |
| 无线 | Bluetooth LE、Bluetooth Mesh、NFC | Wiki 当前比较表写 BLE 5.4；最终功能依赖协议栈/BSP | [S2 §Specifications comparison] |
| 外部 IO | 11×数字/PWM、6×ADC | D0-D10；ADC 位于 D0-D5 | [S2 §Features/Hardware overview] |

### 机械与环境

板尺寸 21 mm × 17.8 mm，元件单面布置，可使用半孔进行贴装；量产时应以官方 DXF、底部焊盘文件和当前 KiCad 工程为准。[S2 §Features/Resources][S5 p.3]

## 3. 引脚与接线

| 板级引脚 | nRF52840 管脚 | 主要功能 | 接线提示 | 证据 |
| --- | --- | --- | --- | --- |
| 5V/VBUS | - | USB 5 V 电源输入/输出 | 只用于电源，不接 GPIO | [S2 §Hardware overview] |
| GND | - | 地 | 与所有外设共地 | [S2 §Hardware overview] |
| 3V3/3V3_OUT | - | 3.3 V 输出 | 外设总负载需留板耗与瞬态裕量 | [S2 §Hardware overview][S4 p.3] |
| D0/A0 | P0.02/AIN0 | GPIO、ADC | 3.3 V 域 | [S2 §Hardware overview] |
| D1/A1 | P0.03/AIN1 | GPIO、ADC | 3.3 V 域 | [S2 §Hardware overview] |
| D2/A2 | P0.28/AIN4 | GPIO、ADC | 3.3 V 域 | [S2 §Hardware overview] |
| D3/A3 | P0.29/AIN5 | GPIO、ADC | 3.3 V 域 | [S2 §Hardware overview] |
| D4/A4/SDA | P0.04/AIN2 | GPIO、ADC、I2C SDA | I2C 上拉到 3.3 V | [S2 §Hardware overview] |
| D5/A5/SCL | P0.05/AIN3 | GPIO、ADC、I2C SCL | I2C 上拉到 3.3 V | [S2 §Hardware overview] |
| D6/TX | P1.11 | GPIO、UART TX | Arduino 数字编号随 BSP 可能不同 | [S2 §Hardware overview] |
| D7/RX | P1.12 | GPIO、UART RX | Arduino 数字编号随 BSP 可能不同 | [S2 §Hardware overview] |
| D8/SCK | P1.13 | GPIO、SPI SCK | 使用 BSP 的 `SCK`/`D8` 符号 | [S2 §Hardware overview] |
| D9/MISO | P1.14 | GPIO、SPI MISO | 使用 BSP 的 `MISO`/`D9` 符号 | [S2 §Hardware overview] |
| D10/MOSI | P1.15 | GPIO、SPI MOSI | 使用 BSP 的 `MOSI`/`D10` 符号 | [S2 §Hardware overview] |
| NFC1/NFC2 | P0.09/P0.10 | NFC 天线 | 不等同普通单端天线 | [S2 §Hardware overview] |
| SWDIO/SWDCLK | 专用调试口 | SWD | 接 J-Link 时同时接 GND/参考电压 | [S2 §Access the SWD Pins] |

### 最小可用电路

开发调试只需可传数据的 USB-C 线；脱机运行可用 Seeed 指定的 3.7 V 锂电池接 BAT 焊盘。外设接 3V3 与 GND，I2C 上拉至 3.3 V；高浪涌负载使用独立电源并共地。[S2 §Hardware setup][S3 p.18]

### 禁止接法

- 禁止把 VBUS/5V 直接接到 GPIO/ADC。
- 禁止反接裸 BAT 焊盘或使用充电化学体系不明的电池。
- 禁止把 `3V3_OUT` 的稳压器额定值当作外设可独占电流。[S4 p.3]

## 4. 接口、协议与时序

### 接口概览

常规外设为 1×UART、1×I2C、1×SPI；USB 用于供电/下载/串口，NFC1/NFC2 用于 NFC 天线，SWD 用于调试和恢复。[S2 §Specifications comparison/Hardware overview]

### 初始化/上电时序

1. 选择与硬件匹配的板卡和 BSP。
2. 在连接外设前将其片选/使能引脚置于安全态。
3. 初始化 USB/串口后再初始化共享总线。
4. 低功耗应用在进入 System OFF 前关闭外设并让板载 QSPI Flash 进入 deep power-down。[S2 §Two Arduino Libraries/Power Consumption Verification]

### 数据/寄存器/命令格式

本板没有单一设备协议；UART/I2C/SPI 的模式和速率由所接外设决定。板载 RGB LED 为共阳极，`LOW` 点亮、`HIGH` 熄灭；使用 `LED_RED`、`LED_BLUE`、`LED_GREEN` 等板级符号。[S2 §Playing with the built-in 3-in-one LED]

### 事务状态机

电池 ADC：配置 P0.14 为仅下拉/输出低 → 等待分压稳定 → 读取 P0.31/AIN7 → 完成采样；充电时保持安全控制状态。若 ADC 值接近量程上限，立即停止并检查控制脚与分压网络。[S2 §FAQ Q3][S4 p.3]

## 5. 工作流程与驱动设计

### 平台无关流程

1. 通过 USB-C 数据线连接，安装 Seeed nRF52 板卡包。
2. 依据目标选择 BSP：BLE/低功耗优先 `Seeed nRF52 Boards`；Mbed/TinyML 工作流可选 `Seeed nRF52 mbed-enabled Boards`。
3. 选择精确板型和端口，先运行 Blink。
4. 用板级符号初始化引脚，再逐个接入外设。
5. 记录 BSP 名称、版本、Bootloader 和测试固件提交。[S2 §Two Arduino Libraries/Getting started]

### 驱动接口建议

封装 `board_init`、`battery_read_mv`、`enter_system_off`、`led_set` 和总线初始化接口。不要在业务代码中散落 P0.xx 和易随 BSP 改变的 Arduino 数字编号。

### 参考实现

原指南的低功耗示例会先发送 QSPI `0xB9` 深度掉电命令，再调用 `sd_power_system_off()`；该代码依赖 Adafruit/Bluefruit BSP，不应直接移植到 Mbed BSP。[S2 §Power Consumption Verification]

## 6. 验证方法

| 测试 | 前置条件 | 操作 | 期望结果 | 仪器/记录 | 状态 |
| --- | --- | --- | --- | --- | --- |
| USB/Bootloader | 数据 USB 线、正确 BSP | 上传 Blink；失败时双击 Reset | 出现端口，红色用户 LED 低有效闪烁 | Arduino 日志 | untested |
| 3.3 V 电源 | USB 5 V、限流 | 空载测 3V3，再逐步加负载 | 电压稳定，无异常温升 | 电源/万用表 | untested |
| 引脚映射 | 测试夹具 | 对 D0-D10 逐一翻转/采样 | 与表格和 BSP 符号一致 | 示波器/日志 | untested |
| 低功耗 | 无外接负载 | 运行官方 deep-sleep 流程 | 板级电流接近官方 <5 µA 目标 | µA 表/功耗分析仪 | untested |
| 电池 ADC | 受控 3.7 V 输入 | P0.14 输出低后采 P0.31 | ADC 不过量程，换算稳定 | 万用表/日志 | untested |

## 7. 故障排查

| 现象 | 高概率原因 | 检查方法 | 修复 |
| --- | --- | --- | --- |
| 无串口/上传卡住 | 线缆仅供电、端口错、应用占用 USB | 换数据线，单击/双击 Reset | 进入 Bootloader 后重传 |
| `Serial` 编译失败 | 使用非 Mbed BSP 且未引入 TinyUSB | 核对 BSP | 按官方说明加入 `Adafruit_TinyUSB.h` 或改用 Mbed BSP |
| LED 逻辑相反 | 共阳极、低有效 | 测 LED 管脚 | LOW=亮、HIGH=灭 |
| 休眠电流偏高 | QSPI/外设未关、调试器或 LED 仍连接 | 分层断开并测电流 | 关闭 QSPI、外设和调试接口 |
| 电池 ADC 异常高 | P0.14 控制错误 | 同时测 P0.14/P0.31 | 仅输出低启用读取并复核分压 |

## 8. 限制、安全与可靠性

- 充电器用于官方资料描述的 3.7 V 锂电池；无保护电芯、其他化学体系或反接保护能力未在本次资料中证明。[S3 p.18][S4 p.3]
- nRF52840、板载稳压器和充电芯片各自的绝对最大额定应从器件数据手册核对；板级 `5V`/`3V3` 标签不是 GPIO 容限。
- 无线性能取决于天线净空、外壳、地平面和发射功率；贴装母板不得在天线区域铺铜/放置金属，需按官方 PCB 资料复核。
- 量产前必须锁定 PCB/原理图修订、BSP、Bootloader、采购 SKU 和认证文件。

## 9. 来源与冲突处理

### 来源表

| ID | 等级 | 标题/发布者 | 版本/日期 | 定位信息 | 路径或 URL | 访问日期 | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | B | XIAO nRF52840 系列入门指南/Seeed（本地剪藏） | 2025-04-17；剪藏 2026-08-02 | 全文 | `C:\Users\19542\Documents\Obsidian Vault\Clippings\Seeed Studio XIAO nRF52840 系列入门指南.md`；SHA-256 `710CCC…025B` | 2026-08-02 | 输入快照 |
| S2 | B | Getting Started with XIAO nRF52840 Series/Seeed Wiki | 更新 2025-04-17 | Features、Hardware overview、FAQ、Resources | https://wiki.seeedstudio.com/XIAO_BLE/ | 2026-08-02 | 当前板级指南 |
| S3 | A | Seeed Studio XIAO Series SoM Datasheet | 当前在线版 | p.6-p.7、p.18-p.20 | https://files.seeedstudio.com/wiki/XIAO/Seeed-Studio-XIAO-Series-SOM-Datasheet.pdf | 2026-08-02 | 板级规格、功耗、电源 |
| S4 | A | XIAO nRF52840 Schematic/Seeed | Rev V1.1，2025-12-19 | p.3 | https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_PDF.pdf | 2026-08-02 | 电源、充电、信号网络 |
| S5 | A | XIAO nRF52840 Tape & Reel Industrial Product Datasheet/Seeed | 更新 2026-07-30 | p.4-p.5 | https://files.seeedstudio.com/Bazaar/product_pdf/101991463.pdf | 2026-08-02 | 通用板级规格；包装变体 |

### 冲突裁决

| 主题 | 采用值 | 其他说法 | 采用理由 | 影响/验证 |
| --- | --- | --- | --- | --- |
| 充电电流 | 默认按 50 mA ±5 mA 设计 | Wiki 与工业数据表写 50/100 mA 可选 | SoM 手册给出有公差的板级规格；Wiki 的文字与代码对 P0.13 模式表述不一致 | 未核对 BQ25101/ISET 网络前不切换到 100 mA |
| 3.3 V 可用电流 | 外设预算按小于 200 mA，并实测降额 | 稳压器标注 250 mA | 250 mA 是芯片额定，非板级可分配电流 | 量产前做最大负载、温升和无线发射瞬态测试 |
| BLE 版本 | 记录为 Bluetooth LE，项目按实际协议栈能力声明 | 不同官方资料写 Bluetooth 5.0 与 5.4 | 芯片能力、协议栈和认证版本不是同一概念 | 锁定 BSP/SoftDevice/Zephyr 版本后核对认证 |

## 10. 待确认项

| 优先级 | 问题 | 风险 | 获取证据/实测方法 | 负责人/状态 |
| --- | --- | --- | --- | --- |
| P0 | P0.13 选择 50/100 mA 的准确 GPIO 模式与适用板修订 | 过大充电电流可能损伤小容量电池 | 对照 BQ25101 数据手册、Rev V1.1 原理图和实板电流测试 | 未安排 |
| P1 | 具体采购板的 PCB 修订与 3V3 可用持续/峰值电流 | 外设负载导致复位或过热 | 记录丝印，做阶梯负载与无线峰值测试 | 未安排 |
| P2 | 当前项目采用的 BSP/Bootloader 版本 | 引脚编号、USB 与低功耗行为不同 | 固化构建清单和上传日志 | 未安排 |

