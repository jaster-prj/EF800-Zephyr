/* SPDX-License-Identifier: Apache-2.0 */
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

enum ef800_boot_target ef800_boot_decide(
	const struct ef800_boot_decision_input *input);

#endif /* EF800_BOOT_DECISION_H */
