/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Hardware-debug commands for MCP23017 GPIO expander tests. */

#ifndef TEST_HWDEBUG_MCP23017_H
#define TEST_HWDEBUG_MCP23017_H

/** @brief Run the combined MCP23017 diagnostic suite.
 * @return None; reports test results on the debug console.
 */
void hwdebug_mcp23017_test(void);

/** @brief Test MCP23017 register-level behavior using raw transactions.
 * @return None; reports test results on the debug console.
 */
void hwdebug_mcp23017_raw_test(void);

/** @brief Test MCP23017 access through Zephyr's GPIO HAL.
 * @return None; reports test results on the debug console.
 */
void hwdebug_mcp23017_hal_test(void);

/** @brief Test the shared MCP23017 driver wrapper.
 * @return None; reports test results on the debug console.
 */
void hwdebug_mcp23017_driver_test(void);

#endif
