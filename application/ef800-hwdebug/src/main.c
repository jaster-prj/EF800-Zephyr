/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>

#include "flash.h"
#include "mcp23017.h"

#define CONSOLE_LINE_SIZE 128U
#define CONSOLE_RX_QUEUE_SIZE 128U

static const struct device *const console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static char console_line[CONSOLE_LINE_SIZE];
static size_t console_line_length;
K_MSGQ_DEFINE(console_rx_queue, sizeof(uint8_t), CONSOLE_RX_QUEUE_SIZE, 1);

const struct device *hwdebug_console_device(void)
{
	return console_uart;
}

static void print_help(void)
{
	hwdebug_console_puts("commands:\r\n"
		"\r\n"
		"hardware tests:\r\n"
		"  hardware test flash|mcp23017|all\r\n"
		"\r\n"
		"flash / QUADSPI:\r\n"
		"  flash info\r\n"
		"  flash probe\r\n"
		"  flash recover\r\n"
		"  flash jedec\r\n"
		"  flash sfdp [offset] [length]\r\n"
		"  flash read <offset> <length>\r\n"
		"  flash test spi|qspi-1line|qspi-4line|qpi|all\r\n"
		"  qspi bitbang spi|qpi\r\n"
		"\r\n"
		"MCP23017:\r\n"
		"  mcp23017 test\r\n"
		"  mcp23017 raw\r\n"
		"  mcp23017 stm32-hal\r\n"
		"  mcp23017 shared-driver\r\n");
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	if (text == NULL || value == NULL || text[0] == '\0' || text[0] == '-') {
		return -EINVAL;
	}
	parsed = strtoul(text, &end, 0);
	if (end == text || *end != '\0' || parsed > UINT32_MAX) {
		return -EINVAL;
	}
	*value = (uint32_t)parsed;
	return 0;
}

static void handle_command(char *line)
{
	char *command = strtok(line, " \t");
	char *subcommand;
	char *argument;
	char *extra;
	uint32_t offset;
	uint32_t length;

	if (command == NULL) return;
	if (strcmp(command, "help") == 0) { print_help(); return; }
	if (strcmp(command, "hardware") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 && argument != NULL &&
			strtok(NULL, " \t") == NULL) {
			if (strcmp(argument, "flash") == 0 || strcmp(argument, "all") == 0)
				hwdebug_flash_test_group("all");
			if (strcmp(argument, "mcp23017") == 0 || strcmp(argument, "all") == 0)
				hwdebug_mcp23017_test();
			return;
		}
		hwdebug_console_puts("error: usage: hardware test flash|mcp23017|all\r\n");
		return;
	}
	if (strcmp(command, "mcp23017") == 0) {
		subcommand = strtok(NULL, " \t");
		if (subcommand != NULL && strtok(NULL, " \t") == NULL) {
			if (strcmp(subcommand, "test") == 0) {
				hwdebug_mcp23017_test();
			} else if (strcmp(subcommand, "raw") == 0) {
				hwdebug_mcp23017_raw_test();
			} else if (strcmp(subcommand, "stm32-hal") == 0) {
				hwdebug_mcp23017_hal_test();
			} else if (strcmp(subcommand, "shared-driver") == 0) {
				hwdebug_mcp23017_driver_test();
			} else {
				hwdebug_console_puts("error: unknown MCP23017 test; use help\r\n");
			}
		} else {
			hwdebug_console_puts("error: usage: mcp23017 test\r\n");
		}
		return;
	}
	if (strcmp(command, "flash") != 0 && strcmp(command, "qspi") != 0) {
		hwdebug_console_puts("error: unknown command; use help\r\n"); return;
	}
	subcommand = strtok(NULL, " \t");
	if (subcommand == NULL) { print_help(); return; }
	if (strcmp(command, "qspi") == 0 && strcmp(subcommand, "bitbang") == 0) {
		argument = strtok(NULL, " \t");
		if (argument == NULL || strtok(NULL, " \t") != NULL)
			hwdebug_console_puts("error: usage: qspi bitbang spi|qpi\r\n");
		else hwdebug_flash_bitbang(argument);
		return;
	}
	if (strcmp(subcommand, "info") == 0 && strtok(NULL, " \t") == NULL) {
		hwdebug_flash_info(); return;
	}
	if ((strcmp(subcommand, "probe") == 0 || strcmp(subcommand, "recover") == 0 ||
		strcmp(subcommand, "jedec") == 0) && strtok(NULL, " \t") == NULL) {
		if (strcmp(subcommand, "probe") == 0) hwdebug_flash_probe();
		else if (strcmp(subcommand, "recover") == 0) (void)hwdebug_flash_recover(true);
		else hwdebug_flash_jedec();
		return;
	}
	if (strcmp(subcommand, "test") == 0) {
		argument = strtok(NULL, " \t");
		if (argument != NULL && strtok(NULL, " \t") == NULL) {
			if (strcmp(argument, "qspi") == 0) { hwdebug_flash_test_group("qspi-1line"); hwdebug_flash_test_group("qspi-4line"); }
			else hwdebug_flash_test_group(argument);
			return;
		}
		hwdebug_console_puts("error: usage: flash test spi|qspi-1line|qspi-4line|qpi|all\r\n"); return;
	}
	if (strcmp(subcommand, "sfdp") == 0) {
		argument = strtok(NULL, " \t"); extra = strtok(NULL, " \t");
		if (argument == NULL) { offset = 0U; length = 64U; }
		else if (parse_u32(argument, &offset) != 0 || (extra != NULL && parse_u32(extra, &length) != 0) || strtok(NULL, " \t") != NULL) {
			hwdebug_console_puts("error: usage: flash sfdp [offset] [length]\r\n"); return;
		} else length = extra == NULL ? 64U : length;
		hwdebug_flash_sfdp(offset, length); return;
	}
	if (strcmp(subcommand, "read") == 0) {
		argument = strtok(NULL, " \t"); extra = strtok(NULL, " \t");
		if (argument == NULL || extra == NULL || strtok(NULL, " \t") != NULL || parse_u32(argument, &offset) != 0 || parse_u32(extra, &length) != 0)
			hwdebug_console_puts("error: usage: flash read <offset> <length>\r\n");
		else hwdebug_flash_read(offset, length);
		return;
	}
	hwdebug_console_puts("error: unknown flash command; use help\r\n");
}

static void console_rx_callback(const struct device *device, void *user_data)
{
	uint8_t data[16]; int count;
	ARG_UNUSED(user_data);
	while (uart_irq_update(device) && uart_irq_is_pending(device)) {
		if (!uart_irq_rx_ready(device)) continue;
		count = uart_fifo_read(device, data, sizeof(data));
		for (int index = 0; index < count; ++index) (void)k_msgq_put(&console_rx_queue, &data[index], K_NO_WAIT);
	}
}

int main(void)
{
	uint8_t received;
	int watchdog_result = hwdebug_flash_watchdog_init();
	if (!device_is_ready(console_uart)) return 0;
	uart_irq_callback_set(console_uart, console_rx_callback);
	uart_irq_rx_enable(console_uart);
	hwdebug_console_puts("boot: main\r\nboot: UART4 ready\r\nboot: clock source HSE 25 MHz -> PLL -> SYSCLK 80 MHz\r\nEF800 hardware debug\r\n");
	hwdebug_console_printf("watchdog: %s (%d)\r\n", watchdog_result == 0 ? "feeding" : "unavailable", watchdog_result);
	print_help(); hwdebug_console_puts("> ");
	while (true) {
		hwdebug_flash_watchdog_feed();
		if (k_msgq_get(&console_rx_queue, &received, K_MSEC(100)) != 0) continue;
		if (received == '\r' || received == '\n') {
			if (console_line_length != 0U) { console_line[console_line_length] = '\0'; handle_command(console_line); console_line_length = 0U; }
			hwdebug_console_puts("> ");
		} else if (received == '\b' || received == 0x7fU) {
			if (console_line_length != 0U) { --console_line_length; hwdebug_console_puts("\b \b"); }
		} else if (received >= 0x20U && received <= 0x7eU && console_line_length + 1U < sizeof(console_line)) {
			console_line[console_line_length++] = (char)received; uart_poll_out(console_uart, received);
		}
	}
}
