/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_time.h"

#include <errno.h>
#include <time.h>

#include <zephyr/sys/timeutil.h>

static bool leap_year(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
	static const uint8_t days[] = {
		31U, 28U, 31U, 30U, 31U, 30U,
		31U, 31U, 30U, 31U, 30U, 31U,
	};

	if (month == 1 && leap_year(year)) {
		return 29;
	}
	return days[month];
}

bool ef800_rtc_time_valid(const struct rtc_time *time)
{
	int year;

	if (time == NULL) {
		return false;
	}
	year = time->tm_year + 1900;
	return year >= 2000 && year <= 2099 &&
		time->tm_mon >= 0 && time->tm_mon < 12 &&
		time->tm_mday >= 1 &&
		time->tm_mday <= days_in_month(year, time->tm_mon) &&
		time->tm_wday >= 0 && time->tm_wday < 7 &&
		time->tm_hour >= 0 && time->tm_hour < 24 &&
		time->tm_min >= 0 && time->tm_min < 60 &&
		time->tm_sec >= 0 && time->tm_sec < 60;
}

static int settings_valid(const struct ef800_time_settings *settings)
{
	return settings != NULL && settings->utc_offset_minutes >= -24 * 60 &&
		settings->utc_offset_minutes <= 24 * 60;
}

static int epoch_to_rtc_time(int64_t epoch, struct rtc_time *time)
{
	static const uint16_t cumulative_days[] = {
		0U, 31U, 59U, 90U, 120U, 151U, 181U,
		212U, 243U, 273U, 304U, 334U,
	};
	int64_t days;
	int64_t seconds;
	int64_t z;
	int64_t era;
	int64_t doe;
	int64_t yoe;
	int64_t year;
	int64_t day_of_year;
	int64_t month_part;
	int month;
	int day;

	if (time == NULL) {
		return -EINVAL;
	}
	days = epoch / 86400;
	seconds = epoch % 86400;
	if (seconds < 0) {
		--days;
		seconds += 86400;
	}

	/* Proleptic Gregorian conversion, with 1970-01-01 as day zero. */
	z = days + 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	year = yoe + era * 400;
	day_of_year = doe - (365 * yoe + yoe / 4 - yoe / 100);
	month_part = (5 * day_of_year + 2) / 153;
	day = (int)(day_of_year - (153 * month_part + 2) / 5 + 1);
	month = (int)(month_part + (month_part < 10 ? 3 : -9));
	year += month <= 2;

	time->tm_sec = (int)(seconds % 60);
	time->tm_min = (int)((seconds / 60) % 60);
	time->tm_hour = (int)(seconds / 3600);
	time->tm_mday = day;
	time->tm_mon = month - 1;
	time->tm_year = (int)year - 1900;
	time->tm_wday = (int)((days + 4) % 7);
	if (time->tm_wday < 0) {
		time->tm_wday += 7;
	}
	time->tm_yday = (int)cumulative_days[month - 1] + day - 1;
	if (month > 2 && leap_year((int)year)) {
		++time->tm_yday;
	}
	time->tm_isdst = -1;
	time->tm_nsec = 0;
	return time->tm_year >= 99 && time->tm_year <= 199 ? 0 : -ERANGE;
}

int ef800_local_time_to_utc(const struct rtc_time *local,
				   const struct ef800_time_settings *settings,
				   struct rtc_time *utc)
{
	struct tm converted = {0};
	int64_t epoch;

	if (!ef800_rtc_time_valid(local) || !settings_valid(settings) ||
	    utc == NULL) {
		return -EINVAL;
	}

	converted.tm_sec = local->tm_sec;
	converted.tm_min = local->tm_min;
	converted.tm_hour = local->tm_hour;
	converted.tm_mday = local->tm_mday;
	converted.tm_mon = local->tm_mon;
	converted.tm_year = local->tm_year;
	epoch = timeutil_timegm64(&converted);
	epoch -= (int64_t)settings->utc_offset_minutes * 60;
	if (settings->daylight_saving) {
		epoch -= 60 * 60;
	}
	if (epoch_to_rtc_time(epoch, utc) != 0 || !ef800_rtc_time_valid(utc)) {
		return -ERANGE;
	}
	return 0;
}

int ef800_utc_time_to_local(const struct rtc_time *utc,
					const struct ef800_time_settings *settings,
					struct rtc_time *local)
{
	struct tm converted = {0};
	int64_t epoch;

	if (!ef800_rtc_time_valid(utc) || !settings_valid(settings) ||
	    local == NULL) {
		return -EINVAL;
	}

	converted.tm_sec = utc->tm_sec;
	converted.tm_min = utc->tm_min;
	converted.tm_hour = utc->tm_hour;
	converted.tm_mday = utc->tm_mday;
	converted.tm_mon = utc->tm_mon;
	converted.tm_year = utc->tm_year;
	epoch = timeutil_timegm64(&converted);
	epoch += (int64_t)settings->utc_offset_minutes * 60;
	if (settings->daylight_saving) {
		epoch += 60 * 60;
	}
	if (epoch_to_rtc_time(epoch, local) != 0 || !ef800_rtc_time_valid(local)) {
		return -ERANGE;
	}
	return 0;
}
