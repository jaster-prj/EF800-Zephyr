/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Exercise Zephyr user-mode isolation with a dummy plugin. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/app_memory/mem_domain.h>
#include <zephyr/arch/exception.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/flash.h>

#include "usb_cdc_host.h"
#if CONFIG_EF800_SPIKE_FAULT_MODE == 7
#include "pm_dummy_plugin_host.h"
#include "../dummy_plugin/dummy_plugin_api.h"
#include "plugin_manager/plugin_abi.h"
#include "plugin_manager/plugin_manager.h"
#endif

#include <cmsis_core.h>

#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#define SPIKE_STACK_SIZE 2048
#define SPIKE_REGION_SIZE 1024
#define SPIKE_MESSAGE_CAPACITY 128
#define SPIKE_MAILBOX_FIXED_SIZE \
	(3U * sizeof(uintptr_t) + 2U * sizeof(atomic_t) + \
	 2U * sizeof(uint16_t) + sizeof(uint8_t) + sizeof(int32_t) + \
	 2U * SPIKE_MESSAGE_CAPACITY)

enum spike_worker_request_kind {
	SPIKE_WORKER_REQUEST_PROCESS = 1U,
	SPIKE_WORKER_REQUEST_CREATE,
	SPIKE_WORKER_REQUEST_DESTROY,
};

#define SPIKE_FAULT_NONE 0
#define SPIKE_FAULT_READ_HOST 1
#define SPIKE_FAULT_WRITE_HOST 2
#define SPIKE_FAULT_EXECUTE_XN 3
#define SPIKE_FAULT_INVALID_INSTRUCTION 4
#define SPIKE_FAULT_STACK_GUARD_WRITE 5
#define SPIKE_FAULT_STACK_OVERFLOW 6
#define SPIKE_FAULT_USB_CDC_TEST 7
#define SPIKE_FAULT_PRIVILEGED_THREAD 8
#define SPIKE_FAULT_ISR 9
#define SPIKE_FAULT_NESTED 10
#define SPIKE_FAULT_UNATTRIBUTABLE 11
#define SPIKE_FAULT_RO_WRITE 12
#define SPIKE_FAULT_MAILBOX_EXECUTE 13
#define SPIKE_FAULT_KERNEL_OBJECT_READ 14
#define SPIKE_FAULT_MMIO_READ 15
#define SPIKE_FAULT_UNMAPPED_PLUGIN_READ 16

#define SPIKE_FATAL_REBOOT_COOKIE 0xEF80FA17U

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
extern uint8_t __plugin_text_start[];
extern uint8_t __plugin_text_end[];
extern uint8_t __plugin_text_load_start[];
#endif

/* Power-of-two regions make the ARMv7-M MPU layout explicit and measurable. */
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
#define EF800_EXT_SRAM_BASE 0x60000000U
static uint8_t *const plugin_ro = (uint8_t *)(EF800_EXT_SRAM_BASE + 0x1000U);
static uint8_t *const plugin_rw = (uint8_t *)(EF800_EXT_SRAM_BASE + 0x2000U);
static uint8_t *const plugin_unmapped = (uint8_t *)(EF800_EXT_SRAM_BASE + 0x3000U);
static struct k_heap external_plugin_heap;
static void *plugin_rw_allocation;
#define SPIKE_REGION_SIZEOF(array) SPIKE_REGION_SIZE
#else
static uint8_t plugin_ro[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE);
static uint8_t plugin_rw[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE);
static uint8_t plugin_unmapped[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE);
static void *plugin_rw_allocation = plugin_rw;
#define SPIKE_REGION_SIZEOF(array) sizeof(array)
#endif
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
static uint8_t plugin_loaded_text[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE)
	__attribute__((section(".plugin_text.pm_image")));
#endif
struct plugin_mailbox_data {
	volatile uintptr_t guard_write_address;
	volatile uintptr_t lifecycle_instance;
	volatile uintptr_t lifecycle_entry;
	atomic_t request_sequence;
	atomic_t response_sequence;
	volatile uint16_t request_length;
	volatile uint16_t response_length;
	volatile int32_t operation_status;
	volatile uint8_t request_kind;
	volatile uint8_t request[SPIKE_MESSAGE_CAPACITY];
	volatile uint8_t response[SPIKE_MESSAGE_CAPACITY];
	volatile uint8_t values[SPIKE_REGION_SIZE - SPIKE_MAILBOX_FIXED_SIZE];
} __aligned(SPIKE_REGION_SIZE);
BUILD_ASSERT(sizeof(struct plugin_mailbox_data) == SPIKE_REGION_SIZE);
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
#define plugin_mailbox (*(struct plugin_mailbox_data *)(EF800_EXT_SRAM_BASE + 0x4000U))
#else
static struct plugin_mailbox_data plugin_mailbox __aligned(SPIKE_REGION_SIZE);
#endif

static struct k_mem_domain plugin_domain;
static struct k_thread plugin_thread;
K_THREAD_STACK_DEFINE(plugin_stack, SPIKE_STACK_SIZE);
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
K_SEM_DEFINE(plugin_fault_sem, 0, 1);
static atomic_t plugin_fault_reason = ATOMIC_INIT(-1);
static atomic_t plugin_fault_pc = ATOMIC_INIT(0);
static volatile bool plugin_fault_expected;
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_NESTED
static volatile bool plugin_fault_nested_triggered;
#endif
#endif
static volatile uint32_t protected_host_word = 0x13579bdfU;
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
static bool plugin_domain_initialized;
static bool plugin_thread_in_domain;
static bool plugin_thread_active;
static bool plugin_restart_permitted = true;
static uint32_t plugin_generation;
static int plugin_last_reap_status;
static bool plugin_last_request_faulted;
static bool plugin_last_request_timed_out;
static enum {
	SPIKE_PLUGIN_STOPPED,
	SPIKE_PLUGIN_RUNNING,
	SPIKE_PLUGIN_CRASHED,
} plugin_state;
#endif

/* Backup-domain registers survive the EF800 bootloader's software-reset
 * handoff; ordinary .noinit SRAM does not survive that handoff reliably. */
static void backup_domain_enable_write(void)
{
	RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
	(void)RCC->APB1ENR1;
	PWR->CR1 |= PWR_CR1_DBP;
	while ((PWR->CR1 & PWR_CR1_DBP) == 0U) {
	}
}

static void fatal_reboot_marker_set(void)
{
	backup_domain_enable_write();
	RTC->BKP0R = SPIKE_FATAL_REBOOT_COOKIE;
	RTC->BKP1R = ~SPIKE_FATAL_REBOOT_COOKIE;
	RTC->BKP2R = CONFIG_EF800_SPIKE_FAULT_MODE;
	__DSB();
	__ISB();
}

static bool fatal_reboot_marker_matches_mode(void)
{
	bool valid;

	backup_domain_enable_write();
	valid = RTC->BKP0R == SPIKE_FATAL_REBOOT_COOKIE &&
		RTC->BKP1R == ~SPIKE_FATAL_REBOOT_COOKIE;
	if (valid && RTC->BKP2R == CONFIG_EF800_SPIKE_FAULT_MODE) {
		return true;
	}
	if (valid) {
		RTC->BKP0R = 0U;
		RTC->BKP1R = 0U;
		RTC->BKP2R = 0U;
		__DSB();
	}
	return false;
}

#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
static struct k_mem_partition plugin_ro_partition = {
	.start = (uintptr_t)plugin_ro,
	.size = SPIKE_REGION_SIZE,
	.attr = K_MEM_PARTITION_P_RO_U_RO,
};
#else
K_MEM_PARTITION_DEFINE(plugin_ro_partition, plugin_ro, SPIKE_REGION_SIZEOF(plugin_ro),
	K_MEM_PARTITION_P_RO_U_RO);
#endif
static struct k_mem_partition plugin_text_partition = {
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	.start = (uintptr_t)plugin_loaded_text,
#else
	.start = (uintptr_t)__plugin_text_start,
#endif
	.size = SPIKE_REGION_SIZE,
	.attr = K_MEM_PARTITION_P_RX_U_RX,
};
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
static struct k_mem_partition plugin_rw_partition = {
	.start = (uintptr_t)plugin_rw,
	.size = SPIKE_REGION_SIZE,
	.attr = K_MEM_PARTITION_P_RW_U_RW,
};
#else
K_MEM_PARTITION_DEFINE(plugin_rw_partition, plugin_rw, SPIKE_REGION_SIZEOF(plugin_rw),
	K_MEM_PARTITION_P_RW_U_RW);
#endif
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
static struct k_mem_partition plugin_mailbox_partition = {
	.start = (uintptr_t)&plugin_mailbox,
	.size = sizeof(plugin_mailbox),
	.attr = K_MEM_PARTITION_P_RW_U_RW,
};
#else
K_MEM_PARTITION_DEFINE(plugin_mailbox_partition, &plugin_mailbox,
	sizeof(plugin_mailbox), K_MEM_PARTITION_P_RW_U_RW);
#endif

#if defined(CONFIG_BOARD_EF800)
/* USART1 is an assignable EF800 measurement-port route. Keep the L496 spike
 * silent on that peripheral so a connected instrument is not disturbed. */
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(usart6), okay)
#define SPIKE_AUX_UART_NODE DT_NODELABEL(usart6)
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(usart1), okay)
#define SPIKE_AUX_UART_NODE DT_NODELABEL(usart1)
#endif

static void aux_uart_write(const char *text)
{
#if defined(SPIKE_AUX_UART_NODE)
	const struct device *uart = DEVICE_DT_GET(SPIKE_AUX_UART_NODE);

	if (!device_is_ready(uart)) {
		return;
	}
	for (const char *cursor = text; *cursor != '\0'; cursor++) {
		uart_poll_out(uart, (unsigned char)*cursor);
	}
#else
	ARG_UNUSED(text);
#endif
}

static void report_spi_flash(void)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi_flash0), okay)
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(spi_flash0));
	const struct flash_parameters *parameters;
	uint8_t probe[16] = {0};
	int ret;

	if (!device_is_ready(flash)) {
		printk("spi flash not ready\n");
		return;
	}
	parameters = flash_get_parameters(flash);
	ret = flash_read(flash, 0, probe, sizeof(probe));
	printk("spi flash write_block=%u erase=%u read=%d bytes=%02x%02x%02x%02x PASS\n",
		(unsigned int)parameters->write_block_size,
		(unsigned int)flash_params_get_erase_cap(parameters),
		ret, probe[0], probe[1], probe[2], probe[3]);
#else
	printk("spi flash node unavailable\n");
#endif
}

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_NONE
static void plugin_worker_step(void)
	__attribute__((noinline, section(".plugin_text.worker")));
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_STACK_OVERFLOW
static void plugin_stack_overflow(unsigned int depth)
	__attribute__((noinline, section(".plugin_text.worker")));
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
static bool is_expected_fault_pc(uint32_t stacked_pc)
{
	uintptr_t pc = stacked_pc & ~1U;
	uintptr_t start;
	uintptr_t end;

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_EXECUTE_XN
	start = (uintptr_t)plugin_rw;
	end = start + SPIKE_REGION_SIZEOF(plugin_rw);
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_MAILBOX_EXECUTE
	start = (uintptr_t)&plugin_mailbox;
	end = start + sizeof(plugin_mailbox);
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	start = (uintptr_t)plugin_loaded_text;
	end = start + sizeof(plugin_loaded_text);
#else
	start = (uintptr_t)__plugin_text_start;
	end = (uintptr_t)__plugin_text_end;
#endif
	return pc >= start && pc < end;
}

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
static void plugin_worker_fault(void)
	__attribute__((noinline, section(".plugin_text.worker")));
#endif
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_STACK_OVERFLOW
static void plugin_stack_overflow(unsigned int depth)
{
	volatile uint8_t frame[256];
	void (*volatile recurse)(unsigned int) = plugin_stack_overflow;

	frame[0] = (uint8_t)depth;
	recurse(depth + 1U);
	/* Keep each recursive frame live until the recursive call returns. */
	__asm__ volatile("" : : "r"(frame[0]) : "memory");
}
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_ISR
static void isr_fault_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	__asm__ volatile("udf #0" ::: "memory");
}

K_TIMER_DEFINE(isr_fault_timer, isr_fault_timer_expiry, NULL);
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE && \
	CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
static void plugin_worker_fault(void)
{
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_READ_HOST
	volatile uint32_t value = protected_host_word;
	plugin_mailbox.values[3] = (uint8_t)value;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_WRITE_HOST
	protected_host_word = 0x2468ace0U;
	plugin_mailbox.values[3] = 0x52U;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_EXECUTE_XN
	void (*entry)(void) = (void (*)(void))((uintptr_t)plugin_rw | 1U);

	entry();
	plugin_mailbox.values[3] = 0x58U;
	while (true) {
		__asm__ volatile("nop");
  }
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_INVALID_INSTRUCTION
	__asm__ volatile("udf #0" ::: "memory");
	plugin_mailbox.values[3] = 0x49U;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_STACK_GUARD_WRITE
	*(volatile uint32_t *)plugin_mailbox.guard_write_address = 0x47554152U;
	plugin_mailbox.values[3] = 0x47U;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_STACK_OVERFLOW
	plugin_stack_overflow(0U);
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_NESTED
	__asm__ volatile("udf #0" ::: "memory");
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_RO_WRITE
	*(volatile uint8_t *)plugin_ro = 0xA5U;
	plugin_mailbox.values[3] = 0x52U;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_MAILBOX_EXECUTE
	{
		void (*entry)(void) = (void (*)(void))(
			((uintptr_t)&plugin_mailbox.request[0]) | 1U);

		entry();
	}
	plugin_mailbox.values[3] = 0x4DU;
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_KERNEL_OBJECT_READ
	{
		volatile uint32_t value = *(volatile uint32_t *)&plugin_thread;

		plugin_mailbox.values[3] = (uint8_t)value;
	}
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_MMIO_READ
	{
		volatile uint32_t value = *(volatile uint32_t *)(uintptr_t)0x40000000U;

		plugin_mailbox.values[3] = (uint8_t)value;
	}
	while (true) {
		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_UNMAPPED_PLUGIN_READ
	{
		volatile uint8_t value = *(volatile uint8_t *)plugin_unmapped;

		plugin_mailbox.values[3] = value;
	}
	while (true) {
		__asm__ volatile("nop");
	}
#endif
}
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_NONE
static void plugin_worker_step(void)
{
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
	uint8_t *allocation = (uint8_t *)(uintptr_t)plugin_mailbox.lifecycle_entry;
#else
	uint8_t *allocation = plugin_rw_allocation;
#endif
	allocation[0] = 0x5aU;
	plugin_mailbox.values[0] = allocation[0];
	plugin_mailbox.values[1] = 1U;
	plugin_mailbox.values[2] = 0xa5U;
}
#endif

static void plugin_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	int handled_sequence = 0;
	void *instance = NULL;

	plugin_mailbox.values[4] = k_is_user_context() ? 1U : 0U;
	plugin_mailbox.values[5] = 1U;
	__DMB();
	while (true) {
		int sequence = atomic_get(&plugin_mailbox.request_sequence);

		if (sequence != handled_sequence) {
			uint16_t length = plugin_mailbox.request_length;
			uint8_t command[SPIKE_MESSAGE_CAPACITY];
			size_t response_length = 0U;
			int operation_status = PM_EINVAL;
			bool stop_worker = false;

			handled_sequence = sequence;
			if (length > sizeof(command)) {
				operation_status = PM_EOVERFLOW;
			} else {
				for (size_t i = 0U; i < length; ++i) {
					command[i] = plugin_mailbox.request[i];
				}
			}

			if (operation_status != PM_EOVERFLOW) {
				switch (plugin_mailbox.request_kind) {
				case SPIKE_WORKER_REQUEST_CREATE: {
					pm_plugin_create_fn create =
						(pm_plugin_create_fn)(uintptr_t)
						plugin_mailbox.lifecycle_entry;
					const struct pm_service_table services = {
						.count = 0U, .items = NULL,
					};

					if (!k_is_user_context() || instance != NULL ||
						create == NULL) {
						operation_status = PM_EPERM;
						break;
					}
					operation_status = create(&services, &instance);
					if (operation_status == PM_OK && instance == NULL) {
						operation_status = PM_EPROTO;
					}
					if (operation_status == PM_OK) {
						plugin_mailbox.lifecycle_instance =
							(uintptr_t)instance;
					}
					break;
				}
				case SPIKE_WORKER_REQUEST_DESTROY: {
					pm_plugin_destroy_fn destroy =
						(pm_plugin_destroy_fn)(uintptr_t)
						plugin_mailbox.lifecycle_entry;

					if (!k_is_user_context() || instance == NULL ||
						instance != (void *)(uintptr_t)
							plugin_mailbox.lifecycle_instance ||
						destroy == NULL) {
						operation_status = PM_EPERM;
						break;
					}
					operation_status = destroy(instance);
					if (operation_status == PM_OK) {
						instance = NULL;
						stop_worker = true;
					}
					break;
				}
				case SPIKE_WORKER_REQUEST_PROCESS: {
					struct spike_dummy_plugin_instance *dummy_instance =
						instance;

					if (!k_is_user_context() || instance == NULL ||
						dummy_instance->process == NULL) {
						operation_status = PM_EPERM;
						break;
					}
					operation_status = dummy_instance->process(
						instance, command, length,
						(uint8_t *)plugin_mailbox.response,
						SPIKE_MESSAGE_CAPACITY, &response_length);
					break;
				}
				default:
					operation_status = PM_EINVAL;
					break;
				}
			}

			plugin_mailbox.operation_status = operation_status;
			plugin_mailbox.response_length = (uint16_t)response_length;
			__DMB();
			atomic_set(&plugin_mailbox.response_sequence, sequence);
			if (stop_worker) {
				/* K_USER entry functions must not return through
				 * arch_user_mode_enter(); self-abort performs the supported
				 * transition to the terminated-thread state. */
				k_thread_abort(k_current_get());
				for (;;) {
					k_yield();
				}
			}
		}

		__asm__ volatile("nop");
	}
#elif CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
	#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_UNATTRIBUTABLE
	/* Fault from the user worker trampoline, outside the registered plugin
	 * text range. The narrow fatal hook must not classify it as plugin code. */
	plugin_mailbox.values[4] = k_is_user_context() ? 1U : 0U;
	__asm__ volatile("udf #0" ::: "memory");
	for (;;) {
		__asm__ volatile("nop");
	}
	#else
	plugin_mailbox.values[4] = k_is_user_context() ? 1U : 0U;

	plugin_worker_fault();
	#endif
#else
	plugin_mailbox.values[4] = k_is_user_context() ? 1U : 0U;
	plugin_worker_step();
	while (true) {
		/* Keep the worker alive without another userspace syscall; the
		 * supervisor observes completion through the granted mailbox. */
	}
#endif
}

/* Returning from this hook lets Zephyr's fatal path abort the current thread.
 * Only the known user worker and a thread-mode exception are recoverable in
 * this test image; every other fatal condition retains halt behavior. */
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_NESTED
	if (!plugin_fault_nested_triggered && plugin_fault_expected &&
		k_current_get() == &plugin_thread && esf != NULL &&
		is_expected_fault_pc(esf->basic.pc) &&
		(esf->basic.xpsr & IPSR_ISR_Msk) == 0U) {
		/* Deliberately fault inside the handler once. The recursive fatal
		 * entry must not be mistaken for the original plugin fault. */
		plugin_fault_nested_triggered = true;
		__asm__ volatile("udf #0" ::: "memory");
	}
#endif
	if (CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE &&
		plugin_fault_expected && k_current_get() == &plugin_thread &&
		esf != NULL && is_expected_fault_pc(esf->basic.pc) &&
		(esf->basic.xpsr & IPSR_ISR_Msk) == 0U) {
		atomic_set(&plugin_fault_pc, (atomic_val_t)esf->basic.pc);
		atomic_set(&plugin_fault_reason, (atomic_val_t)reason);
		k_sem_give(&plugin_fault_sem);
		return;
	}
	/* Unattributable faults must never be reported as plugin failures. The
	 * architecture has already emitted the fatal reason/register frame before
	 * calling this hook. Record a one-shot marker and reset instead of returning
	 * through Zephyr's thread-abort path with unknown system state. */
	fatal_reboot_marker_set();
	sys_reboot(SYS_REBOOT_COLD);
}
#endif

static int install_plugin_domain(void)
{
	struct k_mem_partition *partitions[] = {
		&plugin_text_partition,
		&plugin_ro_partition,
		&plugin_rw_partition,
		&plugin_mailbox_partition,
	};
	int ret;

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	if (!plugin_domain_initialized) {
		ret = k_mem_domain_init(&plugin_domain, ARRAY_SIZE(partitions),
			partitions);
		if (ret != 0) {
			return ret;
		}
		plugin_domain_initialized = true;
	}
#else
	ret = k_mem_domain_init(&plugin_domain, ARRAY_SIZE(partitions), partitions);
	if (ret != 0) {
		return ret;
	}
#endif

	ret = k_mem_domain_add_thread(&plugin_domain, &plugin_thread);
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	if (ret == 0) {
		plugin_thread_in_domain = true;
	}
#endif
	return ret;
}

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
static uint32_t load_plugin_text(void)
{
	size_t text_size = (size_t)(__plugin_text_end - __plugin_text_start);

	/* The linker reserves an SRAM VMA and a flash LMA for the test function.
	 * Copy it while privileged; the worker's MPU domain later exposes only the
	 * aligned RX partition. */
	memcpy(__plugin_text_start, __plugin_text_load_start, text_size);
	__DSB();
	__ISB();
	return *(const uint32_t *)__plugin_text_start;
}
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
#define SPIKE_USB_LINE_CAPACITY 128U
#define SPIKE_PLUGIN_COMMAND_MAX (SPIKE_MESSAGE_CAPACITY - 8U)

static char usb_line[SPIKE_USB_LINE_CAPACITY];
static size_t usb_line_length;
static bool usb_line_overflow;

static void usb_write_text(const char *text)
{
	spike_usb_cdc_host_write((const uint8_t *)text, strlen(text));
}

static void usb_writef(const char *format, ...)
{
	char buffer[192];
	va_list args;
	int length;

	va_start(args, format);
	length = vsnprintk(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (length > 0) {
		size_t available = MIN((size_t)length, sizeof(buffer) - 1U);

		spike_usb_cdc_host_write((const uint8_t *)buffer, available);
	}
	spike_usb_cdc_host_write((const uint8_t *)"\r\n", 2U);
}

static bool usb_line_equals(const char *expected)
{
	return strlen(expected) == usb_line_length &&
		memcmp(usb_line, expected, usb_line_length) == 0;
}

static int reap_plugin_thread(bool abort_thread)
{
	int ret;

	if (!plugin_thread_active) {
		return 0;
	}
	plugin_fault_expected = false;
	if (abort_thread) {
		k_thread_abort(&plugin_thread);
	}
	ret = k_thread_join(&plugin_thread, K_MSEC(500));
	if (ret != 0) {
		k_thread_abort(&plugin_thread);
		ret = k_thread_join(&plugin_thread, K_MSEC(500));
	}
	if (ret != 0) {
		return ret;
	}
	if (plugin_thread_in_domain) {
		/* Zephyr 4.4.2 has no public remove-thread API. Moving the dead
		 * worker to the default domain removes its plugin permissions. */
		ret = k_mem_domain_add_thread(&k_mem_domain_default, &plugin_thread);
		if (ret != 0) {
			return ret;
		}
		plugin_thread_in_domain = false;
	}
	plugin_thread_active = false;
	return 0;
}

static int dispatch_worker_request(enum spike_worker_request_kind kind,
				   const uint8_t *command,
				   size_t command_length,
				   uint8_t *response,
				   size_t response_capacity,
				   size_t *response_length,
				   void **instance_out)
{
	int sequence;
	int64_t deadline;

	plugin_last_request_faulted = false;
	plugin_last_request_timed_out = false;
	plugin_last_reap_status = 0;
	if (!plugin_thread_active || command_length > SPIKE_MESSAGE_CAPACITY ||
		(command_length > 0U && command == NULL)) {
		return PM_EINVAL;
	}
	sequence = atomic_get(&plugin_mailbox.request_sequence) + 1;
	if (sequence <= 0) {
		sequence = 1;
	}
	for (size_t i = 0U; i < command_length; ++i) {
		plugin_mailbox.request[i] = command[i];
	}
	plugin_mailbox.request_length = (uint16_t)command_length;
	plugin_mailbox.response_length = 0U;
	plugin_mailbox.request_kind = (uint8_t)kind;
	__DMB();
	atomic_set(&plugin_mailbox.request_sequence, sequence);
	deadline = k_uptime_get() + 1000;

	while (k_uptime_get() < deadline) {
		if (k_sem_take(&plugin_fault_sem, K_NO_WAIT) == 0) {
			plugin_last_request_faulted = true;
			plugin_last_reap_status = reap_plugin_thread(false);
			return PM_EIO;
		}
		if (atomic_get(&plugin_mailbox.response_sequence) == sequence) {
			int status = plugin_mailbox.operation_status;
			size_t length = plugin_mailbox.response_length;

			__DMB();
			if (status == PM_OK) {
				if (response != NULL) {
					if (length > response_capacity) {
						return PM_EOVERFLOW;
					}
					for (size_t i = 0U; i < length; ++i) {
						response[i] = plugin_mailbox.response[i];
					}
				}
				if (response_length != NULL) {
					*response_length = length;
				}
				if (instance_out != NULL) {
					*instance_out = (void *)(uintptr_t)
						plugin_mailbox.lifecycle_instance;
				}
			}
			return status;
		}
		k_msleep(1);
	}

	plugin_last_request_faulted = true;
	plugin_last_request_timed_out = true;
	plugin_last_reap_status = reap_plugin_thread(true);
	return PM_EIO;
}

static int dispatch_plugin_create(void *context, pm_plugin_create_fn entry,
				  const struct pm_service_table *services,
				  void **instance)
{
	ARG_UNUSED(context);
	if (entry == NULL || services == NULL || services->count != 0U ||
		instance == NULL) {
		return PM_ENOTSUP;
	}
	plugin_mailbox.lifecycle_entry = (uintptr_t)entry;
	plugin_mailbox.lifecycle_instance = 0U;
	__DMB();
	return dispatch_worker_request(SPIKE_WORKER_REQUEST_CREATE, NULL, 0U,
				       NULL, 0U, NULL, instance);
}

static int dispatch_plugin_destroy(void *context, pm_plugin_destroy_fn entry,
				   void *instance)
{
	ARG_UNUSED(context);
	if (entry == NULL || instance == NULL ||
		instance != (void *)(uintptr_t)plugin_mailbox.lifecycle_instance) {
		return PM_EINVAL;
	}
	plugin_mailbox.lifecycle_entry = (uintptr_t)entry;
	__DMB();
	return dispatch_worker_request(SPIKE_WORKER_REQUEST_DESTROY, NULL, 0U,
				       NULL, 0U, NULL, NULL);
}

static const struct pm_manager_instance_executor plugin_executor = {
	.create = dispatch_plugin_create,
	.destroy = dispatch_plugin_destroy,
};

static int start_plugin(void)
{
	k_tid_t thread;
	int ret;

	if (plugin_state != SPIKE_PLUGIN_STOPPED || plugin_thread_active ||
		!plugin_restart_permitted) {
		return -EBUSY;
	}

	memset(&plugin_mailbox, 0, sizeof(plugin_mailbox));
	k_sem_reset(&plugin_fault_sem);
	ret = spike_pm_dummy_plugin_load(plugin_loaded_text,
						 sizeof(plugin_loaded_text), plugin_ro,
						 SPIKE_REGION_SIZEOF(plugin_ro), plugin_rw,
						 SPIKE_REGION_SIZEOF(plugin_rw), &protected_host_word);
	if (ret != PM_OK) {
		return ret;
	}
	thread = k_thread_create(&plugin_thread, plugin_stack,
		K_THREAD_STACK_SIZEOF(plugin_stack), plugin_worker,
		NULL, NULL, NULL, 5, K_USER, K_FOREVER);
	if (thread == NULL) {
		return -ENOMEM;
	}
	/* The console runs with userspace enabled. Grant its host thread the
	 * explicit object permission needed for bounded join/abort supervision. */
	k_object_access_grant(&plugin_thread, k_current_get());
	plugin_thread_active = true;
	ret = install_plugin_domain();
	if (ret != 0) {
		if (reap_plugin_thread(true) != 0) {
			plugin_restart_permitted = false;
		}
		return ret;
	}
	plugin_fault_expected = true;
	k_thread_start(&plugin_thread);

	for (int i = 0; i < 100 && plugin_mailbox.values[5] == 0U; ++i) {
		k_msleep(1);
	}
	if (plugin_mailbox.values[5] == 0U || plugin_mailbox.values[4] != 1U) {
		ret = reap_plugin_thread(true);
		if (ret == 0) {
			ret = spike_pm_dummy_plugin_unload();
		}
		if (ret != PM_OK) {
			plugin_restart_permitted = false;
		}
		return -EACCES;
	}
	plugin_last_reap_status = 0;
	ret = spike_pm_dummy_plugin_create_instance(&plugin_executor);
	if (ret != PM_OK) {
		if (plugin_thread_active) {
			plugin_last_reap_status = reap_plugin_thread(true);
		}
		if (plugin_last_reap_status == 0) {
			int unload_status = spike_pm_dummy_plugin_unload();

			if (unload_status != PM_OK) {
				plugin_restart_permitted = false;
			}
		} else {
			plugin_restart_permitted = false;
		}
		return ret;
	}

	plugin_generation++;
	plugin_state = SPIKE_PLUGIN_RUNNING;
	return 0;
}

static int submit_plugin_command(const uint8_t *command, size_t command_length,
				 uint8_t *response, size_t response_capacity,
				 size_t *response_length)
{
	int ret;

	if (plugin_state != SPIKE_PLUGIN_RUNNING || command == NULL ||
		command_length == 0U || command_length > SPIKE_PLUGIN_COMMAND_MAX ||
		response == NULL || response_length == NULL) {
		return -EINVAL;
	}
	ret = dispatch_worker_request(SPIKE_WORKER_REQUEST_PROCESS, command,
				      command_length, response,
				      response_capacity, response_length, NULL);
	if (plugin_last_request_faulted) {
		atomic_val_t reason = atomic_get(&plugin_fault_reason);
		atomic_val_t pc = atomic_get(&plugin_fault_pc);
		int abandon_ret = PM_EBUSY;
		int unload_ret = PM_EBUSY;

		if (plugin_last_reap_status == 0) {
			abandon_ret = spike_pm_dummy_plugin_abandon_instance();
			if (abandon_ret == PM_OK) {
				unload_ret = spike_pm_dummy_plugin_unload();
			}
		}
		if (abandon_ret != PM_OK || unload_ret != PM_OK) {
			plugin_restart_permitted = false;
		}
		plugin_state = SPIKE_PLUGIN_CRASHED;
		if (plugin_last_request_timed_out) {
			usb_writef("PLUGIN_TIMEOUT reap=%d abandon=%d unload=%d; use RESTART",
				plugin_last_reap_status, abandon_ret, unload_ret);
			return -ETIMEDOUT;
		}
		usb_writef("PLUGIN_FAULT reason=%d pc=0x%08x reap=%d abandon=%d unload=%d; use RESTART",
			(int)reason, (unsigned int)pc, plugin_last_reap_status,
			abandon_ret, unload_ret);
		return -EFAULT;
	}
	return ret == PM_OK ? 0 : ret;
}

static int finish_plugin(void)
{
	int ret;

	if (plugin_state != SPIKE_PLUGIN_RUNNING) {
		return -EALREADY;
	}
	ret = spike_pm_dummy_plugin_destroy_instance(&plugin_executor);
	if (ret != 0) {
		int reap_ret = plugin_last_reap_status;

		if (plugin_thread_active) {
			reap_ret = reap_plugin_thread(true);
		}
		if (reap_ret == 0 && !plugin_thread_active) {
			int abandon_ret = spike_pm_dummy_plugin_abandon_instance();

			if (abandon_ret == PM_OK) {
				if (spike_pm_dummy_plugin_unload() != PM_OK) {
					plugin_restart_permitted = false;
				}
			} else {
				plugin_restart_permitted = false;
			}
		} else {
			plugin_restart_permitted = false;
		}
		plugin_state = SPIKE_PLUGIN_CRASHED;
		return ret;
	}
	ret = reap_plugin_thread(false);
	if (ret != 0) {
		plugin_state = SPIKE_PLUGIN_CRASHED;
		plugin_restart_permitted = false;
		return ret;
	}
	ret = spike_pm_dummy_plugin_unload();
	if (ret != PM_OK) {
		plugin_restart_permitted = false;
		plugin_state = SPIKE_PLUGIN_CRASHED;
		return ret;
	}
	plugin_state = SPIKE_PLUGIN_STOPPED;
	return 0;
}

static int restart_plugin(void)
{
	int ret;

	if (plugin_state == SPIKE_PLUGIN_RUNNING) {
		ret = finish_plugin();
		if (ret != 0) {
			return ret;
		}
	} else if (plugin_state == SPIKE_PLUGIN_CRASHED) {
		if (!plugin_restart_permitted) {
			return -EIO;
		}
		ret = reap_plugin_thread(true);
		if (ret != 0) {
			return ret;
		}
		plugin_state = SPIKE_PLUGIN_STOPPED;
	}
	ret = spike_pm_dummy_plugin_unload();
	if (ret != PM_OK) {
		plugin_restart_permitted = false;
		return ret;
	}

	return start_plugin();
}

static const char *plugin_state_name(void)
{
	switch (plugin_state) {
	case SPIKE_PLUGIN_RUNNING:
		return "RUNNING";
	case SPIKE_PLUGIN_CRASHED:
		return "CRASHED";
	default:
		return "STOPPED";
	}
}

static void handle_usb_command(void)
{
	int ret;

	if (usb_line_length == 0U) {
		return;
	}
	usb_line[usb_line_length] = '\0';
	if (usb_line_equals("START")) {
		ret = start_plugin();
		if (ret == 0) {
			usb_writef("OK STARTED generation=%u user=%u",
				plugin_generation, plugin_mailbox.values[4]);
		} else {
			usb_writef("ERR START %d state=%s", ret, plugin_state_name());
		}
	} else if (usb_line_equals("FINISH")) {
		ret = finish_plugin();
		usb_writef(ret == 0 ? "OK FINISHED" : "ERR FINISH %d state=%s",
			   ret, plugin_state_name());
	} else if (usb_line_equals("RESTART")) {
		ret = restart_plugin();
		if (ret == 0) {
			usb_writef("OK RESTARTED generation=%u user=%u",
				plugin_generation, plugin_mailbox.values[4]);
		} else {
			usb_writef("ERR RESTART %d state=%s", ret, plugin_state_name());
		}
	} else if (usb_line_equals("STATUS")) {
		usb_writef("STATUS %s generation=%u", plugin_state_name(),
			   plugin_generation);
	} else if (usb_line_overflow) {
		usb_write_text("ERR LINE_TOO_LONG\r\n");
	} else if (plugin_state != SPIKE_PLUGIN_RUNNING) {
		usb_write_text("ERR NOT_RUNNING; use START\r\n");
	} else {
		uint8_t response[SPIKE_MESSAGE_CAPACITY];
		size_t response_length;

		ret = submit_plugin_command((const uint8_t *)usb_line,
					    usb_line_length, response,
					    sizeof(response), &response_length);
		if (ret == 0) {
			spike_usb_cdc_host_write(response, response_length);
			spike_usb_cdc_host_write((const uint8_t *)"\r\n", 2U);
		} else if (ret != -EFAULT && ret != -ETIMEDOUT) {
			usb_writef("ERR PLUGIN_COMMAND %d", ret);
		}
	}

	usb_line_length = 0U;
	usb_line_overflow = false;
}

static int run_usb_cdc_console(void)
{
	uint8_t byte;
	int ret = spike_usb_cdc_host_init();

	if (ret != 0) {
		printk("USB CDC init failed: %d\n", ret);
		return ret;
	}
	printk("USER_USB CDC host ready; ST-LINK console remains on USART1\n");
	usb_write_text("READY EF800-ISOLATION-CDC. Commands: START, FINISH, RESTART, STATUS, PING <text>, CRASH\r\n");

	for (;;) {
		ret = spike_usb_cdc_host_read(&byte);
		if (ret == 0) {
			if (byte == '\r') {
				continue;
			}
			if (byte == '\n') {
				handle_usb_command();
			} else if (usb_line_length < sizeof(usb_line) - 1U) {
				usb_line[usb_line_length++] = (char)byte;
			} else {
				usb_line_overflow = true;
			}
		}
		k_msleep(1);
	}
}
#endif

int main(void)
{
#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
	const struct device *fmc = DEVICE_DT_GET(DT_NODELABEL(fmc));
	const struct device *sram_dev = DEVICE_DT_GET(DT_NODELABEL(fmc_sram));
#endif
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
	k_tid_t worker;
	int ret;
#endif
	if (fatal_reboot_marker_matches_mode()) {
		printk("CONTROLLED_FATAL_REBOOT_CONFIRMED; test disarmed\n");
		while (true) {
			/* The fitted IWDG remains enabled across the application's
			 * system resets; keep the confirmation state alive until the
			 * test image is replaced. */
			IWDG->KR = 0xAAAAU;
			k_msleep(1000);
		}
	}

#if CONFIG_EF800_SPIKE_EXTERNAL_SRAM
	if (!device_is_ready(fmc) || !device_is_ready(sram_dev)) {
		printk("external SRAM unavailable: fmc=%u sram=%u\n",
			(unsigned int)device_is_ready(fmc),
			(unsigned int)device_is_ready(sram_dev));
		return -ENODEV;
	}
	/* SRAM requires at least 150 us after power-up before first access. */
	k_sleep(K_USEC(150));
	memset(plugin_ro, 0, SPIKE_REGION_SIZE);
	memset(plugin_rw, 0, SPIKE_REGION_SIZE);
	memset(plugin_unmapped, 0, SPIKE_REGION_SIZE);
	memset(&plugin_mailbox, 0, sizeof(plugin_mailbox));
	k_heap_init(&external_plugin_heap, plugin_rw, SPIKE_REGION_SIZE);
	plugin_rw_allocation = k_heap_alloc(&external_plugin_heap, 32U,
						    K_NO_WAIT);
	if (plugin_rw_allocation == NULL) {
		printk("external SRAM heap allocation failed\n");
		return -ENOMEM;
	}
	((uint8_t *)plugin_rw_allocation)[0] = 0x11U;
	if (((uint8_t *)plugin_rw_allocation)[0] != 0x11U) {
		printk("external SRAM heap allocation readback failed\n");
		return -EIO;
	}
	plugin_mailbox.lifecycle_entry = (uintptr_t)plugin_rw_allocation;
	printk("external SRAM partitions initialized at %p..%p\n",
		(void *)EF800_EXT_SRAM_BASE,
		(void *)(EF800_EXT_SRAM_BASE + 0x5000U));
	printk("external Zephyr heap allocation=%p size=32 readback=0x11\n",
		plugin_rw_allocation);
#endif

	printk("EF800 isolation spike\n");
	aux_uart_write("EF800 isolation auxiliary UART ready\r\n");
	report_spi_flash();
	printk("userspace=%d arm_mpu=%d xor_write=%d\n",
		IS_ENABLED(CONFIG_USERSPACE), IS_ENABLED(CONFIG_ARM_MPU),
		IS_ENABLED(CONFIG_EXECUTE_XOR_WRITE));
	printk("mpu_regions=%u min_align=%u stack_guard=%d\n",
		(unsigned int)((MPU->TYPE & MPU_TYPE_DREGION_Msk) >>
			MPU_TYPE_DREGION_Pos),
		(unsigned int)CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE,
		IS_ENABLED(CONFIG_MPU_STACK_GUARD));
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	printk("pm_text=%p/%u ro=%p/%u rw=%p/%u mailbox=%p/%u stack=%p/%u\n",
		plugin_loaded_text, (unsigned int)sizeof(plugin_loaded_text),
		plugin_ro, (unsigned int)SPIKE_REGION_SIZEOF(plugin_ro), plugin_rw,
		(unsigned int)SPIKE_REGION_SIZEOF(plugin_rw), &plugin_mailbox,
		(unsigned int)sizeof(plugin_mailbox), plugin_stack,
		(unsigned int)K_THREAD_STACK_SIZEOF(plugin_stack));
#else
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	printk("pm_text=%p/%u ro=%p/%u rw=%p/%u mailbox=%p/%u stack=%p/%u\n",
		plugin_loaded_text, (unsigned int)sizeof(plugin_loaded_text),
		plugin_ro, (unsigned int)SPIKE_REGION_SIZEOF(plugin_ro), plugin_rw,
		(unsigned int)SPIKE_REGION_SIZEOF(plugin_rw), &plugin_mailbox,
		(unsigned int)sizeof(plugin_mailbox), plugin_stack,
		(unsigned int)K_THREAD_STACK_SIZEOF(plugin_stack));
#else
	printk("text=%p/%u ro=%p/%u rw=%p/%u mailbox=%p/%u stack=%p/%u\n",
		__plugin_text_start, (unsigned int)SPIKE_REGION_SIZE,
		plugin_ro, (unsigned int)SPIKE_REGION_SIZEOF(plugin_ro), plugin_rw,
		(unsigned int)SPIKE_REGION_SIZEOF(plugin_rw), &plugin_mailbox,
		(unsigned int)sizeof(plugin_mailbox), plugin_stack,
		(unsigned int)K_THREAD_STACK_SIZEOF(plugin_stack));
#endif
#endif
	printk("fault_mode=%u protected_host_word=%p\n",
		(unsigned int)CONFIG_EF800_SPIKE_FAULT_MODE,
		(const void *)&protected_host_word);
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_USB_CDC_TEST
	printk("plugin text copied: LMA=%p VMA=%p size=%u first=0x%08x\n",
		__plugin_text_load_start, __plugin_text_start,
		(unsigned int)(__plugin_text_end - __plugin_text_start),
		(unsigned int)load_plugin_text());
#endif
#endif

#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_USB_CDC_TEST
	return run_usb_cdc_console();
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_PRIVILEGED_THREAD
	printk("injecting privileged-thread fault; worker supervisor must not be notified\n");
	__asm__ volatile("udf #0" ::: "memory");
	return -EFAULT;
#elif CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_ISR
	printk("injecting ISR fault; worker supervisor must not be notified\n");
	k_timer_start(&isr_fault_timer, K_MSEC(100), K_NO_WAIT);
	while (true) {
		k_sleep(K_FOREVER);
	}
#else
	worker = k_thread_create(&plugin_thread, plugin_stack,
		K_THREAD_STACK_SIZEOF(plugin_stack), plugin_worker, NULL, NULL, NULL,
		5, K_USER, K_FOREVER);
	if (worker == NULL) {
		printk("worker create failed\n");
		return -ENOMEM;
	}
	ret = install_plugin_domain();
	if (ret != 0) {
		printk("domain install failed: %d\n", ret);
		return ret;
	}

	printk("worker=%p domain=%p partitions=%u\n", &plugin_thread,
		&plugin_domain, (unsigned int)plugin_domain.num_partitions);
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_STACK_GUARD_WRITE
	if (plugin_thread.arch.priv_stack_start < MPU_GUARD_ALIGN_AND_SIZE) {
		printk("privileged stack guard address unavailable\n");
		return -EINVAL;
	}
	plugin_mailbox.guard_write_address =
		plugin_thread.arch.priv_stack_start - MPU_GUARD_ALIGN_AND_SIZE;
	printk("worker privileged-stack guard=%p size=%u\n",
		(void *)plugin_mailbox.guard_write_address,
		(unsigned int)MPU_GUARD_ALIGN_AND_SIZE);
#endif
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_UNMAPPED_PLUGIN_READ
	printk("unmapped plugin-sized probe=%p/%u\n", plugin_unmapped,
		(unsigned int)SPIKE_REGION_SIZEOF(plugin_unmapped));
#endif
#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
	plugin_fault_expected = true;
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_EXECUTE_XN
	/* A valid Thumb return instruction distinguishes execute-never from an
	 * invalid instruction if the XN mapping is accidentally permissive. */
	plugin_rw[0] = 0x70U;
	plugin_rw[1] = 0x47U;
#endif
#if CONFIG_EF800_SPIKE_FAULT_MODE == SPIKE_FAULT_MAILBOX_EXECUTE
	/* A valid Thumb return instruction proves the mailbox is XN. */
	plugin_mailbox.request[0] = 0x70U;
	plugin_mailbox.request[1] = 0x47U;
#endif
#endif
	k_thread_start(&plugin_thread);

#if CONFIG_EF800_SPIKE_FAULT_MODE != SPIKE_FAULT_NONE
	ret = k_sem_take(&plugin_fault_sem, K_SECONDS(5));
	if (ret != 0) {
		printk("worker fault was not reported: %d access_marker=0x%02x\n",
			ret, plugin_mailbox.values[3]);
		k_thread_abort(&plugin_thread);
		return ret;
	}
	printk("worker fault reported reason=%d pc=0x%08x; worker termination delegated to Zephyr\n",
		(int)atomic_get(&plugin_fault_reason),
		(unsigned int)atomic_get(&plugin_fault_pc));
	printk("worker was unprivileged=%u protected-access-marker=0x%02x\n",
		plugin_mailbox.values[4], plugin_mailbox.values[3]);
	return 0;
#else

	for (int i = 0; i < 200 && plugin_mailbox.values[2] != 0xa5U; i++) {
		k_msleep(10);
	}
	if (plugin_mailbox.values[2] != 0xa5U) {
		printk("worker completion failed\n");
		return -ETIMEDOUT;
	}

	printk("worker user=%d result=0x%02x PASS\n", plugin_mailbox.values[1],
		(unsigned int)plugin_mailbox.values[0]);
	k_thread_abort(&plugin_thread);
	return 0;
#endif
#endif
}
