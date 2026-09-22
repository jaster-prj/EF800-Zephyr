/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_boot_decision.h"

#include <stddef.h>

enum ef800_boot_target ef800_boot_decide(
	const struct ef800_boot_decision_input *input)
{
	if (input == NULL) {
		return EF800_BOOT_DECISION_NONE;
	}
	if (input->failure_limit_reached && input->backup_valid) {
		return EF800_BOOT_DECISION_BACKUP;
	}
	if (input->main_valid) {
		return EF800_BOOT_DECISION_MAIN;
	}
	if (input->backup_valid) {
		return EF800_BOOT_DECISION_BACKUP;
	}
	return EF800_BOOT_DECISION_NONE;
}
