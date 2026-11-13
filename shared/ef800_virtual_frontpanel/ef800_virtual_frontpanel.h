/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate the EF800 keyboard/display front panel for host control. */
#ifndef EF800_VIRTUAL_FRONTPANEL_H
#define EF800_VIRTUAL_FRONTPANEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ef800_virtual_display.h"
#include "ef800_virtual_keyboard.h"

struct ef800_virtual_frontpanel {
	struct ef800_virtual_display *display;
	struct ef800_virtual_keyboard *keyboard;
	uint16_t port;
	bool running;
};

typedef int (*ef800_virtual_plugin_assign_fn)(void *context, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);
typedef int (*ef800_virtual_plugin_execute_fn)(void *context, uint8_t port_id);
typedef int (*ef800_virtual_composition_set_fn)(void *context,
						const char *group,
						const char *token);

/** @brief Start front-panel protocol service and bind display/keyboard state.
 * @param panel Panel state to initialize.
 * @param display Virtual LCD frame state.
 * @param keyboard Virtual keyboard state.
 * @param port Host transport port.
 * @return 0 on start; negative errno when transport/setup fails.
 */
int ef800_virtual_frontpanel_start(struct ef800_virtual_frontpanel *panel,
					   struct ef800_virtual_display *display,
					   struct ef800_virtual_keyboard *keyboard,
					   uint16_t port);

/** @brief Stop the host service and release all simulated keys.
 * @param panel Panel state to stop.
 * @return None.
 */
void ef800_virtual_frontpanel_stop(struct ef800_virtual_frontpanel *panel);

/** @brief Install plugin-assignment request callback.
 * @param callback Assignment callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void ef800_virtual_frontpanel_set_plugin_assign_callback(
	ef800_virtual_plugin_assign_fn callback, void *context);

/** @brief Install plugin-execution request callback.
 * @param callback Execution callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void ef800_virtual_frontpanel_set_plugin_execute_callback(
	ef800_virtual_plugin_execute_fn callback, void *context);

/** @brief Install composition group-selection callback.
 * @param callback Selection callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void ef800_virtual_frontpanel_set_composition_callback(
	ef800_virtual_composition_set_fn callback, void *context);

/** @brief Publish the current composition state to connected host clients.
 * @param json Serialized composition state.
 * @param length Byte count.
 * @return None; malformed/oversized state is ignored.
 */
void ef800_virtual_frontpanel_set_composition_state(const char *json,
						    size_t length);

/** @brief Publish LCD contrast state to connected host clients.
 * @param enabled True when adjustable contrast is available.
 * @param duty_percent Current duty setting.
 * @return None.
 */
void ef800_virtual_frontpanel_set_lcd_contrast(bool enabled,
						uint8_t duty_percent);

/** @brief Queue a structured log record for host delivery.
 * @param severity Log severity.
 * @param text Message bytes.
 * @param length Message size.
 * @return None.
 */
void ef800_virtual_frontpanel_log_record(uint8_t severity,
						 const uint8_t *text, size_t length);

#endif
