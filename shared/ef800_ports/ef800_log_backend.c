/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>

#include "ef800_console.h"

LOG_MODULE_REGISTER(ef800_log_backend, LOG_LEVEL_NONE);

#define EF800_LOG_BUFFER_SIZE 128U

struct ef800_log_data {
	uint32_t log_format;
	bool in_panic;
};

static struct ef800_log_data log_data;
static uint8_t log_buffer[EF800_LOG_BUFFER_SIZE];

static int log_char_out(uint8_t *data, size_t length, void *context)
{
	ARG_UNUSED(context);
	(void)ef800_console_write(data, length);
	return (int)length;
}

LOG_OUTPUT_DEFINE(ef800_log_output, log_char_out, log_buffer,
			  sizeof(log_buffer));

static void log_process(const struct log_backend *const backend,
			union log_msg_generic *msg)
{
	const struct ef800_log_data *data = backend->cb->ctx;
	log_format_func_t format = log_format_func_t_get(data->log_format);

	if (format != NULL) {
		format(&ef800_log_output, &msg->log,
		       log_backend_std_get_flags());
	}
}

static void log_init_backend(const struct log_backend *const backend)
{
	struct ef800_log_data *data = backend->cb->ctx;

	data->log_format = LOG_OUTPUT_TEXT;
	data->in_panic = false;
	log_output_ctx_set(&ef800_log_output, data);
	(void)ef800_console_init(false);
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
