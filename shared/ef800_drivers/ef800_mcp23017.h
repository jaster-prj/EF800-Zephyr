/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide synchronized GPIO-expander operations for MCP23017. */
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

/** @brief Bind a ready Zephyr GPIO device and initialize access locking.
 * @param mcp Driver state to initialize.
 * @param dev GPIO device representing the expander.
 * @return 0 on open; negative errno when device is unavailable or invalid.
 */
int ef800_mcp23017_open(struct ef800_mcp23017 *mcp,
			const struct device *dev);

/* Probe the GPIO port without accessing MCP23017 registers directly. */

/** @brief Probe the GPIO port through Zephyr's GPIO API.
 * @param dev Expander GPIO device.
 * @param value Receives sampled port value.
 * @return 0 on read; negative errno on device or GPIO failure.
 */
int ef800_mcp23017_probe(const struct device *dev, uint16_t *value);

/* Configure selected pins as push-pull outputs and set their initial level. */

/** @brief Configure selected pins as outputs and apply their initial levels.
 * @param mcp Initialized expander state.
 * @param mask Pins to configure as outputs.
 * @param value Initial high/low values for masked pins.
 * @return 0 on configuration; negative errno on GPIO failure.
 */
int ef800_mcp23017_configure_outputs(struct ef800_mcp23017 *mcp,
					     uint16_t mask, uint16_t value);

/* Read or update the physical 16-bit GPIO port. */

/** @brief Read the current 16-bit GPIO port state.
 * @param mcp Initialized expander state.
 * @param value Receives pin levels.
 * @return 0 on read; negative errno on GPIO failure.
 */
int ef800_mcp23017_read(struct ef800_mcp23017 *mcp, uint16_t *value);

/** @brief Update selected output pins while preserving unmasked outputs.
 * @param mcp Initialized expander state.
 * @param mask Pins to update.
 * @param value New levels for masked pins.
 * @return 0 on write; negative errno on GPIO failure.
 */
int ef800_mcp23017_write(struct ef800_mcp23017 *mcp,
				 uint16_t mask, uint16_t value);

/** @brief Set one output pin level.
 * @param mcp Initialized expander state.
 * @param pin Pin number from 0 through 15.
 * @param value Desired level.
 * @return 0 on update; negative errno for invalid pin or GPIO failure.
 */
int ef800_mcp23017_set_pin(struct ef800_mcp23017 *mcp,
				   uint8_t pin, bool value);

#endif /* EF800_MCP23017_H */
