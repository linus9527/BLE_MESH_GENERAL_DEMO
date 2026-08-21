# AquaMesh Dashboard 数据映射与计算规则

| 项目 | 内容 |
| --- | --- |
| 文档状态 | 资方 Demo 与固件联调契约 |
| 版本 | v1.0 |
| 日期 | 2026-08-21 |
| 适用范围 | pH、ORP、溶解氧、水位压力、进水阀 |
| 上游 | USB 串口网关输出的单行 JSON |
| 下游 | Node-RED 状态、历史数据、FlowFuse Dashboard 2.0 |

## 1. 如何阅读本映射

网关每次从串口输出一个完整 JSON 对象。Node-RED 解析后，将该对象放入 `msg.payload`。例如：

```json
{"type":"ph_report","sensor_id":"PH_01","ph":7.24,"temperature_c":24.8,"ph_mv":6.2,"timestamp_ms":1730000025000}
```

则页面读取路径为 `msg.payload.ph`、`msg.payload.temperature_c` 等。本文表格中的“JSON 字段”均指串口 JSON 根字段；在 Node-RED Function 节点中应在字段前加 `msg.payload.`。

必须遵守以下边界：

1. 网关已经换算好的工程值直接展示，Node-RED 不再次除以倍率。
2. `raw_value`、`decimal_places` 仅供诊断和校验，不能替代有效的 `value`。
3. 页面上出现的判断性标签必须有字段或规则来源；没有来源时只显示中性描述，不能凭 Demo 数值推断“正常水质”。
4. 同一 XIAO 可连接多个 RS485 地址，但每个传感器仍单独上报 JSON，并按 `sensor_id` 独立更新。

## 2. 设备身份与数据归属

| 含义 | JSON 字段 | 用途 | 缺失处理 |
| --- | --- | --- | --- |
| 消息类型 | `type` | 选择消息处理器，如 `ph_report` | 丢入诊断事件，不更新卡片 |
| XIAO 节点 ID | `node_id` | 统计 3 个节点、关联节点在线状态 | 联调期可由 `mesh_addr` 临时映射，正式版必填 |
| 传感器 ID | `sensor_id` | 卡片、状态、历史记录的稳定主键 | 不允许用数组顺序代替；缺失时不入正式历史 |
| 传感器类型 | `sensor_type` | 选择注册表与卡片结构 | 可由受支持的 `type` 推导并记录警告 |
| Mesh 地址 | `mesh_addr` | 显示所属 Mesh 节点、路由诊断 | 保留最近有效值并标记字段不完整 |
| Modbus 地址 | `rs485_address` | 区分同一 XIAO 上的多个传感器 | RS485 传感器正式版必填 |
| 设备采样时间 | `timestamp_ms` | 历史数据的采样时间 | 缺失时用 Node-RED 接收时间并标记 `timestamp_source=received` |

设备归属键为 `sensor_id`，不是 `mesh_addr`。例如 pH 和 ORP 共用一个 XIAO 时，两包可具有相同 `node_id/mesh_addr`，但必须具有不同 `sensor_id/rs485_address`。

## 3. 首页全局标签

| 页面标签 | 数据来源 | 计算与展示逻辑 |
| --- | --- | --- |
| 网关在线 | 串口连接状态 + 最近合法网关 JSON 接收时间 | 串口打开不等于在线；收到可识别 JSON 后才显示在线。串口关闭或静默超时显示未连接/通信静默 |
| 节点 `3 / 3` | 活跃设备状态中的唯一 `node_id` | `在线唯一 node_id 数 / 注册节点总数`；同一节点的多个传感器只计 1 次 |
| 设备 `5 / 5` | 四个 `sensor_id` + 一个阀门 `device_id` | `在线设备数 / 注册设备总数`；不能用收到的数据包数量计算 |
| 传感器 `4 / 4 在线` | 四个传感器的派生状态 | 状态为“数据正常”才计在线正常；过期、故障和离线分别计入异常 |
| 活动异常 | 未恢复的事件/告警集合 | 按唯一告警 ID 计数，恢复事件关闭对应告警，不累计历史条数 |
| 数据周期 `2 s` | 设备注册表 `sampleIntervalMs` | 展示配置值；若设备周期不一致则显示“2–5 s”等范围 |
| 最近更新 | Node-RED `received_at_ms` | 取首页四个传感器最近一次接收时间的最大值，格式化为“刚刚/2 秒前”；不使用浏览器刷新时间 |

## 4. 四张传感器卡

### 4.1 pH 卡

消息筛选：`type === "ph_report"` 且 `sensor_id` 匹配 pH 注册设备。

| 页面标签 | JSON 字段 | 展示/计算逻辑 |
| --- | --- | --- |
| pH 主读数 | `ph` | 有限数字，保留 2 位，单位固定显示 `pH`；示例 `7.24 pH` |
| 水温 | `temperature_c` | 有限数字，保留 1 位，显示 `24.8 °C` |
| 电极电压 | `ph_mv` | 有限数字，保留 1 位，显示 `6.2 mV` |
| 主变量 · 实时读数 | 无数值计算 | 中性说明文案，不表示该 pH 已达到业务合格范围 |
| 字段 `ph_report.ph` | `type` + `ph` | Demo 的溯源提示；正式详情抽屉可显示完整路径 |
| 15 分钟趋势 | `ph` + 时间 | x 轴取有效采样时间，y 轴取 `ph`；只连有效点，不用 `0` 补空值 |

### 4.2 ORP 卡

消息筛选：`type === "orp_report"` 且 `sensor_id` 匹配 ORP 注册设备。

| 页面标签 | JSON 字段 | 展示/计算逻辑 |
| --- | --- | --- |
| ORP 主读数 | `orp_mv` | 有限数字，保留 1 位，显示 `236.4 mV` |
| 水温 | `temperature_c` | 保留 1 位，显示 `24.7 °C` |
| 漂移 | `orp_drift_mv` | 保留 1 位并保留正负号；示例 `+1.2 mV` |
| 氧化还原电位 | 无数值计算 | 指标名称，不包含合格/不合格判断 |
| 字段 `orp_report.orp_mv` | `type` + `orp_mv` | Demo 的溯源提示 |
| 15 分钟趋势 | `orp_mv` + 时间 | y 轴取 `orp_mv`，不能与温度或漂移混画成一条线 |

### 4.3 溶解氧卡

消息筛选：`type === "do_report"` 且 `sensor_id` 匹配 DO 注册设备。

| 页面标签 | JSON 字段 | 展示/计算逻辑 |
| --- | --- | --- |
| 溶解氧主读数 | `dissolved_oxygen_mg_l` | 有限数字，保留 2 位，显示 `8.35 mg/L` |
| 饱和度 | `saturation_pct` | 四舍五入为整数，显示 `96 %` |
| 水温 | `temperature_c` | 保留 1 位，显示 `25.0 °C` |
| 空气校准已完成 | `calibration_status_known`、`air_calibrated` | 仅当状态已知且 `air_calibrated === true` 时显示；已知且为 false 显示“空气校准未完成”；未知/空值显示“校准状态未知” |
| 15 分钟趋势 | `dissolved_oxygen_mg_l` + 时间 | y 轴只取 DO 浓度；饱和度和温度作为详情中的独立序列 |

### 4.4 水位压力卡

消息筛选：`type === "water_level_report"` 且 `sensor_id` 匹配水位注册设备。

| 页面标签 | JSON 字段/状态 | 展示/计算逻辑 |
| --- | --- | --- |
| 水位压力主读数 | `value` | 网关已换算的工程值，按 `decimal_places` 显示，最多保留合理位数；Demo 为 2 位 |
| 单位 | `unit`、`unit_code`、设备注册表 | 按第 5 节解析；内部 canonical 单位为 `mH2O`，页面排版为 `mH₂O` |
| 水柱压力 · unit code 7 | `unit_code` | 仅在 `unit_code === 7` 时显示；否则显示实际单位来源，不写死代码 7 |
| 阈值状态 | 当前 canonical 值 + 下/上限 | `value <= lower` 显示“低液位”；`value >= upper` 显示“高液位”；中间显示“保持区间”。它不是百分比，也不推导水箱装满比例 |
| 单位来自网关数据包 | 单位解析结果 `unit_source` | `payload`、`unit_code`、`registry` 或 `unknown` 分别显示对应来源 |
| 15 分钟趋势 | canonical 值 + 时间 | 单位可换算时先统一为 `mH2O`；不可换算的不同单位不能画在同一坐标轴 |

水位原始寄存器换算由 XIAO 或网关完成：

```text
value = signed_int16(raw_value) / 10 ^ decimal_places
```

例如 `raw_value=86`、`decimal_places=2` 得到 `value=0.86`。如果包中已经有 `value=0.86`，Node-RED 直接使用 `value`，不得再次除以 100。

## 5. 水位单位解析

Node-RED 输出统一状态：

```json
{
  "display_unit": "mH₂O",
  "canonical_unit": "mH2O",
  "canonical_value": 0.86,
  "unit_source": "payload",
  "unit_valid_for_control": true
}
```

解析优先级：

1. `unit` 非空且受支持：规范化拼写后采用，`unit_source="payload"`。
2. 无 `unit`，但 `unit_code` 已知：当前 `7 -> mH2O`，`unit_source="unit_code"`。
3. 二者均无：读取对应 `sensor_id` 的注册表 `defaultUnit`，`unit_source="registry"`。
4. 包内提供了未知或不兼容单位时，不允许用默认单位覆盖错误信息，`unit_source="unknown"`，页面显示 `?` 并暂停自动控制。

单位规范化只处理已明确支持的等价写法，例如 `mH2O`、`mH₂O`；禁止根据量级猜单位。

## 6. 卡片状态计算

每个 `sensor_id` 独立维护 `last_received_at_ms`、`last_valid_at_ms`、`last_value` 和故障状态。

| 页面状态 | 判定优先级与条件 | 主读数处理 |
| --- | --- | --- |
| 网关未连接 | 网关连接状态无效 | 保留最后有效值并注明非实时；从未有值则 `—` |
| 设备离线 | 收到该设备离线消息，或达到离线超时 | 同上 |
| 传感器故障 | 收到该 `sensor_id` 的 `sensor_error`/故障位 | 同上，不用故障包里的 `0` 覆盖 |
| 数据过期 | 当前时间减 `last_valid_at_ms > 60 s` | 保留最后有效值并显示时间 |
| 数据不完整 | 当前报告缺少某个非主字段 | 主字段有效则继续更新主读数和历史，缺失次字段显示 `—` |
| 数据正常 | 在线、未故障、未过期且主字段有效 | 更新主读数、历史和最后有效时间 |

状态优先级从上到下；例如网关断开后不能仍显示“数据正常”。

## 7. 趋势图与时间

| 图表元素 | 来源 | 规则 |
| --- | --- | --- |
| 15 分钟横轴 | `timestamp_ms` 或接收时间 | 优先设备采样时间；缺失时用接收时间并记录来源 |
| 趋势纵轴 | 各卡主读数字段 | pH=`ph`，ORP=`orp_mv`，DO=`dissolved_oxygen_mg_l`，水位=`canonical_value` |
| “现在”端点 | 最新有效历史点 | 不是浏览器当前时钟生成的假点 |
| 断线空档 | 时间间隔超过配置阈值 | 折线断开，不插值伪造数据 |
| 近 1 天/周/月 | 历史聚合结果 | 显示聚合粒度；旧查询响应不得覆盖用户最后选择 |

## 8. 水阀模式、阈值与命令映射

### 8.1 页面状态来源

| 页面标签/控件 | 数据或状态来源 | 展示/计算逻辑 |
| --- | --- | --- |
| 自动/手动 | Node-RED 持久化控制配置 `valveControl.mode` | `manual` 优先级高于自动规则；切换需确认 |
| 下限/上限 | `valveControl.lowerThreshold/upperThreshold` | 统一保存 canonical `mH2O`，显示当前单位 |
| 最后指令：打开/关闭 | 最近 `valve_result` | 仅当 `result="executed"` 后更新；无位置反馈时不能写“阀门实际已打开” |
| 网关已接受 | `valve_command_accepted.request_id` | 命令第一阶段，只表示网关接收，不表示节点执行 |
| 节点已确认 | `valve_result.request_id` + `result` | 第二阶段；同一 `request_id` 关联结果 |
| 自动规则判断 | canonical 水位 + 模式 + 阈值 + 数据有效性 | 见 8.3；仅在 `mode="auto"` 时运行 |

### 8.2 上下限配置

持久化配置建议：

```json
{
  "valveControl": {
    "mode": "auto",
    "lowerThreshold": 0.60,
    "upperThreshold": 1.20,
    "canonicalUnit": "mH2O",
    "minimumGap": 0.05,
    "confirmSamples": 3,
    "updatedBy": "dashboard",
    "updatedAt": "2026-08-21T14:10:00+08:00"
  }
}
```

阈值设置弹窗包含下限和上限两个数字输入框，并执行以下校验：

1. 两个值都必须为有限数字。
2. `lowerThreshold < upperThreshold`。
3. `upperThreshold - lowerThreshold >= 0.05 mH2O`，防止滞回区过窄。
4. 两个值必须位于该水位传感器的有效量程内；量程优先使用已确认的设备注册表/寄存器值。
5. 输入步长优先取 `10^-decimal_places`；没有小数位信息时 Demo 使用 `0.01 mH2O`。
6. 保存成功后写持久化上下文或数据库并记录事件；页面刷新和 Node-RED 重启后仍生效。
7. 自动模式下保存阈值视为“保存并立即应用”，弹窗明确提示，并用当前有效水位立刻重新判断。
8. 配置不存在时才采用 Demo 默认值 `0.60/1.20 mH2O`；默认值不是现场正式参数。

### 8.3 自动控制计算

水阀当前定义为进水阀：

```text
value <= lowerThreshold  -> 目标 open
value >= upperThreshold  -> 目标 closed
lowerThreshold < value < upperThreshold -> 保持最后确认命令
```

正式控制建议要求连续 `confirmSamples=3` 个有效样本落在同一触发区后才下发，以过滤单点毛刺。若期间出现无效样本或回到保持区，连续计数清零。Demo 的“低液位/高液位”快捷场景等价于注入 3 个连续样本，以便现场演示。

自动规则还必须同时满足：模式为 `auto`、网关在线、水位设备在线、数据未过期、单位可换算、当前无同目标待处理命令。任一关键条件失效时显示“自动控制降级”；Demo 暂按安全策略请求关闭，正式策略仍需现场确认。

### 8.4 下行命令与结果

自动规则或手动操作生成：

```json
{
  "type": "valve_command",
  "request_id": "01J5X7M2R8...",
  "device_id": "BLE_MESH_VALVE",
  "source": "auto",
  "state": "open",
  "reason": {
    "sensor_id": "WATER_LEVEL_01",
    "value": 0.45,
    "unit": "mH2O",
    "lowerThreshold": 0.60,
    "upperThreshold": 1.20
  }
}
```

手动命令的 `source` 为 `manual`；可不包含自动判断 `reason`，但必须包含 `request_id`、`device_id` 和目标 `state`。网关接受与节点结果必须使用相同 `request_id` 返回。

## 9. 完整示例：JSON 到页面

输入：

```json
{
  "type": "water_level_report",
  "node_id": "BLE_MESH_RS485_02",
  "sensor_id": "WATER_LEVEL_01",
  "sensor_type": "water_level",
  "mesh_addr": "0x0009",
  "rs485_address": 4,
  "value": 0.86,
  "unit_code": 7,
  "unit": "mH2O",
  "raw_value": 86,
  "decimal_places": 2,
  "timestamp_ms": 1730000025000
}
```

阈值配置为下限 `0.60`、上限 `1.20` 时，页面和规则得到：

| 输出 | 结果 | 原因 |
| --- | --- | --- |
| 水位主读数 | `0.86 mH₂O` | 直接使用 `value`，单位来自 `unit` |
| 阈值状态 | `保持区间` | `0.60 < 0.86 < 1.20` |
| 自动决策 | 保持最后确认命令 | 滞回区内不改变阀门 |
| 趋势 y 值 | `0.86` | canonical 单位已是 `mH2O` |
| 设备归属 | 节点 `BLE_MESH_RS485_02` / RS485 地址 `4` | 来自身份字段，不从 unit code 推导 |

## 10. 联调检查清单

- 固件/网关团队逐个确认四种报告的 `type` 和字段拼写。
- 每个传感器确认稳定 `node_id`、`sensor_id`、`mesh_addr`、`rs485_address`。
- 验证同一 XIAO 的两个传感器不会覆盖彼此卡片和历史。
- 验证 `value` 已换算后 Node-RED 不重复缩放。
- 验证 unit code `7` 只用于单位映射，不被当作从站地址。
- 验证页面每个判断性标签都能追溯到字段或本文件中的计算规则。
- 验证修改上下限后持久化、事件记录和自动规则立即重算。
- 验证阈值非法、单位未知、数据过期、网关断开时不会误开阀。
- 验证命令接受与执行结果使用同一 `request_id` 关联。
