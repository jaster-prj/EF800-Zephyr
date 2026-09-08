/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_HWDEBUG_TEST_RESULT_H
#define EF800_HWDEBUG_TEST_RESULT_H

#include <stdbool.h>

struct hwdebug_test_result {
	const char *name;
	int error;
	const char *description;
};

void hwdebug_test_begin(const char *name);
void hwdebug_test_fail(const char *description, int error);
void hwdebug_test_pass(void);
bool hwdebug_test_failed(void);

#endif
