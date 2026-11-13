/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Validate RTC values and convert between local time and UTC. */
#ifndef EF800_TIME_H
#define EF800_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/rtc.h>

struct ef800_time_settings {
	int32_t utc_offset_minutes;
	bool daylight_saving;
};

/** @brief Validate calendar fields and their month/day relationships.
 * @param time RTC calendar value to check.
 * @return True when all fields represent a valid calendar time.
 */
bool ef800_rtc_time_valid(const struct rtc_time *time);

/** @brief Parse a signed UTC offset in hours or HH:MM form.
 * @param text NUL-terminated offset text.
 * @param offset_minutes Receives signed offset in minutes.
 * @return 0 on parse; negative errno for malformed or out-of-range text.
 */
int ef800_parse_utc_offset(const char *text, int32_t *offset_minutes);

/** @brief Convert local calendar time to UTC using offset and DST settings.
 * @param local Local calendar value.
 * @param settings UTC offset and daylight-saving configuration.
 * @param utc Receives converted UTC calendar value.
 * @return 0 on conversion; negative errno for invalid input/time.
 */
int ef800_local_time_to_utc(const struct rtc_time *local,
				   const struct ef800_time_settings *settings,
				   struct rtc_time *utc);

/** @brief Convert UTC calendar time to local time using offset and DST settings.
 * @param utc UTC calendar value.
 * @param settings UTC offset and daylight-saving configuration.
 * @param local Receives converted local calendar value.
 * @return 0 on conversion; negative errno for invalid input/time.
 */
int ef800_utc_time_to_local(const struct rtc_time *utc,
					const struct ef800_time_settings *settings,
					struct rtc_time *local);

#endif /* EF800_TIME_H */
