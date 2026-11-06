/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_PROFILE_H
#define EF800_VIRTUAL_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

int ef800_virtual_profile_apply(const uint8_t *payload, size_t length);
bool ef800_virtual_profile_available(uint8_t device);
uint8_t ef800_virtual_profile_battery_soc(void);
uint32_t ef800_virtual_profile_battery_voltage_uv(void);
bool ef800_virtual_profile_battery_override_enabled(void);

#endif
