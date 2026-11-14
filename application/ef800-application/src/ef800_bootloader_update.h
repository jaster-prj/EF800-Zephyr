/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file
 * @brief Install a manually authorized EF800 Bootloader package.
 */
#ifndef EF800_BOOTLOADER_UPDATE_H_
#define EF800_BOOTLOADER_UPDATE_H_

#include "ef800_firmware_journal.h"

/**
 * @brief Journal callback invoked before each destructive update phase.
 *
 * @param context Caller-owned callback context.
 * @param phase Phase being entered.
 * @return 0 to continue; a negative errno aborts before that phase starts.
 */
typedef int (*ef800_bootloader_update_phase_fn)(
	void *context, enum ef800_firmware_journal_phase phase);

/**
 * @brief Validate and install a staged Bootloader package.
 *
 * @details The caller must hold exclusive ownership of the shared filesystem.
 * The function accepts only the L496 Bootloader target and fixed partition,
 * verifies the complete package before erase, verifies flash readback and
 * vectors after programming, and invokes the phase callback before mutation.
 * Package cleanup occurs only after the COMPLETE phase is durably recorded.
 *
 * @param package_path Staged package path.
 * @param coordinator_version Running EF800 Application version.
 * @param phase_callback Journal callback, required.
 * @param callback_context Callback context.
 * @return 0 after verified installation; negative errno on failure. A failed
 *         update leaves the staged package available for diagnosis/retry.
 */
int ef800_bootloader_update_install(
	const char *package_path,
	const struct ef800_firmware_semver *coordinator_version,
	ef800_bootloader_update_phase_fn phase_callback,
	void *callback_context);

#endif /* EF800_BOOTLOADER_UPDATE_H_ */
