/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Drive T6963 text and graphics LCD operations over GPIO. */
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

/** @brief Initialize driver state and bind GPIO configuration.
 * @param lcd Driver state.
 * @param config Pin mapping and display geometry.
 * @return 0 on initialization; negative errno for invalid config/device.
 */
int ef800_t6963_init(struct ef800_t6963 *lcd,
			const struct ef800_t6963_config *config);

/** @brief Reset the controller and configure its text/graphics memory areas.
 * @param lcd Initialized driver state.
 * @return 0 when ready; negative errno on GPIO/controller failure.
 */
int ef800_t6963_initialize(struct ef800_t6963 *lcd);

/** @brief Clear text, graphics, and character-generator memory.
 * @param lcd Ready LCD driver.
 * @return 0 on clear; negative errno on controller failure.
 */
int ef800_t6963_clear(struct ef800_t6963 *lcd);

/** @brief Position the text cursor.
 * @param lcd Ready LCD driver.
 * @param column Zero-based text column.
 * @param row Zero-based text row.
 * @return 0 on positioning; negative errno on invalid coordinates/failure.
 */
int ef800_t6963_text_cursor(struct ef800_t6963 *lcd,
				uint8_t column, uint8_t row);

/** @brief Write one character at the current text cursor.
 * @param lcd Ready LCD driver.
 * @param character Character to write.
 * @return 0 on write; negative errno on controller failure.
 */
int ef800_t6963_write_char(struct ef800_t6963 *lcd, char character);

/** @brief Write a NUL-terminated string at the current text cursor.
 * @param lcd Ready LCD driver.
 * @param string Text to write.
 * @return 0 on write; negative errno on invalid input/controller failure.
 */
int ef800_t6963_write_string(struct ef800_t6963 *lcd,
				     const char *string);

/** @brief Read text-plane bytes beginning at a row and column.
 * @param lcd Ready LCD driver.
 * @param column Starting column.
 * @param row Starting row.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @return 0 on read; negative errno on invalid bounds/controller failure.
 */
int ef800_t6963_read_text(struct ef800_t6963 *lcd, uint8_t column,
				  uint8_t row, uint8_t *data, size_t length);

/** @brief Set or clear a pixel in the graphics plane.
 * @param lcd Ready LCD driver.
 * @param x Horizontal pixel coordinate.
 * @param y Vertical pixel coordinate.
 * @param value True sets the pixel; false clears it.
 * @return 0 on update; negative errno on invalid coordinates/failure.
 */
int ef800_t6963_write_pixel(struct ef800_t6963 *lcd,
				    uint16_t x, uint16_t y, bool value);

/** @brief Clear the complete text plane.
 * @param lcd Ready LCD driver.
 * @return 0 on clear; negative errno on controller failure.
 */
int ef800_t6963_clear_text(struct ef800_t6963 *lcd);

/** @brief Clear text rows below the reserved header.
 * @param lcd Ready LCD driver.
 * @return 0 on clear; negative errno on controller failure.
 */
int ef800_t6963_clear_text_body(struct ef800_t6963 *lcd);

/** @brief Clear the graphics plane.
 * @param lcd Ready LCD driver.
 * @return 0 on clear; negative errno on controller failure.
 */
int ef800_t6963_clear_graphic(struct ef800_t6963 *lcd);

/** @brief Clear character-generator memory.
 * @param lcd Ready LCD driver.
 * @return 0 on clear; negative errno on controller failure.
 */
int ef800_t6963_clear_character_generator(struct ef800_t6963 *lcd);

/** @brief Position the graphics cursor in pixel coordinates.
 * @param lcd Ready LCD driver.
 * @param x Horizontal coordinate.
 * @param y Vertical coordinate.
 * @return 0 on positioning; negative errno when out of bounds or unavailable.
 */
int ef800_t6963_graphic_cursor(struct ef800_t6963 *lcd,
				       uint16_t x, uint16_t y);

/** @brief Define one character-generator glyph from eight row patterns.
 * @param lcd Ready LCD driver.
 * @param character Character code to define.
 * @param pattern Eight bytes describing glyph rows.
 * @return 0 on update; negative errno for invalid input/controller failure.
 */
int ef800_t6963_define_character(struct ef800_t6963 *lcd,
					 uint8_t character, const uint8_t pattern[8]);

/** @brief Select text, graphics, cursor, and blink visibility modes.
 * @param lcd Ready LCD driver.
 * @param text Enable text display.
 * @param graphics Enable graphics display.
 * @param cursor Enable cursor display.
 * @param blink Enable cursor blink.
 * @return 0 on update; negative errno on controller failure.
 */
int ef800_t6963_set_display_mode(struct ef800_t6963 *lcd,
					 bool text, bool graphics, bool cursor,
					 bool blink);

/** @brief Set the cursor shape pattern.
 * @param lcd Ready LCD driver.
 * @param pattern Controller-defined cursor pattern value.
 * @return 0 on update; negative errno on controller failure.
 */
int ef800_t6963_set_cursor_pattern(struct ef800_t6963 *lcd,
					   uint8_t pattern);

/** @brief Set the text cursor pointer without changing the text address.
 * @param lcd Ready LCD driver.
 * @param column Zero-based column.
 * @param row Zero-based row.
 * @return 0 on update; negative errno for invalid coordinates/failure.
 */
int ef800_t6963_set_cursor_pointer(struct ef800_t6963 *lcd,
					   uint8_t column, uint8_t row);

/** @brief Set controller address and character-generator modes.
 * @param lcd Ready LCD driver.
 * @param mode Controller text/graphics mode selector.
 * @param cgram Character-generator mode selector.
 * @return 0 on update; negative errno on invalid mode/controller failure.
 */
int ef800_t6963_set_mode(struct ef800_t6963 *lcd, char mode, char cgram);

/** @brief Draw a line between two pixel coordinates.
 * @param lcd Ready LCD driver.
 * @param x0 Start horizontal coordinate.
 * @param y0 Start vertical coordinate.
 * @param x1 End horizontal coordinate.
 * @param y1 End vertical coordinate.
 * @return 0 on draw; negative errno for invalid coordinates/controller failure.
 */
int ef800_t6963_create_line(struct ef800_t6963 *lcd,
					int x0, int y0, int x1, int y1);

/** @brief Draw a horizontal line across the display at one vertical position.
 * @param lcd Ready LCD driver.
 * @param y Vertical pixel coordinate.
 * @return 0 on draw; negative errno for invalid coordinate/controller failure.
 */
int ef800_t6963_draw_horizontal_line(struct ef800_t6963 *lcd, uint16_t y);

/** @brief Draw a circle centered at the supplied pixel coordinate.
 * @param lcd Ready LCD driver.
 * @param center_x Horizontal center coordinate.
 * @param center_y Vertical center coordinate.
 * @param radius Circle radius in pixels.
 * @return 0 on draw; negative errno for invalid geometry/controller failure.
 */
int ef800_t6963_create_circle(struct ef800_t6963 *lcd,
					  int center_x, int center_y, int radius);

/* Pure address helper for the text-area layout. */

/** @brief Calculate a text-plane address from origin and cell coordinates.
 * @param home Text-plane base address.
 * @param columns Text-plane row width.
 * @param column Zero-based column.
 * @param row Zero-based row.
 * @return Controller memory address for the requested cell.
 */
uint16_t ef800_t6963_text_address(uint16_t home, uint8_t columns,
					  uint8_t column, uint8_t row);

#endif /* EF800_T6963_H */
