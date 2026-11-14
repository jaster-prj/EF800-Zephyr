/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Track and report one hardware-debug test result. */

#ifndef TEST_HWDEBUG_TEST_RESULT_H
#define TEST_HWDEBUG_TEST_RESULT_H

#include <stdbool.h>

struct hwdebug_test_result {
	const char *name;
	int error;
	const char *description;
};

/** @brief Start a test and clear its previous failure state.
 * @param name Display name of the test.
 * @return None.
 */
void hwdebug_test_begin(const char *name);

/** @brief Record and print a test failure.
 * @param description Short failure detail.
 * @param error Error/status code associated with the failure.
 * @return None.
 */
void hwdebug_test_fail(const char *description, int error);

/** @brief Mark the active test as passed and print its result.
 * @return None.
 */
void hwdebug_test_pass(void);

/** @brief Check whether the active test has recorded a failure.
 * @return True after a failure, otherwise false.
 */
bool hwdebug_test_failed(void);

#endif
