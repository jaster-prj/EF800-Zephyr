/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_virtual_serial_peers.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ef800_virtual_serial_peers, LOG_LEVEL_INF);

#define PEER_COUNT 2U
#define PEER_SCRIPT_SIZE 128U
#define PEER_PROFILE_NONE 0U
#define PEER_PROFILE_ELTA3 1U
#define PEER_PROFILE_FTS3XX 2U

struct virtual_peer {
	const struct device *uart;
	uint8_t response[PEER_SCRIPT_SIZE];
	size_t response_length;
	int32_t delay_ms;
	int32_t error;
 bool available;
 uint32_t transaction_count;
 uint8_t command[16];
 size_t command_length;
	uint8_t pending_command[16];
	size_t pending_command_length;
	uint8_t profile;
	struct k_work_delayable work;
};

static struct virtual_peer peers[PEER_COUNT] = {
	/* EF800 Port 1 is UART3; Port 2 is USART1. */
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart3)),
		.response = {'A', '\r', '\n'}, .response_length = 3U, .available = true },
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart1)),
		.response = {'1','2','3','4',' ','0','4','5','2','7','0','0',' ',
			'0','9','0','0','0','0','0','\r','\n'},
		.response_length = 22U, .available = true },
};

static uint32_t dummy_random(void)
{
	static uint32_t state = 0x6d2b79f5U;
	state = state * 1664525U + 1013904223U;
	return state;
}

static uint32_t dummy_random_range(uint32_t limit)
{
	return limit == 0U ? 0U : dummy_random() % limit;
}

static size_t generate_elta3_measurement(char *buffer, size_t capacity)
{
	uint32_t horizontal = dummy_random_range(3600001U);
	uint32_t vertical = dummy_random_range(900001U);
	uint32_t distance = dummy_random_range(100000U);
	int written;

	if ((dummy_random() & 1U) == 0U) {
		written = snprintk(buffer, capacity, "#33 %u.%04u; %u.%04u;\r\n",
			horizontal / 10000U, horizontal % 10000U,
			vertical / 10000U, vertical % 10000U);
	} else {
		written = snprintk(buffer, capacity,
			"#48 0 %u.%03u; %u.%04u; %u.%04u\r\n",
			distance / 1000U, distance % 1000U,
			horizontal / 10000U, horizontal % 10000U,
			vertical / 10000U, vertical % 10000U);
	}
	return written > 0 && (size_t)written < capacity ? (size_t)written : 0U;
}

static size_t generate_fennel_measurement(char *buffer, size_t capacity)
{
	int written = snprintk(buffer, capacity, "%07u %03u%02u%02u %03u%02u%02u\r\n",
		dummy_random_range(1000000U),
		dummy_random_range(90U), dummy_random_range(60U), dummy_random_range(60U),
		dummy_random_range(360U), dummy_random_range(60U), dummy_random_range(60U));
	return written > 0 && (size_t)written < capacity ? (size_t)written : 0U;
}

static void peer_work(struct k_work *work)
{
	struct virtual_peer *peer = CONTAINER_OF(work, struct virtual_peer, work.work);
	LOG_INF("virtual serial peer %u received command length %u first=0x%02x",
		(unsigned)(peer - peers + 1U), (unsigned)peer->pending_command_length,
		peer->pending_command_length == 0U ? 0U : peer->pending_command[0]);

	if (!peer->available || peer->error != 0 ||
		(peer->profile == PEER_PROFILE_NONE && peer->response_length == 0U)) {
		return;
	}
	if (peer->profile == PEER_PROFILE_ELTA3 && peer->pending_command_length >= 3U &&
		peer->pending_command[0] == 'S' && peer->pending_command[1] == '1' &&
		peer->pending_command[2] == '4') {
		static const uint8_t acknowledge[] = {'A', '\r', '\n'};
		LOG_INF("virtual serial peer %u response ACK", (unsigned)(peer - peers + 1U));
		(void)uart_emul_put_rx_data(peer->uart, acknowledge,
			sizeof(acknowledge));
	} else if (peer->profile == PEER_PROFILE_ELTA3 && peer->pending_command_length >= 1U &&
		peer->pending_command[0] == 'B') {
		char measurement[96];
		size_t length = generate_elta3_measurement(measurement, sizeof(measurement));
		LOG_INF("virtual serial peer %u response randomized Elta3 measurement",
			(unsigned)(peer - peers + 1U));
		(void)uart_emul_put_rx_data(peer->uart, (const uint8_t *)measurement, length);
	} else if (peer->profile == PEER_PROFILE_ELTA3 && peer->pending_command_length >= 1U &&
		peer->pending_command[0] == 'Q') {
		/* Elta3's Q command ends a readout and has no response. */
	} else if (peer->profile == PEER_PROFILE_FTS3XX &&
		peer->pending_command_length >= 1U && peer->pending_command[0] == 0x11U) {
		char measurement[64];
		size_t length = generate_fennel_measurement(measurement, sizeof(measurement));
		(void)uart_emul_put_rx_data(peer->uart, (const uint8_t *)measurement, length);
	} else {
		(void)uart_emul_put_rx_data(peer->uart, peer->response,
			peer->response_length);
	}
	peer->pending_command_length = 0U;
}

static void peer_tx_ready(const struct device *dev, size_t size, void *context)
{
	struct virtual_peer *peer = context;
	uint8_t bytes[PEER_SCRIPT_SIZE];
	uint32_t length;

	ARG_UNUSED(size);
	length = uart_emul_get_tx_data(dev, bytes, sizeof(bytes));
	for (uint32_t index = 0U; index < length; ++index) {
		if (peer->command_length < sizeof(peer->command)) {
			peer->command[peer->command_length++] = bytes[index];
		}
		if (bytes[index] == '\n') {
			memcpy(peer->pending_command, peer->command, peer->command_length);
			peer->pending_command_length = peer->command_length;
			peer->transaction_count++;
			if (peer->available) {
				k_work_reschedule(&peer->work, K_MSEC(MAX(peer->delay_ms, 0)));
			}
			peer->command_length = 0U;
		}
	}
}

int ef800_virtual_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available)
{
	struct virtual_peer *peer;

	if (port == 0U || port > PEER_COUNT || length > PEER_SCRIPT_SIZE ||
		(response == NULL && length != 0U)) {
		return -EINVAL;
	}
	peer = &peers[port - 1U];
	memcpy(peer->response, response, length);
	peer->response_length = length;
	peer->transaction_count = 0U;
	peer->command_length = 0U;
	peer->delay_ms = delay_ms;
	peer->error = error;
	peer->available = available;
	return 0;
}

int ef800_virtual_serial_peer_set_profile(uint8_t port, uint8_t profile)
{
	if (port == 0U || port > PEER_COUNT || profile > PEER_PROFILE_FTS3XX) {
		return -EINVAL;
	}
	peers[port - 1U].profile = profile;
	peers[port - 1U].command_length = 0U;
	peers[port - 1U].pending_command_length = 0U;
	return 0;
}

static int serial_peers_init(void)
{
	for (size_t index = 0U; index < ARRAY_SIZE(peers); ++index) {
		k_work_init_delayable(&peers[index].work, peer_work);
		if (device_is_ready(peers[index].uart)) {
			uart_emul_callback_tx_data_ready_set(peers[index].uart,
				peer_tx_ready, &peers[index]);
		}
	}
	return 0;
}

SYS_INIT(serial_peers_init, APPLICATION, 0);
