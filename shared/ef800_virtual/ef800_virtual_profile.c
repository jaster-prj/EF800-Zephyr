/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_virtual_profile.h"

#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/sys/util.h>

#include "ef800_virtual_serial_peers.h"

#define PROFILE_DEVICE_SERIAL_1 1U
#define PROFILE_DEVICE_SERIAL_2 2U
#define PROFILE_DEVICE_RTC 3U
#define PROFILE_DEVICE_BATTERY 4U
#define PROFILE_DEVICE_STORAGE 5U
#define PROFILE_DEVICE_LCD 6U
#define PROFILE_DEVICE_MCP 7U
#define PROFILE_OP_SET_AVAILABILITY 1U
#define PROFILE_OP_SET_DELAY 2U
#define PROFILE_OP_SET_ERROR 3U
#define PROFILE_OP_SET_RESPONSE 4U
#define PROFILE_OP_SET_DUMMY 5U
#define PROFILE_MAX_RESPONSE 128U

struct profile_state {
	uint8_t response[PROFILE_MAX_RESPONSE];
	size_t response_length;
	int32_t delay_ms;
	int32_t error;
	bool available;
};

static struct profile_state serial_profiles[2] = {
	{ .available = true }, { .available = true },
};
static bool device_available[8] = {false, true, true, true, true, true, true, true};
static uint8_t battery_soc = 80U;
static uint32_t battery_voltage_uv = 3800000U;

static uint32_t read_u32(const uint8_t *value)
{
	return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
		((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

int ef800_virtual_profile_apply(const uint8_t *payload, size_t length)
{
	struct profile_state *state;
	uint8_t device;
	uint8_t operation;

	if (payload == NULL || length < 2U || length > 2U + 4U + 1U + PROFILE_MAX_RESPONSE) {
		return -EINVAL;
	}
	device = payload[0];
	operation = payload[1];
	if (device < PROFILE_DEVICE_SERIAL_1 || device > PROFILE_DEVICE_MCP) {
		return -ENODEV;
	}
	if (device >= PROFILE_DEVICE_RTC) {
		if (device == PROFILE_DEVICE_BATTERY && operation == 5U && length == 7U) {
			battery_soc = payload[2] > 100U ? 100U : payload[2];
			battery_voltage_uv = read_u32(&payload[3]);
			return 0;
		}
		if (operation != PROFILE_OP_SET_AVAILABILITY || length != 3U || payload[2] > 1U) {
			return -ENOTSUP;
		}
		device_available[device] = payload[2] != 0U;
		return 0;
	}
	state = &serial_profiles[device - 1U];

	switch (operation) {
	case PROFILE_OP_SET_DUMMY:
		if (length != 3U || payload[2] > 2U) return -EINVAL;
		return ef800_virtual_serial_peer_set_profile(device, payload[2]);
	case PROFILE_OP_SET_AVAILABILITY:
		if (length != 3U || payload[2] > 1U) return -EINVAL;
		state->available = payload[2] != 0U;
		break;
	case PROFILE_OP_SET_DELAY:
		if (length != 6U || read_u32(&payload[2]) > 60000U) return -EINVAL;
		state->delay_ms = (int32_t)read_u32(&payload[2]);
		break;
	case PROFILE_OP_SET_ERROR:
		if (length != 6U) return -EINVAL;
		state->error = (int32_t)read_u32(&payload[2]);
		break;
	case PROFILE_OP_SET_RESPONSE:
		if (length < 3U || length > 3U + PROFILE_MAX_RESPONSE) return -EINVAL;
		state->response_length = length - 2U;
		memcpy(state->response, &payload[2], state->response_length);
		break;
	default:
		return -ENOTSUP;
	}
	return ef800_virtual_serial_peer_set(device, state->response,
		state->response_length, state->delay_ms, state->error, state->available);
}

bool ef800_virtual_profile_available(uint8_t device)
{
	return device < ARRAY_SIZE(device_available) ? device_available[device] : false;
}

uint8_t ef800_virtual_profile_battery_soc(void)
{
	return battery_soc;
}

uint32_t ef800_virtual_profile_battery_voltage_uv(void)
{
	return battery_voltage_uv;
}
