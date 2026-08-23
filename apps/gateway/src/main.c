#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_device.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_protocol.h"
#include "app_rs485_config.h"

#define GATEWAY_JSON_LINE_MAX       640
#define SERIAL_LINE_MAX             256
#define SERIAL_RX_QUEUE_SIZE        256
#define SERIAL_CONNECTION_POLL_MS   100
#define GENERAL_NODE_MAX            8
#define GENERAL_SENSOR_COUNT        4
#define NODE_ID_MAX                 40
#define SENSOR_ID_MAX               32
#define REQUEST_ID_MAX              48

struct tracked_sensor {
	bool known;
	bool online;
	uint8_t rs485_address;
};

struct tracked_node {
	bool used;
	bool online;
	uint32_t hardware_id;
	uint16_t mesh_address;
	int64_t last_seen_ms;
	char node_id[NODE_ID_MAX];
	struct tracked_sensor sensors[GENERAL_SENSOR_COUNT];
};

struct pending_ph_calibration {
	bool active;
	uint16_t mesh_address;
	uint8_t sequence;
	char request_id[REQUEST_ID_MAX];
	char sensor_id[SENSOR_ID_MAX];
};

static struct tracked_node nodes[GENERAL_NODE_MAX];
static struct pending_ph_calibration pending_ph;
static const struct device *const serial = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Gateway serial device must be USB CDC ACM");

static atomic_t serial_ready;
static atomic_t gateway_provisioned;
static uint8_t gateway_sequence;
static uint8_t command_sequence;

K_MSGQ_DEFINE(serial_rx_queue, sizeof(uint8_t), SERIAL_RX_QUEUE_SIZE, 4);
K_MUTEX_DEFINE(serial_tx_mutex);
K_MUTEX_DEFINE(node_mutex);

static void gateway_heartbeat_handler(struct k_work *work);
static void node_liveness_handler(struct k_work *work);
static void serial_connection_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(gateway_heartbeat_work, gateway_heartbeat_handler);
K_WORK_DELAYABLE_DEFINE(node_liveness_work, node_liveness_handler);
K_WORK_DELAYABLE_DEFINE(serial_connection_work, serial_connection_handler);

static int64_t uptime_ms(void)
{
	return k_uptime_get();
}

static void emit_json(const char *format, ...)
{
	char line[GATEWAY_JSON_LINE_MAX];
	va_list arguments;
	int length;

	if (!atomic_get(&serial_ready)) {
		return;
	}

	va_start(arguments, format);
	length = vsnprintk(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (length < 0) {
		return;
	}

	length = MIN(length, (int)sizeof(line) - 1);
	k_mutex_lock(&serial_tx_mutex, K_FOREVER);
	for (int index = 0; index < length; index++) {
		uart_poll_out(serial, line[index]);
	}
	uart_poll_out(serial, '\n');
	k_mutex_unlock(&serial_tx_mutex);
}

static void emit_gateway_online(void)
{
	emit_json("{\"schema_version\":1,\"type\":\"gateway_online\",\"category\":\"status\",\"device_id\":\"BLE_MESH_GATEWAY\",\"gateway_id\":\"%s\",\"online\":true,\"firmware_version\":\"%s\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  APP_GATEWAY_ID, APP_FIRMWARE_VERSION, (long long)uptime_ms());
}

static void serial_connection_handler(struct k_work *work)
{
	uint32_t dtr = 0U;
	bool connected;
	bool was_connected;

	ARG_UNUSED(work);
	connected = uart_line_ctrl_get(serial, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U;
	was_connected = atomic_get(&serial_ready) != 0;
	if (connected && !was_connected) {
		atomic_set(&serial_ready, 1);
		if (atomic_get(&gateway_provisioned)) {
			emit_gateway_online();
		}
	} else if (!connected && was_connected) {
		atomic_clear(&serial_ready);
	}
	(void)k_work_reschedule(&serial_connection_work,
				K_MSEC(SERIAL_CONNECTION_POLL_MS));
}

static int sensor_index(enum app_sensor_type sensor_type)
{
	if (sensor_type < APP_SENSOR_PH || sensor_type > APP_SENSOR_WATER_LEVEL) {
		return -1;
	}
	return (int)sensor_type - 1;
}

static const char *sensor_type_name(enum app_sensor_type sensor_type)
{
	switch (sensor_type) {
	case APP_SENSOR_PH:
		return "ph";
	case APP_SENSOR_DO:
		return "do";
	case APP_SENSOR_ORP:
		return "orp";
	case APP_SENSOR_WATER_LEVEL:
		return "water_level";
	default:
		return "unknown";
	}
}

static const char *sensor_id_prefix(enum app_sensor_type sensor_type)
{
	switch (sensor_type) {
	case APP_SENSOR_PH:
		return "PH";
	case APP_SENSOR_DO:
		return "DO";
	case APP_SENSOR_ORP:
		return "ORP";
	case APP_SENSOR_WATER_LEVEL:
		return "WATER_LEVEL";
	default:
		return "UNKNOWN";
	}
}

static uint8_t default_rs485_address(enum app_sensor_type sensor_type)
{
	switch (sensor_type) {
	case APP_SENSOR_PH:
		return APP_RS485_PH_ADDRESS;
	case APP_SENSOR_DO:
		return APP_RS485_DO_ADDRESS;
	case APP_SENSOR_ORP:
		return APP_RS485_ORP_ADDRESS;
	case APP_SENSOR_WATER_LEVEL:
		return APP_RS485_WATER_LEVEL_ADDRESS;
	default:
		return 0U;
	}
}

static void format_node_id(char *buffer, size_t size, uint32_t hardware_id)
{
	snprintk(buffer, size, "BLE_MESH_RS485_%08X", hardware_id);
}

static void format_sensor_id(char *buffer, size_t size, const struct tracked_node *node,
			     enum app_sensor_type sensor_type)
{
	snprintk(buffer, size, "%s_%08X", sensor_id_prefix(sensor_type), node->hardware_id);
}

static struct tracked_node *node_by_mesh_locked(uint16_t mesh_address)
{
	for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
		if (nodes[index].used && nodes[index].mesh_address == mesh_address) {
			return &nodes[index];
		}
	}
	return NULL;
}

static struct tracked_node *node_by_hardware_id_locked(uint32_t hardware_id)
{
	for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
		if (nodes[index].used && nodes[index].hardware_id == hardware_id) {
			return &nodes[index];
		}
	}
	return NULL;
}

static bool any_node_offline_locked(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
		if (nodes[index].used && !nodes[index].online) {
			return true;
		}
	}
	return false;
}

static void refresh_gateway_led_locked(void)
{
	app_led_set(any_node_offline_locked() ? APP_LED_WARNING : APP_LED_ONLINE);
}

static struct tracked_node *mark_node_online(uint16_t mesh_address, uint32_t hardware_id)
{
	struct tracked_node *node;
	bool first_seen;
	bool recovered;

	k_mutex_lock(&node_mutex, K_FOREVER);
	node = node_by_hardware_id_locked(hardware_id);
	if (node == NULL) {
		for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
			if (!nodes[index].used) {
				node = &nodes[index];
				memset(node, 0, sizeof(*node));
				node->used = true;
				node->hardware_id = hardware_id;
				format_node_id(node->node_id, sizeof(node->node_id), hardware_id);
				break;
			}
		}
	}
	if (node == NULL) {
		k_mutex_unlock(&node_mutex);
		return NULL;
	}

	first_seen = node->last_seen_ms == 0;
	recovered = !first_seen && !node->online;
	node->online = true;
	node->mesh_address = mesh_address;
	node->last_seen_ms = uptime_ms();
	if (first_seen || recovered) {
		emit_json("{\"schema_version\":1,\"type\":\"device_online\",\"category\":\"status\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"device_id\":\"%s\",\"device_name\":\"RS485_Node\",\"mesh_addr\":\"0x%04x\",\"online\":true,\"reason\":\"%s\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
			  APP_GATEWAY_ID, node->node_id, node->node_id, node->mesh_address,
			  first_seen ? "boot" : "mesh_recovered", (long long)uptime_ms());
	}
	refresh_gateway_led_locked();
	k_mutex_unlock(&node_mutex);
	return node;
}

static struct tracked_node *mark_node_seen(uint16_t mesh_address)
{
	struct tracked_node *node;

	k_mutex_lock(&node_mutex, K_FOREVER);
	node = node_by_mesh_locked(mesh_address);
	if (node != NULL) {
		node->online = true;
		node->last_seen_ms = uptime_ms();
	}
	k_mutex_unlock(&node_mutex);
	return node;
}

static uint8_t node_sensor_address(struct tracked_node *node,
				   enum app_sensor_type sensor_type)
{
	int index = sensor_index(sensor_type);
	uint8_t address = default_rs485_address(sensor_type);

	if (index < 0) {
		return address;
	}
	k_mutex_lock(&node_mutex, K_FOREVER);
	if (node->sensors[index].rs485_address != 0U) {
		address = node->sensors[index].rs485_address;
	}
	node->sensors[index].known = true;
	node->sensors[index].online = true;
	k_mutex_unlock(&node_mutex);
	return address;
}

static void gateway_heartbeat_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)app_mesh_send_gateway_heartbeat(gateway_sequence++);
	(void)k_work_reschedule(&gateway_heartbeat_work,
				K_MSEC(APP_GATEWAY_HEARTBEAT_INTERVAL_MS));
}

static void node_liveness_handler(struct k_work *work)
{
	int64_t now = uptime_ms();

	ARG_UNUSED(work);
	k_mutex_lock(&node_mutex, K_FOREVER);
	for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
		struct tracked_node *node = &nodes[index];

		if (!node->used || !node->online ||
		    now - node->last_seen_ms < APP_NODE_OFFLINE_TIMEOUT_MS) {
			continue;
		}
		node->online = false;
		emit_json("{\"schema_version\":1,\"type\":\"device_offline\",\"category\":\"status\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"device_id\":\"%s\",\"mesh_addr\":\"0x%04x\",\"online\":false,\"timeout_ms\":%d,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
			  APP_GATEWAY_ID, node->node_id, node->node_id, node->mesh_address,
			  APP_NODE_OFFLINE_TIMEOUT_MS, (long long)now);
	}
	refresh_gateway_led_locked();
	k_mutex_unlock(&node_mutex);
	(void)k_work_reschedule(&node_liveness_work, K_SECONDS(1));
}

static void format_fixed_1(char *buffer, size_t size, int16_t value)
{
	int32_t magnitude = value < 0 ? -(int32_t)value : value;

	snprintk(buffer, size, "%s%ld.%01ld", value < 0 ? "-" : "",
		  (long)(magnitude / 10), (long)(magnitude % 10));
}

static void format_fixed_2(char *buffer, size_t size, int16_t value)
{
	int32_t magnitude = value < 0 ? -(int32_t)value : value;

	snprintk(buffer, size, "%s%ld.%02ld", value < 0 ? "-" : "",
		  (long)(magnitude / 100), (long)(magnitude % 100));
}

static void format_scaled(char *buffer, size_t size, int16_t value, uint8_t decimals)
{
	int32_t magnitude = value < 0 ? -(int32_t)value : value;
	int32_t divisor = 1;

	for (uint8_t index = 0; index < decimals; index++) {
		divisor *= 10;
	}
	if (decimals == 0U) {
		snprintk(buffer, size, "%s%ld", value < 0 ? "-" : "", (long)magnitude);
	} else if (decimals == 1U) {
		snprintk(buffer, size, "%s%ld.%01ld", value < 0 ? "-" : "",
			  (long)(magnitude / divisor), (long)(magnitude % divisor));
	} else if (decimals == 2U) {
		snprintk(buffer, size, "%s%ld.%02ld", value < 0 ? "-" : "",
			  (long)(magnitude / divisor), (long)(magnitude % divisor));
	} else {
		snprintk(buffer, size, "%s%ld.%03ld", value < 0 ? "-" : "",
			  (long)(magnitude / divisor), (long)(magnitude % divisor));
	}
}

static const char *transmitter_unit_name(uint8_t unit_code)
{
	static const char *const units[] = {
		"MPa", "kPa", "Pa", "bar", "mbar", "kg/cm2", "psi", "mH2O", "mmH2O",
	};

	return unit_code < ARRAY_SIZE(units) ? units[unit_code] : "unknown";
}

static const char *ph_calibration_point_name(uint8_t point)
{
	switch (point) {
	case APP_PH_CALIBRATION_PH_4_00:
		return "4.00";
	case APP_PH_CALIBRATION_PH_6_86:
		return "6.86";
	case APP_PH_CALIBRATION_PH_7_00:
		return "7.00";
	case APP_PH_CALIBRATION_PH_9_18:
		return "9.18";
	case APP_PH_CALIBRATION_PH_10_00:
		return "10.00";
	case APP_PH_CALIBRATION_PH_10_01:
		return "10.01";
	default:
		return "unknown";
	}
}

static const char *ph_calibration_result_name(uint8_t result)
{
	if (result == APP_PH_CALIBRATION_SUCCESS) {
		return "success";
	}
	if (result == APP_PH_CALIBRATION_COMMUNICATION_ERROR) {
		return "communication_error";
	}
	return "rejected";
}

static void handle_sensor_status(struct tracked_node *node,
				 const struct app_mesh_message *message)
{
	enum app_sensor_type sensor_type = message->payload[1];
	uint8_t address = message->payload[2];
	enum app_sensor_status status = message->payload[3];
	enum app_sensor_error error = message->payload[4];
	char sensor_id[SENSOR_ID_MAX];
	int index = sensor_index(sensor_type);

	if (index < 0) {
		return;
	}
	format_sensor_id(sensor_id, sizeof(sensor_id), node, sensor_type);
	k_mutex_lock(&node_mutex, K_FOREVER);
	node->sensors[index].known = true;
	node->sensors[index].online = status != APP_SENSOR_STATUS_ERROR;
	node->sensors[index].rs485_address = address;
	k_mutex_unlock(&node_mutex);

	if (status == APP_SENSOR_STATUS_ERROR) {
		emit_json("{\"schema_version\":1,\"type\":\"sensor_error\",\"category\":\"error\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"%s\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"error_code\":\"%s\",\"error_layer\":\"%s\",\"retryable\":true,\"quality\":\"bad\",\"sequence\":%u,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
			  APP_GATEWAY_ID, node->node_id, sensor_id, sensor_type_name(sensor_type),
			  node->mesh_address, address,
			  error == APP_SENSOR_ERROR_INVALID_DATA ? "sensor_invalid_data" :
								   "modbus_timeout",
			  error == APP_SENSOR_ERROR_INVALID_DATA ? "sensor" : "modbus",
			  message->payload[5], (long long)uptime_ms());
	} else {
		emit_json("{\"schema_version\":1,\"type\":\"%s\",\"category\":\"status\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"%s\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"status\":\"ok\",\"quality\":\"unknown\",\"sequence\":%u,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
			  status == APP_SENSOR_STATUS_RECOVERED ? "sensor_recovered" :
								       "sensor_online",
			  APP_GATEWAY_ID, node->node_id, sensor_id, sensor_type_name(sensor_type),
			  node->mesh_address, address, message->payload[5],
			  (long long)uptime_ms());
	}
}

static void handle_ph_report(struct tracked_node *node,
			     const struct app_mesh_message *message)
{
	char sensor_id[SENSOR_ID_MAX];
	char temperature[16];
	char ph[16];
	char millivolts[16];
	uint8_t address = node_sensor_address(node, APP_SENSOR_PH);

	format_sensor_id(sensor_id, sizeof(sensor_id), node, APP_SENSOR_PH);
	format_fixed_1(temperature, sizeof(temperature),
		       (int16_t)sys_get_le16(&message->payload[1]));
	format_fixed_2(ph, sizeof(ph), (int16_t)sys_get_le16(&message->payload[3]));
	format_fixed_1(millivolts, sizeof(millivolts),
		       (int16_t)sys_get_le16(&message->payload[5]));
	emit_json("{\"schema_version\":1,\"type\":\"ph_report\",\"category\":\"telemetry\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"ph\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"sequence\":%u,\"ph\":%s,\"temperature_c\":%s,\"ph_mv\":%s,\"quality\":\"good\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  APP_GATEWAY_ID, node->node_id, sensor_id, node->mesh_address, address,
		  message->payload[7], ph, temperature, millivolts, (long long)uptime_ms());
}

static void handle_do_report(struct tracked_node *node,
			     const struct app_mesh_message *message)
{
	char sensor_id[SENSOR_ID_MAX];
	char dissolved_oxygen[16];
	char temperature[16];
	uint8_t address = node_sensor_address(node, APP_SENSOR_DO);
	uint8_t flags = message->payload[6];
	bool known = (flags & APP_DO_CALIBRATION_STATUS_KNOWN) != 0U;

	format_sensor_id(sensor_id, sizeof(sensor_id), node, APP_SENSOR_DO);
	format_fixed_2(dissolved_oxygen, sizeof(dissolved_oxygen),
		       (int16_t)sys_get_le16(&message->payload[1]));
	format_fixed_1(temperature, sizeof(temperature),
		       (int16_t)sys_get_le16(&message->payload[3]));
	emit_json("{\"schema_version\":1,\"type\":\"do_report\",\"category\":\"telemetry\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"do\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"sequence\":%u,\"dissolved_oxygen_mg_l\":%s,\"temperature_c\":%s,\"saturation_pct\":%u,\"calibration_status_known\":%s,\"air_calibrated\":%s,\"zero_calibrated\":%s,\"quality\":\"%s\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  APP_GATEWAY_ID, node->node_id, sensor_id, node->mesh_address, address,
		  message->payload[7], dissolved_oxygen, temperature, message->payload[5],
		  known ? "true" : "false",
		  known ? (flags & APP_DO_CALIBRATION_AIR_COMPLETE ? "true" : "false") : "null",
		  known ? (flags & APP_DO_CALIBRATION_ZERO_COMPLETE ? "true" : "false") : "null",
		  known ? "good" : "uncertain", (long long)uptime_ms());
}

static void handle_orp_report(struct tracked_node *node,
			      const struct app_mesh_message *message)
{
	char sensor_id[SENSOR_ID_MAX];
	char temperature[16];
	char orp[16];
	char drift[16];
	uint8_t address = node_sensor_address(node, APP_SENSOR_ORP);

	format_sensor_id(sensor_id, sizeof(sensor_id), node, APP_SENSOR_ORP);
	format_fixed_1(temperature, sizeof(temperature),
		       (int16_t)sys_get_le16(&message->payload[1]));
	format_fixed_1(orp, sizeof(orp), (int16_t)sys_get_le16(&message->payload[3]));
	format_fixed_1(drift, sizeof(drift), (int16_t)sys_get_le16(&message->payload[5]));
	emit_json("{\"schema_version\":1,\"type\":\"orp_report\",\"category\":\"telemetry\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"orp\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"sequence\":%u,\"temperature_c\":%s,\"orp_mv\":%s,\"orp_drift_mv\":%s,\"quality\":\"good\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  APP_GATEWAY_ID, node->node_id, sensor_id, node->mesh_address, address,
		  message->payload[7], temperature, orp, drift, (long long)uptime_ms());
}

static void handle_water_level_report(struct tracked_node *node,
				      const struct app_mesh_message *message)
{
	char sensor_id[SENSOR_ID_MAX];
	char value[20];
	int16_t raw_value = (int16_t)sys_get_le16(&message->payload[1]);
	uint8_t decimal_places = message->payload[3];
	uint8_t unit_code = message->payload[4];
	bool unit_valid = unit_code <= APP_TRANSMITTER_UNIT_MMH2O;
	uint8_t address = node_sensor_address(node, APP_SENSOR_WATER_LEVEL);

	format_sensor_id(sensor_id, sizeof(sensor_id), node, APP_SENSOR_WATER_LEVEL);
	format_scaled(value, sizeof(value), raw_value, decimal_places);
	emit_json("{\"schema_version\":1,\"type\":\"water_level_report\",\"category\":\"telemetry\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"sensor_type\":\"water_level\",\"mesh_addr\":\"0x%04x\",\"rs485_address\":%u,\"sequence\":%u,\"value\":%s,\"unit\":\"%s\",\"unit_code\":%u,\"unit_code_namespace\":\"generic_modbus_transmitter_v1\",\"raw_value\":%d,\"decimal_places\":%u,\"unit_valid\":%s,\"quality\":\"%s\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  APP_GATEWAY_ID, node->node_id, sensor_id, node->mesh_address, address,
		  message->payload[5], value, transmitter_unit_name(unit_code), unit_code,
		  raw_value, decimal_places, unit_valid ? "true" : "false",
		  unit_valid ? "good" : "uncertain", (long long)uptime_ms());
}

static void handle_ph_calibration_result(struct tracked_node *node,
					 const struct app_mesh_message *message)
{
	char request_id[REQUEST_ID_MAX];
	char sensor_id[SENSOR_ID_MAX];

	format_sensor_id(sensor_id, sizeof(sensor_id), node, APP_SENSOR_PH);
	snprintk(request_id, sizeof(request_id), "mesh-result-%03u", message->payload[3]);
	k_mutex_lock(&node_mutex, K_FOREVER);
	if (pending_ph.active && pending_ph.mesh_address == message->source &&
	    pending_ph.sequence == message->payload[3]) {
		strncpy(request_id, pending_ph.request_id, sizeof(request_id) - 1U);
		request_id[sizeof(request_id) - 1U] = '\0';
		pending_ph.active = false;
	}
	k_mutex_unlock(&node_mutex);
	emit_json("{\"schema_version\":1,\"type\":\"ph_calibration_result\",\"category\":\"command_result\",\"request_id\":\"%s\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"sensor_id\":\"%s\",\"calibration_point\":\"%s\",\"result\":\"%s\",\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  request_id, APP_GATEWAY_ID, node->node_id, sensor_id,
		  ph_calibration_point_name(message->payload[1]),
		  ph_calibration_result_name(message->payload[2]), (long long)uptime_ms());
}

static void mesh_message_received(const struct app_mesh_message *message)
{
	struct tracked_node *node;

	switch (message->opcode) {
	case APP_OPCODE_NODE_ONLINE:
		if (message->payload[1] != APP_DEVICE_GENERAL_RS485 || message->payload_len < 7U) {
			return;
		}
		node = mark_node_online(message->source, sys_get_le32(&message->payload[3]));
		if (node != NULL &&
		    app_mesh_send_node_online_ack(message->source, APP_DEVICE_GENERAL_RS485,
						  message->payload[2])) {
			emit_json("{\"schema_version\":1,\"type\":\"mesh_send_failed\",\"category\":\"error\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"error_code\":\"online_ack_failed\",\"error_layer\":\"mesh\",\"retryable\":true,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
				  APP_GATEWAY_ID, node->node_id, (long long)uptime_ms());
		}
		break;
	case APP_OPCODE_NODE_HEARTBEAT:
		if (message->payload[1] != APP_DEVICE_GENERAL_RS485) {
			return;
		}
		node = mark_node_seen(message->source);
		if (node != NULL) {
			emit_json("{\"schema_version\":1,\"type\":\"device_heartbeat\",\"category\":\"status\",\"gateway_id\":\"%s\",\"node_id\":\"%s\",\"device_id\":\"%s\",\"mesh_addr\":\"0x%04x\",\"online\":true,\"state_flags\":%u,\"sequence\":%u,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
				  APP_GATEWAY_ID, node->node_id, node->node_id, node->mesh_address,
				  message->payload[2], message->payload[3],
				  (long long)uptime_ms());
		}
		break;
	case APP_OPCODE_SENSOR_STATUS:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_sensor_status(node, message);
		}
		break;
	case APP_OPCODE_PH_REPORT:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_ph_report(node, message);
		}
		break;
	case APP_OPCODE_DO_REPORT:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_do_report(node, message);
		}
		break;
	case APP_OPCODE_ORP_REPORT:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_orp_report(node, message);
		}
		break;
	case APP_OPCODE_WATER_LEVEL_REPORT:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_water_level_report(node, message);
		}
		break;
	case APP_OPCODE_PH_CALIBRATION_RESULT:
		node = mark_node_seen(message->source);
		if (node != NULL) {
			handle_ph_calibration_result(node, message);
		}
		break;
	default:
		break;
	}
}

static void mesh_provisioned(void)
{
	app_led_set(APP_LED_ONLINE);
	atomic_set(&gateway_provisioned, 1);
	emit_gateway_online();
	(void)k_work_reschedule(&gateway_heartbeat_work, K_NO_WAIT);
	(void)k_work_reschedule(&node_liveness_work, K_SECONDS(1));
}

static void mesh_reset(void)
{
	(void)k_work_cancel_delayable(&gateway_heartbeat_work);
	(void)k_work_cancel_delayable(&node_liveness_work);
	atomic_clear(&gateway_provisioned);
	k_mutex_lock(&node_mutex, K_FOREVER);
	memset(nodes, 0, sizeof(nodes));
	memset(&pending_ph, 0, sizeof(pending_ph));
	k_mutex_unlock(&node_mutex);
	app_led_set(APP_LED_UNPROVISIONED);
}

static const struct app_mesh_callbacks mesh_callbacks = {
	.provisioned = mesh_provisioned,
	.reset = mesh_reset,
	.message_received = mesh_message_received,
};

static bool json_read_string(const char *line, const char *key, char *value, size_t value_size)
{
	char token[40];
	const char *start;
	const char *end;

	snprintk(token, sizeof(token), "\"%s\"", key);
	start = strstr(line, token);
	if (start == NULL || (start = strchr(start + strlen(token), ':')) == NULL) {
		return false;
	}
	start++;
	while (isspace((unsigned char)*start)) {
		start++;
	}
	if (*start++ != '\"' || (end = strchr(start, '\"')) == NULL ||
	    (size_t)(end - start) >= value_size) {
		return false;
	}
	memcpy(value, start, end - start);
	value[end - start] = '\0';
	return true;
}

static bool parse_ph_calibration_point(const char *text,
				       enum app_ph_calibration_point *point)
{
	if (strcmp(text, "4.00") == 0) {
		*point = APP_PH_CALIBRATION_PH_4_00;
	} else if (strcmp(text, "6.86") == 0) {
		*point = APP_PH_CALIBRATION_PH_6_86;
	} else if (strcmp(text, "7.00") == 0) {
		*point = APP_PH_CALIBRATION_PH_7_00;
	} else if (strcmp(text, "9.18") == 0) {
		*point = APP_PH_CALIBRATION_PH_9_18;
	} else if (strcmp(text, "10.00") == 0) {
		*point = APP_PH_CALIBRATION_PH_10_00;
	} else if (strcmp(text, "10.01") == 0) {
		*point = APP_PH_CALIBRATION_PH_10_01;
	} else {
		return false;
	}
	return true;
}

static void emit_api_error(const char *request_id, const char *error_code, bool retryable)
{
	emit_json("{\"schema_version\":1,\"type\":\"api_error\",\"category\":\"error\",\"request_id\":\"%s\",\"gateway_id\":\"%s\",\"error_code\":\"%s\",\"error_layer\":\"gateway\",\"retryable\":%s,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  request_id, APP_GATEWAY_ID, error_code, retryable ? "true" : "false",
		  (long long)uptime_ms());
}

static struct tracked_node *online_ph_node_for_sensor_id(const char *requested_sensor_id)
{
	char sensor_id[SENSOR_ID_MAX];
	struct tracked_node *found = NULL;
	int ph_index = sensor_index(APP_SENSOR_PH);

	k_mutex_lock(&node_mutex, K_FOREVER);
	for (size_t index = 0; index < ARRAY_SIZE(nodes); index++) {
		if (!nodes[index].used || !nodes[index].online ||
		    !nodes[index].sensors[ph_index].online) {
			continue;
		}
		format_sensor_id(sensor_id, sizeof(sensor_id), &nodes[index], APP_SENSOR_PH);
		if (strcmp(sensor_id, requested_sensor_id) == 0) {
			found = &nodes[index];
			break;
		}
	}
	k_mutex_unlock(&node_mutex);
	return found;
}

static void process_serial_line(const char *line)
{
	char type[32];
	char request_id[REQUEST_ID_MAX];
	char sensor_id[SENSOR_ID_MAX];
	char point_text[16];
	enum app_ph_calibration_point point;
	struct tracked_node *node;
	uint8_t sequence;
	int err;

	if (!json_read_string(line, "type", type, sizeof(type)) ||
	    !json_read_string(line, "request_id", request_id, sizeof(request_id))) {
		emit_api_error("invalid-request", "invalid_schema", false);
		return;
	}
	if (strcmp(type, "ph_calibrate") != 0 ||
	    !json_read_string(line, "sensor_id", sensor_id, sizeof(sensor_id)) ||
	    !json_read_string(line, "calibration_point", point_text, sizeof(point_text)) ||
	    !parse_ph_calibration_point(point_text, &point)) {
		emit_api_error(request_id, "invalid_command", false);
		return;
	}

	node = online_ph_node_for_sensor_id(sensor_id);
	if (node == NULL) {
		emit_api_error(request_id, "target_offline", true);
		return;
	}

	k_mutex_lock(&node_mutex, K_FOREVER);
	if (pending_ph.active) {
		k_mutex_unlock(&node_mutex);
		emit_api_error(request_id, "command_busy", true);
		return;
	}
	k_mutex_unlock(&node_mutex);

	sequence = command_sequence++;
	k_mutex_lock(&node_mutex, K_FOREVER);
	pending_ph.active = true;
	pending_ph.mesh_address = node->mesh_address;
	pending_ph.sequence = sequence;
	strncpy(pending_ph.request_id, request_id, sizeof(pending_ph.request_id) - 1U);
	pending_ph.request_id[sizeof(pending_ph.request_id) - 1U] = '\0';
	strncpy(pending_ph.sensor_id, sensor_id, sizeof(pending_ph.sensor_id) - 1U);
	pending_ph.sensor_id[sizeof(pending_ph.sensor_id) - 1U] = '\0';
	k_mutex_unlock(&node_mutex);

	err = app_mesh_send_ph_calibration(node->mesh_address, point, sequence);
	if (err) {
		k_mutex_lock(&node_mutex, K_FOREVER);
		pending_ph.active = false;
		k_mutex_unlock(&node_mutex);
		emit_api_error(request_id, "mesh_send_failed", true);
		return;
	}
	emit_json("{\"schema_version\":1,\"type\":\"ph_calibration_accepted\",\"category\":\"command_ack\",\"request_id\":\"%s\",\"gateway_id\":\"%s\",\"sensor_id\":\"%s\",\"accepted\":true,\"uptime_ms\":%lld,\"time_quality\":\"unsynchronized\"}",
		  request_id, APP_GATEWAY_ID, sensor_id, (long long)uptime_ms());
}

static void serial_interrupt_handler(const struct device *device, void *user_data)
{
	uint8_t buffer[32];
	int received;

	ARG_UNUSED(user_data);
	while (uart_irq_update(device) && uart_irq_is_pending(device)) {
		if (!uart_irq_rx_ready(device)) {
			continue;
		}
		received = uart_fifo_read(device, buffer, sizeof(buffer));
		for (int index = 0; index < received; index++) {
			(void)k_msgq_put(&serial_rx_queue, &buffer[index], K_NO_WAIT);
		}
	}
}

static int serial_init(void)
{
	int err;

	if (!device_is_ready(serial)) {
		return -ENODEV;
	}
	err = usb_enable(NULL);
	if (err && err != -EALREADY) {
		return err;
	}
	err = uart_irq_callback_user_data_set(serial, serial_interrupt_handler, NULL);
	if (err) {
		return err;
	}
	uart_irq_rx_enable(serial);
	(void)k_work_reschedule(&serial_connection_work,
				K_MSEC(SERIAL_CONNECTION_POLL_MS));
	return 0;
}

int main(void)
{
	char line[SERIAL_LINE_MAX];
	size_t line_length = 0;
	uint8_t character;

	if (app_led_init() || serial_init()) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}
	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	while (true) {
		if (k_msgq_get(&serial_rx_queue, &character, K_FOREVER)) {
			continue;
		}
		if (character == '\r') {
			continue;
		}
		if (character == '\n') {
			line[line_length] = '\0';
			if (line_length > 0U) {
				process_serial_line(line);
			}
			line_length = 0;
			continue;
		}
		if (line_length + 1U < sizeof(line)) {
			line[line_length++] = character;
		} else {
			line_length = 0;
			emit_api_error("invalid-request", "line_too_long", false);
		}
	}
}
