---
title: "Seeed Studio XIAO nRF52840 系列索引"
aliases: ["XIAO nRF52840 系列"]
category: "外设"
peripheral_type: "开发板索引"
manufacturer: "Seeed Studio"
model: "XIAO nRF52840 Series"
knowledge_status: "conditional"
created: "2026-08-02"
updated: "2026-08-02"
tags: ["外设", "开发板", "nRF52840", "BLE"]
---

# Seeed Studio XIAO nRF52840 系列索引

> 原剪藏同时描述四种硬件。它们共用 nRF52840、USB-C、板载天线、外部 QSPI Flash、锂电池充电和 XIAO 生态，但外部引脚数与板载传感器不同，必须按具体型号选用笔记。[S1][S2]

| 型号 | 外部资源摘要 | 板载传感器 | 当前状态 | 型号页 |
| --- | --- | --- | --- | --- |
| XIAO nRF52840 | D0-D10；1×UART、1×I2C、1×SPI；6×ADC | 无 | conditional | [[Seeed-Studio-XIAO-nRF52840]] |
| XIAO nRF52840 Sense | 与普通版相同的主排针 | LSM6DS3TR-C IMU、PDM 麦克风 | conditional | [[Seeed-Studio-XIAO-nRF52840-Sense]] |
| XIAO nRF52840 Plus | D0-D19；新增 I2S、第 2 UART、第 2 SPI | 无 | needs-research | [[Seeed-Studio-XIAO-nRF52840-Plus]] |
| XIAO nRF52840 Sense Plus | Plus 引脚形态 | LSM6DS3TR-C IMU、PDM 麦克风 | needs-research | [[Seeed-Studio-XIAO-nRF52840-Sense-Plus]] |

## 选型提示

- 只需要 BLE、NFC 和常规外设：普通版。
- 需要板载运动/音频采集：Sense。
- 需要更多外部 IO、第二组 UART/SPI 或 I2S：Plus。
- 同时需要更多 IO 与板载 IMU/麦克风：Sense Plus。

## 系列级风险

- `Seeed nRF52 Boards` 与 `Seeed nRF52 mbed-enabled Boards` 的 Arduino 编号/能力侧重点不同；优先使用 `D0`、`LED_RED` 等板级符号并锁定 BSP 版本。[S1 §两个 Arduino 库][S2 §Two Arduino Libraries]
- RGB 用户 LED 为共阳极、低电平点亮；充电 LED 不是普通用户 LED。[S1 §使用板载三合一 LED][S2 §Playing with the built-in 3-in-one LED]
- 电池 ADC 读取涉及 `P0.14/READ_BAT_ENABLE` 与 `P0.31/AIN7_BAT`，错误控制可能让 ADC 引脚接近 3.6 V 输入上限。[S2 §FAQ Q3]
- 官方 Wiki 对 Plus 写有“18×GPIO(PWM)”与“20/6 PWM/模拟引脚”两种说法；详细引脚表列出 D0-D19。本索引按 20 个编号引脚组织，但在完成原理图/BSP 核验前不宣称 20 路都具备相同 PWM/ADC 能力。[S2 §Features/Specifications comparison/Hardware overview]

## 来源

| ID | 等级 | 来源 | 版本/日期 | 路径或 URL | 用途 |
| --- | --- | --- | --- | --- | --- |
| S1 | B | Seeed Studio XIAO nRF52840 系列入门指南（本地剪藏） | 页面发布 2025-04-17；剪藏 2026-08-02 | `C:\Users\19542\Documents\Obsidian Vault\Clippings\Seeed Studio XIAO nRF52840 系列入门指南.md`；SHA-256 `710CCC…025B` | 原始输入快照 |
| S2 | B | Seeed Studio Wiki: Getting Started with XIAO nRF52840 Series | 页面标注更新 2025-04-17；访问 2026-08-02 | https://wiki.seeedstudio.com/XIAO_BLE/ | 当前官方指南、引脚表、FAQ |

