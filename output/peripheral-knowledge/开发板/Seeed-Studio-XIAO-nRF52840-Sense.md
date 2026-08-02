---
title: "Seeed Studio XIAO nRF52840 Sense"
aliases: ["XIAO nRF52840 Sense", "XIAO BLE Sense"]
category: "外设"
peripheral_type: "无线传感开发板"
manufacturer: "Seeed Studio"
model: "XIAO nRF52840 Sense"
variant: "Sense，板载 LSM6DS3TR-C 与 PDM 麦克风"
module_or_component: "module"
interfaces: ["USB", "GPIO", "ADC", "UART", "I2C", "SPI", "NFC", "SWD", "Bluetooth LE", "PDM"]
knowledge_status: "conditional"
hardware_validation: "untested"
source_grade: "A"
created: "2026-08-02"
updated: "2026-08-02"
tags: ["外设", "开发板", "Seeed", "nRF52840", "BLE", "IMU", "PDM"]
---

# Seeed Studio XIAO nRF52840 Sense

## 0. 结论卡

> 在标准 XIAO nRF52840 上增加 LSM6DS3TR-C 六轴 IMU 与 PDM 数字麦克风，适合运动/音频 TinyML；主排针仍为 D0-D10，但板载传感器占用内部 GPIO/I2C 资源。[S2 §Features/Hardware overview][S3 p.6-p.7]

| 项目 | 结论 |
| --- | --- |
| 典型用途 | 动作识别、姿态、音频采集、BLE TinyML |
| 关键接口 | 标准版 D0-D10 + 内部 IMU I2C/中断 + PDM CLK/DATA |
| 供电/逻辑 | USB-C 5 V；BAT 标称 3.7 V；系统与 GPIO 3.3 V |
| 最重要限制 | 板载传感器占用 P1.08、P0.11、P0.16、P1.00 与内部 I2C；不要重用 |
| 主来源 | [S2] Seeed Wiki、[S3] SoM Datasheet、[S4] 官方原理图 |
| 硬件验证 | untested |

### 必看警告

- 读取电池前将 `P0.14/READ_BAT_ENABLE` 配置为输出低；错误置高可能使 `P0.31/AIN7_BAT` 接近 3.6 V 输入上限。[S2 §FAQ Q3]
- 不要把 `P1.08`、`P0.11`、`P0.16`、`P1.00` 当作空闲 GPIO；它们分别参与 IMU 供电/中断和 PDM 数据/时钟。[S2 §Hardware overview]
- RGB LED 低有效；传感器库/BSP 选择会影响现成示例和引脚符号。[S2 §Two Arduino Libraries/Playing with LED]

## 1. 识别信息

| 项目 | 内容 | 证据 |
| --- | --- | --- |
| 制造商 | Seeed Studio | [S2 页面标题] |
| 完整型号 | Seeed Studio XIAO nRF52840 Sense | [S2 §Specifications comparison] |
| 变体/封装/板版本 | Sense；原理图 Rev V1.1，2025-12-19 | [S4 p.1-p.2] |
| 裸器件或模块 | 带 MCU、天线、Flash、充电与传感器的开发板/模块 | [S3 p.6-p.7][S4 p.3] |
| 识别特征 | 标准 XIAO 外形，板载 LSM6DS3TR-C 与 MSM261D3526H1CPM PDM 麦克风 | [S2 §Features][S4 p.3] |

### 相近型号与兼容性

普通版没有 IMU/麦克风；Sense Plus 另有 D11-D19 扩展焊盘。主排针相似不代表底部焊盘、内部占用或固件板型可互换。[S2 §Specifications comparison]

## 2. 关键参数

### 电气参数

| 参数 | 最小 | 典型 | 最大 | 单位 | 条件/备注 | 证据 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| USB/VBUS 输入 |  | 5 |  | V | Type-C | [S3 p.18 §4.1.4] |
| BAT 输入 |  | 3.7 |  | V | 锂离子电池标称值 | [S3 p.18 §4.1.4] |
| 系统/GPIO 电压域 |  | 3.3 |  | V | 系统电源网络 | [S4 p.3] |
| 正常功耗 |  | 10.38 |  | mA | SoM 手册给出的板级值，未分解传感器工作状态 | [S3 p.18 §4.1.3] |
| 低功耗模式 |  | <5 |  | µA | 需关闭 QSPI、IMU/麦克风及其他负载 | [S3 p.18 §4.1.3][S2 §Power Consumption Verification] |
| 充电电流 | 45 | 50 | 55 | mA | 默认规格；官方 Wiki 另有 50/100 mA 可选说法 | [S3 p.18 §4.1.5][S2 §Battery Charging current] |

### 性能参数

| 参数 | 数值/范围 | 条件 | 证据 |
| --- | --- | --- | --- |
| MCU | nRF52840 Cortex-M4F，最高 64 MHz | 1 MB Flash、256 KB RAM | [S2 §Specifications comparison] |
| 板载存储 | 2 MB QSPI Flash | 低功耗前需单独进入 deep power-down | [S2 §Features/Power Consumption Verification] |
| IMU | LSM6DS3TR-C 六轴 | 具体量程/ODR/电流见器件笔记 | [S2 §Features][S4 p.3] |
| 麦克风 | MSM261D3526H1CPM PDM 数字麦克风 | 具体时钟/采样率见器件笔记 | [S2 §Resources][S4 p.3] |

### 机械与环境

尺寸 21 mm × 17.8 mm，元件单面布置；音频和惯性性能会受外壳声孔、机械耦合、振动和安装方向影响。[S2 §Features][S3 p.6]

## 3. 引脚与接线

外部 D0-D10、5V、3V3、GND、NFC 和 SWD 映射与 [[Seeed-Studio-XIAO-nRF52840]] 相同。[S2 §Hardware overview]

| 内部功能 | nRF52840 管脚 | 方向/作用 | 设计要求 | 证据 |
| --- | --- | --- | --- | --- |
| IMU_PWR | P1.08 | IMU 电源开关 | 低功耗时按库/原理图安全关闭 | [S2 §Hardware overview] |
| IMU_INT1 | P0.11 | IMU 中断输入 | 配置正确边沿与上拉 | [S2 §Hardware overview] |
| 内部 I2C SDA/SCL | P0.04/P0.05 网络 | IMU 通信 | 与外部 D4/D5 共享总线，避免地址/上拉冲突 | [S4 p.3] |
| PDM_DATA | P0.16 | 麦克风数据输入 | 不作为普通 GPIO | [S2 §Hardware overview] |
| PDM_CLK | P1.00 | 麦克风时钟输出 | 不作为普通 GPIO | [S2 §Hardware overview] |
| READ_BAT_ENABLE | P0.14 | 电池分压使能 | 仅输出低/下拉启用 | [S2 §FAQ Q3][S4 p.3] |
| AIN7_BAT | P0.31/AIN7 | 电池 ADC | 先启用安全分压再采样 | [S2 §FAQ Q3] |

### 最小可用电路

基础开发只需 USB-C 数据线。接外部 I2C 器件时先核对与板载 IMU 的地址、上拉和总线速率；电池使用符合 Seeed 板级充电器要求的 3.7 V 锂电池。[S2 §Hardware setup][S3 p.18]

### 禁止接法

- 不得驱动板载传感器占用的内部管脚为相反电平。
- 不得让外部 I2C 上拉到 5 V。
- 不得在电池充电/采样时错误置高 P0.14。[S2 §FAQ Q3][S4 p.3]

## 4. 接口、协议与时序

### 接口概览

外部为 UART/I2C/SPI/NFC/SWD；IMU 通过内部 I2C 与中断连接，麦克风使用 PDM CLK/DATA。两个 Arduino BSP 对 BLE/低功耗与 Mbed/IMU/PDM 支持侧重点不同。[S2 §Two Arduino Libraries]

### 初始化/上电时序

先初始化 3.3 V 与内部总线，再按传感器库控制 IMU 电源并等待启动；PDM 外设在缓冲区就绪后再开时钟。进入 System OFF 前停止采样、关闭 IMU/麦克风、让 QSPI 深度掉电。[S2 §Power Consumption Verification][S4 p.3]

### 数据/寄存器/命令格式

IMU 寄存器、量程、ODR 和中断格式属于 LSM6DS3TR-C；PDM 的时钟与抽取属于 MCU/PDM 驱动。本文只记录板级连线，不复制器件数据手册。[S2 §Resources]

### 事务状态机

传感采集：打开相应电源/时钟 → 等待器件就绪 → 配置量程/采样率 → DMA/中断采集 → 检查溢出 → 停止并释放资源。低功耗切换必须确保没有正在进行的 I2C/PDM 事务。

## 5. 工作流程与驱动设计

### 平台无关流程

1. 安装 Seeed nRF52 BSP 并选择精确的 Sense 板型。
2. 先运行 Blink/串口，再分别验证 IMU 与 PDM 官方示例。
3. 固定坐标系、采样率、量程、音频增益和缓冲区大小。
4. 最后集成 BLE；记录丢包、调度延迟和功耗。
5. 进入休眠前显式关闭传感器与 QSPI。[S2 §Two Arduino Libraries/Getting started]

### 驱动接口建议

分离 `board_power`、`imu`、`microphone`、`battery` 与 `ble` 模块；用事件/环形缓冲而非长阻塞循环。将坐标系和采样率写入数据元信息。

### 参考实现

优先使用与所选 BSP 同源的官方 IMU/PDM 示例；不要把非 Mbed BSP 的 Bluefruit 低功耗代码与 Mbed 传感代码直接混编。[S2 §Two Arduino Libraries]

## 6. 验证方法

| 测试 | 前置条件 | 操作 | 期望结果 | 仪器/记录 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 基础板卡 | 数据 USB 线 | 上传 Blink、打开串口 | LED 低有效，串口稳定 | Arduino 日志 | untested |
| IMU 静态 | 板水平静置 | 读取三轴加速度/角速度 | 重力轴约 1 g，静态角速度接近零 | 原始 CSV | untested |
| IMU 方向 | 标记板坐标 | 逐轴翻转 | 符号与坐标定义一致 | CSV/图表 | untested |
| PDM | 安静与定频声源 | 采集 PCM | 无饱和，频谱出现目标峰 | WAV/频谱 | untested |
| 并发 | BLE + IMU + PDM | 持续运行并统计 | 无缓冲溢出/断连 | 日志 | untested |
| 低功耗 | 停止所有传感器 | 进入 System OFF | 接近官方 <5 µA 目标 | 功耗分析仪 | untested |

## 7. 故障排查

| 现象 | 高概率原因 | 检查方法 | 修复 |
| --- | --- | --- | --- |
| IMU 无响应 | 选错板型/BSP、IMU 电源未开、I2C 冲突 | 扫描内部总线、测 P1.08 | 使用 Sense 板型和匹配库 |
| PDM 全零/噪声 | PDM 管脚/时钟或缓冲配置错误 | 看 P1.00 时钟和 P0.16 数据 | 使用官方示例基线 |
| BLE + 音频丢样 | 调度/缓冲不足 | 统计溢出和连接事件延迟 | DMA、双缓冲、降低采样/发送率 |
| 休眠电流偏高 | IMU、麦克风、QSPI 或调试器未关 | 分项关断测电流 | 完整停机顺序 |

## 8. 限制、安全与可靠性

- 麦克风声孔不能被胶、外壳或防水膜随意遮挡；安装结构需要声学验证。
- IMU 坐标轴、零偏、温漂与安装应力需要整机标定，不能只依赖芯片典型值。
- 外部 I2C 与板载 IMU 共享网络时要核对总上拉和地址。
- 电池、GPIO、天线与量产限制同 [[Seeed-Studio-XIAO-nRF52840]]。[S2 §Resources][S4 p.3]

## 9. 来源与冲突处理

### 来源表

| ID | 等级 | 标题/发布者 | 版本/日期 | 定位信息 | 路径或 URL | 访问日期 | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | B | XIAO nRF52840 系列入门指南/Seeed（本地剪藏） | 2025-04-17；剪藏 2026-08-02 | 全文 | `C:\Users\19542\Documents\Obsidian Vault\Clippings\Seeed Studio XIAO nRF52840 系列入门指南.md`；SHA-256 `710CCC…025B` | 2026-08-02 | 输入快照 |
| S2 | B | Getting Started with XIAO nRF52840 Series/Seeed Wiki | 更新 2025-04-17 | Features、Hardware overview、FAQ、Resources | https://wiki.seeedstudio.com/XIAO_BLE/ | 2026-08-02 | 当前板级指南 |
| S3 | A | Seeed Studio XIAO Series SoM Datasheet | 当前在线版 | p.6-p.7、p.18-p.20 | https://files.seeedstudio.com/wiki/XIAO/Seeed-Studio-XIAO-Series-SOM-Datasheet.pdf | 2026-08-02 | 板级规格、功耗、电源 |
| S4 | A | XIAO nRF52840 Schematic/Seeed | Rev V1.1，2025-12-19 | p.3 | https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_PDF.pdf | 2026-08-02 | IMU、PDM、电源与内部网络 |

### 冲突裁决

| 主题 | 采用值 | 其他说法 | 采用理由 | 影响/验证 |
| --- | --- | --- | --- | --- |
| 充电电流 | 默认按 50 mA ±5 mA | Wiki 写 50/100 mA 可选 | SoM 手册给出有公差规格，Wiki 的 P0.13 文字/代码不一致 | 未实测前不切换 100 mA |
| Arduino BSP | 按应用选择并锁定一个 BSP | 两套 BSP 的示例和编号略有不同 | 官方明确区分 BLE/低功耗与 Mbed/IMU/PDM 侧重点 | 禁止跨 BSP 硬编码数字引脚 |
| 原理图名称 | Rev V1.1 文件同时覆盖普通/Sense 装配差异 | Wiki 资源链接为同一 PDF | 原理图含可选传感器网络 | 以实板 BOM/丝印确认是否装配 U3/MIC1 |

## 10. 待确认项

| 优先级 | 问题 | 风险 | 获取证据/实测方法 | 负责人/状态 |
| --- | --- | --- | --- | --- |
| P0 | 当前 PCB 修订的 P0.13 充电电流控制真值表 | 电池充电电流不匹配 | 核对 BQ25101、ISET 网络并实测 | 未安排 |
| P1 | LSM6DS3TR-C 的实板 I2C 地址、坐标方向与上拉 | 数据轴向/总线冲突 | 运行 WHO_AM_I、轴向翻转与电阻测量 | 未安排 |
| P1 | 麦克风声学方向、推荐 PDM 时钟及可用采样率 | 音频失真/不可复现 | 查麦克风数据手册并录制频响样本 | 未安排 |

