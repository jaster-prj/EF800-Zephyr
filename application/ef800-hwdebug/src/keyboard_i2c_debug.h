/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Inspect and test the keyboard I2C updater interface. */
#ifndef EF800_HWDEBUG_KEYBOARD_I2C_DEBUG_H_
#define EF800_HWDEBUG_KEYBOARD_I2C_DEBUG_H_

/** @brief Report keyboard updater I2C device readiness and status.
 * @return None.
 */
void hwdebug_keyboard_i2c_status(void);

/** @brief Run the keyboard updater ROM identification transaction.
 * @return None; prints identification and test result.
 */
void hwdebug_keyboard_i2c_rom_test(void);

#endif /* EF800_HWDEBUG_KEYBOARD_I2C_DEBUG_H_ */
