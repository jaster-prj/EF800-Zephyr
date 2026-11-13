/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate MCP23017 GPIO register behavior using Zephyr GPIO. */
#ifndef VIRTUAL_EF800_MCP23017_H
#define VIRTUAL_EF800_MCP23017_H

#include <stdint.h>

typedef void (*virtual_ef800_mcp23017_output_cb_t)(uint16_t value, void *context);

/** @brief Register a callback for emulated expander output changes.
 * @param callback Receives the new 16-bit output state, or NULL to clear.
 * @param context Opaque callback argument.
 * @return 0 on update; negative errno if callback registration fails.
 */
int virtual_ef800_mcp23017_output_callback_set(
	virtual_ef800_mcp23017_output_cb_t callback, void *context);

#endif
