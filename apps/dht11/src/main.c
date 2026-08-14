#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

#define TEMPERATURE_MIN_C 30
#define TEMPERATURE_MAX_C 35
#define HUMIDITY_MIN_PCT  60
#define HUMIDITY_MAX_PCT  80

static const struct device *const dht11 = DEVICE_DT_GET(DT_ALIAS(dht11));
static bool dht11_ready;
static bool temperature_alert;
static bool humidity_alert;
static uint8_t sequence;

static void send_alert_if_changed(enum app_dht_metric metric, bool *active, bool out_of_range,
				  int8_t value)
{
	if (*active == out_of_range) {
		return;
	}

	*active = out_of_range;
	(void)app_mesh_send_dht_alert(metric,
		out_of_range ? APP_ALERT_ENTERED : APP_ALERT_CLEARED, value, sequence++);
}

static void report_dht11(void)
{
	struct sensor_value temperature;
	struct sensor_value humidity;
	int8_t temperature_c;
	uint8_t humidity_pct;
	bool temperature_out_of_range;
	bool humidity_out_of_range;

	if (!dht11_ready || sensor_sample_fetch(dht11) ||
	    sensor_channel_get(dht11, SENSOR_CHAN_AMBIENT_TEMP, &temperature) ||
	    sensor_channel_get(dht11, SENSOR_CHAN_HUMIDITY, &humidity)) {
		app_node_set_sensor_error(true);
		(void)app_mesh_send_node_heartbeat(APP_DEVICE_DHT11,
			app_node_state_flags(), sequence++);
		return;
	}
	app_node_set_sensor_error(false);

	temperature_c = CLAMP(temperature.val1, INT8_MIN, INT8_MAX);
	humidity_pct = CLAMP(humidity.val1, 0, UINT8_MAX);
	temperature_out_of_range = temperature_c < TEMPERATURE_MIN_C ||
				   temperature_c > TEMPERATURE_MAX_C;
	humidity_out_of_range = humidity_pct < HUMIDITY_MIN_PCT || humidity_pct > HUMIDITY_MAX_PCT;

	app_node_set_warning(temperature_out_of_range || humidity_out_of_range);
	(void)app_mesh_send_dht_report(temperature_c, humidity_pct, sequence++);
	send_alert_if_changed(APP_DHT_METRIC_TEMPERATURE, &temperature_alert,
			      temperature_out_of_range, temperature_c);
	send_alert_if_changed(APP_DHT_METRIC_HUMIDITY, &humidity_alert, humidity_out_of_range,
			      (int8_t)humidity_pct);
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
		app_node_online_ack(message->payload[1], message->payload[2]);
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
	.device_type = APP_DEVICE_DHT11,
	.periodic = report_dht11,
};

int main(void)
{
	if (app_led_init()) {
		return 0;
	}

	dht11_ready = device_is_ready(dht11);
	if (app_node_init(&node_config)) {
		app_led_set(APP_LED_ERROR);
		return 0;
	}

	if (app_mesh_init(&mesh_callbacks)) {
		app_led_set(APP_LED_ERROR);
	}

	return 0;
}
