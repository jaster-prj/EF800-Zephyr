/* SPDX-License-Identifier: Apache-2.0 */
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

int ef800_max17043_init(struct ef800_max17043 *gauge,
			const struct i2c_dt_spec *spec);
int ef800_max17043_read(struct ef800_max17043 *gauge,
			struct ef800_max17043_status *status);

/* Pure register conversions kept separate for native/unit testing. */
uint8_t ef800_max17043_soc_percent(uint16_t raw_soc);
uint32_t ef800_max17043_voltage_uv(uint16_t raw_vcell);

#endif /* EF800_MAX17043_H */
