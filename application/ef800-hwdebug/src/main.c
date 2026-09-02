/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define QSPI_NODE DT_NODELABEL(qspi_nor)
#define QSPI_MAX_DUMP 256U
#define SFDP_MAX_OFFSET 4096U
#define CONSOLE_LINE_SIZE 128U
#define CONSOLE_RX_QUEUE_SIZE 128U
#define CONSOLE_OUTPUT_SIZE 192U

static const struct device *const qspi = DEVICE_DT_GET(QSPI_NODE);
static const struct device *const console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static char console_line[CONSOLE_LINE_SIZE];
static size_t console_line_length;
static bool console_ready;
K_MSGQ_DEFINE(console_rx_queue, sizeof(uint8_t), CONSOLE_RX_QUEUE_SIZE, 1);

static void console_puts(const char *text)
{
	while (*text != '\0') {
		uart_poll_out(console_uart, (unsigned char)*text++);
	}
}

static void console_printf(const char *format, ...)
{
	char output[CONSOLE_OUTPUT_SIZE];
	va_list arguments;

	va_start(arguments, format);
	vsnprintk(output, sizeof(output), format, arguments);
	va_end(arguments);
	console_puts(output);
}

static void console_hexdump(uint32_t offset, const uint8_t *data, size_t length)
{
	char output[96];
	int used;

	for (size_t line = 0U; line < length; line += 16U) {
		used = snprintk(output, sizeof(output), "%08x:", offset + line);
		for (size_t index = 0U; index < 16U; ++index) {
			if (line + index < length) {
				used += snprintk(&output[used], sizeof(output) - used,
						" %02x", data[line + index]);
			} else {
				used += snprintk(&output[used], sizeof(output) - used, "   ");
			}
		}
		used += snprintk(&output[used], sizeof(output) - used, "  |");
		for (size_t index = 0U; index < 16U && line + index < length; ++index) {
			uint8_t value = data[line + index];

			used += snprintk(&output[used], sizeof(output) - used, "%c",
					(value >= 0x20U && value <= 0x7eU) ? value : '.');
		}
		snprintk(&output[used], sizeof(output) - used, "|\r\n");
		console_puts(output);
	}
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	if (text == NULL || value == NULL || text[0] == '\0' || text[0] == '-') {
		return -EINVAL;
	}
	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
		return -EINVAL;
	}
	*value = (uint32_t)parsed;
	return 0;
}

static int qspi_require_ready(void)
{
	if (!device_is_ready(qspi)) {
		console_puts("error: QSPI flash device is not ready\r\n");
		return -ENODEV;
	}
	return 0;
}

static int cmd_qspi_info(void)
{
	const struct flash_parameters *parameters;
	struct flash_pages_info page;
	uint64_t size;
	int ret;

	ret = qspi_require_ready();
	if (ret != 0) {
		return ret;
	}

	ret = flash_get_size(qspi, &size);
	if (ret != 0) {
		console_printf("error: flash_get_size failed: %d\r\n", ret);
		return ret;
	}
	parameters = flash_get_parameters(qspi);
	console_printf("device: %s\r\nready: yes\r\n", qspi->name);
	console_printf("size: %" PRIu64 " bytes (%" PRIu64 " Mbit)\r\n", size,
			(size * 8U) / (1024U * 1024U));
	console_printf("write-block: %zu bytes, erase-value: 0x%02x\r\n",
			parameters->write_block_size, parameters->erase_value);

	ret = flash_get_page_info_by_offs(qspi, 0, &page);
	if (ret == 0) {
		console_printf("erase-page: %zu bytes\r\n", page.size);
	}
	return 0;
}

static int cmd_qspi_jedec(void)
{
	uint8_t id[3];
	int ret;

	ret = qspi_require_ready();
	if (ret != 0) {
		return ret;
	}

	ret = flash_read_jedec_id(qspi, id);
	if (ret != 0) {
		console_printf("error: JEDEC ID read failed: %d\r\n", ret);
		return ret;
	}
	console_printf("jedec-id: %02x %02x %02x\r\n", id[0], id[1], id[2]);
	return 0;
}

static int cmd_qspi_sfdp(uint32_t offset, uint32_t length)
{
	uint8_t data[QSPI_MAX_DUMP];
	int ret;

	if (offset > SFDP_MAX_OFFSET || length == 0U || length > QSPI_MAX_DUMP) {
		console_printf("error: use offset <= %u and length 1..%u\r\n",
				SFDP_MAX_OFFSET, QSPI_MAX_DUMP);
		return -EINVAL;
	}

	ret = qspi_require_ready();
	if (ret != 0) {
		return ret;
	}
	ret = flash_sfdp_read(qspi, offset, data, length);
	if (ret != 0) {
		console_printf("error: SFDP read failed: %d\r\n", ret);
		return ret;
	}
	console_printf("SFDP offset 0x%08x, length %u\r\n", offset, length);
	console_hexdump(offset, data, length);
	return 0;
}

static int cmd_qspi_read(uint32_t offset, uint32_t length)
{
	uint8_t data[QSPI_MAX_DUMP];
	uint64_t flash_size;
	int ret;

	if (length == 0U || length > QSPI_MAX_DUMP) {
		console_printf("error: length must be between 1 and %u\r\n",
				QSPI_MAX_DUMP);
		return -EINVAL;
	}

	ret = qspi_require_ready();
	if (ret != 0) {
		return ret;
	}
	ret = flash_get_size(qspi, &flash_size);
	if (ret != 0 || (uint64_t)offset + length > flash_size) {
		console_puts("error: read range is outside the configured flash\r\n");
		return ret != 0 ? ret : -EINVAL;
	}
	ret = flash_read(qspi, offset, data, length);
	if (ret != 0) {
		console_printf("error: flash read failed: %d\r\n", ret);
		return ret;
	}
	console_hexdump(offset, data, length);
	return 0;
}

static void print_help(void)
{
	console_puts("commands:\r\n"
			     "  qspi info\r\n"
			     "  qspi jedec\r\n"
			     "  qspi sfdp [offset] [length]\r\n"
			     "  qspi read <offset> <length>\r\n");
}

static void handle_command(char *line)
{
	char *command = strtok(line, " \t");
	char *subcommand;
	char *argument;
	char *extra;
	uint32_t offset;
	uint32_t length;

	if (command == NULL) {
		return;
	}
	if (strcmp(command, "help") == 0) {
		print_help();
		return;
	}
	if (strcmp(command, "qspi") != 0) {
		console_puts("error: unknown command; use help\r\n");
		return;
	}

	subcommand = strtok(NULL, " \t");
	if (subcommand == NULL) {
		print_help();
		return;
	}
	if (strcmp(subcommand, "info") == 0 && strtok(NULL, " \t") == NULL) {
		(void)cmd_qspi_info();
		return;
	}
	if (strcmp(subcommand, "jedec") == 0 && strtok(NULL, " \t") == NULL) {
		(void)cmd_qspi_jedec();
		return;
	}
	if (strcmp(subcommand, "sfdp") == 0) {
		argument = strtok(NULL, " \t");
		extra = strtok(NULL, " \t");
		if (argument == NULL) {
			offset = 0U;
			length = 64U;
		} else if (extra == NULL && parse_u32(argument, &offset) == 0) {
			length = 64U;
		} else if (extra != NULL && strtok(NULL, " \t") == NULL &&
			parse_u32(argument, &offset) == 0 && parse_u32(extra, &length) == 0) {
			/* Parsed below. */
		} else {
			console_puts("error: usage: qspi sfdp [offset] [length]\r\n");
			return;
		}
		(void)cmd_qspi_sfdp(offset, length);
		return;
	}
	if (strcmp(subcommand, "read") == 0) {
		argument = strtok(NULL, " \t");
		extra = strtok(NULL, " \t");
		if (argument == NULL || extra == NULL || strtok(NULL, " \t") != NULL ||
			parse_u32(argument, &offset) != 0 || parse_u32(extra, &length) != 0) {
			console_puts("error: usage: qspi read <offset> <length>\r\n");
			return;
		}
		(void)cmd_qspi_read(offset, length);
		return;
	}
	console_puts("error: unknown qspi command; use help\r\n");
}

static void console_rx_callback(const struct device *device, void *user_data)
{
	uint8_t data[16];
	int count;

	ARG_UNUSED(user_data);
	while (uart_irq_update(device) && uart_irq_is_pending(device)) {
		if (!uart_irq_rx_ready(device)) {
			continue;
		}
		count = uart_fifo_read(device, data, sizeof(data));
		for (int index = 0; index < count; ++index) {
			(void)k_msgq_put(&console_rx_queue, &data[index], K_NO_WAIT);
		}
	}
}

static bool usb_dtr_asserted(void)
{
	uint32_t dtr = 0U;

	return uart_line_ctrl_get(console_uart, UART_LINE_CTRL_DTR, &dtr) == 0 &&
		dtr != 0U;
}

int main(void)
{
	uint8_t received;

	if (!device_is_ready(console_uart)) {
		return 0;
	}
	uart_irq_callback_set(console_uart, console_rx_callback);
	uart_irq_rx_enable(console_uart);
	while (!usb_dtr_asserted()) {
		k_sleep(K_MSEC(100));
	}
	console_ready = true;
	console_puts("EF800 hardware debug, read-only QSPI probe\r\n");
	console_puts("flash: IS25LP032D, expected 32 Mbit / 4 MiB\r\n");
	print_help();
	console_puts("> ");

	while (true) {
		if (k_msgq_get(&console_rx_queue, &received, K_MSEC(100)) != 0) {
			continue;
		}
		if (!console_ready) {
			continue;
		}
		if (received == '\r' || received == '\n') {
			if (console_line_length != 0U) {
				console_line[console_line_length] = '\0';
				handle_command(console_line);
				console_line_length = 0U;
			}
			console_puts("> ");
		} else if (received == '\b' || received == 0x7fU) {
			if (console_line_length != 0U) {
				--console_line_length;
				console_puts("\b \b");
			}
		} else if (received >= 0x20U && received <= 0x7eU) {
			if (console_line_length + 1U < sizeof(console_line)) {
				console_line[console_line_length++] = (char)received;
				uart_poll_out(console_uart, received);
			}
		}
	}
	return 0;
}
