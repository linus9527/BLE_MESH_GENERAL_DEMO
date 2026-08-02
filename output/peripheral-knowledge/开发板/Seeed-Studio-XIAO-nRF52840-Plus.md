---
title: "Seeed Studio XIAO nRF52840 Plus"
aliases: ["XIAO nRF52840 Plus"]
category: "外设"
peripheral_type: "无线开发板"
manufacturer: "Seeed Studio"
model: "XIAO nRF52840 Plus"
variant: "Plus，D0-D19，无板载 IMU/麦克风"
module_or_component: "module"
interfaces: ["USB", "GPIO", "ADC", "UART", "I2C", "SPI", "I2S", "NFC", "SWD", "Bluetooth LE"]
knowledge_status: "needs-research"
hardware_validation: "untested"
source_grade: "B"
created: "2026-08-02"
updated: "2026-08-02"
tags: ["外设", "开发板", "Seeed", "nRF52840", "BLE", "Plus"]
---

# Seeed Studio XIAO nRF52840 Plus

## 0. 结论卡

> nRF52840 Plus 将编号引脚扩展到 D0-D19，并增加第二组 UART/SPI 与 I2S，适合板对板贴装和更多 IO 的项目；但当前 Wiki 对“18 GPIO”与“20 PWM/GPIO”存在冲突，电源/充电部分也未完成 Plus 原理图逐网核对，暂为 `needs-research`。[S2 §Features/Specifications comparison/Hardware overview][S3]

| 项目 | 结论 |
| --- | --- |
| 典型用途 | 多 IO BLE 节点、板对板贴装、带 I2S/双 SPI 的产品 |
| 关键接口 | D0-D19、2×UART、1×I2C、2×SPI、1×I2S、NFC、SWD |
| 供电/逻辑 | Wiki 标出 5V/VBUS、3V3_OUT、GND 与 BAT ADC 网络；精确额定需核对 Plus Rev V1.1 原理图 |
| 最重要限制 | 20 个编号引脚不等于每个都可同时作为独立 PWM/ADC；D14/D15 与 NFC 复用，D16 是电池 ADC |
| 主来源 | [S2] Seeed Wiki 当前引脚表、[S3] Seeed Plus 发布说明 |
| 硬件验证 | untested |

### 必看警告

- D14/P0.09 与 D15/P0.10 同时是 NFC1/NFC2；启用 NFC 时不能再当普通 UART/GPIO 使用。[S2 §XIAO nRF52840 Plus Pin Map]
- D16/P0.31 用于电池 ADC；读取前要正确控制 P0.14/READ_BAT_ENABLE。[S2 §Hardware overview/FAQ Q3]
- 未核对 Plus 原理图前，不把普通版的 3V3 可用电流、充电控制真值表或底部焊盘位置直接套用到 Plus。

## 1. 识别信息

| 项目 | 内容 | 证据 |
| --- | --- | --- |
| 制造商 | Seeed Studio | [S2 页面标题] |
| 完整型号 | Seeed Studio XIAO nRF52840 Plus | [S2 §Specifications comparison] |
| 变体/封装/板版本 | Plus；官方资源链接为 SCH/PCB v1.1 压缩包 | [S2 §Resources] |
| 裸器件或模块 | 可编程无线开发板/可贴装模块 | [S3] |
| 识别特征 | 保留两侧 7 针通孔，同时扩展为 23 个半孔焊盘；无 IMU/麦克风 | [S3][S2 §Specifications comparison] |

### 相近型号与兼容性

普通版只有 D0-D10；Sense Plus 在相同扩展形态上增加 IMU 和麦克风。Plus 的底部焊盘与旧版 XIAO 不应默认机械/电气兼容。[S3]

## 2. 关键参数

### 电气参数

| 参数 | 最小 | 典型 | 最大 | 单位 | 条件/备注 | 证据 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| 5V/VBUS |  | 5 |  | V | Wiki 标为 Power Input/Output；未作为 GPIO 使用 | [S2 §Plus Pin Map] |
| 3V3_OUT |  | 3.3 |  | V | 板级 3.3 V 输出；本次未确认 Plus 可用电流 | [S2 §Plus Pin Map] |
| BAT |  | 3.7 |  | V | 系列指南/相关官方应用使用 3.7 V 锂电；Plus 精确范围需原理图/充电器核验 | [S1 §规格对比/FAQ][S2 §FAQ Q3] |
| 低功耗目标 |  | <5 |  | µA | 系列特性宣称；Plus 的实测条件未在本次资料中单列 | [S2 §Features] |

### 性能参数

| 参数 | 数值/范围 | 条件 | 证据 |
| --- | --- | --- | --- |
| MCU/存储 | nRF52840 64 MHz；1 MB Flash、256 KB RAM、2 MB 板载 Flash | 与系列比较表一致 | [S2 §Specifications comparison] |
| 编号 IO | D0-D19，共 20 个编号引脚 | 部分引脚兼作 NFC/BAT；能力不完全相同 | [S2 §Plus Pin Map][S3] |
| 总线 | 2×UART、1×I2C、2×SPI、1×I2S | 以 Wiki 规格表与引脚表为准 | [S2 §Specifications comparison/Plus Pin Map] |
| ADC | 6 个主模拟输入；D16 是电池 AIN7 | Wiki 表写“20/6 PWM/模拟引脚” | [S2 §Specifications comparison/Plus Pin Map] |

### 机械与环境

官方发布说明称 Plus 有 23 个半孔焊盘（20 GPIO + 3 电源），两侧原有 7 针通孔位置保留；精确外形、天线净空和底部焊盘需从 Plus KiCad/DXF 核对。[S3]

## 3. 引脚与接线

| 板级引脚 | nRF52840 管脚 | 主要功能 | 复用/限制 | 证据 |
| --- | --- | --- | --- | --- |
| D0-D5 | P0.02、P0.03、P0.28、P0.29、P0.04、P0.05 | GPIO、6×ADC；D4/D5 为 I2C | 3.3 V 域 | [S2 §Plus Pin Map] |
| D6/D7 | P1.11/P1.12 | UART0 TX/RX、GPIO | Arduino 数字编号随 BSP 变化 | [S2 §Plus Pin Map] |
| D8-D10 | P1.13/P1.14/P1.15 | SPI0 SCK/MISO/MOSI、GPIO | 使用板级符号 | [S2 §Plus Pin Map] |
| D11-D13 | P0.15/P0.19/P1.01 | I2S SD/SCK/WS、GPIO | Wiki 另标 ADC，但需按 nRF SAADC 通道复核 | [S2 §Plus Pin Map] |
| D14/D15 | P0.09/P0.10 | UART1 RX/TX、GPIO | 与 NFC1/NFC2 复用 | [S2 §Plus Pin Map] |
| D16 | P0.31/AIN7 | 电池电压 ADC | 配合 P0.14 使能；不作为普通随意输入 | [S2 §Plus Pin Map/FAQ Q3] |
| D17-D19 | P1.03/P1.05/P1.07 | SPI1 SCK/MISO/MOSI、GPIO | 第二组 SPI | [S2 §Plus Pin Map] |
| 5V/GND/3V3 | - | 电源 | 不把 5 V 接 GPIO | [S2 §Plus Pin Map] |

### 最小可用电路

USB-C 供电/下载；外设接 3V3 与 GND。按所选 BSP 使用 D0-D19 板级符号；I2C 上拉到 3.3 V，NFC 与 UART1 二选一规划。[S2 §Getting started/Plus Pin Map]

### 禁止接法

- 禁止同时把 D14/D15 接 UART 外设又连接 NFC 天线。
- 禁止把 D16 当作普通 5 V 容忍输入。
- 禁止按旧 XIAO 底部焊盘图制作 Plus 母板。[S2 §Plus Pin Map][S3]

## 4. 接口、协议与时序

### 接口概览

Plus 比普通版新增 I2S（D11-D13）、UART1（D14/D15）和 SPI1（D17-D19）；UART1 与 NFC 复用，D16 专用于电池 ADC。[S2 §Plus Pin Map]

### 初始化/上电时序

上电先保持所有新增总线片选/使能为安全态，再初始化 BSP；确认未启用 NFC 后才使用 UART1。进入低功耗前停止两组 SPI/I2S、关闭外设并处理板载 QSPI。[S2 §Two Arduino Libraries/Power Consumption Verification]

### 数据/寄存器/命令格式

板卡无单一协议。I2S 的角色、时钟和位宽由音频外设决定；SPI 模式、频率和片选时序由从设备决定。引脚表只说明复用能力，不等于默认 Arduino 实例已绑定。

### 事务状态机

启动时读取构建期板型/BSP → 配置电源安全态 → 初始化所需复用功能 → 做回环/设备 ID 验证 → 才启用业务。对 UART1/NFC 等互斥复用在构建期阻止同时开启。[S2 §Plus Pin Map]

## 5. 工作流程与驱动设计

### 平台无关流程

1. 确认实板为 Plus，而不是普通版或 Sense Plus。
2. 安装支持 Plus 的 Seeed nRF52 BSP 版本并选择精确板型。
3. 先验证 D0-D10，再逐组验证 D11-D19。
4. 用逻辑分析仪做 I2S/UART1/SPI1 回环或已知外设测试。
5. 固化引脚复用矩阵，避免运行期冲突。[S2 §Getting started/Hardware overview]

### 驱动接口建议

建立唯一的 `board_pins` 层，业务只引用 `SPI1_SCK`、`I2S_WS` 等语义名。为 NFC/UART1 建立互斥配置检查，为 D16 建立专用电池读取 API。

### 参考实现

当前输入未提供独立 Plus 示例；普通版 Blink 可用于基础验证，但新增总线必须以所选 BSP 的 variant 文件和实测为准。[S1 §软件设置][S2 §Plus Pin Map]

## 6. 验证方法

| 测试 | 前置条件 | 操作 | 期望结果 | 仪器/记录 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 板型识别 | 实板和 BSP | 上传 Blink/打印宏 | 正确识别 Plus | 编译日志 | untested |
| D0-D19 | 测试夹具 | 逐脚输入/输出测试 | 与官方管脚表一致 | 示波器/日志 | untested |
| UART1/NFC | 分别接回环/NFC 天线 | 独立启用 | 单项功能正常，互斥生效 | 日志 | untested |
| SPI1/I2S | 已知外设/逻辑分析仪 | 发送固定模式 | 管脚、极性、频率正确 | 波形 | untested |
| 电源与休眠 | 限流供电 | 阶梯负载并进入休眠 | 电压稳定；记录实际最低电流 | 功耗分析仪 | untested |

## 7. 故障排查

| 现象 | 高概率原因 | 检查方法 | 修复 |
| --- | --- | --- | --- |
| D11-D19 无输出 | 选错普通版 BSP/variant | 检查编译宏和 pin map | 升级并选择 Plus 板型 |
| UART1/NFC 异常 | 复用冲突 | 查看 P0.09/P0.10 配置 | 只启用一个功能 |
| 第二 SPI 无响应 | 实例未绑定或片选错误 | 看 SCK/MOSI/CS 波形 | 显式构造总线并核对引脚 |
| 电池值异常 | P0.14/P0.31 控制错误 | 测两个管脚 | 使用专用读取流程 |

## 8. 限制、安全与可靠性

- 当前资料不足以给出 Plus 版 3V3 可用持续/峰值电流、充电真值表和完整绝对最大额定。
- D11-D19 中的 ADC/PWM 描述需要以 nRF52840 通道能力与 BSP 实现共同确认，不能只看营销表。
- 贴装母板必须使用 Plus v1.1 开源设计核对焊盘、天线净空和热/机械公差。[S2 §Resources][S3]

## 9. 来源与冲突处理

### 来源表

| ID | 等级 | 标题/发布者 | 版本/日期 | 定位信息 | 路径或 URL | 访问日期 | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | B | XIAO nRF52840 系列入门指南/Seeed（本地剪藏） | 2025-04-17；剪藏 2026-08-02 | 全文 | `C:\Users\19542\Documents\Obsidian Vault\Clippings\Seeed Studio XIAO nRF52840 系列入门指南.md`；SHA-256 `710CCC…025B` | 2026-08-02 | 输入快照 |
| S2 | B | Getting Started with XIAO nRF52840 Series/Seeed Wiki | 更新 2025-04-17 | Features、Specifications、Plus Pin Map、Resources | https://wiki.seeedstudio.com/XIAO_BLE/ | 2026-08-02 | 当前引脚与系列说明 |
| S3 | B | Seeed Studio XIAO Plus, More Castellation IOs for SMD Soldering/Seeed | 2025-01-02 | 全文 | https://www.seeedstudio.com/blog/2025/01/02/seeed-studio-xiao-plus-more-castellation-ios-for-smd-soldering/ | 2026-08-02 | Plus 机械/焊盘设计说明 |

### 冲突裁决

| 主题 | 采用值 | 其他说法 | 采用理由 | 影响/验证 |
| --- | --- | --- | --- | --- |
| Plus GPIO 数 | 20 个编号引脚 D0-D19；逐脚记录能力 | Wiki 特性段写 18×GPIO(PWM)，规格表写 20/6 | 详细 Pin Map 和官方发布说明均明确 D0-D19/20 GPIO | 不宣称 20 路均可同时 PWM；以 BSP/实测验证 |
| D11-D13 ADC | 只记录 Wiki 标注，未作为 6 个主 ADC 之外的保证通道 | Plus Pin Map 描述 GPIO/I2S/ADC，规格表仍写 6 ADC | nRF52840 ADC 通道与板级映射需核对 | 驱动中暂不把 D11-D13 暴露为通用 ADC |
| 电源/充电 | 仅采用 5V、3V3 标签和 3.7 V 系列用法 | 普通版资料给出更详细电流 | 不能跨 PCB 修订直接套用 | 完成 Plus 原理图网表核对后升级状态 |

## 10. 待确认项

| 优先级 | 问题 | 风险 | 获取证据/实测方法 | 负责人/状态 |
| --- | --- | --- | --- | --- |
| P0 | Plus Rev V1.1 的电源路径、3V3 额定与充电电流控制 | 过载、过热或电池风险 | 下载官方 SCH/PCB v1.1，逐网核对并实测 | 未安排 |
| P1 | 官方当前 BSP 对 D11-D19 的 Arduino 名称、PWM/ADC 能力 | 固件引脚错误 | 审查 variant 文件并做夹具测试 | 未安排 |
| P1 | Plus 精确尺寸、底部焊盘与天线禁布区 | 母板无法装配/射频性能下降 | 提取官方 KiCad/DXF 并做 DFM | 未安排 |

