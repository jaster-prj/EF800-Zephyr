/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_TIME_H
#define EF800_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/rtc.h>

struct ef800_time_settings {
	int32_t utc_offset_minutes;
	bool daylight_saving;
};

bool ef800_rtc_time_valid(const struct rtc_time *time);
int ef800_local_time_to_utc(const struct rtc_time *local,
				   const struct ef800_time_settings *settings,
				   struct rtc_time *utc);
int ef800_utc_time_to_local(const struct rtc_time *utc,
					const struct ef800_time_settings *settings,
					struct rtc_time *local);

#endif /* EF800_TIME_H */
