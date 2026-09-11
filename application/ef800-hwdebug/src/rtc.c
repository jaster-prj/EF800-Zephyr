/* SPDX-License-Identifier: Apache-2.0 */

#include "rtc.h"
#include "test_result.h"
#include "flash.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
#define EF800_HWDEBUG_RTC_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_ds3231_rtc)
#define EF800_HWDEBUG_RTC_BUS DT_BUS(EF800_HWDEBUG_RTC_NODE)
static const struct device *const rtc = DEVICE_DT_GET(EF800_HWDEBUG_RTC_NODE);
static const struct device *const rtc_i2c = DEVICE_DT_GET(EF800_HWDEBUG_RTC_BUS);
#endif

void hwdebug_rtc_test(void)
{
	hwdebug_test_begin("rtc/ds3231");
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	struct rtc_time time;
	int ret;

	if (!device_is_ready(rtc_i2c)) {
		hwdebug_test_fail("I2C controller is not ready", -ENODEV);
	} else if ((ret = i2c_configure(rtc_i2c, I2C_SPEED_SET(I2C_SPEED_FAST))) != 0) {
		hwdebug_test_fail("Zephyr I2C configure failed", ret);
	} else if (!device_is_ready(rtc)) {
		hwdebug_test_fail("DS3231 RTC is not ready", -ENODEV);
	} else if ((ret = rtc_get_time(rtc, &time)) != 0) {
		hwdebug_test_fail("DS3231 time read failed", ret);
	} else {
		hwdebug_console_printf("rtc: %04d-%02d-%02d %02d:%02d:%02d wday=%d\r\n",
			time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
			time.tm_hour, time.tm_min, time.tm_sec, time.tm_wday);
	}
#else
	hwdebug_test_fail("DS3231 RTC is missing from devicetree", -ENODEV);
#endif
	hwdebug_test_pass();
}
