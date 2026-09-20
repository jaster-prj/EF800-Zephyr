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
#include "rtc.h"
#include "fuel_gauge.h"
#include "peripheral_tests.h"

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
		"  hardware test flash|mcp23017|rtc|fuel-gauge|keyboard|display|all\r\n"
		"  keyboard test|interactive\r\n"
		"  keyboard baud-test\r\n"
		"  keyboard version\r\n"
		"  keyboard version-raw\r\n"
		"  keyboard id-raw\r\n"
		"  keyboard update /EF800:/fwu/EF800_KEYBOARD.bin\r\n"
		"  keyboard receive\r\n"
		"  keyboard bootloader probe\r\n"
		"  display test|interactive|status|wr-test [count]\r\n"
		"  uart test port1|port2\r\n"
		"  lpuart test\r\n"
		"  lpuart host-test\r\n"
		"  lpuart bitbang-test\r\n"
		"  storage test\r\n"
		"  usb cdc test\r\n"
		"  next (storage step)\r\n"
		"  pass|fail|cancel|exit (pending interactive test)\r\n"
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
		"  mcp23017 shared-driver\r\n"
		"\r\n"
		"RTC / fuel gauge:\r\n"
		"  rtc test\r\n"
		"  fuel-gauge test\r\n");
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
	if (hwdebug_interactive_pending() &&
	    (strcmp(command, "pass") == 0 || strcmp(command, "fail") == 0 ||
	     strcmp(command, "cancel") == 0 || strcmp(command, "exit") == 0 ||
	     strcmp(command, "next") == 0 ||
	     strcmp(command, "format-confirm") == 0) &&
	    strtok(NULL, " \t") == NULL) {
		hwdebug_interactive_reply(command);
		return;
	}
	if (strcmp(command, "keyboard") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
		    argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_test("keyboard");
		} else if (subcommand != NULL && strcmp(subcommand, "baud-test") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_baud_test();
		} else if (subcommand != NULL && strcmp(subcommand, "version") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_version();
		} else if (subcommand != NULL && strcmp(subcommand, "version-raw") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_version_raw();
		} else if (subcommand != NULL && strcmp(subcommand, "id-raw") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_id_raw();
		} else if (subcommand != NULL && strcmp(subcommand, "interactive") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_interactive();
		} else if (subcommand != NULL && strcmp(subcommand, "update") == 0 &&
			   argument != NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_update(argument);
		} else if (subcommand != NULL && strcmp(subcommand, "receive") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_receive();
		} else if (subcommand != NULL && strcmp(subcommand, "bootloader") == 0 &&
			   argument != NULL && strcmp(argument, "probe") == 0 &&
			   strtok(NULL, " \t") == NULL) {
			hwdebug_keyboard_bootloader_probe();
		} else {
			hwdebug_console_puts("error: usage: keyboard test|interactive|update <path>\r\n");
		}
		return;
	}
	if (strcmp(command, "display") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
		    argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_display_test();
		} else if (subcommand != NULL && strcmp(subcommand, "interactive") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_display_interactive();
		} else if (subcommand != NULL && strcmp(subcommand, "wr-test") == 0 &&
			   (argument == NULL || strtok(NULL, " \t") == NULL)) {
			uint32_t count = 20U;
			if (argument != NULL && parse_u32(argument, &count) != 0) {
				hwdebug_console_puts("error: usage: display wr-test [count]\r\n");
			} else {
				 hwdebug_display_wr_test(count);
			}
		} else if (subcommand != NULL && strcmp(subcommand, "status") == 0 &&
			   argument == NULL && strtok(NULL, " \t") == NULL) {
			hwdebug_display_status();
		} else {
			hwdebug_console_puts("error: usage: display test|interactive|status|wr-test [count]\r\n");
		}
		return;
	}
	if (strcmp(command, "uart") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
		    argument != NULL && strtok(NULL, " \t") == NULL &&
		    (strcmp(argument, "port1") == 0 || strcmp(argument, "port2") == 0)) {
			hwdebug_uart_test(argument);
		} else {
			hwdebug_console_puts("error: usage: uart test port1|port2\r\n");
		}
		return;
	}
	if (strcmp(command, "lpuart") == 0) {
		subcommand = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
		    strtok(NULL, " \t") == NULL) {
			hwdebug_lpuart_test();
		} else if (subcommand != NULL && strcmp(subcommand, "host-test") == 0 &&
			   strtok(NULL, " \t") == NULL) {
			hwdebug_lpuart_host_test();
		} else if (subcommand != NULL && strcmp(subcommand, "bitbang-test") == 0 &&
			   strtok(NULL, " \t") == NULL) {
			hwdebug_lpuart_bitbang_test();
		} else {
			hwdebug_console_puts("error: usage: lpuart test|host-test\r\n");
		}
		return;
	}
	if (strcmp(command, "storage") == 0) {
		subcommand = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
		    strtok(NULL, " \t") == NULL) {
			hwdebug_storage_test();
		} else {
			hwdebug_console_puts("error: usage: storage test\r\n");
		}
		return;
	}
	if (strcmp(command, "usb") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && argument != NULL &&
		    strcmp(subcommand, "cdc") == 0 && strcmp(argument, "test") == 0 &&
		    strtok(NULL, " \t") == NULL) {
			hwdebug_usb_cdc_test();
		} else {
			hwdebug_console_puts("error: usage: usb cdc test\r\n");
		}
		return;
	}
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
			if (strcmp(argument, "rtc") == 0 || strcmp(argument, "all") == 0)
				hwdebug_rtc_test();
			if (strcmp(argument, "fuel-gauge") == 0 || strcmp(argument, "all") == 0)
				hwdebug_fuel_gauge_test();
			if (strcmp(argument, "keyboard") == 0 || strcmp(argument, "all") == 0)
				hwdebug_keyboard_test("keyboard");
			if (strcmp(argument, "display") == 0 || strcmp(argument, "all") == 0)
				hwdebug_display_test();
			return;
		}
		hwdebug_console_puts("error: usage: hardware test flash|mcp23017|rtc|fuel-gauge|keyboard|display|all\r\n");
		return;
	}
	if (strcmp(command, "rtc") == 0 || strcmp(command, "fuel-gauge") == 0) {
		subcommand = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
			strtok(NULL, " \t") == NULL) {
			if (strcmp(command, "rtc") == 0) hwdebug_rtc_test();
			else hwdebug_fuel_gauge_test();
		} else {
			hwdebug_console_printf("error: usage: %s test\r\n", command);
		}
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
	int keyboard_reset_result;
	if (!device_is_ready(console_uart)) return 0;
	keyboard_reset_result = hwdebug_keyboard_reset_release();
	uart_irq_callback_set(console_uart, console_rx_callback);
	uart_irq_rx_enable(console_uart);
	hwdebug_console_puts("boot: main\r\nboot: UART4 ready\r\nboot: clock source HSE 25 MHz -> PLL -> SYSCLK 80 MHz\r\nEF800 hardware debug\r\n");
	hwdebug_console_printf("watchdog: %s (%d)\r\n", watchdog_result == 0 ? "feeding" : "unavailable", watchdog_result);
	hwdebug_console_printf("keyboard reset: released high (%d)\r\n", keyboard_reset_result);
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
