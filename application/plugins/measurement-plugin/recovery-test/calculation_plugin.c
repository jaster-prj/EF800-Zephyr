/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_contract.h"
#include "ef800_plugin_worker_sdk.h"
#include "plugin_manager/plugin_abi.h"

#ifndef EF800_TEST_PLUGIN_MODE
#error EF800_TEST_PLUGIN_MODE is required
#endif

static struct ef800_calculation_instance test_instance;

#if EF800_TEST_PLUGIN_MODE == 9 || EF800_TEST_PLUGIN_MODE == 10
static struct ef800_plugin_worker_binding test_binding;

static int verify_rejected_service_request(
	const struct pm_service_table *services)
{
	static const char malformed_message[] = "malformed request rejected";
	static const char stale_message[] = "stale generation rejected";
	struct ef800_plugin_worker_message *message;
	int ret;

	ret = ef800_plugin_worker_binding_from_services(services, &test_binding);
	if (ret != 0 || test_binding.sequence == UINT32_MAX) {
		return PM_EPROTO;
	}
	message = &test_binding.mailbox->message;
	*message = (struct ef800_plugin_worker_message){
		.abi_version = EF800_PLUGIN_WORKER_ABI_VERSION,
		.struct_size = sizeof(*message),
		.generation = EF800_TEST_PLUGIN_MODE == 10 ?
			test_binding.generation + 1U : test_binding.generation,
		.sequence = test_binding.sequence + 1U,
		.kind = EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST,
		.operation = EF800_PLUGIN_WORKER_LOG,
		.capability = test_binding.capability,
		.reserved = EF800_TEST_PLUGIN_MODE == 9 ? 1U : 0U,
		.deadline_ms = test_binding.deadline_ms,
	};
	ret = ef800_plugin_worker_syscall(test_binding.mailbox);
	if (ret != -EINVAL) {
		return PM_EPROTO;
	}
	if (EF800_TEST_PLUGIN_MODE == 9) {
		ret = ef800_plugin_worker_log(&test_binding, malformed_message,
			sizeof(malformed_message) - 1U);
	} else {
		ret = ef800_plugin_worker_log(&test_binding, stale_message,
			sizeof(stale_message) - 1U);
	}
	return ret == 0 ? PM_OK : PM_EPROTO;
}
#endif

#if EF800_TEST_PLUGIN_MODE == 5
__attribute__((noinline)) static void overflow_worker_stack(uint32_t depth)
{
	volatile uint8_t frame[128];

	if (depth == UINT32_MAX) {
		return;
	}
	frame[depth & 0x7fU] = (uint8_t)depth;
	overflow_worker_stack(depth + 1U);
	__asm__ volatile("" : : "r"(frame[0]) : "memory");
}
#endif

#if EF800_TEST_PLUGIN_MODE >= 1 && EF800_TEST_PLUGIN_MODE <= 8
static void fault_now(void *opaque)
{
	(void)opaque;
#if EF800_TEST_PLUGIN_MODE == 1 || EF800_TEST_PLUGIN_MODE == 7 || \
	EF800_TEST_PLUGIN_MODE == 8
	__asm__ volatile("udf #0" ::: "memory");
	for (;;) {
		__asm__ volatile("nop");
	}
#elif EF800_TEST_PLUGIN_MODE == 2
	volatile uint32_t value = *(volatile uint32_t *)(uintptr_t)0x20000000U;
	(void)value;
#elif EF800_TEST_PLUGIN_MODE == 3
	*(volatile uint32_t *)(uintptr_t)0x20000000U = 0xfeedc0deU;
#elif EF800_TEST_PLUGIN_MODE == 4
	((void (*)(void))opaque)();
#elif EF800_TEST_PLUGIN_MODE == 5
	overflow_worker_stack(0U);
#elif EF800_TEST_PLUGIN_MODE == 6
	for (;;) {
		__asm__ volatile("nop");
	}
#endif
}
#endif

static int32_t transform(void *opaque, const char *snapshot,
	size_t snapshot_length, char *output, size_t capacity, size_t *written)
{
	if (opaque != &test_instance || snapshot == NULL || output == NULL ||
		written == NULL) {
		return PM_EINVAL;
	}
	(void)snapshot_length;

#if EF800_TEST_PLUGIN_MODE >= 1 && EF800_TEST_PLUGIN_MODE <= 6
	/* Faults originate in package text; XN mode branches into its RW instance. */
	fault_now(opaque);
#endif

	static const char result[] = "healthy calculation";
	size_t length = sizeof(result) - 1U;

	if (length > capacity) {
		return PM_EOVERFLOW;
	}
	for (size_t index = 0U; index < length; ++index) {
		output[index] = result[index];
	}
	*written = length;
	return PM_OK;
}

static const struct ef800_calculation_api test_api = {
	.abi_version = EF800_CALCULATION_API_VERSION,
	.struct_size = sizeof(test_api),
	.transform = transform,
	.reserved = {0U},
};

int pm_plugin_create(const struct pm_service_table *services, void **instance)
{
	if (services == NULL || instance == NULL) {
		return PM_EINVAL;
	}
	#if EF800_TEST_PLUGIN_MODE == 9 || EF800_TEST_PLUGIN_MODE == 10
	if (services->count != 1U || services->items == NULL ||
		verify_rejected_service_request(services) != PM_OK) {
		return PM_EPROTO;
	}
	#else
	if (services->count != 0U) {
		return PM_EINVAL;
	}
	#endif
	if (test_instance.api != NULL) {
		return PM_EBUSY;
	}
	#if EF800_TEST_PLUGIN_MODE == 7
	/* Exercise recovery when create faults before an instance is published. */
	fault_now(NULL);
	#endif
	test_instance.api = &test_api;
	*instance = &test_instance;
	return PM_OK;
}

int pm_plugin_destroy(void *instance)
{
	if (instance != &test_instance || test_instance.api == NULL) {
		return PM_EINVAL;
	}
	#if EF800_TEST_PLUGIN_MODE == 8
	/* Exercise recovery while the manager's destroy dispatch is active. */
	fault_now(NULL);
	#endif
	test_instance.api = NULL;
	return PM_OK;
}
