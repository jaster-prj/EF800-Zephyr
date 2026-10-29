/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_frontpanel.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>

#include "ef800_virtual_protocol.h"
#include "ef800_virtual_profile.h"
#include "ef800_virtual_log.h"
#include "ef800_plugin_host.h"
#include "plugin_manager/plugin_package.h"
#include "ef800_virtual_usb_peer.h"

/* Current PluginManager main keeps package target values format-defined rather
 * than exposing a native target header. The EF800 PMP contract reserves 2 for
 * the 32-bit native_sim/i386 package profile. */
#ifndef PM_PACKAGE_TARGET_NATIVE_SIM
#define PM_PACKAGE_TARGET_NATIVE_SIM 2u
#endif

LOG_MODULE_REGISTER(ef800_virtual_frontpanel, LOG_LEVEL_INF);

#define FRONTPANEL_STACK_SIZE 12288
#define FRONTPANEL_PRIORITY 6
#define FRONTPANEL_QUEUE_SIZE 8
#define PLUGIN_UPLOAD_MAX_SIZE (32u * 1024u)
#define PLUGIN_UPLOAD_ID 0x504C5547u /* "PLUG" */
#define PLUGIN_PATH_MAX 80u

static struct ef800_virtual_frontpanel *active_panel;
static int listen_fd = -1;
static int client_fd = -1;
static uint8_t send_frame[EF800_VIRTUAL_PROTOCOL_HEADER_SIZE +
	EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD];
static uint8_t state_payload[EF800_VIRTUAL_DISPLAY_FRAME_SIZE +
	sizeof(uint32_t)];
static uint8_t receive_buffer[EF800_VIRTUAL_PROTOCOL_HEADER_SIZE +
	EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD];
static pthread_t frontpanel_pthread;
static uint32_t last_sent_sequence;
static bool lcd_contrast_enabled;
static uint8_t lcd_contrast_duty = 78U;
static uint8_t composition_state[EF800_VIRTUAL_COMPOSITION_STATE_MAX];
static size_t composition_state_length;
static ef800_virtual_plugin_assign_fn plugin_assign_callback;
static void *plugin_assign_context;
static ef800_virtual_plugin_execute_fn plugin_execute_callback;
static void *plugin_execute_context;
static ef800_virtual_composition_set_fn composition_set_callback;
static void *composition_set_context;
static struct {
	uint32_t id;
	uint32_t expected_size;
	uint32_t expected_crc;
	uint32_t received;
	char name[64];
	struct fs_file_t file;
	bool active;
	int64_t last_activity_ms;
} plugin_upload;

static struct {
	uint32_t request_id;
	uint32_t expected_size;
	uint32_t expected_crc;
	uint32_t received;
	uint8_t storage;
	char path[EF800_VIRTUAL_MTP_MAX_PATH + 1U];
	bool active;
	uint8_t data[EF800_VIRTUAL_MTP_MAX_FILE_SIZE];
} mtp_upload;
static uint8_t mtp_download_data[EF800_VIRTUAL_MTP_MAX_FILE_SIZE];
static struct {
	uint32_t request_id;
	uint32_t total;
	uint32_t sent;
	uint32_t crc;
	bool active;
} mtp_download;

static struct ef800_virtual_log_store log_store;
static struct ef800_virtual_log_entry replay_entries[EF800_VIRTUAL_LOG_HISTORY_COUNT];
struct mtp_worker_message {
	uint8_t type;
	uint16_t length;
	uint8_t payload[EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD];
};
K_MSGQ_DEFINE(mtp_worker_queue, sizeof(struct mtp_worker_message), 8,
	__alignof__(uint32_t));
K_THREAD_STACK_DEFINE(mtp_worker_stack, 8192);
static struct k_thread mtp_worker_thread;
static pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;

static void frontpanel_thread_main(void *arg1, void *arg2, void *arg3);
static int plugin_reply(uint8_t status, int32_t error, uint32_t value);

void ef800_virtual_frontpanel_log_record(uint8_t severity,
						 const uint8_t *text, size_t length)
{
	ef800_virtual_log_store_record(&log_store, severity, text, length);
}

static void *frontpanel_pthread_main(void *arg)
{
	frontpanel_thread_main(NULL, NULL, NULL);
	return NULL;
}

static int send_message(uint8_t type, const uint8_t *payload, uint16_t length)
{
	size_t frame_length;
	int ret = 0;

	pthread_mutex_lock(&send_mutex);
	frame_length = ef800_virtual_protocol_encode(type, payload, length,
		send_frame, sizeof(send_frame));
	if (client_fd < 0 || frame_length == 0U) {
		ret = -ENOTCONN;
		goto done;
	}
	ssize_t sent = send(client_fd, send_frame, frame_length, MSG_NOSIGNAL);
	if (sent != (ssize_t)frame_length) {
		ret = -EIO;
	}
done:
	pthread_mutex_unlock(&send_mutex);
	return ret;
}

static int send_log_entry(const struct ef800_virtual_log_entry *entry)
{
	uint8_t payload[EF800_VIRTUAL_LOG_PAYLOAD_HEADER_SIZE +
		EF800_VIRTUAL_LOG_MAX_TEXT];
	struct ef800_virtual_log_record record = {
		.severity = entry->severity,
		.flags = entry->flags,
		.sequence = entry->sequence,
		.text_length = entry->text_length,
		.text = entry->text,
	};
	size_t length = ef800_virtual_log_encode(&record, payload, sizeof(payload));

	return length == 0U ? -EINVAL : send_message(EF800_VIRTUAL_MSG_LOG_RECORD,
		payload, (uint16_t)length);
}

static void drain_log_queue(void)
{
	struct ef800_virtual_log_entry entry;

	if (ef800_virtual_log_store_pop(&log_store, &entry) && send_log_entry(&entry) != 0) {
		return;
	}
}

static void send_log_history(void)
{
	uint32_t last_sequence;
	size_t count = ef800_virtual_log_store_snapshot(&log_store, replay_entries,
		ARRAY_SIZE(replay_entries), &last_sequence);

	ARG_UNUSED(last_sequence);
	for (size_t index = 0U; index < count; ++index) {
		if (send_log_entry(&replay_entries[index]) != 0) {
			return;
		}
	}
}

static void send_lcd_contrast_state(void)
{
	uint8_t payload[2];

	pthread_mutex_lock(&send_mutex);
	payload[0] = lcd_contrast_enabled ? 1U : 0U;
	payload[1] = lcd_contrast_duty;
	pthread_mutex_unlock(&send_mutex);
	(void)send_message(EF800_VIRTUAL_MSG_LCD_CONTRAST, payload,
		(uint16_t)sizeof(payload));
}

static void send_composition_state(void)
{
	uint8_t payload[EF800_VIRTUAL_COMPOSITION_STATE_MAX];
	size_t length;

	pthread_mutex_lock(&send_mutex);
	length = composition_state_length;
	if (length != 0U) {
		memcpy(payload, composition_state, length);
	}
	pthread_mutex_unlock(&send_mutex);
	if (length != 0U) {
		(void)send_message(EF800_VIRTUAL_MSG_COMPOSITION_STATE, payload,
			(uint16_t)length);
	}
}

static void send_current_state(void)
{
	char capabilities[128];
	uint32_t sequence;

	snprintk(capabilities, sizeof(capabilities),
		"lcd=virtual keypad=virtual mode=interactive plugins=%u",
		(unsigned)ef800_plugin_host_available_count());
	(void)send_message(EF800_VIRTUAL_MSG_CAPABILITIES,
		(const uint8_t *)capabilities, (uint16_t)strlen(capabilities));
	(void)plugin_reply(0U, 0, (uint32_t)ef800_plugin_host_available_count());
	send_lcd_contrast_state();
	send_composition_state();
	if (active_panel == NULL || ef800_virtual_display_snapshot(
		active_panel->display, &state_payload[sizeof(sequence)],
		EF800_VIRTUAL_DISPLAY_FRAME_SIZE, &sequence) != 0) {
		return;
	}
	memcpy(state_payload, &sequence, sizeof(sequence));
	(void)send_message(EF800_VIRTUAL_MSG_LCD_FRAME, state_payload,
		(uint16_t)sizeof(state_payload));
}

void ef800_virtual_frontpanel_set_lcd_contrast(bool enabled,
						uint8_t duty_percent)
{
	if (duty_percent > 100U) {
		duty_percent = 78U;
	}
	pthread_mutex_lock(&send_mutex);
	lcd_contrast_enabled = enabled;
	lcd_contrast_duty = duty_percent;
	pthread_mutex_unlock(&send_mutex);
	send_lcd_contrast_state();
}

void ef800_virtual_frontpanel_set_composition_callback(
	ef800_virtual_composition_set_fn callback, void *context)
{
	composition_set_callback = callback;
	composition_set_context = context;
}

void ef800_virtual_frontpanel_set_composition_state(const char *json,
						    size_t length)
{
	if (json == NULL || length == 0U ||
	    length > sizeof(composition_state) ||
	    length > EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD) {
		return;
	}
	pthread_mutex_lock(&send_mutex);
	memcpy(composition_state, json, length);
	composition_state_length = length;
	pthread_mutex_unlock(&send_mutex);
	send_composition_state();
}

static void put_u32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool composition_identifier_valid(const char *text, size_t length)
{
	if (text == NULL || length == 0U ||
	    length > EF800_VIRTUAL_COMPOSITION_ID_MAX ||
	    text[0] < 'A' || text[0] > 'Z') {
		return false;
	}
	for (size_t index = 1U; index < length; ++index) {
		if (!((text[index] >= 'A' && text[index] <= 'Z') ||
		      (text[index] >= '0' && text[index] <= '9'))) {
			return false;
		}
	}
	return true;
}

static void handle_composition_control(const struct ef800_virtual_message *message)
{
	uint8_t result[9] = {0};
	uint32_t request_id = 0U;
	int ret = -EINVAL;

	if (message->length >= 6U) {
		request_id = get_u32(message->payload);
		size_t group_length = message->payload[4];
		size_t token_length = message->payload[5];

		if (group_length != 0U && token_length != 0U &&
		    group_length <= EF800_VIRTUAL_COMPOSITION_ID_MAX &&
		    token_length <= EF800_VIRTUAL_COMPOSITION_ID_MAX &&
		    message->length == 6U + group_length + token_length) {
			char group[EF800_VIRTUAL_COMPOSITION_ID_MAX + 1U];
			char token[EF800_VIRTUAL_COMPOSITION_ID_MAX + 1U];

			memcpy(group, &message->payload[6], group_length);
			group[group_length] = '\0';
			memcpy(token, &message->payload[6U + group_length], token_length);
			token[token_length] = '\0';
			if (composition_identifier_valid(group, group_length) &&
			    composition_identifier_valid(token, token_length) &&
			    composition_set_callback != NULL) {
				ret = composition_set_callback(composition_set_context, group, token);
			} else if (composition_set_callback == NULL) {
				ret = -ENOTSUP;
			}
		}
	}
	put_u32(result, request_id);
	result[4] = ret == 0 ? 0U : 1U;
	put_u32(&result[5], (uint32_t)(int32_t)ret);
	(void)send_message(EF800_VIRTUAL_MSG_COMPOSITION_RESULT, result,
		(uint16_t)sizeof(result));
}

static void put_u16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static uint32_t mtp_crc32(const uint8_t *data, size_t length)
{
	uint32_t crc = 0xFFFFFFFFU;

	for (size_t index = 0U; index < length; ++index) {
		crc ^= data[index];
		for (unsigned int bit = 0U; bit < 8U; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

static int mtp_send_result(uint32_t request_id, int status,
			   uint32_t transferred, uint32_t total, uint32_t crc)
{
	uint8_t payload[20];

	put_u32(&payload[0], request_id);
	put_u32(&payload[4], (uint32_t)(int32_t)status);
	put_u32(&payload[8], transferred);
	put_u32(&payload[12], total);
	put_u32(&payload[16], crc);
	return send_message(EF800_VIRTUAL_MSG_MTP_RESULT, payload,
		(uint16_t)sizeof(payload));
}

static int mtp_send_progress(uint32_t request_id, uint32_t transferred,
			     uint32_t total)
{
	uint8_t payload[12];

	put_u32(&payload[0], request_id);
	put_u32(&payload[4], transferred);
	put_u32(&payload[8], total);
	return send_message(EF800_VIRTUAL_MSG_MTP_PROGRESS, payload,
		(uint16_t)sizeof(payload));
}

static bool mtp_path_payload_valid(const uint8_t *path, size_t length)
{
	if (path == NULL || length > EF800_VIRTUAL_MTP_MAX_PATH) {
		return false;
	}
	for (size_t index = 0U; index < length; ++index) {
		if (path[index] == 0U) {
			return false;
		}
	}
	return true;
}

static size_t mtp_bounded_strlen(const char *text, size_t maximum)
{
	size_t length = 0U;

	while (length < maximum && text[length] != '\0') {
		length++;
	}
	return length;
}

static int mtp_handle_list(const uint8_t *payload, size_t length)
{
	struct ef800_virtual_mtp_entry entries[EF800_VIRTUAL_MTP_MAX_OBJECTS];
	uint8_t response[EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD];
	uint32_t request_id;
	size_t path_length;
	size_t count = 0U;
	int status;
	size_t response_length = 9U;

	if (payload == NULL || length < 6U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	path_length = payload[5];
	if (request_id == 0U || payload[4] < EF800_VIRTUAL_MTP_DATA ||
		payload[4] > EF800_VIRTUAL_MTP_FWU ||
		path_length > EF800_VIRTUAL_MTP_MAX_PATH || length != 6U + path_length ||
		!mtp_path_payload_valid(&payload[6], path_length)) {
		status = -EINVAL;
	} else {
		char path[EF800_VIRTUAL_MTP_MAX_PATH + 1U];

		memcpy(path, &payload[6], path_length);
		path[path_length] = '\0';
		status = ef800_virtual_usb_peer_mtp_list(
			(enum ef800_virtual_mtp_storage)payload[4], path,
			entries, ARRAY_SIZE(entries), &count);
	}
	if (status != 0) {
		count = 0U;
	}
	put_u32(&response[0], request_id);
	put_u32(&response[4], (uint32_t)(int32_t)status);
	response[8] = (uint8_t)count;
	for (size_t index = 0U; index < count; ++index) {
		size_t name_length = mtp_bounded_strlen(entries[index].name,
			EF800_VIRTUAL_MTP_MAX_NAME + 1U);
		size_t entry_length = 10U + name_length;

		if (name_length == 0U || name_length > EF800_VIRTUAL_MTP_MAX_NAME ||
			response_length + entry_length > sizeof(response)) {
			status = -EOVERFLOW;
			count = 0U;
			response_length = 9U;
			put_u32(&response[4], (uint32_t)(int32_t)status);
			response[8] = 0U;
			break;
		}
		put_u32(&response[response_length], entries[index].handle);
		put_u32(&response[response_length + 4U], entries[index].size);
		response[response_length + 8U] = entries[index].directory ? 1U : 0U;
		response[response_length + 9U] = (uint8_t)name_length;
		memcpy(&response[response_length + 10U], entries[index].name,
			name_length);
		response_length += entry_length;
	}
	return send_message(EF800_VIRTUAL_MSG_MTP_LIST_RESULT, response,
		(uint16_t)response_length);
}

static int mtp_handle_download(const uint8_t *payload, size_t length)
{
	uint32_t request_id;
	size_t path_length;
	size_t downloaded = 0U;
	int status;

	if (payload == NULL || length < 6U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	path_length = payload[5];
	if (request_id == 0U || payload[4] < EF800_VIRTUAL_MTP_DATA ||
		payload[4] > EF800_VIRTUAL_MTP_FWU ||
		path_length == 0U || path_length > EF800_VIRTUAL_MTP_MAX_PATH ||
		length != 6U + path_length ||
		!mtp_path_payload_valid(&payload[6], path_length)) {
		return mtp_send_result(request_id, -EINVAL, 0U, 0U, 0U);
	}
	if (mtp_download.active || mtp_upload.active) {
		return mtp_send_result(request_id, -EBUSY, 0U, 0U, 0U);
	}
	char path[EF800_VIRTUAL_MTP_MAX_PATH + 1U];
	memcpy(path, &payload[6], path_length);
	path[path_length] = '\0';
	status = ef800_virtual_usb_peer_mtp_download(
		(enum ef800_virtual_mtp_storage)payload[4], path, mtp_download_data,
		sizeof(mtp_download_data), &downloaded);
	if (status != 0) {
		return mtp_send_result(request_id, status, (uint32_t)downloaded, 0U, 0U);
	}
	mtp_download.request_id = request_id;
	mtp_download.total = (uint32_t)downloaded;
	mtp_download.sent = 0U;
	mtp_download.crc = mtp_crc32(mtp_download_data, downloaded);
	mtp_download.active = downloaded != 0U;
	if (downloaded == 0U) {
		return mtp_send_result(request_id, 0U, 0U, 0U, mtp_download.crc);
	}
	/* Sending one bounded frame at a time leaves the control loop available to
	 * process cancellation before the next chunk is requested. */
	size_t chunk_length = MIN(downloaded,
		(size_t)EF800_VIRTUAL_MTP_CHUNK_SIZE);
	uint8_t chunk[10U + EF800_VIRTUAL_MTP_CHUNK_SIZE];
	put_u32(&chunk[0], request_id);
	put_u32(&chunk[4], 0U);
	put_u16(&chunk[8], (uint16_t)chunk_length);
	memcpy(&chunk[10], mtp_download_data, chunk_length);
	status = send_message(EF800_VIRTUAL_MSG_MTP_DOWNLOAD_CHUNK,
		chunk, (uint16_t)(10U + chunk_length));
	if (status != 0) {
		mtp_download.active = false;
		return status;
	}
	mtp_download.sent = (uint32_t)chunk_length;
	return mtp_send_progress(request_id, mtp_download.sent, mtp_download.total);
}

static int mtp_handle_download_next(const uint8_t *payload, size_t length)
{
	uint32_t request_id;
	uint32_t offset;

	if (payload == NULL || length != 8U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	offset = get_u32(&payload[4]);
	if (!mtp_download.active || request_id != mtp_download.request_id ||
		offset != mtp_download.sent) {
		return mtp_send_result(request_id, -EINVAL, mtp_download.sent,
			mtp_download.total, 0U);
	}
	if (mtp_download.sent == mtp_download.total) {
		uint32_t total = mtp_download.total;
		uint32_t crc = mtp_download.crc;

		mtp_download.active = false;
		return mtp_send_result(request_id, 0U, total, total, crc);
	}
	size_t remaining = mtp_download.total - mtp_download.sent;
	size_t chunk_length = MIN(remaining,
		(size_t)EF800_VIRTUAL_MTP_CHUNK_SIZE);
	uint8_t chunk[10U + EF800_VIRTUAL_MTP_CHUNK_SIZE];

	put_u32(&chunk[0], request_id);
	put_u32(&chunk[4], mtp_download.sent);
	put_u16(&chunk[8], (uint16_t)chunk_length);
	memcpy(&chunk[10], &mtp_download_data[mtp_download.sent], chunk_length);
	int ret = send_message(EF800_VIRTUAL_MSG_MTP_DOWNLOAD_CHUNK, chunk,
		(uint16_t)(10U + chunk_length));
	if (ret != 0) {
		mtp_download.active = false;
		return ret;
	}
	mtp_download.sent += (uint32_t)chunk_length;
	return mtp_send_progress(request_id, mtp_download.sent, mtp_download.total);
}

static int mtp_handle_upload_begin(const uint8_t *payload, size_t length)
{
	uint32_t request_id;
	size_t path_length;
	int status = 0;

	if (payload == NULL || length < 14U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	path_length = payload[5];
	if (request_id == 0U || payload[4] < EF800_VIRTUAL_MTP_DATA ||
		payload[4] > EF800_VIRTUAL_MTP_FWU || path_length == 0U ||
		path_length > EF800_VIRTUAL_MTP_MAX_PATH || length != 14U + path_length ||
		!mtp_path_payload_valid(&payload[14], path_length)) {
		status = -EINVAL;
	} else if (get_u32(&payload[6]) == 0U ||
		get_u32(&payload[6]) > EF800_VIRTUAL_MTP_MAX_FILE_SIZE) {
		status = -EFBIG;
	} else if (mtp_upload.active || mtp_download.active) {
		status = -EBUSY;
	} else {
		mtp_upload.request_id = request_id;
		mtp_upload.storage = payload[4];
		mtp_upload.expected_size = get_u32(&payload[6]);
		mtp_upload.expected_crc = get_u32(&payload[10]);
		mtp_upload.received = 0U;
		memcpy(mtp_upload.path, &payload[14], path_length);
		mtp_upload.path[path_length] = '\0';
		mtp_upload.active = true;
	}
	return mtp_send_result(request_id, status, 0U,
		status == 0 ? mtp_upload.expected_size : 0U, 0U);
}

static int mtp_handle_upload_chunk(const uint8_t *payload, size_t length)
{
	uint32_t request_id;
	uint32_t offset;
	size_t chunk_length;

	if (payload == NULL || length < 9U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	offset = get_u32(&payload[4]);
	chunk_length = length - 8U;
	if (!mtp_upload.active || request_id != mtp_upload.request_id ||
		offset != mtp_upload.received || chunk_length == 0U ||
		chunk_length > EF800_VIRTUAL_MTP_CHUNK_SIZE ||
		chunk_length > mtp_upload.expected_size - mtp_upload.received) {
		return mtp_send_result(request_id, -EINVAL,
			mtp_upload.received, mtp_upload.expected_size, 0U);
	}
	memcpy(&mtp_upload.data[mtp_upload.received], &payload[8], chunk_length);
	mtp_upload.received += (uint32_t)chunk_length;
	if (mtp_send_progress(request_id, mtp_upload.received,
		mtp_upload.expected_size) != 0) {
		return -EIO;
	}
	return 0;
}

static int mtp_handle_upload_commit(const uint8_t *payload, size_t length)
{
	uint32_t request_id;
	int status;

	if (payload == NULL || length != 4U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	if (request_id == 0U) {
		mtp_download.active = false;
		mtp_upload.active = false;
	mtp_upload.received = 0U;
		return 0;
	}
	if (!mtp_upload.active || request_id != mtp_upload.request_id) {
		return mtp_send_result(request_id, -ENOENT, 0U, 0U, 0U);
	}
	if (mtp_upload.received != mtp_upload.expected_size) {
		status = -EMSGSIZE;
	} else if (mtp_crc32(mtp_upload.data, mtp_upload.received) !=
		mtp_upload.expected_crc) {
		status = -EBADMSG;
	} else {
		status = ef800_virtual_usb_peer_mtp_upload(
			(enum ef800_virtual_mtp_storage)mtp_upload.storage,
			mtp_upload.path, mtp_upload.data, mtp_upload.received);
	}
	uint32_t transferred = mtp_upload.received;
	uint32_t total = mtp_upload.expected_size;
	uint32_t crc = mtp_upload.expected_crc;
	mtp_upload.active = false;
	mtp_upload.received = 0U;
	return mtp_send_result(request_id, status, transferred, total,
		status == 0 ? crc : 0U);
}

static int mtp_handle_cancel(const uint8_t *payload, size_t length)
{
	uint32_t request_id;

	if (payload == NULL || length != 4U) {
		return -EINVAL;
	}
	request_id = get_u32(payload);
	if (mtp_download.active && mtp_download.request_id == request_id) {
		uint32_t total = mtp_download.total;
		uint32_t sent = mtp_download.sent;

		mtp_download.active = false;
		return mtp_send_result(request_id, -ECANCELED, sent, total, 0U);
	}
	if (!mtp_upload.active || mtp_upload.request_id != request_id) {
		return mtp_send_result(request_id, -ENOENT, 0U, 0U, 0U);
	}
	mtp_upload.active = false;
	mtp_upload.received = 0U;
	return mtp_send_result(request_id, -ECANCELED, 0U,
		mtp_upload.expected_size, 0U);
}

static void mtp_worker_main(void *arg1, void *arg2, void *arg3)
{
	struct mtp_worker_message message;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		if (k_msgq_get(&mtp_worker_queue, &message, K_FOREVER) != 0) {
			continue;
		}
		switch (message.type) {
		case EF800_VIRTUAL_MSG_MTP_LIST:
			(void)mtp_handle_list(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_DOWNLOAD:
			(void)mtp_handle_download(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_DOWNLOAD_NEXT:
			(void)mtp_handle_download_next(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_UPLOAD_BEGIN:
			(void)mtp_handle_upload_begin(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_UPLOAD_CHUNK:
			(void)mtp_handle_upload_chunk(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_UPLOAD_COMMIT:
			(void)mtp_handle_upload_commit(message.payload, message.length);
			break;
		case EF800_VIRTUAL_MSG_MTP_CANCEL:
			(void)mtp_handle_cancel(message.payload, message.length);
			break;
		default:
			LOG_WRN("Ignoring unknown MTP worker message type %u", message.type);
			break;
		}
	}
}

static int mtp_worker_init(void)
{
	k_tid_t thread = k_thread_create(&mtp_worker_thread, mtp_worker_stack,
		K_THREAD_STACK_SIZEOF(mtp_worker_stack), mtp_worker_main,
		NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

	if (thread == NULL) {
		return -EIO;
	}
	(void)k_thread_name_set(thread, "virtual_mtp");
	return 0;
}
SYS_INIT(mtp_worker_init, APPLICATION, 5);

static int mtp_queue_message(const struct ef800_virtual_message *message)
{
	struct mtp_worker_message queued;
	int ret;

	if (message == NULL || message->length > sizeof(queued.payload)) {
		return -EINVAL;
	}
	queued.type = message->type;
	queued.length = message->length;
	if (message->length != 0U) {
		memcpy(queued.payload, message->payload, message->length);
	}
	ret = k_msgq_put(&mtp_worker_queue, &queued, K_MSEC(100U));
	if (ret == 0) {
		return 0;
	}
	uint32_t request_id = message->length >= sizeof(uint32_t) ?
		get_u32(message->payload) : 0U;
	if (message->type == EF800_VIRTUAL_MSG_MTP_LIST) {
		uint8_t response[9];

		put_u32(&response[0], request_id);
		put_u32(&response[4], (uint32_t)(int32_t)-EBUSY);
		response[8] = 0U;
		(void)send_message(EF800_VIRTUAL_MSG_MTP_LIST_RESULT, response,
			(uint16_t)sizeof(response));
	} else if (request_id != 0U) {
		(void)mtp_send_result(request_id, -EBUSY, 0U, 0U, 0U);
	}
	return ret;
}

static void mtp_queue_disconnect_reset(void)
{
	struct ef800_virtual_message reset = {
		.version = EF800_VIRTUAL_PROTOCOL_VERSION,
		.type = EF800_VIRTUAL_MSG_MTP_CANCEL,
		.length = sizeof(uint32_t),
	};
	uint8_t payload[sizeof(uint32_t)] = {0U};

	reset.payload = payload;
	(void)mtp_queue_message(&reset);
}

static bool safe_plugin_name(const uint8_t *name, size_t length)
{
	if (length == 0U || length >= sizeof(plugin_upload.name) ||
		length < 4U || name[length - 4U] != '.') {
		return false;
	}
	for (size_t index = 0U; index < length; ++index) {
		uint8_t value = name[index];
		if (!((value >= 'a' && value <= 'z') ||
			(value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') || value == '_' || value == '-' ||
			value == '.')) {
			return false;
		}
	}
	return name[length - 3U] == 'p' || name[length - 3U] == 'P';
}

static int plugin_reply(uint8_t status, int32_t error, uint32_t value)
{
	uint8_t payload[9];
	payload[0] = status;
	put_u32(&payload[1], (uint32_t)error);
	put_u32(&payload[5], value);
	return send_message(EF800_VIRTUAL_MSG_PLUGIN_STATUS, payload,
		(uint16_t)sizeof(payload));
}

void ef800_virtual_frontpanel_set_plugin_assign_callback(
	ef800_virtual_plugin_assign_fn callback, void *context)
{
	plugin_assign_callback = callback;
	plugin_assign_context = context;
}

void ef800_virtual_frontpanel_set_plugin_execute_callback(
	ef800_virtual_plugin_execute_fn callback, void *context)
{
	plugin_execute_callback = callback;
	plugin_execute_context = context;
}

static int plugin_upload_begin(const uint8_t *payload, size_t length)
{
	uint8_t name_length;
	int ret;

	if (payload == NULL || length < 13U) return -EINVAL;
	if (get_u32(payload) != PLUGIN_UPLOAD_ID) return -EPROTO;
	name_length = payload[12];
	if (length != 13U + name_length ||
		get_u32(&payload[4]) == 0U || get_u32(&payload[4]) > PLUGIN_UPLOAD_MAX_SIZE ||
		!safe_plugin_name(&payload[13], name_length)) return -EINVAL;
	(void)fs_mkdir("/DATA:/plugins");
	if (plugin_upload.active) {
		(void)fs_close(&plugin_upload.file);
		(void)fs_unlink("/DATA:/plugins/.upload.tmp");
	}
	plugin_upload.id = PLUGIN_UPLOAD_ID;
	plugin_upload.expected_size = get_u32(&payload[4]);
	plugin_upload.expected_crc = get_u32(&payload[8]);
	plugin_upload.received = 0U;
	plugin_upload.last_activity_ms = k_uptime_get();
	memcpy(plugin_upload.name, &payload[13], name_length);
	plugin_upload.name[name_length] = '\0';
	fs_file_t_init(&plugin_upload.file);
	ret = fs_open(&plugin_upload.file, "/DATA:/plugins/.upload.tmp",
		FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	plugin_upload.active = ret == 0;
	return ret;
}

static int plugin_upload_chunk(const uint8_t *payload, size_t length)
{
	uint32_t offset;
	ssize_t written;

	if (!plugin_upload.active || payload == NULL || length < 8U ||
		get_u32(payload) != PLUGIN_UPLOAD_ID) return -EPROTO;
	if (k_uptime_get() - plugin_upload.last_activity_ms > 60000) {
		(void)fs_close(&plugin_upload.file);
		(void)fs_unlink("/DATA:/plugins/.upload.tmp");
		plugin_upload.active = false;
		return -ETIMEDOUT;
	}
	offset = get_u32(&payload[4]);
	if (offset != plugin_upload.received ||
		length - 8U > plugin_upload.expected_size - offset) return -EINVAL;
	written = fs_write(&plugin_upload.file, &payload[8], length - 8U);
	if (written != (ssize_t)(length - 8U)) return -EIO;
	plugin_upload.received += (uint32_t)written;
	plugin_upload.last_activity_ms = k_uptime_get();
	return 0;
}

static int plugin_upload_commit(void)
{
	static uint8_t package[PLUGIN_UPLOAD_MAX_SIZE];
	struct pm_package_manifest manifest;
	char final_path[PLUGIN_PATH_MAX];
	ssize_t read;
	int ret;

	if (!plugin_upload.active || plugin_upload.received != plugin_upload.expected_size) {
		return -EINVAL;
	}
	ret = fs_close(&plugin_upload.file);
	plugin_upload.active = false;
	if (ret != 0) return ret;
	fs_file_t_init(&plugin_upload.file);
	ret = fs_open(&plugin_upload.file, "/DATA:/plugins/.upload.tmp", FS_O_READ);
	if (ret != 0) return ret;
	read = fs_read(&plugin_upload.file, package, plugin_upload.expected_size);
	(void)fs_close(&plugin_upload.file);
	if (read != (ssize_t)plugin_upload.expected_size ||
		pm_package_crc32(package, plugin_upload.expected_size) != plugin_upload.expected_crc ||
		pm_package_parse(package, plugin_upload.expected_size, &manifest) != PM_OK ||
		manifest.target_architecture != PM_PACKAGE_TARGET_NATIVE_SIM) {
		(void)fs_unlink("/DATA:/plugins/.upload.tmp");
		return -EPROTO;
	}
	snprintk(final_path, sizeof(final_path), "/DATA:/plugins/%s", plugin_upload.name);
	(void)fs_unlink(final_path);
	ret = fs_rename("/DATA:/plugins/.upload.tmp", final_path);
	return ret;
}

static void publish_changed_state(void)
{
	uint32_t sequence = ef800_virtual_display_sequence(active_panel->display);

	if (sequence == last_sent_sequence) {
		return;
	}
	send_current_state();
	last_sent_sequence = sequence;
	ef800_virtual_display_acknowledge(active_panel->display, sequence);
}

static void frontpanel_thread_main(void *arg1, void *arg2, void *arg3)
{
	size_t receive_length = 0U;
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (active_panel->running) {
		int accepted_fd = accept(listen_fd, NULL, NULL);
		if (accepted_fd < 0) {
			continue;
		}
		pthread_mutex_lock(&send_mutex);
		client_fd = accepted_fd;
		pthread_mutex_unlock(&send_mutex);
		LOG_INF("front panel client accepted");
		send_current_state();
		last_sent_sequence = ef800_virtual_display_sequence(active_panel->display);
		ef800_virtual_display_acknowledge(active_panel->display, last_sent_sequence);
		LOG_INF("front panel current state sent");
		send_log_history();
		drain_log_queue();
		struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
		(void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			sizeof(timeout));
		receive_length = 0U;
		while (active_panel->running) {
			ssize_t count = recv(client_fd, &receive_buffer[receive_length],
				sizeof(receive_buffer) - receive_length, 0);
			struct ef800_virtual_message message;
			if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				publish_changed_state();
				continue;
			}
			if (count <= 0) {
				break;
			}
			receive_length += (size_t)count;
			while (receive_length >= EF800_VIRTUAL_PROTOCOL_HEADER_SIZE) {
				uint16_t payload_length = (uint16_t)receive_buffer[6] |
					((uint16_t)receive_buffer[7] << 8);
				size_t frame_length =
					EF800_VIRTUAL_PROTOCOL_HEADER_SIZE + payload_length;
				if (payload_length > EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD ||
					frame_length > receive_length) {
					break;
				}
				if (ef800_virtual_protocol_decode(receive_buffer, frame_length,
					&message) != 0) {
					receive_length = 0U;
					break;
				}
				if (message.type == EF800_VIRTUAL_MSG_PROFILE_CONTROL) {
				int profile_result = ef800_virtual_profile_apply(message.payload,
					message.length);
					uint8_t status = profile_result == 0 ? 0U : 1U;
					(void)send_message(EF800_VIRTUAL_MSG_ACK, &status, 1U);
				} else if (message.type == EF800_VIRTUAL_MSG_COMPOSITION_CONTROL) {
					handle_composition_control(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_KEY_RELEASE_ALL ||
				message.type == EF800_VIRTUAL_MSG_KEY_DOWN ||
				message.type == EF800_VIRTUAL_MSG_KEY_UP) {
				if (active_panel->keyboard != NULL) {
					if (message.type == EF800_VIRTUAL_MSG_KEY_RELEASE_ALL) {
						ef800_virtual_keyboard_release_all(
							active_panel->keyboard);
					} else if (message.length > 0U) {
						if (message.type == EF800_VIRTUAL_MSG_KEY_DOWN) {
							(void)ef800_virtual_keyboard_press(
								active_panel->keyboard,
								(const char *)message.payload,
								message.length);
						} else {
							(void)ef800_virtual_keyboard_release(
								active_panel->keyboard,
								(const char *)message.payload,
								message.length);
						}
					}
				}
				(void)send_message(EF800_VIRTUAL_MSG_ACK, NULL, 0U);
				} else if (message.type == EF800_VIRTUAL_MSG_PLUGIN_UPLOAD_BEGIN) {
					int result = plugin_upload_begin(message.payload, message.length);
					(void)plugin_reply(result == 0 ? 0U : 1U, result, 0U);
				} else if (message.type == EF800_VIRTUAL_MSG_PLUGIN_UPLOAD_CHUNK) {
					int result = plugin_upload_chunk(message.payload, message.length);
					(void)plugin_reply(result == 0 ? 0U : 1U, result,
						plugin_upload.received);
				} else if (message.type == EF800_VIRTUAL_MSG_PLUGIN_UPLOAD_COMMIT) {
					int result = plugin_upload_commit();
					(void)plugin_reply(result == 0 ? 0U : 1U, result, 0U);
				} else if (message.type == EF800_VIRTUAL_MSG_SIMULATOR_RESTART) {
					(void)plugin_reply(0U, 0, 1U);
					k_msleep(100U);
					exit(42);
				} else if (message.type == EF800_VIRTUAL_MSG_PLUGIN_ASSIGN) {
					int result = -ENOTSUP;
					if (message.length == 9U && plugin_assign_callback != NULL) {
						result = plugin_assign_callback(NULL, message.payload[0],
							get_u32(&message.payload[1]), get_u32(&message.payload[5]));
					}
					(void)plugin_reply(result == 0 ? 0U : 1U, result, 0U);
				} else if (message.type == EF800_VIRTUAL_MSG_PLUGIN_EXECUTE) {
					int result = -ENOTSUP;
					if (message.length == 1U && plugin_execute_callback != NULL) {
						result = plugin_execute_callback(plugin_execute_context,
							message.payload[0]);
					}
					(void)plugin_reply(result == 0 ? 0U : 1U, result, 0U);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_LIST) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_DOWNLOAD) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_DOWNLOAD_NEXT) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_UPLOAD_BEGIN) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_UPLOAD_CHUNK) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_UPLOAD_COMMIT) {
					(void)mtp_queue_message(&message);
				} else if (message.type == EF800_VIRTUAL_MSG_MTP_CANCEL) {
					(void)mtp_queue_message(&message);
				}
				memmove(receive_buffer, &receive_buffer[frame_length],
					receive_length - frame_length);
				receive_length -= frame_length;
			}
			publish_changed_state();
			drain_log_queue();
		}
		pthread_mutex_lock(&send_mutex);
		close(client_fd);
		client_fd = -1;
		pthread_mutex_unlock(&send_mutex);
		if (active_panel->keyboard != NULL) {
			ef800_virtual_keyboard_release_all(active_panel->keyboard);
		}
		mtp_queue_disconnect_reset();
		mtp_download.active = false;
	}
	close(listen_fd);
	listen_fd = -1;
}

int ef800_virtual_frontpanel_start(struct ef800_virtual_frontpanel *panel,
					   struct ef800_virtual_display *display,
					   struct ef800_virtual_keyboard *keyboard,
					   uint16_t port)
{
	struct sockaddr_in address = {0};

	if (panel == NULL || display == NULL || port == 0U || active_panel != NULL) {
		return -EINVAL;
	}
	panel->display = display;
	panel->keyboard = keyboard;
	panel->port = port;
	panel->running = true;
	active_panel = panel;
	listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd < 0) {
		LOG_ERR("front panel socket failed: %d", errno);
		active_panel = NULL;
		panel->running = false;
		return -errno;
	}
	int reuse = 1;
	(void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);
	if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
		listen(listen_fd, 1) < 0) {
		int error = errno;
		LOG_ERR("front panel bind/listen failed: %d", error);
		close(listen_fd);
		listen_fd = -1;
		active_panel = NULL;
		panel->running = false;
		return -error;
	}
	if (pthread_create(&frontpanel_pthread, NULL, frontpanel_pthread_main,
		NULL) != 0) {
		LOG_ERR("front panel worker thread creation failed");
		close(listen_fd);
		listen_fd = -1;
		active_panel = NULL;
		panel->running = false;
		return -EAGAIN;
	}
	return 0;
}

void ef800_virtual_frontpanel_stop(struct ef800_virtual_frontpanel *panel)
{
	if (panel == NULL) {
		return;
	}
	panel->running = false;
	pthread_mutex_lock(&send_mutex);
	if (client_fd >= 0) {
		shutdown(client_fd, SHUT_RDWR);
	}
	pthread_mutex_unlock(&send_mutex);
	if (listen_fd >= 0) {
		shutdown(listen_fd, SHUT_RDWR);
	}
	active_panel = NULL;
}
