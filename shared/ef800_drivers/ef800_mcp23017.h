/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MCP23017_H
#define EF800_MCP23017_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

struct ef800_mcp23017 {
	const struct device *dev;
	uint16_t output_mask;
	struct k_mutex lock;
};

/* Open the standard Zephyr GPIO-expander device from devicetree. */
int ef800_mcp23017_open(struct ef800_mcp23017 *mcp,
			const struct device *dev);

/* Probe the GPIO port without accessing MCP23017 registers directly. */
int ef800_mcp23017_probe(const struct device *dev, uint16_t *value);

/* Configure selected pins as push-pull outputs and set their initial level. */
int ef800_mcp23017_configure_outputs(struct ef800_mcp23017 *mcp,
					     uint16_t mask, uint16_t value);

/* Read or update the physical 16-bit GPIO port. */
int ef800_mcp23017_read(struct ef800_mcp23017 *mcp, uint16_t *value);
int ef800_mcp23017_write(struct ef800_mcp23017 *mcp,
				 uint16_t mask, uint16_t value);
int ef800_mcp23017_set_pin(struct ef800_mcp23017 *mcp,
				   uint8_t pin, bool value);

#endif /* EF800_MCP23017_H */
