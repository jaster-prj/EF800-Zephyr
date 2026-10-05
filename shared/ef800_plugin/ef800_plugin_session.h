/* SPDX-License-Identifier: Apache-2.0 */
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

void ef800_plugin_session_init(struct ef800_plugin_session *session);
int ef800_plugin_session_open(struct ef800_plugin_session *session,
	void *instance, const struct ef800_plugin_ui_api *api);
int ef800_plugin_session_render(const struct ef800_plugin_session *session,
	struct ef800_plugin_screen *screen);
int ef800_plugin_session_key(struct ef800_plugin_session *session,
	const struct ef800_plugin_key_event *event,
	enum ef800_plugin_key_result *result);
void ef800_plugin_session_close(struct ef800_plugin_session *session);

#endif
