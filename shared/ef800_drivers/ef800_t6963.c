/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_t6963.h"

#include <errno.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ef800_t6963, LOG_LEVEL_INF);

#define T6963_SET_CURSOR_POINTER       0x21U
#define T6963_SET_OFFSET_REGISTER      0x22U
#define T6963_SET_ADDRESS_POINTER      0x24U
#define T6963_SET_TEXT_HOME_ADDRESS    0x40U
#define T6963_SET_TEXT_AREA            0x41U
#define T6963_SET_GRAPHIC_HOME_ADDRESS 0x42U
#define T6963_SET_GRAPHIC_AREA         0x43U
#define T6963_SET_DATA_AUTO_WRITE     0xB0U
#define T6963_SET_DATA_AUTO_READ      0xB1U
#define T6963_AUTO_RESET              0xB2U
#define T6963_MODE_SET                 0x80U
#define T6963_DATA_WRITE_AND_INCREMENT 0xC0U
#define T6963_DATA_READ_AND_INCREMENT  0xC1U
#define T6963_DATA_READ_NO_INCREMENT   0xC5U
#define T6963_DISPLAY_MODE             0x90U

static uint16_t graphic_address(const struct ef800_t6963 *lcd,
					uint16_t x, uint16_t y);
static int read_display_data_locked(struct ef800_t6963 *lcd, uint8_t *value,
					    bool increment);

static int set_pin(const struct gpio_dt_spec *spec, int value)
{
	return gpio_pin_set_dt(spec, value);
}

static int set_data_direction(struct ef800_t6963 *lcd, bool output)
{
	for (uint8_t pin = 0U; pin < 8U; ++pin) {
		int ret = gpio_pin_configure_dt(&lcd->config.data[pin],
			output ? GPIO_OUTPUT : GPIO_INPUT);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

static int write_data_bus(struct ef800_t6963 *lcd, uint8_t value)
{
	for (uint8_t pin = 0U; pin < 8U; ++pin) {
		int ret = gpio_pin_set_dt(&lcd->config.data[pin],
			(value & BIT(pin)) != 0U);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

static int read_data_bus(struct ef800_t6963 *lcd, uint8_t *value)
{
	uint8_t result = 0U;

	for (uint8_t pin = 0U; pin < 8U; ++pin) {
		int ret = gpio_pin_get_dt(&lcd->config.data[pin]);
		if (ret < 0) {
			return ret;
		}
		if (ret != 0) {
			result |= BIT(pin);
		}
	}
	*value = result;
	return 0;
}

static int read_cycle_locked(struct ef800_t6963 *lcd, uint8_t *value)
{
	int ret;

	ret = set_data_direction(lcd, false);
	if (ret == 0) {
		ret = set_pin(&lcd->config.cd, 0);
	}
	if (ret == 0) {
		ret = set_pin(&lcd->config.rd, 0);
	}
	if (ret == 0) {
		ret = set_pin(&lcd->config.ce, 0);
	}
	if (ret == 0) {
		k_busy_wait(1U);
		ret = read_data_bus(lcd, value);
	}

	(void)set_pin(&lcd->config.ce, 1);
	(void)set_pin(&lcd->config.rd, 1);
	(void)set_data_direction(lcd, true);
	return ret;
}

static int read_status_locked(struct ef800_t6963 *lcd, uint8_t *status)
{
	int ret;

	ret = set_pin(&lcd->config.cd, 1);
	if (ret != 0) {
		return ret;
	}
	ret = set_data_direction(lcd, false);
	if (ret != 0) {
		return ret;
	}
	ret = set_pin(&lcd->config.rd, 0);
	if (ret == 0) {
		ret = set_pin(&lcd->config.ce, 0);
	}
	if (ret == 0) {
		k_busy_wait(1U);
		ret = read_data_bus(lcd, status);
	}

	(void)set_pin(&lcd->config.ce, 1);
	(void)set_pin(&lcd->config.rd, 1);
	(void)set_data_direction(lcd, true);
	return ret;
}

static int wait_ready_locked(struct ef800_t6963 *lcd)
{
	uint8_t status = 0U;

	for (uint32_t elapsed = 0U;
	     elapsed < CONFIG_EF800_MAIN_LCD_STATUS_TIMEOUT_US;
	     elapsed += 10U) {
		int ret = read_status_locked(lcd, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & 0x03U) == 0x03U) {
			return 0;
		}
		k_busy_wait(10U);
	}

	return -ETIMEDOUT;
}

static int write_cycle_locked(struct ef800_t6963 *lcd, uint8_t value,
				      bool command)
{
	int ret;

	ret = set_data_direction(lcd, true);
	if (ret == 0) {
		ret = write_data_bus(lcd, value);
	}
	if (ret == 0) {
		ret = set_pin(&lcd->config.cd, command ? 1 : 0);
	}
	if (ret == 0) {
		ret = set_pin(&lcd->config.wr, 0);
	}
	if (ret == 0) {
		ret = set_pin(&lcd->config.ce, 0);
	}
	k_busy_wait(1U);
	(void)set_pin(&lcd->config.ce, 1);
	(void)set_pin(&lcd->config.wr, 1);
	return ret;
}

static int write_command_locked(struct ef800_t6963 *lcd, uint8_t command)
{
	int ret = wait_ready_locked(lcd);

	if (ret == 0) {
		ret = write_cycle_locked(lcd, command, true);
	}
	return ret;
}

static int write_data_locked(struct ef800_t6963 *lcd, uint8_t value)
{
	int ret = wait_ready_locked(lcd);

	if (ret == 0) {
		ret = write_cycle_locked(lcd, value, false);
	}
	return ret;
}

static int set_address_locked(struct ef800_t6963 *lcd, uint16_t address)
{
	int ret = write_data_locked(lcd, (uint8_t)(address & 0xFFU));

	if (ret == 0) {
		ret = write_data_locked(lcd, (uint8_t)(address >> 8));
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_ADDRESS_POINTER);
	}
	return ret;
}

static int write_display_data_locked(struct ef800_t6963 *lcd, uint8_t value)
{
	int ret = write_data_locked(lcd, value);

	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_DATA_WRITE_AND_INCREMENT);
	}
	return ret;
}

static int write_pixel_locked(struct ef800_t6963 *lcd,
				      uint16_t x, uint16_t y, bool value)
{
	uint16_t address;
	uint8_t current;
	uint8_t mask;
	int ret;

	if (x >= lcd->config.pixels_horizontal ||
	    y >= lcd->config.pixels_vertical) {
		return -EINVAL;
	}

	address = graphic_address(lcd, x, y);
	mask = BIT((lcd->config.font_width - 1U) -
			  (x % lcd->config.font_width));
	ret = set_address_locked(lcd, address);
	if (ret == 0) {
		ret = read_display_data_locked(lcd, &current, false);
	}
	if (ret == 0) {
		current = value ? (uint8_t)(current | mask) :
			(uint8_t)(current & (uint8_t)~mask);
		ret = set_address_locked(lcd, address);
	}
	if (ret == 0) {
		ret = write_display_data_locked(lcd, current);
	}
	return ret;
}

static int read_display_data_locked(struct ef800_t6963 *lcd, uint8_t *value,
					    bool increment)
{
	int ret;

	ret = wait_ready_locked(lcd);
	if (ret != 0) {
		return ret;
	}
	ret = write_cycle_locked(lcd,
				 increment ? T6963_DATA_READ_AND_INCREMENT :
				 T6963_DATA_READ_NO_INCREMENT, true);
	if (ret != 0) {
		return ret;
	}
	return read_cycle_locked(lcd, value);
}

static int clear_area_locked(struct ef800_t6963 *lcd, uint16_t home,
				     uint32_t size)
{
	int ret = set_address_locked(lcd, home);

	for (uint32_t index = 0U; ret == 0 && index < size; ++index) {
		ret = write_display_data_locked(lcd, 0U);
	}
	return ret;
}

static int clear_display_locked(struct ef800_t6963 *lcd)
{
	uint16_t text_size = (uint16_t)lcd->columns * lcd->rows;
	uint16_t graphic_home = text_size;
	uint32_t graphic_size = (uint32_t)(lcd->config.pixels_horizontal /
						 lcd->config.font_width) *
						 lcd->config.pixels_vertical;
	uint16_t character_generator =
		(uint16_t)(((lcd->config.memory_kb / 2U) - 1U) * 0x800U);
	int ret;

	ret = clear_area_locked(lcd, 0U, text_size);
	if (ret == 0) {
		ret = clear_area_locked(lcd, graphic_home, graphic_size);
	}
	if (ret == 0) {
		ret = clear_area_locked(lcd, character_generator, 256U * 8U);
	}
	return ret;
}

static uint16_t graphic_address(const struct ef800_t6963 *lcd,
					uint16_t x, uint16_t y)
{
	return (uint16_t)((uint16_t)lcd->columns * lcd->rows +
				 (uint16_t)y * lcd->columns +
				 (x / lcd->config.font_width));
}

uint16_t ef800_t6963_text_address(uint16_t home, uint8_t columns,
					  uint8_t column, uint8_t row)
{
	return (uint16_t)(home + column + ((uint16_t)columns * row));
}

int ef800_t6963_init(struct ef800_t6963 *lcd,
			const struct ef800_t6963_config *config)
{
	if (lcd == NULL || config == NULL || config->font_width == 0U ||
	    config->pixels_horizontal == 0U || config->pixels_vertical == 0U ||
	    config->memory_kb < 2U ||
	    (config->pixels_horizontal % config->font_width) != 0U ||
	    config->pixels_vertical < 8U || (config->memory_kb % 2U) != 0U) {
		return -EINVAL;
	}

	lcd->config = *config;
	lcd->columns = (uint8_t)(config->pixels_horizontal /
					 config->font_width);
	lcd->rows = (uint8_t)(config->pixels_vertical / 8U);
	lcd->cursor_column = 0U;
	lcd->cursor_row = 0U;
	lcd->ready = false;
	k_mutex_init(&lcd->lock);

	for (uint8_t pin = 0U; pin < 8U; ++pin) {
		if (!gpio_is_ready_dt(&lcd->config.data[pin])) {
			return -ENODEV;
		}
	}
	if (!gpio_is_ready_dt(&lcd->config.cd) ||
	    !gpio_is_ready_dt(&lcd->config.wr) ||
	    !gpio_is_ready_dt(&lcd->config.rd) ||
	    !gpio_is_ready_dt(&lcd->config.ce) ||
	    !gpio_is_ready_dt(&lcd->config.fs) ||
	    !gpio_is_ready_dt(&lcd->config.reset)) {
		return -ENODEV;
	}

	return 0;
}

int ef800_t6963_initialize(struct ef800_t6963 *lcd)
{
	int ret;
	uint16_t graphic_home;

	if (lcd == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&lcd->lock, K_FOREVER);
	for (uint8_t pin = 0U; pin < 8U; ++pin) {
		ret = gpio_pin_configure_dt(&lcd->config.data[pin], GPIO_OUTPUT);
		if (ret != 0) {
			goto unlock;
		}
	}
	ret = gpio_pin_configure_dt(&lcd->config.cd, GPIO_OUTPUT);
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&lcd->config.wr, GPIO_OUTPUT);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&lcd->config.rd, GPIO_OUTPUT);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&lcd->config.ce, GPIO_OUTPUT);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&lcd->config.fs, GPIO_OUTPUT);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&lcd->config.reset, GPIO_OUTPUT);
	}
	if (ret != 0) {
		goto unlock;
	}

	/* T6963 control lines are active low except CD. */
	(void)set_pin(&lcd->config.cd, 1);
	(void)set_pin(&lcd->config.wr, 1);
	(void)set_pin(&lcd->config.rd, 1);
	(void)set_pin(&lcd->config.ce, 1);
	(void)set_pin(&lcd->config.fs, lcd->config.font_width == 8U ? 0 : 1);
	(void)set_pin(&lcd->config.reset, 0);
	k_busy_wait(1000U);
	(void)set_pin(&lcd->config.reset, 1);
	ret = write_data_bus(lcd, 0xFFU);
	if (ret != 0) {
		goto unlock;
	}

	graphic_home = (uint16_t)lcd->columns * lcd->rows;
	ret = write_data_locked(lcd, (uint8_t)(graphic_home & 0xFFU));
	if (ret == 0) {
		ret = write_data_locked(lcd, (uint8_t)(graphic_home >> 8));
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_GRAPHIC_HOME_ADDRESS);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, lcd->columns);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, 0U);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_GRAPHIC_AREA);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, 0U);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, 0U);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_TEXT_HOME_ADDRESS);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, lcd->columns);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, 0U);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_TEXT_AREA);
	}
	if (ret == 0) {
		ret = write_data_locked(lcd,
			(uint8_t)((lcd->config.memory_kb / 2U) - 1U));
	}
	if (ret == 0) {
		ret = write_data_locked(lcd, 0U);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_OFFSET_REGISTER);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_DISPLAY_MODE | 0x0CU);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_MODE_SET | 0x00U);
	}
	if (ret == 0) {
		ret = clear_display_locked(lcd);
	}
	if (ret == 0) {
		lcd->ready = true;
		LOG_INF("T6963 LCD ready (%ux%u, %u columns x %u rows)",
			lcd->config.pixels_horizontal, lcd->config.pixels_vertical,
			lcd->columns, lcd->rows);
	}

unlock:
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_clear(struct ef800_t6963 *lcd)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = clear_display_locked(lcd);
	lcd->cursor_column = 0U;
	lcd->cursor_row = 0U;
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_text_cursor(struct ef800_t6963 *lcd,
				uint8_t column, uint8_t row)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	if (column >= lcd->columns) {
		column = (uint8_t)(lcd->columns - 1U);
	}
	if (row >= lcd->rows) {
		row = (uint8_t)(lcd->rows - 1U);
	}

	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = set_address_locked(lcd,
		ef800_t6963_text_address(0U, lcd->columns, column, row));
	if (ret == 0) {
		lcd->cursor_column = column;
		lcd->cursor_row = row;
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}

static int write_char_locked(struct ef800_t6963 *lcd, char character)
{
	uint8_t value = (character >= ' ') ? (uint8_t)character - 32U : 0U;
	int ret;

	if (character == '\n') {
		lcd->cursor_column = 0U;
		if (lcd->cursor_row + 1U < lcd->rows) {
			++lcd->cursor_row;
		}
		return set_address_locked(lcd,
			ef800_t6963_text_address(0U, lcd->columns,
						  lcd->cursor_column, lcd->cursor_row));
	}
	if (character == '\r') {
		lcd->cursor_column = 0U;
		return set_address_locked(lcd,
			ef800_t6963_text_address(0U, lcd->columns,
						  lcd->cursor_column, lcd->cursor_row));
	}

	ret = write_display_data_locked(lcd, value);
	if (ret != 0) {
		return ret;
	}
	++lcd->cursor_column;
	if (lcd->cursor_column >= lcd->columns) {
		lcd->cursor_column = 0U;
		if (lcd->cursor_row + 1U < lcd->rows) {
			++lcd->cursor_row;
		} else {
			lcd->cursor_row = 0U;
			return set_address_locked(lcd,
				ef800_t6963_text_address(0U, lcd->columns,
						  lcd->cursor_column, lcd->cursor_row));
		}
	}
	return 0;
}

int ef800_t6963_write_char(struct ef800_t6963 *lcd, char character)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_char_locked(lcd, character);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_write_string(struct ef800_t6963 *lcd,
				     const char *string)
{
	int ret = 0;

	if (lcd == NULL || string == NULL || !lcd->ready) {
		return -EINVAL;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	for (const char *cursor = string; *cursor != '\0'; ++cursor) {
		ret = write_char_locked(lcd, *cursor);
		if (ret != 0) {
			break;
		}
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_write_pixel(struct ef800_t6963 *lcd,
				    uint16_t x, uint16_t y, bool value)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	if (x >= lcd->config.pixels_horizontal ||
	    y >= lcd->config.pixels_vertical) {
		return -EINVAL;
	}

	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_pixel_locked(lcd, x, y, value);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

static int clear_region(struct ef800_t6963 *lcd, uint16_t home, uint32_t size)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = clear_area_locked(lcd, home, size);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_clear_text(struct ef800_t6963 *lcd)
{
	if (lcd == NULL) {
		return -EINVAL;
	}
	return clear_region(lcd, 0U, (uint32_t)lcd->columns * lcd->rows);
}

int ef800_t6963_clear_graphic(struct ef800_t6963 *lcd)
{
	uint32_t size;

	if (lcd == NULL || lcd->config.font_width == 0U) {
		return -EINVAL;
	}
	size = (uint32_t)(lcd->config.pixels_horizontal /
				  lcd->config.font_width) * lcd->config.pixels_vertical;
	return clear_region(lcd, (uint16_t)lcd->columns * lcd->rows, size);
}

int ef800_t6963_clear_character_generator(struct ef800_t6963 *lcd)
{
	uint16_t home;

	if (lcd == NULL || lcd->config.memory_kb < 2U) {
		return -EINVAL;
	}
	home = (uint16_t)(((lcd->config.memory_kb / 2U) - 1U) * 0x800U);
	return clear_region(lcd, home, 256U * 8U);
}

int ef800_t6963_graphic_cursor(struct ef800_t6963 *lcd,
				       uint16_t x, uint16_t y)
{
	int ret;

	if (lcd == NULL || !lcd->ready || x >= lcd->config.pixels_horizontal ||
	    y >= lcd->config.pixels_vertical) {
		return -EINVAL;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = set_address_locked(lcd, graphic_address(lcd, x, y));
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_define_character(struct ef800_t6963 *lcd,
					 uint8_t character, const uint8_t pattern[8])
{
	uint16_t home;
	int ret;

	if (lcd == NULL || pattern == NULL || !lcd->ready ||
	    lcd->config.memory_kb < 2U) {
		return -EINVAL;
	}
	home = (uint16_t)(((lcd->config.memory_kb / 2U) - 1U) * 0x800U);
	k_mutex_lock(&lcd->lock, K_FOREVER);
	home = (uint16_t)(home + (uint16_t)(character & 0x1FU) * 8U);
	ret = set_address_locked(lcd, home);
	for (uint8_t index = 0U; ret == 0 && index < 8U; ++index) {
		ret = write_display_data_locked(lcd, pattern[index]);
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_set_display_mode(struct ef800_t6963 *lcd,
					 bool text, bool graphics, bool cursor,
					 bool blink)
{
	uint8_t command = T6963_DISPLAY_MODE;
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	if (graphics) {
		command |= BIT(3);
	}
	if (text) {
		command |= BIT(2);
	}
	if (cursor) {
		command |= BIT(1);
	}
	if (blink) {
		command |= BIT(0);
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_command_locked(lcd, command);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_set_cursor_pattern(struct ef800_t6963 *lcd,
					   uint8_t pattern)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	pattern = MIN(pattern, 7U);
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_command_locked(lcd, 0xA0U | pattern);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_set_cursor_pointer(struct ef800_t6963 *lcd,
					   uint8_t column, uint8_t row)
{
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	column = MIN(column, (uint8_t)(lcd->columns - 1U));
	row = MIN(row, (uint8_t)(lcd->rows - 1U));
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_data_locked(lcd, column);
	if (ret == 0) {
		ret = write_data_locked(lcd, row);
	}
	if (ret == 0) {
		ret = write_command_locked(lcd, T6963_SET_CURSOR_POINTER);
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_set_mode(struct ef800_t6963 *lcd, char mode, char cgram)
{
	uint8_t command = T6963_MODE_SET;
	int ret;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	switch (mode) {
	case 'X':
	case 'x':
	case '^':
		command |= 1U;
		break;
	case 'A':
	case 'a':
	case '&':
		command |= 3U;
		break;
	case 'T':
	case 't':
		command |= 4U;
		break;
	default:
		break;
	}
	if (cgram == 'E' || cgram == 'e') {
		command |= 8U;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	ret = write_command_locked(lcd, command);
	k_mutex_unlock(&lcd->lock);
	return ret;
}

int ef800_t6963_create_line(struct ef800_t6963 *lcd,
					int x0, int y0, int x1, int y1)
{
        int dx = abs(x1 - x0);
	int sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0);
	int sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	int ret = 0;

	if (lcd == NULL || !lcd->ready) {
		return -ENODEV;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	while (true) {
		ret = write_pixel_locked(lcd, (uint16_t)x0, (uint16_t)y0, true);
		if (ret != 0 || (x0 == x1 && y0 == y1)) {
			break;
		}
		if (2 * error >= dy) {
			error += dy;
			x0 += sx;
		}
		if (2 * error <= dx) {
			error += dx;
			y0 += sy;
		}
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}

static int circle_points_locked(struct ef800_t6963 *lcd,
					int center_x, int center_y, int x, int y)
{
	int ret;
	int points[8][2] = {
		{center_x + x, center_y + y}, {center_x - x, center_y + y},
		{center_x + x, center_y - y}, {center_x - x, center_y - y},
		{center_x + y, center_y + x}, {center_x - y, center_y + x},
		{center_x + y, center_y - x}, {center_x - y, center_y - x},
	};

	for (size_t index = 0U; index < ARRAY_SIZE(points); ++index) {
		ret = write_pixel_locked(lcd, (uint16_t)points[index][0],
					 (uint16_t)points[index][1], true);
		if (ret != 0 && (points[index][0] >= 0 &&
				 points[index][0] < lcd->config.pixels_horizontal &&
				 points[index][1] >= 0 &&
				 points[index][1] < lcd->config.pixels_vertical)) {
			return ret;
		}
	}
	return 0;
}

int ef800_t6963_create_circle(struct ef800_t6963 *lcd,
					  int center_x, int center_y, int radius)
{
	int error;
	int x;
	int y;
	int ret = 0;

	if (lcd == NULL || !lcd->ready || radius < 0) {
		return -EINVAL;
	}
	k_mutex_lock(&lcd->lock, K_FOREVER);
	error = -radius;
	x = radius;
	y = 0;
	while (x >= y && ret == 0) {
		ret = circle_points_locked(lcd, center_x, center_y, x, y);
		error += y++;
		error += y;
		if (error >= 0) {
			error -= --x;
			error -= x;
		}
	}
	k_mutex_unlock(&lcd->lock);
	return ret;
}
