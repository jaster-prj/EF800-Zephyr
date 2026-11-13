/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "virtual_ef800_supervisor.h"

static volatile bool ui_thread_progress;

static void ui_worker(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	ui_thread_progress = true;
}

K_THREAD_DEFINE(ui_worker_thread, 512, ui_worker, NULL, NULL, NULL, 4, 0, 0);

ZTEST(shared_virtual_supervisor, test_ui_progress_and_timeout_recovery)
{
	struct virtual_ef800_supervisor supervisor;
	struct ef800_ui_state ui;
	struct virtual_ef800_plugin_result result;

	ef800_ui_init(&ui);
	virtual_ef800_supervisor_start(&supervisor, &ui,
		VIRTUAL_EF800_PLUGIN_DELAYED, 100U);
	result = virtual_ef800_supervisor_step(&supervisor, 20U);
	zassert_false(result.completed, NULL);
	k_msleep(1U);
	zassert_true(ui_thread_progress, NULL);
	virtual_ef800_supervisor_start(&supervisor, &ui,
		VIRTUAL_EF800_PLUGIN_TIMEOUT, 100U);
	result = virtual_ef800_supervisor_step(&supervisor, 1U);
	zassert_true(result.quarantined, NULL);
	zassert_true(supervisor.quarantined, NULL);
	zassert_equal(ui.diagnostic, EF800_UI_DIAGNOSTIC_TIMEOUT, NULL);
	zassert_false(ui.plugin_session_active, NULL);
}

ZTEST(shared_virtual_supervisor, test_partial_output_does_not_mutate_workspace)
{
	struct virtual_ef800_supervisor supervisor;
	struct ef800_ui_state ui;
	struct virtual_ef800_plugin_result result;

	ef800_ui_init(&ui);
	virtual_ef800_supervisor_start(&supervisor, &ui,
		VIRTUAL_EF800_PLUGIN_PARTIAL_OUTPUT, 100U);
	result = virtual_ef800_supervisor_step(&supervisor, 1U);
	zassert_true(result.completed, NULL);
	zassert_false(result.output_accepted, NULL);
	zassert_false(supervisor.workspace_mutated, NULL);
}

ZTEST(shared_virtual_supervisor, test_cleanup_failure_escalates)
{
	struct virtual_ef800_supervisor supervisor;
	struct ef800_ui_state ui;
	struct virtual_ef800_plugin_result result;

	ef800_ui_init(&ui);
	virtual_ef800_supervisor_start(&supervisor, &ui,
		VIRTUAL_EF800_PLUGIN_TIMEOUT, 100U);
	virtual_ef800_supervisor_set_cleanup_safe(&supervisor, false);
	result = virtual_ef800_supervisor_step(&supervisor, 1U);
	zassert_true(result.quarantined, NULL);
	zassert_equal(result.error, -4, NULL);
}

ZTEST(shared_virtual_supervisor, test_unrelated_healthy_plugin_can_run)
{
	struct virtual_ef800_supervisor failed;
	struct virtual_ef800_supervisor healthy;
	struct ef800_ui_state ui;
	struct virtual_ef800_plugin_result result;

	ef800_ui_init(&ui);
	virtual_ef800_supervisor_start(&failed, &ui,
		VIRTUAL_EF800_PLUGIN_TIMEOUT, 100U);
	result = virtual_ef800_supervisor_step(&failed, 1U);
	zassert_true(result.quarantined, NULL);
	virtual_ef800_supervisor_start(&healthy, &ui,
		VIRTUAL_EF800_PLUGIN_HEALTHY, 100U);
	result = virtual_ef800_supervisor_step(&healthy, 1U);
	zassert_true(result.completed, NULL);
	zassert_true(result.output_accepted, NULL);
}

ZTEST_SUITE(shared_virtual_supervisor, NULL, NULL, NULL, NULL, NULL);
