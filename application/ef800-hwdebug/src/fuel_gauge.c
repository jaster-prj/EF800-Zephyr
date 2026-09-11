/* SPDX-License-Identifier: Apache-2.0 */

#include "fuel_gauge.h"
#include "test_result.h"
#include "flash.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#include <zephyr/drivers/fuel_gauge.h>

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17048)
static const struct i2c_dt_spec fuel_gauge_i2c =
	I2C_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_max17048));
static const struct device *const fuel_gauge =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_max17048));
#endif

void hwdebug_fuel_gauge_test(void)
{
	hwdebug_test_begin("fuel-gauge/max17048");
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17048)
	union fuel_gauge_prop_val voltage;
	union fuel_gauge_prop_val soc;
	int ret;

	if (!i2c_is_ready_dt(&fuel_gauge_i2c)) {
		hwdebug_test_fail("I2C controller is not ready", -ENODEV);
	} else if ((ret = i2c_configure(fuel_gauge_i2c.bus,
		I2C_SPEED_SET(I2C_SPEED_FAST))) != 0) {
		hwdebug_test_fail("Zephyr I2C configure failed", ret);
	} else if (!device_is_ready(fuel_gauge)) {
		hwdebug_test_fail("MAX17048 device is not ready", -ENODEV);
	} else if ((ret = fuel_gauge_get_prop(fuel_gauge, FUEL_GAUGE_VOLTAGE,
		&voltage)) != 0) {
		hwdebug_test_fail("MAX17048 voltage read failed", ret);
	} else if ((ret = fuel_gauge_get_prop(fuel_gauge,
		FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &soc)) != 0) {
		hwdebug_test_fail("MAX17048 SOC read failed", ret);
	} else {
		hwdebug_console_printf("fuel-gauge: soc=%u%% voltage=%d uV\r\n",
			soc.relative_state_of_charge, voltage.voltage);
	}
#else
	hwdebug_test_fail("MAX17048 is missing from devicetree", -ENODEV);
#endif
	hwdebug_test_pass();
}
