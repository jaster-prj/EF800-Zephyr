/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_host.h"
#include "ef800_memory_pool.h"

#include <errno.h>
#include <string.h>

#if CONFIG_BOARD_NATIVE_SIM
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/app_memory/mem_domain.h>
#include <zephyr/arch/arm/arm-m-switch.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER && !CONFIG_BOARD_NATIVE_SIM
#include <zephyr/arch/exception.h>
#include <zephyr/arch/arm/arch.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <cmsis_core.h>
#endif

#include "plugin_manager/plugin_descriptor.h"
#include "plugin_manager/plugin_contract.h"
#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_manager_zephyr.h"
#include "plugin_manager/plugin_target_arm.h"
#ifdef CONFIG_BOARD_NATIVE_SIM
#include "plugin_manager/plugin_target_native.h"
#endif

#include "ef800_main.h"
#include "ef800_plugin_contract.h"
#include "ef800_measurement_plugin.h"
#include "ef800_plugin_recovery_state.h"
#include "ef800_legacy_port_service.h"
#include "ef800_plugin_worker_abi.h"
#include "ef800_plugin_worker.h"
#include "ef800_plugin_syscall.h"
#include "ef800_memory_pool.h"
LOG_MODULE_REGISTER(ef800_plugin_host, LOG_LEVEL_INF);

#define EF800_PLUGIN_DIRECTORY "plugins"
#if CONFIG_EF800_MAIN_PLUGIN_RECOVERY_TEST_DIRECTORY
#undef EF800_PLUGIN_DIRECTORY
#define EF800_PLUGIN_DIRECTORY "data/EF800-recovery-test"
#endif
#define EF800_PLUGIN_SUFFIX ".PMP"
#if CONFIG_EF800_MAIN_PLUGIN_RECOVERY_TEST_DIRECTORY
#define EF800_PLUGIN_MAX_ENTRIES 2u
#define EF800_PLUGIN_SCAN_ENTRIES 9u
#define EF800_PLUGIN_DESCRIPTOR_SLOTS 9u
#elif CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_MAX_ENTRIES 16u
#define EF800_PLUGIN_SCAN_ENTRIES EF800_PLUGIN_MAX_ENTRIES
#define EF800_PLUGIN_DESCRIPTOR_SLOTS EF800_PLUGIN_SCAN_ENTRIES
#else
#define EF800_PLUGIN_MAX_ENTRIES 3u
#define EF800_PLUGIN_SCAN_ENTRIES 16u
#define EF800_PLUGIN_DESCRIPTOR_SLOTS EF800_PLUGIN_MAX_ENTRIES
#endif
#define EF800_PLUGIN_MAX_PACKAGE_SIZE (32u * 1024u)
#if CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_TEXT_SIZE 0x8000u
#define EF800_PLUGIN_RODATA_SIZE 0x8000u
#define EF800_PLUGIN_DATA_SIZE 0x4000u
#define EF800_PLUGIN_BSS_SIZE 0x4000u
#else
#define EF800_PLUGIN_TEXT_SIZE EF800_PLUGIN_MAX_PACKAGE_SIZE
#define EF800_PLUGIN_RODATA_SIZE EF800_PLUGIN_MAX_PACKAGE_SIZE
#define EF800_PLUGIN_DATA_SIZE 0x0800u
#define EF800_PLUGIN_BSS_SIZE 0x0800u
#endif
#define EF800_PLUGIN_RW_REGION_SIZE 0x1000u
#ifndef CONFIG_EF800_MAIN_PLUGIN_WORKER_STACK_SIZE
#define CONFIG_EF800_MAIN_PLUGIN_WORKER_STACK_SIZE 2048u
#endif
#define EF800_PLUGIN_WORKER_STACK_SIZE \
	CONFIG_EF800_MAIN_PLUGIN_WORKER_STACK_SIZE
#define EF800_PLUGIN_WORKER_OUTPUT_OFFSET \
	EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT
#define EF800_PLUGIN_DEADLINE_LIFECYCLE_CREATE_MS 30000U
#define EF800_PLUGIN_DEADLINE_LIFECYCLE_DESTROY_MS 3000U
#define EF800_PLUGIN_DEADLINE_MEASURE_MS 30000U
#if CONFIG_EF800_MAIN_PLUGIN_RECOVERY_TEST_DIRECTORY
#define EF800_PLUGIN_DEADLINE_CALCULATION_MS 1000U
#else
#define EF800_PLUGIN_DEADLINE_CALCULATION_MS 60000U
#endif
#define EF800_PLUGIN_DEADLINE_INTERACTIVE_MS 1000U
#define EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE \
	EF800_PLUGIN_MAX_RESULT

static struct pm_manager *manager;
static struct pm_application_contract contract;
/* PluginManager retains this adapter as its filesystem identity and callback context. */
static struct pm_zephyr_filesystem plugin_filesystem;
static struct ef800_plugin_registry *active_registry;
static bool ready;
K_MUTEX_DEFINE(plugin_host_mutex);
static struct pm_plugin_descriptor descriptor_slots[EF800_PLUGIN_DESCRIPTOR_SLOTS];
static uint32_t descriptor_slot_ids[EF800_PLUGIN_DESCRIPTOR_SLOTS];
static bool descriptor_slot_used[EF800_PLUGIN_DESCRIPTOR_SLOTS];

struct plugin_image_slot {
	bool used;
	uint8_t *text;
	uint8_t *rodata;
	uint8_t *data;
	uint8_t *bss;
};

#if !CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_INTERNAL_ARENA_SIZE 0x4000u
static uint8_t plugin_text_internal[EF800_PLUGIN_INTERNAL_ARENA_SIZE]
	__aligned(EF800_PLUGIN_INTERNAL_ARENA_SIZE);
static uint8_t plugin_rodata_internal[EF800_PLUGIN_INTERNAL_ARENA_SIZE]
	__aligned(EF800_PLUGIN_INTERNAL_ARENA_SIZE);
static uint8_t *plugin_text_arena = plugin_text_internal;
static uint8_t *plugin_rodata_arena = plugin_rodata_internal;
static size_t plugin_text_capacity = EF800_PLUGIN_INTERNAL_ARENA_SIZE;
static size_t plugin_rodata_capacity = EF800_PLUGIN_INTERNAL_ARENA_SIZE;
static bool plugin_arenas_external;
BUILD_ASSERT(sizeof(plugin_text_internal) == EF800_PLUGIN_INTERNAL_ARENA_SIZE);
BUILD_ASSERT(sizeof(plugin_rodata_internal) == EF800_PLUGIN_INTERNAL_ARENA_SIZE);
#else
/* Native-sim loads each package into its own mmap-backed image slot. These
 * capacities still bound admission, but are not addresses in shared arenas. */
static const size_t plugin_text_capacity = EF800_PLUGIN_TEXT_SIZE;
static const size_t plugin_rodata_capacity = EF800_PLUGIN_RODATA_SIZE;
#endif

#if CONFIG_BOARD_NATIVE_SIM
#define EF800_PLUGIN_IMAGE_SLOT_COUNT EF800_PLUGIN_MAX_ENTRIES
#else
#define EF800_PLUGIN_IMAGE_SLOT_COUNT 1U
#endif
static struct plugin_image_slot plugin_image_slots[EF800_PLUGIN_IMAGE_SLOT_COUNT];
#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static struct ef800_port_api port_api;
#endif
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static uint32_t worker_service_placeholder;
#endif
static struct pm_service service_items[2];
static struct pm_service_table service_table;
static struct ef800_plugin_recovery_ledger plugin_recovery_ledger;
static struct k_spinlock plugin_recovery_lock;
static ef800_plugin_route_recovery_fn route_recovery_handler;
static void *route_recovery_context;
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void recovery_ledger_set_route(uint32_t plugin_id, uint8_t port_id,
	uint32_t connection, bool claimed,
	const struct ef800_measurement_transport *transport);
static int plugin_worker_recover_failure(
	const struct ef800_plugin_async_request *request,
	struct ef800_plugin_async_result *result);
#endif

#define EF800_PLUGIN_MPU_ALIGNMENT 1024u

struct plugin_mailbox_region {
	struct ef800_plugin_worker_mailbox mailbox;
	uint8_t padding[EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE -
		EF800_PLUGIN_RW_REGION_SIZE -
		sizeof(struct ef800_plugin_worker_mailbox)];
	uint8_t plugin_rw[EF800_PLUGIN_RW_REGION_SIZE]
		__aligned(EF800_PLUGIN_RW_REGION_SIZE);
} __aligned(EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE);

BUILD_ASSERT(sizeof(struct plugin_mailbox_region) ==
	EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE);
BUILD_ASSERT(offsetof(struct plugin_mailbox_region, plugin_rw) ==
	EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE - EF800_PLUGIN_RW_REGION_SIZE);
static struct plugin_mailbox_region plugin_mailbox_region __used;
#if !CONFIG_BOARD_NATIVE_SIM
#define plugin_rw_arena (plugin_mailbox_region.plugin_rw)
#endif
K_THREAD_STACK_DEFINE(plugin_worker_stack,
	CONFIG_EF800_MAIN_PLUGIN_WORKER_STACK_SIZE);
static struct k_thread plugin_worker_thread __used;
static volatile uintptr_t worker_support_anchor[3];

#if !CONFIG_BOARD_NATIVE_SIM
static struct k_mem_domain plugin_worker_domain;
static struct k_mem_partition plugin_worker_text_partition = {
	.start = (uintptr_t)plugin_text_internal,
	.size = EF800_PLUGIN_INTERNAL_ARENA_SIZE,
	.attr = K_MEM_PARTITION_P_RX_U_RX,
};
static struct k_mem_partition plugin_worker_rodata_partition = {
	.start = (uintptr_t)plugin_rodata_internal,
	.size = EF800_PLUGIN_INTERNAL_ARENA_SIZE,
	.attr = K_MEM_PARTITION_P_RO_U_RO,
};
static struct k_mem_partition plugin_worker_mailbox_partition = {
	.start = (uintptr_t)&plugin_mailbox_region,
	.size = sizeof(plugin_mailbox_region),
	.attr = K_MEM_PARTITION_P_RW_U_RW,
};
/* Zephyr's user-thread entry reads the current TLS pointer through
 * __aeabi_read_tp. Expose only its aligned word/neighbor region read-only;
 * mapping the writable libc partition would let a plugin corrupt libc state. */
static struct k_mem_partition plugin_worker_tls_partition = {
	.start = (uintptr_t)&z_arm_tls_ptr,
	.size = 32U,
	.attr = K_MEM_PARTITION_P_RW_U_RO,
};
static struct k_mem_partition *plugin_worker_partitions[] = {
	&plugin_worker_text_partition,
	&plugin_worker_rodata_partition,
	&plugin_worker_mailbox_partition,
	&plugin_worker_tls_partition,
};
BUILD_ASSERT(ARRAY_SIZE(plugin_worker_partitions) == 4U);
static bool plugin_worker_domain_initialized;
static struct k_thread *plugin_worker_domain_thread;
static uint32_t plugin_worker_generation;
K_MUTEX_DEFINE(plugin_worker_domain_mutex);
#endif

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static bool plugin_worker_running;
static bool plugin_worker_thread_prepared;
static bool plugin_worker_poisoned;
static bool plugin_worker_reap_failed;
static bool plugin_worker_faulted;
static uint32_t plugin_worker_fault_reason;
static uint32_t plugin_worker_fault_pc;
static uint32_t plugin_worker_fault_generation;
static uint32_t plugin_worker_capability;
static uint32_t plugin_worker_sequence;
static uint32_t plugin_worker_allowed_services;
static const struct ef800_measurement_transport *plugin_worker_transport;
static const struct ef800_workspace *plugin_worker_workspace;
static uint8_t plugin_manager_instance_token;

#define EF800_PLUGIN_SUPERVISOR_STACK_SIZE 3072u
K_THREAD_STACK_DEFINE(plugin_supervisor_stack,
	EF800_PLUGIN_SUPERVISOR_STACK_SIZE);
static struct k_thread plugin_supervisor_thread;
K_MUTEX_DEFINE(plugin_async_mutex);
K_SEM_DEFINE(plugin_async_wakeup, 0, 1);
static struct ef800_plugin_async_request plugin_async_request;
static struct ef800_plugin_async_result plugin_async_result;
static uint32_t plugin_async_next_token;
static bool plugin_async_busy;
static bool plugin_async_queued;
static bool plugin_async_complete;
static bool plugin_supervisor_started;
static char plugin_async_calculation_output_internal[
	EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE];
static char *plugin_async_calculation_output =
	plugin_async_calculation_output_internal;
#if !CONFIG_BOARD_NATIVE_SIM
	struct plugin_worker_fault_record {
	atomic_t state;
	atomic_t reason;
	atomic_t pc;
	atomic_t generation;
};

static struct plugin_worker_fault_record plugin_worker_fault_record = {
	.state = ATOMIC_INIT(0),
	.reason = ATOMIC_INIT(0),
	.pc = ATOMIC_INIT(0),
	.generation = ATOMIC_INIT(0),
};
K_SEM_DEFINE(plugin_worker_fault_wakeup, 0, 1);
static atomic_t plugin_worker_invocation_active = ATOMIC_INIT(0);
#endif
#endif

#if !CONFIG_BOARD_NATIVE_SIM
static void plugin_arenas_configure(void)
{
	uint8_t *text = NULL;
	uint8_t *rodata = NULL;

	if (ef800_memory_pool_external_ready()) {
		text = ef800_memory_pool_external_alloc(EF800_PLUGIN_TEXT_SIZE,
							EF800_PLUGIN_TEXT_SIZE);
		rodata = ef800_memory_pool_external_alloc(EF800_PLUGIN_RODATA_SIZE,
							  EF800_PLUGIN_RODATA_SIZE);
	}
	if (text != NULL && rodata != NULL) {
		plugin_text_arena = text;
		plugin_rodata_arena = rodata;
		plugin_text_capacity = EF800_PLUGIN_TEXT_SIZE;
		plugin_rodata_capacity = EF800_PLUGIN_RODATA_SIZE;
		plugin_arenas_external = true;
	} else {
		ef800_memory_pool_external_free(text);
		ef800_memory_pool_external_free(rodata);
		plugin_text_arena = plugin_text_internal;
		plugin_rodata_arena = plugin_rodata_internal;
		plugin_text_capacity = sizeof(plugin_text_internal);
		plugin_rodata_capacity = sizeof(plugin_rodata_internal);
		plugin_arenas_external = false;
	}
	plugin_worker_text_partition.start = (uintptr_t)plugin_text_arena;
	plugin_worker_text_partition.size = plugin_text_capacity;
	plugin_worker_rodata_partition.start = (uintptr_t)plugin_rodata_arena;
	plugin_worker_rodata_partition.size = plugin_rodata_capacity;
	LOG_INF("Plugin arenas profile=%s text=%p/%u rodata=%p/%u",
		plugin_arenas_external ? "external" : "internal-reduced",
		plugin_text_arena, (unsigned int)plugin_text_capacity,
		plugin_rodata_arena, (unsigned int)plugin_rodata_capacity);
}
#endif

static void recovery_ledger_manager_state(uint32_t plugin_id,
	bool loaded, bool instance_active)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	plugin_recovery_ledger.active = plugin_id != 0U ||
		plugin_recovery_ledger.active;
	if (plugin_id != 0U) {
		plugin_recovery_ledger.manager_plugin_id = plugin_id;
	}
	plugin_recovery_ledger.manager_loaded = loaded;
	plugin_recovery_ledger.instance_active = instance_active;
	k_spin_unlock(&plugin_recovery_lock, key);
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void recovery_ledger_invocation_begin(uint32_t generation,
	uint32_t operation, size_t output_capacity, uint64_t deadline_ms)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	plugin_recovery_ledger.generation = generation;
	plugin_recovery_ledger.operation = operation;
	plugin_recovery_ledger.invocation_deadline_ms = deadline_ms;
	plugin_recovery_ledger.invocation_pending = true;
	plugin_recovery_ledger.output_pending = output_capacity != 0U;
	plugin_recovery_ledger.output_capacity = output_capacity;
	plugin_recovery_ledger.last_status = 0;
	k_spin_unlock(&plugin_recovery_lock, key);
}

static void recovery_ledger_invocation_end(uint32_t generation, int status)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	if (plugin_recovery_ledger.generation == generation) {
		plugin_recovery_ledger.invocation_pending = false;
		plugin_recovery_ledger.output_pending = false;
		plugin_recovery_ledger.output_capacity = 0U;
		plugin_recovery_ledger.last_status = status;
	}
	k_spin_unlock(&plugin_recovery_lock, key);
}
#endif

static void recovery_ledger_cleanup_mark(uint32_t progress, bool in_progress)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	plugin_recovery_ledger.cleanup_progress |= progress;
	plugin_recovery_ledger.cleanup_in_progress = in_progress;
	k_spin_unlock(&plugin_recovery_lock, key);
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void recovery_ledger_output_pending(size_t capacity)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	plugin_recovery_ledger.output_pending = capacity != 0U;
	plugin_recovery_ledger.output_capacity = (uint32_t)capacity;
	k_spin_unlock(&plugin_recovery_lock, key);
}
#endif

static void recovery_ledger_clear_if_quiescent(void)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	if (!plugin_recovery_ledger.worker_mapped &&
		!plugin_recovery_ledger.manager_loaded &&
		!plugin_recovery_ledger.instance_active &&
		!plugin_recovery_ledger.invocation_pending &&
		!plugin_recovery_ledger.service_pending &&
		!plugin_recovery_ledger.cleanup_in_progress) {
		memset(&plugin_recovery_ledger, 0, sizeof(plugin_recovery_ledger));
	}
	k_spin_unlock(&plugin_recovery_lock, key);
}

#if CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION
/* This adapter exists only for trusted development and native_sim. It lets
 * PluginManager own instance/service bookkeeping, but deliberately executes
 * callbacks inline; it is not the production isolation executor. */
static int execute_plugin_create_inline(
	void *context, pm_plugin_create_fn entry,
	const struct pm_service_table *services, void **instance)
{
	ARG_UNUSED(context);
	return entry == NULL ? PM_EINVAL : entry(services, instance);
}

static int execute_plugin_destroy_inline(void *context,
	pm_plugin_destroy_fn entry, void *instance)
{
	ARG_UNUSED(context);
	return entry == NULL ? PM_EINVAL : entry(instance);
}

static const struct pm_manager_instance_executor inline_executor = {
	.create = execute_plugin_create_inline,
	.destroy = execute_plugin_destroy_inline,
};
#endif

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static int plugin_worker_stop(bool abort_worker);

#if !CONFIG_BOARD_NATIVE_SIM
static bool plugin_worker_fault_reason_supported(unsigned int reason)
{
	switch (reason) {
	case K_ERR_ARM_MEM_DATA_ACCESS:
	case K_ERR_ARM_MEM_INSTRUCTION_ACCESS:
	case K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION:
		return true;
	default:
		return false;
	}
}

static bool plugin_worker_fault_pc_attributable(uintptr_t pc)
{
	uintptr_t address = pc & ~(uintptr_t)1U;
	uintptr_t text_start = (uintptr_t)plugin_text_arena;
	uintptr_t rodata_start = (uintptr_t)plugin_rodata_arena;
	uintptr_t rw_start = (uintptr_t)plugin_rw_arena;
	uintptr_t mailbox_start = (uintptr_t)&plugin_mailbox_region;

	return (address >= text_start &&
		address - text_start < plugin_text_capacity) ||
		(address >= rodata_start &&
		address - rodata_start < plugin_rodata_capacity) ||
		(address >= rw_start && address - rw_start < sizeof(plugin_rw_arena)) ||
		(address >= mailbox_start &&
		address - mailbox_start < sizeof(plugin_mailbox_region));
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	struct k_thread *current = k_current_get();

	/* This thread is created K_USER. A thread-mode frame whose PC is inside
	 * one of its mapped plugin regions makes attribution conservative without
	 * querying privilege through an API that treats handler mode as privileged. */
	if (esf != NULL && current == &plugin_worker_thread &&
		plugin_worker_running &&
		atomic_get(&plugin_worker_invocation_active) != 0 &&
		(esf->basic.xpsr & IPSR_ISR_Msk) == 0U &&
		plugin_worker_fault_reason_supported(reason) &&
		plugin_worker_fault_pc_attributable(esf->basic.pc) &&
		atomic_cas(&plugin_worker_fault_record.state, 0, 1)) {
		/* This is deliberately limited to a preallocated record and the
		 * target-validated semaphore wakeup. Zephyr will abort this thread
		 * after the handler returns; no plugin frame is resumed. */
		atomic_set(&plugin_worker_fault_record.reason, (atomic_val_t)reason);
		atomic_set(&plugin_worker_fault_record.pc,
			(atomic_val_t)esf->basic.pc);
		atomic_set(&plugin_worker_fault_record.generation,
			(atomic_val_t)plugin_worker_generation);
		barrier_dmem_fence_full();
		atomic_set(&plugin_worker_fault_record.state, 2);
		k_sem_give(&plugin_worker_fault_wakeup);
		return;
	}

	/* Unattributable, nested, ISR, privileged, unsupported, or repeated
	 * faults cannot establish plugin-only containment. Zephyr has already
	 * emitted the fatal diagnostic; reset instead of returning to uncertain
	 * host state. */
	sys_reboot(SYS_REBOOT_COLD);
}
#endif /* !CONFIG_BOARD_NATIVE_SIM */

static int plugin_worker_prepare_thread(void)
{
	uint32_t generation;
	int ret;

	if (plugin_worker_thread_prepared) {
		return 0;
	}
	memset(plugin_worker_stack, 0, K_THREAD_STACK_SIZEOF(plugin_worker_stack));
	if (k_thread_create(&plugin_worker_thread, plugin_worker_stack,
		K_THREAD_STACK_SIZEOF(plugin_worker_stack), ef800_plugin_worker_entry,
		ef800_plugin_host_worker_mailbox(), plugin_text_arena,
		plugin_rw_arena, 5, K_USER, K_FOREVER) == NULL) {
		return -ENOMEM;
	}
	k_object_access_grant(&plugin_worker_thread, k_current_get());
	ret = ef800_plugin_host_worker_domain_attach(&plugin_worker_thread,
		&generation);
	if (ret != 0 || generation != plugin_worker_generation) {
		k_thread_abort(&plugin_worker_thread);
		return ret != 0 ? ret : -ESTALE;
	}
	plugin_worker_thread_prepared = true;
	return 0;
}

static int plugin_worker_request(uint32_t operation, const void *input,
	size_t input_length, void *output, size_t output_capacity,
	size_t *output_length, uint32_t timeout_ms)
{
	struct ef800_plugin_worker_mailbox *mailbox =
		ef800_plugin_host_worker_mailbox();
	struct ef800_plugin_worker_message request;
	uint64_t deadline_ms;
	uint32_t sequence;
	int ret;

	if (!plugin_worker_running || mailbox == NULL || timeout_ms == 0U ||
		timeout_ms > EF800_PLUGIN_WORKER_MAX_DEADLINE_MS ||
		input_length > EF800_PLUGIN_WORKER_MAX_WORKSPACE_SNAPSHOT ||
		output_capacity > EF800_PLUGIN_WORKER_MAX_RESULT ||
		(input == NULL && input_length != 0U) ||
		(output == NULL && output_capacity != 0U)) {
		return -EINVAL;
	}
	if (plugin_worker_poisoned) {
		return -EIO;
	}
	ret = plugin_worker_prepare_thread();
	if (ret != 0) {
		return ret;
	}
	if (plugin_worker_sequence == UINT32_MAX) {
		return -EOVERFLOW;
	}
	deadline_ms = (uint64_t)k_uptime_get() + timeout_ms;
	ret = ef800_plugin_gateway_session_set_deadline(plugin_worker_generation,
		deadline_ms);
	if (ret != 0) {
		return ret;
	}
	sequence = ++plugin_worker_sequence;
	recovery_ledger_invocation_begin(plugin_worker_generation, operation,
		output_capacity, deadline_ms);
	if (input_length != 0U) {
		memcpy(mailbox->payload, input, input_length);
	}
	request = (struct ef800_plugin_worker_message){
		.abi_version = EF800_PLUGIN_WORKER_ABI_VERSION,
		.struct_size = sizeof(request),
		.generation = plugin_worker_generation,
		.sequence = sequence,
		.kind = EF800_PLUGIN_WORKER_HOST_INVOKE,
		.operation = operation,
		.capability = plugin_worker_capability,
		.payload_offset = 0U,
		.payload_length = input_length,
		.response_offset = EF800_PLUGIN_WORKER_OUTPUT_OFFSET,
		.response_capacity = output_capacity,
		.deadline_ms = deadline_ms,
	};
	mailbox->message = request;
	barrier_dmem_fence_full();
	mailbox->host_sequence = sequence;
	barrier_dmem_fence_full();
#if !CONFIG_BOARD_NATIVE_SIM
	atomic_set(&plugin_worker_fault_record.state, 0);
	atomic_set(&plugin_worker_fault_record.reason, 0);
	atomic_set(&plugin_worker_fault_record.pc, 0);
	atomic_set(&plugin_worker_fault_record.generation, 0);
	(void)k_sem_reset(&plugin_worker_fault_wakeup);
	plugin_worker_fault_reason = 0U;
	plugin_worker_fault_pc = 0U;
	plugin_worker_fault_generation = 0U;
	atomic_set(&plugin_worker_invocation_active, 1);
#endif
	plugin_worker_faulted = false;
	k_thread_start(&plugin_worker_thread);

	while (mailbox->worker_sequence != sequence) {
#if !CONFIG_BOARD_NATIVE_SIM
		if (k_sem_take(&plugin_worker_fault_wakeup, K_NO_WAIT) == 0 &&
			atomic_get(&plugin_worker_fault_record.state) == 2 &&
			(uint32_t)atomic_get(&plugin_worker_fault_record.generation) ==
				plugin_worker_generation) {
			atomic_set(&plugin_worker_invocation_active, 0);
			goto worker_fault_call;
		}
#endif
		if ((uint64_t)k_uptime_get() >= deadline_ms) {
			ret = -ETIMEDOUT;
			goto abort_worker_call;
		}
		k_sleep(K_MSEC(1));
	}
	barrier_dmem_fence_full();
	request = mailbox->message;
#if !CONFIG_BOARD_NATIVE_SIM
	atomic_set(&plugin_worker_invocation_active, 0);
#endif
	ret = k_thread_join(&plugin_worker_thread, K_MSEC(500));
	if (ret != 0) {
		k_thread_abort(&plugin_worker_thread);
		ret = k_thread_join(&plugin_worker_thread, K_MSEC(500));
	}
	plugin_worker_thread_prepared = false;
	memset(plugin_worker_stack, 0, K_THREAD_STACK_SIZEOF(plugin_worker_stack));
	if (ret != 0) {
		plugin_worker_poisoned = true;
		plugin_worker_reap_failed = true;
		recovery_ledger_invocation_end(plugin_worker_generation, ret);
		return ret;
	}
	if (request.kind != EF800_PLUGIN_WORKER_RESULT ||
		request.generation != plugin_worker_generation ||
		request.sequence != sequence ||
		request.response_length > output_capacity) {
		recovery_ledger_invocation_end(plugin_worker_generation, -EPROTO);
		return -EPROTO;
	}
	if (request.status != PM_OK) {
		if (operation == EF800_PLUGIN_WORKER_GET_WORKSPACE_RECORD &&
			request.status == PM_ENOENT) {
			LOG_DBG("Plugin Workspace record not ready: sequence=%u",
				(unsigned int)sequence);
		} else {
			LOG_WRN("Plugin worker request failed: operation=%u sequence=%u status=%d response=%u",
				(unsigned int)operation, (unsigned int)sequence, request.status,
				(unsigned int)request.response_length);
		}
		recovery_ledger_invocation_end(plugin_worker_generation,
			request.status);
		if (output_capacity != 0U) {
			recovery_ledger_cleanup_mark(
				EF800_PLUGIN_RECOVERY_OUTPUT_DISCARDED, false);
		}
		return request.status;
	}
	if (request.response_length != 0U) {
		memcpy(output, &mailbox->payload[request.response_offset],
			request.response_length);
	}
	if (output_length != NULL) {
		*output_length = request.response_length;
	}
	recovery_ledger_invocation_end(plugin_worker_generation, PM_OK);
	return PM_OK;

#if !CONFIG_BOARD_NATIVE_SIM
worker_fault_call:
	/* The fatal hook returns only for a classified worker fault. Zephyr's
	 * fatal path then aborts that thread before this supervisor resumes. */
	barrier_dmem_fence_full();
	plugin_worker_fault_reason =
		(uint32_t)atomic_get(&plugin_worker_fault_record.reason);
	plugin_worker_fault_pc =
		(uint32_t)atomic_get(&plugin_worker_fault_record.pc);
	plugin_worker_fault_generation =
		(uint32_t)atomic_get(&plugin_worker_fault_record.generation);
	ret = k_thread_join(&plugin_worker_thread, K_MSEC(500));
	if (ret != 0) {
		k_thread_abort(&plugin_worker_thread);
		ret = k_thread_join(&plugin_worker_thread, K_MSEC(500));
	}
	plugin_worker_thread_prepared = false;
	plugin_worker_poisoned = true;
	plugin_worker_faulted = true;
	if (ret != 0) {
		plugin_worker_reap_failed = true;
		recovery_ledger_invocation_end(plugin_worker_generation, -EIO);
		return -EIO;
	}
	recovery_ledger_cleanup_mark(EF800_PLUGIN_RECOVERY_WORKER_STOPPED, true);
	recovery_ledger_invocation_end(plugin_worker_generation, -EFAULT);
	return -EFAULT;
#endif

abort_worker_call:
#if !CONFIG_BOARD_NATIVE_SIM
	atomic_set(&plugin_worker_invocation_active, 0);
#endif
	k_thread_abort(&plugin_worker_thread);
	ret = k_thread_join(&plugin_worker_thread, K_MSEC(500));
	plugin_worker_thread_prepared = false;
	plugin_worker_poisoned = true;
	if (ret != 0) {
		plugin_worker_reap_failed = true;
		recovery_ledger_invocation_end(plugin_worker_generation, -EIO);
		return -EIO;
	}
	recovery_ledger_cleanup_mark(EF800_PLUGIN_RECOVERY_WORKER_STOPPED, true);
	recovery_ledger_invocation_end(plugin_worker_generation, -ETIMEDOUT);
	return -ETIMEDOUT;
}

static int plugin_worker_start(void)
{
	struct ef800_plugin_worker_mailbox *mailbox =
		ef800_plugin_host_worker_mailbox();
	uint32_t generation;
	uint64_t deadline_ms;
	int ret;

	if (plugin_worker_running || mailbox == NULL) {
		return -EBUSY;
	}
	plugin_worker_poisoned = false;
	plugin_worker_reap_failed = false;
	if (k_thread_create(&plugin_worker_thread, plugin_worker_stack,
		K_THREAD_STACK_SIZEOF(plugin_worker_stack), ef800_plugin_worker_entry,
		mailbox, plugin_text_arena, plugin_rw_arena, 5, K_USER, K_FOREVER) ==
		NULL) {
		return -ENOMEM;
	}
	k_object_access_grant(&plugin_worker_thread, k_current_get());
	ret = ef800_plugin_host_worker_domain_attach(&plugin_worker_thread,
		&generation);
	if (ret != 0) {
		k_thread_abort(&plugin_worker_thread);
		return ret;
	}
	mailbox->plugin_text_base = (uint32_t)(uintptr_t)plugin_text_arena;
	mailbox->plugin_text_size = plugin_text_capacity;
	mailbox->plugin_rodata_base = (uint32_t)(uintptr_t)plugin_rodata_arena;
	mailbox->plugin_rodata_size = plugin_rodata_capacity;
	mailbox->plugin_rw_base = (uint32_t)(uintptr_t)plugin_rw_arena;
	mailbox->plugin_rw_size = sizeof(plugin_rw_arena);
	mailbox->allowed_services = plugin_worker_allowed_services;
	plugin_worker_generation = generation;
	plugin_worker_capability = generation ^ (uint32_t)k_uptime_get_32() ^
		0xef800a5aU;
	if (plugin_worker_capability == 0U) {
		plugin_worker_capability = 1U;
	}
	deadline_ms = (uint64_t)k_uptime_get() +
		EF800_PLUGIN_WORKER_MAX_DEADLINE_MS;
	ret = ef800_plugin_gateway_session_bind(&plugin_worker_thread, mailbox,
		generation, plugin_worker_capability, plugin_worker_allowed_services,
		deadline_ms, plugin_worker_transport, plugin_worker_workspace);
	if (ret != 0) {
		(void)ef800_plugin_host_worker_domain_detach(&plugin_worker_thread,
			generation);
		k_thread_abort(&plugin_worker_thread);
		return ret;
	}
	plugin_worker_running = true;
	plugin_worker_thread_prepared = true;
	{
		k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

		plugin_recovery_ledger.active = true;
		plugin_recovery_ledger.generation = generation;
		plugin_recovery_ledger.worker_mapped = true;
		k_spin_unlock(&plugin_recovery_lock, key);
	}
	return 0;
}

static int plugin_worker_reap_and_unbind(bool abort_worker)
{
	int ret;

	if (!plugin_worker_running) {
		return 0;
	}
	if (plugin_worker_reap_failed) {
		return -EIO;
	}
	if (plugin_worker_thread_prepared) {
		if (abort_worker) {
			k_thread_abort(&plugin_worker_thread);
		}
		ret = k_thread_join(&plugin_worker_thread, K_MSEC(1000));
		if (ret != 0) {
			k_thread_abort(&plugin_worker_thread);
			ret = k_thread_join(&plugin_worker_thread, K_MSEC(1000));
		}
		if (ret != 0) {
			plugin_worker_reap_failed = true;
			return ret;
		}
		plugin_worker_thread_prepared = false;
	}
	ret = ef800_plugin_gateway_session_unbind(plugin_worker_generation);
	if (ret != 0 && ret != -ESTALE) {
		return ret;
	}
	recovery_ledger_cleanup_mark(
		EF800_PLUGIN_RECOVERY_SERVICES_CANCELLED |
		EF800_PLUGIN_RECOVERY_GENERATION_INVALIDATED, true);
	plugin_worker_running = false;
	plugin_worker_capability = 0U;
	return 0;
}

static int plugin_worker_stop(bool abort_worker)
{
	int result = 0;
	int ret;

	if (!plugin_worker_running) {
		return 0;
	}
	recovery_ledger_cleanup_mark(0U, true);
	if (!abort_worker) {
		ret = plugin_worker_request(EF800_PLUGIN_WORKER_STOP,
			NULL, 0U, NULL, 0U, NULL,
			EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
		if (ret != PM_OK) {
			result = ret;
			abort_worker = true;
		}
	}
	ret = plugin_worker_reap_and_unbind(abort_worker);
	if (ret != 0) {
		return result != 0 ? result : ret;
	}
	recovery_ledger_cleanup_mark(EF800_PLUGIN_RECOVERY_WORKER_STOPPED,
		true);
	ret = ef800_plugin_host_worker_domain_detach(&plugin_worker_thread,
		plugin_worker_generation);
	if (ret != 0) {
		return result != 0 ? result : ret;
	}
	if (ret == 0) {
		k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

		plugin_recovery_ledger.worker_mapped = false;
		plugin_recovery_ledger.generation = 0U;
		k_spin_unlock(&plugin_recovery_lock, key);
		recovery_ledger_clear_if_quiescent();
	}
	plugin_worker_generation = 0U;
	plugin_worker_transport = NULL;
	plugin_worker_workspace = NULL;
	plugin_worker_allowed_services = 0U;
	return result != 0 ? result : ret;
}

static int worker_entry_offset(const void *entry, uint32_t *offset)
{
	uintptr_t address = (uintptr_t)entry & ~(uintptr_t)1U;
	uintptr_t base = (uintptr_t)plugin_text_arena;

	if (entry == NULL || offset == NULL || address < base ||
		address - base >= plugin_text_capacity) {
		return -EINVAL;
	}
	*offset = (uint32_t)(address - base);
	return 0;
}

static int configure_worker_services(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	const struct ef800_workspace *workspace)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(active_registry, plugin_id);
	bool route_matches;
	k_spinlock_key_t key;

	if (entry == NULL || entry->state != EF800_PLUGIN_REGISTRY_READY) {
		return -ENODEV;
	}
	plugin_worker_allowed_services = entry->info.worker_services;
	plugin_worker_transport = transport;
	plugin_worker_workspace = workspace;
	key = k_spin_lock(&plugin_recovery_lock);
	route_matches = plugin_recovery_ledger.route_claimed &&
		plugin_recovery_ledger.plugin_id == plugin_id &&
		plugin_recovery_ledger.route_transport == transport;
	plugin_recovery_ledger.active = true;
	plugin_recovery_ledger.plugin_id = plugin_id;
	if (transport != NULL && !plugin_recovery_ledger.route_claimed) {
		plugin_recovery_ledger.route_transport = transport;
	}
	k_spin_unlock(&plugin_recovery_lock, key);
	if ((plugin_worker_allowed_services & EF800_PLUGIN_WORKER_SERVICE_PORT) != 0U &&
		(transport == NULL || transport->write_until == NULL ||
		 transport->read_until == NULL || !route_matches)) {
		return -ENODEV;
	}
	if ((plugin_worker_allowed_services &
		EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT) != 0U &&
		workspace == NULL) {
		return -ENODEV;
	}
	worker_service_placeholder = 1U;
	service_items[0] = (struct pm_service){
		.service_id = {{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
		.version = EF800_PLUGIN_WORKER_SERVICE_VERSION,
		.vtable = &worker_service_placeholder,
	};
	service_table.items = service_items;
	service_table.count = 1U;
	contract.services = service_items;
	contract.service_count = service_table.count;
	return 0;
}

static int execute_plugin_create_worker(void *context,
	pm_plugin_create_fn entry, const struct pm_service_table *services,
	void **instance)
{
	uint32_t offset;
	struct ef800_plugin_worker_mailbox *mailbox;
	int ret;

	ARG_UNUSED(context);
	ARG_UNUSED(services);
	if (entry == NULL || instance == NULL) {
		return PM_EINVAL;
	}
	if (!plugin_worker_running) {
		ret = plugin_worker_start();
		if (ret != 0) {
			return ret;
		}
	}
	ret = worker_entry_offset((const void *)entry, &offset);
	if (ret != 0) {
		(void)plugin_worker_stop(true);
		return PM_EINVAL;
	}
	mailbox = ef800_plugin_host_worker_mailbox();
	mailbox->plugin_instance_valid = 0U;
	ret = plugin_worker_request(EF800_PLUGIN_WORKER_CREATE, &offset,
		sizeof(offset), NULL, 0U, NULL,
		EF800_PLUGIN_DEADLINE_LIFECYCLE_CREATE_MS);
	if (ret == PM_OK && mailbox->plugin_instance_valid != 0U) {
		*instance = &plugin_manager_instance_token;
		return PM_OK;
	}
	if (!plugin_worker_poisoned) {
		(void)plugin_worker_stop(true);
	}
	return ret == PM_OK ? PM_EPROTO : ret;
}

static int execute_plugin_destroy_worker(void *context,
	pm_plugin_destroy_fn entry, void *instance)
{
	uint32_t offset;
	int ret;

	ARG_UNUSED(context);
	if (instance != &plugin_manager_instance_token || entry == NULL ||
		!plugin_worker_running) {
		return PM_EINVAL;
	}
	ret = worker_entry_offset((const void *)entry, &offset);
	if (ret == 0) {
		ret = plugin_worker_request(EF800_PLUGIN_WORKER_DESTROY, &offset,
			sizeof(offset), NULL, 0U, NULL,
			EF800_PLUGIN_DEADLINE_LIFECYCLE_DESTROY_MS);
	}
	if (ret == PM_OK) {
		ret = plugin_worker_stop(false);
	} else if (!plugin_worker_poisoned) {
		plugin_worker_poisoned = true;
		(void)plugin_worker_stop(true);
	}
	return ret;
}

static const struct pm_manager_instance_executor worker_executor = {
	.create = execute_plugin_create_worker,
	.destroy = execute_plugin_destroy_worker,
};

#endif

static int manager_create_instance(uint32_t plugin_id)
{
	int result;

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	result = pm_manager_create_instance_with_executor(manager, plugin_id,
		&worker_executor);
#elif CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION
	result = pm_manager_create_instance_with_executor(manager, plugin_id,
		&inline_executor);
#else
	ARG_UNUSED(plugin_id);
	result = PM_ENOTSUP;
#endif
	if (result == PM_OK) {
		recovery_ledger_manager_state(plugin_id, true, true);
	}
	return result;
}

static int manager_destroy_instance(uint32_t plugin_id)
{
	int result;

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	result = pm_manager_destroy_instance_with_executor(manager, plugin_id,
		&worker_executor);
#elif CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION
	result = pm_manager_destroy_instance_with_executor(manager, plugin_id,
		&inline_executor);
#else
	ARG_UNUSED(plugin_id);
	result = PM_ENOTSUP;
#endif
	if (result == PM_OK) {
		recovery_ledger_manager_state(plugin_id, true, false);
		recovery_ledger_cleanup_mark(
			EF800_PLUGIN_RECOVERY_INSTANCE_DESTROYED, true);
		recovery_ledger_clear_if_quiescent();
	}
	return result;
}

static int manager_unload_tracked(uint32_t plugin_id)
{
	bool worker_mapped;
	k_spinlock_key_t key;
	int result;

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (plugin_worker_reap_failed) {
		return PM_EBUSY;
	}
#endif
	key = k_spin_lock(&plugin_recovery_lock);
	worker_mapped = plugin_recovery_ledger.worker_mapped;
	k_spin_unlock(&plugin_recovery_lock, key);
	if (worker_mapped) {
		return PM_EBUSY;
	}
	result = pm_manager_unload(manager, plugin_id);

	if (result == PM_OK) {
		k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

		if (plugin_recovery_ledger.manager_plugin_id == plugin_id) {
			plugin_recovery_ledger.manager_loaded = false;
			plugin_recovery_ledger.instance_active = false;
			plugin_recovery_ledger.cleanup_in_progress = false;
		}
		k_spin_unlock(&plugin_recovery_lock, key);
		recovery_ledger_clear_if_quiescent();
	}
	return result;
}
static const struct pm_elf_profile elf_profile = {
	.max_sections = 32u,
	.max_image_size = EF800_PLUGIN_MAX_PACKAGE_SIZE,
	.max_ram_size = EF800_PLUGIN_DATA_SIZE,
	.text_address = 0u,
	.rodata_address = 0x2000u,
	.data_address = 0x20010000u,
#if CONFIG_BOARD_NATIVE_SIM
	.machine = PM_ELF_MACHINE_I386,
	.relocation_type = PM_ELF_R_I386_32,
#else
	.machine = PM_ELF_MACHINE_ARM,
	.relocation_type = PM_ELF_R_ARM_ABS32,
#endif
};

static const struct pm_plugin_interface interfaces[] = {
	{
		.interface_id = {{EF800_PLUGIN_MEASUREMENT_UUID_BYTES}},
		.version = 1u,
		.supported_service_ids = (const pm_uuid_t[]){
			{{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
			{{EF800_PLUGIN_WORKER_SERVICE_UUID_BYTES}},
		},
		.supported_service_count = 2u,
	},
	{
		.interface_id = {{EF800_PLUGIN_CALCULATION_UUID_BYTES}},
		.version = 1u,
	},
};

static struct pm_application_contract contract = {
	.application_id = {{EF800_PLUGIN_APPLICATION_UUID_BYTES}},
	.version = EF800_PLUGIN_CONTRACT_VERSION,
	.interfaces = interfaces,
	.interface_count = ARRAY_SIZE(interfaces),
	.services = service_items,
	.service_count = 0u,
};

static int validate_measurement_display(
	const struct ef800_measurement_display *display)
{
	if (display == NULL || display->struct_size < sizeof(*display) ||
		display->row_count > EF800_MEASUREMENT_SCREEN_ROWS ||
		display->selected_action >= 32u ||
		memchr(display->title, '\0', sizeof(display->title)) == NULL ||
		memchr(display->footer, '\0', sizeof(display->footer)) == NULL) {
		return -EINVAL;
	}
	for (uint32_t row = 0U; row < display->row_count; ++row) {
		if (memchr(display->rows[row], '\0', sizeof(display->rows[row])) == NULL) {
			return -EOVERFLOW;
		}
	}
	return 0;
}

static bool metadata_isolation_valid(
	const struct ef800_plugin_metadata *metadata, uint32_t plugin_kind)
{
#if CONFIG_BOARD_NATIVE_SIM
	const size_t active_text_capacity = EF800_PLUGIN_TEXT_SIZE;
	const size_t active_rodata_capacity = EF800_PLUGIN_RODATA_SIZE;
#else
	const size_t active_text_capacity = plugin_text_capacity;
	const size_t active_rodata_capacity = plugin_rodata_capacity;
#endif
	const uint32_t known_services =
		EF800_PLUGIN_WORKER_SERVICE_PORT |
		EF800_PLUGIN_WORKER_SERVICE_WORKSPACE_SNAPSHOT |
		EF800_PLUGIN_WORKER_SERVICE_CLOCK |
		EF800_PLUGIN_WORKER_SERVICE_LOG;
	uint32_t expected_target;

#if defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_BOARD_NATIVE_SIM
	expected_target = EF800_PLUGIN_TARGET_NATIVE_SIM;
#else
	expected_target = EF800_PLUGIN_TARGET_ARM_CORTEX_M;
#endif

	if (metadata->target_architecture != expected_target ||
		(metadata->required_worker_services & ~known_services) != 0U ||
		metadata->minimum_plugin_ram_size == 0U ||
		metadata->minimum_plugin_ram_size >
			EF800_PLUGIN_DATA_SIZE + EF800_PLUGIN_BSS_SIZE) {
		return false;
	}
	if (metadata->execution_model == EF800_PLUGIN_EXECUTION_LEGACY_DIRECT) {
#if CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION
		/* Explicitly trusted development/native_sim compatibility only. */
		return metadata->isolation_abi_version == 0U &&
			metadata->required_worker_services == 0U &&
			metadata->minimum_mailbox_size == 0U &&
			metadata->minimum_worker_stack_size == 0U &&
			metadata->text_alignment == 4U &&
			metadata->rodata_alignment == 4U &&
			metadata->data_alignment == 4U;
#else
		return false;
#endif
	}
	#if CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION
	/* The development executor is deliberately inline; it cannot honor the
	 * worker-only contract even when a package declares one. */
	if (metadata->execution_model == EF800_PLUGIN_EXECUTION_USER_WORKER) {
		return false;
	}
	#endif
	if (metadata->execution_model != EF800_PLUGIN_EXECUTION_USER_WORKER ||
		metadata->isolation_abi_version != EF800_PLUGIN_WORKER_ABI_VERSION ||
		metadata->minimum_mailbox_size !=
			EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE ||
		metadata->minimum_worker_stack_size == 0U ||
		metadata->minimum_worker_stack_size > EF800_PLUGIN_WORKER_STACK_SIZE ||
		metadata->text_alignment != EF800_PLUGIN_MPU_ALIGNMENT ||
		metadata->rodata_alignment != EF800_PLUGIN_MPU_ALIGNMENT ||
		metadata->data_alignment != EF800_PLUGIN_MPU_ALIGNMENT) {
		return false;
	}
	if (metadata->maximum_text_size == 0U ||
		metadata->maximum_text_size > active_text_capacity ||
		metadata->maximum_rodata_size == 0U ||
		metadata->maximum_rodata_size > active_rodata_capacity ||
		metadata->maximum_data_size > EF800_PLUGIN_DATA_SIZE ||
		metadata->maximum_bss_size > EF800_PLUGIN_BSS_SIZE ||
		metadata->maximum_shared_buffer_size > EF800_PLUGIN_RW_REGION_SIZE ||
		metadata->maximum_data_size + metadata->maximum_bss_size >
			metadata->minimum_plugin_ram_size) {
		return false;
	}
	if (plugin_kind == EF800_PLUGIN_KIND_MEASUREMENT &&
		(metadata->required_worker_services &
		 EF800_PLUGIN_WORKER_SERVICE_PORT) == 0U) {
		return false;
	}
	return true;
}

static uint8_t *image_bss_storage(const struct pm_elf_image *image)
{
	if (image == NULL) {
		return NULL;
	}
	for (size_t index = 0U; index < ARRAY_SIZE(plugin_image_slots); ++index) {
		struct plugin_image_slot *slot = &plugin_image_slots[index];

		if (slot->used && slot->text == image->text &&
			slot->rodata == image->rodata && slot->data == image->data) {
			return slot->bss;
		}
	}
	return NULL;
}

static int convert_entry(void *context, uint32_t address,
	enum pm_elf_entry_kind kind, void *function_out)
{
	const struct pm_elf_image *image = context;
	uintptr_t target;

#if CONFIG_BOARD_NATIVE_SIM
	target = (uintptr_t)image->text + (address - image->text_address);
#else
	target = (uintptr_t)image->text +
		((address & ~1u) - image->text_address) + (address & 1u);
#endif

	ARG_UNUSED(kind);
	*(void **)function_out = (void *)target;
	return PM_OK;
}

static int adapt_descriptor(const struct pm_wire_plugin_descriptor *wire,
	const struct pm_elf_image *image,
	const struct pm_package_manifest *manifest,
	const struct pm_plugin_descriptor **result, void *context)
{
	#if !CONFIG_BOARD_NATIVE_SIM
	struct pm_arm_entry_adapter arm_adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_entry,
		.context = (void *)image,
	};
	#endif
	#if CONFIG_BOARD_NATIVE_SIM
	struct pm_native_entry_adapter native_adapter = {
		.executable_base = image->text_address,
		.executable_size = image->text_size,
		.convert = convert_entry,
		.context = (void *)image,
	};
	#endif
	struct pm_descriptor_bind_config bind = {
#if CONFIG_BOARD_NATIVE_SIM
		.resolve = pm_native_entry_resolve,
		.context = &native_adapter,
#else
		.resolve = pm_arm_entry_resolve,
		.context = &arm_adapter,
#endif
	};
	struct pm_plugin_descriptor *descriptor_slot = NULL;
	for (size_t index = 0U; index < ARRAY_SIZE(descriptor_slots); ++index) {
		if (descriptor_slot_used[index] &&
			descriptor_slot_ids[index] == wire->plugin_id) {
			descriptor_slot = &descriptor_slots[index];
			break;
		}
	}
	if (descriptor_slot == NULL) {
		for (size_t index = 0U; index < ARRAY_SIZE(descriptor_slots); ++index) {
			if (!descriptor_slot_used[index]) {
				descriptor_slot_used[index] = true;
				descriptor_slot_ids[index] = wire->plugin_id;
				descriptor_slot = &descriptor_slots[index];
				break;
			}
		}
	}
	if (descriptor_slot == NULL) {
		return PM_ENOMEM;
	}
	int status;

	ARG_UNUSED(manifest);
	ARG_UNUSED(context);
#if CONFIG_BOARD_NATIVE_SIM
	status = pm_native_entry_adapter_validate(&native_adapter, image);
#else
	status = pm_arm_entry_adapter_validate(&arm_adapter, image);
#endif
	if (status != PM_OK) {
		LOG_WRN("Plugin entry adapter validation failed: %d text=%p size=%u",
			status, image == NULL ? NULL : image->text,
			(unsigned)(image == NULL ? 0U : image->text_size));
	}
	if (status == PM_OK) {
		status = pm_wire_descriptor_bind(wire, image, &bind, descriptor_slot);
		if (status != PM_OK) {
			LOG_WRN("Plugin wire descriptor bind failed: %d create=0x%08x destroy=0x%08x",
				status, wire->create_address, wire->destroy_address);
		}
	}
	if (status == PM_OK &&
		(descriptor_slot->extension == NULL ||
		 descriptor_slot->extension_size < sizeof(struct ef800_plugin_metadata))) {
		LOG_WRN("Plugin %u metadata extension missing/truncated: size=%u",
			(unsigned int)wire->plugin_id,
			(unsigned int)descriptor_slot->extension_size);
		status = PM_ENOTSUP;
	}
	if (status == PM_OK) {
		const struct ef800_plugin_metadata *metadata =
			(const struct ef800_plugin_metadata *)descriptor_slot->extension;

		if (memcmp(metadata->metadata_id,
			(uint8_t[]){EF800_PLUGIN_METADATA_UUID_BYTES}, 16u) != 0 ||
			metadata->version != EF800_PLUGIN_METADATA_VERSION ||
			metadata->size < sizeof(*metadata)) {
			status = PM_ENOTSUP;
		} else if (metadata->execution_model ==
			EF800_PLUGIN_EXECUTION_USER_WORKER) {
			uintptr_t text_alignment = metadata->text_alignment;
			uintptr_t rodata_alignment = metadata->rodata_alignment;
			uintptr_t data_alignment = metadata->data_alignment;
			uint8_t *bss_storage = image_bss_storage(image);

			if (!metadata_isolation_valid(metadata,
				pm_uuid_equal(&wire->interface_id,
					&(pm_uuid_t){{EF800_PLUGIN_MEASUREMENT_UUID_BYTES}}) ?
					EF800_PLUGIN_KIND_MEASUREMENT :
					EF800_PLUGIN_KIND_CALCULATION) ||
				image->text_size > metadata->maximum_text_size ||
				image->rodata_size > metadata->maximum_rodata_size ||
				image->data_size > metadata->maximum_data_size ||
				image->bss_size > metadata->maximum_bss_size ||
				(uintptr_t)image->text % text_alignment != 0U ||
				(uintptr_t)image->rodata % rodata_alignment != 0U ||
				(uintptr_t)image->data % data_alignment != 0U ||
				bss_storage == NULL ||
				(uintptr_t)bss_storage % data_alignment != 0U) {
				status = PM_ENOTSUP;
			}
		}
	}
	if (status == PM_OK) {
		*result = descriptor_slot;
	} else {
		for (size_t index = 0U; index < ARRAY_SIZE(descriptor_slots); ++index) {
			if (&descriptor_slots[index] == descriptor_slot) {
				descriptor_slot_used[index] = false;
				descriptor_slot_ids[index] = 0U;
				memset(descriptor_slot, 0, sizeof(*descriptor_slot));
				break;
			}
		}
	}
	return status;
}

static int allocate_image(void *context, size_t text_size, size_t rodata_size,
	size_t data_size, size_t bss_size, uint8_t **text, uint8_t **rodata,
	uint8_t **data, uint8_t **bss)
{
	struct plugin_image_slot *slot;
	size_t bss_offset;

	ARG_UNUSED(context);
	if (text == NULL || rodata == NULL || data == NULL || bss == NULL ||
		text_size > plugin_text_capacity ||
		rodata_size > plugin_rodata_capacity ||
		data_size > EF800_PLUGIN_DATA_SIZE || bss_size > EF800_PLUGIN_BSS_SIZE) {
		return PM_EOVERFLOW;
	}
	bss_offset = (data_size + EF800_PLUGIN_MPU_ALIGNMENT - 1U) &
		~((size_t)EF800_PLUGIN_MPU_ALIGNMENT - 1U);
	#if !CONFIG_BOARD_NATIVE_SIM
	if (bss_offset > EF800_PLUGIN_RW_REGION_SIZE ||
		bss_size > EF800_PLUGIN_RW_REGION_SIZE - bss_offset) {
		return PM_EOVERFLOW;
	}
	#else
	ARG_UNUSED(bss_offset);
	#endif
	for (size_t index = 0U; index < ARRAY_SIZE(plugin_image_slots); ++index) {
		slot = &plugin_image_slots[index];
		if (!slot->used) {
#if CONFIG_BOARD_NATIVE_SIM
			slot->text = mmap(NULL, EF800_PLUGIN_TEXT_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->rodata = mmap(NULL, EF800_PLUGIN_RODATA_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->data = mmap(NULL, EF800_PLUGIN_DATA_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			slot->bss = mmap(NULL, EF800_PLUGIN_BSS_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (slot->text == MAP_FAILED || slot->rodata == MAP_FAILED ||
				slot->data == MAP_FAILED || slot->bss == MAP_FAILED) {
				if (slot->text != MAP_FAILED) munmap(slot->text, EF800_PLUGIN_TEXT_SIZE);
				if (slot->rodata != MAP_FAILED) munmap(slot->rodata, EF800_PLUGIN_RODATA_SIZE);
				if (slot->data != MAP_FAILED) munmap(slot->data, EF800_PLUGIN_DATA_SIZE);
				if (slot->bss != MAP_FAILED) munmap(slot->bss, EF800_PLUGIN_BSS_SIZE);
				slot->text = slot->rodata = slot->data = slot->bss = NULL;
				return PM_ENOMEM;
			}
#else
			slot->text = plugin_text_arena;
			slot->rodata = plugin_rodata_arena;
			slot->data = plugin_rw_arena;
			slot->bss = plugin_rw_arena + bss_offset;
			memset(plugin_text_arena, 0, plugin_text_capacity);
			memset(plugin_rodata_arena, 0, plugin_rodata_capacity);
			memset(plugin_rw_arena, 0, sizeof(plugin_rw_arena));
#endif
			slot->used = true;
			*text = slot->text;
			*rodata = slot->rodata;
			*data = slot->data;
			*bss = slot->bss;
			return PM_OK;
		}
	}
	return PM_ENOMEM;
}

static void free_image(void *context, uint8_t *text, uint8_t *rodata,
	uint8_t *data, uint8_t *bss)
{
	ARG_UNUSED(context);
	for (size_t index = 0U; index < ARRAY_SIZE(plugin_image_slots); ++index) {
		struct plugin_image_slot *slot = &plugin_image_slots[index];
		if (slot->text == text && slot->rodata == rodata &&
			slot->data == data && slot->bss == bss) {
#if CONFIG_BOARD_NATIVE_SIM
			munmap(slot->text, EF800_PLUGIN_TEXT_SIZE);
			munmap(slot->rodata, EF800_PLUGIN_RODATA_SIZE);
			munmap(slot->data, EF800_PLUGIN_DATA_SIZE);
			munmap(slot->bss, EF800_PLUGIN_BSS_SIZE);
			slot->text = slot->rodata = slot->data = slot->bss = NULL;
#else
			memset(plugin_text_arena, 0, plugin_text_capacity);
			memset(plugin_rodata_arena, 0, plugin_rodata_capacity);
			memset(plugin_rw_arena, 0, sizeof(plugin_rw_arena));
			slot->data = slot->bss = NULL;
			slot->text = slot->rodata = NULL;
#endif
			recovery_ledger_cleanup_mark(
				EF800_PLUGIN_RECOVERY_MEMORY_ZEROED, true);
			slot->used = false;
			return;
		}
	}
}

static int resolve_plugin_import(void *context, const char *name,
	uint32_t *address)
{
	ARG_UNUSED(context);
	if (name == NULL || address == NULL ||
		strcmp(name, "ef800_plugin_worker_syscall") != 0) {
		return PM_ENOENT;
	}
	*address = (uint32_t)(uintptr_t)ef800_plugin_worker_gateway_entry;
	return PM_OK;
}

#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static int port_write(void *context, const uint8_t *data, uint32_t length)
{
	const struct ef800_measurement_transport *transport = context;
	return transport == NULL || transport->write == NULL ? PM_EINVAL :
		transport->write(transport->context, data, length);
}

static int port_read(void *context, uint8_t *data, uint32_t capacity,
	int32_t timeout_ms)
{
	const struct ef800_measurement_transport *transport = context;
	return transport == NULL || transport->read == NULL ? PM_EINVAL :
		transport->read(transport->context, data, timeout_ms);
}
#endif

static struct pm_manager_package_config package_config = {
	.elf_profile = &elf_profile,
#if CONFIG_BOARD_NATIVE_SIM
	.target_architecture = PM_PACKAGE_TARGET_NATIVE_SIM,
#else
	.target_architecture = PM_PACKAGE_TARGET_ARM_CORTEX_M,
#endif
	.adapt_wire_descriptor = adapt_descriptor,
	.resolve = resolve_plugin_import,
	.alloc_image = allocate_image,
	.free_image = free_image,
};

static int available_index(uint32_t plugin_id)
{
	struct pm_available_plugin available;

	for (size_t index = 0u; index < pm_manager_available_count(manager);
		++index) {
		if (pm_manager_available_get(manager, index, &available) == PM_OK &&
			available.plugin_id == plugin_id) {
			return (int)index;
		}
	}
	return -ENOENT;
}

static bool plugin_registry_ready(uint32_t plugin_id,
	enum ef800_plugin_kind expected_kind)
{
	const struct ef800_plugin_registry_entry *entry =
		ef800_plugin_registry_find(active_registry, plugin_id);

	return entry != NULL && entry->state == EF800_PLUGIN_REGISTRY_READY &&
		entry->info.kind == expected_kind;
}

static int activate_measurement(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	void **instance)
{
	int index;
	uint32_t loaded_id;
	int status;

	if (!ready || instance == NULL ||
		!plugin_registry_ready(plugin_id, EF800_PLUGIN_KIND_MEASUREMENT)) {
		return -ENODEV;
	}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	status = configure_worker_services(plugin_id, transport, NULL);
	if (status != 0) {
		return status;
	}
#else
	port_api.abi_version = EF800_PORT_API_VERSION;
	port_api.struct_size = sizeof(port_api);
	port_api.context = (void *)transport;
	port_api.write = port_write;
	port_api.read = port_read;
	service_items[0] = (struct pm_service){
		.service_id = {{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
		.version = 1u,
		.vtable = &port_api,
	};
	service_table.items = service_items;
	service_table.count = 1U;
	contract.services = service_items;
	contract.service_count = service_table.count;
#endif
	index = available_index(plugin_id);
	if (index < 0) {
		return index;
	}
#if CONFIG_BOARD_NATIVE_SIM
	/* Native discovery keeps the validated relocatable image resident.  This
	 * avoids a second FAT read/load cycle through the simulator boundary and
	 * lets activation create the already-registered descriptor instance. */
	status = manager_create_instance(plugin_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, plugin_id, instance);
		if (status == PM_OK) {
			return PM_OK;
		}
		(void)manager_destroy_instance(plugin_id);
	}
#endif
	LOG_INF("Loading plugin %u index %d config profile=%p adapt=%p alloc=%p manager=%p",
		(unsigned)plugin_id, index, package_config.elf_profile,
		package_config.adapt_wire_descriptor, package_config.alloc_image, manager);
	status = pm_manager_load_available(manager, (size_t)index,
		&package_config, &loaded_id);
	if (status != PM_OK) {
		LOG_WRN("Plugin %u load failed: %d", plugin_id, status);
		return status;
	}
	recovery_ledger_manager_state(loaded_id, true, false);
	status = manager_create_instance(loaded_id);
	if (status != PM_OK) {
		LOG_WRN("Plugin %u instance create failed: %d", plugin_id, status);
		(void)manager_unload_tracked(loaded_id);
		return status;
	}
	status = pm_manager_get_instance(manager, loaded_id, instance);
	if (status != PM_OK) {
		(void)manager_destroy_instance(loaded_id);
		(void)manager_unload_tracked(loaded_id);
	}
	return status;
}

static void deactivate_measurement(uint32_t plugin_id)
{
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (plugin_worker_poisoned) {
		/* The supervisor must abandon rather than re-enter a timed-out image. */
		return;
	}
	#endif
	(void)manager_destroy_instance(plugin_id);
	#if !CONFIG_BOARD_NATIVE_SIM
	(void)manager_unload_tracked(plugin_id);
	#endif
	recovery_ledger_clear_if_quiescent();
}

int ef800_plugin_host_measurement_session_open(
	struct ef800_measurement_plugin_session *session, uint32_t plugin_id,
	const struct ef800_measurement_transport *transport)
{
	struct ef800_measurement_instance *instance;
	const struct ef800_plugin_registry_entry *entry;
	int status;

	if (session == NULL || transport == NULL || session->state !=
		EF800_MEASUREMENT_SESSION_CLOSED || !ready) {
		return -EINVAL;
	}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (session->route_port_id == 0U || !session->route_claimed) {
		return -EPERM;
	}
#endif
	entry = ef800_plugin_registry_find(active_registry, plugin_id);
	if (entry == NULL || entry->state != EF800_PLUGIN_REGISTRY_READY ||
		entry->info.kind != EF800_PLUGIN_KIND_MEASUREMENT ||
		entry->info.measurement_actions == 0U) {
		return -ENODEV;
	}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	status = configure_worker_services(plugin_id, transport, NULL);
	if (status != 0) {
		return status;
	}
#else
	port_api.abi_version = EF800_PORT_API_VERSION;
	port_api.struct_size = sizeof(port_api);
	port_api.context = (void *)transport;
	port_api.write = port_write;
	port_api.read = port_read;
	service_items[0] = (struct pm_service){
		.service_id = {{EF800_PLUGIN_PORT_SERVICE_UUID_BYTES}},
		.version = 1u,
		.vtable = &port_api,
	};
	service_table.items = service_items;
	service_table.count = 1U;
	contract.services = service_items;
	contract.service_count = service_table.count;
#endif
	#if CONFIG_BOARD_NATIVE_SIM
	/* Native discovery keeps the validated image resident. Reuse that
	 * descriptor instead of loading a second copy into another bounded slot. */
	session->loaded_id = plugin_id;
	{
		const struct pm_plugin_descriptor *loaded_descriptor = NULL;
		if (pm_manager_get_descriptor(manager, plugin_id, &loaded_descriptor) == PM_OK &&
			loaded_descriptor != NULL) {
			LOG_INF("Native session descriptor plugin=%u create=%p destroy=%p services=%u",
				(unsigned)plugin_id, loaded_descriptor->create,
				loaded_descriptor->destroy,
				(unsigned)loaded_descriptor->required_service_count);
		}
	}
	LOG_INF("Native session: creating plugin %u instance", (unsigned)plugin_id);
	status = manager_create_instance(session->loaded_id);
	LOG_INF("Native session: create result %d", status);
	if (status != PM_OK) {
		return status;
	}
	#else
	status = pm_manager_load_available(manager, (size_t)available_index(plugin_id),
		&package_config, &session->loaded_id);
	if (status != PM_OK) {
		return status;
	}
	recovery_ledger_manager_state(session->loaded_id, true, false);
	status = manager_create_instance(session->loaded_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, session->loaded_id,
			(void **)&instance);
	}
	#endif
	#if CONFIG_BOARD_NATIVE_SIM
	LOG_INF("Native session: getting plugin %u instance", (unsigned)session->loaded_id);
	status = pm_manager_get_instance(manager, session->loaded_id,
		(void **)&instance);
	LOG_INF("Native session: get result %d instance=%p", status, instance);
	#endif
	if (status != PM_OK) {
		(void)manager_destroy_instance(session->loaded_id);
	#if !CONFIG_BOARD_NATIVE_SIM
		(void)manager_unload_tracked(session->loaded_id);
	#endif
		return status;
	}
	session->plugin_id = plugin_id;
	session->actions = entry->info.measurement_actions;
	session->instance = instance;
	session->transport = transport;
	session->state = EF800_MEASUREMENT_SESSION_ACTIVE;
	return 0;
}

int ef800_plugin_host_measurement_session_measure(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_data *measurement)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_measurement_result result = {.struct_size = sizeof(result)};
	size_t response_length = 0U;
	int status;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		(session->actions & EF800_MEASUREMENT_ACTION_MEASURE) == 0U ||
		measurement == NULL || session->instance != &plugin_manager_instance_token) {
		return -EINVAL;
	}
	status = plugin_worker_request(EF800_PLUGIN_WORKER_MEASURE, NULL, 0U,
		&result, sizeof(result), &response_length,
		EF800_PLUGIN_DEADLINE_MEASURE_MS);
	if (status != PM_OK || response_length != sizeof(result) ||
		result.struct_size < sizeof(result)) {
		LOG_WRN("Plugin worker measure rejected: status=%d response=%u expected=%u struct=%u",
			status, (unsigned int)response_length,
			(unsigned int)sizeof(result), (unsigned int)result.struct_size);
		return -EIO;
	}
	measurement->distance_mm = result.distance_mm;
	measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
	measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
	measurement->control = result.control;
	return 0;
#else
	struct ef800_measurement_instance *instance;
	struct ef800_measurement_result result = {.struct_size = sizeof(result)};
	int status;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		(session->actions & EF800_MEASUREMENT_ACTION_MEASURE) == 0U ||
		measurement == NULL) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL || instance->api->measure == NULL) {
		return -EPROTO;
	}
	if (instance->api->abi_version != EF800_MEASUREMENT_API_VERSION ||
		instance->api->struct_size < sizeof(struct ef800_measurement_api) ||
		instance->api->ui_abi_version != EF800_MEASUREMENT_UI_API_VERSION ||
		instance->api->ui_struct_size < sizeof(struct ef800_measurement_api) ||
		instance->api->render == NULL || instance->api->handle_key == NULL) {
		return -EPROTO;
	}
	status = instance->api->measure(instance, &result);
	if (status == PM_OK) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	return status == PM_OK ? 0 : -EIO;
#endif
}

int ef800_plugin_host_measurement_session_set_context(
	struct ef800_measurement_plugin_session *session,
	const struct ef800_measurement_context *context)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (session == NULL || context == NULL || session->state !=
		EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token ||
		context->struct_size < sizeof(*context)) {
		return -EINVAL;
	}
	return plugin_worker_request(EF800_PLUGIN_WORKER_SET_CONTEXT, context,
		sizeof(*context), NULL, 0U, NULL,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS) == PM_OK ? 0 : -EIO;
#else
	struct ef800_measurement_instance *instance;

	if (session == NULL || context == NULL || session->state !=
		EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->set_context == NULL ||
		context->struct_size < sizeof(*context)) {
		return -ENOTSUP;
	}
	return instance->api->set_context(instance, context) == PM_OK ? 0 : -EIO;
#endif
}

int ef800_plugin_host_measurement_session_input(
	struct ef800_measurement_plugin_session *session, uint32_t input)
{
	uint32_t action = input == EF800_MEASUREMENT_INPUT_UP ?
		EF800_MEASUREMENT_ACTION_UP : input == EF800_MEASUREMENT_INPUT_DOWN ?
		EF800_MEASUREMENT_ACTION_DOWN : input == EF800_MEASUREMENT_INPUT_ENTER ?
		EF800_MEASUREMENT_ACTION_ENTER : 0U;
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	int worker_status;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token || action == 0U) {
		return -ENOTSUP;
	}
	worker_status = plugin_worker_request(EF800_PLUGIN_WORKER_INPUT, &input,
		sizeof(input), NULL, 0U, NULL,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
	if (worker_status != PM_OK) {
		LOG_WRN("Plugin %u input callback failed: input=%u status=%d",
			(unsigned int)session->plugin_id, (unsigned int)input,
			worker_status);
	}
	return worker_status;
#else
	struct ef800_measurement_instance *instance;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		action == 0U) {
		return -ENOTSUP;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL || instance->api->handle_input == NULL) {
		return -ENOTSUP;
	}
	return instance->api->handle_input(instance, input) == PM_OK ? 0 : -EIO;
#endif
}

int ef800_plugin_host_measurement_session_key(
	struct ef800_measurement_plugin_session *session, uint8_t key,
	bool *consumed)
{
	struct ef800_measurement_key_event event = {
		.struct_size = sizeof(event),
		.code = EF800_MEASUREMENT_KEY_CHARACTER,
		.character = key,
	};
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	uint32_t worker_result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
	size_t response_length = 0U;
	int status;

	if (session == NULL || consumed == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token) {
		return -EINVAL;
	}
	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		event.code = EF800_MEASUREMENT_KEY_BACK;
	}
	status = plugin_worker_request(EF800_PLUGIN_WORKER_KEY, &event,
		sizeof(event), &worker_result, sizeof(worker_result),
		&response_length, EF800_PLUGIN_DEADLINE_MEASURE_MS);
	if (status != PM_OK || response_length != sizeof(worker_result)) {
		return -EIO;
	}
	*consumed = worker_result == EF800_MEASUREMENT_KEY_CONSUMED ||
		worker_result == EF800_MEASUREMENT_KEY_REQUEST_BACK;
	return worker_result == EF800_MEASUREMENT_KEY_ERROR ? -EIO :
		worker_result == EF800_MEASUREMENT_KEY_UNSUPPORTED ? -ENOTSUP : 0;
#else
	uint32_t result = EF800_MEASUREMENT_KEY_NOT_CONSUMED;
	struct ef800_measurement_instance *instance;

	if (session == NULL || consumed == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->handle_key == NULL) return -EPROTO;
	if (key == 0x1BU || key == 0x08U || key == 0x7FU) {
		event.code = EF800_MEASUREMENT_KEY_BACK;
	}
	if (instance->api->handle_key(instance, &event, &result) != PM_OK) {
		return -EIO;
	}
	*consumed = result == EF800_MEASUREMENT_KEY_CONSUMED ||
		result == EF800_MEASUREMENT_KEY_REQUEST_BACK;
	return result == EF800_MEASUREMENT_KEY_ERROR ? -EIO :
		result == EF800_MEASUREMENT_KEY_UNSUPPORTED ? -ENOTSUP : 0;
#endif
}

int ef800_plugin_host_measurement_session_render(
	struct ef800_measurement_plugin_session *session,
	struct ef800_measurement_display *display)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	size_t response_length = 0U;
	int status;

	if (session == NULL || display == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token) {
		return -EINVAL;
	}
	status = plugin_worker_request(EF800_PLUGIN_WORKER_RENDER, NULL, 0U,
		display, sizeof(*display), &response_length,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
	if (status != PM_OK || response_length != sizeof(*display)) {
		return -EIO;
	}
	return validate_measurement_display(display);
#else
	struct ef800_measurement_instance *instance;

	if (session == NULL || display == NULL ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance->api == NULL || instance->api->render == NULL ||
		instance->api->ui_abi_version != EF800_MEASUREMENT_UI_API_VERSION ||
		instance->api->ui_struct_size < sizeof(struct ef800_measurement_api)) {
		return -EPROTO;
	}
	return instance->api->render(instance, display) == PM_OK ?
		validate_measurement_display(display) : -EIO;
#endif
}

int ef800_plugin_host_measurement_session_workspace_record(
	struct ef800_measurement_plugin_session *session, char *buffer,
	size_t capacity, size_t *length)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	size_t response_length = 0U;
	int status;

	if (session == NULL || buffer == NULL || length == NULL || capacity == 0U ||
		capacity > EF800_PLUGIN_MAX_RESULT ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token) {
		return -EINVAL;
	}
	status = plugin_worker_request(EF800_PLUGIN_WORKER_GET_WORKSPACE_RECORD,
		NULL, 0U, buffer, capacity, &response_length,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
	if (status == PM_ENOENT) {
		return -ENODATA;
	}
	if (status != PM_OK || response_length >= capacity) {
		return status == PM_OK ? -EOVERFLOW : -EIO;
	}
	buffer[response_length] = '\0';
	*length = response_length;
	return 0;
#else
	struct ef800_measurement_instance *instance;
	uint32_t output_length = 0U;
	int status;

	if (session == NULL || buffer == NULL || length == NULL || capacity == 0U ||
		session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->get_workspace_record == NULL) {
		return -ENOTSUP;
	}
	status = instance->api->get_workspace_record(instance, buffer,
		(uint32_t)capacity, &output_length);
	if (status == PM_ENOENT) {
		return -ENODATA;
	}
	if (status != PM_OK || output_length >= capacity) {
		return status == PM_OK ? -EOVERFLOW : -EIO;
	}
	*length = output_length;
	return 0;
#endif
}

int ef800_plugin_host_measurement_session_workspace_result(
	struct ef800_measurement_plugin_session *session, int status)
{
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE ||
		session->instance != &plugin_manager_instance_token) {
		return -EINVAL;
	}
	int32_t workspace_status = status;
	return plugin_worker_request(EF800_PLUGIN_WORKER_WORKSPACE_RECORD_RESULT,
		&workspace_status, sizeof(workspace_status), NULL, 0U, NULL,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS) == PM_OK ? 0 : -EIO;
#else
	struct ef800_measurement_instance *instance;

	if (session == NULL || session->state != EF800_MEASUREMENT_SESSION_ACTIVE) {
		return -EINVAL;
	}
	instance = session->instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->workspace_record_result == NULL) {
		return -ENOTSUP;
	}
	return instance->api->workspace_record_result(instance, status) == PM_OK ?
		0 : -EIO;
#endif
}

void ef800_plugin_host_measurement_session_close(
	struct ef800_measurement_plugin_session *session)
{
	if (session == NULL || session->state == EF800_MEASUREMENT_SESSION_CLOSED) {
		return;
	}
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (plugin_worker_poisoned) {
		*session = (struct ef800_measurement_plugin_session){0};
		return;
	}
	#endif
	if (manager != NULL && session->loaded_id != 0U) {
		(void)manager_destroy_instance(session->loaded_id);
	#if !CONFIG_BOARD_NATIVE_SIM && CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		if (!plugin_worker_poisoned) {
			(void)manager_unload_tracked(session->loaded_id);
		}
	#elif !CONFIG_BOARD_NATIVE_SIM
		(void)manager_unload_tracked(session->loaded_id);
	#endif
	}
	*session = (struct ef800_measurement_plugin_session){0};
}

static int calculate_snapshot(uint32_t plugin_id,
	const struct ef800_workspace *workspace, const char *snapshot,
	size_t snapshot_length, char *output, size_t output_capacity,
	size_t *output_length)
{
	int index;
	uint32_t loaded_id;
	void *opaque = NULL;
#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_calculation_instance *instance;
#endif
	struct pm_manager_package_config calculation_config = package_config;
	int status;

	if (!ready || !plugin_registry_ready(plugin_id,
		EF800_PLUGIN_KIND_CALCULATION) || workspace == NULL ||
		snapshot == NULL || output == NULL || output_length == NULL ||
		snapshot_length == 0U ||
		snapshot_length > EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT ||
		output_capacity == 0U || output_capacity > EF800_PLUGIN_MAX_RESULT) {
		return -EINVAL;
	}
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	status = configure_worker_services(plugin_id, NULL, workspace);
	if (status != 0) {
		return status;
	}
#endif
	index = available_index(plugin_id);
	if (index < 0) {
		return index;
	}
	status = pm_manager_load_available(manager, (size_t)index,
		&calculation_config, &loaded_id);
	if (status != PM_OK) {
		return -EIO;
	}
	recovery_ledger_manager_state(loaded_id, true, false);
	status = manager_create_instance(loaded_id);
	if (status == PM_OK) {
		status = pm_manager_get_instance(manager, loaded_id, &opaque);
	}
	if (status == PM_OK) {
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
		status = plugin_worker_request(EF800_PLUGIN_WORKER_CALCULATE,
			snapshot, snapshot_length, output, output_capacity,
			output_length, EF800_PLUGIN_DEADLINE_CALCULATION_MS);
#else
		instance = opaque;
		if (instance == NULL || instance->api == NULL ||
			instance->api->transform == NULL) {
			status = PM_EPROTO;
		} else {
			status = instance->api->transform(instance, snapshot,
				snapshot_length, output, output_capacity, output_length);
		}
#endif
	}
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	if (!plugin_worker_poisoned) {
	#endif
		(void)manager_destroy_instance(loaded_id);
		(void)manager_unload_tracked(loaded_id);
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	}
	#endif
	return status == PM_OK ? 0 : -EIO;
}

int ef800_plugin_host_calculate(uint32_t plugin_id,
	const struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length)
{
	size_t snapshot_length = 0U;
	int status;

	if (workspace == NULL || snapshot == NULL || output == NULL ||
		output_length == NULL || snapshot_capacity == 0U ||
		snapshot_capacity > EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT) {
		return -EINVAL;
	}
	status = ef800_workspace_snapshot(workspace, snapshot, snapshot_capacity,
		&snapshot_length);
	if (status != 0) {
		return status;
	}
	return calculate_snapshot(plugin_id, workspace, snapshot, snapshot_length,
		output, output_capacity, output_length);
}

int ef800_plugin_host_calculate_confirmed(uint32_t plugin_id,
	struct ef800_workspace *workspace, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length)
{
	int ret = ef800_plugin_host_calculate(plugin_id, workspace, snapshot,
		snapshot_capacity, output, output_capacity, output_length);

	if (ret != 0) {
		return ret;
	}
	if (*output_length == 0U || *output_length > output_capacity) {
		return -EFBIG;
	}
	/* The host performs the only Workspace mutation after explicit confirmation. */
	return ef800_workspace_append(workspace, output,
		*output_length);
}

static int host_lock(void *context)
{
	ARG_UNUSED(context);
	return k_mutex_lock(&plugin_host_mutex, K_FOREVER) == 0 ? PM_OK : PM_EBUSY;
}

static void host_unlock(void *context)
{
	ARG_UNUSED(context);
	(void)k_mutex_unlock(&plugin_host_mutex);
}

static void register_available_plugins(void)
{
	struct pm_available_plugin available;

	for (size_t index = 0u; index < pm_manager_available_count(manager);
		++index) {
		struct ef800_plugin_descriptor_info info = {
			.struct_size = sizeof(info),
			.interface_version = 1u,
			.kind = EF800_PLUGIN_KIND_CALCULATION,
			.menu_category = EF800_PLUGIN_MENU_CALCULATION,
		};

		if (pm_manager_available_get(manager, index, &available) != PM_OK) {
			continue;
		}
		info.plugin_id = available.plugin_id;
		info.interface_version = available.interface_version;
		strncpy(info.name, available.name == NULL ? "plugin" : available.name,
			sizeof(info.name) - 1u);
		strncpy(info.version, available.version == NULL ? "unknown" :
			available.version, sizeof(info.version) - 1u);
		if (pm_uuid_equal(&available.interface_id,
			&(pm_uuid_t){{EF800_PLUGIN_MEASUREMENT_UUID_BYTES}})) {
			info.kind = EF800_PLUGIN_KIND_MEASUREMENT;
			info.menu_category = EF800_PLUGIN_MENU_MEASUREMENT;
			info.required_hardware = EF800_PLUGIN_HARDWARE_SERIAL;
			info.measurement_actions = 0U;
		} else if (pm_uuid_equal(&available.interface_id,
			&(pm_uuid_t){{EF800_PLUGIN_CALCULATION_UUID_BYTES}})) {
			info.kind = EF800_PLUGIN_KIND_CALCULATION;
			info.menu_category = EF800_PLUGIN_MENU_CALCULATION;
		}
		if (ef800_plugin_registry_register_external(active_registry, &info) != 0) {
			LOG_WRN("Plugin %u rejected from registry", info.plugin_id);
		}
	}
}

static void inspect_available_metadata(void)
{
	for (size_t index = 0u; index < pm_manager_available_count(manager); ++index) {
		uint32_t plugin_id;
		uint32_t loaded_id;
		struct pm_available_plugin available;
		const struct pm_plugin_descriptor *descriptor = NULL;
		bool compatible = false;
		int status;

		if (pm_manager_available_get(manager, index, &available) != PM_OK) {
			continue;
		}
		plugin_id = available.plugin_id;
		status = pm_manager_load_available(manager, index, &package_config,
			&loaded_id);
		if (status != PM_OK) {
			(void)ef800_plugin_registry_record_diagnostic(active_registry,
				plugin_id, EF800_PLUGIN_REGISTRY_INCOMPATIBLE);
			continue;
		}
		if (loaded_id != plugin_id) {
		#if !CONFIG_BOARD_NATIVE_SIM
			(void)manager_unload_tracked(loaded_id);
		#endif
			(void)ef800_plugin_registry_record_diagnostic(active_registry,
				plugin_id, EF800_PLUGIN_REGISTRY_INCOMPATIBLE);
			continue;
		}
		int descriptor_status = pm_manager_get_descriptor(manager, plugin_id,
			&descriptor);
		if (descriptor_status == PM_OK &&
			descriptor != NULL && descriptor->extension != NULL &&
			descriptor->extension_size >=
			sizeof(struct ef800_plugin_metadata)) {
			const struct ef800_plugin_metadata *metadata =
				(const struct ef800_plugin_metadata *)descriptor->extension;
			if (memcmp(metadata->metadata_id,
				(uint8_t[]){EF800_PLUGIN_METADATA_UUID_BYTES}, 16u) == 0 &&
				metadata->version == EF800_PLUGIN_METADATA_VERSION &&
				metadata->size >= sizeof(*metadata)) {
				struct ef800_plugin_registry_entry *entry =
					(struct ef800_plugin_registry_entry *)
					ef800_plugin_registry_find(active_registry, plugin_id);
				uint32_t plugin_kind = entry == NULL ? 0U : entry->info.kind;

				if (metadata_isolation_valid(metadata, plugin_kind) &&
					(plugin_kind != EF800_PLUGIN_KIND_MEASUREMENT ||
					 (metadata->measurement_ui_version ==
						EF800_MEASUREMENT_UI_API_VERSION &&
					  metadata->measurement_ui_size >=
						sizeof(struct ef800_measurement_api)))) {
					compatible = true;
				(void)ef800_plugin_registry_set_hardware(active_registry,
					plugin_id, metadata->required_hardware);
				if (entry != NULL) {
					entry->info.worker_services =
						metadata->required_worker_services;
				}
				if (entry != NULL &&
					plugin_kind == EF800_PLUGIN_KIND_MEASUREMENT) {
					LOG_INF("Plugin %u metadata serial: baud=%u data=%u parity=%u stop=%u flow=%u",
						(unsigned int)plugin_id,
						(unsigned int)metadata->serial_baudrate,
						(unsigned int)metadata->serial_data_bits,
						(unsigned int)metadata->serial_parity,
						(unsigned int)metadata->serial_stop_bits,
						(unsigned int)metadata->serial_flow_control);
					entry->info.measurement_actions =
							metadata->measurement_actions;
						entry->info.serial_baudrate = metadata->serial_baudrate;
						entry->info.serial_data_bits = metadata->serial_data_bits;
						entry->info.serial_parity = metadata->serial_parity;
						entry->info.serial_stop_bits = metadata->serial_stop_bits;
						entry->info.serial_flow_control = metadata->serial_flow_control;
					}
				}
				if (!compatible) {
					LOG_WRN("Plugin %u metadata rejected: version=%u size=%u model=%u target=%u text=%u rodata=%u data=%u bss=%u",
						(unsigned int)plugin_id,
						(unsigned int)metadata->version,
						(unsigned int)metadata->size,
						(unsigned int)metadata->execution_model,
						(unsigned int)metadata->target_architecture,
						(unsigned int)metadata->maximum_text_size,
						(unsigned int)metadata->maximum_rodata_size,
						(unsigned int)metadata->maximum_data_size,
						(unsigned int)metadata->maximum_bss_size);
				}
			}
			else {
				LOG_WRN("Plugin %u has invalid metadata identity/size: extension=%u version=%u size=%u",
					(unsigned int)plugin_id,
					(unsigned int)descriptor->extension_size,
					(unsigned int)metadata->version,
					(unsigned int)metadata->size);
			}
		} else {
			LOG_WRN("Plugin %u descriptor metadata unavailable: status=%d descriptor=%p extension=%p size=%u",
				(unsigned int)plugin_id, descriptor_status, (void *)descriptor,
				descriptor == NULL ? NULL : descriptor->extension,
				(unsigned int)(descriptor == NULL ? 0U : descriptor->extension_size));
		}
		if (!compatible) {
			(void)ef800_plugin_registry_record_diagnostic(active_registry,
				available.plugin_id, EF800_PLUGIN_REGISTRY_INCOMPATIBLE);
		}
		#if !CONFIG_BOARD_NATIVE_SIM
		(void)manager_unload_tracked(plugin_id);
		#endif
	}
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void plugin_async_execute(
	const struct ef800_plugin_async_request *request,
	struct ef800_plugin_async_result *result)
{
	result->operation = request->operation;
	result->failure_class = EF800_PLUGIN_FAILURE_NONE;
	result->worker_generation = 0U;
	result->fault_reason = 0U;
	result->fault_pc = 0U;
	result->plugin_id = request->plugin_id != 0U ? request->plugin_id :
		request->session == NULL ? 0U : request->session->plugin_id;
	if (result->plugin_id != 0U) {
		enum ef800_plugin_registry_state state =
			ef800_plugin_registry_state_for(active_registry,
				result->plugin_id);

		if (state == EF800_PLUGIN_REGISTRY_FAULTED) {
			result->status = -EHOSTDOWN;
			result->failure_class = EF800_PLUGIN_FAILURE_FAULT;
			return;
		}
		if (state == EF800_PLUGIN_REGISTRY_TIMED_OUT) {
			result->status = -ETIMEDOUT;
			result->failure_class = EF800_PLUGIN_FAILURE_TIMEOUT;
			return;
		}
	}
	if (request->route_port_id != 0U) {
		recovery_ledger_set_route(request->plugin_id,
			request->route_port_id, request->route_connection,
			request->route_claimed, request->transport);
	}
	switch (request->operation) {
	case EF800_PLUGIN_ASYNC_SESSION_OPEN:
		request->session->route_port_id = request->route_port_id;
		request->session->route_connection = request->route_connection;
		request->session->route_claimed = request->route_claimed;
		result->status = ef800_plugin_host_measurement_session_open(
			request->session, request->plugin_id, request->transport);
		if (result->status == 0) {
			result->status = ef800_plugin_host_measurement_session_set_context(
				request->session, &request->context);
			if (result->status != 0) {
				ef800_plugin_host_measurement_session_close(request->session);
			}
		}
		break;
	case EF800_PLUGIN_ASYNC_SESSION_KEY:
		result->status = ef800_plugin_host_measurement_session_key(
			request->session, request->key, &result->value.key.consumed);
		break;
	case EF800_PLUGIN_ASYNC_SESSION_INPUT:
		result->status = ef800_plugin_host_measurement_session_input(
			request->session, request->input);
		break;
	case EF800_PLUGIN_ASYNC_SESSION_RENDER:
		result->status = ef800_plugin_host_measurement_session_render(
			request->session, &result->value.display);
		break;
	case EF800_PLUGIN_ASYNC_SESSION_MEASURE:
		result->status = ef800_plugin_host_measurement_session_measure(
			request->session, &result->value.measurement);
		break;
	case EF800_PLUGIN_ASYNC_WORKSPACE_RECORD:
		result->status = ef800_plugin_host_measurement_session_workspace_record(
			request->session, result->value.workspace_record.data,
			sizeof(result->value.workspace_record.data),
			&result->value.workspace_record.length);
		break;
	case EF800_PLUGIN_ASYNC_WORKSPACE_RESULT:
		result->status = ef800_plugin_host_measurement_session_workspace_result(
			request->session, request->workspace_status);
		break;
	case EF800_PLUGIN_ASYNC_SESSION_CLOSE:
		ef800_plugin_host_measurement_session_close(request->session);
		result->status = 0;
		break;
	case EF800_PLUGIN_ASYNC_MEASURE:
		result->status = ef800_plugin_host_measure(request->plugin_id,
			request->transport, &result->value.measurement);
		break;
	case EF800_PLUGIN_ASYNC_INPUT:
		result->status = ef800_plugin_host_input(request->plugin_id,
			request->transport, request->input);
		break;
	case EF800_PLUGIN_ASYNC_CALCULATE: {
		size_t output_length = 0U;

		if (request->workspace == NULL || request->snapshot == NULL ||
			request->snapshot_length == 0U ||
			request->snapshot_length > EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT ||
			request->output_capacity == 0U ||
			request->output_capacity >
				EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE) {
			result->status = -EINVAL;
			break;
		}
		result->status = calculate_snapshot(request->plugin_id,
			request->workspace, request->snapshot, request->snapshot_length,
			plugin_async_calculation_output, request->output_capacity,
			&output_length);
		if (result->status == 0) {
			result->value.calculation.data = plugin_async_calculation_output;
			result->value.calculation.length = output_length;
		}
		break;
	}
	default:
		result->status = -ENOTSUP;
		break;
	}
	if (result->status != 0 && result->status != -ENODATA &&
		!plugin_worker_poisoned && request->session != NULL &&
		request->operation != EF800_PLUGIN_ASYNC_SESSION_CLOSE &&
		request->session->state == EF800_MEASUREMENT_SESSION_ACTIVE) {
		/* A callback returned an error but did not time out. Close it normally
		 * on the worker before asking the UI to return to its launch point. */
		ef800_plugin_host_measurement_session_close(request->session);
	}
	if (result->status != 0 && result->status != -ENODATA) {
		result->failure_class = EF800_PLUGIN_FAILURE_CALLBACK;
	}
	if (plugin_worker_poisoned) {
		bool worker_faulted = plugin_worker_faulted;
		if (worker_faulted) {
			result->worker_generation = plugin_worker_fault_generation;
			result->fault_reason = plugin_worker_fault_reason;
			result->fault_pc = plugin_worker_fault_pc;
			LOG_WRN("Plugin worker fault: generation=%u reason=%u pc=0x%08x",
				(unsigned)plugin_worker_fault_generation,
				(unsigned)plugin_worker_fault_reason,
				(unsigned)plugin_worker_fault_pc);
		}
		int recovery_status = plugin_worker_recover_failure(request, result);

		result->recovery_required = true;
		result->recovery_status = recovery_status;
		if (recovery_status != 0) {
			result->failure_class = EF800_PLUGIN_FAILURE_RECOVERY;
		} else if (worker_faulted) {
			result->failure_class = EF800_PLUGIN_FAILURE_FAULT;
		} else if (result->status == -ETIMEDOUT) {
			result->failure_class = EF800_PLUGIN_FAILURE_TIMEOUT;
		} else {
			result->failure_class = EF800_PLUGIN_FAILURE_CALLBACK;
		}
		plugin_worker_faulted = false;
	}
}

static void plugin_supervisor_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		struct ef800_plugin_async_request request;
		struct ef800_plugin_async_result result = {0};
		uint32_t token;

		(void)k_sem_take(&plugin_async_wakeup, K_FOREVER);
		if (k_mutex_lock(&plugin_async_mutex, K_FOREVER) != 0) {
			continue;
		}
		if (!plugin_async_queued) {
			(void)k_mutex_unlock(&plugin_async_mutex);
			continue;
		}
		request = plugin_async_request;
		token = plugin_async_next_token;
		plugin_async_queued = false;
		(void)k_mutex_unlock(&plugin_async_mutex);

		result.token = token;
		result.generation = token;
		plugin_async_execute(&request, &result);
		if (result.status == 0) {
			size_t output_capacity = 0U;

			switch (result.operation) {
			case EF800_PLUGIN_ASYNC_SESSION_KEY:
				output_capacity = sizeof(result.value.key);
				break;
			case EF800_PLUGIN_ASYNC_SESSION_RENDER:
				output_capacity = sizeof(result.value.display);
				break;
			case EF800_PLUGIN_ASYNC_SESSION_MEASURE:
			case EF800_PLUGIN_ASYNC_MEASURE:
				output_capacity = sizeof(result.value.measurement);
				break;
			case EF800_PLUGIN_ASYNC_WORKSPACE_RECORD:
				output_capacity = result.value.workspace_record.length;
				break;
			case EF800_PLUGIN_ASYNC_CALCULATE:
				output_capacity = result.value.calculation.length;
				break;
			default:
				break;
			}
			recovery_ledger_output_pending(output_capacity);
		}

		if (k_mutex_lock(&plugin_async_mutex, K_FOREVER) == 0) {
			/* A token check prevents a late completion from replacing newer work. */
			if (plugin_async_busy && token == plugin_async_next_token) {
				plugin_async_result = result;
				plugin_async_complete = true;
			}
			(void)k_mutex_unlock(&plugin_async_mutex);
		}
	}
}

static int plugin_supervisor_start(void)
{
	if (plugin_supervisor_started) {
		return 0;
	}
	if (k_thread_create(&plugin_supervisor_thread, plugin_supervisor_stack,
		K_THREAD_STACK_SIZEOF(plugin_supervisor_stack),
		plugin_supervisor_entry, NULL, NULL, NULL, 4, 0, K_NO_WAIT) == NULL) {
		return -ENOMEM;
	}
	(void)k_thread_name_set(&plugin_supervisor_thread,
		"plugin_supervisor");
	plugin_supervisor_started = true;
	return 0;
}

static void plugin_worker_discard_outputs(
	struct ef800_plugin_async_result *result)
{
	memset(plugin_async_calculation_output, 0,
		 EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE);
	memset(&result->value, 0, sizeof(result->value));
	recovery_ledger_output_pending(0U);
	recovery_ledger_cleanup_mark(
		EF800_PLUGIN_RECOVERY_OUTPUT_DISCARDED, true);
}

static int plugin_worker_recover_failure(
	const struct ef800_plugin_async_request *request,
	struct ef800_plugin_async_result *result)
{
	struct ef800_plugin_recovery_ledger ledger;
	int failure_status;
	int ret;

	ret = plugin_worker_reap_and_unbind(true);
	if (ret != 0) {
		LOG_ERR("Plugin recovery worker reap failed: %d", ret);
		plugin_worker_discard_outputs(result);
		return ret;
	}
	if (ef800_plugin_host_recovery_snapshot(&ledger) != 0) {
		plugin_worker_discard_outputs(result);
		return -EIO;
	}
	failure_status = ledger.last_status;
	LOG_WRN("Plugin recovery ledger: plugin=%u mapped=%u loaded=%u instance=%u service=%u",
		(unsigned)ledger.manager_plugin_id, ledger.worker_mapped,
		ledger.manager_loaded, ledger.instance_active, ledger.service_pending);
	if (ledger.service_pending) {
		plugin_worker_discard_outputs(result);
		return -EBUSY;
	}
	if (ledger.route_claimed) {
		if (route_recovery_handler == NULL) {
			plugin_worker_discard_outputs(result);
			return -ENOTSUP;
		}
		ret = route_recovery_handler(route_recovery_context, &ledger);
		if (ret != 0) {
			plugin_worker_discard_outputs(result);
			return ret;
		}
		ef800_plugin_host_recovery_route_released(ledger.route_port_id,
			ledger.route_connection);
	}
	plugin_worker_discard_outputs(result);

	if (ledger.instance_active) {
		ret = pm_manager_abandon_instance(manager, ledger.manager_plugin_id);
		if (ret != PM_OK) {
			LOG_ERR("Plugin recovery instance abandon failed: %d", ret);
			return ret;
		}
		recovery_ledger_manager_state(ledger.manager_plugin_id, true, false);
		recovery_ledger_cleanup_mark(
			EF800_PLUGIN_RECOVERY_INSTANCE_ABANDONED, true);
	}

	if (ledger.worker_mapped) {
		ret = ef800_plugin_host_worker_domain_detach(&plugin_worker_thread,
			ledger.generation);
		if (ret != 0) {
			LOG_ERR("Plugin recovery domain detach failed: %d", ret);
			return ret;
		}
		{
			k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);
			bool cleared = ef800_plugin_recovery_domain_detached(
				&plugin_recovery_ledger.generation,
				&plugin_recovery_ledger.worker_mapped, ledger.generation);

			k_spin_unlock(&plugin_recovery_lock, key);
			if (!cleared) {
				LOG_ERR("Plugin recovery ledger detach generation mismatch");
				return -ESTALE;
			}
		}
	}
	if (ledger.manager_loaded) {
		ret = manager_unload_tracked(ledger.manager_plugin_id);
		if (ret != PM_OK) {
			LOG_ERR("Plugin recovery unload failed: %d", ret);
			return ret;
		}
	}
	plugin_worker_generation = 0U;
	plugin_worker_transport = NULL;
	plugin_worker_workspace = NULL;
	plugin_worker_allowed_services = 0U;
	plugin_worker_poisoned = false;
	plugin_worker_reap_failed = false;
	if (request->session != NULL) {
		*request->session = (struct ef800_measurement_plugin_session){0};
	}
	result->recovery_required = true;
	result->recovery_status = 0;
	if (failure_status != 0) {
		result->status = failure_status;
	}
	recovery_ledger_cleanup_mark(
		EF800_PLUGIN_RECOVERY_OUTPUT_DISCARDED, false);
	recovery_ledger_clear_if_quiescent();
	return 0;
}

int ef800_plugin_host_async_submit(
	const struct ef800_plugin_async_request *request, uint32_t *token)
{
	int ret;

	if (request == NULL || token == NULL || !plugin_supervisor_started) {
		return -EINVAL;
	}
	if (request->session == NULL &&
		request->operation != EF800_PLUGIN_ASYNC_MEASURE &&
		request->operation != EF800_PLUGIN_ASYNC_INPUT &&
		request->operation != EF800_PLUGIN_ASYNC_CALCULATE) {
		return -EINVAL;
	}
	if (request->operation < EF800_PLUGIN_ASYNC_SESSION_OPEN ||
		request->operation > EF800_PLUGIN_ASYNC_CALCULATE) {
		return -EINVAL;
	}
	if ((request->operation == EF800_PLUGIN_ASYNC_SESSION_OPEN ||
		request->operation == EF800_PLUGIN_ASYNC_MEASURE ||
		request->operation == EF800_PLUGIN_ASYNC_INPUT) &&
		(request->route_port_id == 0U || !request->route_claimed ||
		request->plugin_id == 0U || request->transport == NULL)) {
		return -EPERM;
	}
	if (request->operation == EF800_PLUGIN_ASYNC_CALCULATE &&
		(request->plugin_id == 0U || request->workspace == NULL ||
		request->snapshot == NULL || request->snapshot_length == 0U ||
		request->snapshot_length > EF800_PLUGIN_MAX_WORKSPACE_SNAPSHOT ||
		request->output_capacity == 0U ||
		request->output_capacity > EF800_PLUGIN_MAX_RESULT)) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&plugin_async_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	if (plugin_async_busy) {
		ret = -EBUSY;
	} else if (plugin_async_next_token == UINT32_MAX) {
		ret = -EOVERFLOW;
	} else {
		++plugin_async_next_token;
		plugin_async_request = *request;
		plugin_async_busy = true;
		plugin_async_queued = true;
		plugin_async_complete = false;
		*token = plugin_async_next_token;
		ret = 0;
	}
	(void)k_mutex_unlock(&plugin_async_mutex);
	if (ret == 0) {
		k_sem_give(&plugin_async_wakeup);
	}
	return ret;
}

int ef800_plugin_host_async_poll(uint32_t token,
	struct ef800_plugin_async_result *result)
{
	int ret;

	if (token == 0U || result == NULL) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&plugin_async_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	if (!plugin_async_busy || token != plugin_async_next_token) {
		ret = -ESTALE;
	} else if (!plugin_async_complete) {
		ret = -EAGAIN;
	} else {
		*result = plugin_async_result;
		if (result->token != token || result->generation != token) {
			ret = -ESTALE;
		} else if (result->operation == EF800_PLUGIN_ASYNC_CALCULATE &&
			result->status == 0) {
			/* Keep the serialized slot reserved until the host applies or
			 * discards the copied calculation result. */
			ret = 0;
		} else {
			plugin_async_busy = false;
			plugin_async_complete = false;
			recovery_ledger_output_pending(0U);
			memset(plugin_async_calculation_output, 0,
				 EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE);
			ret = 0;
		}
	}
	(void)k_mutex_unlock(&plugin_async_mutex);
	return ret;
}

int ef800_plugin_host_async_release(uint32_t token)
{
	int ret = k_mutex_lock(&plugin_async_mutex, K_FOREVER);

	if (ret != 0) {
		return ret;
	}
	if (!plugin_async_busy || !plugin_async_complete ||
		token != plugin_async_next_token ||
		plugin_async_result.operation != EF800_PLUGIN_ASYNC_CALCULATE ||
		plugin_async_result.token != token) {
		ret = -ESTALE;
	} else {
		memset(plugin_async_calculation_output, 0,
			 EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE);
		plugin_async_result.value.calculation.data = NULL;
		plugin_async_result.value.calculation.length = 0U;
		plugin_async_busy = false;
		plugin_async_complete = false;
		recovery_ledger_output_pending(0U);
		ret = 0;
	}
	(void)k_mutex_unlock(&plugin_async_mutex);
	return ret;
}
#endif

int ef800_plugin_host_recovery_snapshot(
	struct ef800_plugin_recovery_ledger *snapshot)
{
	k_spinlock_key_t key;

	if (snapshot == NULL) {
		return -EINVAL;
	}
	key = k_spin_lock(&plugin_recovery_lock);
	*snapshot = plugin_recovery_ledger;
	k_spin_unlock(&plugin_recovery_lock, key);
	return 0;
}

int ef800_plugin_host_set_route_recovery_handler(
	ef800_plugin_route_recovery_fn handler, void *context)
{
	if (handler == NULL || route_recovery_handler != NULL) {
		return -EINVAL;
	}
	route_recovery_handler = handler;
	route_recovery_context = context;
	return 0;
}

void ef800_plugin_host_recovery_route_released(uint8_t port_id,
	uint32_t connection)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	if (plugin_recovery_ledger.route_port_id == port_id &&
		plugin_recovery_ledger.route_connection == connection) {
		plugin_recovery_ledger.route_claimed = false;
		plugin_recovery_ledger.cleanup_progress |=
			EF800_PLUGIN_RECOVERY_ROUTE_RELEASED;
	}
	k_spin_unlock(&plugin_recovery_lock, key);
	recovery_ledger_clear_if_quiescent();
}

void ef800_plugin_host_recovery_service_begin(uint32_t generation,
	uint32_t operation)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	if (plugin_recovery_ledger.active &&
		plugin_recovery_ledger.generation == generation) {
		plugin_recovery_ledger.service_pending = true;
		plugin_recovery_ledger.pending_service_operation = operation;
	}
	k_spin_unlock(&plugin_recovery_lock, key);
}

void ef800_plugin_host_recovery_service_end(uint32_t generation,
	uint32_t operation)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	if (plugin_recovery_ledger.generation == generation &&
		plugin_recovery_ledger.pending_service_operation == operation) {
		plugin_recovery_ledger.service_pending = false;
		plugin_recovery_ledger.pending_service_operation = 0U;
	}
	k_spin_unlock(&plugin_recovery_lock, key);
}

#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
static void recovery_ledger_set_route(uint32_t plugin_id, uint8_t port_id,
	uint32_t connection, bool claimed,
	const struct ef800_measurement_transport *transport)
{
	k_spinlock_key_t key = k_spin_lock(&plugin_recovery_lock);

	plugin_recovery_ledger.active = true;
	plugin_recovery_ledger.plugin_id = plugin_id;
	plugin_recovery_ledger.route_port_id = port_id;
	plugin_recovery_ledger.route_connection = connection;
	plugin_recovery_ledger.route_claimed = claimed;
	plugin_recovery_ledger.route_transport = transport;
	k_spin_unlock(&plugin_recovery_lock, key);
}
#endif

int ef800_plugin_host_init(struct ef800_plugin_registry *registry)
{
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	char *external_output;
	#endif
	struct pm_manager_config config = {
		.contract = &contract,
		.max_plugins = EF800_PLUGIN_MAX_ENTRIES,
		.lock = host_lock,
		.unlock = host_unlock,
	};
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	external_output = ef800_memory_pool_external_alloc(
		EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE, 32U);
	if (external_output != NULL) {
		plugin_async_calculation_output = external_output;
		LOG_INF("Calculation output scratch uses external SRAM (%u bytes at %p)",
			(unsigned)EF800_PLUGIN_ASYNC_CALCULATION_OUTPUT_SIZE,
			plugin_async_calculation_output);
	} else {
		LOG_INF("Calculation output scratch uses internal fallback (%u bytes)",
			(unsigned)sizeof(plugin_async_calculation_output_internal));
	}
#endif
	plugin_filesystem = (struct pm_zephyr_filesystem){
		.mount = ef800_storage_mount_descriptor(),
		.plugin_directory = EF800_PLUGIN_DIRECTORY,
		.candidate_suffix = EF800_PLUGIN_SUFFIX,
		.max_entries = EF800_PLUGIN_SCAN_ENTRIES,
		.max_name_length = EF800_PLUGIN_MAX_NAME_LENGTH,
		.max_package_size = EF800_PLUGIN_MAX_PACKAGE_SIZE,
	};
	int result;

	/* Keep the worker stack, thread object, and MPU mailbox arena linked into
	 * the image even before external packages are discovered. */
	worker_support_anchor[0] = (uintptr_t)&plugin_worker_thread;
	worker_support_anchor[1] = (uintptr_t)plugin_worker_stack;
	worker_support_anchor[2] = (uintptr_t)&plugin_mailbox_region;
	ready = false;
	active_registry = registry;
	if (registry == NULL) {
		return -EINVAL;
	}
#if !CONFIG_BOARD_NATIVE_SIM
	plugin_arenas_configure();
#endif
#if !CONFIG_EF800_MAIN_PLUGIN_DEV_DIRECT_EXECUTION && \
	!CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	LOG_WRN("External plugins disabled: isolated worker executor is not enabled");
	return -ENOTSUP;
#endif
	if (!ef800_storage_is_mounted()) {
		return -ENODEV;
	}
	manager = pm_manager_create(&config);
	if (manager == NULL) {
		return -ENOMEM;
	}
	result = pm_manager_zephyr_attach_filesystem(manager, &plugin_filesystem);
	if (result == PM_OK) {
		result = pm_manager_zephyr_scan(manager, &plugin_filesystem);
	}
	if (result != PM_OK) {
		LOG_WRN("Plugin discovery unavailable: %d", result);
		pm_manager_destroy(manager);
		manager = NULL;
		return -EIO;
	}
	register_available_plugins();
	inspect_available_metadata();
	ready = true;
#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	result = plugin_supervisor_start();
	if (result != 0) {
		ready = false;
		LOG_ERR("Plugin supervisor start failed: %d", result);
		return result;
	}
#endif
	LOG_INF("Plugin discovery found %u package(s)",
		(unsigned)pm_manager_available_count(manager));
	return 0;
}

int ef800_plugin_host_worker_domain_attach(struct k_thread *worker,
	uint32_t *generation)
{
#if CONFIG_BOARD_NATIVE_SIM
	ARG_UNUSED(worker);
	ARG_UNUSED(generation);
	return -ENOTSUP;
#else
	int ret;
	uintptr_t text_start = (uintptr_t)plugin_text_arena;
	uintptr_t rodata_start = (uintptr_t)plugin_rodata_arena;
	uintptr_t rw_start = (uintptr_t)plugin_rw_arena;
	uintptr_t mailbox_start = (uintptr_t)&plugin_mailbox_region;

	if (worker == NULL || generation == NULL || k_is_user_context()) {
		return -EINVAL;
	}
	if ((text_start % plugin_text_capacity) != 0U ||
		(rodata_start % plugin_rodata_capacity) != 0U ||
		(rw_start % EF800_PLUGIN_RW_REGION_SIZE) != 0U ||
		(mailbox_start % EF800_PLUGIN_WORKER_MAILBOX_REGION_SIZE) != 0U) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&plugin_worker_domain_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	if (plugin_worker_domain_thread != NULL) {
		if (plugin_worker_domain_thread != worker) {
			(void)k_mutex_unlock(&plugin_worker_domain_mutex);
			return -EBUSY;
		}
		ret = k_mem_domain_add_thread(&plugin_worker_domain, worker);
		if (ret == 0) {
			*generation = plugin_worker_generation;
		}
		(void)k_mutex_unlock(&plugin_worker_domain_mutex);
		return ret;
	}
	if (!plugin_worker_domain_initialized) {
		ret = k_mem_domain_init(&plugin_worker_domain,
			ARRAY_SIZE(plugin_worker_partitions), plugin_worker_partitions);
		if (ret != 0) {
			(void)k_mutex_unlock(&plugin_worker_domain_mutex);
			return ret;
		}
		plugin_worker_domain_initialized = true;
	}
	memset(&plugin_mailbox_region, 0, sizeof(plugin_mailbox_region));
	plugin_worker_generation++;
	if (plugin_worker_generation == 0U) {
		plugin_worker_generation++;
	}
	ret = k_mem_domain_add_thread(&plugin_worker_domain, worker);
	if (ret == 0) {
		plugin_worker_domain_thread = worker;
		*generation = plugin_worker_generation;
		plugin_mailbox_region.mailbox.worker_generation =
			plugin_worker_generation;
	}
	(void)k_mutex_unlock(&plugin_worker_domain_mutex);
	return ret;
#endif
}

int ef800_plugin_host_worker_domain_detach(struct k_thread *worker,
	uint32_t generation)
{
#if CONFIG_BOARD_NATIVE_SIM
	ARG_UNUSED(worker);
	ARG_UNUSED(generation);
	return -ENOTSUP;
#else
	int ret;

	if (worker == NULL || generation == 0U || k_is_user_context()) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&plugin_worker_domain_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	if (plugin_worker_domain_thread != worker ||
		plugin_worker_generation != generation) {
		(void)k_mutex_unlock(&plugin_worker_domain_mutex);
		return -ESTALE;
	}
	ret = ef800_plugin_gateway_session_unbind(generation);
	if (ret != 0 && ret != -ESTALE) {
		(void)k_mutex_unlock(&plugin_worker_domain_mutex);
		return ret;
	}
	recovery_ledger_cleanup_mark(
		EF800_PLUGIN_RECOVERY_SERVICES_CANCELLED |
		EF800_PLUGIN_RECOVERY_GENERATION_INVALIDATED, true);
	ret = k_mem_domain_add_thread(&k_mem_domain_default, worker);
	if (ret == 0) {
		recovery_ledger_cleanup_mark(
			EF800_PLUGIN_RECOVERY_MAPPING_REMOVED, true);
		plugin_worker_domain_thread = NULL;
		memset(&plugin_mailbox_region, 0, sizeof(plugin_mailbox_region));
		memset(plugin_worker_stack, 0,
			K_THREAD_STACK_SIZEOF(plugin_worker_stack));
		plugin_worker_generation++;
		if (plugin_worker_generation == 0U) {
			plugin_worker_generation++;
		}
		recovery_ledger_cleanup_mark(0U, false);
	}
	(void)k_mutex_unlock(&plugin_worker_domain_mutex);
	return ret;
#endif
}

struct ef800_plugin_worker_mailbox *ef800_plugin_host_worker_mailbox(void)
{
	return &plugin_mailbox_region.mailbox;
}

int ef800_plugin_host_measure(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_data *measurement)
{
	void *opaque_instance = NULL;
#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_measurement_instance *instance;
#endif
	struct ef800_measurement_result result = {
		.struct_size = sizeof(result),
	};
	int status = activate_measurement(plugin_id, transport, &opaque_instance);
	LOG_INF("Native runtime plugin %u activate=%d instance=%p",
		(unsigned)plugin_id, status, opaque_instance);

	if (status != 0) {
		return status;
	}
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	size_t response_length = 0U;
	status = plugin_worker_request(EF800_PLUGIN_WORKER_MEASURE, NULL, 0U,
		&result, sizeof(result), &response_length,
		EF800_PLUGIN_DEADLINE_MEASURE_MS);
	if (status == PM_OK && response_length == sizeof(result) &&
		measurement != NULL) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	deactivate_measurement(plugin_id);
	return status == PM_OK && response_length == sizeof(result) ? 0 : -EIO;
	#else
	instance = opaque_instance;
	if (instance == NULL || instance->api == NULL || instance->api->measure == NULL) {
		deactivate_measurement(plugin_id);
		return -EPROTO;
	}
	status = instance->api->measure(instance, &result);
	LOG_INF("Native runtime plugin %u measure=%d",
		(unsigned)plugin_id, status);
	if (status == PM_OK && measurement != NULL) {
		measurement->distance_mm = result.distance_mm;
		measurement->horizontal_gon_1e4 = result.horizontal_gon_1e4;
		measurement->vertical_gon_1e4 = result.vertical_gon_1e4;
		measurement->control = result.control;
	}
	deactivate_measurement(plugin_id);
	LOG_INF("Native runtime plugin %u deactivated", (unsigned)plugin_id);
	return status == PM_OK ? 0 : -EIO;
	#endif
}

int ef800_plugin_host_input(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport, uint32_t input)
{
	void *opaque_instance = NULL;
#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_measurement_instance *instance;
#endif
	int status = activate_measurement(plugin_id, transport, &opaque_instance);

	if (status != 0) {
		return status;
	}
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	status = plugin_worker_request(EF800_PLUGIN_WORKER_INPUT, &input,
		sizeof(input), NULL, 0U, NULL,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
	deactivate_measurement(plugin_id);
	return status == PM_OK ? 0 : -EIO;
	#else
	instance = opaque_instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->handle_input == NULL) {
		deactivate_measurement(plugin_id);
		return -ENOTSUP;
	}
	status = instance->api->handle_input(instance, input);
	deactivate_measurement(plugin_id);
	return status == PM_OK ? 0 : -EIO;
	#endif
}

int ef800_plugin_host_serial_config(uint32_t plugin_id,
	const struct ef800_measurement_transport *transport,
	struct ef800_measurement_serial_config *config)
{
	void *opaque_instance = NULL;
#if !CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	struct ef800_measurement_instance *instance;
#endif
	struct ef800_serial_defaults defaults = {
		.struct_size = sizeof(defaults),
	};
	int status;

	if (config == NULL) {
		return -EINVAL;
	}
	/* Defaults are descriptor metadata and do not require a live port. */
	if (transport == NULL) {
		const struct ef800_plugin_registry_entry *entry =
			ef800_plugin_registry_find(active_registry, plugin_id);

		if (entry == NULL || entry->state != EF800_PLUGIN_REGISTRY_READY) {
			return -ENOENT;
		}
		*config = (struct ef800_measurement_serial_config){
			.baudrate = entry->info.serial_baudrate,
			.data_bits = entry->info.serial_data_bits,
			.parity = entry->info.serial_parity == EF800_SERIAL_PARITY_ODD ?
				EF800_MEASUREMENT_PARITY_ODD :
				entry->info.serial_parity == EF800_SERIAL_PARITY_EVEN ?
				EF800_MEASUREMENT_PARITY_EVEN : EF800_MEASUREMENT_PARITY_NONE,
			.stop_bits = entry->info.serial_stop_bits,
			.flow_control = entry->info.serial_flow_control ==
				EF800_SERIAL_FLOW_RTS_CTS ? EF800_MEASUREMENT_FLOW_RTS_CTS :
				EF800_MEASUREMENT_FLOW_NONE,
		};
		return 0;
	}
	status = activate_measurement(plugin_id, transport, &opaque_instance);
	if (status != 0) {
		return status;
	}
	#if CONFIG_EF800_MAIN_PLUGIN_USER_WORKER
	size_t response_length = 0U;
	status = plugin_worker_request(EF800_PLUGIN_WORKER_GET_SERIAL_DEFAULTS,
		NULL, 0U, &defaults, sizeof(defaults), &response_length,
		EF800_PLUGIN_DEADLINE_INTERACTIVE_MS);
	deactivate_measurement(plugin_id);
	if (status != PM_OK || response_length != sizeof(defaults)) {
		return -EIO;
	}
	#else
	instance = opaque_instance;
	if (instance == NULL || instance->api == NULL ||
		instance->api->get_serial_defaults == NULL) {
		deactivate_measurement(plugin_id);
		return -ENOTSUP;
	}
	status = instance->api->get_serial_defaults(instance, &defaults);
	deactivate_measurement(plugin_id);
	if (status != PM_OK) {
		return -EIO;
	}
	#endif
	*config = (struct ef800_measurement_serial_config){
		.baudrate = defaults.baudrate,
		.data_bits = (uint8_t)defaults.data_bits,
		.parity = defaults.parity == EF800_SERIAL_PARITY_ODD ?
			EF800_MEASUREMENT_PARITY_ODD :
			defaults.parity == EF800_SERIAL_PARITY_EVEN ?
			EF800_MEASUREMENT_PARITY_EVEN : EF800_MEASUREMENT_PARITY_NONE,
		.stop_bits = (uint8_t)defaults.stop_bits,
		.flow_control = defaults.flow_control == EF800_SERIAL_FLOW_RTS_CTS ?
			EF800_MEASUREMENT_FLOW_RTS_CTS : EF800_MEASUREMENT_FLOW_NONE,
	};
	return 0;
}

bool ef800_plugin_host_is_ready(void)
{
	return ready;
}

size_t ef800_plugin_host_available_count(void)
{
	return manager == NULL ? 0u : pm_manager_available_count(manager);
}
