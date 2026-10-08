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
	sizeof(uint32_t) + EF800_VIRTUAL_DISPLAY_TEXT_ROWS *
	(EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS + 1U)];
static uint8_t receive_buffer[EF800_VIRTUAL_PROTOCOL_HEADER_SIZE +
	EF800_VIRTUAL_PROTOCOL_MAX_PAYLOAD];
static pthread_t frontpanel_pthread;
static uint32_t last_sent_sequence;
static ef800_virtual_plugin_assign_fn plugin_assign_callback;
static void *plugin_assign_context;
static ef800_virtual_plugin_execute_fn plugin_execute_callback;
static void *plugin_execute_context;
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

static struct ef800_virtual_log_store log_store;
static struct ef800_virtual_log_entry replay_entries[EF800_VIRTUAL_LOG_HISTORY_COUNT];

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
	size_t frame_length = ef800_virtual_protocol_encode(type, payload, length,
		send_frame, sizeof(send_frame));

	if (client_fd < 0 || frame_length == 0U) {
		return -ENOTCONN;
	}
	ssize_t sent;
	sent = send(client_fd, send_frame, frame_length, MSG_NOSIGNAL);
	if (sent != (ssize_t)frame_length) {
		return -EIO;
	}
	return 0;
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
	if (active_panel == NULL || ef800_virtual_display_snapshot(
		active_panel->display, &state_payload[sizeof(sequence)],
		EF800_VIRTUAL_DISPLAY_FRAME_SIZE, &sequence) != 0) {
		return;
	}
	memcpy(state_payload, &sequence, sizeof(sequence));
	memcpy(&state_payload[sizeof(sequence) + EF800_VIRTUAL_DISPLAY_FRAME_SIZE],
		active_panel->display->text, sizeof(active_panel->display->text));
	(void)send_message(EF800_VIRTUAL_MSG_LCD_FRAME, state_payload,
		(uint16_t)sizeof(state_payload));
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
	(void)fs_mkdir("/EF800:/plugins");
	if (plugin_upload.active) {
		(void)fs_close(&plugin_upload.file);
		(void)fs_unlink("/EF800:/plugins/.upload.tmp");
	}
	plugin_upload.id = PLUGIN_UPLOAD_ID;
	plugin_upload.expected_size = get_u32(&payload[4]);
	plugin_upload.expected_crc = get_u32(&payload[8]);
	plugin_upload.received = 0U;
	plugin_upload.last_activity_ms = k_uptime_get();
	memcpy(plugin_upload.name, &payload[13], name_length);
	plugin_upload.name[name_length] = '\0';
	fs_file_t_init(&plugin_upload.file);
	ret = fs_open(&plugin_upload.file, "/EF800:/plugins/.upload.tmp",
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
		(void)fs_unlink("/EF800:/plugins/.upload.tmp");
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
	ret = fs_open(&plugin_upload.file, "/EF800:/plugins/.upload.tmp", FS_O_READ);
	if (ret != 0) return ret;
	read = fs_read(&plugin_upload.file, package, plugin_upload.expected_size);
	(void)fs_close(&plugin_upload.file);
	if (read != (ssize_t)plugin_upload.expected_size ||
		pm_package_crc32(package, plugin_upload.expected_size) != plugin_upload.expected_crc ||
		pm_package_parse(package, plugin_upload.expected_size, &manifest) != PM_OK ||
		manifest.target_architecture != PM_PACKAGE_TARGET_NATIVE_SIM) {
		(void)fs_unlink("/EF800:/plugins/.upload.tmp");
		return -EPROTO;
	}
	snprintk(final_path, sizeof(final_path), "/EF800:/plugins/%s", plugin_upload.name);
	(void)fs_unlink(final_path);
	ret = fs_rename("/EF800:/plugins/.upload.tmp", final_path);
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
		client_fd = accept(listen_fd, NULL, NULL);
		if (client_fd < 0) {
			continue;
		}
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
				}
				memmove(receive_buffer, &receive_buffer[frame_length],
					receive_length - frame_length);
				receive_length -= frame_length;
			}
			publish_changed_state();
			drain_log_queue();
		}
		close(client_fd);
		if (active_panel->keyboard != NULL) {
			ef800_virtual_keyboard_release_all(active_panel->keyboard);
		}
		client_fd = -1;
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
	if (client_fd >= 0) {
		shutdown(client_fd, SHUT_RDWR);
	}
	if (listen_fd >= 0) {
		shutdown(listen_fd, SHUT_RDWR);
	}
	active_panel = NULL;
}
