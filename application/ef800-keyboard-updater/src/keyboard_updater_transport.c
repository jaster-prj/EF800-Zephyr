/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_updater_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define UPDATER_I2C_ADDRESS EF800_KEYBOARD_UPDATER_I2C_ADDRESS

static const struct device *const i2c_target =
	DEVICE_DT_GET(DT_NODELABEL(i2c1));
static uint8_t request_frame[EF800_KB_UPDATE_REQUEST_MAX] __aligned(4);
static size_t request_length;
static uint32_t last_byte_ms;
static bool request_is_version;
static bool request_timed_out;
static bool request_overflow;
static bool write_transaction;
static bool read_transaction;
static uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t response_length;
static size_t response_position;
static atomic_t request_busy;
static atomic_t request_pending;
static atomic_t response_ready;
static atomic_t reboot_after_response;
static atomic_t reboot_pending;

static int write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	if (atomic_get(&request_busy) != 0) {
		return -EBUSY;
	}
	atomic_set(&request_busy, 1);
	atomic_set(&response_ready, 0);
	request_length = 0U;
	request_is_version = false;
	request_timed_out = false;
	request_overflow = false;
	last_byte_ms = k_uptime_get_32();
	write_transaction = true;
	read_transaction = false;
	return 0;
}

static int write_received(struct i2c_target_config *config, uint8_t byte)
{
	uint32_t now = k_uptime_get_32();

	ARG_UNUSED(config);
	if (!ef800_kb_update_frame_timing_valid(last_byte_ms, now)) {
		request_timed_out = true;
	}
	last_byte_ms = now;
	if (request_length == 0U && !request_is_version &&
	    byte == EF800_KB_UPDATE_VERSION_COMMAND) {
		request_is_version = true;
		return 0;
	}
	if (request_is_version || request_length >= sizeof(request_frame)) {
		request_overflow = true;
		return 0;
	}
	request_frame[request_length++] = byte;
	return 0;
}

static int read_requested(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	read_transaction = true;
	write_transaction = false;
	response_position = 0U;
	if (atomic_get(&response_ready) == 0) {
		*byte = EF800_KB_PROTOCOL_BUSY;
		response_length = 1U;
		response_position = 1U;
		return 0;
	}
	if (response_length == 0U) {
		*byte = EF800_KB_PROTOCOL_NACK;
		response_length = 1U;
		response_position = 1U;
		return 0;
	}
	*byte = response[response_position++];
	return 0;
}

static int read_processed(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
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
		atomic_set(&request_pending, 1);
	} else if (read_transaction) {
		read_transaction = false;
		if (response_position >= response_length) {
			atomic_set(&response_ready, 0);
			if (atomic_cas(&reboot_after_response, 1, 0)) {
				atomic_set(&reboot_pending, 1);
			}
		}
	}
	return 0;
}

static const struct i2c_target_callbacks callbacks = {
	.write_requested = write_requested,
	.write_received = write_received,
	.read_requested = read_requested,
	.read_processed = read_processed,
	.stop = i2c_stop,
};

static struct i2c_target_config config = {
	.address = UPDATER_I2C_ADDRESS,
	.callbacks = &callbacks,
};

int ef800_keyboard_updater_transport_init(void)
{
	if (!device_is_ready(i2c_target)) {
		return -ENODEV;
	}
	return i2c_target_register(i2c_target, &config);
}

bool ef800_keyboard_updater_request_take(
	uint8_t out[EF800_KB_UPDATE_REQUEST_MAX], size_t *out_length,
	bool *is_version, bool *timed_out, bool *overflow)
{
	if (out == NULL || out_length == NULL || is_version == NULL ||
		timed_out == NULL || overflow == NULL ||
		!atomic_cas(&request_pending, 1, 0)) {
		return false;
	}
	memcpy(out, request_frame, request_length);
	*out_length = request_length;
	*is_version = request_is_version;
	*timed_out = request_timed_out;
	*overflow = request_overflow;
	return true;
}

int ef800_keyboard_updater_response_submit(
	const uint8_t data[EF800_KB_UPDATE_RESPONSE_MAX], size_t length,
	bool reboot)
{
	if (data == NULL || length > sizeof(response)) {
		return -EINVAL;
	}
	memcpy(response, data, length);
	response_length = length;
	response_position = 0U;
	if (reboot) {
		atomic_set(&reboot_after_response, 1);
	}
	atomic_set(&response_ready, 1);
	atomic_set(&request_busy, 0);
	return 0;
}

bool ef800_keyboard_updater_reboot_take(void)
{
	return atomic_cas(&reboot_pending, 1, 0);
}
