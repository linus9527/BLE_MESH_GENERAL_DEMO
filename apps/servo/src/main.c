#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

#define SERVO_DEFAULT_STOP_US 1500
#define SERVO_MIN_STOP_US     1300
#define SERVO_MAX_STOP_US     1700
#define SERVO_SPEED_RANGE_US  500

static const struct pwm_dt_spec servo_pwm = PWM_DT_SPEC_GET(DT_ALIAS(servo_pwm));
static uint16_t stop_pulse_us = SERVO_DEFAULT_STOP_US;
static enum app_servo_direction current_direction = APP_SERVO_DIRECTION_STOP;
static uint8_t current_speed;
static uint8_t sequence;
static bool pwm_ready;

static int servo_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			      void *cb_arg)
{
	if (strcmp(name, "stop_pulse_us") != 0 || len != sizeof(stop_pulse_us)) {
		return -ENOENT;
	}

	if (read_cb(cb_arg, &stop_pulse_us, sizeof(stop_pulse_us)) != sizeof(stop_pulse_us)) {
		return -EIO;
	}

	if (stop_pulse_us < SERVO_MIN_STOP_US || stop_pulse_us > SERVO_MAX_STOP_US) {
		stop_pulse_us = SERVO_DEFAULT_STOP_US;
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(servo, "servo", NULL, servo_settings_set, NULL, NULL);

static int set_servo(enum app_servo_direction direction, uint8_t speed_pct)
{
	uint32_t pulse_us;
	uint32_t offset_us;

	if (!pwm_ready) {
		return -ENODEV;
	}

	if (direction == APP_SERVO_DIRECTION_STOP || speed_pct == 0U) {
		direction = APP_SERVO_DIRECTION_STOP;
		speed_pct = 0U;
		pulse_us = stop_pulse_us;
	} else if (speed_pct > 100U ||
		   (direction != APP_SERVO_DIRECTION_FORWARD &&
		    direction != APP_SERVO_DIRECTION_REVERSE)) {
		return -EINVAL;
	} else {
		offset_us = (SERVO_SPEED_RANGE_US * speed_pct) / 100U;
		pulse_us = direction == APP_SERVO_DIRECTION_FORWARD ? stop_pulse_us + offset_us :
							    stop_pulse_us - offset_us;
	}

	if (pwm_set_pulse_dt(&servo_pwm, PWM_USEC(pulse_us))) {
		return -EIO;
	}

	current_direction = direction;
	current_speed = speed_pct;
	return 0;
}

static void send_heartbeat(void)
{
	(void)app_mesh_send_node_heartbeat(APP_DEVICE_SERVO, app_node_state_flags(), sequence++);
}

static void gateway_timeout(void)
{
	(void)set_servo(APP_SERVO_DIRECTION_STOP, 0);
	app_led_set(APP_LED_SAFE_STOP);
	(void)app_mesh_send_servo_result(APP_SERVO_RESULT_SAFE_STOPPED,
					APP_SERVO_DIRECTION_STOP, 0, sequence++);
}

static void handle_servo_command(const struct app_mesh_message *message)
{
	enum app_servo_command command = message->payload[1];
	enum app_servo_direction direction = message->payload[2];
	uint8_t speed_pct = message->payload[3];
	uint8_t command_sequence = message->payload[4];
	int err;

	if (command == APP_SERVO_COMMAND_STOP) {
		direction = APP_SERVO_DIRECTION_STOP;
		speed_pct = 0;
	}

	err = command == APP_SERVO_COMMAND_RUN || command == APP_SERVO_COMMAND_STOP ?
		set_servo(direction, speed_pct) : -EINVAL;
	if (err) {
		(void)app_mesh_send_servo_result(APP_SERVO_RESULT_INVALID, current_direction,
						 current_speed, command_sequence);
		return;
	}

	(void)app_mesh_send_servo_result(direction == APP_SERVO_DIRECTION_STOP ?
					 APP_SERVO_RESULT_STOPPED : APP_SERVO_RESULT_EXECUTED,
					 direction, speed_pct, command_sequence);
}

static void handle_servo_calibration(const struct app_mesh_message *message)
{
	uint16_t requested_stop_pulse_us = sys_get_le16(&message->payload[1]);
	uint8_t command_sequence = message->payload[3];

	if (requested_stop_pulse_us < SERVO_MIN_STOP_US ||
	    requested_stop_pulse_us > SERVO_MAX_STOP_US) {
		(void)app_mesh_send_servo_result(APP_SERVO_RESULT_INVALID, current_direction,
						 current_speed, command_sequence);
		return;
	}

	stop_pulse_us = requested_stop_pulse_us;
	(void)settings_save_one("servo/stop_pulse_us", &stop_pulse_us, sizeof(stop_pulse_us));
	(void)set_servo(APP_SERVO_DIRECTION_STOP, 0);
	(void)app_mesh_send_servo_result(APP_SERVO_RESULT_STOPPED,
					APP_SERVO_DIRECTION_STOP, 0, command_sequence);
}

static void mesh_provisioned(void)
{
	app_node_set_warning(set_servo(APP_SERVO_DIRECTION_STOP, 0) != 0);
	app_node_mesh_ready();
}

static void mesh_reset(void)
{
	(void)set_servo(APP_SERVO_DIRECTION_STOP, 0);
	app_node_mesh_reset();
}

static void mesh_message_received(const struct app_mesh_message *message)
{
	switch (message->opcode) {
	case APP_OPCODE_GATEWAY_HEARTBEAT:
		app_node_gateway_heartbeat();
		break;
	case APP_OPCODE_NODE_ONLINE_ACK:
		app_node_online_ack(message->payload[1], message->payload[2]);
		break;
	case APP_OPCODE_SERVO_COMMAND:
		handle_servo_command(message);
		break;
	case APP_OPCODE_SERVO_CALIBRATE_STOP:
		handle_servo_calibration(message);
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
	.device_type = APP_DEVICE_SERVO,
	.periodic = send_heartbeat,
	.gateway_timeout = gateway_timeout,
	.periodic_interval_ms = APP_NODE_HEARTBEAT_INTERVAL_MS,
};

int main(void)
{
	if (app_led_init() || app_node_init(&node_config)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	pwm_ready = pwm_is_ready_dt(&servo_pwm);
	if (!pwm_ready || set_servo(APP_SERVO_DIRECTION_STOP, 0)) {
		app_node_set_warning(true);
	}

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}
