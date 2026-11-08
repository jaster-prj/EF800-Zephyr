/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_BOOT_TRACE_H
#define EF800_BOOT_TRACE_H

#include <zephyr/kernel.h>

#if !defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

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

static inline void ef800_boot_trace_result(const char *stage, int result)
{
	char line[96];

	snprintk(line, sizeof(line), "boot update: %s rc=%d\r\n", stage, result);
	ef800_boot_trace(line);
}
#else
static inline void ef800_boot_trace(const char *text)
{
	ARG_UNUSED(text);
}

static inline void ef800_boot_trace_result(const char *stage, int result)
{
	ARG_UNUSED(stage);
	ARG_UNUSED(result);
}
#endif

#endif /* EF800_BOOT_TRACE_H */
