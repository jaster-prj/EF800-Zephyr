/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_T6963_H
#define EF800_T6963_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

struct ef800_t6963_config {
	struct gpio_dt_spec data[8];
	struct gpio_dt_spec cd;
	struct gpio_dt_spec wr;
	struct gpio_dt_spec rd;
	struct gpio_dt_spec ce;
	struct gpio_dt_spec fs;
	struct gpio_dt_spec reset;
	uint16_t pixels_horizontal;
	uint16_t pixels_vertical;
	uint8_t font_width;
	uint8_t memory_kb;
};

struct ef800_t6963 {
	struct ef800_t6963_config config;
	struct k_mutex lock;
	uint8_t columns;
	uint8_t rows;
	uint8_t cursor_column;
	uint8_t cursor_row;
	bool ready;
};

int ef800_t6963_init(struct ef800_t6963 *lcd,
			const struct ef800_t6963_config *config);
int ef800_t6963_initialize(struct ef800_t6963 *lcd);
int ef800_t6963_clear(struct ef800_t6963 *lcd);
int ef800_t6963_text_cursor(struct ef800_t6963 *lcd,
				uint8_t column, uint8_t row);
int ef800_t6963_write_char(struct ef800_t6963 *lcd, char character);
int ef800_t6963_write_string(struct ef800_t6963 *lcd,
				     const char *string);
int ef800_t6963_read_text(struct ef800_t6963 *lcd, uint8_t column,
				  uint8_t row, uint8_t *data, size_t length);
int ef800_t6963_write_pixel(struct ef800_t6963 *lcd,
				    uint16_t x, uint16_t y, bool value);
int ef800_t6963_clear_text(struct ef800_t6963 *lcd);
int ef800_t6963_clear_text_body(struct ef800_t6963 *lcd);
int ef800_t6963_clear_graphic(struct ef800_t6963 *lcd);
int ef800_t6963_clear_character_generator(struct ef800_t6963 *lcd);
int ef800_t6963_graphic_cursor(struct ef800_t6963 *lcd,
				       uint16_t x, uint16_t y);
int ef800_t6963_define_character(struct ef800_t6963 *lcd,
					 uint8_t character, const uint8_t pattern[8]);
int ef800_t6963_set_display_mode(struct ef800_t6963 *lcd,
					 bool text, bool graphics, bool cursor,
					 bool blink);
int ef800_t6963_set_cursor_pattern(struct ef800_t6963 *lcd,
					   uint8_t pattern);
int ef800_t6963_set_cursor_pointer(struct ef800_t6963 *lcd,
					   uint8_t column, uint8_t row);
int ef800_t6963_set_mode(struct ef800_t6963 *lcd, char mode, char cgram);
int ef800_t6963_create_line(struct ef800_t6963 *lcd,
					int x0, int y0, int x1, int y1);
int ef800_t6963_draw_horizontal_line(struct ef800_t6963 *lcd, uint16_t y);
int ef800_t6963_create_circle(struct ef800_t6963 *lcd,
					  int center_x, int center_y, int radius);

/* Pure address helper for the text-area layout. */
uint16_t ef800_t6963_text_address(uint16_t home, uint8_t columns,
					  uint8_t column, uint8_t row);

#endif /* EF800_T6963_H */
