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
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_qspi.h>

#include "flash.h"
#include "test_result.h"

#define console_puts hwdebug_console_puts
#define console_printf hwdebug_console_printf
#define console_hexdump hwdebug_console_hexdump

#define QSPI_CONTROLLER_NODE DT_NODELABEL(quadspi)
#define QSPI_MAX_DUMP 256U
#define SFDP_MAX_OFFSET 4096U
#define QSPI_SIZE_BYTES (4U * 1024U * 1024U)
#define QSPI_TEST_CLOCK_HZ 1000000U
#define QSPI_TEST_READ_SIZE 16U
#define QSPI_RECOVERY_WAIT_MS 1U
#define QSPI_BITBANG_HALF_PERIOD_US 2U
#define CONSOLE_OUTPUT_SIZE 192U
#define DEBUG_WATCHDOG_TIMEOUT_MS 10000U

#define QSPI_CMD_READ_ID 0x9fU
#define QSPI_CMD_READ_ID_QPI 0xafU
#define QSPI_CMD_READ_STATUS 0x05U
#define QSPI_CMD_READ_SFDP 0x5aU
#define QSPI_CMD_READ_DATA 0x03U
#define QSPI_CMD_FAST_READ 0x0bU
#define QSPI_CMD_FAST_READ_QUAD_OUTPUT 0x6bU
#define QSPI_CMD_FAST_READ_QUAD_IO 0xebU
#define QSPI_CMD_ENTER_QPI 0x35U
#define QSPI_CMD_EXIT_QPI 0xf5U
#define QSPI_CMD_RELEASE_POWER_DOWN 0xabU
#define QSPI_CMD_RESET_ENABLE 0x66U
#define QSPI_CMD_RESET_MEMORY 0x99U

#define QSPI_STATUS_QE BIT(6)

#define EXPECTED_JEDEC_MANUFACTURER 0x9dU
#define EXPECTED_JEDEC_TYPE 0x60U
#define EXPECTED_JEDEC_CAPACITY 0x16U

PINCTRL_DT_DEFINE(QSPI_CONTROLLER_NODE);

static const struct stm32_pclken qspi_clock =
	STM32_CLOCK_INFO(0, QSPI_CONTROLLER_NODE);
static const struct pinctrl_dev_config *const qspi_pinctrl =
	PINCTRL_DT_DEV_CONFIG_GET(QSPI_CONTROLLER_NODE);
static QSPI_HandleTypeDef qspi_handle;
static uint32_t qspi_clock_hz;
static HAL_StatusTypeDef qspi_init_status = HAL_ERROR;
static bool qspi_initialized;
static const struct device *const gpioe = DEVICE_DT_GET(DT_NODELABEL(gpioe));
static const struct device *const gpiof = DEVICE_DT_GET(DT_NODELABEL(gpiof));
#define QSPI_GPIO_NCS 11U /* PE11 */
#define QSPI_GPIO_CLK 10U /* PF10 */
#define QSPI_GPIO_IO0 8U  /* PF8 */
#define QSPI_GPIO_IO1 9U  /* PF9 */
#define QSPI_GPIO_IO2 7U  /* PF7 */
#define QSPI_GPIO_IO3 6U  /* PF6 */
#if DT_HAS_ALIAS(watchdog0) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
static const struct device *const debug_watchdog =
	DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int debug_watchdog_channel = -1;
#endif
static bool test_quiet;
static bool backend_test_failed;

struct qspi_transaction {
	const char *name;
	const char *format;
	uint8_t instruction;
	uint32_t instruction_mode;
	uint32_t address_mode;
	uint32_t alternate_byte_mode;
	uint32_t alternate_bytes;
	uint32_t dummy_cycles;
	uint32_t data_mode;
};

static bool jedec_is_expected(const uint8_t id[3]);
static const char *response_class(const uint8_t *data, size_t length);
static int qspi_direct_init(uint32_t flash_id);
static int run_read(const struct qspi_transaction *transaction, uint32_t address,
			uint8_t *data, size_t length, bool dump);
static const struct qspi_transaction spi_jedec;
static void run_test(const char *name, int (*test)(void));
void hwdebug_flash_test_all(void);

static int debug_watchdog_init(void)
{
#if DT_HAS_ALIAS(watchdog0) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	const struct wdt_timeout_cfg timeout = {
		.window = {
			.min = 0U,
			.max = DEBUG_WATCHDOG_TIMEOUT_MS,
		},
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ret;

	if (!device_is_ready(debug_watchdog)) {
		return -ENODEV;
	}
	debug_watchdog_channel = wdt_install_timeout(debug_watchdog, &timeout);
	if (debug_watchdog_channel < 0) {
		return debug_watchdog_channel;
	}
	ret = wdt_setup(debug_watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret != 0) {
		return ret;
	}
	return wdt_feed(debug_watchdog, debug_watchdog_channel);
#else
	return -ENODEV;
#endif
}

static void debug_watchdog_feed(void)
{
#if DT_HAS_ALIAS(watchdog0) && DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	if (debug_watchdog_channel >= 0) {
		(void)wdt_feed(debug_watchdog, debug_watchdog_channel);
	}
#endif
}

void hwdebug_console_puts(const char *text)
{
	const struct device *console_uart = hwdebug_console_device();

	while (*text != '\0') {
		uart_poll_out(console_uart, (unsigned char)*text++);
	}
}

void hwdebug_console_printf(const char *format, ...)
{
	char output[CONSOLE_OUTPUT_SIZE];
	va_list arguments;

	va_start(arguments, format);
	vsnprintk(output, sizeof(output), format, arguments);
	va_end(arguments);
	hwdebug_console_puts(output);
}

void hwdebug_console_hexdump(uint32_t offset, const uint8_t *data, size_t length)
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
		hwdebug_console_puts(output);
	}
}

static int qspi_hal_result(HAL_StatusTypeDef status)
{
	return status == HAL_OK ? 0 : -EIO;
}

static void qspi_abort(void)
{
	if (qspi_initialized) {
		(void)HAL_QSPI_Abort(&qspi_handle);
	}
}

static int qspi_bitbang_prepare_pins(bool high)
{
	static const uint8_t data_pins[] = {
		QSPI_GPIO_IO0, QSPI_GPIO_IO1, QSPI_GPIO_IO2, QSPI_GPIO_IO3
	};
	int ret;

	if (qspi_initialized) {
		qspi_abort();
		(void)HAL_QSPI_DeInit(&qspi_handle);
		qspi_initialized = false;
	}
	if (!device_is_ready(gpioe) || !device_is_ready(gpiof)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure(gpioe, QSPI_GPIO_NCS, GPIO_OUTPUT_HIGH);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_configure(gpiof, QSPI_GPIO_CLK, GPIO_OUTPUT_HIGH);
	if (ret != 0) {
		return ret;
	}
	for (size_t index = 0U; index < ARRAY_SIZE(data_pins); ++index) {
		ret = gpio_pin_configure(gpiof, data_pins[index], GPIO_OUTPUT_HIGH);
		if (ret != 0) {
			return ret;
		}
	}
	ret = gpio_pin_set(gpioe, QSPI_GPIO_NCS, high);
	if (ret == 0) {
		ret = gpio_pin_set(gpiof, QSPI_GPIO_CLK, high);
	}
	if (ret == 0) {
		for (size_t index = 0U; index < ARRAY_SIZE(data_pins); ++index) {
			ret = gpio_pin_set(gpiof, data_pins[index], high);
			if (ret != 0) {
				break;
			}
		}
	}
	if (ret == 0) {
	}
	return ret;
}

static int qspi_bitbang_prepare(void)
{
	int ret;

	ret = qspi_bitbang_prepare_pins(true);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_set(gpiof, QSPI_GPIO_CLK, 0);
	if (ret == 0) {
		ret = gpio_pin_set(gpiof, QSPI_GPIO_IO0, 0);
	}
	return ret;
}

static void qspi_bitbang_clock_high(void)
{
	(void)gpio_pin_set(gpiof, QSPI_GPIO_CLK, 1);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
}

static void qspi_bitbang_clock_low(void)
{
	(void)gpio_pin_set(gpiof, QSPI_GPIO_CLK, 0);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
}

static void qspi_bitbang_spi_write_byte(uint8_t value)
{
	for (uint8_t mask = BIT(7); mask != 0U; mask >>= 1) {
		(void)gpio_pin_set(gpiof, QSPI_GPIO_IO0, (value & mask) != 0U);
		qspi_bitbang_clock_high();
		qspi_bitbang_clock_low();
	}
}

static uint8_t qspi_bitbang_spi_read_byte(void)
{
	uint8_t value = 0U;

	for (uint8_t bit = 0U; bit < 8U; ++bit) {
		qspi_bitbang_clock_high();
		value = (uint8_t)((value << 1) |
			(gpio_pin_get(gpiof, QSPI_GPIO_IO1) > 0 ? 1U : 0U));
		qspi_bitbang_clock_low();
	}
	return value;
}

static int qspi_bitbang_spi_jedec(uint8_t id[3])
{
	int ret = qspi_bitbang_prepare();

	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_configure(gpiof, QSPI_GPIO_IO1, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 0);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
	qspi_bitbang_spi_write_byte(QSPI_CMD_READ_ID);
	for (size_t index = 0U; index < 3U; ++index) {
		id[index] = qspi_bitbang_spi_read_byte();
	}
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 1);
	return 0;
}

static void qspi_bitbang_qpi_write_nibble(uint8_t value)
{
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO0, value & BIT(0));
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO1, value & BIT(1));
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO2, value & BIT(2));
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO3, value & BIT(3));
	qspi_bitbang_clock_high();
	qspi_bitbang_clock_low();
}

static uint8_t qspi_bitbang_qpi_read_nibble(void)
{
	uint8_t value;

	qspi_bitbang_clock_high();
	value = (gpio_pin_get(gpiof, QSPI_GPIO_IO0) > 0 ? BIT(0) : 0U) |
		(gpio_pin_get(gpiof, QSPI_GPIO_IO1) > 0 ? BIT(1) : 0U) |
		(gpio_pin_get(gpiof, QSPI_GPIO_IO2) > 0 ? BIT(2) : 0U) |
		(gpio_pin_get(gpiof, QSPI_GPIO_IO3) > 0 ? BIT(3) : 0U);
	qspi_bitbang_clock_low();
	return value;
}

static int qspi_bitbang_qpi_jedec(uint8_t id[3])
{
	static const uint8_t data_pins[] = {
		QSPI_GPIO_IO0, QSPI_GPIO_IO1, QSPI_GPIO_IO2, QSPI_GPIO_IO3
	};
	int ret = qspi_bitbang_prepare();

	if (ret != 0) {
		return ret;
	}
	/* Enter QPI using the SPI-framed 0x35 command before changing IO width. */
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO1, 0);
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO2, 1);
	(void)gpio_pin_set(gpiof, QSPI_GPIO_IO3, 1);
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 0);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
	qspi_bitbang_spi_write_byte(QSPI_CMD_ENTER_QPI);
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 1);
	k_busy_wait(QSPI_RECOVERY_WAIT_MS * 1000U);
	console_puts("qspi-bitbang-qpi: enter command sent\r\n");

	/* The flash is now in QPI mode; send 0x9F on all four IO lines. */
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 0);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
	qspi_bitbang_qpi_write_nibble(QSPI_CMD_READ_ID >> 4);
	qspi_bitbang_qpi_write_nibble(QSPI_CMD_READ_ID & 0x0fU);
	for (size_t index = 0U; index < ARRAY_SIZE(data_pins); ++index) {
		ret = gpio_pin_configure(gpiof, data_pins[index], GPIO_INPUT);
		if (ret != 0) {
			(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 1);
			return ret;
		}
	}
	for (size_t index = 0U; index < 3U; ++index) {
		id[index] = (uint8_t)(qspi_bitbang_qpi_read_nibble() << 4);
		id[index] |= qspi_bitbang_qpi_read_nibble();
	}
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 1);
	return 0;
}

static int qspi_bitbang_qpi_exit(void)
{
	int ret = qspi_bitbang_prepare_pins(false);

	if (ret != 0) {
		return ret;
	}
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 0);
	k_busy_wait(QSPI_BITBANG_HALF_PERIOD_US);
	qspi_bitbang_qpi_write_nibble(QSPI_CMD_EXIT_QPI >> 4);
	qspi_bitbang_qpi_write_nibble(QSPI_CMD_EXIT_QPI & 0x0fU);
	(void)gpio_pin_set(gpioe, QSPI_GPIO_NCS, 1);
	k_busy_wait(QSPI_RECOVERY_WAIT_MS * 1000U);
	return 0;
}

static void cmd_qspi_bitbang(const char *mode)
{
	uint8_t id[3] = {0};
	int ret;

	if (strcmp(mode, "spi") == 0) {
		ret = qspi_bitbang_spi_jedec(id);
	} else if (strcmp(mode, "qpi") == 0) {
		ret = qspi_bitbang_qpi_jedec(id);
	} else {
		console_puts("error: usage: qspi bitbang spi|qpi\r\n");
		return;
	}
	if (ret != 0) {
		console_printf("qspi-bitbang-%s: failed error=%d\r\n", mode, ret);
		return;
	}
	console_printf("qspi-bitbang-%s: JEDEC %02x %02x %02x class=%s expected=%s\r\n",
		       mode, id[0], id[1], id[2], response_class(id, sizeof(id)),
		       jedec_is_expected(id) ? "yes" : "no");
	if (strcmp(mode, "qpi") == 0) {
		console_printf("qspi-bitbang-qpi: exit=%d\r\n", qspi_bitbang_qpi_exit());
	}
}

static int qspi_run_transaction(const struct qspi_transaction *transaction,
					uint32_t address, uint8_t *data, size_t length)
{
	QSPI_CommandTypeDef command = {
		.Instruction = transaction->instruction,
		.Address = address,
		.AlternateBytes = transaction->alternate_bytes,
		.AddressSize = QSPI_ADDRESS_24_BITS,
		.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS,
		.DummyCycles = transaction->dummy_cycles,
		.InstructionMode = transaction->instruction_mode,
		.AddressMode = transaction->address_mode,
		.AlternateByteMode = transaction->alternate_byte_mode,
		.DataMode = transaction->data_mode,
		.NbData = length,
		.DdrMode = QSPI_DDR_MODE_DISABLE,
		.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY,
		.SIOOMode = QSPI_SIOO_INST_EVERY_CMD,
	};

	if (!qspi_initialized || length > UINT32_MAX) {
		return -EINVAL;
	}
	debug_watchdog_feed();
	if (qspi_hal_result(HAL_QSPI_Command(&qspi_handle, &command,
					     HAL_QSPI_TIMEOUT_DEFAULT_VALUE)) != 0) {
		qspi_abort();
		return -EIO;
	}
	if (data == NULL || length == 0U) {
		return 0;
	}
	if (qspi_hal_result(HAL_QSPI_Receive(&qspi_handle, data,
					    HAL_QSPI_TIMEOUT_DEFAULT_VALUE)) != 0) {
		qspi_abort();
		return -EIO;
	}
	debug_watchdog_feed();
	return 0;
}

static int qspi_direct_init(uint32_t flash_id)
{
	uint32_t ahb_clock;
	uint32_t prescaler;
	int ret;

	ARG_UNUSED(flash_id);

	if (qspi_initialized) {
		return 0;
	}
	if (qspi_initialized) {
		qspi_abort();
		qspi_initialized = false;
	}
	ret = pinctrl_apply_state(qspi_pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}
	ret = clock_control_on(DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE),
				       (clock_control_subsys_t)&qspi_clock);
	if (ret != 0) {
		return ret;
	}
	/* GPIO bit-bang tests can leave QUADSPI state and flags latched. */
	__HAL_RCC_QSPI_FORCE_RESET();
	__HAL_RCC_QSPI_RELEASE_RESET();
	ret = clock_control_get_rate(DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE),
					     (clock_control_subsys_t)&qspi_clock,
					     &ahb_clock);
	if (ret != 0) {
		return ret;
	}
	qspi_clock_hz = ahb_clock;
	for (prescaler = 0U; prescaler < 256U; ++prescaler) {
		if (ahb_clock / (prescaler + 1U) <= QSPI_TEST_CLOCK_HZ) {
			break;
		}
	}
	if (prescaler >= 256U) {
		return -EINVAL;
	}

	qspi_handle.Instance = (QUADSPI_TypeDef *)DT_REG_ADDR_BY_IDX(
		QSPI_CONTROLLER_NODE, 0);
	qspi_handle.Init.ClockPrescaler = prescaler;
	qspi_handle.Init.FifoThreshold = 1U;
	qspi_handle.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
	qspi_handle.Init.FlashSize = 21U;
	qspi_handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
	qspi_handle.Init.ClockMode = QSPI_CLOCK_MODE_0;
	qspi_handle.Init.FlashID = QSPI_FLASH_ID_1;
	qspi_handle.Init.DualFlash = QSPI_DUALFLASH_DISABLE;

	qspi_init_status = HAL_QSPI_Init(&qspi_handle);
	qspi_initialized = qspi_init_status == HAL_OK;
	return qspi_initialized ? 0 : -EIO;
}

static bool buffer_is_value(const uint8_t *data, size_t length, uint8_t value)
{
	for (size_t index = 0U; index < length; ++index) {
		if (data[index] != value) {
			return false;
		}
	}
	return true;
}

static bool jedec_is_expected(const uint8_t id[3])
{
	return id[0] == EXPECTED_JEDEC_MANUFACTURER &&
		id[1] == EXPECTED_JEDEC_TYPE && id[2] == EXPECTED_JEDEC_CAPACITY;
}

static bool response_is_plausible(const uint8_t *data, size_t length)
{
	return !buffer_is_value(data, length, 0x00U) &&
		!buffer_is_value(data, length, 0xffU);
}

static const char *response_class(const uint8_t *data, size_t length)
{
	if (buffer_is_value(data, length, 0x00U)) {
		return "all-00";
	}
	if (buffer_is_value(data, length, 0xffU)) {
		return "all-ff";
	}
	return "data";
}

static void print_transaction(const struct qspi_transaction *transaction,
				      uint32_t address, size_t length)
{
	if (test_quiet) {
		return;
	}
	console_printf("transaction: %s opcode=%02x format=%s dummy=%u",
		       transaction->name, transaction->instruction,
		       transaction->format, transaction->dummy_cycles);
	if (transaction->address_mode != QSPI_ADDRESS_NONE) {
		console_printf(" address=0x%06x", address);
	}
	if (transaction->alternate_byte_mode != QSPI_ALTERNATE_BYTES_NONE) {
		console_printf(" mode=%02x", transaction->alternate_bytes & 0xffU);
	}
	console_printf(" length=%u\r\n", (unsigned int)length);
}

static int run_read(const struct qspi_transaction *transaction, uint32_t address,
			    uint8_t *data, size_t length, bool dump)
{
	int ret;

	memset(data, 0, length);
	print_transaction(transaction, address, length);
	ret = qspi_run_transaction(transaction, address, data, length);
	if (ret != 0) {
		backend_test_failed = true;
		console_printf("result: error=%d hal=%u qspi-error=0x%08x\r\n",
			       ret, qspi_init_status, HAL_QSPI_GetError(&qspi_handle));
		return ret;
	}
	if (!test_quiet) {
		console_printf("result: ok class=%s\r\n", response_class(data, length));
	}
	if (dump) {
		console_hexdump(address, data, length);
	} else {
		if (test_quiet) {
			return 0;
		}
		console_puts("data:");
		for (size_t index = 0U; index < length; ++index) {
			console_printf(" %02x", data[index]);
		}
		console_puts("\r\n");
	}
	return 0;
}

static int run_command(const struct qspi_transaction *transaction)
{
	int ret;

	print_transaction(transaction, 0U, 0U);
	ret = qspi_run_transaction(transaction, 0U, NULL, 0U);
	if (!test_quiet) {
		console_printf("result: %s error=%d qspi-error=0x%08x\r\n",
		       ret == 0 ? "sent" : "failed", ret,
		       HAL_QSPI_GetError(&qspi_handle));
	}
	return ret;
}

static const struct qspi_transaction spi_jedec = {
	.name = "spi-jedec",
	.format = "1-0-1",
	.instruction = QSPI_CMD_READ_ID,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_NONE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.data_mode = QSPI_DATA_1_LINE,
};

static const struct qspi_transaction qpi_jedec_af = {
	.name = "qpi-jedec-af",
	.format = "4-0-4",
	.instruction = QSPI_CMD_READ_ID_QPI,
	.instruction_mode = QSPI_INSTRUCTION_4_LINES,
	.address_mode = QSPI_ADDRESS_NONE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.data_mode = QSPI_DATA_4_LINES,
};

static const struct qspi_transaction qpi_jedec_9f = {
	.name = "qpi-jedec-9f",
	.format = "4-0-4",
	.instruction = QSPI_CMD_READ_ID,
	.instruction_mode = QSPI_INSTRUCTION_4_LINES,
	.address_mode = QSPI_ADDRESS_NONE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.data_mode = QSPI_DATA_4_LINES,
};

static const struct qspi_transaction spi_status = {
	.name = "spi-status",
	.format = "1-0-1",
	.instruction = QSPI_CMD_READ_STATUS,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_NONE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.data_mode = QSPI_DATA_1_LINE,
};

static const struct qspi_transaction spi_sfdp = {
	.name = "spi-sfdp",
	.format = "1-1-1",
	.instruction = QSPI_CMD_READ_SFDP,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_1_LINE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.dummy_cycles = 8U,
	.data_mode = QSPI_DATA_1_LINE,
};

static const struct qspi_transaction spi_read = {
	.name = "spi-read",
	.format = "1-1-1",
	.instruction = QSPI_CMD_READ_DATA,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_1_LINE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.data_mode = QSPI_DATA_1_LINE,
};

static const struct qspi_transaction spi_fast_read = {
	.name = "spi-fast-read",
	.format = "1-1-1",
	.instruction = QSPI_CMD_FAST_READ,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_1_LINE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.dummy_cycles = 8U,
	.data_mode = QSPI_DATA_1_LINE,
};

static const struct qspi_transaction qspi_quad_output_read = {
	.name = "qspi-quad-output-read",
	.format = "1-1-4",
	.instruction = QSPI_CMD_FAST_READ_QUAD_OUTPUT,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_1_LINE,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.dummy_cycles = 8U,
	.data_mode = QSPI_DATA_4_LINES,
};

static const struct qspi_transaction qspi_quad_io_read = {
	.name = "qspi-quad-io-read",
	.format = "1-4-4",
	.instruction = QSPI_CMD_FAST_READ_QUAD_IO,
	.instruction_mode = QSPI_INSTRUCTION_1_LINE,
	.address_mode = QSPI_ADDRESS_4_LINES,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_4_LINES,
	.alternate_bytes = 0x00U,
	.dummy_cycles = 4U,
	.data_mode = QSPI_DATA_4_LINES,
};

static const struct qspi_transaction qpi_fast_read = {
	.name = "qpi-fast-read",
	.format = "4-4-4",
	.instruction = QSPI_CMD_FAST_READ,
	.instruction_mode = QSPI_INSTRUCTION_4_LINES,
	.address_mode = QSPI_ADDRESS_4_LINES,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
	.dummy_cycles = 6U,
	.data_mode = QSPI_DATA_4_LINES,
};

static const struct qspi_transaction qpi_quad_io_read = {
	.name = "qpi-quad-io-read",
	.format = "4-4-4",
	.instruction = QSPI_CMD_FAST_READ_QUAD_IO,
	.instruction_mode = QSPI_INSTRUCTION_4_LINES,
	.address_mode = QSPI_ADDRESS_4_LINES,
	.alternate_byte_mode = QSPI_ALTERNATE_BYTES_4_LINES,
	.alternate_bytes = 0x00U,
	.dummy_cycles = 4U,
	.data_mode = QSPI_DATA_4_LINES,
};

static struct qspi_transaction command_transaction(const char *name,
						    uint8_t instruction,
						    uint32_t instruction_mode)
{
	return (struct qspi_transaction) {
		.name = name,
		.format = instruction_mode == QSPI_INSTRUCTION_4_LINES ? "4-0-0" :
			"1-0-0",
		.instruction = instruction,
		.instruction_mode = instruction_mode,
		.address_mode = QSPI_ADDRESS_NONE,
		.alternate_byte_mode = QSPI_ALTERNATE_BYTES_NONE,
		.data_mode = QSPI_DATA_NONE,
	};
}

static int qspi_recover(bool verbose)
{
	uint8_t id[3] = {0};
	struct qspi_transaction transaction;
	int ret;

	ret = qspi_direct_init(QSPI_FLASH_ID_1);
	if (ret != 0) {
		console_printf("recovery: init failed error=%d hal=%u qspi-error=0x%08x\r\n",
			       ret, qspi_init_status, HAL_QSPI_GetError(&qspi_handle));
		return ret;
	}
	qspi_abort();
	if (verbose) {
		console_puts("recovery: begin\r\n");
	}

	transaction = command_transaction("qpi-release-power-down",
						  QSPI_CMD_RELEASE_POWER_DOWN,
						  QSPI_INSTRUCTION_4_LINES);
	(void)run_command(&transaction);
	k_msleep(QSPI_RECOVERY_WAIT_MS);
	transaction = command_transaction("qpi-reset-enable", QSPI_CMD_RESET_ENABLE,
						  QSPI_INSTRUCTION_4_LINES);
	(void)run_command(&transaction);
	transaction = command_transaction("qpi-reset", QSPI_CMD_RESET_MEMORY,
						  QSPI_INSTRUCTION_4_LINES);
	(void)run_command(&transaction);
	k_msleep(QSPI_RECOVERY_WAIT_MS);
	transaction = command_transaction("qpi-exit", QSPI_CMD_EXIT_QPI,
						  QSPI_INSTRUCTION_4_LINES);
	(void)run_command(&transaction);

	transaction = command_transaction("spi-release-power-down",
						  QSPI_CMD_RELEASE_POWER_DOWN,
						  QSPI_INSTRUCTION_1_LINE);
	(void)run_command(&transaction);
	k_msleep(QSPI_RECOVERY_WAIT_MS);
	transaction = command_transaction("spi-reset-enable", QSPI_CMD_RESET_ENABLE,
						  QSPI_INSTRUCTION_1_LINE);
	(void)run_command(&transaction);
	transaction = command_transaction("spi-reset", QSPI_CMD_RESET_MEMORY,
						  QSPI_INSTRUCTION_1_LINE);
	(void)run_command(&transaction);
	k_msleep(QSPI_RECOVERY_WAIT_MS);

	ret = run_read(&spi_jedec, 0U, id, sizeof(id), false);
	if (ret == 0 && response_is_plausible(id, sizeof(id))) {
		console_printf("recovery: SPI response plausible expected=%s\r\n",
			       jedec_is_expected(id) ? "yes" : "no");
		return 0;
	}
	console_puts("recovery: no plausible SPI JEDEC response\r\n");
	return ret != 0 ? ret : -ENODEV;
}

static int recover_before_testcase(const char *name)
{
	int ret;

	console_printf("recovery-before: %s\r\n", name);
	ret = qspi_recover(false);
	console_printf("recovery-before: %s (%d)\r\n",
		       ret == 0 ? "complete" : "unconfirmed", ret);
	return ret;
}

static int recover_after_testcase(const char *name)
{
	int ret;

	console_printf("recovery-after: %s\r\n", name);
	ret = qspi_recover(false);
	console_printf("recovery-after: %s (%d)\r\n",
		       ret == 0 ? "complete" : "unconfirmed", ret);
	return ret;
}

static void cmd_flash_info(void)
{
	uint8_t id[3] = {0};
	int ret = qspi_direct_init(QSPI_FLASH_ID_1);

	console_printf("device: IS25LP032D-JTLE-TR\r\ncontroller-ready: %s\r\n",
		       ret == 0 ? "yes" : "no");
	console_printf("size: %u bytes (32 Mbit)\r\n", QSPI_SIZE_BYTES);
	console_printf("controller: STM32 QUADSPI Bank 1 at %u Hz\r\n",
		       qspi_initialized ? qspi_clock_hz /
			       (qspi_handle.Init.ClockPrescaler + 1U) : 0U);
	console_puts("protocols: SPI 1-1-1, QSPI 1-1-4/1-4-4, QPI 4-4-4\r\n");
	console_puts("pins: IO0=PF8 IO1=PF9 IO2=PF7 IO3=PF6 CLK=PF10 NCS=PE11\r\n");
	if (ret == 0 && run_read(&spi_jedec, 0U, id, sizeof(id), false) == 0) {
		console_printf("JEDEC expected: %s\r\n", jedec_is_expected(id) ? "yes" : "no");
	}
}

static void cmd_flash_probe(void)
{
	uint8_t id[3];

	if (qspi_direct_init(QSPI_FLASH_ID_1) != 0) {
		console_puts("probe: QUADSPI controller initialization failed\r\n");
		return;
	}
	console_puts("probe: read-only current-state attempts, no recovery\r\n");
	(void)run_read(&qpi_jedec_af, 0U, id, sizeof(id), false);
	(void)run_read(&qpi_jedec_9f, 0U, id, sizeof(id), false);
	(void)run_read(&spi_jedec, 0U, id, sizeof(id), false);
}

static void cmd_flash_jedec(void)
{
	uint8_t id[3];

	if (qspi_direct_init(QSPI_FLASH_ID_1) == 0 &&
		run_read(&spi_jedec, 0U, id, sizeof(id), false) == 0) {
		console_printf("expected IS25LP032D: %s\r\n",
			       jedec_is_expected(id) ? "yes" : "no");
	}
}

static void cmd_flash_sfdp(uint32_t offset, uint32_t length)
{
	uint8_t data[QSPI_MAX_DUMP];
	int ret;

	if (offset > SFDP_MAX_OFFSET || length == 0U || length > QSPI_MAX_DUMP) {
		console_printf("error: use offset <= %u and length 1..%u\r\n",
			       SFDP_MAX_OFFSET, QSPI_MAX_DUMP);
		return;
	}
	ret = qspi_direct_init(QSPI_FLASH_ID_1);
	if (ret != 0 || run_read(&spi_sfdp, offset, data, length, true) != 0) {
		return;
	}
	if (offset == 0U && length >= 4U) {
		console_printf("SFDP signature: %s\r\n",
			       memcmp(data, "SFDP", 4U) == 0 ? "valid" : "invalid");
	}
}

static void cmd_flash_read(uint32_t offset, uint32_t length)
{
	uint8_t data[QSPI_MAX_DUMP];

	if (length == 0U || length > QSPI_MAX_DUMP ||
		(uint64_t)offset + length > QSPI_SIZE_BYTES) {
		console_puts("error: read range is outside 4 MiB or exceeds 256 bytes\r\n");
		return;
	}
	if (qspi_direct_init(QSPI_FLASH_ID_1) == 0) {
		(void)run_read(&spi_read, offset, data, length, true);
	}
}

static void print_status(uint8_t status)
{
	console_printf("status: %02x WIP=%u WEL=%u QE=%u\r\n", status,
		       status & BIT(0) ? 1U : 0U, status & BIT(1) ? 1U : 0U,
		       status & QSPI_STATUS_QE ? 1U : 0U);
}

static void compare_read(const char *name, const uint8_t *reference,
			 const uint8_t *data, size_t length)
{
	bool match = memcmp(reference, data, length) == 0;
	if (!match) {
		backend_test_failed = true;
	}
	if (!test_quiet) {
		console_printf("compare: %s=%s\r\n", name, match ? "match" : "different");
	}
}

static bool test_begin(const char *name)
{
	int recovery_result;

	console_printf("test %s: begin\r\n", name);
	if (qspi_direct_init(QSPI_FLASH_ID_1) != 0) {
		console_puts("test: QUADSPI controller initialization failed\r\n");
		return false;
	}
	recovery_result = recover_before_testcase(name);
	if (recovery_result != 0) {
		console_puts("test: continuing despite unconfirmed recovery\r\n");
	}
	return true;
}

static void test_end(const char *name)
{
	(void)recover_after_testcase(name);
	console_printf("test %s: end\r\n", name);
}

static void backend_test_spi(void)
{
	uint8_t id[3];
	uint8_t status[1];
	uint8_t sfdp[16];
	uint8_t first[QSPI_TEST_READ_SIZE];
	uint8_t second[QSPI_TEST_READ_SIZE];

	if (!test_begin("spi")) {
		test_end("spi");
		return;
	}
	if (run_read(&spi_jedec, 0U, id, sizeof(id), false) == 0) {
		console_printf("JEDEC expected: %s\r\n",
			       jedec_is_expected(id) ? "yes" : "no");
	}
	if (run_read(&spi_status, 0U, status, sizeof(status), false) == 0) {
		print_status(status[0]);
	}
	if (run_read(&spi_sfdp, 0U, sfdp, sizeof(sfdp), true) == 0) {
		console_printf("SFDP signature: %s\r\n",
			       memcmp(sfdp, "SFDP", 4U) == 0 ? "valid" : "invalid");
	}
	if (run_read(&spi_read, 0U, first, sizeof(first), true) == 0 &&
		run_read(&spi_read, 0U, second, sizeof(second), true) == 0) {
		compare_read("spi-repeat", first, second, sizeof(first));
	}
	test_end("spi");
}

static void backend_test_qspi_1line(void)
{
	uint8_t normal[QSPI_TEST_READ_SIZE];
	uint8_t fast[QSPI_TEST_READ_SIZE];

	if (!test_begin("qspi-1line")) {
		test_end("qspi-1line");
		return;
	}
	if (run_read(&spi_read, 0U, normal, sizeof(normal), true) == 0 &&
		run_read(&spi_fast_read, 0U, fast, sizeof(fast), true) == 0) {
		compare_read("qspi-1line-fast", normal, fast, sizeof(normal));
	}
	test_end("qspi-1line");
}

static void backend_test_qspi_4line(void)
{
	uint8_t status[1];
	uint8_t reference[QSPI_TEST_READ_SIZE];
	uint8_t quad_output[QSPI_TEST_READ_SIZE];
	uint8_t quad_io[QSPI_TEST_READ_SIZE];

	if (!test_begin("qspi-4line")) {
		test_end("qspi-4line");
		return;
	}
	if (run_read(&spi_status, 0U, status, sizeof(status), false) == 0) {
		print_status(status[0]);
		if ((status[0] & QSPI_STATUS_QE) == 0U) {
			console_puts("note: QE=0; ISSI quad SPI reads are not expected to work\r\n");
		}
	}
	if (run_read(&spi_read, 0U, reference, sizeof(reference), true) == 0) {
		if (run_read(&qspi_quad_output_read, 0U, quad_output,
			     sizeof(quad_output), true) == 0) {
			compare_read("qspi-1-1-4", reference, quad_output,
				     sizeof(reference));
		}
		if (run_read(&qspi_quad_io_read, 0U, quad_io, sizeof(quad_io), true) == 0) {
			compare_read("qspi-1-4-4", reference, quad_io, sizeof(reference));
		}
	}
	test_end("qspi-4line");
}

static void backend_test_qpi(void)
{
	uint8_t id[3];
	uint8_t first[QSPI_TEST_READ_SIZE];
	uint8_t second[QSPI_TEST_READ_SIZE];
	struct qspi_transaction transaction;

	if (!test_begin("qpi")) {
		test_end("qpi");
		return;
	}
	transaction = command_transaction("spi-enter-qpi", QSPI_CMD_ENTER_QPI,
						  QSPI_INSTRUCTION_1_LINE);
	(void)run_command(&transaction);
	if (run_read(&qpi_jedec_af, 0U, id, sizeof(id), false) == 0) {
		console_printf("QPI AF expected: %s\r\n",
			       jedec_is_expected(id) ? "yes" : "no");
	}
	if (run_read(&qpi_jedec_9f, 0U, id, sizeof(id), false) == 0) {
		console_printf("QPI 9F expected: %s\r\n",
			       jedec_is_expected(id) ? "yes" : "no");
	}
	if (run_read(&qpi_fast_read, 0U, first, sizeof(first), true) == 0 &&
		run_read(&qpi_quad_io_read, 0U, second, sizeof(second), true) == 0) {
		compare_read("qpi-0b-vs-eb", first, second, sizeof(first));
	}
	transaction = command_transaction("qpi-exit", QSPI_CMD_EXIT_QPI,
						  QSPI_INSTRUCTION_4_LINES);
	(void)run_command(&transaction);
	test_end("qpi");
}

int hwdebug_flash_test_spi(void)
{
	backend_test_failed = false;
	test_quiet = true;
	backend_test_spi();
	test_quiet = false;
	return backend_test_failed ? -EIO : 0;
}

int hwdebug_flash_test_qspi_1line(void)
{
	backend_test_failed = false;
	test_quiet = true;
	backend_test_qspi_1line();
	test_quiet = false;
	return backend_test_failed ? -EIO : 0;
}

int hwdebug_flash_test_qspi_4line(void)
{
	backend_test_failed = false;
	test_quiet = true;
	backend_test_qspi_4line();
	test_quiet = false;
	return backend_test_failed ? -EIO : 0;
}

int hwdebug_flash_test_qpi(void)
{
	backend_test_failed = false;
	test_quiet = true;
	backend_test_qpi();
	test_quiet = false;
	return backend_test_failed ? -EIO : 0;
}

static void run_test(const char *name, int (*test)(void))
{
	int ret;

	hwdebug_test_begin(name);
	ret = test();
	if (ret != 0) {
		hwdebug_test_fail("protocol validation failed", ret);
	}
	hwdebug_test_pass();
}

int hwdebug_flash_watchdog_init(void)
{
	return debug_watchdog_init();
}

void hwdebug_flash_watchdog_feed(void)
{
	debug_watchdog_feed();
}

void hwdebug_flash_bitbang(const char *mode)
{
	cmd_qspi_bitbang(mode);
}

void hwdebug_flash_info(void)
{
	cmd_flash_info();
}

void hwdebug_flash_probe(void)
{
	cmd_flash_probe();
}

void hwdebug_flash_jedec(void)
{
	cmd_flash_jedec();
}

void hwdebug_flash_sfdp(uint32_t offset, uint32_t length)
{
	cmd_flash_sfdp(offset, length);
}

void hwdebug_flash_read(uint32_t offset, uint32_t length)
{
	cmd_flash_read(offset, length);
}

int hwdebug_flash_recover(bool verbose)
{
	return qspi_recover(verbose);
}

void hwdebug_flash_test_group(const char *name)
{
	if (name == NULL || name[0] == '\0' || strcmp(name, "all") == 0) {
		hwdebug_flash_test_all();
	} else if (strcmp(name, "spi") == 0) {
		run_test("flash/spi", hwdebug_flash_test_spi);
	} else if (strcmp(name, "qspi-1line") == 0) {
		run_test("flash/qspi-1line", hwdebug_flash_test_qspi_1line);
	} else if (strcmp(name, "qspi-4line") == 0) {
		run_test("flash/qspi-4line", hwdebug_flash_test_qspi_4line);
	} else if (strcmp(name, "qpi") == 0) {
		run_test("flash/qpi", hwdebug_flash_test_qpi);
	}
}

void hwdebug_flash_test_all(void)
{
	hwdebug_flash_test_group("spi");
	hwdebug_flash_test_group("qspi-1line");
	hwdebug_flash_test_group("qspi-4line");
	hwdebug_flash_test_group("qpi");
}

/* Console command parsing and the application loop live in main.c. */
#if 0

static void handle_command(char *line)
{
	char *command = strtok(line, " \t");
	char *subcommand;
	char *argument;
	char *extra;
	char *test_name;
	uint32_t offset;
	uint32_t length;

	if (command == NULL) {
		return;
	}
	if (strcmp(command, "help") == 0) {
		print_help();
		return;
	}
	if (strcmp(command, "hardware") == 0) {
		subcommand = strtok(NULL, " \t");
		argument = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 && argument != NULL &&
			strtok(NULL, " \t") == NULL) {
			if (strcmp(argument, "flash") == 0 || strcmp(argument, "all") == 0) {
				hwdebug_flash_test_group("all");
			}
			if (strcmp(argument, "mcp23017") == 0 || strcmp(argument, "all") == 0) {
				hwdebug_mcp23017_test();
			}
			return;
		}
		console_puts("error: usage: hardware test flash|mcp23017|all\r\n");
		return;
	}
	if (strcmp(command, "mcp23017") == 0) {
		subcommand = strtok(NULL, " \t");
		if (subcommand != NULL && strcmp(subcommand, "test") == 0 &&
			strtok(NULL, " \t") == NULL) {
			hwdebug_mcp23017_test();
			return;
		}
		console_puts("error: usage: mcp23017 test\r\n");
		return;
	}
	if (strcmp(command, "flash") != 0 && strcmp(command, "qspi") != 0) {
		console_puts("error: unknown command; use help\r\n");
		return;
	}
	subcommand = strtok(NULL, " \t");
	if (subcommand == NULL) {
		print_help();
		return;
	}
	if (strcmp(command, "qspi") == 0 && strcmp(subcommand, "bitbang") == 0) {
		argument = strtok(NULL, " \t");
		if (argument == NULL || strtok(NULL, " \t") != NULL) {
			console_puts("error: usage: qspi bitbang spi|qpi\r\n");
		} else {
			cmd_qspi_bitbang(argument);
		}
		return;
	}
	if (strcmp(subcommand, "info") == 0) {
		argument = strtok(NULL, " \t");
		if (argument == NULL && strtok(NULL, " \t") == NULL) {
			cmd_flash_info();
		} else {
			console_puts("error: usage: flash info\r\n");
		}
		return;
	}
	if ((strcmp(subcommand, "probe") == 0 ||
	     strcmp(subcommand, "recover") == 0 || strcmp(subcommand, "jedec") == 0) &&
		strtok(NULL, " \t") == NULL) {
		if (strcmp(subcommand, "probe") == 0) {
			cmd_flash_probe();
		} else if (strcmp(subcommand, "recover") == 0) {
			(void)qspi_recover(true);
		} else {
			cmd_flash_jedec();
		}
		return;
	}
	if (strcmp(subcommand, "test") == 0) {
		test_name = strtok(NULL, " \t");
		if (test_name == NULL || strtok(NULL, " \t") != NULL) {
			console_puts("error: usage: flash test spi|qspi-1line|qspi-4line|qspi|qpi|all\r\n");
			return;
		}
		if (strcmp(test_name, "spi") == 0) {
			hwdebug_flash_test_group("spi");
		} else if (strcmp(test_name, "qspi-1line") == 0) {
			hwdebug_flash_test_group("qspi-1line");
		} else if (strcmp(test_name, "qspi-4line") == 0) {
			hwdebug_flash_test_group("qspi-4line");
		} else if (strcmp(test_name, "qspi") == 0) {
			hwdebug_flash_test_group("qspi-1line");
			hwdebug_flash_test_group("qspi-4line");
		} else if (strcmp(test_name, "qpi") == 0) {
			hwdebug_flash_test_group("qpi");
		} else if (strcmp(test_name, "all") == 0) {
			hwdebug_flash_test_group("all");
		} else {
			console_puts("error: unknown flash test; use help\r\n");
		}
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
			Parsed below.
		} else {
			console_puts("error: usage: flash sfdp [offset] [length]\r\n");
			return;
		}
		cmd_flash_sfdp(offset, length);
		return;
	}
	if (strcmp(subcommand, "read") == 0) {
		argument = strtok(NULL, " \t");
		extra = strtok(NULL, " \t");
		if (argument == NULL || extra == NULL || strtok(NULL, " \t") != NULL ||
			parse_u32(argument, &offset) != 0 || parse_u32(extra, &length) != 0) {
			console_puts("error: usage: flash read <offset> <length>\r\n");
			return;
		}
		cmd_flash_read(offset, length);
		return;
	}
	console_puts("error: unknown flash command; use help\r\n");
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

int main(void)
{
	uint8_t received;
	int watchdog_result;

	watchdog_result = debug_watchdog_init();
	if (!device_is_ready(console_uart)) {
		return 0;
	}
	uart_irq_callback_set(console_uart, console_rx_callback);
	uart_irq_rx_enable(console_uart);
	console_puts("boot: main\r\n");
	console_puts("boot: UART4 ready\r\n");
	console_puts("boot: clock source HSE 25 MHz -> PLL -> SYSCLK 80 MHz\r\n");
	console_puts("EF800 hardware debug, Bank 1 read-only SPI/QSPI/QPI tests\r\n");
	console_puts("flash: IS25LP032D-JTLE-TR, expected 32 Mbit / 4 MiB\r\n");
	console_printf("watchdog: %s (%d)\r\n",
		       watchdog_result == 0 ? "feeding" : "unavailable", watchdog_result);
	print_help();
	console_puts("> ");

	while (true) {
		debug_watchdog_feed();
		if (k_msgq_get(&console_rx_queue, &received, K_MSEC(100)) != 0) {
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
}
#endif
