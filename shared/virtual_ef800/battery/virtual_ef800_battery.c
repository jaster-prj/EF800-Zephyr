/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_profile.h"

bool virtual_ef800_battery_available(void)
{
	return virtual_ef800_profile_available(4U);
}

uint8_t ef800_battery_profile_soc(void)
{
	return virtual_ef800_profile_battery_soc();
}

uint32_t ef800_battery_profile_voltage_uv(void)
{
	return virtual_ef800_profile_battery_voltage_uv();
}

bool ef800_battery_profile_override_enabled(void)
{
	return virtual_ef800_profile_battery_override_enabled();
}
