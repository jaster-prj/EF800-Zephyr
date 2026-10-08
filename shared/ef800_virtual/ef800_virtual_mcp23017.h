/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_MCP23017_H
#define EF800_VIRTUAL_MCP23017_H

#include <stdint.h>

typedef void (*ef800_virtual_mcp23017_output_cb_t)(uint16_t value, void *context);

int ef800_virtual_mcp23017_output_callback_set(
	ef800_virtual_mcp23017_output_cb_t callback, void *context);

#endif
