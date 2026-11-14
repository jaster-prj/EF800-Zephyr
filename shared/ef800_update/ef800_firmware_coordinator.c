/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_firmware_coordinator.h"

#include <errno.h>

#include <zephyr/sys/util.h>

int ef800_firmware_coordinate(
	const struct ef800_firmware_coordinator *coordinator,
	const char *const package_paths[6])
{
	static const enum ef800_firmware_target_id targets[] = {
		EF800_FIRMWARE_TARGET_APPLICATION,
		EF800_FIRMWARE_TARGET_BACKUP,
		EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER,
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER,
		EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION,
		EF800_FIRMWARE_TARGET_BOOTLOADER,
	};

	if (coordinator == NULL || package_paths == NULL ||
	    coordinator->ready == NULL ||
	    coordinator->package_present == NULL ||
	    coordinator->run_target == NULL || coordinator->report == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(targets); ++i) {
		if (package_paths[i] == NULL) {
			return -EINVAL;
		}
	}
	if (!coordinator->ready(coordinator->context)) {
		return -EAGAIN;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(targets); ++i) {
		bool present = false;
		int result = coordinator->package_present(coordinator->context,
			package_paths[i], &present);

		if (result != 0) {
			int report_result = coordinator->report(coordinator->context,
				targets[i], package_paths[i],
				EF800_FIRMWARE_COORDINATOR_FAILED, result);

			return report_result != 0 ? report_result : result;
		}
		if (!present) {
			result = coordinator->report(coordinator->context, targets[i],
				NULL, EF800_FIRMWARE_COORDINATOR_SKIPPED, 0);
			if (result != 0) {
				return result;
			}
			continue;
		}

		result = coordinator->run_target(coordinator->context, targets[i],
			package_paths[i]);
		if (result == -ENOTSUP) {
			result = coordinator->report(coordinator->context, targets[i],
				package_paths[i],
				EF800_FIRMWARE_COORDINATOR_UNSUPPORTED, result);
			if (result != 0) {
				return result;
			}
			continue;
		}
		if (result != 0) {
			int report_result = coordinator->report(coordinator->context,
				targets[i], package_paths[i],
				EF800_FIRMWARE_COORDINATOR_FAILED, result);

			return report_result != 0 ? report_result : result;
		}
		result = coordinator->report(coordinator->context, targets[i],
			NULL, EF800_FIRMWARE_COORDINATOR_COMPLETE, 0);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}
