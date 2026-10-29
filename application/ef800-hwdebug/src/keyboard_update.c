/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stm32l4xx.h>

#include "flash.h"
#include "keyboard_protocol.h"
#include "peripheral_tests.h"
#include "ef800_keyboard_package.h"
#include "ef800_update.h"

#define KB_UPDATE_PATH_MAX 64U
#define KB_UPDATE_MAX_SIZE (256U * 1024U)
#define KB_UPDATE_CHUNK 128U
#define KB_UPDATE_TIMEOUT_MS 1500U
#define KB_ROM_ACK 0x79U
#define KB_ROM_NACK 0x1FU
#define KB_ROM_GET_ID 0x02U
#define KB_ROM_EXTENDED_ERASE 0x44U
#define KB_ROM_WRITE_MEMORY 0x31U
#define KB_ROM_GO 0x21U
#define KB_ROM_ADDRESS 0x08000000U
#define KB_ROM_DEVICE_ID 0x0435U
#define KB_RX_MAGIC 0x424B4645U
#define KB_RX_PATH "/DATA:/fwu/EF800_KEYBOARD.pkg"
#define KB_PACKAGE_MAX_SIZE (EF800_KEYBOARD_PACKAGE_HEADER_SIZE + \
	EF800_KEYBOARD_PACKAGE_LIMIT)

static const struct device *const update_uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static int configure_update_uart(uint32_t baudrate)
{
	const struct uart_config config = {
		.baudrate = baudrate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(update_uart, &config);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
	for (size_t i = 0U; i < length; ++i) {
		crc ^= data[i];
		for (unsigned int bit = 0U; bit < 8U; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
		}
	}
	return crc;
}

static int receive_console_byte(uint8_t *value, uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() < deadline) {
		if (uart_poll_in(console_uart, value) == 0) {
			return 0;
		}
		k_msleep(1U);
	}
	return -ETIMEDOUT;
}

static int receive_console_u32(uint32_t *value)
{
	uint8_t bytes[4];

	for (size_t i = 0U; i < sizeof(bytes); ++i) {
		if (receive_console_byte(&bytes[i], 3000U) != 0) {
			return -ETIMEDOUT;
		}
	}
	*value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return 0;
}

static int receive_console_magic(void)
{
	static const uint8_t magic[] = { 0x45U, 0x46U, 0x4BU, 0x42U };
	uint8_t byte;
	size_t matched = 0U;

	while (matched < sizeof(magic)) {
		if (receive_console_byte(&byte, 3000U) != 0) {
			return -ETIMEDOUT;
		}
		if (byte == magic[matched]) {
			++matched;
		} else {
			matched = byte == magic[0] ? 1U : 0U;
		}
	}
	return 0;
}

static int validate_keyboard_package(void)
{
	struct fs_file_t file;
	struct ef800_keyboard_package_header header;
	struct fs_dirent entry;
	uint8_t buffer[256];
	uint32_t crc = 0U;
	uint32_t remaining;
	int ret;

	ret = fs_stat(KB_RX_PATH, &entry);
	if (ret != 0 || entry.type != FS_DIR_ENTRY_FILE ||
		entry.size < EF800_KEYBOARD_PACKAGE_HEADER_SIZE) {
		return -EINVAL;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, KB_RX_PATH, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	ret = fs_read(&file, &header, sizeof(header)) == sizeof(header) &&
		ef800_keyboard_package_valid(&header, entry.size) ? 0 : -EINVAL;
	if (ret == 0) {
		remaining = header.image_size;
		while (remaining != 0U) {
			size_t count = MIN(sizeof(buffer), remaining);
			if (fs_read(&file, buffer, count) != (ssize_t)count) {
				ret = -EIO;
				break;
			}
			crc = ef800_crc32_update(crc, buffer, count);
			remaining -= count;
		}
		if (ret == 0 && crc != header.image_crc32) {
			ret = -EBADMSG;
		}
	}
	(void)fs_close(&file);
	if (ret == 0) {
		hwdebug_console_printf("\r\nkeyboard receive: package valid version=%u.%u.%u "
			"target=0x%04x size=%u\r\n", header.version_major,
			header.version_minor, header.version_patch, header.target_id,
			(unsigned int)header.image_size);
	}
	return ret;
}

void hwdebug_keyboard_receive(void)
{
	struct fs_file_t file;
	uint8_t buffer[256];
	uint32_t size, expected_crc;
	uint32_t crc = 0xFFFFFFFFU;
	uint8_t reserved[4];
	size_t received = 0U;
	int ret;

	if (!device_is_ready(console_uart)) {
		return;
	}
	ret = hwdebug_storage_prepare_update();
	if (ret != 0) {
		hwdebug_console_printf("keyboard receive: storage unavailable=%d\r\n", ret);
		return;
	}
	/* The command parser has consumed the line ending; switch COM7 to binary. */
	uart_irq_rx_disable(console_uart);
	uint8_t stale;
	while (uart_poll_in(console_uart, &stale) == 0) {
	}
	hwdebug_console_puts("keyboard receive: ready\r\n");
	if (receive_console_magic() != 0) {
		hwdebug_console_puts("keyboard receive: magic timeout\r\n");
		goto done;
	}
	uart_poll_out(console_uart, 'M');
	if (receive_console_u32(&size) != 0 ||
		receive_console_u32(&expected_crc) != 0) {
		hwdebug_console_puts("keyboard receive: metadata timeout\r\n");
		goto done;
	}
	for (size_t i = 0U; i < sizeof(reserved); ++i) {
		if (receive_console_byte(&reserved[i], 3000U) != 0) goto done;
	}
	if (size == 0U || size > KB_PACKAGE_MAX_SIZE) {
		hwdebug_console_puts("keyboard receive: invalid size\r\n");
		goto done;
	}
	fs_file_t_init(&file);
	(void)fs_mkdir("/DATA:/fwu");
	ret = fs_open(&file, KB_RX_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		hwdebug_console_printf("keyboard receive: open failed=%d\r\n", ret);
		goto done;
	}
	uart_poll_out(console_uart, 'R');
	while (received < size) {
		size_t length = MIN(sizeof(buffer), size - received);
		for (size_t i = 0U; i < length; ++i) {
			if (receive_console_byte(&buffer[i], 3000U) != 0) {
				(void)fs_close(&file);
				goto done;
			}
		}
		if (fs_write(&file, buffer, length) != (ssize_t)length) {
			(void)fs_close(&file);
			goto done;
		}
		crc = crc32_update(crc, buffer, length);
		received += length;
	}
	(void)fs_close(&file);
	crc ^= 0xFFFFFFFFU;
	if (crc != expected_crc) {
		uart_poll_out(console_uart, 'E');
		hwdebug_console_printf("\r\nkeyboard receive: CRC failed size=%u\r\n",
			(unsigned int)size);
		goto done;
	}
	ret = validate_keyboard_package();
	uart_poll_out(console_uart, ret == 0 ? 'K' : 'E');
	if (ret != 0) {
		hwdebug_console_printf("\r\nkeyboard receive: package validation failed=%d; "
			"file retained\r\n", ret);
	} else {
		hwdebug_console_printf("keyboard receive: package upload successful "
			"size=%u\r\n", (unsigned int)size);
	}
done:
	uart_irq_rx_enable(console_uart);
}

static uint8_t xor_bytes(const uint8_t *data, size_t length)
{
	uint8_t checksum = 0U;

	for (size_t index = 0U; index < length; ++index) {
		checksum ^= data[index];
	}
	return checksum;
}

static int read_byte(uint8_t *value, uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() < deadline) {
		if (uart_poll_in(update_uart, value) == 0) {
			return 0;
		}
		k_msleep(1U);
	}
	return -ETIMEDOUT;
}

static int expect_ack(const char *step)
{
	uint8_t response;
	int ret = read_byte(&response, KB_UPDATE_TIMEOUT_MS);

	if (ret != 0) {
		hwdebug_console_printf("keyboard update: %s timeout\r\n", step);
		return ret;
	}
	if (response != KB_ROM_ACK) {
		hwdebug_console_printf("keyboard update: %s response=0x%02x\r\n",
			step, response);
		return response == KB_ROM_NACK ? -EIO : -EPROTO;
	}
	return 0;
}

static int send_command(uint8_t command, const char *step)
{
	uint8_t frame[] = { command, (uint8_t)~command };

	uart_poll_out(update_uart, frame[0]);
	uart_poll_out(update_uart, frame[1]);
	return expect_ack(step);
}

static int send_address(uint32_t address)
{
	uint8_t frame[5] = {
		(uint8_t)(address >> 24), (uint8_t)(address >> 16),
		(uint8_t)(address >> 8), (uint8_t)address, 0U,
	};

	frame[4] = xor_bytes(frame, 4U);
	for (size_t index = 0U; index < sizeof(frame); ++index) {
		uart_poll_out(update_uart, frame[index]);
	}
	return expect_ack("address");
}

static int keyboard_rom_enter(void)
{
	static const uint8_t application_ack[] = {
		CMD_RETURN, CMD_KEYBOARD_BOOTLOADER, CMD_ACK,
	};
	uint8_t byte;
	int ret;

	if (!device_is_ready(update_uart)) {
		return -ENODEV;
	}
	while (uart_poll_in(update_uart, &byte) == 0) {
	}
	if (configure_update_uart(KB_UART_BAUDRATE) != 0) {
		return -EIO;
	}
	uart_poll_out(update_uart, CMD_KEYBOARD_BAUDRATE);
	uart_poll_out(update_uart, KB_BAUD_SELECTOR_9600);
	k_msleep(20U);
	if (configure_update_uart(KB_UART_BAUDRATE) != 0) {
		return -EIO;
	}
	uart_poll_out(update_uart, CMD_KEYBOARD_BOOTLOADER);
	k_msleep(2U);
	for (size_t index = 0U; index < sizeof(application_ack); ++index) {
		ret = read_byte(&byte, KB_UPDATE_TIMEOUT_MS);
		if (ret != 0 || byte != application_ack[index]) {
			return ret != 0 ? ret : -EPROTO;
		}
	}
	k_msleep(50U);
	uart_poll_out(update_uart, 0x7FU);
	return expect_ack("sync");
}

void hwdebug_keyboard_bootloader_probe(void)
{
	uint8_t byte;
	int ret;

	if (!device_is_ready(update_uart)) {
		hwdebug_console_puts("keyboard bootloader: UART unavailable\r\n");
		return;
	}
	ret = configure_update_uart(KB_UART_BAUDRATE);
	if (ret == 0) {
		ret = keyboard_rom_enter();
	}
	if (ret == 0) {
		hwdebug_console_puts("keyboard bootloader: sync ACK at 115200\r\n");
		return;
	}

	/* Re-enter the application before trying the alternate baud rate. */
	hwdebug_console_printf("keyboard bootloader: 115200 failed=%d; retrying 9600\r\n", ret);
	(void)configure_update_uart(KB_UART_BAUDRATE);
	while (uart_poll_in(update_uart, &byte) == 0) {
	}
	uart_poll_out(update_uart, CMD_KEYBOARD_BOOTLOADER);
	for (size_t index = 0U; index < 3U; ++index) {
		if (read_byte(&byte, KB_UPDATE_TIMEOUT_MS) != 0) {
			hwdebug_console_puts("keyboard bootloader: application ACK timeout at 9600\r\n");
			return;
		}
	}
	k_msleep(50U);
	uart_poll_out(update_uart, 0x7FU);
	ret = expect_ack("sync-9600");
	hwdebug_console_printf("keyboard bootloader: 9600 result=%d\r\n", ret);
}

static int keyboard_rom_get_id(void)
{
	uint8_t count;
	uint8_t id[2];
	int ret = send_command(KB_ROM_GET_ID, "GET_ID");

	if (ret != 0) {
		return ret;
	}
	ret = read_byte(&count, KB_UPDATE_TIMEOUT_MS);
	if (ret != 0 || count != 1U) {
		return ret != 0 ? ret : -EPROTO;
	}
	ret = read_byte(&id[0], KB_UPDATE_TIMEOUT_MS);
	if (ret == 0) ret = read_byte(&id[1], KB_UPDATE_TIMEOUT_MS);
	if (ret == 0) ret = read_byte(&count, KB_UPDATE_TIMEOUT_MS);
	if (ret != 0) {
		return ret;
	}
	hwdebug_console_printf("keyboard update: device id=0x%02x%02x\r\n",
		id[0], id[1]);
	return ((uint16_t)id[0] << 8 | id[1]) == KB_ROM_DEVICE_ID ? 0 : -ENODEV;
}

static int keyboard_rom_erase(void)
{
	uint8_t frame[2 + 2 + 128 * 2 + 1];
	uint8_t *cursor = frame;

	*cursor++ = 0U;
	*cursor++ = 127U;
	for (uint16_t page = 0U; page < 128U; ++page) {
		*cursor++ = (uint8_t)(page >> 8);
		*cursor++ = (uint8_t)page;
	}
	*cursor = xor_bytes(frame, (size_t)(cursor - frame));
	for (size_t index = 0U; index <= (size_t)(cursor - frame); ++index) {
		uart_poll_out(update_uart, frame[index]);
	}
	return expect_ack("erase");
}

static int keyboard_rom_write(struct fs_file_t *file, size_t size)
{
	uint8_t frame[KB_UPDATE_CHUNK + 2U];

	for (size_t offset = 0U; offset < size; offset += KB_UPDATE_CHUNK) {
		size_t length = MIN(KB_UPDATE_CHUNK, size - offset);
		size_t padded = ROUND_UP(length, 8U);
		memset(frame + 1U, 0xFF, KB_UPDATE_CHUNK);
		ssize_t read = fs_read(file, frame + 1U, length);

		if (read != (ssize_t)length) {
			return -EIO;
		}
		frame[0] = (uint8_t)(padded - 1U);
		frame[padded + 1U] = xor_bytes(frame, padded + 1U);
		if (send_command(KB_ROM_WRITE_MEMORY, "WRITE_MEMORY") != 0 ||
			send_address(KB_ROM_ADDRESS + (uint32_t)offset) != 0) {
			return -EIO;
		}
		for (size_t index = 0U; index <= padded + 1U; ++index) {
			uart_poll_out(update_uart, frame[index]);
		}
		if (expect_ack("write") != 0) {
			return -EIO;
		}
	}
	return 0;
}

void hwdebug_keyboard_update(const char *path)
{
	uint8_t header[8];
	struct fs_dirent stat;
	struct fs_file_t file;
	size_t size;
	ssize_t read;
	int ret;

	if (path == NULL || strlen(path) >= KB_UPDATE_PATH_MAX) {
		hwdebug_console_puts("keyboard update: invalid path\r\n");
		return;
	}
	ret = hwdebug_storage_prepare_update();
	if (ret != 0) {
		hwdebug_console_printf("keyboard update: storage unavailable=%d\r\n", ret);
		return;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		hwdebug_console_printf("keyboard update: open failed=%d\r\n", ret);
		return;
	}
	ret = fs_stat(path, &stat);
	if (ret != 0 || stat.size <= 0 || stat.size > KB_UPDATE_MAX_SIZE) {
		(void)fs_close(&file);
		hwdebug_console_puts("keyboard update: invalid image size\r\n");
		return;
	}
	size = (size_t)stat.size;
	read = fs_read(&file, header, sizeof(header));
	if (read != sizeof(header) || (header[0] & 3U) != 0U ||
		header[1] != 0x00U || header[2] != 0x00U || header[3] < 0x20U ||
		header[4] < 0x01U || fs_seek(&file, 0, FS_SEEK_SET) != 0) {
		(void)fs_close(&file);
		hwdebug_console_puts("keyboard update: invalid raw image\r\n");
		return;
	}
	hwdebug_console_printf("keyboard update: image size=%u\r\n", (unsigned int)size);
	ret = keyboard_rom_enter();
	if (ret == 0) ret = keyboard_rom_get_id();
	if (ret == 0) ret = keyboard_rom_erase();
	if (ret == 0) ret = keyboard_rom_write(&file, size);
	(void)fs_close(&file);
	if (ret == 0) {
		hwdebug_console_puts("keyboard update: complete; resetting keyboard\r\n");
		NVIC_SystemReset();
	} else {
		hwdebug_console_printf("keyboard update: failed=%d\r\n", ret);
	}
}
