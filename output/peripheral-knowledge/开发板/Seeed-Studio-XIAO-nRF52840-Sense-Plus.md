---
title: "Seeed Studio XIAO nRF52840 Sense Plus"
aliases: ["XIAO nRF52840 Sense Plus"]
category: "外设"
peripheral_type: "无线传感开发板"
manufacturer: "Seeed Studio"
model: "XIAO nRF52840 Sense Plus"
variant: "Sense Plus，D0-D19，板载 IMU 与 PDM 麦克风"
module_or_component: "module"
interfaces: ["USB", "GPIO", "ADC", "UART", "I2C", "SPI", "I2S", "NFC", "SWD", "Bluetooth LE", "PDM"]
knowledge_status: "needs-research"
hardware_validation: "untested"
source_grade: "B"
created: "2026-08-02"
updated: "2026-08-02"
tags: ["外设", "开发板", "Seeed", "nRF52840", "BLE", "Plus", "IMU", "PDM"]
---

# Seeed Studio XIAO nRF52840 Sense Plus

## 0. 结论卡

> 将 Plus 的 D0-D19 扩展引脚与 Sense 的 LSM6DS3TR-C IMU、PDM 麦克风组合在一块板上；适合多传感 TinyML，但外部 I2S 与板载 PDM、共享 I2C、NFC/UART1 和电池 ADC 的资源冲突必须统一规划。[S2 §Specifications comparison/Sense Plus Pin Map]

| 项目 | 结论 |
| --- | --- |
| 典型用途 | 多传感 BLE、动作/音频 TinyML、板对板贴装 |
| 关键接口 | D0-D19、双 UART/SPI、I2S、内部 IMU/PDM、NFC、SWD |
| 供电/逻辑 | Wiki 标出 5V/VBUS、3V3_OUT 与电池 ADC；Plus 精确额定待原理图核验 |
| 最重要限制 | 内部传感器占用专用 GPIO；D14/D15 与 NFC 复用；D16 为电池 ADC |
| 主来源 | [S2] Seeed Wiki 当前 Sense Plus 引脚表、[S3] Plus 发布说明 |
| 硬件验证 | untested |

### 必看警告

- `P1.08/P0.11` 属于 IMU，`P0.16/P1.00` 属于 PDM 麦克风，不得按普通空闲 GPIO 重配。[S2 §Sense Plus Pin Map]
- D14/D15 与 NFC 复用；D16/P0.31 用于电池 ADC，需 P0.14 正确使能。[S2 §Sense Plus Pin Map/FAQ Q3]
- Wiki 的 Plus GPIO 数量和充电电流控制描述内部不一致，状态保持 `needs-research`。

## 1. 识别信息

| 项目 | 内容 | 证据 |
| --- | --- | --- |
| 制造商 | Seeed Studio | [S2 页面标题] |
| 完整型号 | Seeed Studio XIAO nRF52840 Sense Plus | [S2 §Specifications comparison] |
| 变体/封装/板版本 | Sense Plus；官方提供 (Sense) Plus SCH/PCB v1.1 | [S2 §Resources] |
| 裸器件或模块 | 带 MCU、天线、存储、充电、IMU 与麦克风的开发板/模块 | [S2 §Features] |
| 识别特征 | Plus 扩展半孔焊盘 + 板载 IMU/PDM 麦克风 | [S2 §Hardware overview][S3] |

### 相近型号与兼容性

与 Plus 相比增加板载传感器及内部管脚占用；与旧 Sense 相比增加 D11-D19 和新焊盘布局。不能仅凭 XIAO 主排针相似互换母板或固件板型。

## 2. 关键参数

### 电气参数

| 参数 | 最小 | 典型 | 最大 | 单位 | 条件/备注 | 证据 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| 5V/VBUS |  | 5 |  | V | Power Input/Output 标签 | [S2 §Sense Plus Pin Map] |
| 3V3_OUT |  | 3.3 |  | V | 系统逻辑域；本次未确认可用电流 | [S2 §Sense Plus Pin Map] |
| BAT |  | 3.7 |  | V | 系列指南描述的锂电标称值；Plus 精确范围待核对 | [S1 §规格对比/FAQ] |
| 低功耗目标 |  | <5 |  | µA | 系列特性；Sense Plus 各传感器关断条件需实测 | [S2 §Features] |

### 性能参数

| 参数 | 数值/范围 | 条件 | 证据 |
| --- | --- | --- | --- |
| MCU/存储 | nRF52840 64 MHz；1 MB Flash、256 KB RAM、2 MB 板载 Flash | 系列共同项 | [S2 §Specifications comparison] |
| 编号 IO | D0-D19 | 能力不完全相同；有 NFC/BAT 复用 | [S2 §Sense Plus Pin Map] |
| 外部总线 | 2×UART、1×I2C、2×SPI、1×I2S | 以 BSP variant 与实测确认实例 | [S2 §Specifications comparison] |
| 传感器 | LSM6DS3TR-C 六轴 IMU、PDM 麦克风 | 板载 | [S2 §Features/Specifications comparison] |

### 机械与环境

Plus 系列具有 23 个半孔焊盘（官方发布称 20 GPIO + 3 电源）并保留两侧通孔；精确尺寸、传感器方向、麦克风声孔与天线禁布区需从当前 KiCad/DXF 提取。[S3]

## 3. 引脚与接线

外部 D0-D19 映射与 [[Seeed-Studio-XIAO-nRF52840-Plus]] 相同。[S2 §Sense Plus Pin Map]

| 内部/复用功能 | nRF52840 管脚 | 作用 | 限制 | 证据 |
| --- | --- | --- | --- | --- |
| IMU_PWR | P1.08 | IMU 电源开关 | 低功耗流程必须控制 | [S2 §Sense Plus Pin Map] |
| IMU_INT1 | P0.11 | IMU 中断 | 不能当普通 GPIO | [S2 §Sense Plus Pin Map] |
| 内部 I2C | P0.04/P0.05 网络 | IMU SDA/SCL | 与 D4/D5 外部 I2C 共享 | [S1 §资源/原理图说明] |
| PDM_DATA/PDM_CLK | P0.16/P1.00 | 麦克风数据/时钟 | 与外部 D11-D13 的 I2S 不是同一接口 | [S2 §Sense Plus Pin Map] |
| D14/D15 | P0.09/P0.10 | UART1 或 NFC1/NFC2 | 功能互斥 | [S2 §Sense Plus Pin Map] |
| D16 | P0.31/AIN7 | 电池 ADC | 配合 P0.14 使用 | [S2 §Sense Plus Pin Map/FAQ Q3] |

### 最小可用电路

USB-C 进行下载和基础验证；外设接 3V3/GND。外接 I2C 器件必须核对板载 IMU 地址/上拉；音频扩展需区分板载 PDM 与 D11-D13 I2S。[S2 §Getting started/Sense Plus Pin Map]

### 禁止接法

- 禁止把外部 I2C 上拉到 5 V。
- 禁止同时启用 D14/D15 的 NFC 与 UART1。
- 禁止覆盖 IMU/PDM 内部管脚配置。
- 禁止按旧 Sense 底部焊盘图制作 Sense Plus 母板。[S2 §Sense Plus Pin Map][S3]

## 4. 接口、协议与时序

### 接口概览

外部支持 UART0/I2C/SPI0、I2S、UART1、SPI1；内部 IMU 使用共享 I2C 和中断，麦克风使用 PDM。D14/D15 复用 NFC，D16 是电池 ADC。[S2 §Sense Plus Pin Map]

### 初始化/上电时序

安全初始化外部片选 → 初始化内部 I2C/IMU 电源 → 等待 IMU 就绪 → 配置 PDM 缓冲 → 初始化 BLE/外部总线。进入低功耗前反向停止事务并关闭传感器、外设与 QSPI。[S2 §Two Arduino Libraries/Power Consumption Verification]

### 数据/寄存器/命令格式

IMU、PDM 与外部 I2S/SPI 的格式分别由器件和 BSP 定义；本文不把它们合并成一个“音频/传感器协议”。驱动必须保存采样率、量程、坐标系和时间戳。

### 事务状态机

建立统一资源管理器：申请引脚复用/总线 → 上电传感器 → 配置 → DMA/中断采样 → 检查缓冲/超时 → 停止 → 下电。构建时阻止 NFC/UART1 和其他互斥组合。[S2 §Sense Plus Pin Map]

## 5. 工作流程与驱动设计

### 平台无关流程

1. 锁定支持 Sense Plus 的 BSP 和板型。
2. 依次验证 Blink、D0-D19、IMU、PDM、BLE。
3. 再验证第二 SPI/UART 与外部 I2S。
4. 最后进行并发压力和低功耗测试。
5. 保存板修订、BSP/Bootloader 和原始波形/功耗记录。[S2 §Getting started]

### 驱动接口建议

把板级资源集中到 `board_support`，分别提供 IMU、PDM、I2S、两组 SPI/UART 和 battery API；每个 API 返回占用冲突和超时错误，不在业务层硬编码 P0.xx。

### 参考实现

普通 Sense 的传感器示例可作为逻辑参考，Plus 新增引脚必须以 Sense Plus BSP variant 为准。低功耗示例需同时验证 IMU、麦克风、外部 QSPI 和扩展外设都已关断。[S1 §两个 Arduino 库/功耗验证]

## 6. 验证方法

| 测试 | 前置条件 | 操作 | 期望结果 | 仪器/记录 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 板型/引脚 | 正确 BSP | 对 D0-D19 逐脚测试 | 映射与 variant 一致 | 夹具/日志 | untested |
| IMU | 板水平、坐标标记 | 静态和逐轴翻转 | 重力/方向一致 | CSV | untested |
| PDM | 安静与定频声源 | 录音 | 无饱和、频谱正确 | WAV/频谱 | untested |
| 多总线 | SPI0/SPI1/UART0/UART1/I2S 夹具 | 分别及并发运行 | 无复用冲突/丢样 | 逻辑分析仪 | untested |
| 低功耗 | 所有外设可关 | 进入 System OFF | 记录实际电流与唤醒源 | 功耗分析仪 | untested |

## 7. 故障排查

| 现象 | 高概率原因 | 检查方法 | 修复 |
| --- | --- | --- | --- |
| 新增引脚不可用 | BSP 仍选普通 Sense | 打印板型宏 | 选择 Sense Plus variant |
| IMU/I2C 冲突 | 地址/上拉或电源控制错误 | 扫描总线、测 P1.08 | 调整外设地址/上拉与初始化顺序 |
| PDM/I2S 混淆 | 把板载 PDM 当成 D11-D13 I2S | 查看 P1.00/P0.16 与 D11-D13 波形 | 分离驱动实例 |
| 休眠电流高 | 任一传感器/外设/QSPI 未关闭 | 分层关断 | 完整资源回收 |

## 8. 限制、安全与可靠性

- 当前资料不足以确定 Sense Plus 的完整电源额定、充电控制真值表、传感器坐标和所有新增 GPIO 的 PWM/ADC 支持。
- 麦克风声孔、IMU 安装方向和天线净空均会受母板/外壳影响，量产前必须做声学、惯性与射频验证。
- 多总线并发会增加功耗、DMA/中断竞争与无线时延，不能只验证单个示例。[S2 §Resources][S3]

## 9. 来源与冲突处理

### 来源表

| ID | 等级 | 标题/发布者 | 版本/日期 | 定位信息 | 路径或 URL | 访问日期 | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | B | XIAO nRF52840 系列入门指南/Seeed（本地剪藏） | 2025-04-17；剪藏 2026-08-02 | 全文 | `C:\Users\19542\Documents\Obsidian Vault\Clippings\Seeed Studio XIAO nRF52840 系列入门指南.md`；SHA-256 `710CCC…025B` | 2026-08-02 | 输入快照 |
| S2 | B | Getting Started with XIAO nRF52840 Series/Seeed Wiki | 更新 2025-04-17 | Features、Specifications、Sense Plus Pin Map、Resources | https://wiki.seeedstudio.com/XIAO_BLE/ | 2026-08-02 | 当前引脚与传感器说明 |
| S3 | B | Seeed Studio XIAO Plus, More Castellation IOs for SMD Soldering/Seeed | 2025-01-02 | 全文 | https://www.seeedstudio.com/blog/2025/01/02/seeed-studio-xiao-plus-more-castellation-ios-for-smd-soldering/ | 2026-08-02 | Plus 机械/焊盘设计说明 |

### 冲突裁决

| 主题 | 采用值 | 其他说法 | 采用理由 | 影响/验证 |
| --- | --- | --- | --- | --- |
| Plus GPIO 数 | 20 个编号引脚 D0-D19，逐脚记录能力 | Wiki 特性段写 18×GPIO(PWM) | 详细 Pin Map 与发布说明明确 D0-D19/20 GPIO | 不宣称所有引脚等价；按 BSP/实测确认 |
| I2S 与 PDM | 作为两个独立外设接口处理 | 概览可能统称音频接口 | 引脚表分别给出 D11-D13 I2S 与内部 P0.16/P1.00 PDM | 驱动、DMA 和时钟分别配置 |
| 普通 Sense 资料复用 | 只复用 MCU/传感器逻辑，不复用焊盘/额定 | 系列 Wiki 将多个变体放在一页 | PCB 形态和新增引脚明显不同 | 完成 Sense Plus v1.1 原理图/BOM 抽取 |

## 10. 待确认项

| 优先级 | 问题 | 风险 | 获取证据/实测方法 | 负责人/状态 |
| --- | --- | --- | --- | --- |
| P0 | Sense Plus Rev V1.1 电源、充电和电池 ADC 网络 | 电池/ADC 损坏风险 | 提取官方 SCH/PCB 并实测充电/分压 | 未安排 |
| P1 | Sense Plus BSP variant 的 D11-D19、内部传感器和 DMA 绑定 | 引脚冲突或丢样 | 审查 variant/库源码并跑夹具 | 未安排 |
| P1 | IMU 坐标、麦克风方向、天线/声孔禁布区 | 数据错误或性能下降 | 核对 PCB/BOM/3D 并做样机测试 | 未安排 |

