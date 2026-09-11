/* SPDX-License-Identifier: Apache-2.0 */

#include "peripheral_tests.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_uart.h>

#include "ef800_t6963.h"
#include "flash.h"
#include "keyboard_protocol.h"
#include "test_result.h"

#define LCD_NODE DT_PATH(zephyr_user)
#define TEST_UART_TIMEOUT_MS 1000U
#define KEYBOARD_RESET_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec keyboard_reset =
	GPIO_DT_SPEC_GET(KEYBOARD_RESET_NODE, keyboard_reset_gpios);

enum interactive_test {
	INTERACTIVE_NONE,
	INTERACTIVE_KEYBOARD,
	INTERACTIVE_DISPLAY,
	INTERACTIVE_STORAGE,
};

static enum interactive_test interactive_waiting;
static unsigned int storage_step;
static const struct device *uart_for_name(const char *name)
{
	if (name != NULL && strcmp(name, "port1") == 0) {
		return DEVICE_DT_GET(DT_NODELABEL(usart3));
	}
	if (name != NULL && strcmp(name, "port2") == 0) {
		return DEVICE_DT_GET(DT_NODELABEL(usart1));
	}
	return NULL;
}

static int configure_uart(const struct device *uart)
{
	const struct uart_config config = {
		.baudrate = 115200,
		.parity = UART_CFG_PARITY_ODD,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	return uart_configure(uart, &config);
}

static int configure_keyboard_uart(const struct device *uart)
{
	const struct uart_config config = {
		.baudrate = KB_UART_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	return uart_configure(uart, &config);
}

static int uart_receive(const struct device *uart, uint8_t expected,
				uint32_t timeout_ms)
{
	uint64_t deadline = k_uptime_get() + timeout_ms;
	uint8_t byte;

	while (k_uptime_get() < deadline) {
		if (uart_poll_in(uart, &byte) == 0) {
			return byte == expected ? 0 : -EPROTO;
		}
		k_sleep(K_MSEC(1));
	}
	return -ETIMEDOUT;
}

static void uart_drain(const struct device *uart)
{
	uint8_t byte;

	while (uart_poll_in(uart, &byte) == 0) {
	}
}

static int keyboard_reset_and_wait(const struct device *uart)
{
	uint8_t initialized;
	int ret;

	if (!gpio_is_ready_dt(&keyboard_reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}
	uart_drain(uart);
	k_msleep(10U);
	ret = gpio_pin_set_dt(&keyboard_reset, 1);
	if (ret != 0) {
		return ret;
	}
	initialized = CMD_INITIALIZED;
	return uart_receive(uart, initialized, TEST_UART_TIMEOUT_MS);
}

static UART_HandleTypeDef lpuart1_hal_uart;

static int lpuart1_hal_init(void)
{
	GPIO_InitTypeDef tx_gpio = {
		.Pin = GPIO_PIN_0,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};
	GPIO_InitTypeDef rx_gpio = {
		.Pin = GPIO_PIN_7,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};

	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_LPUART1_CLK_ENABLE();
	__HAL_RCC_LPUART1_FORCE_RESET();
	__HAL_RCC_LPUART1_RELEASE_RESET();
	HAL_GPIO_Init(GPIOC, &tx_gpio);
	HAL_GPIO_Init(GPIOG, &rx_gpio);

	lpuart1_hal_uart.Instance = LPUART1;
	lpuart1_hal_uart.Init.BaudRate = KB_UART_BAUDRATE;
	lpuart1_hal_uart.Init.WordLength = UART_WORDLENGTH_8B;
	lpuart1_hal_uart.Init.StopBits = UART_STOPBITS_1;
	lpuart1_hal_uart.Init.Parity = UART_PARITY_NONE;
	lpuart1_hal_uart.Init.Mode = UART_MODE_TX_RX;
	lpuart1_hal_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	lpuart1_hal_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	lpuart1_hal_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
	lpuart1_hal_uart.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;

	return HAL_UART_Init(&lpuart1_hal_uart) == HAL_OK ? 0 : -EIO;
}

static void lpuart1_hal_print_registers(void)
{
	hwdebug_console_printf("rcc: AHB2ENR=0x%08x APB1ENR1=0x%08x CCIPR=0x%08x\r\n",
		(unsigned int)RCC->AHB2ENR, (unsigned int)RCC->APB1ENR1,
		(unsigned int)RCC->CCIPR);
	hwdebug_console_printf("rcc: APB1ENR2=0x%08x APB1RSTR2=0x%08x LPUART1EN=%u\r\n",
		(unsigned int)RCC->APB1ENR2, (unsigned int)RCC->APB1RSTR2,
		(unsigned int)((RCC->APB1ENR2 & RCC_APB1ENR2_LPUART1EN) != 0U));
	hwdebug_console_printf("gpioc: MODER=0x%08x PUPDR=0x%08x AFR0=0x%08x (LPUART1 TX=PC0)\r\n",
		(unsigned int)GPIOC->MODER, (unsigned int)GPIOC->PUPDR,
		(unsigned int)GPIOC->AFR[0]);
	hwdebug_console_printf("gpiog: MODER=0x%08x PUPDR=0x%08x AFR0=0x%08x AFR1=0x%08x (LPUART1 RX=PG7)\r\n",
		(unsigned int)GPIOG->MODER, (unsigned int)GPIOG->PUPDR,
		(unsigned int)GPIOG->AFR[0], (unsigned int)GPIOG->AFR[1]);
	hwdebug_console_printf("lpuart1: CR1=0x%08x CR2=0x%08x CR3=0x%08x BRR=0x%08x ISR=0x%08x\r\n",
		(unsigned int)LPUART1->CR1, (unsigned int)LPUART1->CR2,
		(unsigned int)LPUART1->CR3, (unsigned int)LPUART1->BRR,
		(unsigned int)LPUART1->ISR);
}

void hwdebug_lpuart_test(void)
{
	uint8_t received = 0U;
	int reset_configure;
	int reset_assert;
	int reset_release;
	int ret;

	hwdebug_test_begin("lpuart1/stm32-hal");
	ret = lpuart1_hal_init();
	lpuart1_hal_print_registers();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 initialization failed", ret);
		return;
	}

	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	k_msleep(10U);
	reset_release = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 1) : reset_configure;
	hwdebug_console_printf("keyboard reset: configure=%d assert=%d release=%d\r\n",
		reset_configure, reset_assert, reset_release);
	if (reset_configure != 0 || reset_assert != 0 || reset_release != 0) {
		hwdebug_test_fail("keyboard reset GPIO sequence failed", -EIO);
		return;
	}

	/* Enter the blocking receive immediately after reset release. */
	ret = HAL_UART_Receive(&lpuart1_hal_uart, &received, 1U,
		TEST_UART_TIMEOUT_MS);
	hwdebug_console_printf("receive: status=%u byte=0x%02x uart-error=0x%08x\r\n",
		(unsigned int)ret, (unsigned int)received,
		(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
	if (ret != HAL_OK) {
		hwdebug_test_fail("STM32 HAL LPUART1 receive failed", -ETIMEDOUT);
		return;
	}
	if (received != CMD_INITIALIZED) {
		hwdebug_test_fail("unexpected keyboard startup byte", -EPROTO);
		return;
	}
	hwdebug_test_pass();
}

void hwdebug_lpuart_host_test(void)
{
	static const uint8_t pattern[] = { 0x55U, 0xAAU, 0x00U, 0xFFU, 0x12U, 0x34U };
	uint8_t response;
	int reset_configure;
	int reset_assert;
	int ret;

	hwdebug_test_begin("lpuart1/host-echo");
	ret = lpuart1_hal_init();
	lpuart1_hal_print_registers();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 initialization failed", ret);
		return;
	}

	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	hwdebug_console_printf("keyboard reset held: configure=%d assert=%d\r\n",
		reset_configure, reset_assert);
	if (reset_configure != 0 || reset_assert != 0) {
		hwdebug_test_fail("keyboard reset hold failed", -EIO);
		return;
	}

	hwdebug_console_puts("host: echo the six bytes on COM10 at 115200 8N1\r\n");
	hwdebug_console_puts("host: 55 aa 00 ff 12 34\r\n");
	for (size_t index = 0U; index < sizeof(pattern); ++index) {
		if (HAL_UART_Transmit(&lpuart1_hal_uart, &pattern[index], 1U,
				TEST_UART_TIMEOUT_MS) != HAL_OK) {
			hwdebug_test_fail("STM32 HAL LPUART1 transmit failed", -EIO);
			return;
		}
		ret = HAL_UART_Receive(&lpuart1_hal_uart, &response, 1U,
			TEST_UART_TIMEOUT_MS);
		if (ret != HAL_OK) {
			hwdebug_console_printf("host: receive[%u] tx=0x%02x status=%u uart-error=0x%08x\r\n",
				(unsigned int)index, (unsigned int)pattern[index],
				(unsigned int)ret,
				(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
			hwdebug_test_fail("STM32 HAL LPUART1 host receive failed", -ETIMEDOUT);
			return;
		}
		if (response != pattern[index]) {
			hwdebug_console_printf("host: echo[%u] expected=0x%02x received=0x%02x\r\n",
				(unsigned int)index, (unsigned int)pattern[index],
				(unsigned int)response);
			hwdebug_test_fail("LPUART1 host echo mismatch", -EPROTO);
			return;
		}
		hwdebug_console_printf("host: echo[%u]=0x%02x\r\n", (unsigned int)index,
			(unsigned int)response);
	}
	hwdebug_console_puts("host: all echo bytes valid uart-error=0x00000000\r\n");
	hwdebug_test_pass();
}

static void lpuart_bitbang_bit(const struct gpio_dt_spec *tx, int level)
{
	(void)gpio_pin_set_dt(tx, level);
	k_busy_wait(9U);
}

static void lpuart_bitbang_byte(const struct gpio_dt_spec *tx, uint8_t value)
{
	lpuart_bitbang_bit(tx, 0);
	for (unsigned int bit = 0U; bit < 8U; ++bit) {
		lpuart_bitbang_bit(tx, (value >> bit) & 1U);
	}
	lpuart_bitbang_bit(tx, 1);
}

void hwdebug_lpuart_bitbang_test(void)
{
	static const uint8_t pattern[] = { 0x55U, 0xAAU, 0x00U, 0xFFU, 0x34U };
	uint8_t response[sizeof(pattern)] = { 0U };
	const struct gpio_dt_spec tx = {
		.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
		.pin = 0U,
		.dt_flags = 0U,
	};
	GPIO_InitTypeDef rx_gpio = {
		.Pin = GPIO_PIN_7,
		.Mode = GPIO_MODE_AF_PP,
		.Pull = GPIO_PULLUP,
		.Speed = GPIO_SPEED_FREQ_VERY_HIGH,
		.Alternate = GPIO_AF8_LPUART1,
	};
	int reset_configure;
	int reset_assert;
	int ret;

	hwdebug_test_begin("lpuart1/bitbang-echo");
	ret = lpuart1_hal_init();
	if (ret != 0) {
		hwdebug_test_fail("STM32 HAL LPUART1 RX initialization failed", ret);
		return;
	}
	reset_configure = gpio_pin_configure_dt(&keyboard_reset, GPIO_OUTPUT_INACTIVE);
	reset_assert = reset_configure == 0 ? gpio_pin_set_dt(&keyboard_reset, 0) : reset_configure;
	if (reset_configure != 0 || reset_assert != 0) {
		hwdebug_test_fail("keyboard reset hold failed", -EIO);
		return;
	}

	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	HAL_GPIO_Init(GPIOG, &rx_gpio);
	ret = gpio_pin_configure(tx.port, tx.pin, GPIO_OUTPUT_HIGH);
	if (ret != 0) {
		hwdebug_test_fail("bitbang TX GPIO configuration failed", ret);
		return;
	}

	hwdebug_console_puts("bitbang: echo 55 aa 00 ff 34 on COM10 at 115200 8N1\r\n");
	for (size_t index = 0U; index < sizeof(pattern); ++index) {
		lpuart_bitbang_byte(&tx, pattern[index]);
	}
	(void)gpio_pin_set_dt(&tx, 1);

	for (size_t index = 0U; index < sizeof(response); ++index) {
		ret = HAL_UART_Receive(&lpuart1_hal_uart, &response[index], 1U,
			TEST_UART_TIMEOUT_MS);
		if (ret != HAL_OK) {
			hwdebug_console_printf("bitbang: receive[%u] status=%u uart-error=0x%08x\r\n",
				(unsigned int)index, (unsigned int)ret,
				(unsigned int)HAL_UART_GetError(&lpuart1_hal_uart));
			hwdebug_test_fail("bitbang host receive failed", -ETIMEDOUT);
			return;
		}
	}

	hwdebug_console_puts("bitbang: rx");
	for (size_t index = 0U; index < sizeof(response); ++index) {
		hwdebug_console_printf(" %02x", (unsigned int)response[index]);
		if (response[index] != pattern[index]) {
			hwdebug_console_puts("\r\n");
			hwdebug_test_fail("bitbang host echo mismatch", -EPROTO);
			return;
		}
	}
	hwdebug_console_puts("\r\n");
	hwdebug_test_pass();
}

static int keyboard_response(const struct device *uart,
				     const uint8_t *response, size_t length)
{
	for (size_t index = 0U; index < length; ++index) {
		int ret = uart_receive(uart, response[index], TEST_UART_TIMEOUT_MS);

		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

static int keyboard_command(const struct device *uart, uint8_t command,
				    const uint8_t *payload, size_t payload_length,
				    const uint8_t *response, size_t response_length)
{
	uart_drain(uart);
	uart_poll_out(uart, command);
	for (size_t index = 0U; index < payload_length; ++index) {
		uart_poll_out(uart, payload[index]);
	}
	return keyboard_response(uart, response, response_length);
}

void hwdebug_keyboard_test(const char *port_name)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
	static const uint8_t ack_activate[] = { CMD_RETURN, CMD_KEYBOARD_ACTIVATE,
		CMD_ACK };
	static const uint8_t ack_deactivate[] = { CMD_RETURN, CMD_KEYBOARD_DEACTIVATE,
		CMD_ACK };
	static const uint8_t ack_typematic[] = { CMD_RETURN, CMD_KEYBOARD_TYPEMATIC,
		CMD_ACK, 0x2BU };
	static const uint8_t ack_debug[] = { CMD_RETURN, CMD_KEYBOARD_SET_DEBUG,
		CMD_ACK, 1U };
	static const uint8_t ack_debug_disable[] = { CMD_RETURN,
		CMD_KEYBOARD_SET_DEBUG, CMD_ACK, 0U };
	static const uint8_t id_response[] = { CMD_RETURN, CMD_KEYBOARD_ID,
		0xABU, 0x83U };
	static const uint8_t key_name_a[] = { KB_DEBUG_ACTION_PRESS, 1U, 'a' };
	static const uint8_t key_name_a_release[] = { KB_DEBUG_ACTION_RELEASE, 1U, 'a' };
	static const uint8_t make_a[] = { 0x1EU };
	static const uint8_t break_a[] = { KB_SCANCODE_BREAK, 0x1EU };
	int ret;
	ARG_UNUSED(port_name);

	hwdebug_test_begin("keyboard/protocol");
	ret = configure_keyboard_uart(uart);
	if (ret == 0) {
		ret = keyboard_reset_and_wait(uart);
	}
	if (ret == 0) {
		static const uint8_t echo[] = { CMD_KEYBOARD_ECHO };
		ret = keyboard_command(uart, CMD_KEYBOARD_ECHO, NULL, 0U,
					       echo, sizeof(echo));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_ID, NULL, 0U,
					       id_response, sizeof(id_response));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_ACTIVATE, NULL, 0U,
					       ack_activate, sizeof(ack_activate));
	}
	if (ret == 0) {
		static const uint8_t typematic[] = { 0x2BU };
		ret = keyboard_command(uart, CMD_KEYBOARD_TYPEMATIC, typematic,
					       sizeof(typematic), ack_typematic,
					       sizeof(ack_typematic));
	}
	if (ret == 0) {
		static const uint8_t enable[] = { 1U };
		ret = keyboard_command(uart, CMD_KEYBOARD_SET_DEBUG, enable,
					       sizeof(enable), ack_debug, sizeof(ack_debug));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEBUG, key_name_a,
					       sizeof(key_name_a), make_a, sizeof(make_a));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEBUG, key_name_a_release,
					       sizeof(key_name_a_release), break_a,
					       sizeof(break_a));
	}
	if (ret == 0) {
		static const uint8_t disable[] = { 0U };
		ret = keyboard_command(uart, CMD_KEYBOARD_SET_DEBUG, disable,
					       sizeof(disable), ack_debug_disable,
					       sizeof(ack_debug_disable));
	}
	if (ret == 0) {
		ret = keyboard_command(uart, CMD_KEYBOARD_DEACTIVATE, NULL, 0U,
					       ack_deactivate, sizeof(ack_deactivate));
	}
	if (ret != 0) {
		hwdebug_test_fail("keyboard startup or echo failed", ret);
	}
	hwdebug_test_pass();
}

void hwdebug_keyboard_interactive(void)
{
	hwdebug_console_puts("Keyboard interactive test: connect the controller and run the\r\n"
		"documented keyboard debug sequence from the MainApplication console.\r\n"
		"Reply with 'pass' or 'fail'. Use 'cancel' or 'exit' to leave the test.\r\n");
	interactive_waiting = INTERACTIVE_KEYBOARD;
}

static const struct ef800_t6963_config lcd_config = {
	.data = {
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 0),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 1),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 2),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 3),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 4),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 5),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 6),
		GPIO_DT_SPEC_GET_BY_IDX(LCD_NODE, data_gpios, 7),
	},
	.cd = GPIO_DT_SPEC_GET(LCD_NODE, cd_gpios),
	.wr = GPIO_DT_SPEC_GET(LCD_NODE, wr_gpios),
	.rd = GPIO_DT_SPEC_GET(LCD_NODE, rd_gpios),
	.ce = GPIO_DT_SPEC_GET(LCD_NODE, ce_gpios),
	.fs = GPIO_DT_SPEC_GET(LCD_NODE, fs_gpios),
	.reset = GPIO_DT_SPEC_GET(LCD_NODE, reset_gpios),
	.pixels_horizontal = 240,
	.pixels_vertical = 64,
	.font_width = 6,
	.memory_kb = 8,
};

void hwdebug_display_test(void)
{
	struct ef800_t6963 lcd;
	static const char text[] = "EF800 LCD TEST";
	int ret;

	hwdebug_test_begin("display/t6963-write");
	ret = ef800_t6963_init(&lcd, &lcd_config);
	if (ret == 0) {
		ret = ef800_t6963_initialize(&lcd);
	}
	if (ret == 0) {
		ret = ef800_t6963_text_cursor(&lcd, 0U, 0U);
	}
	if (ret == 0) {
		ret = ef800_t6963_write_string(&lcd, text);
	}
	if (ret == 0) {
		ret = ef800_t6963_create_line(&lcd, 0, 16, 239, 16);
	}
	if (ret == 0) {
		ret = ef800_t6963_create_line(&lcd, 0, 32, 239, 32);
	}
	if (ret != 0) {
		hwdebug_test_fail("T6963 write-only test failed", ret);
	}
	hwdebug_test_pass();
}

void hwdebug_display_interactive(void)
{
	struct ef800_t6963 lcd;
	int ret;

	hwdebug_test_begin("display/interactive");
	ret = ef800_t6963_init(&lcd, &lcd_config);
	if (ret == 0) ret = ef800_t6963_initialize(&lcd);
	if (ret == 0) ret = ef800_t6963_clear(&lcd);
	if (ret == 0) ret = ef800_t6963_text_cursor(&lcd, 0U, 0U);
	if (ret == 0) ret = ef800_t6963_write_string(&lcd, "EF800 DISPLAY TEST");
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 16, 239, 16);
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 32, 239, 32);
	if (ret == 0) ret = ef800_t6963_create_line(&lcd, 0, 0, 239, 63);
	if (ret != 0) {
		hwdebug_test_fail("T6963 interactive display failed", ret);
		hwdebug_test_pass();
		return;
	}
	hwdebug_console_puts("Display test active: verify 'EF800 DISPLAY TEST' and the line pattern.\r\n"
		"Reply with 'pass' or 'fail'. Use 'cancel' or 'exit' to leave the test.\r\n");
	interactive_waiting = INTERACTIVE_DISPLAY;
}

void hwdebug_display_wr_test(uint32_t count)
{
	const struct gpio_dt_spec wr = GPIO_DT_SPEC_GET(LCD_NODE, wr_gpios);
	const struct gpio_dt_spec rd = GPIO_DT_SPEC_GET(LCD_NODE, rd_gpios);
	const struct gpio_dt_spec ce = GPIO_DT_SPEC_GET(LCD_NODE, ce_gpios);
	const struct gpio_dt_spec cd = GPIO_DT_SPEC_GET(LCD_NODE, cd_gpios);
	int ret;

	if (count == 0U || count > 1000U) {
		hwdebug_console_puts("error: WR pulse count must be 1..1000\r\n");
		return;
	}
	hwdebug_test_begin("display/wr-gpio");
	ret = gpio_pin_configure_dt(&wr, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&rd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&ce, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&cd, GPIO_OUTPUT);
	if (ret == 0) {
		(void)gpio_pin_set_dt(&rd, 1);
		(void)gpio_pin_set_dt(&ce, 1);
		(void)gpio_pin_set_dt(&cd, 1);
		(void)gpio_pin_set_dt(&wr, 1);
		for (uint32_t index = 0U; index < count; ++index) {
			(void)gpio_pin_set_dt(&wr, 0);
			k_busy_wait(1000U);
			(void)gpio_pin_set_dt(&wr, 1);
			k_busy_wait(1000U);
		}
	}
	(void)gpio_pin_set_dt(&wr, 1);
	(void)gpio_pin_set_dt(&rd, 1);
	(void)gpio_pin_set_dt(&ce, 1);
	if (ret != 0) hwdebug_test_fail("WR GPIO pulse test failed", ret);
	hwdebug_test_pass();
}

void hwdebug_display_status(void)
{
	const struct gpio_dt_spec *data[] = {
		&lcd_config.data[0], &lcd_config.data[1], &lcd_config.data[2],
		&lcd_config.data[3], &lcd_config.data[4], &lcd_config.data[5],
		&lcd_config.data[6], &lcd_config.data[7],
	};
	int ret = 0;

	hwdebug_console_puts("display status: raw T6963 status reads\r\n");
	for (size_t index = 0U; index < ARRAY_SIZE(data); ++index) {
		ret = gpio_pin_configure_dt(data[index], GPIO_INPUT);
		if (ret != 0) {
			hwdebug_console_printf("ERROR data[%u] configure=%d\r\n",
				(unsigned int)index, ret);
			return;
		}
	}
	ret = gpio_pin_configure_dt(&lcd_config.cd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.rd, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.ce, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.wr, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.fs, GPIO_OUTPUT);
	if (ret == 0) ret = gpio_pin_configure_dt(&lcd_config.reset, GPIO_OUTPUT);
	if (ret != 0) {
		hwdebug_console_printf("ERROR control configure=%d\r\n", ret);
		return;
	}

	(void)gpio_pin_set_dt(&lcd_config.cd, 1);
	(void)gpio_pin_set_dt(&lcd_config.wr, 1);
	(void)gpio_pin_set_dt(&lcd_config.rd, 1);
	(void)gpio_pin_set_dt(&lcd_config.ce, 1);
	(void)gpio_pin_set_dt(&lcd_config.fs, 1);
	(void)gpio_pin_set_dt(&lcd_config.reset, 1);
	k_busy_wait(1000U);

	for (unsigned int sample = 0U; sample < 8U; ++sample) {
		uint8_t value = 0U;

		(void)gpio_pin_set_dt(&lcd_config.cd, 1);
		(void)gpio_pin_set_dt(&lcd_config.rd, 0);
		(void)gpio_pin_set_dt(&lcd_config.ce, 0);
		k_busy_wait(2U);
		for (size_t bit = 0U; bit < ARRAY_SIZE(data); ++bit) {
			int level = gpio_pin_get_dt(data[bit]);
			if (level < 0) {
				hwdebug_console_printf("ERROR status[%u] data[%u]=%d\r\n",
					sample, (unsigned int)bit, level);
				ret = level;
				break;
			}
			if (level != 0) value |= BIT(bit);
		}
		(void)gpio_pin_set_dt(&lcd_config.ce, 1);
		(void)gpio_pin_set_dt(&lcd_config.rd, 1);
		if (ret != 0) return;
		hwdebug_console_printf("status[%u]=0x%02x ready=%s\r\n", sample,
			value, (value & 0x03U) == 0x03U ? "yes" : "no");
		k_busy_wait(100U);
	}
}

void hwdebug_uart_test(const char *port_name)
{
	const struct device *uart = uart_for_name(port_name);
	int ret;

	hwdebug_test_begin(port_name != NULL && strcmp(port_name, "port1") == 0 ?
			   "uart3/rs232-loopback" : "uart1/rs232-loopback");
	ret = configure_uart(uart);
	if (ret == 0) {
		uart_poll_out(uart, 0x55U);
		ret = uart_receive(uart, 0x55U, TEST_UART_TIMEOUT_MS);
	}
	if (ret != 0) {
		hwdebug_test_fail("selected UART loopback failed", ret);
	}
	hwdebug_test_pass();
	hwdebug_console_puts("UART test uses 115200 8O2. Connect the PC RS232 adapter\r\n"
		"through the approved level converter and connect TX to RX for loopback.\r\n");
}

void hwdebug_storage_test(void)
{
	storage_step = 1U;
	interactive_waiting = INTERACTIVE_STORAGE;
	hwdebug_console_puts("Storage ownership test requires the normal images, not hwdebug.\r\n"
		"Step 1/5: boot MainApplication with FAT enabled and confirm a known\r\n"
		"Workspace file is readable. Reply 'next' when complete.\r\n"
		"Use 'cancel' or 'exit' to leave the test.\r\n");
}

bool hwdebug_interactive_pending(void)
{
	return interactive_waiting != INTERACTIVE_NONE;
}

void hwdebug_interactive_reply(const char *reply)
{
	if (interactive_waiting == INTERACTIVE_NONE) {
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && strcmp(reply, "next") == 0) {
		++storage_step;
		switch (storage_step) {
		case 2U:
			hwdebug_console_puts("Step 2/5: assign Port 4 to MSD and connect USB VBUS.\r\n"
				"Reply 'next' when complete.\r\n");
			break;
		case 3U:
			hwdebug_console_puts("Step 3/5: confirm logs show FAT unmount before USB MSC\r\n"
				"enable. Reply 'next' when complete.\r\n");
			break;
		case 4U:
			hwdebug_console_puts("Step 4/5: confirm the host enumerates MSC and reads the\r\n"
				"same volume without formatting. Reply 'next' when complete.\r\n");
			break;
		case 5U:
			hwdebug_console_puts("Step 5/5: disconnect USB, reset, and confirm FAT mounts\r\n"
				"again. Reply 'pass' or 'fail' for the final result.\r\n");
			break;
		default:
			break;
		}
		return;
	}
	if (interactive_waiting == INTERACTIVE_STORAGE && storage_step != 5U &&
	    (strcmp(reply, "pass") == 0 || strcmp(reply, "fail") == 0)) {
		hwdebug_console_puts("reply 'next' to continue the storage checklist\r\n");
		return;
	}
	if (strcmp(reply, "pass") == 0) {
		hwdebug_console_puts("PASS\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	} else if (strcmp(reply, "fail") == 0) {
		hwdebug_console_puts("FAIL\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	} else if (strcmp(reply, "cancel") == 0 || strcmp(reply, "exit") == 0) {
		hwdebug_console_puts("interactive test cancelled\r\n");
		interactive_waiting = INTERACTIVE_NONE;
	}
}
