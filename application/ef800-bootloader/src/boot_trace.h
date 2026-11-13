/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emit early boot diagnostics over the bootloader UART. */
#ifndef EF800_BOOT_TRACE_H
#define EF800_BOOT_TRACE_H

#include <zephyr/kernel.h>

#if !defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

/** @brief Write diagnostic text to UART4; a no-op on native_sim.
 * @param text NUL-terminated message.
 * @return None.
 */
static inline void ef800_boot_trace(const char *text)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart4));

	if (text == NULL || !device_is_ready(uart)) {
		return;
	}
	while (*text != '\0') {
		uart_poll_out(uart, (uint8_t)*text++);
	}
}

/** @brief Format and emit a boot-update stage result.
 * @param stage Short stage name.
 * @param result Stage result/status code.
 * @return None.
 */
static inline void ef800_boot_trace_result(const char *stage, int result)
{
	char line[96];

	snprintk(line, sizeof(line), "boot update: %s rc=%d\r\n", stage, result);
	ef800_boot_trace(line);
}
#else

/** @brief Native-simulation no-op implementation of boot tracing.
 * @param text Ignored diagnostic text.
 * @return None.
 */
static inline void ef800_boot_trace(const char *text)
{
	ARG_UNUSED(text);
}

/** @brief Native-simulation no-op implementation of result tracing.
 * @param stage Ignored stage name.
 * @param result Ignored result code.
 * @return None.
 */
static inline void ef800_boot_trace_result(const char *stage, int result)
{
	ARG_UNUSED(stage);
	ARG_UNUSED(result);
}
#endif

#endif /* EF800_BOOT_TRACE_H */
