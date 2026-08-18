#include <stdbool.h>
#include <stdint.h>

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/sys/util.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

#define ORP_MODBUS_ADDRESS          1U
#define ORP_MODBUS_TEMPERATURE_REG  0U
#define ORP_MODBUS_VALUE_FIRST_REG  9U
#define ORP_MODBUS_VALUE_REG_COUNT  2U
#define ORP_MODBUS_TIMEOUT_US       300000U
#define ORP_TEMPERATURE_MIN_X10     -200
#define ORP_TEMPERATURE_MAX_X10     600
#define ORP_VALUE_MIN_X10           -10000
#define ORP_VALUE_MAX_X10           10000
#define ORP_DRIFT_MIN_X10           -10000
#define ORP_DRIFT_MAX_X10           10000
#define ORP_THREAD_STACK_SIZE       2048
#define ORP_THREAD_PRIORITY         7

#define ORP_MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = ORP_MODBUS_TIMEOUT_US,
	.serial = {
		.baud = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits_client = UART_CFG_STOP_BITS_1,
	},
};

static int modbus_iface = -1;
static bool modbus_ready;
static uint8_t sequence;
static struct k_thread orp_thread;
K_THREAD_STACK_DEFINE(orp_thread_stack, ORP_THREAD_STACK_SIZE);
K_SEM_DEFINE(sample_request, 0, 1);

static int init_modbus_client(void)
{
	const char *iface_name = DEVICE_DT_NAME(ORP_MODBUS_NODE);

	modbus_iface = modbus_iface_get_by_name(iface_name);
	if (modbus_iface < 0) {
		return modbus_iface;
	}

	return modbus_init_client(modbus_iface, modbus_client_param);
}

static int read_orp_values(int16_t *temperature_x10, int16_t *orp_x10,
			   int16_t *drift_x10)
{
	uint16_t temperature_register;
	uint16_t value_registers[ORP_MODBUS_VALUE_REG_COUNT];
	int err;

	if (!modbus_ready) {
		return -ENODEV;
	}

	err = modbus_read_holding_regs(modbus_iface, ORP_MODBUS_ADDRESS,
				       ORP_MODBUS_TEMPERATURE_REG, &temperature_register, 1U);
	if (err) {
		return err;
	}

	err = modbus_read_holding_regs(modbus_iface, ORP_MODBUS_ADDRESS,
				       ORP_MODBUS_VALUE_FIRST_REG, value_registers,
				       ARRAY_SIZE(value_registers));
	if (err) {
		return err;
	}

	*temperature_x10 = (int16_t)temperature_register;
	*orp_x10 = (int16_t)value_registers[0];
	*drift_x10 = (int16_t)value_registers[1];
	if (*temperature_x10 < ORP_TEMPERATURE_MIN_X10 ||
	    *temperature_x10 > ORP_TEMPERATURE_MAX_X10 ||
	    *orp_x10 < ORP_VALUE_MIN_X10 || *orp_x10 > ORP_VALUE_MAX_X10 ||
	    *drift_x10 < ORP_DRIFT_MIN_X10 || *drift_x10 > ORP_DRIFT_MAX_X10) {
		return -ERANGE;
	}

	return 0;
}

static void sample_orp(void)
{
	int16_t temperature_x10;
	int16_t orp_x10;
	int16_t drift_x10;

	if (read_orp_values(&temperature_x10, &orp_x10, &drift_x10)) {
		app_node_set_sensor_error(true);
		(void)app_mesh_send_node_heartbeat(APP_DEVICE_ORP, app_node_state_flags(),
						 sequence++);
		return;
	}

	app_node_set_sensor_error(false);
	(void)app_mesh_send_orp_report(temperature_x10, orp_x10, drift_x10, sequence++);
}

static void orp_thread_entry(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		k_sem_take(&sample_request, K_FOREVER);
		sample_orp();
	}
}

static void request_sample(void)
{
	k_sem_give(&sample_request);
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
	.device_type = APP_DEVICE_ORP,
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
	app_node_set_sensor_error(!modbus_ready);
	(void)k_thread_create(&orp_thread, orp_thread_stack,
			      K_THREAD_STACK_SIZEOF(orp_thread_stack), orp_thread_entry,
			      NULL, NULL, NULL, K_PRIO_PREEMPT(ORP_THREAD_PRIORITY), 0,
			      K_NO_WAIT);
	(void)k_thread_name_set(&orp_thread, "orp_modbus");

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}
