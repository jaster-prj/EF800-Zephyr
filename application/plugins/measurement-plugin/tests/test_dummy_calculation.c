/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ef800_plugin_contract.h"
#include "plugin_manager/plugin_abi.h"

int pm_plugin_create(const struct pm_service_table *services, void **instance);
int pm_plugin_destroy(void *instance);

int main(void)
{
	static const char snapshot[] = "-STP 1.234 P001 \r\n-MEAS 2.000 P001 \r\n";
	struct pm_service_table services = {0};
	struct ef800_calculation_instance *instance;
	char output[128];
	size_t written = 0U;
	unsigned point;
	unsigned whole;
	unsigned fraction;
	unsigned code;

	assert(pm_plugin_create(&services, (void **)&instance) == PM_OK);
	assert(instance != NULL && instance->api != NULL);
	assert(instance->api->abi_version == EF800_CALCULATION_API_VERSION);
	assert(instance->api->transform(instance, snapshot, sizeof(snapshot) - 1U,
		output, sizeof(output), &written) == PM_OK);
	assert(written > 0U && written < sizeof(output));
	output[written] = '\0';
	assert(sscanf(output, "-D%5u %u.%3u R%5u \r\n", &point, &whole,
		&fraction, &code) == 4);
	assert(point < 100000U && whole < 500U && fraction < 1000U &&
		code < 100000U);
	assert(pm_plugin_destroy(instance) == PM_OK);
	return 0;
}
