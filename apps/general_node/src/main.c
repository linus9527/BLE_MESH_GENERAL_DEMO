#include <stdbool.h>
#include <stdint.h>

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"
#include "app_rs485_config.h"

#define PH_FIRST_REG                  0x0000U
#define PH_REG_COUNT                  3U
#define PH_CALIBRATION_REG            0x0007U
#define DO_MEASUREMENT_FIRST_REG      0x2001U
#define DO_MEASUREMENT_REG_COUNT      6U
#define DO_CALIBRATION_STATUS_REG     0x200fU
#define ORP_TEMPERATURE_REG           0x0000U
#define ORP_VALUE_FIRST_REG           0x0009U
#define ORP_VALUE_REG_COUNT           2U
#define TRANSMITTER_UNIT_FIRST_REG    0x0002U
#define TRANSMITTER_UNIT_REG_COUNT    3U
#define GENERAL_THREAD_STACK_SIZE     4096
#define GENERAL_THREAD_PRIORITY       7
#define GENERAL_WORKER_TICK_MS        100

#define GENERAL_MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

enum sensor_runtime_state {
	SENSOR_STATE_UNKNOWN,
	SENSOR_STATE_ABSENT,
	SENSOR_STATE_ONLINE,
	SENSOR_STATE_LOST,
};

struct sensor_slot {
	enum app_sensor_type type;
	uint8_t address;
	enum sensor_runtime_state state;
	uint8_t failure_count;
	int64_t next_action_ms;
};

struct calibration_task {
	enum app_ph_calibration_point point;
	uint8_t sequence;
};

static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = APP_RS485_TIMEOUT_US,
	.serial = {
	.baud = APP_RS485_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits_client = UART_CFG_STOP_BITS_1,
	},
};

static struct sensor_slot sensors[] = {
	{ .type = APP_SENSOR_PH, .address = APP_RS485_PH_ADDRESS },
	{ .type = APP_SENSOR_DO, .address = APP_RS485_DO_ADDRESS },
	{ .type = APP_SENSOR_ORP, .address = APP_RS485_ORP_ADDRESS },
	{ .type = APP_SENSOR_WATER_LEVEL, .address = APP_RS485_WATER_LEVEL_ADDRESS },
};

static int modbus_iface = -1;
static bool modbus_ready;
static uint8_t mesh_sequence;
static atomic_t mesh_ready;
static atomic_t announce_inventory;
static struct k_thread general_thread;
K_THREAD_STACK_DEFINE(general_thread_stack, GENERAL_THREAD_STACK_SIZE);
K_MSGQ_DEFINE(calibration_queue, sizeof(struct calibration_task), 4, 4);
K_SEM_DEFINE(worker_wakeup, 0, 1);

static int init_modbus_client(void)
{
	const char *iface_name = DEVICE_DT_NAME(GENERAL_MODBUS_NODE);

	modbus_iface = modbus_iface_get_by_name(iface_name);
	if (modbus_iface < 0) {
		return modbus_iface;
	}

	return modbus_init_client(modbus_iface, modbus_client_param);
}

static int read_ph(const struct sensor_slot *sensor)
{
	uint16_t registers[PH_REG_COUNT];
	int16_t temperature_x10;
	int16_t ph_x100;
	int16_t ph_mv_x10;
	int err;

	err = modbus_read_holding_regs(modbus_iface, sensor->address, PH_FIRST_REG,
				       registers, ARRAY_SIZE(registers));
	if (err) {
		return err;
	}

	temperature_x10 = (int16_t)registers[0];
	ph_x100 = (int16_t)registers[1];
	ph_mv_x10 = (int16_t)registers[2];
	if (temperature_x10 < -200 || temperature_x10 > 600 || ph_x100 < 0 ||
	    ph_x100 > 1400 || ph_mv_x10 < -10000 || ph_mv_x10 > 10000) {
		return -ERANGE;
	}

	if (atomic_get(&mesh_ready)) {
		(void)app_mesh_send_ph_report(temperature_x10, ph_x100, ph_mv_x10,
					  mesh_sequence++);
	}
	return 0;
}

static int read_do(const struct sensor_slot *sensor)
{
	uint16_t registers[DO_MEASUREMENT_REG_COUNT];
	uint16_t calibration_status;
	uint16_t dissolved_oxygen_x100;
	int16_t temperature_x10;
	uint8_t saturation_pct;
	uint8_t calibration_flags = 0U;
	int err;

	err = modbus_read_holding_regs(modbus_iface, sensor->address,
				       DO_MEASUREMENT_FIRST_REG, registers,
				       ARRAY_SIZE(registers));
	if (err) {
		return err;
	}

	dissolved_oxygen_x100 = registers[0];
	temperature_x10 = (int16_t)registers[2];
	if (dissolved_oxygen_x100 > 2000U || temperature_x10 < 0 ||
	    temperature_x10 > 500 || registers[4] > 200U) {
		return -ERANGE;
	}
	saturation_pct = (uint8_t)registers[4];

	err = modbus_read_holding_regs(modbus_iface, sensor->address,
				       DO_CALIBRATION_STATUS_REG, &calibration_status, 1U);
	if (!err) {
		calibration_flags = APP_DO_CALIBRATION_STATUS_KNOWN |
			(calibration_status & (APP_DO_CALIBRATION_AIR_COMPLETE |
					       APP_DO_CALIBRATION_ZERO_COMPLETE));
	}

	if (atomic_get(&mesh_ready)) {
		(void)app_mesh_send_do_report(dissolved_oxygen_x100, temperature_x10,
					  saturation_pct, calibration_flags, mesh_sequence++);
	}
	return 0;
}

static int read_orp(const struct sensor_slot *sensor)
{
	uint16_t temperature_register;
	uint16_t value_registers[ORP_VALUE_REG_COUNT];
	int16_t temperature_x10;
	int16_t orp_x10;
	int16_t drift_x10;
	int err;

	err = modbus_read_holding_regs(modbus_iface, sensor->address,
				       ORP_TEMPERATURE_REG, &temperature_register, 1U);
	if (err) {
		return err;
	}
	err = modbus_read_holding_regs(modbus_iface, sensor->address,
				       ORP_VALUE_FIRST_REG, value_registers,
				       ARRAY_SIZE(value_registers));
	if (err) {
		return err;
	}

	temperature_x10 = (int16_t)temperature_register;
	orp_x10 = (int16_t)value_registers[0];
	drift_x10 = (int16_t)value_registers[1];
	if (temperature_x10 < -200 || temperature_x10 > 600 || orp_x10 < -10000 ||
	    orp_x10 > 10000 || drift_x10 < -10000 || drift_x10 > 10000) {
		return -ERANGE;
	}

	if (atomic_get(&mesh_ready)) {
		(void)app_mesh_send_orp_report(temperature_x10, orp_x10, drift_x10,
					   mesh_sequence++);
	}
	return 0;
}

static int read_water_level(const struct sensor_slot *sensor)
{
	uint16_t registers[TRANSMITTER_UNIT_REG_COUNT];
	uint8_t unit_code;
	uint8_t decimal_places;
	int16_t raw_value;
	int err;

	err = modbus_read_holding_regs(modbus_iface, sensor->address,
				       TRANSMITTER_UNIT_FIRST_REG, registers,
				       ARRAY_SIZE(registers));
	if (err) {
		return err;
	}

	unit_code = (uint8_t)registers[0];
	decimal_places = (uint8_t)registers[1];
	raw_value = (int16_t)registers[2];
	if (registers[0] > APP_TRANSMITTER_UNIT_MMH2O || registers[1] > 3U) {
		return -ERANGE;
	}

	if (atomic_get(&mesh_ready)) {
		(void)app_mesh_send_water_level_report(raw_value, decimal_places, unit_code,
						    mesh_sequence++);
	}
	return 0;
}

static int read_sensor(const struct sensor_slot *sensor)
{
	switch (sensor->type) {
	case APP_SENSOR_PH:
		return read_ph(sensor);
	case APP_SENSOR_DO:
		return read_do(sensor);
	case APP_SENSOR_ORP:
		return read_orp(sensor);
	case APP_SENSOR_WATER_LEVEL:
		return read_water_level(sensor);
	default:
		return -EINVAL;
	}
}

static bool any_sensor_lost(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(sensors); index++) {
		if (sensors[index].state == SENSOR_STATE_LOST) {
			return true;
		}
	}
	return false;
}

static void update_sensor_error_led(void)
{
	app_node_set_sensor_error(!modbus_ready || any_sensor_lost());
}

static enum app_sensor_error sensor_error_from_errno(int err)
{
	return err == -ERANGE ? APP_SENSOR_ERROR_INVALID_DATA : APP_SENSOR_ERROR_MODBUS;
}

static void send_sensor_status(const struct sensor_slot *sensor,
			       enum app_sensor_status status, enum app_sensor_error error)
{
	if (!atomic_get(&mesh_ready)) {
		return;
	}
	(void)app_mesh_send_sensor_status(sensor->type, sensor->address, status, error,
					  mesh_sequence++);
}

static void service_sensor(struct sensor_slot *sensor, int64_t now)
{
	enum sensor_runtime_state previous_state = sensor->state;
	int err = read_sensor(sensor);

	if (!err) {
		sensor->state = SENSOR_STATE_ONLINE;
		sensor->failure_count = 0U;
		sensor->next_action_ms = now + APP_SENSOR_REPORT_INTERVAL_MS;
		if (previous_state != SENSOR_STATE_ONLINE) {
			send_sensor_status(sensor,
				previous_state == SENSOR_STATE_LOST ? APP_SENSOR_STATUS_RECOVERED :
								      APP_SENSOR_STATUS_ONLINE,
				APP_SENSOR_ERROR_NONE);
		}
		update_sensor_error_led();
		return;
	}

	if (previous_state == SENSOR_STATE_ONLINE) {
		sensor->failure_count++;
		if (sensor->failure_count < APP_SENSOR_FAILURE_LIMIT) {
			sensor->next_action_ms = now + APP_SENSOR_REPORT_INTERVAL_MS;
			return;
		}
		sensor->state = SENSOR_STATE_LOST;
		send_sensor_status(sensor, APP_SENSOR_STATUS_ERROR,
				   sensor_error_from_errno(err));
		update_sensor_error_led();
	} else if (previous_state == SENSOR_STATE_UNKNOWN) {
		sensor->state = SENSOR_STATE_ABSENT;
	}

	sensor->next_action_ms = now + APP_SENSOR_REPROBE_INTERVAL_MS;
}

static int ph_calibration_command(enum app_ph_calibration_point point, uint16_t *command)
{
	switch (point) {
	case APP_PH_CALIBRATION_PH_4_00:
		*command = 0x000b;
		break;
	case APP_PH_CALIBRATION_PH_6_86:
		*command = 0x000c;
		break;
	case APP_PH_CALIBRATION_PH_7_00:
		*command = 0x000d;
		break;
	case APP_PH_CALIBRATION_PH_9_18:
		*command = 0x000e;
		break;
	case APP_PH_CALIBRATION_PH_10_00:
		*command = 0x000f;
		break;
	case APP_PH_CALIBRATION_PH_10_01:
		*command = 0x0010;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void process_calibration(const struct calibration_task *task)
{
	enum app_ph_calibration_result result = APP_PH_CALIBRATION_SUCCESS;
	uint16_t command;

	if (ph_calibration_command(task->point, &command)) {
		result = APP_PH_CALIBRATION_REJECTED;
	} else if (!modbus_ready ||
		   modbus_write_holding_reg(modbus_iface, APP_RS485_PH_ADDRESS,
					    PH_CALIBRATION_REG, command)) {
		result = APP_PH_CALIBRATION_COMMUNICATION_ERROR;
	}
	(void)app_mesh_send_ph_calibration_result(task->point, result, task->sequence);
}

static void announce_detected_sensors(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(sensors); index++) {
		if (sensors[index].state == SENSOR_STATE_ONLINE) {
			send_sensor_status(&sensors[index], APP_SENSOR_STATUS_ONLINE,
					   APP_SENSOR_ERROR_NONE);
			sensors[index].next_action_ms = 0;
		} else if (sensors[index].state == SENSOR_STATE_LOST) {
			send_sensor_status(&sensors[index], APP_SENSOR_STATUS_ERROR,
					   APP_SENSOR_ERROR_MODBUS);
		}
	}
}

static void general_thread_entry(void *unused1, void *unused2, void *unused3)
{
	struct calibration_task calibration;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		if (!k_msgq_get(&calibration_queue, &calibration, K_NO_WAIT)) {
			process_calibration(&calibration);
		}
		if (atomic_cas(&announce_inventory, 1, 0)) {
			announce_detected_sensors();
		}
		if (modbus_ready) {
			int64_t now = k_uptime_get();

			for (size_t index = 0; index < ARRAY_SIZE(sensors); index++) {
				if (now >= sensors[index].next_action_ms) {
					service_sensor(&sensors[index], now);
					break;
				}
			}
		}
		(void)k_sem_take(&worker_wakeup, K_MSEC(GENERAL_WORKER_TICK_MS));
	}
}

static void send_heartbeat(void)
{
	(void)app_mesh_send_node_heartbeat(APP_DEVICE_GENERAL_RS485, app_node_state_flags(),
					   mesh_sequence++);
}

static void mesh_provisioned(void)
{
	atomic_set(&mesh_ready, 1);
	atomic_set(&announce_inventory, 1);
	app_node_mesh_ready();
	k_sem_give(&worker_wakeup);
}

static void mesh_reset(void)
{
	atomic_clear(&mesh_ready);
	app_node_mesh_reset();
}

static void mesh_message_received(const struct app_mesh_message *message)
{
	switch (message->opcode) {
	case APP_OPCODE_GATEWAY_HEARTBEAT:
		app_node_gateway_heartbeat();
		break;
	case APP_OPCODE_NODE_ONLINE_ACK:
		(void)app_node_online_ack(message->payload[1], message->payload[2]);
		break;
	case APP_OPCODE_PH_CALIBRATE: {
		const struct calibration_task task = {
			.point = message->payload[1],
			.sequence = message->payload[2],
		};

		if (k_msgq_put(&calibration_queue, &task, K_NO_WAIT)) {
			(void)app_mesh_send_ph_calibration_result(
				task.point, APP_PH_CALIBRATION_REJECTED, task.sequence);
		} else {
			k_sem_give(&worker_wakeup);
		}
		break;
	}
	default:
		break;
	}
}

static const struct app_mesh_callbacks mesh_callbacks = {
	.provisioned = mesh_provisioned,
	.reset = mesh_reset,
	.message_received = mesh_message_received,
};

static const struct app_node_config node_config = {
	.device_type = APP_DEVICE_GENERAL_RS485,
	.periodic = send_heartbeat,
	.periodic_interval_ms = APP_NODE_HEARTBEAT_INTERVAL_MS,
};

int main(void)
{
	if (app_led_init() || app_node_init(&node_config)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	modbus_ready = init_modbus_client() == 0;
	update_sensor_error_led();
	(void)k_thread_create(&general_thread, general_thread_stack,
			      K_THREAD_STACK_SIZEOF(general_thread_stack),
			      general_thread_entry, NULL, NULL, NULL,
			      K_PRIO_PREEMPT(GENERAL_THREAD_PRIORITY), 0, K_NO_WAIT);
	(void)k_thread_name_set(&general_thread, "general_modbus");

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}
	return 0;
}
