/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_memory_pool.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#define EF800_POOL_TRACKED_ALLOCATION_COUNT 16U

struct tracked_allocation {
	void *ptr;
	size_t size;
};

static struct tracked_allocation external_allocations[
	EF800_POOL_TRACKED_ALLOCATION_COUNT];
static size_t external_used;
K_MUTEX_DEFINE(external_pool_lock);

#if defined(CONFIG_BOARD_EF800)
#define EF800_EXT_SRAM_BASE 0x60000000U
#define EF800_EXT_SRAM_SIZE 0x00080000U
#define EF800_SRAM_TEST_WORDS 32U

static struct k_heap external_heap;
static bool external_ready;

static int external_sram_validate(void)
{
	const struct device *fmc = DEVICE_DT_GET(DT_NODELABEL(fmc));
	const struct device *sram = DEVICE_DT_GET(DT_NODELABEL(fmc_sram));
	volatile uint32_t *const test_words =
		(volatile uint32_t *)(uintptr_t)(EF800_EXT_SRAM_BASE +
			EF800_EXT_SRAM_SIZE - sizeof(uint32_t) * EF800_SRAM_TEST_WORDS);
	uint32_t saved[EF800_SRAM_TEST_WORDS];
	int result = 0;

#if defined(CONFIG_EF800_APPLICATION_SRAM_TEST_FAIL) && \
	CONFIG_EF800_APPLICATION_SRAM_TEST_FAIL
	return -EIO;
#endif

	if (!device_is_ready(fmc) || !device_is_ready(sram)) {
		return -ENODEV;
	}

	/* IS61 power-up initialization interval, before touching the device. */
	k_sleep(K_USEC(150));
	for (size_t i = 0U; i < EF800_SRAM_TEST_WORDS; ++i) {
		saved[i] = test_words[i];
	}
	for (size_t i = 0U; i < EF800_SRAM_TEST_WORDS; ++i) {
		test_words[i] = 0xA5C30000U ^ (uint32_t)i;
	}
	for (size_t i = 0U; i < EF800_SRAM_TEST_WORDS; ++i) {
		if (test_words[i] != (0xA5C30000U ^ (uint32_t)i)) {
			result = -EIO;
			break;
		}
	}
	for (size_t i = 0U; i < EF800_SRAM_TEST_WORDS; ++i) {
		test_words[i] = saved[i];
	}
	return result;
}
#endif

int ef800_memory_pool_init(void)
{
#if defined(CONFIG_BOARD_EF800)
	int result = external_sram_validate();

	if (result != 0) {
		external_ready = false;
		return result;
	}
	k_heap_init(&external_heap, (void *)(uintptr_t)EF800_EXT_SRAM_BASE,
		    EF800_EXT_SRAM_SIZE);
	external_ready = true;
	return 0;
#else
	return -ENOTSUP;
#endif
}

bool ef800_memory_pool_external_ready(void)
{
#if defined(CONFIG_BOARD_EF800)
	return external_ready;
#else
	return false;
#endif
}

void *ef800_memory_pool_internal_alloc(size_t size)
{
	return malloc(size);
}

void ef800_memory_pool_internal_free(void *ptr)
{
	free(ptr);
}

void *ef800_memory_pool_external_alloc(size_t size, size_t align)
{
#if defined(CONFIG_BOARD_EF800)
	void *ptr;
	size_t free_slot = EF800_POOL_TRACKED_ALLOCATION_COUNT;

	if (!external_ready) {
		return NULL;
	}
	ptr = k_heap_aligned_alloc(&external_heap, align, size, K_NO_WAIT);
	if (ptr == NULL || k_mutex_lock(&external_pool_lock, K_FOREVER) != 0) {
		if (ptr != NULL) {
			k_heap_free(&external_heap, ptr);
		}
		return NULL;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(external_allocations); ++i) {
		if (external_allocations[i].ptr == NULL) {
			free_slot = i;
			break;
		}
	}
	if (free_slot == ARRAY_SIZE(external_allocations)) {
		(void)k_mutex_unlock(&external_pool_lock);
		k_heap_free(&external_heap, ptr);
		return NULL;
	}
	external_allocations[free_slot] = (struct tracked_allocation){
		.ptr = ptr,
		.size = size,
	};
	external_used += size;
	(void)k_mutex_unlock(&external_pool_lock);
	return ptr;
#else
	ARG_UNUSED(size);
	ARG_UNUSED(align);
	return NULL;
#endif
}

void ef800_memory_pool_external_free(void *ptr)
{
#if defined(CONFIG_BOARD_EF800)
	if (external_ready && ptr != NULL) {
		if (k_mutex_lock(&external_pool_lock, K_FOREVER) == 0) {
			for (size_t i = 0U; i < ARRAY_SIZE(external_allocations); ++i) {
				if (external_allocations[i].ptr == ptr) {
					external_used -= external_allocations[i].size;
					external_allocations[i] = (struct tracked_allocation){0};
					break;
				}
			}
			(void)k_mutex_unlock(&external_pool_lock);
		}
		k_heap_free(&external_heap, ptr);
	}
#else
	ARG_UNUSED(ptr);
#endif
}

size_t ef800_memory_pool_external_capacity(void)
{
#if defined(CONFIG_BOARD_EF800)
	return external_ready ? EF800_EXT_SRAM_SIZE : 0U;
#else
	return 0U;
#endif
}

size_t ef800_memory_pool_external_used(void)
{
#if defined(CONFIG_BOARD_EF800)
	size_t used;

	if (!external_ready || k_mutex_lock(&external_pool_lock, K_FOREVER) != 0) {
		return 0U;
	}
	used = external_used;
	(void)k_mutex_unlock(&external_pool_lock);
	return used;
#else
	return 0U;
#endif
}

size_t ef800_memory_pool_internal_capacity(void)
{
#if defined(CONFIG_BOARD_EF800)
	return CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE;
#else
	return 0U;
#endif
}
