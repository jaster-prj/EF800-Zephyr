/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate MCP23017 GPIO register behavior using Zephyr GPIO. */
#ifndef EF800_VIRTUAL_MCP23017_H
#define EF800_VIRTUAL_MCP23017_H

#include <stdint.h>

typedef void (*ef800_virtual_mcp23017_output_cb_t)(uint16_t value, void *context);

/** @brief Register a callback for emulated expander output changes.
 * @param callback Receives the new 16-bit output state, or NULL to clear.
 * @param context Opaque callback argument.
 * @return 0 on update; negative errno if callback registration fails.
 */
int ef800_virtual_mcp23017_output_callback_set(
	ef800_virtual_mcp23017_output_cb_t callback, void *context);

#endif
