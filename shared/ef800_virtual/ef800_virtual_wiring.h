/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Route virtual front-panel and peripheral signals between devices. */
#ifndef EF800_VIRTUAL_WIRING_H
#define EF800_VIRTUAL_WIRING_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Read the current virtual MCP23017 output state.
 * @return 16-bit expander output value.
 */
uint16_t ef800_virtual_wiring_mcp_state(void);

/** @brief Check whether the virtual keyboard reset line is released.
 * @return True when reset is inactive.
 */
bool ef800_virtual_wiring_keyboard_reset_released(void);

/** @brief Check whether the virtual LCD reset line is released.
 * @return True when reset is inactive.
 */
bool ef800_virtual_wiring_lcd_reset_released(void);

/** @brief Read the virtual LCD font-select signal.
 * @return True when the signal is active.
 */
bool ef800_virtual_wiring_lcd_fs_active(void);

/** @brief Check whether USB is routed to the virtual device.
 * @return True when selected.
 */
bool ef800_virtual_wiring_usb_selected(void);

#endif
