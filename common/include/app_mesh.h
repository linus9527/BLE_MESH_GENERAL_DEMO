#ifndef BLE_MESH_APP_MESH_H_
#define BLE_MESH_APP_MESH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_protocol.h"

struct app_mesh_message {
	uint8_t opcode;
	uint16_t source;
	uint8_t payload[APP_MESH_MAX_PAYLOAD];
	size_t payload_len;
};

struct app_mesh_callbacks {
	void (*provisioned)(void);
	void (*reset)(void);
	void (*message_received)(const struct app_mesh_message *message);
};

int app_mesh_init(const struct app_mesh_callbacks *callbacks);
bool app_mesh_is_provisioned(void);

int app_mesh_send_node_online(enum app_device_type device_type, uint8_t token);
int app_mesh_send_node_online_ack(uint16_t destination, enum app_device_type device_type,
				  uint8_t token);
int app_mesh_send_node_heartbeat(enum app_device_type device_type, uint8_t state_flags,
				 uint8_t sequence);
int app_mesh_send_dht_report(int8_t temperature_c, uint8_t humidity_pct, uint8_t sequence);
int app_mesh_send_dht_alert(enum app_dht_metric metric, enum app_alert_state state,
				 int8_t value, uint8_t sequence);
int app_mesh_send_button_event(bool pressed, uint8_t sequence);
int app_mesh_send_servo_command(enum app_servo_command command,
				enum app_servo_direction direction, uint8_t speed_pct,
				uint8_t sequence);
int app_mesh_send_servo_result(enum app_servo_result result,
			       enum app_servo_direction direction, uint8_t speed_pct,
			       uint8_t sequence);
int app_mesh_send_gateway_heartbeat(uint8_t sequence);
int app_mesh_send_servo_calibration(uint16_t stop_pulse_us, uint8_t sequence);
int app_mesh_send_ph_report(int16_t temperature_x10, int16_t ph_x100, int16_t ph_mv_x10,
			    uint8_t sequence);
int app_mesh_send_ph_calibration(uint16_t destination,
			 enum app_ph_calibration_point point, uint8_t sequence);
int app_mesh_send_ph_calibration_result(enum app_ph_calibration_point point,
					enum app_ph_calibration_result result,
					uint8_t sequence);

#endif
