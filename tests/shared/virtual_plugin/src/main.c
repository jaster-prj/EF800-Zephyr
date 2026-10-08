/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "ef800_virtual_plugin.h"

ZTEST(shared_virtual_plugin, test_delayed_plugin_completes_before_deadline)
{
	struct ef800_virtual_plugin plugin;
	struct ef800_virtual_plugin_result result;

	ef800_virtual_plugin_init(&plugin, EF800_VIRTUAL_PLUGIN_DELAYED, 100U);
	result = ef800_virtual_plugin_step(&plugin, 20U);
	zassert_false(result.completed, NULL);
	result = ef800_virtual_plugin_step(&plugin, 80U);
	zassert_true(result.completed, NULL);
	zassert_true(result.output_accepted, NULL);
	zassert_false(result.quarantined, NULL);
}

ZTEST(shared_virtual_plugin, test_timeout_and_invalid_service_quarantine)
{
	struct ef800_virtual_plugin plugin;
	struct ef800_virtual_plugin_result result;

	ef800_virtual_plugin_init(&plugin, EF800_VIRTUAL_PLUGIN_TIMEOUT, 50U);
	result = ef800_virtual_plugin_step(&plugin, 1U);
	zassert_true(result.quarantined, NULL);
	ef800_virtual_plugin_init(&plugin,
		EF800_VIRTUAL_PLUGIN_MALFORMED_SERVICE, 50U);
	result = ef800_virtual_plugin_step(&plugin, 1U);
	zassert_true(result.quarantined, NULL);
}

ZTEST(shared_virtual_plugin, test_partial_output_is_not_accepted)
{
	struct ef800_virtual_plugin plugin;
	struct ef800_virtual_plugin_result result;

	ef800_virtual_plugin_init(&plugin, EF800_VIRTUAL_PLUGIN_PARTIAL_OUTPUT, 50U);
	result = ef800_virtual_plugin_step(&plugin, 1U);
	zassert_true(result.completed, NULL);
	zassert_false(result.output_accepted, NULL);
	zassert_false(result.quarantined, NULL);
}

ZTEST_SUITE(shared_virtual_plugin, NULL, NULL, NULL, NULL, NULL);
