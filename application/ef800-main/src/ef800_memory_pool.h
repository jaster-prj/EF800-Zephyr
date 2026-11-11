/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MEMORY_POOL_H
#define EF800_MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>

/* Called before MainApplication foundation/plugin initialization. */
int ef800_memory_pool_init(void);
bool ef800_memory_pool_external_ready(void);
void *ef800_memory_pool_internal_alloc(size_t size);
void ef800_memory_pool_internal_free(void *ptr);
void *ef800_memory_pool_external_alloc(size_t size, size_t align);
void ef800_memory_pool_external_free(void *ptr);
size_t ef800_memory_pool_external_capacity(void);
size_t ef800_memory_pool_external_used(void);
size_t ef800_memory_pool_internal_capacity(void);

#endif /* EF800_MEMORY_POOL_H */
