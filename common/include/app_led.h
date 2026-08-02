#ifndef BLE_MESH_APP_LED_H_
#define BLE_MESH_APP_LED_H_

enum app_led_state {
	APP_LED_UNPROVISIONED,
	APP_LED_PROVISIONING,
	APP_LED_WAIT_GATEWAY,
	APP_LED_ONLINE,
	APP_LED_WARNING,
	APP_LED_ERROR,
	APP_LED_SAFE_STOP,
};

int app_led_init(void);
void app_led_set(enum app_led_state state);

#endif