/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>

#include "ef800_plugin_contract.h"
#include "plugin_manager/plugin_abi.h"

#define DUMMY_STP_PREFIX "D"
#define DUMMY_CODE_PREFIX "R"

static struct ef800_calculation_instance instance;

static void append_digits(char *output, size_t *offset, uint32_t value,
	size_t width)
{
	while (width != 0U) {
		uint32_t divisor = 1U;

		for (size_t index = 1U; index < width; ++index) {
			divisor *= 10U;
		}
		output[(*offset)++] = (char)('0' + (value / divisor) % 10U);
		--width;
	}
}

static uint32_t next_random(uint32_t *state)
{
	uint32_t value = *state;

	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	*state = value;
	return value;
}

/* cppcheck-suppress constParameterCallback -- calculation API v1 callback fixes void * */
static int32_t transform(void *opaque, const char *snapshot, size_t length,
	char *output, size_t capacity, size_t *written)
{
	uint32_t state = 2166136261u;
	uint32_t point;
	uint32_t height;
	size_t offset = 0U;

	if (opaque != &instance || snapshot == NULL || length == 0U ||
		output == NULL || written == NULL) {
		return PM_EINVAL;
	}
	/* Seed the dummy generator from the selected Workspace, so a different
	 * Workspace produces a different plausible position without host services. */
	for (size_t index = 0U; index < length; ++index) {
		state = (state ^ (uint8_t)snapshot[index]) * 16777619u;
	}
	if (state == 0U) {
		state = 0x6d2b79f5u;
	}
	point = next_random(&state) % 100000u;
	height = next_random(&state) % 500000u;
	if (capacity < 25U) {
		return PM_EOVERFLOW;
	}
	output[offset++] = '-';
	output[offset++] = 'D';
	append_digits(output, &offset, point, 5U);
	output[offset++] = ' ';
	append_digits(output, &offset, height / 1000U, 3U);
	output[offset++] = '.';
	append_digits(output, &offset, height % 1000U, 3U);
	output[offset++] = ' ';
	output[offset++] = 'R';
	append_digits(output, &offset, next_random(&state) % 100000U, 5U);
	output[offset++] = ' ';
	output[offset++] = '\r';
	output[offset++] = '\n';
	*written = offset;
	return PM_OK;
}

static const struct ef800_calculation_api api = {
	.abi_version = EF800_CALCULATION_API_VERSION,
	.struct_size = sizeof(api),
	.transform = transform,
	.reserved = {0U},
};

int pm_plugin_create(const struct pm_service_table *services, void **created)
{
	if (services == NULL || created == NULL || services->count != 0U) {
		return PM_EINVAL;
	}
	if (instance.api != NULL) {
		return PM_EBUSY;
	}
	instance.api = &api;
	*created = &instance;
	return PM_OK;
}

/* cppcheck-suppress constParameterPointer -- plugin manager ABI requires void * */
int pm_plugin_destroy(void *created)
{
	if (created != &instance || instance.api != &api) {
		return PM_EINVAL;
	}
	instance.api = NULL;
	return PM_OK;
}
