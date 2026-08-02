#ifndef BLE_MESH_APP_NODE_H_
#define BLE_MESH_APP_NODE_H_

#include <stdbool.h>
#include <stdint.h>

#include "app_protocol.h"

struct app_node_config {
	enum app_device_type device_type;
	void (*periodic)(void);
	void (*gateway_timeout)(void);
};

int app_node_init(const struct app_node_config *config);
void app_node_mesh_ready(void);
void app_node_mesh_reset(void);
void app_node_gateway_heartbeat(void);
bool app_node_online_ack(enum app_device_type device_type, uint8_t token);
void app_node_set_warning(bool warning);
uint8_t app_node_state_flags(void);

#endif