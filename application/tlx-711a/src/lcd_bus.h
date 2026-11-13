/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Abstract the T6963 emulator's parallel LCD bus operations. */
#ifndef TLX711A_LCD_BUS_H
#define TLX711A_LCD_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*lcd_bus_transaction_cb)(bool command, uint8_t value);
typedef uint8_t (*lcd_bus_read_cb)(bool command);
typedef void (*lcd_bus_reset_cb)(void);

/** @brief Install callbacks implementing LCD bus transactions and reset.
 * @param transaction Callback for command/data writes.
 * @param read Callback for command/data reads.
 * @param reset Callback for controller reset.
 * @return 0 on setup; negative errno for invalid callbacks.
 */
int lcd_bus_init(lcd_bus_transaction_cb transaction, lcd_bus_read_cb read,
                 lcd_bus_reset_cb reset);

/** @brief Get the number of chip-enable transitions observed.
 * @return Transition count.
 */
uint32_t lcd_bus_ce_count(void);

/** @brief Get the number of write transactions observed.
 * @return Write count.
 */
uint32_t lcd_bus_write_count(void);

/** @brief Get the number of read transactions observed.
 * @return Read count.
 */
uint32_t lcd_bus_read_count(void);

/** @brief Get the number of write-strobe edges observed.
 * @return Edge count.
 */
uint32_t lcd_bus_wr_edge_count(void);

/** @brief Get the last command/data selector used for a write.
 * @return Nonzero when the last bus write was a command, otherwise zero.
 */
uint8_t lcd_bus_last_write_command(void);

/** @brief Get the last value written on the bus.
 * @return Last data/command byte.
 */
uint8_t lcd_bus_last_write_value(void);

/** @brief Read current emulated WR/RD/CE/CD control levels.
 * @param wr Receives write-strobe level.
 * @param rd Receives read-strobe level.
 * @param ce Receives chip-enable level.
 * @param cd Receives command/data selector level.
 * @return 0 on read; negative errno for invalid outputs.
 */
int lcd_bus_control_levels(uint8_t *wr, uint8_t *rd, uint8_t *ce, uint8_t *cd);

/** @brief Get configured emulated LCD font width.
 * @return Font width in pixels.
 */
int lcd_bus_font_width(void);

#endif
