/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Run destructive SRAM diagnostics for hardware bring-up. */

#ifndef EF800_SRAM_TEST_H_
#define EF800_SRAM_TEST_H_

/** @brief Test accessible SRAM regions and report detected faults.
 * @return None; results are written to the hardware-debug console.
 */
void hwdebug_sram_test(void);

#endif
