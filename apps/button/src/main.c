#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

#define BUTTON_SAMPLE_INTERVAL_MS 10
#define BUTTON_STABLE_SAMPLE_COUNT 5

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(button0), gpios);
static struct gpio_callback button_callback;
static bool button_pressed;
static bool button_candidate_pressed;
static uint8_t button_stable_samples;
static uint8_t sequence;

static void button_debounce_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(button_debounce_work, button_debounce_handler);

static void button_changed(const struct device *port, struct gpio_callback *callback,
			   uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&button_debounce_work, K_MSEC(BUTTON_SAMPLE_INTERVAL_MS));
}

static void button_debounce_handler(struct k_work *work)
{
	bool pressed;
	int value;

	ARG_UNUSED(work);
	value = gpio_pin_get_dt(&button);
	if (value < 0) {
		(void)k_work_reschedule(&button_debounce_work,
				K_MSEC(BUTTON_SAMPLE_INTERVAL_MS));
		return;
	}

	pressed = value > 0;
	if (pressed != button_candidate_pressed) {
		button_candidate_pressed = pressed;
		button_stable_samples = 1U;
	} else if (button_stable_samples < BUTTON_STABLE_SAMPLE_COUNT) {
		button_stable_samples++;
	}

	if (button_stable_samples < BUTTON_STABLE_SAMPLE_COUNT) {
		(void)k_work_reschedule(&button_debounce_work,
				K_MSEC(BUTTON_SAMPLE_INTERVAL_MS));
		return;
	}
	if (button_candidate_pressed == button_pressed) {
		return;
	}

	button_pressed = button_candidate_pressed;
	(void)app_mesh_send_button_event(button_pressed, sequence++);
}

static void send_heartbeat(void)
{
	(void)app_mesh_send_node_heartbeat(APP_DEVICE_BUTTON, app_node_state_flags(), sequence++);
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
	.device_type = APP_DEVICE_BUTTON,
	.periodic = send_heartbeat,
};

static int button_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		return err;
	}

	err = gpio_pin_get_dt(&button);
	if (err < 0) {
		return err;
	}
	button_pressed = err > 0;
	button_candidate_pressed = button_pressed;
	button_stable_samples = BUTTON_STABLE_SAMPLE_COUNT;
	gpio_init_callback(&button_callback, button_changed, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_callback);
	if (err) {
		return err;
	}

	return gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
}

int main(void)
{
	if (app_led_init() || button_init() || app_node_init(&node_config)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}