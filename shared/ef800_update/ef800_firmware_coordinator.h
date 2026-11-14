/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Testable sequencing policy for the six-target firmware update set.
 */
#ifndef EF800_FIRMWARE_COORDINATOR_H
#define EF800_FIRMWARE_COORDINATOR_H

#include <stdbool.h>

#include "ef800_firmware_package.h"

enum ef800_firmware_coordinator_status {
	EF800_FIRMWARE_COORDINATOR_SKIPPED,
	EF800_FIRMWARE_COORDINATOR_COMPLETE,
	EF800_FIRMWARE_COORDINATOR_FAILED,
	EF800_FIRMWARE_COORDINATOR_UNSUPPORTED,
};

struct ef800_firmware_coordinator {
	void *context;
	/* Called before any staged-file access; false means an owner blocks work. */
	bool (*ready)(void *context);
	int (*package_present)(void *context, const char *path, bool *present);
	int (*run_target)(void *context, enum ef800_firmware_target_id target,
			  const char *path);
	int (*report)(void *context, enum ef800_firmware_target_id target,
		      const char *path,
		      enum ef800_firmware_coordinator_status status, int result);
};

/**
 * Process the six package paths in their fixed dependency order.
 *
 * Missing packages are reported as skipped. Unsupported targets are reported
 * and do not block later independent targets. Any other error stops the run.
 * The caller retains ownership of files and durable result recording.
 */
int ef800_firmware_coordinate(
	const struct ef800_firmware_coordinator *coordinator,
	const char *const package_paths[6]);

#endif /* EF800_FIRMWARE_COORDINATOR_H */
