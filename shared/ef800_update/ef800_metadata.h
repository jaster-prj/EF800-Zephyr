/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_METADATA_H
#define EF800_METADATA_H

#include <stdbool.h>

#include "ef800_update.h"

struct flash_area;

#define EF800_METADATA_NVS_ID 1U

int ef800_metadata_read(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid);
int ef800_metadata_write(const struct flash_area *area,
				struct ef800_boot_metadata *metadata);
int ef800_metadata_read_current(struct ef800_boot_metadata *metadata,
					bool *valid);
int ef800_metadata_write_current(struct ef800_boot_metadata *metadata);

#ifdef EF800_TEST_METADATA
/* Reset only the adapter's in-memory NVS cache between isolated test cases. */
void ef800_metadata_test_forget_state(void);
#endif

#endif /* EF800_METADATA_H */
