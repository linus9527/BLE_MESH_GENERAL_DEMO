#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include "app_mesh.h"

static const struct app_mesh_callbacks *registered_callbacks;

static uint8_t device_uuid[16] = {
	0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
};

static int dispatch_message(uint8_t opcode, const struct bt_mesh_msg_ctx *ctx,
			    struct net_buf_simple *buf)
{
	struct app_mesh_message message = {
		.opcode = opcode,
		.source = ctx->addr,
		.payload_len = buf->len,
	};

	if (message.payload_len > sizeof(message.payload)) {
		return -EMSGSIZE;
	}
	if (message.payload_len == 0U || buf->data[0] != APP_PROTOCOL_VERSION) {
		return -EPROTO;
	}

	memcpy(message.payload, buf->data, message.payload_len);
	if (registered_callbacks != NULL && registered_callbacks->message_received != NULL) {
		registered_callbacks->message_received(&message);
	}

	return 0;
}

#define APP_MESSAGE_HANDLER(name, code) \
	static int name(const struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx, \
			struct net_buf_simple *buf) \
	{ \
		ARG_UNUSED(model); \
		return dispatch_message(code, ctx, buf); \
	}

APP_MESSAGE_HANDLER(node_heartbeat_received, APP_OPCODE_NODE_HEARTBEAT)
APP_MESSAGE_HANDLER(dht_report_received, APP_OPCODE_DHT_REPORT)
APP_MESSAGE_HANDLER(dht_alert_received, APP_OPCODE_DHT_ALERT)
APP_MESSAGE_HANDLER(button_event_received, APP_OPCODE_BUTTON_EVENT)
APP_MESSAGE_HANDLER(servo_command_received, APP_OPCODE_SERVO_COMMAND)
APP_MESSAGE_HANDLER(servo_result_received, APP_OPCODE_SERVO_RESULT)
APP_MESSAGE_HANDLER(gateway_heartbeat_received, APP_OPCODE_GATEWAY_HEARTBEAT)
APP_MESSAGE_HANDLER(servo_calibration_received, APP_OPCODE_SERVO_CALIBRATE_STOP)
APP_MESSAGE_HANDLER(node_online_received, APP_OPCODE_NODE_ONLINE)
APP_MESSAGE_HANDLER(node_online_ack_received, APP_OPCODE_NODE_ONLINE_ACK)
APP_MESSAGE_HANDLER(ph_report_received, APP_OPCODE_PH_REPORT)
APP_MESSAGE_HANDLER(ph_calibrate_received, APP_OPCODE_PH_CALIBRATE)
APP_MESSAGE_HANDLER(ph_calibration_result_received, APP_OPCODE_PH_CALIBRATION_RESULT)

static const struct bt_mesh_model_op application_model_ops[] = {
	{ APP_MESH_OP(APP_OPCODE_NODE_HEARTBEAT), BT_MESH_LEN_EXACT(4), node_heartbeat_received },
	{ APP_MESH_OP(APP_OPCODE_DHT_REPORT), BT_MESH_LEN_EXACT(4), dht_report_received },
	{ APP_MESH_OP(APP_OPCODE_DHT_ALERT), BT_MESH_LEN_EXACT(5), dht_alert_received },
	{ APP_MESH_OP(APP_OPCODE_BUTTON_EVENT), BT_MESH_LEN_EXACT(3), button_event_received },
	{ APP_MESH_OP(APP_OPCODE_SERVO_COMMAND), BT_MESH_LEN_EXACT(5), servo_command_received },
	{ APP_MESH_OP(APP_OPCODE_SERVO_RESULT), BT_MESH_LEN_EXACT(5), servo_result_received },
	{ APP_MESH_OP(APP_OPCODE_GATEWAY_HEARTBEAT), BT_MESH_LEN_EXACT(2), gateway_heartbeat_received },
	{ APP_MESH_OP(APP_OPCODE_SERVO_CALIBRATE_STOP), BT_MESH_LEN_EXACT(4), servo_calibration_received },
	{ APP_MESH_OP(APP_OPCODE_NODE_ONLINE), BT_MESH_LEN_EXACT(3), node_online_received },
	{ APP_MESH_OP(APP_OPCODE_NODE_ONLINE_ACK), BT_MESH_LEN_EXACT(3), node_online_ack_received },
	{ APP_MESH_OP(APP_OPCODE_PH_REPORT), BT_MESH_LEN_EXACT(8), ph_report_received },
	{ APP_MESH_OP(APP_OPCODE_PH_CALIBRATE), BT_MESH_LEN_EXACT(3), ph_calibrate_received },
	{ APP_MESH_OP(APP_OPCODE_PH_CALIBRATION_RESULT), BT_MESH_LEN_EXACT(4),
	  ph_calibration_result_received },
	BT_MESH_MODEL_OP_END,
};

BT_MESH_MODEL_PUB_DEFINE(application_model_pub, NULL, APP_MESH_MAX_PAYLOAD);

static struct bt_mesh_model sig_models[] = {
	BT_MESH_MODEL_CFG_SRV,
};

static struct bt_mesh_model vendor_models[] = {
	BT_MESH_MODEL_VND(APP_VENDOR_COMPANY_ID, APP_VENDOR_MODEL_ID, application_model_ops,
			  &application_model_pub, NULL),
};

static const struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, sig_models, vendor_models),
};

static const struct bt_mesh_comp composition = {
	.cid = APP_VENDOR_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void provisioning_complete(uint16_t net_idx, uint16_t address)
{
	ARG_UNUSED(net_idx);
	ARG_UNUSED(address);

	if (registered_callbacks != NULL && registered_callbacks->provisioned != NULL) {
		registered_callbacks->provisioned();
	}
}

static void provisioning_reset(void)
{
	if (registered_callbacks != NULL && registered_callbacks->reset != NULL) {
		registered_callbacks->reset();
	}

	(void)bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static const struct bt_mesh_prov provisioning = {
	.uuid = device_uuid,
	.complete = provisioning_complete,
	.reset = provisioning_reset,
};

static uint32_t mesh_opcode(enum app_opcode opcode)
{
	return APP_MESH_OP(opcode);
}

static int send_message(enum app_opcode opcode, uint16_t destination, const uint8_t *payload,
			ssize_t payload_len)
{
	struct bt_mesh_msg_ctx context = {
		.app_idx = vendor_models[0].keys[0],
		.addr = destination,
		.send_ttl = BT_MESH_TTL_DEFAULT,
	};

	if (!bt_mesh_is_provisioned()) {
		return -EAGAIN;
	}
	if (context.app_idx == BT_MESH_KEY_UNUSED) {
		return -EACCES;
	}
	if (payload_len < 0 || payload_len > APP_MESH_MAX_PAYLOAD) {
		return -EMSGSIZE;
	}

	BT_MESH_MODEL_BUF_DEFINE(buffer, APP_MESH_OP(APP_OPCODE_SERVO_CALIBRATE_STOP),
			 APP_MESH_MAX_PAYLOAD);
	bt_mesh_model_msg_init(&buffer, mesh_opcode(opcode));
	net_buf_simple_add_mem(&buffer, payload, payload_len);

	return bt_mesh_model_send(&vendor_models[0], &context, &buffer, NULL, NULL);
}

int app_mesh_init(const struct app_mesh_callbacks *callbacks)
{
	int err;

	registered_callbacks = callbacks;
	(void)hwinfo_get_device_id(&device_uuid[8], sizeof(device_uuid) - 8U);

	err = bt_enable(NULL);
	if (err) {
		return err;
	}

	err = bt_mesh_init(&provisioning, &composition);
	if (err) {
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err) {
			return err;
		}
	}

	if (bt_mesh_is_provisioned()) {
		if (registered_callbacks != NULL && registered_callbacks->provisioned != NULL) {
			registered_callbacks->provisioned();
		}
		return 0;
	}

	return bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

bool app_mesh_is_provisioned(void)
{
	return bt_mesh_is_provisioned();
}

int app_mesh_send_node_online(enum app_device_type device_type, uint8_t token)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, device_type, token };

	return send_message(APP_OPCODE_NODE_ONLINE, APP_GROUP_NODE_STATUS, payload,
			    sizeof(payload));
}

int app_mesh_send_node_online_ack(uint16_t destination, enum app_device_type device_type,
				  uint8_t token)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, device_type, token };

	return send_message(APP_OPCODE_NODE_ONLINE_ACK, destination, payload, sizeof(payload));
}

int app_mesh_send_node_heartbeat(enum app_device_type device_type, uint8_t state_flags,
				 uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, device_type, state_flags, sequence };

	return send_message(APP_OPCODE_NODE_HEARTBEAT, APP_GROUP_NODE_STATUS, payload,
			    sizeof(payload));
}

int app_mesh_send_dht_report(int8_t temperature_c, uint8_t humidity_pct, uint8_t sequence)
{
	const uint8_t payload[] = {
		APP_PROTOCOL_VERSION,
		(uint8_t)temperature_c,
		humidity_pct,
		sequence,
	};

	return send_message(APP_OPCODE_DHT_REPORT, APP_GROUP_NODE_STATUS, payload, sizeof(payload));
}

int app_mesh_send_dht_alert(enum app_dht_metric metric, enum app_alert_state state,
				 int8_t value, uint8_t sequence)
{
	const uint8_t payload[] = {
		APP_PROTOCOL_VERSION,
		metric,
		state,
		(uint8_t)value,
		sequence,
	};

	return send_message(APP_OPCODE_DHT_ALERT, APP_GROUP_NODE_STATUS, payload, sizeof(payload));
}

int app_mesh_send_button_event(bool pressed, uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, pressed ? 1U : 0U, sequence };

	return send_message(APP_OPCODE_BUTTON_EVENT, APP_GROUP_NODE_STATUS, payload, sizeof(payload));
}

int app_mesh_send_servo_command(enum app_servo_command command,
				enum app_servo_direction direction, uint8_t speed_pct,
				uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, command, direction, speed_pct, sequence };

	return send_message(APP_OPCODE_SERVO_COMMAND, APP_GROUP_SERVO_CONTROL, payload,
			    sizeof(payload));
}

int app_mesh_send_servo_result(enum app_servo_result result,
			       enum app_servo_direction direction, uint8_t speed_pct,
			       uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, result, direction, speed_pct, sequence };

	return send_message(APP_OPCODE_SERVO_RESULT, APP_GROUP_NODE_STATUS, payload, sizeof(payload));
}

int app_mesh_send_gateway_heartbeat(uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, sequence };

	return send_message(APP_OPCODE_GATEWAY_HEARTBEAT, APP_GROUP_GATEWAY_HEARTBEAT,
			    payload, sizeof(payload));
}

int app_mesh_send_servo_calibration(uint16_t stop_pulse_us, uint8_t sequence)
{
	uint8_t payload[] = { APP_PROTOCOL_VERSION, 0, 0, sequence };

	sys_put_le16(stop_pulse_us, &payload[1]);
	return send_message(APP_OPCODE_SERVO_CALIBRATE_STOP, APP_GROUP_SERVO_CONTROL, payload,
			    sizeof(payload));
}

int app_mesh_send_ph_report(int16_t temperature_x10, int16_t ph_x100, int16_t ph_mv_x10,
			    uint8_t sequence)
{
	uint8_t payload[8] = { APP_PROTOCOL_VERSION };

	sys_put_le16((uint16_t)temperature_x10, &payload[1]);
	sys_put_le16((uint16_t)ph_x100, &payload[3]);
	sys_put_le16((uint16_t)ph_mv_x10, &payload[5]);
	payload[7] = sequence;
	return send_message(APP_OPCODE_PH_REPORT, APP_GROUP_NODE_STATUS, payload, sizeof(payload));
}

int app_mesh_send_ph_calibration(uint16_t destination,
			 enum app_ph_calibration_point point, uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, point, sequence };

	return send_message(APP_OPCODE_PH_CALIBRATE, destination, payload, sizeof(payload));
}

int app_mesh_send_ph_calibration_result(enum app_ph_calibration_point point,
					enum app_ph_calibration_result result,
					uint8_t sequence)
{
	const uint8_t payload[] = { APP_PROTOCOL_VERSION, point, result, sequence };

	return send_message(APP_OPCODE_PH_CALIBRATION_RESULT, APP_GROUP_NODE_STATUS, payload,
			    sizeof(payload));
}
