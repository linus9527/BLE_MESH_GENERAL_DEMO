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
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_protocol.h"

#define SERIAL_LINE_MAX 192
#define SERIAL_RX_QUEUE_SIZE 256

struct tracked_device {
	enum app_device_type type;
	const char *id;
	const char *name;
	bool known;
	bool online;
	uint16_t mesh_address;
	int64_t last_seen_ms;
};

static struct tracked_device devices[] = {
	{ APP_DEVICE_DHT11, "BLE_MESH_DHT11", "DHT11_Node" },
	{ APP_DEVICE_BUTTON, "BLE_MESH_BUTTON", "Button_Node" },
	{ APP_DEVICE_SERVO, "BLE_MESH_SERVO", "Servo_Node" },
};

static const struct device *const serial = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static bool serial_ready;
static uint8_t gateway_sequence;
static uint8_t command_sequence;

K_MSGQ_DEFINE(serial_rx_queue, sizeof(uint8_t), SERIAL_RX_QUEUE_SIZE, 4);
K_MUTEX_DEFINE(serial_tx_mutex);
K_MUTEX_DEFINE(device_mutex);

static void gateway_heartbeat_handler(struct k_work *work);
static void device_liveness_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(gateway_heartbeat_work, gateway_heartbeat_handler);
K_WORK_DELAYABLE_DEFINE(device_liveness_work, device_liveness_handler);

static int64_t timestamp_ms(void)
{
	return k_uptime_get();
}

static void emit_json(const char *format, ...)
{
	char line[256];
	va_list arguments;
	int length;

	if (!serial_ready) {
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

static struct tracked_device *device_for_type(enum app_device_type type)
{
	for (size_t index = 0; index < ARRAY_SIZE(devices); index++) {
		if (devices[index].type == type) {
			return &devices[index];
		}
	}

	return NULL;
}

static bool any_known_device_offline_locked(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(devices); index++) {
		if (devices[index].known && !devices[index].online) {
			return true;
		}
	}

	return false;
}

static void refresh_gateway_led_locked(void)
{
	app_led_set(any_known_device_offline_locked() ? APP_LED_ERROR : APP_LED_ONLINE);
}

static struct tracked_device *mark_device_seen(enum app_device_type type, uint16_t mesh_address)
{
	struct tracked_device *device = device_for_type(type);
	bool first_seen;
	bool recovered;

	if (device == NULL) {
		return NULL;
	}

	k_mutex_lock(&device_mutex, K_FOREVER);
	first_seen = !device->known;
	recovered = device->known && !device->online;
	device->known = true;
	device->online = true;
	device->mesh_address = mesh_address;
	device->last_seen_ms = timestamp_ms();

	if (first_seen || recovered) {
		emit_json("{\"type\":\"device_online\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"reason\":\"%s\",\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address,
			  first_seen ? "boot" : "mesh_recovered", (long long)timestamp_ms());
	}

	refresh_gateway_led_locked();
	k_mutex_unlock(&device_mutex);
	return device;
}

static void gateway_heartbeat_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)app_mesh_send_gateway_heartbeat(gateway_sequence++);
	(void)k_work_reschedule(&gateway_heartbeat_work, K_MSEC(APP_HEARTBEAT_INTERVAL_MS));
}

static void device_liveness_handler(struct k_work *work)
{
	int64_t now = timestamp_ms();

	ARG_UNUSED(work);
	k_mutex_lock(&device_mutex, K_FOREVER);
	for (size_t index = 0; index < ARRAY_SIZE(devices); index++) {
		struct tracked_device *device = &devices[index];

		if (!device->online || now - device->last_seen_ms < APP_OFFLINE_TIMEOUT_MS) {
			continue;
		}

		device->online = false;
		emit_json("{\"type\":\"device_offline\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"timeout_ms\":%d,\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address, APP_OFFLINE_TIMEOUT_MS,
			  (long long)now);
	}

	refresh_gateway_led_locked();
	k_mutex_unlock(&device_mutex);
	(void)k_work_reschedule(&device_liveness_work, K_SECONDS(1));
}

static const char *metric_name(uint8_t metric)
{
	return metric == APP_DHT_METRIC_TEMPERATURE ? "temperature" : "humidity";
}

static const char *alert_state_name(uint8_t state)
{
	return state == APP_ALERT_ENTERED ? "entered" : "cleared";
}

static const char *direction_name(uint8_t direction)
{
	switch (direction) {
	case APP_SERVO_DIRECTION_FORWARD:
		return "forward";
	case APP_SERVO_DIRECTION_REVERSE:
		return "reverse";
	default:
		return "stop";
	}
}

static const char *servo_result_name(uint8_t result)
{
	switch (result) {
	case APP_SERVO_RESULT_EXECUTED:
		return "executed";
	case APP_SERVO_RESULT_STOPPED:
		return "stopped";
	case APP_SERVO_RESULT_SAFE_STOPPED:
		return "safe_stopped";
	default:
		return "invalid";
	}
}

static void mesh_message_received(const struct app_mesh_message *message)
{
	struct tracked_device *device;
	int8_t value;

	switch (message->opcode) {
	case APP_OPCODE_NODE_ONLINE:
		device = mark_device_seen(message->payload[1], message->source);
		if (device == NULL) {
			return;
		}
		if (app_mesh_send_node_online_ack(message->source, message->payload[1],
						  message->payload[2])) {
			emit_json("{\"type\":\"online_ack_failed\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"timestamp_ms\":%lld}",
				  device->id, device->name, device->mesh_address,
				  (long long)timestamp_ms());
		}
		break;
	case APP_OPCODE_NODE_HEARTBEAT:
		device = mark_device_seen(message->payload[1], message->source);
		if (device == NULL) {
			return;
		}
		emit_json("{\"type\":\"device_heartbeat\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"state_flags\":%u,\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address, message->payload[2],
			  (long long)timestamp_ms());
		if (message->payload[1] == APP_DEVICE_DHT11 &&
		    (message->payload[2] & APP_NODE_STATE_SENSOR_ERROR)) {
			emit_json("{\"type\":\"sensor_error\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"error\":\"read_failed\",\"timestamp_ms\":%lld}",
				  device->id, device->name, device->mesh_address,
				  (long long)timestamp_ms());
		}
		break;
	case APP_OPCODE_DHT_REPORT:
		device = mark_device_seen(APP_DEVICE_DHT11, message->source);
		if (device == NULL) {
			return;
		}
		emit_json("{\"type\":\"dht_report\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"temperature_c\":%d,\"humidity_pct\":%u,\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address, (int8_t)message->payload[1],
			  message->payload[2], (long long)timestamp_ms());
		break;
	case APP_OPCODE_DHT_ALERT:
		device = mark_device_seen(APP_DEVICE_DHT11, message->source);
		if (device == NULL) {
			return;
		}
		value = (int8_t)message->payload[3];
		emit_json("{\"type\":\"dht_alert\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"metric\":\"%s\",\"state\":\"%s\",\"value\":%d,\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address, metric_name(message->payload[1]),
			  alert_state_name(message->payload[2]), value, (long long)timestamp_ms());
		break;
	case APP_OPCODE_BUTTON_EVENT:
		device = mark_device_seen(APP_DEVICE_BUTTON, message->source);
		if (device == NULL) {
			return;
		}
		emit_json("{\"type\":\"button_event\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"state\":\"%s\",\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address,
			  message->payload[1] ? "pressed" : "released", (long long)timestamp_ms());
		break;
	case APP_OPCODE_SERVO_RESULT:
		device = mark_device_seen(APP_DEVICE_SERVO, message->source);
		if (device == NULL) {
			return;
		}
		emit_json("{\"type\":\"servo_result\",\"device_id\":\"%s\",\"device_name\":\"%s\",\"mesh_addr\":\"0x%04x\",\"result\":\"%s\",\"direction\":\"%s\",\"speed_pct\":%u,\"timestamp_ms\":%lld}",
			  device->id, device->name, device->mesh_address,
			  servo_result_name(message->payload[1]), direction_name(message->payload[2]),
			  message->payload[3], (long long)timestamp_ms());
		break;
	default:
		break;
	}
}

static void mesh_provisioned(void)
{
	app_led_set(APP_LED_ONLINE);
	emit_json("{\"type\":\"gateway_online\",\"device_id\":\"BLE_MESH_GATEWAY\",\"device_name\":\"Gateway_Node\",\"timestamp_ms\":%lld}",
		  (long long)timestamp_ms());
	(void)k_work_reschedule(&gateway_heartbeat_work, K_NO_WAIT);
	(void)k_work_reschedule(&device_liveness_work, K_SECONDS(1));
}

static void mesh_reset(void)
{
	(void)k_work_cancel_delayable(&gateway_heartbeat_work);
	(void)k_work_cancel_delayable(&device_liveness_work);
	k_mutex_lock(&device_mutex, K_FOREVER);
	for (size_t index = 0; index < ARRAY_SIZE(devices); index++) {
		devices[index].known = false;
		devices[index].online = false;
		devices[index].mesh_address = BT_MESH_ADDR_UNASSIGNED;
		devices[index].last_seen_ms = 0;
	}
	k_mutex_unlock(&device_mutex);
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

static bool json_read_int(const char *line, const char *key, long *value)
{
	char token[40];
	char *end;
	const char *start;

	snprintk(token, sizeof(token), "\"%s\"", key);
	start = strstr(line, token);
	if (start == NULL || (start = strchr(start + strlen(token), ':')) == NULL) {
		return false;
	}

	start++;
	while (isspace((unsigned char)*start)) {
		start++;
	}
	*value = strtol(start, &end, 10);
	return end != start;
}

static void emit_invalid_command(const char *reason)
{
	emit_json("{\"type\":\"invalid_command\",\"reason\":\"%s\",\"timestamp_ms\":%lld}",
		  reason, (long long)timestamp_ms());
}

static void process_serial_line(const char *line)
{
	char type[32];
	char device_id[32];
	char direction[16];
	struct tracked_device *servo = device_for_type(APP_DEVICE_SERVO);
	bool servo_online;
	long value;
	int err;

	if (!json_read_string(line, "type", type, sizeof(type)) ||
	    !json_read_string(line, "device_id", device_id, sizeof(device_id)) ||
	    strcmp(device_id, "BLE_MESH_SERVO") != 0) {
		emit_invalid_command("invalid_schema");
		return;
	}
	k_mutex_lock(&device_mutex, K_FOREVER);
	servo_online = servo != NULL && servo->online;
	k_mutex_unlock(&device_mutex);
	if (!servo_online) {
		emit_json("{\"type\":\"node_offline\",\"device_id\":\"BLE_MESH_SERVO\",\"device_name\":\"Servo_Node\",\"timestamp_ms\":%lld}",
			  (long long)timestamp_ms());
		return;
	}

	if (strcmp(type, "servo_command") == 0) {
		enum app_servo_direction servo_direction;

		if (!json_read_string(line, "direction", direction, sizeof(direction)) ||
		    !json_read_int(line, "speed_pct", &value) || value < 0 || value > 100) {
			emit_invalid_command("invalid_servo_command");
			return;
		}

		if (strcmp(direction, "forward") == 0) {
			servo_direction = APP_SERVO_DIRECTION_FORWARD;
		} else if (strcmp(direction, "reverse") == 0) {
			servo_direction = APP_SERVO_DIRECTION_REVERSE;
		} else if (strcmp(direction, "stop") == 0 && value == 0) {
			servo_direction = APP_SERVO_DIRECTION_STOP;
		} else {
			emit_invalid_command("invalid_direction");
			return;
		}

		err = app_mesh_send_servo_command(servo_direction == APP_SERVO_DIRECTION_STOP ?
					 APP_SERVO_COMMAND_STOP : APP_SERVO_COMMAND_RUN,
					 servo_direction, value, command_sequence++);
		if (err) {
			emit_json("{\"type\":\"mesh_send_failed\",\"device_id\":\"BLE_MESH_SERVO\",\"error\":%d,\"timestamp_ms\":%lld}",
				  err, (long long)timestamp_ms());
			return;
		}

		emit_json("{\"type\":\"servo_command_accepted\",\"device_id\":\"BLE_MESH_SERVO\",\"direction\":\"%s\",\"speed_pct\":%ld,\"timestamp_ms\":%lld}",
			  direction, value, (long long)timestamp_ms());
		return;
	}

	if (strcmp(type, "servo_calibrate_stop") == 0) {
		if (!json_read_int(line, "stop_pulse_us", &value) || value < 1300 || value > 1700) {
			emit_invalid_command("invalid_stop_pulse_us");
			return;
		}

		err = app_mesh_send_servo_calibration(value, command_sequence++);
		if (err) {
			emit_json("{\"type\":\"mesh_send_failed\",\"device_id\":\"BLE_MESH_SERVO\",\"error\":%d,\"timestamp_ms\":%lld}",
				  err, (long long)timestamp_ms());
			return;
		}

		emit_json("{\"type\":\"servo_calibration_accepted\",\"device_id\":\"BLE_MESH_SERVO\",\"stop_pulse_us\":%ld,\"timestamp_ms\":%lld}",
			  value, (long long)timestamp_ms());
		return;
	}

	emit_invalid_command("unsupported_type");
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
	if (!device_is_ready(serial)) {
		return -ENODEV;
	}

	serial_ready = true;
	uart_irq_callback_user_data_set(serial, serial_interrupt_handler, NULL);
	uart_irq_rx_enable(serial);
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
			emit_invalid_command("line_too_long");
		}
	}
}
