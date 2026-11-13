/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Initialize and account for internal and external allocation pools. */
#ifndef EF800_MEMORY_POOL_H
#define EF800_MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>

/* Called before MainApplication foundation/plugin initialization. */

/** @brief Initialize allocation pools before foundation and plugin services.
 * @return 0 on success; negative errno if pool setup fails.
 */
int ef800_memory_pool_init(void);

/** @brief Check whether external memory is available for allocation.
 * @return True when the external pool was initialized.
 */
bool ef800_memory_pool_external_ready(void);

/** @brief Allocate aligned storage from the internal pool.
 * @param size Requested byte count.
 * @return Pointer to storage, or NULL when allocation fails.
 */
void *ef800_memory_pool_internal_alloc(size_t size);

/** @brief Return an internal-pool allocation.
 * @param ptr Pointer previously returned by the internal allocator.
 * @return None.
 */
void ef800_memory_pool_internal_free(void *ptr);

/** @brief Allocate storage from the external pool with the requested alignment.
 * @param size Requested byte count.
 * @param align Required alignment in bytes.
 * @return Pointer to storage, or NULL when unavailable/unsatisfied.
 */
void *ef800_memory_pool_external_alloc(size_t size, size_t align);

/** @brief Return an external-pool allocation.
 * @param ptr Pointer previously returned by the external allocator.
 * @return None.
 */
void ef800_memory_pool_external_free(void *ptr);

/** @brief Get total external-pool capacity.
 * @return Capacity in bytes.
 */
size_t ef800_memory_pool_external_capacity(void);

/** @brief Get currently allocated external-pool bytes.
 * @return Used capacity in bytes.
 */
size_t ef800_memory_pool_external_used(void);

/** @brief Get total internal-pool capacity.
 * @return Capacity in bytes.
 */
size_t ef800_memory_pool_internal_capacity(void);

#endif /* EF800_MEMORY_POOL_H */
