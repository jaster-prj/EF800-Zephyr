/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TLX711A_LCD_BUS_H
#define TLX711A_LCD_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*lcd_bus_transaction_cb)(bool command, uint8_t value);
typedef uint8_t (*lcd_bus_read_cb)(bool command);
typedef void (*lcd_bus_reset_cb)(void);
int lcd_bus_init(lcd_bus_transaction_cb transaction, lcd_bus_read_cb read,
                 lcd_bus_reset_cb reset);
uint32_t lcd_bus_ce_count(void);
uint32_t lcd_bus_write_count(void);
uint32_t lcd_bus_read_count(void);
uint32_t lcd_bus_wr_edge_count(void);
uint8_t lcd_bus_last_write_command(void);
uint8_t lcd_bus_last_write_value(void);
int lcd_bus_control_levels(uint8_t *wr, uint8_t *rd, uint8_t *ce, uint8_t *cd);
int lcd_bus_font_width(void);

#endif
