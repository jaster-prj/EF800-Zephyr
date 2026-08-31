/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PERSISTENCE_FAKE_H
#define EF800_PERSISTENCE_FAKE_H

#include "ef800_persistence.h"

#define EF800_PERSISTENCE_FAKE_FILE_COUNT 32U
#define EF800_PERSISTENCE_FAKE_FILE_SIZE 512U

struct ef800_persistence_fake_file {
	bool used;
	enum ef800_persistence_root root;
	char name[EF800_PERSISTENCE_MAX_NAME + 1U];
	uint8_t data[EF800_PERSISTENCE_FAKE_FILE_SIZE];
	size_t length;
};

struct ef800_persistence_fake {
	struct ef800_persistence_backend backend;
	struct ef800_persistence_fake_file files[EF800_PERSISTENCE_FAKE_FILE_COUNT];
};

void ef800_persistence_fake_init(struct ef800_persistence_fake *fake);

#endif /* EF800_PERSISTENCE_FAKE_H */
