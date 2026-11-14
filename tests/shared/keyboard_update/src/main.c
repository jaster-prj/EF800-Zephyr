/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ef800_keyboard_update.h"
#undef EF800_KB_UPDATE_ACK
#undef EF800_KB_UPDATE_NACK
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_firmware_coordinator.h"
#include "ef800_keyboard_update_service.h"
#include "ef800_keyboard_i2c.h"

static void put_le32(uint8_t *p, uint32_t value)
{
	p[0] = value & 0xffU;
	p[1] = (value >> 8) & 0xffU;
	p[2] = (value >> 16) & 0xffU;
	p[3] = (value >> 24) & 0xffU;
}

static void make_header(uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE],
			uint8_t target, uint32_t size, uint32_t crc)
{
	memset(wire, 0, EF800_KB_UPDATE_HEADER_SIZE);
	put_le32(&wire[0], EF800_KB_UPDATE_MAGIC);
	wire[4] = target;
	put_le32(&wire[8], size);
	put_le32(&wire[12], crc);
}

struct coordinator_test_context {
	bool present[3];
	const char *run_paths[3];
	size_t run_count;
	enum ef800_firmware_target_id reported_targets[6];
	enum ef800_keyboard_update_result reported_status[6];
	int reported_results[6];
	size_t report_count;
	int fail_run_index;
	int presence_error_index;
};

static int coordinator_test_present(void *context, const char *path,
				    bool *present)
{
	struct coordinator_test_context *test = context;
	size_t index;

	if (strcmp(path, "updater") == 0) {
		index = 0U;
	} else if (strcmp(path, "bootloader") == 0) {
		index = 1U;
	} else if (strcmp(path, "application") == 0) {
		index = 2U;
	} else {
		return -EINVAL;
	}
	if ((int)index == test->presence_error_index) {
		return -EIO;
	}
	*present = test->present[index];
	return 0;
}

static int coordinator_test_run(void *context, const char *path)
{
	struct coordinator_test_context *test = context;
	size_t index = test->run_count++;

	if (index < ARRAY_SIZE(test->run_paths)) {
		test->run_paths[index] = path;
	}
	return (int)index == test->fail_run_index ? -EHOSTDOWN : 0;
}

static int coordinator_test_report(void *context,
	enum ef800_firmware_target_id target,
	enum ef800_keyboard_update_result status, int result)
{
	struct coordinator_test_context *test = context;
	size_t index = test->report_count++;

	if (index < ARRAY_SIZE(test->reported_targets)) {
		test->reported_targets[index] = target;
		test->reported_status[index] = status;
		test->reported_results[index] = result;
	}
	return 0;
}

static const char *const coordinator_test_paths[] = {
	"updater", "bootloader", "application",
};

static struct ef800_keyboard_update_coordinator make_test_coordinator(
	struct coordinator_test_context *context)
{
	return (struct ef800_keyboard_update_coordinator){
		.context = context,
		.package_present = coordinator_test_present,
		.run_package = coordinator_test_run,
		.report = coordinator_test_report,
	};
}

static bool update_test_storage_mounted(void *context)
{
	ARG_UNUSED(context);
	return true;
}

static bool update_test_storage_externally_owned(void *context)
{
	ARG_UNUSED(context);
	return true;
}

static int update_test_callback(void *context)
{
	ARG_UNUSED(context);
	return 0;
}

static int update_test_identify(void *context, uint16_t address)
{
	ARG_UNUSED(context);
	ARG_UNUSED(address);
	return 0;
}

static int update_test_fallback(void *context, uint8_t *version)
{
	ARG_UNUSED(context);
	ARG_UNUSED(version);
	return 0;
}

static int update_test_wait_application(
	void *context, const struct ef800_keyboard_package_header *header)
{
	ARG_UNUSED(context);
	ARG_UNUSED(header);
	return 0;
}

struct full_update_test_context {
	bool ready;
	bool present[6];
	int fail_target;
	int unsupported_target;
	enum ef800_firmware_target_id run_targets[6];
	size_t run_count;
	enum ef800_firmware_target_id report_targets[12];
	enum ef800_firmware_coordinator_status report_status[12];
	int report_results[12];
	size_t report_count;
};

static const enum ef800_firmware_target_id full_update_targets[] = {
	EF800_FIRMWARE_TARGET_APPLICATION,
	EF800_FIRMWARE_TARGET_BACKUP,
	EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER,
	EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER,
	EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION,
	EF800_FIRMWARE_TARGET_BOOTLOADER,
};

static const char *const full_update_paths[] = {
	"/DATA:/fwu/EF800_APP.pkg",
	"/DATA:/fwu/EF800_BACKUP.pkg",
	"/DATA:/fwu/EF800_KEYBOARD_UPDATER.pkg",
	"/DATA:/fwu/EF800_KEYBOARD_BOOT.pkg",
	"/DATA:/fwu/EF800_KEYBOARD_APP.pkg",
	"/DATA:/fwu/EF800_BOOT.pkg",
};

static bool full_update_ready(void *context)
{
	return ((struct full_update_test_context *)context)->ready;
}

static int full_update_package_present(void *context, const char *path,
	bool *present)
{
	struct full_update_test_context *test = context;

	for (size_t i = 0U; i < ARRAY_SIZE(full_update_paths); ++i) {
		if (strcmp(path, full_update_paths[i]) == 0) {
			*present = test->present[i];
			return 0;
		}
	}
	return -EINVAL;
}

static int full_update_run_target(void *context,
	enum ef800_firmware_target_id target, const char *path)
{
	struct full_update_test_context *test = context;
	size_t run_index = test->run_count++;

	ARG_UNUSED(path);
	if (run_index < ARRAY_SIZE(test->run_targets)) {
		test->run_targets[run_index] = target;
	}
	if ((int)target == test->unsupported_target) {
		return -ENOTSUP;
	}
	if ((int)target == test->fail_target) {
		return -EIO;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(full_update_targets); ++i) {
		if (full_update_targets[i] == target) {
			/* Successful install cleans only its own staged package. */
			test->present[i] = false;
			break;
		}
	}
	return 0;
}

static int full_update_report(void *context,
	enum ef800_firmware_target_id target,
	const char *path,
	enum ef800_firmware_coordinator_status status, int result)
{
	struct full_update_test_context *test = context;
	size_t index = test->report_count++;

	ARG_UNUSED(path);
	if (index < ARRAY_SIZE(test->report_targets)) {
		test->report_targets[index] = target;
		test->report_status[index] = status;
		test->report_results[index] = result;
	}
	return 0;
}

static struct ef800_firmware_coordinator make_full_update_coordinator(
	struct full_update_test_context *context)
{
	return (struct ef800_firmware_coordinator){
		.context = context,
		.ready = full_update_ready,
		.package_present = full_update_package_present,
		.run_target = full_update_run_target,
		.report = full_update_report,
	};
}

ZTEST(ef800_keyboard_update, test_versioned_package_names_select_exact_keyboard_target)
{
	enum ef800_firmware_target_id target = 0U;

	zassert_ok(ef800_keyboard_update_package_select(
		"/DATA:/fwu/EF800_KEYBOARD_UPDATER.pkg", &target), NULL);
	zassert_equal(target, EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER, NULL);
	zassert_ok(ef800_keyboard_update_package_select(
		"/DATA:/fwu/EF800_KEYBOARD_BOOT.pkg", &target), NULL);
	zassert_equal(target, EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER, NULL);
	zassert_ok(ef800_keyboard_update_package_select(
		"/DATA:/fwu/EF800_KEYBOARD_APP.pkg", &target), NULL);
	zassert_equal(target, EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION, NULL);
	zassert_equal(ef800_keyboard_update_package_select(
		"/DATA:/fwu/EF800_BACKUP.pkg", &target), -EINVAL, NULL);
	zassert_equal(ef800_keyboard_update_package_select(
		"/DATA:/fwu/EF800_KEYBOARD_APP.pkg", NULL), -EINVAL, NULL);
}

ZTEST(ef800_keyboard_update, test_coordinator_reports_missing_packages_as_skipped)
{
	struct coordinator_test_context context = {
		.fail_run_index = -1,
		.presence_error_index = -1,
	};
	struct ef800_keyboard_update_coordinator coordinator =
		make_test_coordinator(&context);

	zassert_ok(ef800_keyboard_update_coordinate(&coordinator,
		coordinator_test_paths), NULL);
	zassert_equal(context.run_count, 0U, NULL);
	zassert_equal(context.report_count, 3U, NULL);
	zassert_equal(context.reported_targets[0],
		EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER, NULL);
	zassert_equal(context.reported_targets[1],
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER, NULL);
	zassert_equal(context.reported_targets[2],
		EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION, NULL);
	for (size_t i = 0U; i < context.report_count; ++i) {
		zassert_equal(context.reported_status[i],
			EF800_KEYBOARD_UPDATE_SKIPPED, NULL);
		zassert_equal(context.reported_results[i], 0, NULL);
	}
}

ZTEST(ef800_keyboard_update, test_full_update_progresses_all_six_targets_in_order)
{
	struct full_update_test_context context = {
		.ready = true,
		.present = {true, true, true, true, true, true},
		.fail_target = -1,
		.unsupported_target = -1,
	};
	struct ef800_firmware_coordinator coordinator =
		make_full_update_coordinator(&context);

	zassert_ok(ef800_firmware_coordinate(&coordinator, full_update_paths), NULL);
	zassert_equal(context.run_count, ARRAY_SIZE(full_update_targets), NULL);
	zassert_equal(context.report_count, ARRAY_SIZE(full_update_targets), NULL);
	for (size_t i = 0U; i < ARRAY_SIZE(full_update_targets); ++i) {
		zassert_equal(context.run_targets[i], full_update_targets[i], NULL);
		zassert_equal(context.report_targets[i], full_update_targets[i], NULL);
		zassert_equal(context.report_status[i],
			EF800_FIRMWARE_COORDINATOR_COMPLETE, NULL);
		zassert_equal(context.report_results[i], 0, NULL);
	}
}

ZTEST(ef800_keyboard_update, test_full_update_partial_set_skips_absent_targets)
{
	static const enum ef800_firmware_coordinator_status expected_status[] = {
		EF800_FIRMWARE_COORDINATOR_SKIPPED,
		EF800_FIRMWARE_COORDINATOR_COMPLETE,
		EF800_FIRMWARE_COORDINATOR_SKIPPED,
		EF800_FIRMWARE_COORDINATOR_COMPLETE,
		EF800_FIRMWARE_COORDINATOR_SKIPPED,
		EF800_FIRMWARE_COORDINATOR_SKIPPED,
	};
	struct full_update_test_context context = {
		.ready = true,
		.present = {false, true, false, true, false, false},
		.fail_target = -1,
		.unsupported_target = -1,
	};
	struct ef800_firmware_coordinator coordinator =
		make_full_update_coordinator(&context);

	zassert_ok(ef800_firmware_coordinate(&coordinator, full_update_paths), NULL);
	zassert_equal(context.run_count, 2U, NULL);
	zassert_equal(context.run_targets[0], EF800_FIRMWARE_TARGET_BACKUP, NULL);
	zassert_equal(context.run_targets[1],
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER, NULL);
	zassert_equal(context.report_count, ARRAY_SIZE(full_update_targets), NULL);
	for (size_t i = 0U; i < ARRAY_SIZE(full_update_targets); ++i) {
		zassert_equal(context.report_targets[i], full_update_targets[i], NULL);
		zassert_equal(context.report_status[i],
			expected_status[i], NULL);
	}
}

ZTEST(ef800_keyboard_update, test_full_update_restart_resumes_after_interrupted_target)
{
	struct full_update_test_context context = {
		.ready = true,
		.present = {true, true, true, true, true, false},
		.fail_target = EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER,
		.unsupported_target = -1,
	};
	struct ef800_firmware_coordinator coordinator =
		make_full_update_coordinator(&context);

	zassert_equal(ef800_firmware_coordinate(&coordinator, full_update_paths),
		-EIO, NULL);
	zassert_equal(context.run_count, 4U, NULL);
	zassert_true(context.present[3], "failed target package must remain staged");
	zassert_true(context.present[4], "later target must remain staged");

	context.ready = true; /* Simulated reboot: persisted files are re-inspected. */
	context.fail_target = -1;
	context.run_count = 0U;
	context.report_count = 0U;
	zassert_ok(ef800_firmware_coordinate(&coordinator, full_update_paths), NULL);
	zassert_equal(context.run_count, 2U, NULL);
	zassert_equal(context.run_targets[0],
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER, NULL);
	zassert_equal(context.run_targets[1],
		EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION, NULL);
}

ZTEST(ef800_keyboard_update, test_full_update_does_not_start_while_storage_is_owned)
{
	struct full_update_test_context context = {
		.ready = false,
		.present = {true, true, true, true, true, true},
		.fail_target = -1,
		.unsupported_target = -1,
	};
	struct ef800_firmware_coordinator coordinator =
		make_full_update_coordinator(&context);

	zassert_equal(ef800_firmware_coordinate(&coordinator, full_update_paths),
		-EAGAIN, NULL);
	zassert_equal(context.run_count, 0U, NULL);
	zassert_equal(context.report_count, 0U, NULL);
}

ZTEST(ef800_keyboard_update, test_coordinator_orders_updates_and_reports_completion)
{
	struct coordinator_test_context context = {
		.present = {true, true, true},
		.fail_run_index = -1,
		.presence_error_index = -1,
	};
	struct ef800_keyboard_update_coordinator coordinator =
		make_test_coordinator(&context);
	static const enum ef800_firmware_target_id expected_targets[] = {
		EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER,
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER,
		EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION,
	};

	zassert_ok(ef800_keyboard_update_coordinate(&coordinator,
		coordinator_test_paths), NULL);
	zassert_equal(context.run_count, 3U, NULL);
	zassert_equal(context.report_count, 3U, NULL);
	for (size_t i = 0U; i < 3U; ++i) {
		zassert_equal(strcmp(context.run_paths[i],
			coordinator_test_paths[i]), 0, NULL);
		zassert_equal(context.reported_targets[i], expected_targets[i], NULL);
		zassert_equal(context.reported_status[i],
			EF800_KEYBOARD_UPDATE_COMPLETE, NULL);
		zassert_equal(context.reported_results[i], 0, NULL);
	}
}

ZTEST(ef800_keyboard_update, test_failed_updater_handoff_stops_later_targets)
{
	struct coordinator_test_context context = {
		.present = {true, true, true},
		.fail_run_index = 0,
		.presence_error_index = -1,
	};
	struct ef800_keyboard_update_coordinator coordinator =
		make_test_coordinator(&context);
	int result = ef800_keyboard_update_coordinate(&coordinator,
		coordinator_test_paths);

	zassert_equal(result, -EHOSTDOWN, NULL);
	zassert_equal(context.run_count, 1U, NULL);
	zassert_equal(context.report_count, 1U, NULL);
	zassert_equal(context.reported_targets[0],
		EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER, NULL);
	zassert_equal(context.reported_status[0],
		EF800_KEYBOARD_UPDATE_FAILED, NULL);
	zassert_equal(context.reported_results[0], -EHOSTDOWN, NULL);
}

ZTEST(ef800_keyboard_update, test_coordinator_reports_presence_failure_and_stops)
{
	struct coordinator_test_context context = {
		.present = {true, true, true},
		.fail_run_index = -1,
		.presence_error_index = 1,
	};
	struct ef800_keyboard_update_coordinator coordinator =
		make_test_coordinator(&context);
	int result = ef800_keyboard_update_coordinate(&coordinator,
		coordinator_test_paths);

	zassert_equal(result, -EIO, NULL);
	zassert_equal(context.run_count, 1U, NULL);
	zassert_equal(context.report_count, 2U, NULL);
	zassert_equal(context.reported_targets[0],
		EF800_FIRMWARE_TARGET_KEYBOARD_UPDATER, NULL);
	zassert_equal(context.reported_status[0],
		EF800_KEYBOARD_UPDATE_COMPLETE, NULL);
	zassert_equal(context.reported_targets[1],
		EF800_FIRMWARE_TARGET_KEYBOARD_BOOTLOADER, NULL);
	zassert_equal(context.reported_status[1],
		EF800_KEYBOARD_UPDATE_FAILED, NULL);
}

ZTEST(ef800_keyboard_update, test_image_kind_selects_bounded_update_plan)
{
	struct ef800_keyboard_update_plan plan;

	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_APPLICATION, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_PACKAGE_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_PACKAGE_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_APPLICATION, NULL);
	zassert_equal(plan.service_address, EF800_KEYBOARD_UPDATER_I2C_ADDRESS,
		NULL);
	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_BOOTLOADER, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_BOOT_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_BOOT_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_BOOTLOADER, NULL);
	zassert_ok(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_UPDATER, &plan), NULL);
	zassert_equal(plan.image_base, EF800_KEYBOARD_UPDATER_BASE, NULL);
	zassert_equal(plan.image_limit, EF800_KEYBOARD_UPDATER_LIMIT, NULL);
	zassert_equal(plan.target, EF800_KB_UPDATE_TARGET_UPDATER, NULL);
	zassert_equal(plan.service_address, EF800_KEYBOARD_I2C_ADDRESS, NULL);
	zassert_equal(ef800_keyboard_update_plan_select(0U, &plan), -EINVAL, NULL);
	zassert_equal(ef800_keyboard_update_plan_select(
		EF800_KEYBOARD_IMAGE_APPLICATION, NULL), -EINVAL, NULL);
}

ZTEST(ef800_keyboard_update, test_transaction_requires_complete_adapter_context)
{
	struct ef800_keyboard_update_transaction transaction = {0};

	zassert_equal(ef800_keyboard_update_transaction_run(NULL), -EINVAL, NULL);
	zassert_equal(ef800_keyboard_update_transaction_run(&transaction), -EINVAL,
		NULL);
}

ZTEST(ef800_keyboard_update, test_transaction_defers_when_mtp_or_msc_owns_storage)
{
	bool active = false;
	const struct ef800_keyboard_update_transaction transaction = {
		.package_path = "/DATA:/fwu/EF800_KEYBOARD_APP.pkg",
		.active = &active,
		.storage_mounted = update_test_storage_mounted,
		.storage_externally_owned = update_test_storage_externally_owned,
		.enter_resident_updater = update_test_callback,
		.wait_application_service = update_test_callback,
		.identify = update_test_identify,
		.enter_updater_fallback = update_test_fallback,
		.wait_application = update_test_wait_application,
	};

	zassert_equal(ef800_keyboard_update_transaction_run(&transaction),
		-EAGAIN, NULL);
	zassert_false(active, NULL);
}

ZTEST(ef800_keyboard_update, test_valid_targets_decode_with_fixed_sizes)
{
	uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE];
	struct ef800_kb_update_header header;

	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION,
		EF800_KEYBOARD_PACKAGE_LIMIT, 0x12345678U);
	zassert_true(ef800_kb_update_header_decode(wire, &header), NULL);
	zassert_equal(header.target, EF800_KEYBOARD_IMAGE_APPLICATION, NULL);
	zassert_equal(header.image_size, EF800_KEYBOARD_PACKAGE_LIMIT, NULL);

	make_header(wire, EF800_KEYBOARD_IMAGE_BOOTLOADER,
		EF800_KEYBOARD_BOOT_LIMIT, 0U);
	zassert_true(ef800_kb_update_header_decode(wire, &header), NULL);
	zassert_equal(header.target, EF800_KEYBOARD_IMAGE_BOOTLOADER, NULL);
}

ZTEST(ef800_keyboard_update, test_malformed_header_target_and_bounds_rejected)
{
	uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE];
	struct ef800_kb_update_header header;

	make_header(wire, 0U, 32U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_BOOTLOADER,
		EF800_KEYBOARD_BOOT_LIMIT + 1U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION,
		EF800_KEYBOARD_PACKAGE_LIMIT + 1U, 0U);
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
	make_header(wire, EF800_KEYBOARD_IMAGE_APPLICATION, 1U, 0U);
	wire[7] = 1U;
	zassert_false(ef800_kb_update_header_decode(wire, &header), NULL);
}

ZTEST(ef800_keyboard_update, test_ordered_chunks_and_crc_required)
{
	static const uint8_t image[] = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U};
	struct ef800_kb_update_header header = {
		.target = EF800_KEYBOARD_IMAGE_APPLICATION,
		.image_size = sizeof(image),
		.image_crc32 = ef800_crc32(image, sizeof(image)),
	};
	struct ef800_kb_update_stream stream;

	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_false(ef800_kb_update_stream_chunk(&stream, 1U, image, 2U), NULL);
	zassert_equal(stream.received, 0U, NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, image, 2U), NULL);
	zassert_false(ef800_kb_update_stream_chunk(&stream, 2U, image + 2U,
		EF800_KB_UPDATE_CHUNK_MAX + 1U), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 2U, image + 2U, 3U), NULL);
	zassert_true(ef800_kb_update_stream_finish(&stream), NULL);
	zassert_false(stream.active, NULL);

	header.image_crc32 ^= 1U;
	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, image,
		sizeof(image)), NULL);
	zassert_false(ef800_kb_update_stream_finish(&stream), NULL);
}

ZTEST(ef800_keyboard_update, test_interrupted_application_stream_fails_closed)
{
	static const uint8_t payload[] = {0xaaU, 0xbbU, 0xccU};
	struct ef800_kb_update_header header = {
		.target = EF800_KEYBOARD_IMAGE_APPLICATION,
		.image_size = sizeof(payload),
		.image_crc32 = ef800_crc32(payload, sizeof(payload)),
	};
	struct ef800_kb_update_stream stream;

	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, payload, 2U), NULL);
	zassert_false(ef800_kb_update_stream_finish(&stream), NULL);
	/* A fresh BEGIN resets the interrupted transaction for a clean retry. */
	zassert_true(ef800_kb_update_stream_begin(&stream, &header), NULL);
	zassert_equal(stream.received, 0U, NULL);
	zassert_equal(stream.crc32, 0U, NULL);
	zassert_true(ef800_kb_update_stream_chunk(&stream, 0U, payload,
		sizeof(payload)), NULL);
	zassert_true(ef800_kb_update_stream_finish(&stream), NULL);
}

ZTEST(ef800_keyboard_update, test_update_service_recovery_authorization)
{
	/* The resident updater remains available to repair either replaceable image. */
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_BOOTLOADER), NULL);
	/* Neither service may rewrite the image it is executing. */
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_BOOTLOADER), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
}

ZTEST(ef800_keyboard_update, test_custom_command_frames_and_timeout)
{
	uint8_t frame[] = {EF800_KB_UPDATE_CMD_WRITE_MEMORY, 0U, 0x12U};
	uint8_t encoded[8];
	uint8_t ack[] = {EF800_KB_PROTOCOL_ACK, 0x12U, 0x6BU};
	uint8_t nack[] = {EF800_KB_PROTOCOL_NACK};
	uint8_t busy[] = {EF800_KB_PROTOCOL_BUSY};
	uint8_t version[EF800_KB_UPDATE_VERSION_SIZE];

	zassert_equal(EF800_KB_PROTOCOL_ACK, 0x79U, NULL);
	zassert_equal(EF800_KB_PROTOCOL_NACK, 0x1FU, NULL);
	zassert_equal(EF800_KB_PROTOCOL_BUSY, 0x76U, NULL);
	zassert_equal(ef800_kb_update_version_encode(
		EF800_KB_UPDATE_VERSION_COMMAND, EF800_KB_UPDATE_VERSION_ACK,
		1U, 2U, 3U, version, sizeof(version)), sizeof(version), NULL);
	zassert_equal(version[0], 0xF2U, NULL);
	zassert_equal(version[1], 1U, NULL);
	zassert_equal(version[2], 2U, NULL);
	zassert_equal(version[3], 3U, NULL);
	zassert_equal(version[4], 0x06U, NULL);
	zassert_equal(ef800_kb_update_version_encode(
		EF800_KB_UPDATE_VERSION_COMMAND, EF800_KB_UPDATE_VERSION_ACK,
		1U, 2U, 3U, version, sizeof(version) - 1U), 0U, NULL);
	frame[1] = (uint8_t)~frame[0];
	frame[2] = ef800_kb_update_checksum(frame, 2U);
	zassert_true(ef800_kb_update_frame_valid(frame, sizeof(frame)), NULL);
	frame[1] ^= 1U;
	zassert_false(ef800_kb_update_frame_valid(frame, sizeof(frame)), NULL);
	zassert_equal(ef800_kb_update_frame_encode(0x31U, &frame[2], 1U,
		encoded, sizeof(encoded)), 4U, NULL);
	zassert_equal(encoded[0], 0x31U, NULL);
	zassert_equal(encoded[1], 0xCEU, NULL);
	zassert_true(ef800_kb_update_frame_valid(encoded, 4U), NULL);
	zassert_true(ef800_kb_update_response_valid(ack, sizeof(ack)), NULL);
	zassert_true(ef800_kb_update_response_valid(nack, sizeof(nack)), NULL);
	zassert_true(ef800_kb_update_response_valid(busy, sizeof(busy)), NULL);
	ack[2] ^= 1U;
	zassert_false(ef800_kb_update_response_valid(ack, sizeof(ack)), NULL);
	zassert_false(ef800_kb_update_response_valid(busy, 2U), NULL);
	zassert_false(ef800_kb_update_frame_timing_valid(100U,
		100U + EF800_KB_UPDATE_FRAME_TIMEOUT_MS + 1U), NULL);
	zassert_true(ef800_kb_update_frame_timing_valid(UINT32_MAX - 100U,
		899U), NULL);
}

ZTEST(ef800_keyboard_update, test_package_targets_match_partition_map)
{
	struct ef800_keyboard_package_header header = {
		.magic = EF800_KEYBOARD_PACKAGE_MAGIC,
		.format = EF800_KEYBOARD_PACKAGE_FORMAT,
		.header_size = EF800_KEYBOARD_PACKAGE_HEADER_SIZE,
		.target_id = EF800_KEYBOARD_PACKAGE_TARGET,
		.image_base = EF800_KEYBOARD_PACKAGE_BASE,
		.image_size = EF800_KEYBOARD_PACKAGE_LIMIT,
		.image_kind = EF800_KEYBOARD_IMAGE_APPLICATION,
	};

	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = EF800_KEYBOARD_IMAGE_BOOTLOADER;
	header.image_base = EF800_KEYBOARD_BOOT_BASE;
	header.image_size = EF800_KEYBOARD_BOOT_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = EF800_KEYBOARD_IMAGE_UPDATER;
	header.image_base = EF800_KEYBOARD_UPDATER_BASE;
	header.image_size = EF800_KEYBOARD_UPDATER_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_base = EF800_KEYBOARD_PACKAGE_BASE;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
}

ZTEST(ef800_keyboard_update, test_update_service_little_endian_helpers)
{
	uint8_t bytes[4];

	ef800_keyboard_update_put_le16(bytes, UINT16_C(0xA17C));
	zassert_equal(bytes[0], 0x7CU, NULL);
	zassert_equal(bytes[1], 0xA1U, NULL);
	ef800_keyboard_update_put_le32(bytes, UINT32_C(0x78563412));
	zassert_equal(bytes[0], 0x12U, NULL);
	zassert_equal(bytes[1], 0x34U, NULL);
	zassert_equal(bytes[2], 0x56U, NULL);
	zassert_equal(bytes[3], 0x78U, NULL);
	zassert_equal(ef800_keyboard_update_get_le32(bytes),
		UINT32_C(0x78563412), NULL);
}

ZTEST(ef800_keyboard_update, test_fixed_partition_page_and_write_bounds)
{
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 0U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 15U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_BOOTLOADER, 16U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 16U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 111U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 112U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 112U), NULL);
	zassert_true(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 127U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 128U), NULL);
	zassert_false(ef800_kb_update_page_valid(
		(enum ef800_kb_update_target)99, 0U), NULL);

	zassert_true(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 256U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08007FF8U, 16U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008001U, 8U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 7U), NULL);
	zassert_false(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_APPLICATION, 0x08008000U, 264U), NULL);
	zassert_true(ef800_kb_update_write_valid(
		EF800_KB_UPDATE_TARGET_UPDATER, 0x08038000U, 8U), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
	zassert_true(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_UPDATER), NULL);
	zassert_false(ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
		EF800_KB_UPDATE_TARGET_APPLICATION), NULL);
}

ZTEST_SUITE(ef800_keyboard_update, NULL, NULL, NULL, NULL, NULL);
