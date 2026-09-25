/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_console.h"

#if defined(CONFIG_SHELL)

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define EF800_SHELL_RX_SIZE 128U

struct ef800_shell_context {
	shell_transport_handler_t handler;
	void *handler_context;
	struct k_spinlock lock;
	struct k_timer rx_timer;
	struct k_work_delayable rx_work;
	uint8_t rx_buffer[EF800_SHELL_RX_SIZE];
	size_t rx_start;
	size_t rx_length;
};

static struct ef800_shell_context shell_context;

static void shell_transport_notify_work(struct k_work *work)
{
	struct k_work_delayable *delayable = CONTAINER_OF(work,
		struct k_work_delayable, work);
	struct ef800_shell_context *context = CONTAINER_OF(delayable,
		struct ef800_shell_context, rx_work);

	if (context->rx_length != 0U && context->handler != NULL) {
		context->handler(SHELL_TRANSPORT_EVT_RX_RDY, context->handler_context);
	}
}

static void shell_transport_poll(struct ef800_shell_context *context)
{
	uint8_t value;
	k_spinlock_key_t key = k_spin_lock(&context->lock);

	while (context->rx_length < EF800_SHELL_RX_SIZE &&
		ef800_console_read(&value) == 0) {
		context->rx_buffer[(context->rx_start + context->rx_length) %
			EF800_SHELL_RX_SIZE] = value;
		context->rx_length++;
	}
	k_spin_unlock(&context->lock, key);

	if (context->rx_length != 0U) {
		(void)k_work_reschedule(&context->rx_work, K_MSEC(1U));
	} else if (context->handler != NULL) {
		context->handler(SHELL_TRANSPORT_EVT_RX_RDY, context->handler_context);
	}
}

static void shell_transport_timer_handler(struct k_timer *timer)
{
	struct ef800_shell_context *context = CONTAINER_OF(timer,
		struct ef800_shell_context, rx_timer);

	shell_transport_poll(context);
}

static int shell_transport_init(const struct shell_transport *transport,
				const void *config,
				shell_transport_handler_t handler,
				void *context)
{
	ARG_UNUSED(config);
	shell_context.handler = handler;
	shell_context.handler_context = context;
	shell_context.rx_start = 0U;
	shell_context.rx_length = 0U;
	k_timer_init(&shell_context.rx_timer, shell_transport_timer_handler, NULL);
	k_work_init_delayable(&shell_context.rx_work, shell_transport_notify_work);
	k_timer_start(&shell_context.rx_timer, K_MSEC(10U), K_MSEC(10U));
	ARG_UNUSED(transport);
	return 0;
}

static int shell_transport_uninit(const struct shell_transport *transport)
{
	ARG_UNUSED(transport);
	shell_context.handler = NULL;
	shell_context.handler_context = NULL;
	k_timer_stop(&shell_context.rx_timer);
	(void)k_work_cancel_delayable(&shell_context.rx_work);
	shell_context.rx_start = 0U;
	shell_context.rx_length = 0U;
	return 0;
}

static int shell_transport_enable(const struct shell_transport *transport,
				  bool blocking_tx)
{
	ARG_UNUSED(transport);
	ARG_UNUSED(blocking_tx);
	return 0;
}

static int shell_transport_write(const struct shell_transport *transport,
				 const void *data, size_t length, size_t *count)
{
	ARG_UNUSED(transport);
	if (data == NULL || count == NULL) {
		return -EINVAL;
	}
	(void)ef800_console_write(data, length);
	*count = length;
	return 0;
}

static int shell_transport_read(const struct shell_transport *transport,
				void *data, size_t length, size_t *count)
{
	struct ef800_shell_context *context = transport->ctx;
	k_spinlock_key_t key;

	if (data == NULL || count == NULL || context == NULL) {
		return -EINVAL;
	}
	key = k_spin_lock(&context->lock);
	*count = MIN(length, context->rx_length);
	for (size_t index = 0U; index < *count; ++index) {
		((uint8_t *)data)[index] = context->rx_buffer[
			(context->rx_start + index) % EF800_SHELL_RX_SIZE];
	}
	context->rx_start = (context->rx_start + *count) % EF800_SHELL_RX_SIZE;
	context->rx_length -= *count;
	k_spin_unlock(&context->lock, key);
	while (*count < length) {
		uint8_t value;
		int result = ef800_console_read(&value);

		if (result == 0) {
			((uint8_t *)data)[*count] = value;
			(*count)++;
			continue;
		}
		if (*count != 0U) {
			break;
		}
		/* UART hosts can deliver a command in several FIFO bursts. */
		for (uint8_t retry = 0U; retry < 20U; ++retry) {
			k_sleep(K_MSEC(1U));
			if (ef800_console_read(&value) == 0) {
				((uint8_t *)data)[*count] = value;
				(*count)++;
				break;
			}
		}
		break;
	}
	return 0;
}

static void shell_transport_update(const struct shell_transport *transport)
{
	shell_transport_poll(transport->ctx);
}

static const struct shell_transport_api shell_transport_api = {
	.init = shell_transport_init,
	.uninit = shell_transport_uninit,
	.enable = shell_transport_enable,
	.write = shell_transport_write,
	.read = shell_transport_read,
	.update = shell_transport_update,
};

static struct shell_transport shell_transport = {
	.api = &shell_transport_api,
	.ctx = &shell_context,
};

SHELL_DEFINE(ef800_shell, "ef800> ", &shell_transport,
		1024U, 100U,
		SHELL_FLAG_OLF_CRLF);

int ef800_shell_init(void)
{
	static const struct shell_backend_config_flags flags = {0};

	return shell_init(&ef800_shell, NULL, flags, false, 0U);
}

#else

int ef800_shell_init(void)
{
	return 0;
}

#endif
