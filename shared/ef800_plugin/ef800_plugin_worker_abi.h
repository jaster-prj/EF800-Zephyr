/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Define and validate the pointer-free plugin worker mailbox ABI.
 */
#ifndef EF800_PLUGIN_WORKER_ABI_H
#define EF800_PLUGIN_WORKER_ABI_H

#include <stddef.h>
#include <stdint.h>

/*
 * Pointer-free mailbox ABI between the privileged supervisor and the single
 * unprivileged plugin worker. The mailbox is shared memory, not a trusted
 * object: the supervisor must copy the fixed header before validating it and
 * must validate/copy every payload range for each request.
 */
#define EF800_PLUGIN_WORKER_ABI_VERSION 1u
#define EF800_PLUGIN_WORKER_MAX_ACTIVE 1u
#define EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT 4096u
#define EF800_PLUGIN_WORKER_MAX_RESULT 4096u
#define EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE \
	(EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT + \
	 EF800_PLUGIN_WORKER_MAX_RESULT)
#define EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE 16384u
#define EF800_PLUGIN_WORKER_MAX_DEADLINE_MS 120000u
#define EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER 128u
#define EF800_PLUGIN_WORKER_MAX_LOG_MESSAGE 96u
#define EF800_PLUGIN_TARGET_ARM_CORTEX_M 1u
#define EF800_PLUGIN_TARGET_NATIVE_SIM 2u
#define EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x77u, 0x6fu, \
	0x72u, 0x6bu, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u
#define EF800_PLUGIN_WORKER_SERVICE_VERSION 1u

enum ef800_plugin_worker_service_capability {
	EF800_PLUGIN_WORKER_SERVICE_PORT = 1u << 0,
	EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT = 1u << 1,
	EF800_PLUGIN_WORKER_SERVICE_CLOCK = 1u << 2,
	EF800_PLUGIN_WORKER_SERVICE_LOG = 1u << 3,
};

enum ef800_plugin_worker_message_kind {
	EF800_PLUGIN_WORKER_HOST_INVOKE = 1,
	EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST = 2,
	EF800_PLUGIN_WORKER_RESULT = 3,
	EF800_PLUGIN_WORKER_SERVICE_RESPONSE = 4,
};

/* Host-to-worker entry points; every callback is run by the worker. */
enum ef800_plugin_worker_call {
	EF800_PLUGIN_WORKER_CREATE = 1,
	EF800_PLUGIN_WORKER_DESTROY,
	EF800_PLUGIN_WORKER_GET_SERIAL_DEFAULTS,
	EF800_PLUGIN_WORKER_MEASURE,
	EF800_PLUGIN_WORKER_INPUT,
	EF800_PLUGIN_WORKER_KEY,
	EF800_PLUGIN_WORKER_RENDER,
	EF800_PLUGIN_WORKER_SET_CONTEXT,
	EF800_PLUGIN_WORKER_GET_WORKSPACE_RECORD,
	EF800_PLUGIN_WORKER_WORKSPACE_RECORD_RESULT,
	EF800_PLUGIN_WORKER_CALCULATE,
	EF800_PLUGIN_WORKER_STOP,
};

/* Worker-to-host requests; no operation exposes a driver or host pointer. */
enum ef800_plugin_worker_service {
	EF800_PLUGIN_WORKER_PORT_WRITE = 0x100,
	EF800_PLUGIN_WORKER_PORT_READ,
	EF800_PLUGIN_WORKER_WORKSPACE_SNAPSHOT,
	EF800_PLUGIN_WORKER_CLOCK_UPTIME,
	EF800_PLUGIN_WORKER_LOG,
};

struct ef800_plugin_worker_message {
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t generation;
	uint32_t sequence;
	uint32_t kind;
	uint32_t operation;
	/* Session-scoped opaque capability; never a pointer or physical port ID. */
	uint32_t capability;
	uint32_t payload_offset;
	uint32_t payload_length;
	uint32_t response_offset;
	uint32_t response_capacity;
	uint32_t response_length;
	int32_t status;
	uint32_t timeout_ms;
	uint32_t reserved;
	/* Keep deadline_ms naturally aligned across 32-bit and 64-bit ABIs. */
	uint32_t deadline_padding;
	uint64_t deadline_ms;
};

_Static_assert(offsetof(struct ef800_plugin_worker_message, deadline_ms) == 64U,
	"plugin worker mailbox deadline offset is part of the ABI");
_Static_assert(sizeof(struct ef800_plugin_worker_message) == 72U,
	"plugin worker mailbox message size is part of the ABI");

struct ef800_plugin_worker_mailbox {
	struct ef800_plugin_worker_message message;
	/* Loader-provided addresses describe only user-mapped plugin regions. */
	uint32_t plugin_text_base;
	uint32_t plugin_text_size;
	uint32_t plugin_rodata_base;
	uint32_t plugin_rodata_size;
	uint32_t plugin_rw_base;
	uint32_t plugin_rw_size;
	uint32_t worker_generation;
	uint32_t allowed_services;
	uint32_t plugin_instance_offset;
	uint32_t plugin_instance_valid;
	volatile uint32_t host_sequence;
	volatile uint32_t worker_sequence;
	uint8_t payload[EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE];
};


_Static_assert(sizeof(struct ef800_plugin_worker_mailbox) <=
	EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE,
	"plugin mailbox must fit its MPU-aligned region");

/*
 * Validate a host-copied message header. This checks framing, ranges, bounded
 * operation-specific sizes, and deadlines. The gateway must additionally
 * verify worker identity/privilege, active generation, capability ownership,
 * operation permission, and safely copy user data before dispatch.
 */

/** @brief Validate a copied mailbox header's framing, ranges, and deadline.
 * @param message Trusted copy of the worker-provided fixed header.
 * @param payload_capacity Size of the mapped payload region.
 * @param active_generation Current worker generation for this session.
 * @param now_ms Current monotonic time in milliseconds.
 * @return 0 when structurally valid; negative errno when any bound is invalid.
 */
int ef800_plugin_worker_message_validate(
	const struct ef800_plugin_worker_message *message,
	size_t payload_capacity, uint32_t active_generation, uint64_t now_ms);

#endif /* EF800_PLUGIN_WORKER_ABI_H */
