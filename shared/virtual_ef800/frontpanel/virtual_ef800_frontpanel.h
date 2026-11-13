/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Emulate the EF800 keyboard/display front panel for host control. */
#ifndef VIRTUAL_EF800_FRONTPANEL_H
#define VIRTUAL_EF800_FRONTPANEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "virtual_ef800_display.h"
#include "virtual_ef800_keyboard.h"

struct virtual_ef800_frontpanel {
	struct virtual_ef800_display *display;
	struct virtual_ef800_keyboard *keyboard;
	uint16_t port;
	bool running;
};

typedef int (*virtual_ef800_plugin_assign_fn)(void *context, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);
typedef int (*virtual_ef800_plugin_execute_fn)(void *context, uint8_t port_id);
typedef int (*virtual_ef800_composition_set_fn)(void *context,
						const char *group,
						const char *token);

/** @brief Start front-panel protocol service and bind display/keyboard state.
 * @param panel Panel state to initialize.
 * @param display Virtual LCD frame state.
 * @param keyboard Virtual keyboard state.
 * @param port Host transport port.
 * @return 0 on start; negative errno when transport/setup fails.
 */
int virtual_ef800_frontpanel_start(struct virtual_ef800_frontpanel *panel,
					   struct virtual_ef800_display *display,
					   struct virtual_ef800_keyboard *keyboard,
					   uint16_t port);

/** @brief Stop the host service and release all simulated keys.
 * @param panel Panel state to stop.
 * @return None.
 */
void virtual_ef800_frontpanel_stop(struct virtual_ef800_frontpanel *panel);

/** @brief Install plugin-assignment request callback.
 * @param callback Assignment callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void virtual_ef800_frontpanel_set_plugin_assign_callback(
	virtual_ef800_plugin_assign_fn callback, void *context);

/** @brief Install plugin-execution request callback.
 * @param callback Execution callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void virtual_ef800_frontpanel_set_plugin_execute_callback(
	virtual_ef800_plugin_execute_fn callback, void *context);

/** @brief Install composition group-selection callback.
 * @param callback Selection callback, or NULL to clear it.
 * @param context Opaque callback argument.
 * @return None.
 */
void virtual_ef800_frontpanel_set_composition_callback(
	virtual_ef800_composition_set_fn callback, void *context);

/** @brief Publish the current composition state to connected host clients.
 * @param json Serialized composition state.
 * @param length Byte count.
 * @return None; malformed/oversized state is ignored.
 */
void virtual_ef800_frontpanel_set_composition_state(const char *json,
						    size_t length);

/** @brief Publish LCD contrast state to connected host clients.
 * @param enabled True when adjustable contrast is available.
 * @param duty_percent Current duty setting.
 * @return None.
 */
void virtual_ef800_frontpanel_set_lcd_contrast(bool enabled,
						uint8_t duty_percent);

/** @brief Queue a structured log record for host delivery.
 * @param severity Log severity.
 * @param text Message bytes.
 * @param length Message size.
 * @return None.
 */
void virtual_ef800_frontpanel_log_record(uint8_t severity,
						 const uint8_t *text, size_t length);

#endif
