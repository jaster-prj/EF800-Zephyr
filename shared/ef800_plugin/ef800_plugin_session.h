/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Manage UI plugin instance activation, rendering, and key input. */
#ifndef EF800_PLUGIN_SESSION_H
#define EF800_PLUGIN_SESSION_H

#include <stdbool.h>

#include "ef800_plugin_contract.h"
#include "ef800_plugin_screen.h"

struct ef800_plugin_session {
	void *instance;
	const struct ef800_plugin_ui_api *api;
	bool active;
	bool text_input;
};

/** @brief Clear session state before opening a plugin.
 * @param session Session state to initialize.
 * @return None.
 */
void ef800_plugin_session_init(struct ef800_plugin_session *session);

/** @brief Bind a plugin instance and validate its UI API before activation.
 * @param session Session state.
 * @param instance Plugin instance pointer.
 * @param api Versioned plugin UI callback table.
 * @return 0 on open; negative errno for invalid/incompatible API.
 */
int ef800_plugin_session_open(struct ef800_plugin_session *session,
	void *instance, const struct ef800_plugin_ui_api *api);

/** @brief Request and validate the plugin's current screen.
 * @param session Active session.
 * @param screen Receives bounded screen model.
 * @return 0 on render; negative errno if inactive, callback fails, or model invalid.
 */
int ef800_plugin_session_render(const struct ef800_plugin_session *session,
	struct ef800_plugin_screen *screen);

/** @brief Forward a key event and receive its consumption/back result.
 * @param session Active session.
 * @param event Key event to deliver.
 * @param result Receives plugin key-handling result.
 * @return 0 on dispatch; negative errno on invalid session or callback failure.
 */
int ef800_plugin_session_key(struct ef800_plugin_session *session,
	const struct ef800_plugin_key_event *event,
	enum ef800_plugin_key_result *result);

/** @brief Clear plugin instance bindings and close the session.
 * @param session Session to close.
 * @return None.
 */
void ef800_plugin_session_close(struct ef800_plugin_session *session);

#endif
