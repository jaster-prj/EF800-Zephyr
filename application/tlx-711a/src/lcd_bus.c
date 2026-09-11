/* SPDX-License-Identifier: Apache-2.0 */
#include "lcd_bus.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define DATA_COUNT 8U

static const struct gpio_dt_spec data_pins[DATA_COUNT] = {
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 6),
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), data_gpios, 7),
};
static const struct gpio_dt_spec cd = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), cd_gpios);
static const struct gpio_dt_spec wr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), wr_gpios);
static const struct gpio_dt_spec rd = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), rd_gpios);
static const struct gpio_dt_spec ce = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ce_gpios);
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), reset_gpios);
static const struct gpio_dt_spec fs = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), fs_gpios);
static struct gpio_callback ce_callback;
static struct gpio_callback wr_callback;
static struct gpio_callback rd_callback;
static struct gpio_callback reset_callback;
static lcd_bus_transaction_cb transaction_callback;
static lcd_bus_read_cb read_callback;
static lcd_bus_reset_cb reset_callback_fn;
static volatile uint8_t read_response;
static volatile uint32_t ce_count;
static volatile uint32_t write_count;
static volatile uint32_t read_count;
static volatile bool cycle_write_seen;
static volatile bool cycle_read_seen;
static volatile bool pending_write;
static volatile bool pending_write_command;
static volatile uint8_t pending_write_value;
static volatile uint32_t wr_edge_count;
static volatile bool pending_read;
static volatile uint8_t last_write_command;
static volatile uint8_t last_write_value;

static int data_direction(bool output);
static uint8_t data_read(void);

static void capture_cycle(void)
{
	if (pending_write && !cycle_write_seen && transaction_callback != NULL) {
		cycle_write_seen = true;
		pending_write = false;
		++write_count;
		last_write_command = pending_write_command;
		last_write_value = pending_write_value;
		transaction_callback(pending_write_command, pending_write_value);
		return;
	}
	if (pending_read && !cycle_read_seen) {
		cycle_read_seen = true;
		pending_read = false;
		++read_count;
		if (read_callback != NULL) {
			read_response = read_callback(gpio_pin_get_dt(&cd) != 0);
		}
		if (data_direction(true) != 0) return;
		for (size_t index = 0U; index < DATA_COUNT; ++index) {
			(void)gpio_pin_set_dt(&data_pins[index],
				(read_response & BIT(index)) != 0);
		}
	}
}

static void write_handler(const struct device *port, struct gpio_callback *cb,
				  uint32_t pins)
{
	ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
	if (gpio_pin_get_dt(&wr) == 0) {
		++wr_edge_count;
		/* Hold the bus byte until /CE confirms a real LCD cycle. */
		pending_write_command = gpio_pin_get_dt(&cd) != 0;
		pending_write_value = data_read();
		pending_write = true;
	}
}

static void read_handler(const struct device *port, struct gpio_callback *cb,
				 uint32_t pins)
{
	ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
	if (gpio_pin_get_dt(&rd) == 0) {
		pending_read = true;
	}
	if (gpio_pin_get_dt(&rd) != 0) {
		(void)data_direction(false);
	}
}

static int data_direction(bool output)
{
	for (size_t index = 0U; index < DATA_COUNT; ++index) {
		/* The shared bus is released as high impedance whenever the dummy is
		 * not returning a read value. Do not add pull resistors to the bus. */
		int ret = gpio_pin_configure_dt(&data_pins[index],
			output ? GPIO_OUTPUT : GPIO_INPUT);
		if (ret != 0) return ret;
	}
	return 0;
}

static uint8_t data_read(void)
{
	uint8_t value = 0U;
	for (size_t index = 0U; index < DATA_COUNT; ++index) {
		if (gpio_pin_get_dt(&data_pins[index]) > 0) value |= BIT(index);
	}
	return value;
}

static void ce_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
	++ce_count;
	/* This callback is registered for the falling edge. Do not re-sample CE:
	 * the interrupt may run after the short pulse has already ended. */
	cycle_write_seen = false;
	cycle_read_seen = false;
	if (pending_read) {
		capture_cycle();
		return;
	}
	/* /WR is shorter than the GPIO interrupt latency on this bus. For a cycle
	 * without a latched /RD, CE is the reliable write transaction boundary. */
	if (transaction_callback != NULL) {
		cycle_write_seen = true;
		++write_count;
		last_write_command = gpio_pin_get_dt(&cd) != 0;
		last_write_value = data_read();
		transaction_callback(last_write_command, last_write_value);
	}
}

uint32_t lcd_bus_ce_count(void)
{
	return ce_count;
}

uint32_t lcd_bus_write_count(void)
{
	return write_count;
}

uint32_t lcd_bus_read_count(void)
{
	return read_count;
}

uint32_t lcd_bus_wr_edge_count(void)
{
	return wr_edge_count;
}

uint8_t lcd_bus_last_write_command(void)
{
	return last_write_command;
}

uint8_t lcd_bus_last_write_value(void)
{
	return last_write_value;
}

int lcd_bus_control_levels(uint8_t *wr_level, uint8_t *rd_level,
				   uint8_t *ce_level, uint8_t *cd_level)
{
	if (wr_level == NULL || rd_level == NULL || ce_level == NULL || cd_level == NULL)
		return -EINVAL;
	*wr_level = (uint8_t)gpio_pin_get_dt(&wr);
	*rd_level = (uint8_t)gpio_pin_get_dt(&rd);
	*ce_level = (uint8_t)gpio_pin_get_dt(&ce);
	*cd_level = (uint8_t)gpio_pin_get_dt(&cd);
	return 0;
}

int lcd_bus_font_width(void)
{
	int level = gpio_pin_get_dt(&fs);
	if (level < 0) return level;
	return level != 0 ? 6 : 8;
}

static void reset_handler(const struct device *port, struct gpio_callback *cb,
				  uint32_t pins)
{
	ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
	if (gpio_pin_get_dt(&reset_pin) > 0 && reset_callback_fn != NULL)
		reset_callback_fn();
}

int lcd_bus_init(lcd_bus_transaction_cb transaction, lcd_bus_read_cb read,
			 lcd_bus_reset_cb reset)
{
	int ret;
	transaction_callback = transaction;
	read_callback = read;
	reset_callback_fn = reset;
	for (size_t index = 0U; index < DATA_COUNT; ++index) {
		if (!device_is_ready(data_pins[index].port)) return -ENODEV;
	}
	ret = data_direction(false);
	if (ret != 0) return ret;
	if (!device_is_ready(fs.port) || !device_is_ready(reset_pin.port)) return -ENODEV;
	ret = gpio_pin_configure_dt(&cd, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) return ret;
	ret = gpio_pin_configure_dt(&wr, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) return ret;
	ret = gpio_pin_configure_dt(&rd, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) return ret;
	ret = gpio_pin_configure_dt(&ce, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) return ret;
	ret = gpio_pin_configure_dt(&reset_pin, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) return ret;
	ret = gpio_pin_configure_dt(&fs, GPIO_INPUT);
	if (ret != 0) return ret;
	gpio_init_callback(&ce_callback, ce_handler, BIT(ce.pin));
	ret = gpio_add_callback(ce.port, &ce_callback);
	if (ret != 0) return ret;
	gpio_init_callback(&wr_callback, write_handler, BIT(wr.pin));
	ret = gpio_add_callback(wr.port, &wr_callback);
	if (ret != 0) return ret;
	gpio_init_callback(&rd_callback, read_handler, BIT(rd.pin));
	ret = gpio_add_callback(rd.port, &rd_callback);
	if (ret != 0) return ret;
	gpio_init_callback(&reset_callback, reset_handler, BIT(reset_pin.pin));
	ret = gpio_add_callback(reset_pin.port, &reset_callback);
	if (ret != 0) return ret;
	ret = gpio_pin_interrupt_configure_dt(&ce, GPIO_INT_EDGE_FALLING);
	if (ret != 0) return ret;
	ret = gpio_pin_interrupt_configure_dt(&wr, GPIO_INT_EDGE_BOTH);
	if (ret != 0) return ret;
	ret = gpio_pin_interrupt_configure_dt(&rd, GPIO_INT_EDGE_BOTH);
	if (ret != 0) return ret;
	return gpio_pin_interrupt_configure_dt(&reset_pin, GPIO_INT_EDGE_TO_ACTIVE);
}
