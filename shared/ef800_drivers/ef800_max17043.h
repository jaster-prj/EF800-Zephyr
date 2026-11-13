/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Read voltage and state of charge from a MAX17043 fuel gauge. */
#ifndef EF800_MAX17043_H
#define EF800_MAX17043_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

struct ef800_max17043_status {
	uint8_t soc_percent;
	uint32_t voltage_uv;
};

struct ef800_max17043 {
	struct i2c_dt_spec i2c;
	struct k_mutex lock;
	bool ready;
};

/** @brief Initialize I2C access and verify the fuel-gauge device.
 * @param gauge Driver state to initialize.
 * @param spec I2C device/address specification.
 * @return 0 when ready; negative errno on I2C/device failure.
 */
int ef800_max17043_init(struct ef800_max17043 *gauge,
			const struct i2c_dt_spec *spec);

/** @brief Read and convert voltage and SOC registers.
 * @param gauge Initialized driver state.
 * @param status Receives SOC percentage and voltage in microvolts.
 * @return 0 on read; negative errno on I2C or invalid-state failure.
 */
int ef800_max17043_read(struct ef800_max17043 *gauge,
			struct ef800_max17043_status *status);

/* Pure register conversions kept separate for native/unit testing. */

/** @brief Convert the fixed-point SOC register to a rounded percentage.
 * @param raw_soc Raw 16-bit register value.
 * @return State of charge, clamped to 100 percent.
 */
uint8_t ef800_max17043_soc_percent(uint16_t raw_soc);

/** @brief Convert the VCELL register to microvolts.
 * @param raw_vcell Raw 16-bit register value.
 * @return Voltage in microvolts.
 */
uint32_t ef800_max17043_voltage_uv(uint16_t raw_vcell);

#endif /* EF800_MAX17043_H */
