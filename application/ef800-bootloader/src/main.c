/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include <cmsis_core.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if CONFIG_HWINFO
#include <zephyr/drivers/hwinfo.h>
#endif

#include "ef800_update.h"
#include "ef800_metadata.h"
#include "ef800_console.h"
#include "boot_update.h"
#include "ef800_boot_decision.h"
#include "ef800_watchdog.h"

LOG_MODULE_REGISTER(ef800_bootloader, LOG_LEVEL_INF);

#define MAIN_PARTITION_ID PARTITION_ID(main_partition)
#define BACKUP_PARTITION_ID PARTITION_ID(backup_partition)
#define QSPI_BOOTSTATE_PARTITION_ID PARTITION_ID(qspi_bootstate_partition)
#define QSPI_CONFIGURATION_PARTITION_ID PARTITION_ID(qspi_configuration_partition)
#define QSPI_DATA_PARTITION_ID PARTITION_ID(qspi_storage_partition)
#define EF800_VECTOR_BYTES (2U * sizeof(uint32_t))
#define QSPI_ERASE_BLOCK_SIZE 0x00001000U

BUILD_ASSERT(PARTITION_EXISTS(main_partition), "main partition is required");
BUILD_ASSERT(PARTITION_EXISTS(backup_partition), "backup partition is required");
BUILD_ASSERT(PARTITION_EXISTS(metadata_partition), "external boot metadata NVS is required");
BUILD_ASSERT(PARTITION_EXISTS(qspi_bootstate_partition), "QSPI boot-state partition is required");
BUILD_ASSERT(PARTITION_EXISTS(qspi_configuration_partition), "QSPI configuration partition is required");
BUILD_ASSERT(PARTITION_EXISTS(qspi_storage_partition), "QSPI shared-data partition is required");
BUILD_ASSERT(PARTITION_OFFSET(boot_partition) == 0x00000000U,
	     "bootloader partition offset must remain 0x00000000");
BUILD_ASSERT(PARTITION_SIZE(boot_partition) == 0x00010000U,
	     "bootloader partition size must remain 64 KiB");
BUILD_ASSERT(PARTITION_OFFSET(backup_partition) == 0x00010000U,
	     "backup partition offset must remain 0x00010000");
BUILD_ASSERT(PARTITION_SIZE(backup_partition) == 0x00020000U,
	     "backup partition size must remain 128 KiB");
BUILD_ASSERT(PARTITION_OFFSET(main_partition) ==
	     PARTITION_OFFSET(backup_partition) + PARTITION_SIZE(backup_partition),
	     "MainApplication must follow the expanded BackupApplication region");
BUILD_ASSERT(PARTITION_OFFSET(qspi_bootstate_partition) == 0x00000000U &&
	     PARTITION_SIZE(qspi_bootstate_partition) == 0x00008000U,
	     "QSPI boot-state partition must be the first 32 KiB");
BUILD_ASSERT(PARTITION_OFFSET(metadata_partition) == 0x00000000U &&
	     PARTITION_SIZE(metadata_partition) == 0x00008000U,
	     "metadata NVS must alias the QSPI boot-state partition");
BUILD_ASSERT(PARTITION_OFFSET(qspi_configuration_partition) == 0x00008000U &&
	     PARTITION_SIZE(qspi_configuration_partition) == 0x000F8000U,
	     "QSPI configuration partition must be 992 KiB after boot-state NVS");
BUILD_ASSERT(PARTITION_OFFSET(qspi_storage_partition) == 0x00100000U &&
	     PARTITION_SIZE(qspi_storage_partition) == 0x00300000U,
	     "QSPI shared-data partition must be the final 3 MiB");
BUILD_ASSERT((PARTITION_OFFSET(qspi_bootstate_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U &&
	     (PARTITION_SIZE(qspi_bootstate_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U &&
	     (PARTITION_OFFSET(qspi_configuration_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U &&
	     (PARTITION_SIZE(qspi_configuration_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U &&
	     (PARTITION_OFFSET(qspi_storage_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U &&
	     (PARTITION_SIZE(qspi_storage_partition) % QSPI_ERASE_BLOCK_SIZE) == 0U,
	     "all QSPI partitions must align to the 4 KiB erase granularity");
BUILD_ASSERT(PARTITION_OFFSET(main_partition) == 0x00030000U,
	     "main partition offset must remain 0x00030000");
BUILD_ASSERT(PARTITION_SIZE(main_partition) == 0x000D0000U,
	     "main partition size must remain 832 KiB");

static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

static int read_vector(const struct flash_area *area, uint32_t vector[2])
{
	if (!flash_area_device_is_ready(area) || area->fa_size < EF800_VECTOR_BYTES) {
		return -ENODEV;
	}

	return flash_area_read(area, 0, vector, EF800_VECTOR_BYTES);
}

static bool partition_is_valid(const struct flash_area *area, uintptr_t address)
{
	uint32_t vector[2];

	if (read_vector(area, vector) != 0) {
		return false;
	}

	return ef800_vector_table_valid(vector, address, area->fa_size,
					 sram_address, sram_size);
}

static bool reset_was_watchdog(void)
{
#if CONFIG_HWINFO
	uint32_t cause;
	bool watchdog = false;

	if (hwinfo_get_reset_cause(&cause) == 0) {
		watchdog = (cause & RESET_WATCHDOG) != 0U;
		/* Prevent accumulated reset flags from counting the same failure twice. */
		(void)hwinfo_clear_reset_cause();
	}
	return watchdog;
#else
	return false;
#endif
}

__attribute__((naked, noreturn)) static void branch_to_application(
	uint32_t stack_pointer, uint32_t reset_handler)
{
	__asm__ volatile (
		"msr msp, r0\n"
		"movs r0, #0\n"
		"msr control, r0\n"
		"msr basepri, r0\n"
		"msr faultmask, r0\n"
		"isb\n"
		"cpsie i\n"
		"bx r1\n"
	);
}

static FUNC_NORETURN void jumpApplication(const struct flash_area *area, uintptr_t address)
{
	uint32_t vector[2];

	if (read_vector(area, vector) != 0 ||
		!ef800_vector_table_valid(vector, address, area->fa_size,
					  sram_address, sram_size)) {
		LOG_ERR("refusing jumpApplication to invalid image at 0x%lx",
			(unsigned long)address);
		while (true) {
			k_sleep(K_FOREVER);
		}
	}
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	for (size_t index = 0U; index < ARRAY_SIZE(NVIC->ICER); ++index) {
		NVIC->ICER[index] = UINT32_MAX;
		NVIC->ICPR[index] = UINT32_MAX;
	}
	MPU->CTRL = 0U;
	SCB->CFSR = SCB->CFSR;
	SCB->HFSR = SCB->HFSR;
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	branch_to_application(vector[0], vector[1]);
}

int main(void)
{
	int watchdog_result;
	int console_result;
	const struct flash_area *main_area;
	const struct flash_area *backup_area;
	struct ef800_boot_metadata metadata __aligned(8);
	bool metadata_valid = false;
	bool metadata_dirty = false;
	bool watchdog_reset = false;
	bool main_valid;
	bool backup_valid;
	bool use_backup = false;
	bool update_installed = false;
	int rc;

	/* Start/feed the option-byte IWDG before slow flash and metadata work. */
	watchdog_result = ef800_watchdog_start(10000U);
	console_result = ef800_console_init(true);
	printk("ef800 bootloader: main (console=%u, init=%d)\r\n",
		ef800_console_get_target(), console_result);
	if (watchdog_result != 0) {
		LOG_WRN("IWDG could not be initialized at boot");
	}

	rc = flash_area_open(MAIN_PARTITION_ID, &main_area);
	if (rc < 0 || !flash_area_device_is_ready(main_area)) {
		LOG_ERR("main partition unavailable: %d", rc);
		return 0;
	}
	rc = flash_area_open(BACKUP_PARTITION_ID, &backup_area);
	if (rc < 0 || !flash_area_device_is_ready(backup_area)) {
		LOG_ERR("backup partition unavailable: %d", rc);
		return 0;
	}

	rc = ef800_metadata_read_current(&metadata, &metadata_valid);
	if (rc < 0 || !metadata_valid) {
		LOG_WRN("boot-state NVS unavailable; using volatile defaults "
			"(failure counter unavailable): %d", rc);
		ef800_boot_metadata_init(&metadata);
		metadata_valid = false;
		metadata_dirty = true;
	}

	if (metadata_valid) {
		watchdog_reset = reset_was_watchdog();
		if (metadata.last_target == EF800_BOOT_TARGET_BACKUP) {
			/* Backup is a recovery boot, not a permanent boot target. */
			metadata.main_failures = 0U;
			metadata.last_target = EF800_BOOT_TARGET_MAIN;
			metadata_dirty = true;
			LOG_INF("backup recovery boot complete; retrying main application");
		} else if (watchdog_reset &&
			metadata.last_target == EF800_BOOT_TARGET_MAIN &&
			metadata.main_failures < UINT32_MAX) {
			metadata.main_failures++;
			metadata_dirty = true;
			LOG_WRN("main watchdog failure %u/%u", metadata.main_failures,
				CONFIG_EF800_BOOT_MAIN_FAILURE_LIMIT);
		}
	}

	rc = ef800_boot_install_update(main_area, PARTITION_ADDRESS(main_partition),
					 sram_address, sram_size);
	update_installed = rc == 0;
	if (rc < 0 && rc != -ENOENT && rc != -ENODEV) {
		LOG_WRN("update inspection failed: %d", rc);
	}
	if (update_installed) {
		metadata.main_failures = 0U;
		metadata.last_target = EF800_BOOT_TARGET_MAIN;
		metadata_dirty = true;
	}

	main_valid = partition_is_valid(main_area, PARTITION_ADDRESS(main_partition));
	backup_valid = partition_is_valid(backup_area,
					 PARTITION_ADDRESS(backup_partition));

	use_backup = ef800_boot_decide(&(struct ef800_boot_decision_input){
		.main_valid = main_valid,
		.backup_valid = backup_valid,
		.failure_limit_reached = metadata.last_target == EF800_BOOT_TARGET_MAIN &&
			metadata.main_failures >=
			CONFIG_EF800_BOOT_MAIN_FAILURE_LIMIT,
	}) == EF800_BOOT_DECISION_BACKUP;

	if (use_backup && backup_valid) {
		LOG_WRN("main failure limit reached; starting backup");
		metadata.last_target = EF800_BOOT_TARGET_BACKUP;
		metadata_dirty = true;
		if (metadata_dirty) {
			rc = ef800_metadata_write_current(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
		jumpApplication(backup_area, PARTITION_ADDRESS(backup_partition));
	}

	if (main_valid) {
		/* A non-watchdog reset is the only safe indication that main ran. */
		if (!watchdog_reset && metadata.main_failures != 0U) {
			metadata.main_failures = 0U;
			metadata_dirty = true;
		}
		if (metadata.last_target != EF800_BOOT_TARGET_MAIN) {
			metadata.last_target = EF800_BOOT_TARGET_MAIN;
			metadata_dirty = true;
		}
		if (metadata_dirty) {
			rc = ef800_metadata_write_current(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
		LOG_INF("starting main application");
		jumpApplication(main_area, PARTITION_ADDRESS(main_partition));
	}

	if (backup_valid) {
		if (metadata.last_target != EF800_BOOT_TARGET_BACKUP) {
			metadata.last_target = EF800_BOOT_TARGET_BACKUP;
			metadata_dirty = true;
		}
		if (metadata_dirty) {
			rc = ef800_metadata_write_current(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
		LOG_WRN("main application invalid; starting backup");
		jumpApplication(backup_area, PARTITION_ADDRESS(backup_partition));
	}

	LOG_ERR("no valid main or backup application");
	while (true) {
		k_sleep(K_FOREVER);
	}
}
