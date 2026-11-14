/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_i2c_debug.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "ef800_keyboard_i2c.h"
#include "flash.h"

#define KEYBOARD_ROM_I2C_ADDRESS 0x48U
#define KEYBOARD_ROM_ACK 0x79U
#define KEYBOARD_ROM_CMD_GET_ID 0x02U
#define KEYBOARD_ROM_CMD_GO 0x21U

static int rom_write(const struct device *bus, const uint8_t *data, size_t length)
{
	return i2c_write(bus, data, length, KEYBOARD_ROM_I2C_ADDRESS);
}

static int rom_read(const struct device *bus, uint8_t *data, size_t length)
{
	return i2c_read(bus, data, length, KEYBOARD_ROM_I2C_ADDRESS);
}

static int rom_wait_ack(const struct device *bus, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;
	uint8_t response;
	int result;

	do {
		result = rom_read(bus, &response, sizeof(response));
		if (result == 0 && response == KEYBOARD_ROM_ACK) {
			return 0;
		}
		if (result == 0) {
		hwdebug_console_printf("keyboard-rom: unexpected byte=0x%02x\r\n",
				response);
			return -EPROTO;
		}
		hwdebug_flash_watchdog_feed();
		k_msleep(10U);
	} while (k_uptime_get() < deadline);

	return result != 0 ? result : -ETIMEDOUT;
}

static int rom_sync(const struct device *bus)
{
	const uint8_t sync = 0x7FU;
	int64_t deadline = k_uptime_get() + 15000U;
	int result;

	do {
		result = rom_write(bus, &sync, sizeof(sync));
		if (result == 0) {
			result = rom_wait_ack(bus, 100U);
			if (result == 0) {
				return 0;
			}
		}
		hwdebug_flash_watchdog_feed();
		k_msleep(50U);
	} while (k_uptime_get() < deadline);

	return result != 0 ? result : -ETIMEDOUT;
}

static int rom_command(const struct device *bus, uint8_t command)
{
	uint8_t packet[] = {command, (uint8_t)~command};
	int result = rom_write(bus, packet, sizeof(packet));

	return result == 0 ? rom_wait_ack(bus, 2000U) : result;
}

void hwdebug_keyboard_i2c_status(void)
{
	const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	const uint8_t request = EF800_KEYBOARD_I2C_CMD_STATUS;
	uint8_t response[EF800_KEYBOARD_I2C_STATUS_SIZE];
	int result;

	memset(response, 0xCC, sizeof(response));

	if (!device_is_ready(bus)) {
		hwdebug_console_printf("keyboard-i2c: I2C1 not ready\r\n");
		return;
	}

	hwdebug_console_printf("keyboard-i2c: I2C1 status query addr=0x%02x "
		"request=0x%02x\r\n", EF800_KEYBOARD_I2C_ADDRESS, request);
	result = i2c_write_read(bus, EF800_KEYBOARD_I2C_ADDRESS,
		&request, sizeof(request), response, sizeof(response));
	hwdebug_console_printf("keyboard-i2c: transfer=%d raw="
		"%02x %02x %02x %02x %02x %02x\r\n", result,
		response[0], response[1], response[2], response[3],
		response[4], response[5]);

	if (result != 0) {
		hwdebug_console_printf("keyboard-i2c: no complete response (%d); "
			"check target power/reset, PB6/PB7 wiring, pull-ups, and address\r\n",
			result);
		return;
	}

	if (response[0] != EF800_KEYBOARD_I2C_CMD_STATUS ||
	    response[4] != EF800_KEYBOARD_I2C_MODE_APPLICATION ||
	    response[5] != EF800_KEYBOARD_I2C_ACK) {
		hwdebug_console_printf("keyboard-i2c: malformed status frame\r\n");
		return;
	}

	hwdebug_console_printf("keyboard-i2c: PASS application version=%u.%u.%u\r\n",
		response[1], response[2], response[3]);
}

void hwdebug_keyboard_i2c_rom_test(void)
{
	const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	const uint8_t enter = EF800_KEYBOARD_I2C_CMD_ENTER_ROM;
	uint8_t enter_response[2] = {0U};
	uint8_t count = 0U;
	uint8_t id[2] = {0U};
	uint8_t go_address[] = {0x08U, 0x00U, 0x00U, 0x00U, 0x08U};
	uint8_t status_request = EF800_KEYBOARD_I2C_CMD_STATUS;
	uint8_t status[EF800_KEYBOARD_I2C_STATUS_SIZE];
	int result;

	if (!device_is_ready(bus)) {
		hwdebug_console_puts("keyboard-rom: I2C1 not ready\r\n");
		return;
	}

	hwdebug_console_puts("keyboard-rom: requesting app handoff (no erase/write)\r\n");
	result = i2c_write_read(bus, EF800_KEYBOARD_I2C_ADDRESS,
		&enter, sizeof(enter), enter_response, sizeof(enter_response));
	if (result != 0 || enter_response[0] != enter ||
	    enter_response[1] != EF800_KEYBOARD_I2C_ACK) {
		hwdebug_console_printf("keyboard-rom: no app handoff response (%d, %02x %02x); "
			"trying ROM directly (expected after option-byte boot)\r\n", result,
			enter_response[0], enter_response[1]);
	} else {
		hwdebug_console_printf("keyboard-rom: application handoff ACK=%02x %02x\r\n",
			enter_response[0], enter_response[1]);
		k_msleep(500U);
		result = i2c_write_read(bus, EF800_KEYBOARD_I2C_ADDRESS,
			&status_request, sizeof(status_request), status, sizeof(status));
		if (result == 0 && status[0] == status_request &&
		    status[4] == EF800_KEYBOARD_I2C_MODE_APPLICATION &&
		    status[5] == EF800_KEYBOARD_I2C_ACK) {
			hwdebug_console_printf("keyboard-rom: FAIL target still answers application I2C "
				"version=%u.%u.%u; ROM handoff/reset did not occur\r\n",
				status[1], status[2], status[3]);
			return;
		}
	}

	result = rom_sync(bus);
	if (result != 0) {
		hwdebug_console_printf("keyboard-rom: ROM sync failed=%d; no flash commands sent\r\n",
			result);
		return;
	}
	hwdebug_console_puts("keyboard-rom: ROM sync ACK\r\n");

	result = rom_command(bus, KEYBOARD_ROM_CMD_GET_ID);
	if (result == 0) {
		result = rom_read(bus, &count, sizeof(count));
	}
	if (result == 0 && count == 1U) {
		result = rom_read(bus, id, sizeof(id));
	} else if (result == 0) {
		result = -EPROTO;
	}
	if (result != 0 || id[0] != 0x04U || id[1] != 0x35U) {
		hwdebug_console_printf("keyboard-rom: GET_ID failed=%d id=%02x %02x; "
			"no erase/write commands sent\r\n", result, id[0], id[1]);
		return;
	}
	hwdebug_console_printf("keyboard-rom: PASS STM32L431 ROM device ID=0x%02x%02x\r\n",
		id[0], id[1]);

	/* Return through the resident first-stage loader without modifying flash. */
	result = rom_command(bus, KEYBOARD_ROM_CMD_GO);
	if (result == 0) {
		result = rom_write(bus, go_address, sizeof(go_address));
	}
	if (result == 0) {
		result = rom_wait_ack(bus, 2000U);
	}
	if (result != 0) {
		hwdebug_console_printf("keyboard-rom: GO to existing loader failed=%d\r\n",
			result);
		return;
	}

	for (int attempt = 0; attempt < 100; ++attempt) {
		result = i2c_write_read(bus, EF800_KEYBOARD_I2C_ADDRESS,
			&status_request, sizeof(status_request), status, sizeof(status));
		if (result == 0 && status[0] == status_request &&
		    status[4] == EF800_KEYBOARD_I2C_MODE_APPLICATION &&
		    status[5] == EF800_KEYBOARD_I2C_ACK) {
			hwdebug_console_printf("keyboard-rom: PASS loader restarted app version=%u.%u.%u\r\n",
				status[1], status[2], status[3]);
			return;
		}
		hwdebug_flash_watchdog_feed();
		k_msleep(100U);
	}
	hwdebug_console_printf("keyboard-rom: ROM GO returned but app status did not recover (%d)\r\n",
		result);
}
