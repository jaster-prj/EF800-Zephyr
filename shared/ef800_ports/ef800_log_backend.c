/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>

#include "ef800_console.h"

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include "ef800_virtual_frontpanel.h"
#include "ef800_virtual_protocol.h"
#endif

LOG_MODULE_REGISTER(ef800_log_backend, LOG_LEVEL_NONE);

#define EF800_LOG_BUFFER_SIZE 128U

struct ef800_log_data {
	uint32_t log_format;
	bool in_panic;
#if defined(CONFIG_BOARD_NATIVE_SIM)
	uint8_t native_severity;
	uint8_t native_buffer[EF800_VIRTUAL_LOG_MAX_TEXT];
	size_t native_length;
#endif
};

static struct ef800_log_data log_data;
static uint8_t log_buffer[EF800_LOG_BUFFER_SIZE];

static int log_char_out(uint8_t *data, size_t length, void *context)
{
	struct ef800_log_data *log_data = context;
	(void)ef800_console_write(data, length);
#if defined(CONFIG_BOARD_NATIVE_SIM)
	if (log_data != NULL && log_data->native_length < sizeof(log_data->native_buffer)) {
		size_t available = sizeof(log_data->native_buffer) - log_data->native_length;
		size_t copy_length = MIN(length, available);
		memcpy(&log_data->native_buffer[log_data->native_length], data, copy_length);
		log_data->native_length += copy_length;
	}
#else
	ARG_UNUSED(log_data);
#endif
	return (int)length;
}

LOG_OUTPUT_DEFINE(ef800_log_output, log_char_out, log_buffer,
			  sizeof(log_buffer));

static void log_process(const struct log_backend *const backend,
			union log_msg_generic *msg)
{
	struct ef800_log_data *data = (struct ef800_log_data *)backend->cb->ctx;
	log_format_func_t format = log_format_func_t_get(data->log_format);

	if (format != NULL) {
#if defined(CONFIG_BOARD_NATIVE_SIM)
		uint32_t level = log_msg_get_level(&msg->log);

		data->native_severity = level >= LOG_LEVEL_DBG ?
			EF800_VIRTUAL_LOG_DEBUG : level == LOG_LEVEL_INF ?
			EF800_VIRTUAL_LOG_INFO : level == LOG_LEVEL_WRN ?
			EF800_VIRTUAL_LOG_WARNING : level == LOG_LEVEL_ERR ?
			EF800_VIRTUAL_LOG_ERROR : EF800_VIRTUAL_LOG_PANIC;
		data->native_length = 0U;
#endif
		format(&ef800_log_output, &msg->log,
		       log_backend_std_get_flags());
#if defined(CONFIG_BOARD_NATIVE_SIM)
		while (data->native_length > 0U &&
			(data->native_buffer[data->native_length - 1U] == '\n' ||
			 data->native_buffer[data->native_length - 1U] == '\r')) {
			data->native_length--;
		}
		if (data->native_length != 0U) {
			ef800_virtual_frontpanel_log_record(data->native_severity,
				data->native_buffer, data->native_length);
		}
#endif
	}
}

static void log_init_backend(const struct log_backend *const backend)
{
	struct ef800_log_data *data = backend->cb->ctx;

	data->log_format = LOG_OUTPUT_TEXT;
	data->in_panic = false;
	log_output_ctx_set(&ef800_log_output, data);
}

static void log_panic(const struct log_backend *const backend)
{
	struct ef800_log_data *data = backend->cb->ctx;

	data->in_panic = true;
	log_output_flush(&ef800_log_output);
}

static void log_dropped(const struct log_backend *const backend, uint32_t count)
{
	ARG_UNUSED(backend);
	#if defined(CONFIG_LOG_OUTPUT) && CONFIG_LOG_OUTPUT
	log_output_dropped_process(&ef800_log_output, count);
	#else
	ARG_UNUSED(count);
	#endif
}

static int log_format_set(const struct log_backend *const backend,
				  uint32_t format)
{
	struct ef800_log_data *data = backend->cb->ctx;

	data->log_format = format;
	return 0;
}

static const struct log_backend_api log_api = {
	.process = log_process,
	.dropped = log_dropped,
	.panic = log_panic,
	.init = log_init_backend,
	.format_set = log_format_set,
};

LOG_BACKEND_DEFINE(ef800_log_backend, log_api, true, &log_data);
