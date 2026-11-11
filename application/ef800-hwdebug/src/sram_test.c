/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stm32l4xx_hal.h>

#include "flash.h"
#include "sram_test.h"

#define EF800_EXT_SRAM_BASE 0x60000000U
#define SRAM_SIZE 0x00080000U
#define EF800_EXT_SRAM_LAST (EF800_EXT_SRAM_BASE + SRAM_SIZE - 1U)

static volatile uint8_t *const sram = (volatile uint8_t *)EF800_EXT_SRAM_BASE;
static bool phase_failure_reported;

static int check_byte(uint32_t offset, uint8_t expected, const char *phase)
{
	uint8_t actual = sram[offset];

	if (actual != expected) {
		if (!phase_failure_reported) {
			hwdebug_console_printf("sram FAIL phase=%s address=0x%08x expected=0x%02x actual=0x%02x\r\n",
				phase, EF800_EXT_SRAM_BASE + offset, expected, actual);
			phase_failure_reported = true;
		}
		return -1;
	}
	return 0;
}

static int test_boundaries(void)
{
	static const uint32_t offsets[] = {
		0U, 1U, 2U, 3U, SRAM_SIZE / 2U - 1U,
		SRAM_SIZE / 2U, SRAM_SIZE - 4U, SRAM_SIZE - 3U,
		SRAM_SIZE - 2U, SRAM_SIZE - 1U
	};
	static const uint8_t values[] = {
		0x12U, 0x34U, 0x56U, 0x78U, 0x9aU,
		0xbcU, 0xdeU, 0xf0U, 0x5aU, 0xa5U
	};

	int failed = 0;

	phase_failure_reported = false;
	for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
		sram[offsets[i]] = values[i];
	}
	for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
		failed |= check_byte(offsets[i], values[i], "boundary") != 0;
	}
	return failed;
}

static int test_data_patterns(void)
{
	static const uint8_t patterns[] = { 0x00U, 0xffU, 0x55U, 0xaaU };
	int failed = 0;

	for (size_t p = 0; p < ARRAY_SIZE(patterns); ++p) {
		phase_failure_reported = false;
		for (uint32_t i = 0; i < SRAM_SIZE; ++i) {
			sram[i] = patterns[p];
			if ((i & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
		for (uint32_t i = 0; i < SRAM_SIZE; ++i) {
			failed |= check_byte(i, patterns[p], "data-pattern") != 0;
			if ((i & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
	}
	return failed;
}

static int test_walking_data(void)
{
	int failed = 0;

	for (uint32_t bit = 0U; bit < 8U; ++bit) {
		uint8_t pattern = (uint8_t)(1U << bit);
		uint8_t patterns[] = { pattern, (uint8_t)~pattern };

		for (size_t i = 0; i < ARRAY_SIZE(patterns); ++i) {
			phase_failure_reported = false;
			for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
				sram[offset] = patterns[i];
				if ((offset & 0x0fffU) == 0U) {
					hwdebug_flash_watchdog_feed();
				}
			}
			for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
				failed |= check_byte(offset, patterns[i], "walking-data") != 0;
				if ((offset & 0x0fffU) == 0U) {
					hwdebug_flash_watchdog_feed();
				}
			}
		}
	}
	return failed;
}

static uint32_t test_prng_next(uint32_t value)
{
	/* xorshift32: deterministic, address-independent pseudorandom data. */
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	return value;
}

static int test_full_range_patterns(void)
{
	static const uint8_t patterns[] = { 0x00U, 0xffU, 0x55U, 0xaaU };
	int failed = 0;

	/* Complementary checkerboards expose stuck and coupled data/address lines. */
	for (size_t p = 0; p < ARRAY_SIZE(patterns); ++p) {
		phase_failure_reported = false;
		for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
			sram[offset] = (offset & 1U) != 0U ? (uint8_t)~patterns[p] : patterns[p];
			if ((offset & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
		for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
			uint8_t expected = (offset & 1U) != 0U ?
				(uint8_t)~patterns[p] : patterns[p];

			failed |= check_byte(offset, expected, "checkerboard") != 0;
			if ((offset & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
	}

	/* Repeat with distinct deterministic pseudorandom streams to catch patterns
	 * that fixed walking/checkerboard tests may not sensitize. */
	for (uint32_t pass = 0U; pass < 4U; ++pass) {
		uint32_t state = 0x9e3779b9U ^ (pass * 0x85ebca6bU);

		phase_failure_reported = false;
		for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
			state = test_prng_next(state);
			sram[offset] = (uint8_t)(state >> 24);
			if ((offset & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
		state = 0x9e3779b9U ^ (pass * 0x85ebca6bU);
		for (uint32_t offset = 0U; offset < SRAM_SIZE; ++offset) {
			state = test_prng_next(state);
			failed |= check_byte(offset, (uint8_t)(state >> 24), "pseudorandom") != 0;
			if ((offset & 0x0fffU) == 0U) {
				hwdebug_flash_watchdog_feed();
			}
		}
	}
	return failed;
}

static int test_address_pattern(void)
{
	int failed = 0;

	phase_failure_reported = false;
	for (uint32_t i = 0; i < SRAM_SIZE; ++i) {
		sram[i] = (uint8_t)(i ^ (i >> 8) ^ (i >> 16) ^ 0x69U);
		if ((i & 0x0fffU) == 0U) {
			hwdebug_flash_watchdog_feed();
		}
	}
	for (uint32_t i = 0; i < SRAM_SIZE; ++i) {
		uint8_t expected = (uint8_t)(i ^ (i >> 8) ^ (i >> 16) ^ 0x69U);

		failed |= check_byte(i, expected, "address-pattern") != 0;
		if ((i & 0x0fffU) == 0U) {
			hwdebug_flash_watchdog_feed();
		}
	}
	return failed;
}

static int test_address_aliases(void)
{
	uint32_t offsets[20];
	uint8_t tags[20];
	size_t count = 0U;
	int failed = 0;

	for (uint32_t offset = 1U; offset < SRAM_SIZE; offset <<= 1) {
		offsets[count] = offset;
		tags[count] = (uint8_t)(count + 1U);
		++count;
	}
	offsets[count] = 0U;
	tags[count] = 0xa5U;
	++count;

	for (size_t i = 0; i < count; ++i) {
		sram[offsets[i]] = tags[i];
	}
	phase_failure_reported = false;
	for (size_t i = 0; i < count; ++i) {
		uint8_t actual = sram[offsets[i]];

		if (actual != tags[i]) {
			hwdebug_console_printf("sram FAIL phase=address-alias address=0x%08x expected=0x%02x actual=0x%02x\r\n",
				EF800_EXT_SRAM_BASE + offsets[i], tags[i], actual);
			failed = 1;
		}
	}
	return failed;
}

void hwdebug_sram_test(void)
{
	const struct device *fmc = DEVICE_DT_GET(DT_NODELABEL(fmc));
	const struct device *sram_device = DEVICE_DT_GET(DT_NODELABEL(fmc_sram));
	int failed = 0;

	if (!DT_NODE_HAS_STATUS(DT_NODELABEL(sram_bank1), okay)) {
		hwdebug_console_puts("sram FAIL: FMC Bank 1 is not enabled in devicetree\r\n");
		return;
	}
	if (!device_is_ready(fmc) || !device_is_ready(sram_device)) {
		hwdebug_console_printf("sram FAIL: FMC init state fmc=%d sram=%d\r\n",
			device_is_ready(fmc), device_is_ready(sram_device));
		return;
	}

	hwdebug_console_printf("sram: FMC Bank 1 window=0x%08x..0x%08x bytes=%u\r\n",
		EF800_EXT_SRAM_BASE, EF800_EXT_SRAM_LAST, SRAM_SIZE);
	hwdebug_console_printf("sram: PF15 mode=%u AF=%u speed=%u (FMC_A9); PG1 mode=%u AF=%u speed=%u (FMC_A11)\r\n",
		(unsigned int)((GPIOF->MODER >> 30) & 0x3U),
		(unsigned int)((GPIOF->AFR[1] >> 28) & 0xfU),
		(unsigned int)((GPIOF->OSPEEDR >> 30) & 0x3U),
		(unsigned int)((GPIOG->MODER >> 2) & 0x3U),
		(unsigned int)((GPIOG->AFR[0] >> 4) & 0xfU),
		(unsigned int)((GPIOG->OSPEEDR >> 2) & 0x3U));
	/* Meet the SRAM's specified minimum power-up initialization interval. */
	k_sleep(K_USEC(150));
	failed |= test_boundaries() != 0;
	failed |= test_data_patterns() != 0;
	failed |= test_walking_data() != 0;
	failed |= test_full_range_patterns() != 0;
	failed |= test_address_pattern() != 0;
	failed |= test_address_aliases() != 0;
	if (failed) {
		hwdebug_console_puts("sram FAIL: region NOT declared usable\r\n");
		return;
	}
	hwdebug_console_puts("sram PASS: full 512 KiB boundary/data/walking/checkerboard/pseudorandom/address/alias checks; production qualification still required\r\n");
}
