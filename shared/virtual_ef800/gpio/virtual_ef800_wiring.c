/* SPDX-License-Identifier: Apache-2.0 */

#include "virtual_ef800_wiring.h"

#include "virtual_ef800_gpio_emul.h"
#include "virtual_ef800_mcp23017.h"
#include "virtual_ef800_keyboard_controller.h"

#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#define MCP_USB_SELECT BIT(8)
#define MCP_LCD_FS BIT(4)
#define MCP_LCD_RESET BIT(5)
#define MCP_KEYBOARD_RESET BIT(12)

static uint16_t mcp_state;

static void mcp_output_changed(uint16_t value, void *context)
{
	ARG_UNUSED(context);
	mcp_state = value;
	virtual_ef800_keyboard_controller_reset_released(
		(value & MCP_KEYBOARD_RESET) != 0U);
}

uint16_t virtual_ef800_wiring_mcp_state(void)
{
	return mcp_state;
}

bool virtual_ef800_wiring_keyboard_reset_released(void)
{
	return (mcp_state & MCP_KEYBOARD_RESET) != 0U;
}

bool virtual_ef800_wiring_lcd_reset_released(void)
{
	return (mcp_state & MCP_LCD_RESET) != 0U;
}

bool virtual_ef800_wiring_lcd_fs_active(void)
{
	return (mcp_state & MCP_LCD_FS) != 0U;
}

bool virtual_ef800_wiring_usb_selected(void)
{
	return (mcp_state & MCP_USB_SELECT) != 0U;
}

static int wiring_init(void)
{
	return virtual_ef800_mcp23017_output_callback_set(mcp_output_changed, NULL);
}

SYS_INIT(wiring_init, APPLICATION, 0);
