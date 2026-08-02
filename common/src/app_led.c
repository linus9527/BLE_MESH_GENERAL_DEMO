#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "app_led.h"

#if !DT_NODE_EXISTS(DT_ALIAS(app_red)) || !DT_NODE_EXISTS(DT_ALIAS(app_green)) || \
	!DT_NODE_EXISTS(DT_ALIAS(app_blue))
#error "Each application must provide app-red, app-green, and app-blue aliases."
#endif

static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(DT_ALIAS(app_red), gpios);
static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(app_green), gpios);
static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(DT_ALIAS(app_blue), gpios);

static enum app_led_state current_state = APP_LED_UNPROVISIONED;
static bool blink_phase;

static void led_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(led_work, led_work_handler);

static void set_color(bool red, bool green, bool blue)
{
	(void)gpio_pin_set_dt(&red_led, red);
	(void)gpio_pin_set_dt(&green_led, green);
	(void)gpio_pin_set_dt(&blue_led, blue);
}

static void led_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	blink_phase = !blink_phase;

	switch (current_state) {
	case APP_LED_UNPROVISIONED:
		set_color(false, false, blink_phase);
		(void)k_work_reschedule(&led_work, K_MSEC(1000));
		break;
	case APP_LED_PROVISIONING:
		set_color(false, false, blink_phase);
		(void)k_work_reschedule(&led_work, K_MSEC(250));
		break;
	case APP_LED_WAIT_GATEWAY:
		set_color(false, false, blink_phase);
		(void)k_work_reschedule(&led_work, K_MSEC(700));
		break;
	case APP_LED_WARNING:
		set_color(blink_phase, blink_phase, false);
		(void)k_work_reschedule(&led_work, K_MSEC(600));
		break;
	case APP_LED_ERROR:
		set_color(blink_phase, false, false);
		(void)k_work_reschedule(&led_work, K_MSEC(400));
		break;
	case APP_LED_SAFE_STOP:
		set_color(blink_phase, false, false);
		(void)k_work_reschedule(&led_work, K_MSEC(150));
		break;
	case APP_LED_ONLINE:
		set_color(false, true, false);
		break;
	}
}

int app_led_init(void)
{
	if (!gpio_is_ready_dt(&red_led) || !gpio_is_ready_dt(&green_led) ||
	    !gpio_is_ready_dt(&blue_led)) {
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE) ||
	    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE) ||
	    gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_INACTIVE)) {
		return -EIO;
	}

	app_led_set(APP_LED_UNPROVISIONED);
	return 0;
}

void app_led_set(enum app_led_state state)
{
	current_state = state;
	blink_phase = false;
	(void)k_work_cancel_delayable(&led_work);
	led_work_handler(NULL);
}
