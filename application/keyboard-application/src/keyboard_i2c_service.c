/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_i2c_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "ef800_keyboard_i2c.h"
#include "ef800_keyboard_update_protocol.h"

#define EF800_KEYBOARD_VERSION_MAJOR 1U
#define EF800_KEYBOARD_VERSION_MINOR 1U
#define EF800_KEYBOARD_VERSION_PATCH 3U
#define KB_I2C_NODE DT_NODELABEL(i2c1)

static const struct device *const i2c_target = DEVICE_DT_GET(KB_I2C_NODE);
static struct i2c_target_config i2c_config;
static uint8_t command;
static bool command_received;
static bool framed_write;
static uint8_t response[EF800_KEYBOARD_I2C_STATUS_SIZE];
static uint8_t response_length;
static uint8_t response_position;
static bool enter_updater_after_stop;
static atomic_t enter_updater_pending;
static uint8_t request[EF800_KB_UPDATE_REQUEST_MAX] __aligned(4);
static size_t request_length;
static uint8_t update_response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t update_response_length;
static size_t update_response_position;
static bool write_transaction;
static bool read_transaction;
static uint32_t last_byte_ms;
static bool request_timed_out;
static bool request_overflow;
static atomic_t request_pending;
static atomic_t update_response_ready;

static int i2c_write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	command = 0U;
	command_received = false;
	framed_write = false;
	response_length = 0U;
	response_position = 0U;
	enter_updater_after_stop = false;
	request_length = 0U;
	request_timed_out = false;
	request_overflow = false;
	last_byte_ms = k_uptime_get_32();
	write_transaction = true;
	read_transaction = false;
	return 0;
}

static int i2c_write_received(struct i2c_target_config *config, uint8_t byte)
{
	ARG_UNUSED(config);
	if (command_received) {
		if (!framed_write &&
		    (command == EF800_KEYBOARD_I2C_CMD_STATUS ||
		     command == EF800_KEYBOARD_I2C_CMD_VERSION ||
		     command == EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER)) {
			return -ENOTSUP;
		}
		if (!ef800_kb_update_frame_timing_valid(last_byte_ms,
							 k_uptime_get_32())) {
			request_timed_out = true;
		}
		last_byte_ms = k_uptime_get_32();
		if (request_length >= sizeof(request)) {
			request_overflow = true;
			return 0;
		}
		request[request_length++] = byte;
		return 0;
	}
	command = byte;
	command_received = true;
	if (byte != EF800_KEYBOARD_I2C_CMD_STATUS &&
	    byte != EF800_KEYBOARD_I2C_CMD_VERSION &&
	    byte != EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER) {
		framed_write = true;
		request[request_length++] = byte;
	}
	return 0;
}

static int i2c_read_requested(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	if (framed_write) {
		read_transaction = true;
		write_transaction = false;
		update_response_position = 0U;
		if (atomic_get(&update_response_ready) == 0) {
			*byte = EF800_KB_PROTOCOL_BUSY;
			update_response_length = 1U;
			update_response_position = 1U;
			return 0;
		}
		if (update_response_length == 0U) {
			*byte = EF800_KB_PROTOCOL_NACK;
			update_response_length = 1U;
			update_response_position = 1U;
			return 0;
		}
		*byte = update_response[update_response_position++];
		return 0;
	}
	if (command == EF800_KEYBOARD_I2C_CMD_STATUS) {
		response[0] = EF800_KEYBOARD_I2C_CMD_STATUS;
		response[1] = EF800_KEYBOARD_VERSION_MAJOR;
		response[2] = EF800_KEYBOARD_VERSION_MINOR;
		response[3] = EF800_KEYBOARD_VERSION_PATCH;
		response[4] = EF800_KEYBOARD_I2C_MODE_APPLICATION;
		response[5] = EF800_KEYBOARD_I2C_ACK;
		response_length = EF800_KEYBOARD_I2C_STATUS_SIZE;
	} else if (command == EF800_KEYBOARD_I2C_CMD_VERSION) {
		response_length = (uint8_t)ef800_kb_update_version_encode(
			EF800_KEYBOARD_I2C_CMD_VERSION, EF800_KEYBOARD_I2C_ACK,
			EF800_KEYBOARD_VERSION_MAJOR, EF800_KEYBOARD_VERSION_MINOR,
			EF800_KEYBOARD_VERSION_PATCH, response, sizeof(response));
	} else if (command == EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER) {
		response[0] = EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER;
		response[1] = EF800_KEYBOARD_I2C_ACK;
		response_length = 2U;
		enter_updater_after_stop = true;
	} else {
		return -ENOTSUP;
	}
	*byte = response[0];
	response_position = 1U;
	return 0;
}

static int i2c_read_processed(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	if (framed_write) {
		if (update_response_position < update_response_length) {
			*byte = update_response[update_response_position++];
		} else {
			*byte = 0xFFU;
		}
		return 0;
	}
	if (response_position < response_length) {
		*byte = response[response_position++];
	} else {
		*byte = 0xFFU;
	}
	return 0;
}

static int i2c_stop(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	if (write_transaction) {
		write_transaction = false;
		if (framed_write) {
			atomic_set(&update_response_ready, 0);
			atomic_set(&request_pending, 1);
		} else if (enter_updater_after_stop &&
			   response_position >= response_length) {
			atomic_set(&enter_updater_pending, 1);
		}
		return 0;
	}
	if (read_transaction) {
		read_transaction = false;
		if (update_response_position >= update_response_length) {
			atomic_set(&update_response_ready, 0);
		}
		return 0;
	}
	if (enter_updater_after_stop && response_position >= response_length) {
		atomic_set(&enter_updater_pending, 1);
	}
	return 0;
}

static const struct i2c_target_callbacks callbacks = {
	.write_requested = i2c_write_requested,
	.write_received = i2c_write_received,
	.read_requested = i2c_read_requested,
	.read_processed = i2c_read_processed,
	.stop = i2c_stop,
};

int ef800_keyboard_i2c_service_init(void)
{
	if (!device_is_ready(i2c_target)) {
		return -ENODEV;
	}
	i2c_config = (struct i2c_target_config) {
		.address = EF800_KEYBOARD_I2C_ADDRESS,
		.callbacks = &callbacks,
	};
	return i2c_target_register(i2c_target, &i2c_config);
}

int ef800_keyboard_i2c_service_unregister(void)
{
	return i2c_target_unregister(i2c_target, &i2c_config);
}

bool ef800_keyboard_i2c_update_take(uint8_t out[EF800_KB_UPDATE_REQUEST_MAX],
				    size_t *out_length,
				    bool *timed_out, bool *overflow)
{
	if (out == NULL || out_length == NULL || timed_out == NULL ||
		overflow == NULL || !atomic_cas(&request_pending, 1, 0)) {
		return false;
	}
	memcpy(out, request, request_length);
	*out_length = request_length;
	*timed_out = request_timed_out;
	*overflow = request_overflow;
	return true;
}

int ef800_keyboard_i2c_update_respond(
	const uint8_t data[EF800_KB_UPDATE_RESPONSE_MAX], size_t length)
{
	if (data == NULL || length > sizeof(update_response)) {
		return -EINVAL;
	}
	memcpy(update_response, data, length);
	update_response_length = length;
	update_response_position = 0U;
	atomic_set(&update_response_ready, 1);
	return 0;
}

bool ef800_keyboard_i2c_updater_entry_take(void)
{
	return atomic_cas(&enter_updater_pending, 1, 0);
}

void ef800_keyboard_i2c_updater_entry_request(void)
{
	atomic_set(&enter_updater_pending, 1);
}
