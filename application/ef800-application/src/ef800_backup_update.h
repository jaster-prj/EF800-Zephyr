/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Install a validated EF800 Backup package from shared storage.
 */
#ifndef EF800_BACKUP_UPDATE_H_
#define EF800_BACKUP_UPDATE_H_

#include "ef800_firmware_package.h"

/**
 * @brief Validate, install, and verify the staged EF800 Backup package.
 *
 * @details The package at @p package_path must target EF800 Backup on an
 * STM32L496 and require no coordinator newer than the running EF800
 * Application. The installer checks the package header and payload CRC before
 * erasing, writes only the fixed Backup partition, verifies each programmed
 * chunk, and checks the installed vector table. The package is removed only
 * after successful verification. The caller must hold exclusive ownership of
 * the shared filesystem and must ensure USB MSC is not using it.
 *
 * @param package_path NUL-terminated path to the staged package.
 * @param coordinator_version Version of the running EF800 Application.
 * @return 0 after verified installation and package cleanup; negative errno on
 *         validation, storage, or flash errors. A failure leaves the package
 *         available for retry/diagnosis.
 */
int ef800_backup_update_install(
	const char *package_path,
	const struct ef800_firmware_semver *coordinator_version);

#endif /* EF800_BACKUP_UPDATE_H_ */
