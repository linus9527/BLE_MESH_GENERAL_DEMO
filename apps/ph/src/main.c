#include <stdbool.h>
#include <stdint.h>

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

#define PH_MODBUS_ADDRESS    1U
#define PH_MODBUS_FIRST_REG  0U
#define PH_MODBUS_REG_COUNT  3U
#define PH_MODBUS_CALIB_REG  7U
#define PH_MODBUS_TIMEOUT_US 300000U
#define PH_THREAD_STACK_SIZE 2048
#define PH_THREAD_PRIORITY   7

#define PH_MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

enum ph_task_type {
	PH_TASK_SAMPLE,
	PH_TASK_CALIBRATE,
};

struct ph_task {
	enum ph_task_type type;
	enum app_ph_calibration_point point;
	uint8_t sequence;
};

static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = PH_MODBUS_TIMEOUT_US,
	.serial = {
		.baud = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits_client = UART_CFG_STOP_BITS_1,
	},
};

static int modbus_iface = -1;
static bool modbus_ready;
static uint8_t sequence;
static struct k_thread ph_thread;
K_THREAD_STACK_DEFINE(ph_thread_stack, PH_THREAD_STACK_SIZE);
K_MSGQ_DEFINE(ph_task_queue, sizeof(struct ph_task), 4, 4);

static void print_modbus_read_request(void)
{
	uint8_t frame[8] = {
		PH_MODBUS_ADDRESS,
		0x03,
	};
	uint16_t crc;

	sys_put_be16(PH_MODBUS_FIRST_REG, &frame[2]);
	sys_put_be16(PH_MODBUS_REG_COUNT, &frame[4]);
	crc = crc16_ansi(frame, 6);
	sys_put_le16(crc, &frame[6]);

	printk("[PH][MODBUS TX]");
	for (size_t index = 0; index < ARRAY_SIZE(frame); index++) {
		printk(" %02X", frame[index]);
	}
	printk("\r\n");
}

static int init_modbus_client(void)
{
	const char *iface_name = DEVICE_DT_NAME(PH_MODBUS_NODE);

	modbus_iface = modbus_iface_get_by_name(iface_name);
	if (modbus_iface < 0) {
		return modbus_iface;
	}

	return modbus_init_client(modbus_iface, modbus_client_param);
}

static int read_ph_values(int16_t *temperature_x10, int16_t *ph_x100, int16_t *ph_mv_x10)
{
	uint16_t registers[PH_MODBUS_REG_COUNT];
	int err;

	if (!modbus_ready) {
		return -ENODEV;
	}

	print_modbus_read_request();
	err = modbus_read_holding_regs(modbus_iface, PH_MODBUS_ADDRESS, PH_MODBUS_FIRST_REG,
				       registers, ARRAY_SIZE(registers));
	printk("[PH][MODBUS RESULT] err=%d\r\n", err);
	if (err) {
		return err;
	}
	printk("[PH][MODBUS RX REG] temperature=0x%04X ph=0x%04X mv=0x%04X\r\n",
	       registers[0], registers[1], registers[2]);

	*temperature_x10 = (int16_t)registers[0];
	*ph_x100 = (int16_t)registers[1];
	*ph_mv_x10 = (int16_t)registers[2];
	if (*temperature_x10 < -200 || *temperature_x10 > 600 ||
	    *ph_x100 < 0 || *ph_x100 > 1400 ||
	    *ph_mv_x10 < -10000 || *ph_mv_x10 > 10000) {
		return -ERANGE;
	}

	return 0;
}

static int calibration_command(enum app_ph_calibration_point point, uint16_t *command)
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

static enum app_ph_calibration_result calibrate_ph(enum app_ph_calibration_point point)
{
	uint16_t command;

	if (calibration_command(point, &command)) {
		return APP_PH_CALIBRATION_REJECTED;
	}
	if (!modbus_ready ||
	    modbus_write_holding_reg(modbus_iface, PH_MODBUS_ADDRESS, PH_MODBUS_CALIB_REG,
				     command)) {
		return APP_PH_CALIBRATION_COMMUNICATION_ERROR;
	}

	return APP_PH_CALIBRATION_SUCCESS;
}

static void sample_ph(void)
{
	int16_t temperature_x10;
	int16_t ph_x100;
	int16_t ph_mv_x10;

	if (read_ph_values(&temperature_x10, &ph_x100, &ph_mv_x10)) {
		app_node_set_sensor_error(true);
		(void)app_mesh_send_node_heartbeat(APP_DEVICE_PH, app_node_state_flags(), sequence++);
		return;
	}

	app_node_set_sensor_error(false);
	(void)app_mesh_send_ph_report(temperature_x10, ph_x100, ph_mv_x10, sequence++);
}

static void ph_thread_entry(void *unused1, void *unused2, void *unused3)
{
	struct ph_task task;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		if (k_msgq_get(&ph_task_queue, &task, K_FOREVER)) {
			continue;
		}

		if (task.type == PH_TASK_SAMPLE) {
			sample_ph();
		} else {
			enum app_ph_calibration_result result = calibrate_ph(task.point);

			app_node_set_sensor_error(result == APP_PH_CALIBRATION_COMMUNICATION_ERROR);
			(void)app_mesh_send_ph_calibration_result(task.point, result, task.sequence);
		}
	}
}

static void request_sample(void)
{
	const struct ph_task task = { .type = PH_TASK_SAMPLE };

	(void)k_msgq_put(&ph_task_queue, &task, K_NO_WAIT);
}

static void mesh_provisioned(void)
{
	app_node_mesh_ready();
}

static void mesh_reset(void)
{
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
		const struct ph_task task = {
			.type = PH_TASK_CALIBRATE,
			.point = message->payload[1],
			.sequence = message->payload[2],
		};

		if (k_msgq_put(&ph_task_queue, &task, K_NO_WAIT)) {
			(void)app_mesh_send_ph_calibration_result(
				task.point, APP_PH_CALIBRATION_REJECTED, task.sequence);
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
	.device_type = APP_DEVICE_PH,
	.periodic = request_sample,
	.periodic_interval_ms = APP_SENSOR_REPORT_INTERVAL_MS,
};

int main(void)
{
	int modbus_err;

	if (app_led_init() || app_node_init(&node_config)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	modbus_err = init_modbus_client();
	modbus_ready = modbus_err == 0;
	printk("[PH][MODBUS INIT] iface=%d err=%d ready=%u\r\n", modbus_iface, modbus_err,
	       modbus_ready ? 1U : 0U);
	app_node_set_sensor_error(!modbus_ready);
	(void)k_thread_create(&ph_thread, ph_thread_stack, K_THREAD_STACK_SIZEOF(ph_thread_stack),
			      ph_thread_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(PH_THREAD_PRIORITY),
			      0, K_NO_WAIT);
	(void)k_thread_name_set(&ph_thread, "ph_modbus");

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}
