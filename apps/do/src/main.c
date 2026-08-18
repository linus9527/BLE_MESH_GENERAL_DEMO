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

#define DO_MODBUS_ADDRESS                1U
#define DO_MODBUS_MEASUREMENT_FIRST_REG  0x2001U
#define DO_MODBUS_MEASUREMENT_REG_COUNT  6U
#define DO_MODBUS_CALIBRATION_STATUS_REG 0x200fU
#define DO_MODBUS_TIMEOUT_US             300000U
#define DO_MAX_X100                      2000U
#define DO_TEMPERATURE_MIN_X10           0
#define DO_TEMPERATURE_MAX_X10           500
#define DO_SATURATION_MAX_PCT            200U
#define DO_THREAD_STACK_SIZE             2048
#define DO_THREAD_PRIORITY               7

#define DO_MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

static const struct modbus_iface_param modbus_client_param = {
	.mode = MODBUS_MODE_RTU,
	.rx_timeout = DO_MODBUS_TIMEOUT_US,
	.serial = {
		.baud = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits_client = UART_CFG_STOP_BITS_1,
	},
};

static int modbus_iface = -1;
static bool modbus_ready;
static uint8_t sequence;
static struct k_thread do_thread;
K_THREAD_STACK_DEFINE(do_thread_stack, DO_THREAD_STACK_SIZE);
K_SEM_DEFINE(sample_request, 0, 1);

static int init_modbus_client(void)
{
	const char *iface_name = DEVICE_DT_NAME(DO_MODBUS_NODE);

	modbus_iface = modbus_iface_get_by_name(iface_name);
	if (modbus_iface < 0) {
		return modbus_iface;
	}

	return modbus_init_client(modbus_iface, modbus_client_param);
}

static int read_do_values(uint16_t *dissolved_oxygen_x100, int16_t *temperature_x10,
			  uint8_t *saturation_pct, uint8_t *calibration_flags)
{
	uint16_t registers[DO_MODBUS_MEASUREMENT_REG_COUNT];
	uint16_t calibration_status;
	int err;

	if (!modbus_ready) {
		return -ENODEV;
	}

	err = modbus_read_holding_regs(modbus_iface, DO_MODBUS_ADDRESS,
				       DO_MODBUS_MEASUREMENT_FIRST_REG, registers,
				       ARRAY_SIZE(registers));
	if (err) {
		return err;
	}

	*dissolved_oxygen_x100 = registers[0];
	*temperature_x10 = (int16_t)registers[2];
	*saturation_pct = (uint8_t)registers[4];
	*calibration_flags = 0U;
	err = modbus_read_holding_regs(modbus_iface, DO_MODBUS_ADDRESS,
				       DO_MODBUS_CALIBRATION_STATUS_REG, &calibration_status, 1U);
	if (!err) {
		*calibration_flags = APP_DO_CALIBRATION_STATUS_KNOWN |
			(calibration_status & (APP_DO_CALIBRATION_AIR_COMPLETE |
					       APP_DO_CALIBRATION_ZERO_COMPLETE));
	}

	if (*dissolved_oxygen_x100 > DO_MAX_X100 ||
	    *temperature_x10 < DO_TEMPERATURE_MIN_X10 ||
	    *temperature_x10 > DO_TEMPERATURE_MAX_X10 ||
	    registers[4] > DO_SATURATION_MAX_PCT) {
		return -ERANGE;
	}

	return 0;
}

static void sample_do(void)
{
	uint16_t dissolved_oxygen_x100;
	int16_t temperature_x10;
	uint8_t saturation_pct;
	uint8_t calibration_flags;

	if (read_do_values(&dissolved_oxygen_x100, &temperature_x10, &saturation_pct,
			   &calibration_flags)) {
		app_node_set_sensor_error(true);
		(void)app_mesh_send_node_heartbeat(APP_DEVICE_DO, app_node_state_flags(),
						 sequence++);
		return;
	}

	app_node_set_sensor_error(false);
	app_node_set_warning(
		(calibration_flags & APP_DO_CALIBRATION_STATUS_KNOWN) != 0U &&
		(calibration_flags & APP_DO_CALIBRATION_AIR_COMPLETE) == 0U);
	(void)app_mesh_send_do_report(dissolved_oxygen_x100, temperature_x10, saturation_pct,
				      calibration_flags, sequence++);
}

static void do_thread_entry(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		k_sem_take(&sample_request, K_FOREVER);
		sample_do();
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
	.device_type = APP_DEVICE_DO,
	.periodic = request_sample,
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
	(void)k_thread_create(&do_thread, do_thread_stack, K_THREAD_STACK_SIZEOF(do_thread_stack),
			      do_thread_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(DO_THREAD_PRIORITY),
			      0, K_NO_WAIT);
	(void)k_thread_name_set(&do_thread, "do_modbus");

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}
