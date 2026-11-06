/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_profile.h"

bool ef800_virtual_battery_available(void)
{
	return ef800_virtual_profile_available(4U);
}

uint8_t ef800_battery_profile_soc(void)
{
	return ef800_virtual_profile_battery_soc();
}

uint32_t ef800_battery_profile_voltage_uv(void)
{
	return ef800_virtual_profile_battery_voltage_uv();
}

bool ef800_battery_profile_override_enabled(void)
{
	return ef800_virtual_profile_battery_override_enabled();
}
