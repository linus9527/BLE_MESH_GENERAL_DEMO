#include <errno.h>

#include <zephyr/kernel.h>

#include "app_led.h"
#include "app_mesh.h"
#include "app_node.h"

static struct app_node_config node_config;
static bool mesh_ready;
static bool gateway_reachable;
static bool gateway_heartbeat_expired;
static bool online_acknowledged;
static bool warning_active;
static uint8_t online_token;

static void periodic_work_handler(struct k_work *work);
static void gateway_watchdog_handler(struct k_work *work);
static void online_notify_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(periodic_work, periodic_work_handler);
K_WORK_DELAYABLE_DEFINE(gateway_watchdog, gateway_watchdog_handler);
K_WORK_DELAYABLE_DEFINE(online_notify_work, online_notify_handler);

static void update_led(void)
{
	if (!mesh_ready) {
		app_led_set(APP_LED_UNPROVISIONED);
		return;
	}

	if (gateway_heartbeat_expired) {
		app_led_set(APP_LED_ERROR);
		return;
	}

	if (!gateway_reachable || !online_acknowledged) {
		app_led_set(APP_LED_WAIT_GATEWAY);
		return;
	}

	app_led_set(warning_active ? APP_LED_WARNING : APP_LED_ONLINE);
}

static void periodic_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (node_config.periodic != NULL) {
		node_config.periodic();
	}

	(void)k_work_reschedule(&periodic_work, K_MSEC(APP_HEARTBEAT_INTERVAL_MS));
}

static void gateway_watchdog_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!gateway_heartbeat_expired) {
		gateway_reachable = false;
		gateway_heartbeat_expired = true;
		online_acknowledged = false;
		online_token++;
		update_led();
		(void)k_work_reschedule(&online_notify_work, K_NO_WAIT);
		if (node_config.gateway_timeout != NULL) {
			node_config.gateway_timeout();
		}
	}
}

static void online_notify_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!mesh_ready || online_acknowledged) {
		return;
	}

	(void)app_mesh_send_node_online(node_config.device_type, online_token);
	(void)k_work_reschedule(&online_notify_work, K_MSEC(APP_ONLINE_NOTIFY_RETRY_MS));
}

int app_node_init(const struct app_node_config *config)
{
	if (config == NULL || config->periodic == NULL) {
		return -EINVAL;
	}

	node_config = *config;
	return 0;
}

void app_node_mesh_ready(void)
{
	mesh_ready = true;
	gateway_reachable = false;
	gateway_heartbeat_expired = false;
	online_acknowledged = false;
	online_token++;
	update_led();
	(void)k_work_reschedule(&online_notify_work, K_NO_WAIT);
	(void)k_work_reschedule(&periodic_work, K_NO_WAIT);
	(void)k_work_reschedule(&gateway_watchdog, K_MSEC(APP_OFFLINE_TIMEOUT_MS));
}

void app_node_mesh_reset(void)
{
	mesh_ready = false;
	gateway_reachable = false;
	gateway_heartbeat_expired = false;
	online_acknowledged = false;
	warning_active = false;
	(void)k_work_cancel_delayable(&online_notify_work);
	(void)k_work_cancel_delayable(&periodic_work);
	(void)k_work_cancel_delayable(&gateway_watchdog);
	update_led();
}

void app_node_gateway_heartbeat(void)
{
	bool was_reachable = gateway_reachable;

	gateway_reachable = true;
	gateway_heartbeat_expired = false;
	update_led();
	if (!was_reachable && !online_acknowledged) {
		(void)k_work_reschedule(&online_notify_work, K_NO_WAIT);
	}
	(void)k_work_reschedule(&gateway_watchdog, K_MSEC(APP_OFFLINE_TIMEOUT_MS));
}

bool app_node_online_ack(enum app_device_type device_type, uint8_t token)
{
	bool first_acknowledgement;

	if (!mesh_ready || device_type != node_config.device_type || token != online_token) {
		return false;
	}

	first_acknowledgement = !online_acknowledged;
	online_acknowledged = true;
	gateway_reachable = true;
	gateway_heartbeat_expired = false;
	(void)k_work_cancel_delayable(&online_notify_work);
	(void)k_work_reschedule(&gateway_watchdog, K_MSEC(APP_OFFLINE_TIMEOUT_MS));
	update_led();
	return first_acknowledgement;
}

void app_node_set_warning(bool warning)
{
	warning_active = warning;
	update_led();
}

uint8_t app_node_state_flags(void)
{
	uint8_t flags = 0;

	if (app_mesh_is_provisioned()) {
		flags |= APP_NODE_STATE_PROVISIONED;
	}
	if (gateway_reachable) {
		flags |= APP_NODE_STATE_GATEWAY_REACHABLE;
	}
	if (warning_active) {
		flags |= APP_NODE_STATE_ALERT;
	}

	return flags;
}