/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_syscall.h"

#include <errno.h>
#include <string.h>

#include <zephyr/internal/syscall_handler.h>
#include <zephyr/logging/log.h>

#include "ef800_plugin_host.h"

LOG_MODULE_REGISTER(ef800_plugin_syscall, LOG_LEVEL_INF);

#define EF800_PLUGIN_GATEWAY_STAGING_SIZE \
	EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT

struct gateway_session {
	bool active;
	struct k_thread *worker;
	struct ef800_plugin_worker_mailbox *mailbox;
	uint32_t generation;
	uint32_t capability;
	uint32_t allowed_services;
	uint32_t last_sequence;
	uint64_t deadline_ms;
	const struct ef800_measurement_transport *transport;
	const struct ef800_workspace *workspace;
};

static struct gateway_session active_session;
#if (defined(CONFIG_USERSPACE) && CONFIG_USERSPACE) || \
	(defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM)
static uint8_t input_staging[EF800_PLUGIN_GATEWAY_STAGING_SIZE];
static uint8_t output_staging[EF800_PLUGIN_GATEWAY_STAGING_SIZE];
#endif
K_MUTEX_DEFINE(gateway_mutex);

#if (defined(CONFIG_USERSPACE) && CONFIG_USERSPACE) || \
	(defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM)
static uint32_t required_service(uint32_t operation)
{
	switch (operation) {
	case EF800_PLUGIN_WORKER_PORT_WRITE:
	case EF800_PLUGIN_WORKER_PORT_READ:
		return EF800_PLUGIN_WORKER_SERVICE_PORT;
	case EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT:
		return EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT;
	case EF800_PLUGIN_WORKER_CLOCK_UPTIME:
		return EF800_PLUGIN_WORKER_SERVICE_CLOCK;
	case EF800_PLUGIN_WORKER_LOG:
		return EF800_PLUGIN_WORKER_SERVICE_LOG;
	default:
		return 0U;
	}
}
#endif

int ef800_plugin_gateway_session_bind(struct k_thread *worker,
	struct ef800_plugin_worker_mailbox *mailbox, uint32_t generation,
	uint32_t capability, uint32_t allowed_services, uint64_t deadline_ms,
	const struct ef800_measurement_transport *transport,
	const struct ef800_workspace *workspace)
{
	const uint32_t known_services =
		EF800_PLUGIN_WORKER_SERVICE_PORT |
		EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT |
		EF800_PLUGIN_WORKER_SERVICE_CLOCK |
		EF800_PLUGIN_WORKER_SERVICE_LOG;
	uint64_t now_ms = (uint64_t)k_uptime_get();

	if (worker == NULL || mailbox == NULL || generation == 0U ||
		capability == 0U ||
		(allowed_services & ~known_services) != 0U || deadline_ms <= now_ms ||
		deadline_ms - now_ms > EF800_PLUGIN_WORKER_MAX_DEADLINE_MS ||
		((allowed_services & EF800_PLUGIN_WORKER_SERVICE_PORT) != 0U &&
		 (transport == NULL || transport->write_until == NULL ||
		  transport->read_until == NULL)) ||
		((allowed_services & EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT) != 0U &&
		 workspace == NULL) ||
		((uintptr_t)mailbox % EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE) != 0U) {
		return -EINVAL;
	}
	if (k_mutex_lock(&gateway_mutex, K_NO_WAIT) != 0) {
		return -EBUSY;
	}
	if (active_session.active) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -EBUSY;
	}
	active_session = (struct gateway_session){
		.active = true,
		.worker = worker,
		.mailbox = mailbox,
		.generation = generation,
		.capability = capability,
		.allowed_services = allowed_services,
		.deadline_ms = deadline_ms,
		.transport = transport,
		.workspace = workspace,
	};
#if (defined(CONFIG_USERSPACE) && CONFIG_USERSPACE) || \
	(defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM)
	memset(input_staging, 0, sizeof(input_staging));
	memset(output_staging, 0, sizeof(output_staging));
#endif
	(void)k_mutex_unlock(&gateway_mutex);
	return 0;
}

int ef800_plugin_gateway_session_unbind(uint32_t generation)
{
	int ret = k_mutex_lock(&gateway_mutex, K_MSEC(100));

	if (ret != 0) {
		return ret;
	}
	if (!active_session.active || active_session.generation != generation) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -ESTALE;
	}
	active_session = (struct gateway_session){0};
#if (defined(CONFIG_USERSPACE) && CONFIG_USERSPACE) || \
	(defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM)
	memset(input_staging, 0, sizeof(input_staging));
	memset(output_staging, 0, sizeof(output_staging));
#endif
	(void)k_mutex_unlock(&gateway_mutex);
	return 0;
}

int ef800_plugin_gateway_session_set_deadline(uint32_t generation,
	uint64_t deadline_ms)
{
	uint64_t now_ms = (uint64_t)k_uptime_get();
	int ret = k_mutex_lock(&gateway_mutex, K_FOREVER);

	if (ret != 0) {
		return ret;
	}
	if (!active_session.active || active_session.generation != generation) {
		ret = -ESTALE;
	} else if (deadline_ms <= now_ms ||
		deadline_ms - now_ms > EF800_PLUGIN_WORKER_MAX_DEADLINE_MS) {
		ret = -EINVAL;
	} else {
		active_session.deadline_ms = deadline_ms;
		ret = 0;
	}
	(void)k_mutex_unlock(&gateway_mutex);
	return ret;
}

#if (defined(CONFIG_USERSPACE) && CONFIG_USERSPACE) || \
	(defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM)
static int copy_user_range(struct ef800_plugin_worker_mailbox *mailbox,
	uint32_t offset, uint8_t *destination, uint32_t length, bool to_user)
{
	uint8_t *range = &mailbox->payload[offset];

	if (length == 0U) {
		return 0;
	}
#if defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM
	if (to_user) {
		memcpy(range, destination, length);
	} else {
		memcpy(destination, range, length);
	}
	return 0;
#else
	if (to_user) {
		if (K_SYSCALL_MEMORY_WRITE(range, length)) {
			return -EFAULT;
		}
		return k_usermode_to_copy(range, destination, length) == 0 ? 0 : -EFAULT;
	}
	if (K_SYSCALL_MEMORY_READ(range, length)) {
		return -EFAULT;
	}
	return k_usermode_from_copy(destination, range, length) == 0 ? 0 : -EFAULT;
#endif
}

static int dispatch_service(struct gateway_session *session,
	struct ef800_plugin_worker_mailbox *mailbox,
	struct ef800_plugin_worker_message *message)
{
	uint32_t service = required_service(message->operation);
	int32_t status = -ENOTSUP;
	uint32_t response_length = 0U;
	uint64_t now_ms;
	int ret;

	if (service == 0U || (session->allowed_services & service) == 0U ||
		message->capability != session->capability) {
		return -EPERM;
	}
	now_ms = (uint64_t)k_uptime_get();
	if (now_ms >= session->deadline_ms ||
		message->deadline_ms > session->deadline_ms ||
		(message->timeout_ms != 0U &&
		 (uint64_t)message->timeout_ms > session->deadline_ms - now_ms)) {
		return -ETIMEDOUT;
	}

	memset(input_staging, 0, sizeof(input_staging));
	memset(output_staging, 0, sizeof(output_staging));
	switch (message->operation) {
	case EF800_PLUGIN_WORKER_PORT_WRITE:
		if (session->transport == NULL ||
			session->transport->write_until == NULL) {
			return -ENODEV;
		}
		ret = copy_user_range(mailbox, message->payload_offset, input_staging,
			message->payload_length, false);
		if (ret != 0) {
			return ret;
		}
		ret = session->transport->write_until(session->transport->context,
			input_staging, message->payload_length, message->deadline_ms);
		status = ret == (int)message->payload_length ? 0 :
			ret < 0 ? ret : -EIO;
		break;
	case EF800_PLUGIN_WORKER_PORT_READ:
		if (session->transport == NULL ||
			session->transport->read_until == NULL) {
			return -ENODEV;
		}
		{
			uint64_t read_deadline = now_ms + message->timeout_ms;

			if (read_deadline > session->deadline_ms) {
				read_deadline = session->deadline_ms;
			}
			ret = session->transport->read_until(
				session->transport->context, &output_staging[0],
				read_deadline);
		}
		if (ret == 1) {
			response_length = 1U;
			status = 0;
		} else {
			status = ret < 0 ? ret : -ETIMEDOUT;
			LOG_WRN("Plugin UART read failed: result=%d timeout=%u",
				ret, (unsigned int)message->timeout_ms);
		}
		break;
	case EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT:
		if (session->workspace == NULL) {
			return -ENODEV;
		}
		{
			size_t length = 0U;
			status = ef800_workspace_snapshot(session->workspace,
				(char *)output_staging, message->response_capacity,
				&length);
			if (status == 0) {
				response_length = (uint32_t)length;
			}
		}
		break;
	case EF800_PLUGIN_WORKER_CLOCK_UPTIME:
		{
			uint64_t uptime = (uint64_t)k_uptime_get();
			memcpy(output_staging, &uptime, sizeof(uptime));
			response_length = sizeof(uptime);
			status = 0;
		}
		break;
	case EF800_PLUGIN_WORKER_LOG:
		ret = copy_user_range(mailbox, message->payload_offset, input_staging,
			message->payload_length, false);
		if (ret != 0) {
			return ret;
		}
		input_staging[message->payload_length] = '\0';
		LOG_INF("plugin[%u]: %s", (unsigned int)session->generation,
			(char *)input_staging);
		status = 0;
		break;
	default:
		return -ENOTSUP;
	}

	if (response_length > message->response_capacity) {
		return -EOVERFLOW;
	}
	ret = copy_user_range(mailbox, message->response_offset, output_staging,
		response_length, true);
	if (ret != 0) {
		return ret;
	}
	message->status = status;
	message->response_length = response_length;
#if defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM
	mailbox->message = *message;
	return 0;
#else
	return k_usermode_to_copy(&mailbox->message, message,
		sizeof(*message)) == 0 ? 0 : -EFAULT;
#endif
}
#endif /* userspace verifier or native_sim gateway */

int z_impl_ef800_plugin_worker_syscall(
	struct ef800_plugin_worker_mailbox *mailbox)
{
#if defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM
	struct ef800_plugin_worker_message message;
	uint32_t required;
	int ret;

	if (mailbox == NULL || k_mutex_lock(&gateway_mutex, K_FOREVER) != 0) {
		return -EFAULT;
	}
	if (!active_session.active || k_current_get() != active_session.worker ||
		mailbox != active_session.mailbox) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -EPERM;
	}
	message = mailbox->message;
	message.deadline_ms = active_session.deadline_ms;
	ret = ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
		active_session.generation, (uint64_t)k_uptime_get());
	if (ret != 0 || message.kind != EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST ||
		message.capability != active_session.capability ||
		message.sequence <= active_session.last_sequence) {
		(void)k_mutex_unlock(&gateway_mutex);
		return ret != 0 ? ret : -EPERM;
	}
	required = required_service(message.operation);
	if (required == 0U ||
		(active_session.allowed_services & required) == 0U) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -EPERM;
	}
	active_session.last_sequence = message.sequence;
	ef800_plugin_host_recovery_service_begin(active_session.generation,
		message.operation);
	ret = dispatch_service(&active_session, mailbox, &message);
	ef800_plugin_host_recovery_service_end(active_session.generation,
		message.operation);
	(void)k_mutex_unlock(&gateway_mutex);
	return ret;
#else
	ARG_UNUSED(mailbox);
	/* Privileged callers must not use a worker-only service capability. */
	return -EPERM;
#endif
}

__pinned_func int ef800_plugin_worker_gateway_entry(
	struct ef800_plugin_worker_mailbox *mailbox)
{
	return ef800_plugin_worker_syscall(mailbox);
}

#if defined(CONFIG_USERSPACE) && CONFIG_USERSPACE
static int z_vrfy_ef800_plugin_worker_syscall(
	struct ef800_plugin_worker_mailbox *mailbox)
{
	struct ef800_plugin_worker_message message;
	uint32_t required;
	int ret;

	if (!k_is_in_user_syscall()) {
		return -EPERM;
	}
	if (mailbox == NULL || k_mutex_lock(&gateway_mutex, K_FOREVER) != 0) {
		return -EFAULT;
	}
	if (!active_session.active || k_current_get() != active_session.worker ||
		mailbox != active_session.mailbox) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -EPERM;
	}
	if (K_SYSCALL_MEMORY_READ(&mailbox->message, sizeof(message)) ||
		k_usermode_from_copy(&message, &mailbox->message,
			sizeof(message)) != 0) {
		LOG_WRN("Plugin gateway rejected unreadable mailbox message");
		(void)k_mutex_unlock(&gateway_mutex);
		return -EFAULT;
	}
	/* The active host deadline is authoritative; user code cannot extend it. */
	message.deadline_ms = active_session.deadline_ms;
	ret = ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE,
		active_session.generation, (uint64_t)k_uptime_get());
	if (ret != 0 || message.kind != EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST ||
		message.capability != active_session.capability ||
		message.deadline_ms > active_session.deadline_ms ||
		message.sequence <= active_session.last_sequence) {
		LOG_WRN("Plugin gateway rejected request: ret=%d kind=%u op=%u seq=%u prev=%u gen=%u active=%u",
			ret, (unsigned int)message.kind,
			(unsigned int)message.operation, (unsigned int)message.sequence,
			(unsigned int)active_session.last_sequence,
			(unsigned int)message.generation,
			(unsigned int)active_session.generation);
		(void)k_mutex_unlock(&gateway_mutex);
		return ret != 0 ? ret : -EPERM;
	}
	/* Preflight every user range before dispatch can perform host-side work. */
	if (K_SYSCALL_MEMORY_WRITE(&mailbox->message, sizeof(message)) ||
		(message.payload_length != 0U &&
		 K_SYSCALL_MEMORY_READ(&mailbox->payload[message.payload_offset],
			message.payload_length)) ||
		(message.response_capacity != 0U &&
		 K_SYSCALL_MEMORY_WRITE(&mailbox->payload[message.response_offset],
			 message.response_capacity))) {
		LOG_WRN("Plugin gateway rejected payload memory: op=%u in=%u/%u out=%u/%u",
			(unsigned int)message.operation,
			(unsigned int)message.payload_offset,
			(unsigned int)message.payload_length,
			(unsigned int)message.response_offset,
			(unsigned int)message.response_capacity);
		(void)k_mutex_unlock(&gateway_mutex);
		return -EFAULT;
	}
	required = required_service(message.operation);
	if (required == 0U || (active_session.allowed_services & required) == 0U) {
		(void)k_mutex_unlock(&gateway_mutex);
		return -EPERM;
	}
	active_session.last_sequence = message.sequence;
	ef800_plugin_host_recovery_service_begin(active_session.generation,
		message.operation);
	ret = dispatch_service(&active_session, mailbox, &message);
	if (ret != 0 || message.status != 0) {
		LOG_WRN("Plugin gateway service result: op=%u syscall=%d status=%d response=%u",
			(unsigned int)message.operation, ret, message.status,
			(unsigned int)message.response_length);
	}
	ef800_plugin_host_recovery_service_end(active_session.generation,
		message.operation);
	(void)k_mutex_unlock(&gateway_mutex);
	return ret;
}

#include <zephyr/syscalls/ef800_plugin_worker_syscall_mrsh.c>
#endif
