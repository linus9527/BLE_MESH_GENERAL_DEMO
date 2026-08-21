# AquaMesh 网关—Dashboard 单层 JSON API

| 项目 | 约定 |
| --- | --- |
| 规范版本 | v1 |
| 状态 | 可实施基线 |
| 表示形式 | 单层 JSON 对象；字段值只能是 string、number、integer、boolean 或 null |
| 串口封装 | UTF-8 NDJSON，一行一个完整对象 |
| 适用链路 | 网关 USB CDC ↔ Node-RED/Dashboard；同一对象也可转发到 MQTT |
| 兼容原则 | 保留现有 `type` 和测量字段，新增字段采用加法演进 |
| Schema | [gateway-dashboard-flat-v1.schema.json](./schemas/gateway-dashboard-flat-v1.schema.json) |
| 示例 | [gateway-dashboard-flat-v1.ndjson](./examples/gateway-dashboard-flat-v1.ndjson) |

## 1. 核心决定

1. API 载荷只允许一个根对象，不允许子对象或数组。需要表达多项数据时使用同一根对象中的具名字段；无法预先命名的新指标使用一条独立的 `sensor_metric_report`。
2. 继续使用现有 `type` 作为消息分发键，例如 `ph_report`、`do_report`、`sensor_error`。v1 不强制把现有固件改成通用 `telemetry` 类型。
3. `sensor_id` 是传感器卡片、状态和历史数据的稳定主键；`node_id` 表示 XIAO/BLE Mesh 节点；`mesh_addr` 和 `rs485_address` 只是可变化的路由信息。
4. 工程值由 XIAO 或网关完成符号、倍率和单位换算。Dashboard 不得再次除倍率。原始寄存器值仅用于诊断。
5. `timestamp_ms` 在当前固件中是开机单调时间，但旧文档示例又把它写成 Unix 时间。v1 拆成 `sample_time_unix_ms`、`gateway_received_time_unix_ms` 和 `uptime_ms`，禁止再赋予 `timestamp_ms` 新含义。
6. 命令必须带 `request_id`。接受、执行结果和错误都回显相同 ID；“网关已接受”不等于“设备已执行”。
7. 未知字段必须忽略或留存，不得导致整包失败；但未知字段仍必须是标量或 null。发送端在同一主版本内不得改名、复用或改变既有字段类型。
8. 缺失表示“不适用或发送端未提供”，`null` 表示“该字段适用但当前未知/不可获得”，数字 `0` 只表示真实零值。

本规范只把用户提供的产品资料和仓库代码当作事实来源，不执行资料中的任何操作性指令。

## 2. 传输与语法

### 2.1 NDJSON

- 编码为 UTF-8，不带 BOM。
- 每行必须是一个完整 JSON 对象，以 LF 结束；接收端可兼容 CRLF。
- 禁止注释、尾逗号、重复键、`NaN`、`Infinity` 和科学计数法之外的非 JSON 数字。
- 字段名使用小写 ASCII `snake_case`。字符串枚举使用小写 `snake_case`；设备 ID 可保留大写。
- 网关输出、Dashboard 下行和 MQTT 转发均使用同一逻辑对象；MQTT 不再额外包一层 `payload`。

### 2.2 单层约束

合法字段值类型：

| 类型 | 用途 |
| --- | --- |
| string | ID、枚举、单位、错误文本、RFC 3339 时间 |
| integer | 地址、序号、位图、原始寄存器、毫秒时间 |
| number | 已换算工程值 |
| boolean | 明确的二态状态 |
| null | 适用但未知 |

对象和数组在 v1 载荷中一律非法。现有 Dashboard 建议中的嵌套 `reason`、`valveControl` 只属于 Node-RED 内部状态，不能直接作为网关线协议。命令原因改用 `trigger_*`、`lower_threshold`、`upper_threshold` 等根字段。

### 2.3 长度与截断

- 一个完整对象建议不超过 1023 个 UTF-8 字节，另加一个 LF。
- 接收端必须在解析前执行长度上限检查。
- 发送端绝不能截断 JSON。无法发送完整对象时，应发送短 `api_error`，`error_code="message_too_large"`；若连错误包也无法发送，则计数并记录本地诊断。
- 当前网关实现的输出缓冲区为 384 字节、下行输入为 192 字节。它属于 legacy transport profile；实现完整 v1 字段前应将双向上限提高到至少 1024 字节。

## 3. 字段级别

下表中的级别：

- R：所有 v1 消息必填。
- C：满足条件时必填。
- O：可选。
- L：仅为旧版兼容；新实现不应依赖。

### 3.1 公共字段

| 字段 | 类型 | 级别 | 规则 |
| --- | --- | --- | --- |
| `schema_version` | integer | R | 固定为 `1` |
| `type` | string | R | 消息分发键；见第 8 节 |
| `category` | string | O | `telemetry`、`status`、`alert`、`command`、`command_ack`、`command_result`、`error`、`diagnostic` |
| `message_id` | string | O | 全局消息 ID；资源允许时推荐 ULID/UUID |
| `request_id` | string/null | C | 命令、命令接受、结果及相关错误必填 |
| `sequence` | integer/null | C | 数据或事件序号；设备支持时必填，允许回绕 |
| `source` | string | O | `sensor`、`gateway`、`dashboard`、`manual`、`auto`、`system` |
| `gateway_id` | string | C | 多网关或 MQTT 场景必填；单一 USB 会话可由连接上下文补齐 |
| `node_id` | string | C | 来自 Mesh 节点的消息必填 |
| `sensor_id` | string | C | 传感器遥测、传感器状态和传感器错误必填 |
| `sensor_type` | string | C | 传感器遥测必填；如 `ph`、`orp`、`do`、`water_level` |
| `device_id` | string | C/L | 执行器和网关必填；旧传感器消息可保留为兼容别名 |
| `device_name` | string/null | O | 展示名称，不作为主键 |
| `manufacturer` | string/null | O | 制造商 |
| `model` | string/null | O | 完整产品型号 |
| `firmware_version` | string/null | O | 产生消息的固件版本 |
| `mesh_addr` | string/null | C | 已配网 Mesh 节点使用 `0x0001` 形式；无地址时为 null |
| `rs485_bus` | integer/null | O | 节点上有多条 RS485 总线时使用，从 0 开始 |
| `rs485_address` | integer/null | C | RS485 传感器正式遥测必填；稳定从站地址使用 1–255 |
| `transport` | string | O | `usb_cdc_ndjson`、`mqtt`、`http`、`internal` |

### 3.2 时间、状态和质量

| 字段 | 类型 | 级别 | 规则 |
| --- | --- | --- | --- |
| `sample_time_unix_ms` | integer/null | O | 传感器采样时刻；只有时钟已同步时才填写 Unix ms |
| `gateway_received_time_unix_ms` | integer/null | O | 网关收到 Mesh/RS485 数据的 Unix ms |
| `received_time_unix_ms` | integer/null | O | Dashboard/Node-RED 收到对象的 Unix ms |
| `uptime_ms` | integer/null | C | 无 RTC 的嵌入式消息应使用单调开机毫秒 |
| `time_quality` | string | C | `synchronized`、`gateway_synchronized`、`received`、`unsynchronized`、`unknown` |
| `timestamp_ms` | integer/null | L | 当前仓库固件中映射到 `uptime_ms`；禁止按数值大小自行猜语义 |
| `online` | boolean/null | O | 通信在线状态 |
| `status` | string | O | `ok`、`warning`、`error`、`offline`、`unknown` |
| `quality` | string | C | 遥测必填：`good`、`uncertain`、`bad`、`stale`、`unknown` |
| `data_valid` | boolean/null | O | 整包主数据是否可用于业务 |
| `range_valid` | boolean/null | O | 工程值是否通过已知量程检查 |
| `unit_valid` | boolean/null | O | 单位是否已识别且与指标兼容 |
| `calibration_valid` | boolean/null | O | 校准是否满足项目使用条件；未知必须为 null |
| `state_flags` | integer/null | O | 节点状态位图，具体命名空间由 `state_flags_namespace` 标识 |
| `state_flags_namespace` | string/null | O | 例如 `aquamesh_node_v1` |
| `fault_bits` | integer/null | O | 传感器原生故障位图 |
| `fault_bits_namespace` | string/null | O | 例如 `mik_do_7019_v1` |

### 3.3 错误与诊断

| 字段 | 类型 | 级别 | 规则 |
| --- | --- | --- | --- |
| `error_code` | string/null | C | 错误消息必填；稳定、可编程处理 |
| `error_message` | string/null | O | 面向人的简短说明；不得放敏感信息 |
| `error_layer` | string/null | O | `sensor`、`modbus`、`mesh`、`gateway`、`transport`、`dashboard`、`storage` |
| `retryable` | boolean/null | O | 是否适合自动重试 |
| `retry_count` | integer/null | O | 已执行重试次数 |
| `modbus_function` | integer/null | O | 例如 3、6、16 |
| `modbus_exception_code` | integer/null | O | 例如 1、2、3 |
| `register_address` | integer/null | O | 诊断涉及的寄存器地址 |
| `crc_ok` | boolean/null | O | CRC 未检查时必须为 null |
| `error` | string/number/null | L | 旧消息别名；规范化时复制为 `error_code`，原值可保留 |
| `reason` | string/null | L | 旧消息的简短原因；嵌套对象不允许进入 v1 |

## 4. 身份和归属

### 4.1 主键

| 对象 | 主键 | 说明 |
| --- | --- | --- |
| 网关 | `gateway_id` | 多网关部署必须稳定 |
| XIAO/Mesh 节点 | `gateway_id + node_id` | `mesh_addr` 重新配网后可能变化 |
| 传感器 | `gateway_id + sensor_id` | Dashboard 卡片和数据库主键 |
| 执行器 | `gateway_id + device_id` | 例如阀门、舵机 |
| 命令 | `gateway_id + request_id` | 用于幂等和结果关联 |

`sensor_id` 不得由数组下标生成，也不得仅等于 `mesh_addr`。同一节点挂多个 Modbus 从站时，它们共享 `node_id` 和 `mesh_addr`，但必须有不同的 `sensor_id` 与 `rs485_address`。

旧固件只有 `device_id` 时，适配器只能根据受控设备注册表映射 `sensor_id`；不能把任意 `device_id` 盲目当作 `sensor_id`。映射失败的消息仍可进入诊断，但不能写正式传感器历史。

### 4.2 从站地址

资料中的地址范围并不完全一致：DO 为 1–254，pH/ORP 文档写 1–255，标准 Modbus 通常还保留广播地址。API 使用 1–255 的整数容器保存设备实际配置，但 0 和广播查询地址不得作为传感器稳定身份。是否允许 248–255 由具体传感器适配器决定。

## 5. 时间语义

Dashboard 选择历史横轴的优先级：

1. `sample_time_unix_ms` 存在且 `time_quality="synchronized"`。
2. `gateway_received_time_unix_ms` 存在且网关时钟已同步。
3. Node-RED 本地 `received_time_unix_ms`，并将派生的 `time_quality` 记为 `received`。

`uptime_ms` 只用于排序、延迟分析和检测设备重启，不能直接显示为公历时间。设备重启后 `uptime_ms` 变小是合法情况。`sequence` 与 `uptime_ms` 一起可用于同一设备会话内去重，但序号回绕和重启都必须被容忍。

迁移期内，当前网关产生的 `timestamp_ms` 必须规范化为：

    uptime_ms = timestamp_ms
    time_quality = "unsynchronized"

不能仅因为数值看起来像 13 位毫秒数就推断它是 Unix 时间。

## 6. 数值、单位和 null

### 6.1 工程值

- 具名字段的单位写在字段名中，例如 `temperature_c`、`orp_mv`、`dissolved_oxygen_mg_l`。
- pH 是无量纲指标，字段保持 `ph`，展示单位可写 `pH`。
- 工程值必须是 JSON number，不得用 `"7.00"` 字符串表达。
- 有符号寄存器先按 `int16` 或资料规定的宽度解释，再换算。
- Dashboard 收到工程值后不得重复缩放。

### 6.2 通用单指标报告

未知或尚未形成稳定具名字段的传感器使用：

| 字段 | 说明 |
| --- | --- |
| `metric` | 稳定指标名，如 `conductivity`、`turbidity` |
| `value` | 已换算工程值 |
| `unit` | canonical ASCII 单位 |
| `raw_value` | 可选原始有符号整数 |
| `decimal_places` | 可选小数位数 |
| `unit_code` | 可选设备原生单位码 |
| `unit_code_namespace` | 单位码来源；禁止把不同厂商的数字码混用 |

`sensor_metric_report` 每包只表达一个主指标。温度等伴随值可继续放在同一根对象中。

### 6.3 canonical 单位

常用单位字符串：

| 量 | canonical 字符串 |
| --- | --- |
| 温度 | `degC`、`degF` |
| 电位 | `mV` |
| 溶解氧/浓度 | `mg/L`、`ug/L`、`g/L`、`ppm` |
| 百分比 | `%` |
| 盐度 | `ppt` |
| 水柱 | `mH2O`、`mmH2O` |
| 压力 | `Pa`、`kPa`、`MPa`、`bar`、`mbar`、`hPa`、`psi`、`kg/cm2`、`mmHg` |
| 电导率 | `uS/cm`、`mS/cm` |
| 电阻率 | `Mohm*cm` |
| 浊度 | `NTU` |
| 电流/电阻 | `uA`、`mA`、`A`、`ohm`、`kohm`、`Mohm` |

页面可将 `mH2O` 排版为 `mH₂O`，但传输值不使用 Unicode 下标。单位未知或不兼容时 `unit_valid=false`，不得用注册表默认值静默覆盖错误单位，也不得继续自动控制。

### 6.4 缺失、null 和零

| 表达 | 含义 | Dashboard 行为 |
| --- | --- | --- |
| 字段缺失 | 不适用、旧发送端未实现或包被降级 | 保留上一有效次字段；按规则标记不完整 |
| `null` | 该字段适用，但当前读不到或未知 | 显示未知，不推断 false/0 |
| `0` / `0.0` | 有效零值 | 正常显示并参与范围检查 |
| `quality="bad"` | 包存在但不能用于业务 | 不覆盖最后有效主值 |

DO 的 `air_calibrated` 和 `zero_calibrated` 在校准状态读取失败时必须为 null，同时 `calibration_status_known=false`；不能写成 false。

## 7. 遥测字段

### 7.1 已实现传感器

| `type` | `sensor_type` | 主字段 | 伴随字段 | v1 条件必填 |
| --- | --- | --- | --- | --- |
| `ph_report` | `ph` | `ph` | `temperature_c`、`ph_mv` | `node_id`、`sensor_id`、`mesh_addr`、`rs485_address`、`quality` |
| `orp_report` | `orp` | `orp_mv` | `temperature_c`、`orp_drift_mv` | 同上 |
| `do_report` | `do` | `dissolved_oxygen_mg_l` | `temperature_c`、`saturation_pct`、`calibration_status_known`、`air_calibrated`、`zero_calibrated` | 同上 |
| `water_level_report` | `water_level` | `value` | `unit`、`unit_code`、`raw_value`、`decimal_places` | 同上，且 `unit_valid` |
| `dht_report` | `temperature_humidity` | `temperature_c` | `humidity_pct` | `node_id`、`sensor_id`、`quality` |

稳定的扩展字段：

| 指标 | 字段 |
| --- | --- |
| pH 零点/斜率/校准点数 | `ph_zero_mv`、`ph_slope_pct`、`ph_calibration_points` |
| DO 盐度/气压/温度模式/滤波 | `salinity_ppt`、`atmospheric_pressure_mmhg`、`temperature_mode`、`filter_coefficient` |
| 电导率/TDS/浊度 | `conductivity_us_cm`、`conductivity_ms_cm`、`tds_mg_l`、`turbidity_ntu` |
| 水质扩展 | `ammonia_nitrogen_mg_l`、`residual_chlorine_mg_l`、`cod_mg_l` |
| 液位/压力 | `water_level_m`、`water_level_pct`、`pressure_kpa` |

没有列出的新指标优先用 `sensor_metric_report + metric/value/unit`，待命名稳定后再在同一主版本中“新增”具名字段，不能借用语义相近的旧字段。

### 7.2 原始诊断

多指标传感器不要在正常遥测中放一个含义不清的 `raw_value`。需要看单个寄存器时发送独立 `sensor_raw_report`：

    {"schema_version":1,"type":"sensor_raw_report","category":"diagnostic","node_id":"BLE_MESH_RS485_01","sensor_id":"DO_01","sensor_type":"do","rs485_address":1,"metric":"dissolved_oxygen","register_address":8193,"raw_value":835,"decimal_places":2,"unit":"mg/L","quality":"good","uptime_ms":25000,"time_quality":"unsynchronized"}

## 8. 消息类型和最低字段

| 类型 | 类别 | 除公共字段外的最低字段 |
| --- | --- | --- |
| `gateway_online` | status | `device_id`、`online=true`、一个有效时间字段 |
| `device_online` / `device_offline` | status | `node_id` 或执行器 `device_id`、`online` |
| `device_heartbeat` | status | `node_id`、`state_flags`、`state_flags_namespace` |
| `sensor_error` | error | `node_id`、`sensor_id`、`sensor_type`、`error_code`、`error_layer` |
| `sensor_recovered` | status | `node_id`、`sensor_id`、`sensor_type`、`status="ok"` |
| `ph_report` | telemetry | 第 7.1 节 pH 字段 |
| `orp_report` | telemetry | 第 7.1 节 ORP 字段 |
| `do_report` | telemetry | 第 7.1 节 DO 字段 |
| `water_level_report` | telemetry | 第 7.1 节水位字段 |
| `sensor_metric_report` | telemetry | `metric`、`value`、`unit`、`unit_valid` |
| `dht_alert` | alert | `sensor_id`、`metric`、`state`、`value` |
| `button_event` | alert | `device_id`、`state=pressed/released` |
| `servo_command` | command | `request_id`、`device_id`、`direction`、`speed_pct` |
| `servo_result` | command_result | `request_id`、`device_id`、`result` |
| `ph_calibrate` | command | `request_id`、`sensor_id`、`calibration_point` |
| `ph_calibration_result` | command_result | `request_id`、`sensor_id`、`calibration_point`、`result` |
| `valve_command` | command | `request_id`、`device_id`、`target_state=open/closed`、`source` |
| `valve_command_accepted` | command_ack | `request_id`、`device_id`、`accepted=true` |
| `valve_result` | command_result | `request_id`、`device_id`、`result`、`target_state` |
| `invalid_command` / `api_error` | error | `error_code`；能解析时回显 `request_id` |
| `mesh_send_failed` | error | `request_id`、目标 ID、`error_code`、`error_layer="mesh"` |

## 9. 命令、确认和幂等

### 9.1 状态机

    sending -> accepted -> succeeded
                |            |
                +-> failed <-+
                +-> unconfirmed (Dashboard 本地超时状态)

- `accepted=true` 只表示网关完成语法、权限、目标在线和入队检查。
- 最终 `result` 建议为 `executed`、`rejected`、`failed`、`timeout`、`safe_stopped`。
- 没有物理限位、位置、水流或电流反馈时，`valve_result` 只能表示“命令已执行”，不得宣称“阀门实际打开”。只有 `feedback_source="physical_feedback"` 才能更新 `actual_state`。

### 9.2 request_id

- Dashboard 生成非空 `request_id`，建议 ULID/UUID，长度 8–64。
- 网关和节点能携带时均原样传递；协议载荷容不下时由网关维护 `request_id ↔ mesh_sequence` 的有界映射。
- 在幂等窗口内重复收到相同 `request_id` 和相同规范化参数时，不重复执行，返回已知接受/结果。
- 相同 `request_id` 携带不同参数时返回 `error_code="request_id_conflict"`。
- 接受、结果、离线、Mesh 发送失败和命令超时都必须尽量回显 `request_id`。

### 9.3 单层阀门命令

    {"schema_version":1,"type":"valve_command","category":"command","request_id":"01K3VALVE0001","device_id":"BLE_MESH_VALVE","source":"auto","target_state":"open","trigger_sensor_id":"WATER_LEVEL_01","trigger_value":0.45,"trigger_unit":"mH2O","lower_threshold":0.60,"upper_threshold":1.20}

`trigger_*` 字段只用于审计。正式自动控制仍必须在 Node-RED 内检查模式、数据新鲜度、单位、量程、连续样本数和待处理命令。

### 9.4 pH 校准

canonical 字段为 `calibration_point`，值为 `4.00`、`6.86`、`7.00`、`9.18`、`10.00` 或 `10.01`。旧 `point` 可在迁移期同时保留。命令成功的判定不能只依据网关接受；应以 `ph_calibration_result` 为准。

## 10. 错误码

| `error_code` | 含义 | 建议 `retryable` |
| --- | --- | --- |
| `invalid_json` | 非法 JSON | false |
| `invalid_schema` | 缺字段、类型错误或出现对象/数组 | false |
| `unsupported_version` | 不支持的主版本 | false |
| `unsupported_type` | 未支持的消息类型 | false |
| `message_too_large` | 超过传输上限 | false |
| `unknown_target` | 未知 sensor/device ID | false |
| `target_offline` | 目标离线 | true |
| `request_id_conflict` | 同 ID 不同命令 | false |
| `modbus_timeout` | 从站无响应 | true |
| `modbus_crc_error` | CRC 错误 | true |
| `modbus_exception` | Modbus 异常响应 | 视异常码 |
| `value_out_of_range` | 工程值超出已知范围 | 视原因 |
| `unit_unknown` | 单位无法识别 | false |
| `sensor_fault` | 传感器原生故障位 | 视故障 |
| `mesh_send_failed` | Mesh 发送失败 | true |
| `command_timeout` | 未收到最终结果 | true |
| `permission_denied` | 命令不在白名单或无权限 | false |

`error_message` 可本地化，业务逻辑只依赖 `error_code`。Modbus 异常必须同时保留 `modbus_function` 和 `modbus_exception_code`。

## 11. 兼容与迁移

### 11.1 旧字段映射

| 当前字段/行为 | v1 处理 |
| --- | --- |
| `type` | 原样保留 |
| 传感器 `device_id` | 原样保留为 legacy；由注册表补 `sensor_id` 和 `node_id` |
| `timestamp_ms` | 当前固件明确映射到 `uptime_ms`，不映射到 Unix 时间 |
| `error` | 复制到稳定 `error_code`；旧字段可继续输出 |
| `point` | 复制到 `calibration_point` |
| DO `saturation_pct` | v1 保持原名，避免无收益改名 |
| 旧水位 `value/unit` | v1 保持；补 `unit_valid` 和 `unit_code_namespace` |
| 嵌套 `reason` | 展平为 `trigger_*`、`lower_threshold`、`upper_threshold` |
| 缺少 `schema_version` | legacy adapter 识别为版本 0；规范化后输出版本 1 |

### 11.2 上线顺序

1. Node-RED 先加入 legacy normalizer：接受现有对象，补 `schema_version=1`、稳定身份、`uptime_ms`、`time_quality`、`quality` 和错误字段。
2. Dashboard/历史库改以 `sensor_id` 为主键，对旧 `device_id` 只做受控回退。
3. 网关提高行长度并开始原生输出 v1 新字段，同时保留现有 `type` 和测量字段。
4. Dashboard 下行命令开始强制 `request_id`；网关回显 ID。
5. 完成一个发布周期的双读验证后，才能在未来 v2 删除 legacy 字段。v1 内不得删除。

### 11.3 读写兼容规则

- v1 reader 必须忽略未知标量字段。
- v1 writer 不得发送对象或数组，即使 reader 能解析。
- 类型一旦发布即固定：不能把 number 改成 numeric string，也不能把 boolean 改成 0/1。
- 枚举新增值时，reader 显示 `unknown` 并保留原值；不能崩溃。
- 主版本不支持时拒绝业务处理并返回 `unsupported_version`；不得静默按旧版本解释。

## 12. 安全与健壮性

- Dashboard 提交的 JSON 必须经过类型、目标 ID、命令、数值范围和权限白名单检查，不能直接透传到串口。
- 解析器应限制行长、字段数量、字符串长度和数值范围，并拒绝重复键。
- 原始坏包可进入受限诊断日志，但必须限长并转义，不能作为新 JSON 片段拼接。
- 控制命令日志至少记录 `request_id`、操作者来源、目标、参数、接受时间、结果和错误。
- 自动水阀规则在网关离线、传感器离线、数据过期、`quality!="good"`、单位无效或量程无效时必须降级；最终安全动作由现场确认，不能由 API 自行假设。
- MQTT topic 建议为 `aquamesh/<gateway_id>/<sensor_or_device_id>/<category>`；payload 就是本规范对象，QoS 和 retain 由部署策略决定。

## 13. 示例

### 13.1 pH

    {"schema_version":1,"type":"ph_report","category":"telemetry","node_id":"BLE_MESH_RS485_01","sensor_id":"PH_01","sensor_type":"ph","mesh_addr":"0x0008","rs485_address":1,"sequence":42,"ph":7.24,"temperature_c":24.8,"ph_mv":6.2,"quality":"good","uptime_ms":25000,"time_quality":"unsynchronized"}

### 13.2 DO 校准状态未知

    {"schema_version":1,"type":"do_report","category":"telemetry","node_id":"BLE_MESH_RS485_02","sensor_id":"DO_01","sensor_type":"do","mesh_addr":"0x0009","rs485_address":1,"sequence":43,"dissolved_oxygen_mg_l":8.35,"temperature_c":25.0,"saturation_pct":96,"calibration_status_known":false,"air_calibrated":null,"zero_calibrated":null,"quality":"uncertain","uptime_ms":27000,"time_quality":"unsynchronized"}

### 13.3 ORP 错误与恢复

    {"schema_version":1,"type":"sensor_error","category":"error","node_id":"BLE_MESH_RS485_01","sensor_id":"ORP_01","sensor_type":"orp","mesh_addr":"0x0008","rs485_address":2,"error_code":"modbus_timeout","error_layer":"modbus","retryable":true,"quality":"bad","uptime_ms":30000,"time_quality":"unsynchronized"}
    {"schema_version":1,"type":"sensor_recovered","category":"status","node_id":"BLE_MESH_RS485_01","sensor_id":"ORP_01","sensor_type":"orp","mesh_addr":"0x0008","rs485_address":2,"status":"ok","quality":"unknown","uptime_ms":34000,"time_quality":"unsynchronized"}

### 13.4 水位

    {"schema_version":1,"type":"water_level_report","category":"telemetry","node_id":"BLE_MESH_RS485_02","sensor_id":"WATER_LEVEL_01","sensor_type":"water_level","mesh_addr":"0x0009","rs485_address":4,"sequence":44,"value":0.86,"unit":"mH2O","unit_code":7,"unit_code_namespace":"generic_modbus_transmitter_v1","raw_value":86,"decimal_places":2,"unit_valid":true,"quality":"good","uptime_ms":29000,"time_quality":"unsynchronized"}

### 13.5 命令接受与结果

    {"schema_version":1,"type":"valve_command","category":"command","request_id":"01K3VALVE0001","device_id":"BLE_MESH_VALVE","source":"manual","target_state":"closed"}
    {"schema_version":1,"type":"valve_command_accepted","category":"command_ack","request_id":"01K3VALVE0001","device_id":"BLE_MESH_VALVE","accepted":true,"uptime_ms":31000,"time_quality":"unsynchronized"}
    {"schema_version":1,"type":"valve_result","category":"command_result","request_id":"01K3VALVE0001","device_id":"BLE_MESH_VALVE","target_state":"closed","result":"executed","feedback_source":"command_ack","uptime_ms":31200,"time_quality":"unsynchronized"}

更多可机器验证的逐行示例见 examples 文件。

## 14. 资料事实与设计边界

| ID | 来源 | 本规范采用的事实 |
| --- | --- | --- |
| S1 | 米科 MIK-DO-7019 手册，U-MIK-DO-7019-CN1，2022-10 | DO、温度、饱和度、校准状态、单位码、故障位和 Modbus 寄存器 |
| S2 | 米科 MIK-pH-8001 手册，U-MIK-PH-8001-LXCN2，2021-03 | pH、温度、mV、校准点和寄存器倍率 |
| S3 | 米科 MIK-ORP-8001 手册，U-MIK-ORP-8001-CN1，2021-09 | ORP、漂移、温度、量程和寄存器倍率 |
| S4 | 齐兴科技 MODBUS 压力变送器通信协议，精确型号/版本未标明 | 通用压力/水位值、单位、小数位、从站地址和波特率 |
| S5 | `apps/gateway/src/main.c` 与 `node-red/` | 当前 JSON、缓冲区、时间语义、Dashboard 字段与 MQTT 行为 |

资料中的明显冲突不被静默合并：

- S1 供电规格是 6–12 VDC，而仓库接线文档使用独立 12 V；API 只上报设备型号和故障，不把接线推荐编码成遥测事实。
- S3 的协议章节复用了大量 pH/DO 表格，且状态寄存器地址自相矛盾；v1 不定义未经确认的 ORP 远程校准命令。
- S2/S3 的用户命令表存在印刷错误（例如重复的 pH9.18、十六进制字符错误）；校准允许值以明确的十进制命令和当前已实现固件交集为准。
- S4 未标明精确变送器型号，水位量程和现场单位仍需实机读取；因此 `unit_code_namespace`、`raw_value` 和 `decimal_places` 必须保留诊断能力。

## 15. 尚未确认但不阻塞 v1

1. 水位变送器精确型号、量程、实际从站地址、当前单位码与小数位。
2. 阀门是否有物理位置、限位、水流或电流反馈。
3. 三个 XIAO 的最终 `node_id`、各传感器 `sensor_id` 和 RS485 地址分配。
4. 网关何时增加 RTC/NTP；在此之前统一使用 `uptime_ms + received_time_unix_ms`。
5. 产品化 MQTT topic 前缀、鉴权和数据保留策略。

这些缺口通过 null、quality、单位命名空间和反馈来源表达，不需要修改 v1 的单层结构。
