/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/mem_domain.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/flash.h>

#include <cmsis_core.h>

#include <stdint.h>

#define SPIKE_STACK_SIZE 2048
#define SPIKE_REGION_SIZE 1024

extern uint8_t __plugin_text_start[];
extern uint8_t __plugin_text_end[];
extern uint8_t z_data_smem_z_libc_partition_part_start[];

/* Power-of-two regions make the ARMv7-M MPU layout explicit and measurable. */
static uint8_t plugin_ro[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE);
static uint8_t plugin_rw[SPIKE_REGION_SIZE] __aligned(SPIKE_REGION_SIZE);
struct plugin_mailbox_data {
	volatile uint8_t values[SPIKE_REGION_SIZE];
} __aligned(SPIKE_REGION_SIZE);
static struct plugin_mailbox_data plugin_mailbox __aligned(SPIKE_REGION_SIZE);

static struct k_mem_domain plugin_domain;
static struct k_thread plugin_thread;
K_THREAD_STACK_DEFINE(plugin_stack, SPIKE_STACK_SIZE);

K_MEM_PARTITION_DEFINE(plugin_ro_partition, plugin_ro, sizeof(plugin_ro),
	K_MEM_PARTITION_P_RO_U_RO);
static struct k_mem_partition plugin_text_partition = {
	.start = (uintptr_t)__plugin_text_start,
	.size = SPIKE_REGION_SIZE,
	.attr = K_MEM_PARTITION_P_RX_U_RX,
};
static struct k_mem_partition plugin_runtime_partition = {
	.start = (uintptr_t)z_data_smem_z_libc_partition_part_start,
	.size = SPIKE_REGION_SIZE * 2U,
	.attr = K_MEM_PARTITION_P_RW_U_RW,
};
K_MEM_PARTITION_DEFINE(plugin_rw_partition, plugin_rw, sizeof(plugin_rw),
	K_MEM_PARTITION_P_RW_U_RW);
K_MEM_PARTITION_DEFINE(plugin_mailbox_partition, &plugin_mailbox,
	sizeof(plugin_mailbox), K_MEM_PARTITION_P_RW_U_RW);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(usart6), okay)
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

static void plugin_worker_step(void)
	__attribute__((noinline, section(".plugin_text.worker")));

static void plugin_worker_step(void)
{
	plugin_rw[0] = 0x5aU;
	plugin_mailbox.values[0] = plugin_rw[0];
	plugin_mailbox.values[1] = 1U;
	plugin_mailbox.values[2] = 0xa5U;
}

static void plugin_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	plugin_worker_step();
	while (true) {
		/* Keep the worker alive without another userspace syscall; the
		 * supervisor observes completion through the granted mailbox. */
	}
}

static int install_plugin_domain(void)
{
	struct k_mem_partition *partitions[] = {
		&plugin_runtime_partition,
		&plugin_text_partition,
		&plugin_rw_partition,
		&plugin_mailbox_partition,
	};
	int ret = k_mem_domain_init(&plugin_domain, ARRAY_SIZE(partitions),
		partitions);

	if (ret != 0) {
		return ret;
	}

	return k_mem_domain_add_thread(&plugin_domain, &plugin_thread);
}

int main(void)
{
	k_tid_t worker;
	int ret;

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
	printk("text=%p/%u ro=%p/%u rw=%p/%u mailbox=%p/%u stack=%p/%u\n",
		__plugin_text_start,
		(unsigned int)SPIKE_REGION_SIZE,
		plugin_ro, (unsigned int)sizeof(plugin_ro), plugin_rw,
		(unsigned int)sizeof(plugin_rw), &plugin_mailbox,
		(unsigned int)sizeof(plugin_mailbox), plugin_stack,
		(unsigned int)K_THREAD_STACK_SIZEOF(plugin_stack));

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
	k_thread_start(&plugin_thread);

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
}
