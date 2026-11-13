/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Represent and update virtual target composition profiles. */
#ifndef EF800_VIRTUAL_PROFILE_H
#define EF800_VIRTUAL_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** @brief Decode and apply a profile-control payload to virtual hardware state.
 * @param payload Encoded profile payload.
 * @param length Payload byte count.
 * @return 0 on apply; negative errno for invalid profile or unsupported fields.
 */
int ef800_virtual_profile_apply(const uint8_t *payload, size_t length);

/** @brief Check whether a virtual device is available in the active profile.
 * @param device Stable virtual device identifier.
 * @return True when available.
 */
bool ef800_virtual_profile_available(uint8_t device);

/** @brief Get profile-overridden battery state of charge.
 * @return SOC percentage, or the configured unavailable value.
 */
uint8_t ef800_virtual_profile_battery_soc(void);

/** @brief Get profile-overridden battery voltage.
 * @return Voltage in microvolts.
 */
uint32_t ef800_virtual_profile_battery_voltage_uv(void);

/** @brief Check whether profile data overrides the battery reading.
 * @return True when a battery override is active.
 */
bool ef800_virtual_profile_battery_override_enabled(void);

#endif
