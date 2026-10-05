/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_session.h"

#include <errno.h>

void ef800_plugin_session_init(struct ef800_plugin_session *session)
{
	if (session != NULL) {
		*session = (struct ef800_plugin_session){0};
	}
}

int ef800_plugin_session_open(struct ef800_plugin_session *session,
	void *instance, const struct ef800_plugin_ui_api *api)
{
	if (session == NULL || instance == NULL || api == NULL ||
		api->abi_version == 0U || api->struct_size < sizeof(*api) ||
		api->render == NULL || api->handle_key == NULL) {
		return -EINVAL;
	}
	session->instance = instance;
	session->api = api;
	session->active = true;
	session->text_input = false;
	return 0;
}

int ef800_plugin_session_render(const struct ef800_plugin_session *session,
	struct ef800_plugin_screen *screen)
{
	if (session == NULL || !session->active || session->api == NULL ||
		screen == NULL) {
		return -EINVAL;
	}
	return session->api->render(session->instance, screen);
}

int ef800_plugin_session_key(struct ef800_plugin_session *session,
	const struct ef800_plugin_key_event *event,
	enum ef800_plugin_key_result *result)
{
	if (session == NULL || !session->active || session->api == NULL ||
		event == NULL || result == NULL) {
		return -EINVAL;
	}
	if (event->code == EF800_PLUGIN_KEY_BACK) {
		*result = EF800_PLUGIN_KEY_REQUEST_BACK;
		return 0;
	}
	return session->api->handle_key(session->instance, event, result);
}

void ef800_plugin_session_close(struct ef800_plugin_session *session)
{
	if (session != NULL) {
		*session = (struct ef800_plugin_session){0};
	}
}
