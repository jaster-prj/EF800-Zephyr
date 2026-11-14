/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Select the main/backup firmware boot target. */
#ifndef EF800_BOOT_DECISION_H_
#define EF800_BOOT_DECISION_H_

#include <stdbool.h>

enum ef800_boot_target {
	EF800_BOOT_DECISION_MAIN,
	EF800_BOOT_DECISION_BACKUP,
	EF800_BOOT_DECISION_NONE,
};

struct ef800_boot_decision_input {
	bool main_valid;
	bool backup_valid;
	bool failure_limit_reached;
};

/** @brief Select an image using validity and the main-image failure limit.
 * @param input Image validity and recovery-threshold state.
 * @return Main, backup, or none when no image is bootable.
 */
enum ef800_boot_target ef800_boot_decide(
	const struct ef800_boot_decision_input *input);

#endif /* EF800_BOOT_DECISION_H */
