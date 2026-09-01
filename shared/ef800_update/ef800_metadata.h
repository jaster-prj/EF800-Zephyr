/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_METADATA_H
#define EF800_METADATA_H

#include <stdbool.h>

#include "ef800_update.h"

struct flash_area;

int ef800_metadata_read(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid);
int ef800_metadata_write(const struct flash_area *area,
				struct ef800_boot_metadata *metadata);
int ef800_metadata_read_current(struct ef800_boot_metadata *metadata,
					bool *valid);
int ef800_metadata_write_current(struct ef800_boot_metadata *metadata);

#endif /* EF800_METADATA_H */
