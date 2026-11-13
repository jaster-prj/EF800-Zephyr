/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_virtual_usb_peer.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/usbh.h>

#include "usbh_ch9.h"
#include "usbh_device.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ef800_virtual_usb_peer, LOG_LEVEL_INF);

#define EF800_VIRTUAL_USB_UDC_NODE DT_NODELABEL(usbotg_fs)
#define EF800_VIRTUAL_USB_UHC_NODE DT_NODELABEL(uhc_vrt0)
#define EF800_VIRTUAL_USB_TRANSFER_CHUNK 512U
#define EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS 5000U
#define EF800_VIRTUAL_USB_ENUM_TIMEOUT_MS 5000U
#define EF800_VIRTUAL_USB_MTP_HEADER_SIZE 12U
#define EF800_VIRTUAL_USB_MTP_MAX_INFO_SIZE 1024U
#define EF800_VIRTUAL_USB_MTP_MAX_CONTAINER_SIZE \
	(EF800_VIRTUAL_USB_MAX_OBJECT_SIZE + EF800_VIRTUAL_USB_MTP_HEADER_SIZE)
#define EF800_VIRTUAL_USB_MTP_TYPE_COMMAND 1U
#define EF800_VIRTUAL_USB_MTP_TYPE_DATA 2U
#define EF800_VIRTUAL_USB_MTP_TYPE_RESPONSE 3U
#define EF800_VIRTUAL_USB_MTP_OP_OPEN_SESSION 0x1002U
#define EF800_VIRTUAL_USB_MTP_OP_GET_STORAGE_IDS 0x1004U
#define EF800_VIRTUAL_USB_MTP_OP_GET_STORAGE_INFO 0x1005U
#define EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT_HANDLES 0x1007U
#define EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT_INFO 0x1008U
#define EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT 0x1009U
#define EF800_VIRTUAL_USB_MTP_OP_SEND_OBJECT_INFO 0x100CU
#define EF800_VIRTUAL_USB_MTP_OP_SEND_OBJECT 0x100DU
#define EF800_VIRTUAL_USB_MTP_RESP_OK 0x2001U
#define EF800_VIRTUAL_USB_MTP_FORMAT_UNDEFINED 0x3000U
#define EF800_VIRTUAL_USB_MTP_FORMAT_ASSOCIATION 0x3001U
#define EF800_VIRTUAL_USB_MTP_ALL_ROOT_OBJECTS 0xFFFFFFFFU
#define EF800_VIRTUAL_USB_MTP_ROOT_PARENT 0xFFFFFFFFU

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_VIRTUAL_USB_UDC_NODE, okay),
	"Native-sim virtual USB peer requires the device-side virtual UDC");
BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_VIRTUAL_USB_UHC_NODE, okay),
	"Native-sim virtual USB peer requires the host-side virtual UHC");

USBH_CONTROLLER_DEFINE(ef800_virtual_uhs,
	DEVICE_DT_GET(EF800_VIRTUAL_USB_UHC_NODE));

struct ef800_virtual_usb_endpoints {
	uint8_t bulk_in;
	uint8_t bulk_out;
	uint16_t bulk_in_mps;
	uint16_t bulk_out_mps;
};

struct ef800_virtual_usb_completion {
	int status;
	size_t length;
};

struct ef800_virtual_usb_mtp_response {
	uint16_t code;
	uint32_t parameters[5];
	size_t parameter_count;
};

struct ef800_virtual_usb_object_info {
	uint32_t handle;
	uint32_t size;
	bool directory;
	char name[EF800_VIRTUAL_USB_MAX_NAME + 1U];
};

K_MUTEX_DEFINE(peer_mutex);
K_SEM_DEFINE(peer_transfer_sem, 0, 1);

static bool peer_host_initialized;
static bool peer_transfer_faulted;
static struct ef800_virtual_usb_completion peer_completion;
static struct usb_device *peer_mtp_device;
static uint32_t peer_transaction_id;
static uint32_t peer_storage_ids[EF800_VIRTUAL_MTP_FWU];
static size_t peer_storage_count;
static bool peer_mtp_session_open;

static void put_u16(uint8_t *buffer, uint16_t value)
{
	sys_put_le16(value, buffer);
}

static void put_u32(uint8_t *buffer, uint32_t value)
{
	sys_put_le32(value, buffer);
}

static uint16_t get_u16(const uint8_t *buffer)
{
	return sys_get_le16(buffer);
}

static uint32_t get_u32(const uint8_t *buffer)
{
	return sys_get_le32(buffer);
}

static size_t bounded_strlen(const char *text, size_t maximum)
{
	size_t length = 0U;

	while (length < maximum && text[length] != '\0') {
		length++;
	}
	return length;
}

static int virtual_usb_transfer_complete(struct usb_device *udev,
					struct uhc_transfer *xfer)
{
	ARG_UNUSED(udev);
	peer_completion.status = xfer->err;
	peer_completion.length = xfer->buf == NULL ? 0U : xfer->buf->len;
	k_sem_give(&peer_transfer_sem);
	return 0;
}

static int bulk_transfer(struct usb_device *udev, uint8_t endpoint,
			 const uint8_t *out, size_t out_length, uint8_t *in,
			 size_t in_capacity, size_t *in_length, uint32_t timeout_ms)
{
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	size_t requested = USB_EP_DIR_IS_IN(endpoint) ? in_capacity : out_length;
	int ret;

	if (peer_transfer_faulted || udev == NULL || requested == 0U ||
		requested > EF800_VIRTUAL_USB_TRANSFER_CHUNK ||
		(USB_EP_DIR_IS_IN(endpoint) && in == NULL) ||
		(!USB_EP_DIR_IS_IN(endpoint) && out == NULL)) {
		return peer_transfer_faulted ? -EIO : -EINVAL;
	}
	xfer = usbh_xfer_alloc(udev, endpoint, virtual_usb_transfer_complete, NULL);
	if (xfer == NULL) {
		return -ENOMEM;
	}
	buf = usbh_xfer_buf_alloc(udev, requested);
	if (buf == NULL) {
		(void)usbh_xfer_free(udev, xfer);
		return -ENOMEM;
	}
	ret = usbh_xfer_buf_add(udev, xfer, buf);
	if (ret != 0) {
		usbh_xfer_buf_free(udev, buf);
		(void)usbh_xfer_free(udev, xfer);
		return ret;
	}
	if (!USB_EP_DIR_IS_IN(endpoint)) {
		net_buf_add_mem(buf, out, out_length);
	}

	k_sem_reset(&peer_transfer_sem);
	peer_completion.status = -EINPROGRESS;
	peer_completion.length = 0U;
	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret != 0) {
		usbh_xfer_buf_free(udev, buf);
		(void)usbh_xfer_free(udev, xfer);
		return ret;
	}
	ret = k_sem_take(&peer_transfer_sem, K_MSEC(timeout_ms));
	if (ret != 0) {
		LOG_ERR("USB bulk transfer timeout ep=0x%02x requested=%zu timeout=%u ms",
			endpoint, requested, timeout_ms);
		ret = usbh_xfer_dequeue(udev, xfer);
		if (ret == 0) {
			ret = k_sem_take(&peer_transfer_sem, K_MSEC(100U));
		}
		if (ret != 0) {
			/* The transfer may still own its buffers; permanently stop issuing
			 * requests rather than freeing memory that the controller may use. */
			peer_transfer_faulted = true;
			return -ETIMEDOUT;
		}
		ret = -ETIMEDOUT;
	} else {
		ret = peer_completion.status;
	}
	if (ret != 0 && ret != -ETIMEDOUT) {
		LOG_ERR("USB bulk transfer failed ep=0x%02x status=%d", endpoint, ret);
	}
	if (ret == 0 && USB_EP_DIR_IS_IN(endpoint)) {
		*in_length = peer_completion.length;
		if (*in_length > in_capacity) {
			ret = -EOVERFLOW;
		} else {
			memcpy(in, buf->data, *in_length);
		}
	}
	usbh_xfer_buf_free(udev, buf);
	{
		int free_ret = usbh_xfer_free(udev, xfer);
		if (ret == 0) {
			ret = free_ret;
		}
	}
	return ret;
}

static int bulk_write(struct usb_device *udev, uint8_t endpoint,
		      const uint8_t *data, size_t length)
{
	size_t offset = 0U;

	if (data == NULL && length != 0U) {
		return -EINVAL;
	}
	while (offset < length) {
		size_t chunk = MIN(length - offset,
				   (size_t)EF800_VIRTUAL_USB_TRANSFER_CHUNK);
		size_t ignored = 0U;
		int ret = bulk_transfer(udev, endpoint, data + offset, chunk,
					NULL, 0U, &ignored,
					EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS);

		if (ret != 0) {
			return ret;
		}
		offset += chunk;
	}
	return 0;
}

static int bulk_read(struct usb_device *udev, uint8_t endpoint,
		     uint8_t *data, size_t capacity, size_t *length,
		     uint32_t timeout_ms)
{
	if (length == NULL) {
		return -EINVAL;
	}
	*length = 0U;
	return bulk_transfer(udev, endpoint, NULL, 0U, data, capacity, length,
			     timeout_ms);
}

static int find_bulk_endpoints(const struct usb_device *udev, uint8_t class_code,
			       struct ef800_virtual_usb_endpoints *endpoints)
{
	const uint8_t *descriptor = udev->cfg_desc;
	const struct usb_cfg_descriptor *config = udev->cfg_desc;
	bool matching_interface = false;
	size_t total_length;

	if (descriptor == NULL || endpoints == NULL) {
		return -EINVAL;
	}
	memset(endpoints, 0, sizeof(*endpoints));
	total_length = sys_le16_to_cpu(config->wTotalLength);
	for (size_t offset = 0U; offset + 2U <= total_length;) {
		uint8_t length = descriptor[offset];
		uint8_t type = descriptor[offset + 1U];

		if (length < 2U || offset + length > total_length) {
			return -EBADMSG;
		}
		if (type == USB_DESC_INTERFACE) {
			matching_interface = length >= 9U &&
				descriptor[offset + 5U] == class_code;
		} else if (matching_interface && type == USB_DESC_ENDPOINT &&
			length >= 7U &&
			(descriptor[offset + 3U] & USB_EP_TRANSFER_TYPE_MASK) ==
			USB_EP_TYPE_BULK) {
			uint8_t address = descriptor[offset + 2U];
			uint16_t mps = sys_get_le16(&descriptor[offset + 4U]) & 0x07ffU;

			if (USB_EP_DIR_IS_IN(address)) {
				endpoints->bulk_in = address;
				endpoints->bulk_in_mps = mps;
			} else {
				endpoints->bulk_out = address;
				endpoints->bulk_out_mps = mps;
			}
		}
		offset += length;
	}
	return endpoints->bulk_in != 0U && endpoints->bulk_out != 0U ? 0 : -ENODEV;
}

static int get_enumerated_device(uint32_t timeout_ms, struct usb_device **udev)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	if (udev == NULL) {
		return -EINVAL;
	}
	*udev = NULL;
	while (k_uptime_get() < deadline) {
		struct usb_device *candidate = usbh_device_get_any(&ef800_virtual_uhs);

		if (candidate != NULL && candidate->state == USB_STATE_CONFIGURED) {
			*udev = candidate;
			return 0;
		}
		k_msleep(5U);
	}
	return -ETIMEDOUT;
}

int ef800_virtual_usb_peer_init(void)
{
	int ret;

	if (peer_host_initialized) {
		return 0;
	}
	if (!device_is_ready(DEVICE_DT_GET(EF800_VIRTUAL_USB_UHC_NODE))) {
		return -ENODEV;
	}
	ret = usbh_init(&ef800_virtual_uhs);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	ret = usbh_enable(&ef800_virtual_uhs);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	ret = uhc_bus_reset(ef800_virtual_uhs.dev);
	if (ret != 0) {
		return ret;
	}
	ret = uhc_bus_resume(ef800_virtual_uhs.dev);
	if (ret != 0) {
		return ret;
	}
	ret = uhc_sof_enable(ef800_virtual_uhs.dev);
	if (ret != 0) {
		return ret;
	}
	peer_host_initialized = true;
	/* cppcheck-suppress knownConditionTrueFalse -- Kconfig controls LOG_* expansion */
	LOG_INF("Native-sim USB host peer enabled");
	return 0;
}

int ef800_virtual_usb_peer_wait_for_device(uint32_t timeout_ms)
{
	struct usb_device *udev;
	int ret = ef800_virtual_usb_peer_init();

	if (ret != 0) {
		return ret;
	}
	return get_enumerated_device(timeout_ms, &udev);
}

void ef800_virtual_usb_peer_invalidate_session(void)
{
	if (k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS)) != 0) {
		return;
	}
	peer_mtp_device = NULL;
	peer_storage_count = 0U;
	peer_mtp_session_open = false;
	peer_transaction_id = 0U;
	k_mutex_unlock(&peer_mutex);
}

static int get_class_device(uint8_t interface_class, struct usb_device **udev,
			    struct ef800_virtual_usb_endpoints *endpoints)
{
	int ret = get_enumerated_device(EF800_VIRTUAL_USB_ENUM_TIMEOUT_MS, udev);

	if (ret != 0) {
		return ret;
	}
	return find_bulk_endpoints(*udev, interface_class, endpoints);
}

static int lock_device(struct usb_device *udev)
{
	return k_mutex_lock(&udev->mutex,
			    K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
}

static void put_mtp_header(uint8_t *buffer, uint32_t length, uint16_t type,
			   uint16_t code, uint32_t transaction_id)
{
	put_u32(&buffer[0], length);
	put_u16(&buffer[4], type);
	put_u16(&buffer[6], code);
	put_u32(&buffer[8], transaction_id);
}

static size_t make_mtp_command(uint8_t *buffer, size_t capacity,
			       uint16_t operation, uint32_t transaction_id,
			       const uint32_t *parameters, size_t parameter_count)
{
	size_t length = EF800_VIRTUAL_USB_MTP_HEADER_SIZE +
		parameter_count * sizeof(uint32_t);

	if (buffer == NULL || parameter_count > 5U || length > capacity ||
		(parameter_count != 0U && parameters == NULL)) {
		return 0U;
	}
	put_mtp_header(buffer, (uint32_t)length,
		EF800_VIRTUAL_USB_MTP_TYPE_COMMAND, operation, transaction_id);
	for (size_t index = 0U; index < parameter_count; ++index) {
		put_u32(&buffer[EF800_VIRTUAL_USB_MTP_HEADER_SIZE +
			index * sizeof(uint32_t)], parameters[index]);
	}
	return length;
}

static int mtp_read_container(struct usb_device *udev, uint8_t endpoint,
			      uint8_t expected_type, uint16_t expected_code,
			      uint32_t expected_transaction, uint8_t *payload,
			      size_t payload_capacity, size_t *payload_length,
			      uint32_t *response_parameters,
			      size_t response_parameter_capacity)
{
	uint8_t chunk[EF800_VIRTUAL_USB_TRANSFER_CHUNK];
	size_t actual = 0U;
	size_t container_received;
	size_t remaining;
	size_t copied = 0U;
	uint32_t container_length;
	uint8_t header[EF800_VIRTUAL_USB_MTP_HEADER_SIZE];
	int ret;

	if (payload_length != NULL) {
		*payload_length = 0U;
	}
	ret = bulk_read(udev, endpoint, chunk, sizeof(chunk), &actual,
			EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS);
	if (ret != 0) {
		return ret;
	}
	if (actual < EF800_VIRTUAL_USB_MTP_HEADER_SIZE) {
		LOG_ERR("Short MTP container header: %zu bytes", actual);
		return -EBADMSG;
	}
	memcpy(header, chunk, sizeof(header));
	container_length = get_u32(&header[0]);
	if (container_length < EF800_VIRTUAL_USB_MTP_HEADER_SIZE ||
		container_length > EF800_VIRTUAL_USB_MTP_MAX_CONTAINER_SIZE ||
		get_u16(&header[4]) != expected_type ||
		get_u16(&header[6]) != expected_code ||
		get_u32(&header[8]) != expected_transaction ||
		actual > container_length) {
		LOG_ERR("MTP container mismatch: len=%u actual=%zu type=%u/%u "
			"code=0x%04x/0x%04x tid=%u/%u", container_length, actual,
			get_u16(&header[4]), expected_type, get_u16(&header[6]),
			expected_code, get_u32(&header[8]), expected_transaction);
		return -EBADMSG;
	}
	container_received = actual;
	remaining = container_length - actual;
	if (actual > EF800_VIRTUAL_USB_MTP_HEADER_SIZE) {
		size_t first_length = actual - EF800_VIRTUAL_USB_MTP_HEADER_SIZE;
		size_t copy_length = MIN(first_length, payload_capacity);

		if (copy_length != 0U && payload != NULL) {
			memcpy(payload, &chunk[EF800_VIRTUAL_USB_MTP_HEADER_SIZE],
			       copy_length);
		}
		copied = copy_length;
	}
	while (remaining != 0U) {
		size_t requested = MIN(remaining, sizeof(chunk));
		size_t copy_length;

		actual = 0U;
		ret = bulk_read(udev, endpoint, chunk, requested, &actual,
				EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS);
		if (ret != 0) {
			return ret;
		}
		if (actual == 0U || actual > remaining) {
			return -EIO;
		}
		copy_length = MIN(actual, payload_capacity - MIN(copied, payload_capacity));
		if (copy_length != 0U && payload != NULL) {
			memcpy(payload + copied, chunk, copy_length);
		}
		copied += copy_length;
		container_received += actual;
		remaining -= actual;
	}
	if (payload_length != NULL) {
		*payload_length = container_length - EF800_VIRTUAL_USB_MTP_HEADER_SIZE;
	}
	if (expected_type == EF800_VIRTUAL_USB_MTP_TYPE_RESPONSE &&
		container_length > EF800_VIRTUAL_USB_MTP_HEADER_SIZE) {
		size_t parameter_count = (container_length -
			EF800_VIRTUAL_USB_MTP_HEADER_SIZE) / sizeof(uint32_t);

		if ((container_length - EF800_VIRTUAL_USB_MTP_HEADER_SIZE) %
			sizeof(uint32_t) != 0U ||
			parameter_count > response_parameter_capacity ||
			(response_parameters == NULL && parameter_count != 0U)) {
			return -EBADMSG;
		}
		for (size_t index = 0U; index < parameter_count; ++index) {
			response_parameters[index] = get_u32(&payload[index * sizeof(uint32_t)]);
		}
	}
	if (container_received != container_length) {
		return -EBADMSG;
	}
	if (payload_length != NULL && *payload_length > payload_capacity) {
		return -ENOSPC;
	}
	return 0;
}

static int mtp_read_response_code(struct usb_device *udev,
				  const struct ef800_virtual_usb_endpoints *endpoints,
				  uint16_t operation, uint32_t transaction_id,
				  struct ef800_virtual_usb_mtp_response *response)
{
	uint8_t header[EF800_VIRTUAL_USB_MTP_HEADER_SIZE + 5U * sizeof(uint32_t)];
	size_t actual = 0U;
	size_t container_length;
	size_t payload_length;
	size_t parameter_count;
	int ret;

	ARG_UNUSED(operation);
	memset(response, 0, sizeof(*response));
	ret = bulk_read(udev, endpoints->bulk_in, header, sizeof(header), &actual,
			EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS);
	if (ret != 0) {
		return ret;
	}
	if (actual < EF800_VIRTUAL_USB_MTP_HEADER_SIZE ||
		get_u16(&header[4]) != EF800_VIRTUAL_USB_MTP_TYPE_RESPONSE ||
		get_u32(&header[8]) != transaction_id) {
		LOG_ERR("MTP response header mismatch: actual=%zu type=%u tid=%u "
			"expected tid=%u", actual,
			actual >= 6U ? get_u16(&header[4]) : 0U,
			actual >= EF800_VIRTUAL_USB_MTP_HEADER_SIZE ? get_u32(&header[8]) : 0U,
			transaction_id);
		return -EBADMSG;
	}
	container_length = get_u32(&header[0]);
	if (container_length < EF800_VIRTUAL_USB_MTP_HEADER_SIZE) {
		return -EBADMSG;
	}
	payload_length = container_length - EF800_VIRTUAL_USB_MTP_HEADER_SIZE;
	if (container_length > actual ||
		payload_length % sizeof(uint32_t) != 0U) {
		LOG_ERR("Malformed MTP response length=%zu actual=%zu", container_length,
			actual);
		return -EBADMSG;
	}
	parameter_count = payload_length / sizeof(uint32_t);
	if (parameter_count > ARRAY_SIZE(response->parameters)) {
		return -E2BIG;
	}
	response->code = get_u16(&header[6]);
	response->parameter_count = parameter_count;
	for (size_t index = 0U; index < parameter_count; ++index) {
		response->parameters[index] = get_u32(&header[
			EF800_VIRTUAL_USB_MTP_HEADER_SIZE + index * sizeof(uint32_t)]);
	}
	return 0;
}

static uint32_t mtp_next_transaction(void)
{
	peer_transaction_id++;
	if (peer_transaction_id == 0U) {
		peer_transaction_id = 1U;
	}
	return peer_transaction_id;
}

static int mtp_send_command(struct usb_device *udev,
			    const struct ef800_virtual_usb_endpoints *endpoints,
			    uint16_t operation, uint32_t transaction_id,
			    const uint32_t *parameters, size_t parameter_count)
{
	uint8_t command[EF800_VIRTUAL_USB_MTP_HEADER_SIZE + 5U * sizeof(uint32_t)];
	size_t length = make_mtp_command(command, sizeof(command), operation,
		transaction_id, parameters, parameter_count);

	if (length == 0U) {
		return -EINVAL;
	}
	return bulk_write(udev, endpoints->bulk_out, command, length);
}

static int mtp_command_simple(struct usb_device *udev,
			      const struct ef800_virtual_usb_endpoints *endpoints,
			      uint16_t operation, const uint32_t *parameters,
			      size_t parameter_count,
			      struct ef800_virtual_usb_mtp_response *response)
{
	uint32_t transaction_id = mtp_next_transaction();
	int ret = mtp_send_command(udev, endpoints, operation, transaction_id,
		parameters, parameter_count);

	if (ret == 0) {
		ret = mtp_read_response_code(udev, endpoints, operation,
			transaction_id, response);
	}
	if (ret == 0 && response->code != EF800_VIRTUAL_USB_MTP_RESP_OK) {
		ret = -EIO;
	}
	return ret;
}

static int mtp_command_data_in(struct usb_device *udev,
			       const struct ef800_virtual_usb_endpoints *endpoints,
			       uint16_t operation, const uint32_t *parameters,
			       size_t parameter_count, uint8_t *data,
			       size_t capacity, size_t *length)
{
	struct ef800_virtual_usb_mtp_response response;
	uint32_t transaction_id = mtp_next_transaction();
	int data_ret;
	int ret;

	ret = mtp_send_command(udev, endpoints, operation, transaction_id,
		parameters, parameter_count);
	if (ret != 0) {
		return ret;
	}
	data_ret = mtp_read_container(udev, endpoints->bulk_in,
		EF800_VIRTUAL_USB_MTP_TYPE_DATA, operation, transaction_id,
		data, capacity, length, NULL, 0U);
	if (data_ret != 0 && data_ret != -ENOSPC) {
		return data_ret;
	}
	ret = mtp_read_response_code(udev, endpoints, operation,
		transaction_id, &response);
	if (ret != 0) {
		return ret;
	}
	if (response.code != EF800_VIRTUAL_USB_MTP_RESP_OK) {
		return -EIO;
	}
	return data_ret;
}

static int mtp_command_data_out(struct usb_device *udev,
				const struct ef800_virtual_usb_endpoints *endpoints,
				uint16_t operation, const uint32_t *parameters,
				size_t parameter_count, const uint8_t *data,
				size_t length,
				struct ef800_virtual_usb_mtp_response *response)
{
	uint8_t chunk[EF800_VIRTUAL_USB_TRANSFER_CHUNK];
	uint32_t transaction_id = mtp_next_transaction();
	size_t total = EF800_VIRTUAL_USB_MTP_HEADER_SIZE + length;
	size_t offset = 0U;
	int ret;

	if (length > EF800_VIRTUAL_USB_MAX_OBJECT_SIZE ||
		(total > EF800_VIRTUAL_USB_MTP_MAX_CONTAINER_SIZE) ||
		(length != 0U && data == NULL)) {
		return -E2BIG;
	}
	ret = mtp_send_command(udev, endpoints, operation, transaction_id,
		parameters, parameter_count);
	if (ret != 0) {
		return ret;
	}
	while (offset < total) {
		size_t chunk_length = MIN(total - offset, sizeof(chunk));
		size_t payload_offset = offset;

		if (offset == 0U) {
			put_mtp_header(chunk, (uint32_t)total,
				EF800_VIRTUAL_USB_MTP_TYPE_DATA, operation,
				transaction_id);
			if (chunk_length > EF800_VIRTUAL_USB_MTP_HEADER_SIZE) {
				size_t copy_length = chunk_length -
					EF800_VIRTUAL_USB_MTP_HEADER_SIZE;
				memcpy(&chunk[EF800_VIRTUAL_USB_MTP_HEADER_SIZE], data,
					copy_length);
			}
		} else {
			memcpy(chunk, data + payload_offset -
				EF800_VIRTUAL_USB_MTP_HEADER_SIZE, chunk_length);
		}
		ret = bulk_write(udev, endpoints->bulk_out, chunk, chunk_length);
		if (ret != 0) {
			return ret;
		}
		offset += chunk_length;
	}
	ret = mtp_read_response_code(udev, endpoints, operation,
		transaction_id, response);
	if (ret == 0 && response->code != EF800_VIRTUAL_USB_MTP_RESP_OK) {
		ret = -EIO;
	}
	return ret;
}

static int mtp_open_session(struct usb_device *udev,
			    const struct ef800_virtual_usb_endpoints *endpoints)
{
	struct ef800_virtual_usb_mtp_response response;
	const uint32_t session_id = 1U;
	int ret;

	peer_transaction_id = 0U;
	ret = mtp_command_simple(udev, endpoints,
		EF800_VIRTUAL_USB_MTP_OP_OPEN_SESSION, &session_id, 1U, &response);
	if (ret != 0) {
		return ret;
	}
	peer_mtp_session_open = true;
	return 0;
}

static int mtp_get_storage_ids(struct usb_device *udev,
			       const struct ef800_virtual_usb_endpoints *endpoints)
{
	uint8_t payload[4U + EF800_VIRTUAL_MTP_FWU * sizeof(uint32_t)];
	size_t length = 0U;
	int ret;

	ret = mtp_command_data_in(udev, endpoints,
		EF800_VIRTUAL_USB_MTP_OP_GET_STORAGE_IDS, NULL, 0U,
		payload, sizeof(payload), &length);
	if (ret != 0) {
		return ret;
	}
	if (length < sizeof(uint32_t)) {
		return -EBADMSG;
	}
	peer_storage_count = get_u32(payload);
	if (peer_storage_count != EF800_VIRTUAL_MTP_FWU ||
		length != sizeof(uint32_t) + peer_storage_count * sizeof(uint32_t)) {
		return -ENODEV;
	}
	for (size_t index = 0U; index < peer_storage_count; ++index) {
		peer_storage_ids[index] = get_u32(&payload[sizeof(uint32_t) +
			index * sizeof(uint32_t)]);
	}
	return 0;
}

static int mtp_ensure_session(struct usb_device *udev,
			      const struct ef800_virtual_usb_endpoints *endpoints)
{
	if (peer_mtp_device != udev) {
		peer_mtp_device = udev;
		peer_mtp_session_open = false;
		peer_storage_count = 0U;
		peer_transaction_id = 0U;
	}
	if (!peer_mtp_session_open) {
		int ret = mtp_open_session(udev, endpoints);

		if (ret != 0) {
			return ret;
		}
		ret = mtp_get_storage_ids(udev, endpoints);
		if (ret != 0) {
			peer_mtp_session_open = false;
			return ret;
		}
	}
	return 0;
}

static int utf8_decode_one(const uint8_t *input, size_t length,
			   uint32_t *codepoint, size_t *consumed)
{
	uint8_t first;

	if (input == NULL || length == 0U || codepoint == NULL || consumed == NULL) {
		return -EINVAL;
	}
	first = input[0];
	if (first < 0x80U) {
		*codepoint = first;
		*consumed = 1U;
		return 0;
	}
	if ((first & 0xE0U) == 0xC0U && length >= 2U &&
		(input[1] & 0xC0U) == 0x80U) {
		*codepoint = ((uint32_t)(first & 0x1FU) << 6) | (input[1] & 0x3FU);
		*consumed = 2U;
		return *codepoint >= 0x80U ? 0 : -EINVAL;
	}
	if ((first & 0xF0U) == 0xE0U && length >= 3U &&
		(input[1] & 0xC0U) == 0x80U && (input[2] & 0xC0U) == 0x80U) {
		*codepoint = ((uint32_t)(first & 0x0FU) << 12) |
			((uint32_t)(input[1] & 0x3FU) << 6) | (input[2] & 0x3FU);
		*consumed = 3U;
		return *codepoint >= 0x800U &&
			!(*codepoint >= 0xD800U && *codepoint <= 0xDFFFU) ? 0 : -EINVAL;
	}
	if ((first & 0xF8U) == 0xF0U && length >= 4U &&
		(input[1] & 0xC0U) == 0x80U && (input[2] & 0xC0U) == 0x80U &&
		(input[3] & 0xC0U) == 0x80U) {
		*codepoint = ((uint32_t)(first & 0x07U) << 18) |
			((uint32_t)(input[1] & 0x3FU) << 12) |
			((uint32_t)(input[2] & 0x3FU) << 6) | (input[3] & 0x3FU);
		*consumed = 4U;
		return *codepoint >= 0x10000U && *codepoint <= 0x10FFFFU ? 0 : -EINVAL;
	}
	return -EINVAL;
}

static int utf8_to_mtp_string(const char *input, uint8_t *output,
			      size_t output_capacity, size_t *output_length)
{
	uint16_t units[EF800_VIRTUAL_USB_MAX_NAME + 1U];
	size_t unit_count = 0U;
	size_t input_length;
	size_t offset = 0U;

	if (input == NULL || output == NULL || output_length == NULL) {
		return -EINVAL;
	}
	input_length = bounded_strlen(input, EF800_VIRTUAL_USB_MAX_NAME + 1U);
	if (input_length == 0U || input_length > EF800_VIRTUAL_USB_MAX_NAME) {
		return -ENAMETOOLONG;
	}
	while (offset < input_length) {
		uint32_t codepoint;
		size_t consumed;
		int ret = utf8_decode_one((const uint8_t *)&input[offset],
			input_length - offset, &codepoint, &consumed);

		if (ret != 0 || codepoint == '/' || codepoint == '\\' || codepoint < 0x20U) {
			return -EINVAL;
		}
		if (codepoint < 0x10000U) {
			if (unit_count >= EF800_VIRTUAL_USB_MAX_NAME) {
				return -ENAMETOOLONG;
			}
			units[unit_count++] = (uint16_t)codepoint;
		} else {
			uint32_t value = codepoint - 0x10000U;

			if (unit_count + 2U > EF800_VIRTUAL_USB_MAX_NAME) {
				return -ENAMETOOLONG;
			}
			units[unit_count++] = (uint16_t)(0xD800U + (value >> 10));
			units[unit_count++] = (uint16_t)(0xDC00U + (value & 0x3FFU));
		}
		offset += consumed;
	}
	units[unit_count++] = 0U;
	if (unit_count > UINT8_MAX || output_capacity < 1U + unit_count * 2U) {
		return -ENOSPC;
	}
	output[0] = (uint8_t)unit_count;
	for (size_t index = 0U; index < unit_count; ++index) {
		put_u16(&output[1U + index * 2U], units[index]);
	}
	*output_length = 1U + unit_count * 2U;
	return 0;
}

static int mtp_string_to_utf8(const uint8_t *input, size_t length,
			      char *output, size_t output_capacity)
{
	size_t unit_count;
	size_t output_length = 0U;

	if (input == NULL || output == NULL || output_capacity == 0U || length < 1U) {
		return -EINVAL;
	}
	unit_count = input[0];
	if (unit_count == 0U || length < 1U + unit_count * 2U ||
		get_u16(&input[1U + (unit_count - 1U) * 2U]) != 0U) {
		return -EBADMSG;
	}
	for (size_t index = 0U; index + 1U < unit_count; ++index) {
		uint32_t codepoint = get_u16(&input[1U + index * 2U]);
		uint8_t encoded[4];
		size_t encoded_length;

		if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
			uint32_t low;

			if (++index + 1U >= unit_count) {
				return -EBADMSG;
			}
			low = get_u16(&input[1U + index * 2U]);
			if (low < 0xDC00U || low > 0xDFFFU) {
				return -EBADMSG;
			}
			codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) +
				(low - 0xDC00U);
		} else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
			return -EBADMSG;
		}
		if (codepoint < 0x80U) {
			encoded[0] = (uint8_t)codepoint;
			encoded_length = 1U;
		} else if (codepoint < 0x800U) {
			encoded[0] = (uint8_t)(0xC0U | (codepoint >> 6));
			encoded[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
			encoded_length = 2U;
		} else if (codepoint < 0x10000U) {
			encoded[0] = (uint8_t)(0xE0U | (codepoint >> 12));
			encoded[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
			encoded[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
			encoded_length = 3U;
		} else {
			encoded[0] = (uint8_t)(0xF0U | (codepoint >> 18));
			encoded[1] = (uint8_t)(0x80U | ((codepoint >> 12) & 0x3FU));
			encoded[2] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3FU));
			encoded[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
			encoded_length = 4U;
		}
		if (output_length + encoded_length >= output_capacity) {
			return -ENOSPC;
		}
		memcpy(&output[output_length], encoded, encoded_length);
		output_length += encoded_length;
	}
	output[output_length] = '\0';
	return 0;
}

static int mtp_get_handles(struct usb_device *udev,
			   const struct ef800_virtual_usb_endpoints *endpoints,
			   uint32_t storage_id, uint32_t parent_handle,
			   uint32_t handles[EF800_VIRTUAL_USB_MAX_OBJECTS],
			   size_t *handle_count)
{
	uint32_t parameters[3] = {storage_id, 0U, parent_handle};
	uint8_t payload[sizeof(uint32_t) * (1U + EF800_VIRTUAL_USB_MAX_OBJECTS)];
	size_t payload_length = 0U;
	int ret;

	ret = mtp_command_data_in(udev, endpoints,
		EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT_HANDLES, parameters,
		ARRAY_SIZE(parameters), payload, sizeof(payload), &payload_length);
	if (ret != 0) {
		return ret;
	}
	if (payload_length < sizeof(uint32_t)) {
		LOG_ERR("MTP GetObjectHandles payload too short: %zu", payload_length);
		return -EBADMSG;
	}
	*handle_count = get_u32(payload);
	if (*handle_count > EF800_VIRTUAL_USB_MAX_OBJECTS ||
		payload_length != sizeof(uint32_t) +
		*handle_count * sizeof(uint32_t)) {
		LOG_ERR("MTP handle count invalid: count=%zu payload=%zu",
			*handle_count, payload_length);
		return -EOVERFLOW;
	}
	for (size_t index = 0U; index < *handle_count; ++index) {
		handles[index] = get_u32(&payload[sizeof(uint32_t) +
			index * sizeof(uint32_t)]);
	}
	return 0;
}

static int mtp_get_object_info(struct usb_device *udev,
			       const struct ef800_virtual_usb_endpoints *endpoints,
			       uint32_t handle,
			       struct ef800_virtual_usb_object_info *info)
{
	uint32_t parameters[1] = {handle};
	uint8_t payload[EF800_VIRTUAL_USB_MTP_MAX_INFO_SIZE];
	size_t length = 0U;
	uint8_t name_length;
	int ret;

	ret = mtp_command_data_in(udev, endpoints,
		EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT_INFO, parameters,
		ARRAY_SIZE(parameters), payload, sizeof(payload), &length);
	if (ret != 0) {
		return ret;
	}
	if (length < 53U) {
		LOG_ERR("MTP ObjectInfo is too short: %zu", length);
		return -EBADMSG;
	}
	name_length = payload[52];
	if (name_length == 0U ||
		length < 53U + (size_t)name_length * sizeof(uint16_t)) {
		LOG_ERR("MTP ObjectInfo filename invalid: size=%zu chars=%u",
			length, name_length);
		return -EBADMSG;
	}
	info->handle = handle;
	info->directory = get_u16(&payload[4]) ==
		EF800_VIRTUAL_USB_MTP_FORMAT_ASSOCIATION;
	info->size = get_u32(&payload[8]);
	ret = mtp_string_to_utf8(&payload[52],
		1U + (size_t)name_length * sizeof(uint16_t), info->name,
		sizeof(info->name));
	if (ret != 0) {
		LOG_ERR("MTP ObjectInfo UTF-16 conversion failed: %d", ret);
	}
	return ret;
}

static int mtp_storage_id(enum ef800_virtual_mtp_storage storage,
			  uint32_t *storage_id)
{
	if (storage < EF800_VIRTUAL_MTP_DATA || storage > EF800_VIRTUAL_MTP_FWU ||
		storage_id == NULL || (size_t)storage > peer_storage_count) {
		return -EINVAL;
	}
	*storage_id = peer_storage_ids[(size_t)storage - 1U];
	return 0;
}

static int validate_relative_path(const char *path)
{
	size_t length;
	size_t segment_start = 0U;

	if (path == NULL) {
		return -EINVAL;
	}
	length = bounded_strlen(path, EF800_VIRTUAL_USB_MAX_PATH + 1U);
	if (length > EF800_VIRTUAL_USB_MAX_PATH ||
		(path[0] == '/' && length != 0U) || strchr(path, '\\') != NULL) {
		return -EINVAL;
	}
	for (size_t index = 0U; index <= length; ++index) {
		if (path[index] == '/' || path[index] == '\0') {
			size_t segment_length = index - segment_start;

			if (segment_length == 0U && index != length) {
				return -EINVAL;
			}
			if ((segment_length == 1U && path[segment_start] == '.') ||
				(segment_length == 2U && path[segment_start] == '.' &&
				 path[segment_start + 1U] == '.')) {
				return -EINVAL;
			}
			if (segment_length > EF800_VIRTUAL_USB_MAX_NAME) {
				return -ENAMETOOLONG;
			}
			segment_start = index + 1U;
		}
	}
	return 0;
}

static int mtp_lookup_path(struct usb_device *udev,
			   const struct ef800_virtual_usb_endpoints *endpoints,
			   uint32_t storage_id, const char *path,
			   struct ef800_virtual_usb_object_info *info,
			   uint32_t *parent_handle)
{
	uint32_t parent = EF800_VIRTUAL_USB_MTP_ALL_ROOT_OBJECTS;
	char segment[EF800_VIRTUAL_USB_MAX_NAME + 1U];
	size_t path_length;
	size_t offset = 0U;
	int ret = validate_relative_path(path);

	if (ret != 0) {
		return ret;
	}
	path_length = strlen(path);
	if (path_length == 0U) {
		if (parent_handle != NULL) {
			*parent_handle = parent;
		}
		return 0;
	}
	while (offset < path_length) {
		uint32_t handles[EF800_VIRTUAL_USB_MAX_OBJECTS];
		size_t handle_count = 0U;
		size_t segment_end = offset;
		size_t segment_length;
		bool found = false;
		bool has_more;

		while (segment_end < path_length && path[segment_end] != '/') {
			segment_end++;
		}
		segment_length = segment_end - offset;
		memcpy(segment, &path[offset], segment_length);
		segment[segment_length] = '\0';
		has_more = segment_end < path_length;
		ret = mtp_get_handles(udev, endpoints, storage_id, parent, handles,
			&handle_count);
		if (ret != 0) {
			return ret;
		}
		for (size_t index = 0U; index < handle_count; ++index) {
			struct ef800_virtual_usb_object_info candidate;

			ret = mtp_get_object_info(udev, endpoints, handles[index],
				&candidate);
			if (ret != 0) {
				return ret;
			}
			if (strcmp(candidate.name, segment) == 0) {
				if (has_more && !candidate.directory) {
					return -ENOTDIR;
				}
			if (info != NULL) {
				*info = candidate;
			}
			parent = candidate.handle;
			found = true;
			break;
			}
		}
		if (!found) {
			return -ENOENT;
		}
		offset = segment_end + 1U;
	}
	if (parent_handle != NULL) {
		*parent_handle = parent;
	}
	return 0;
}

static int mtp_prepare_locked(struct usb_device **udev,
			      struct ef800_virtual_usb_endpoints *endpoints)
{
	int ret = ef800_virtual_usb_peer_init();

	if (ret != 0) {
		return ret;
	}
	ret = get_class_device(0x06U, udev, endpoints);
	if (ret != 0) {
		LOG_WRN("Virtual USB peer did not enumerate MTP interface: %d", ret);
		return ret;
	}
	ret = lock_device(*udev);
	if (ret != 0) {
		return ret;
	}
	ret = mtp_ensure_session(*udev, endpoints);
	if (ret != 0) {
		k_mutex_unlock(&(*udev)->mutex);
	}
	return ret;
}

static void mtp_finish_locked(struct usb_device *udev)
{
	k_mutex_unlock(&udev->mutex);
	k_mutex_unlock(&peer_mutex);
}

int ef800_virtual_usb_peer_cdc_write(const uint8_t *data, size_t length)
{
	struct usb_device *udev;
	struct ef800_virtual_usb_endpoints endpoints;
	int ret;

	if (data == NULL || length == 0U) {
		return -EINVAL;
	}
	ret = k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_virtual_usb_peer_init();
	if (ret == 0) {
		ret = get_class_device(0x0AU, &udev, &endpoints);
	}
	if (ret == 0) {
		ret = lock_device(udev);
		if (ret == 0) {
			ret = bulk_write(udev, endpoints.bulk_out, data, length);
			k_mutex_unlock(&udev->mutex);
		}
	}
	k_mutex_unlock(&peer_mutex);
	return ret;
}

int ef800_virtual_usb_peer_cdc_read(uint8_t *data, size_t capacity,
				    size_t *length, uint32_t timeout_ms)
{
	struct usb_device *udev;
	struct ef800_virtual_usb_endpoints endpoints;
	int ret;

	if (data == NULL || capacity == 0U || length == NULL || timeout_ms == 0U) {
		return -EINVAL;
	}
	*length = 0U;
	ret = k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_virtual_usb_peer_init();
	if (ret == 0) {
		ret = get_class_device(0x0AU, &udev, &endpoints);
	}
	if (ret == 0) {
		ret = lock_device(udev);
		if (ret == 0) {
			ret = bulk_read(udev, endpoints.bulk_in, data,
				MIN(capacity, (size_t)EF800_VIRTUAL_USB_TRANSFER_CHUNK),
				length, timeout_ms);
			k_mutex_unlock(&udev->mutex);
		}
	}
	k_mutex_unlock(&peer_mutex);
	return ret;
}

int ef800_virtual_usb_peer_mtp_list(enum ef800_virtual_mtp_storage storage,
				    const char *path,
				    struct ef800_virtual_mtp_entry *entries,
				    size_t capacity, size_t *count)
{
	struct usb_device *udev;
	struct ef800_virtual_usb_endpoints endpoints;
	uint32_t storage_id;
	uint32_t parent = EF800_VIRTUAL_USB_MTP_ALL_ROOT_OBJECTS;
	uint32_t handles[EF800_VIRTUAL_USB_MAX_OBJECTS];
	size_t handle_count = 0U;
	int ret;

	if (entries == NULL || count == NULL || capacity == 0U) {
		return -EINVAL;
	}
	*count = 0U;
	ret = k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	ret = mtp_prepare_locked(&udev, &endpoints);
	if (ret != 0) {
		k_mutex_unlock(&peer_mutex);
		return ret;
	}
	ret = mtp_storage_id(storage, &storage_id);
	if (ret == 0 && path != NULL && path[0] != '\0') {
		struct ef800_virtual_usb_object_info info;

		ret = mtp_lookup_path(udev, &endpoints, storage_id, path, &info, &parent);
		if (ret == 0 && !info.directory) {
			ret = -ENOTDIR;
		}
	}
	if (ret == 0) {
		ret = mtp_get_handles(udev, &endpoints, storage_id, parent,
			handles, &handle_count);
	}
	if (ret == 0 && handle_count > capacity) {
		ret = -ENOSPC;
	}
	for (size_t index = 0U; ret == 0 && index < handle_count; ++index) {
		struct ef800_virtual_usb_object_info info;

		ret = mtp_get_object_info(udev, &endpoints, handles[index], &info);
		if (ret == 0) {
			entries[index] = (struct ef800_virtual_mtp_entry){
				.handle = info.handle,
				.size = info.size,
				.directory = info.directory,
			};
			strcpy(entries[index].name, info.name);
			*count = index + 1U;
		}
	}
	mtp_finish_locked(udev);
	return ret;
}

int ef800_virtual_usb_peer_mtp_download(enum ef800_virtual_mtp_storage storage,
					const char *path, uint8_t *data,
					size_t capacity, size_t *length)
{
	struct usb_device *udev;
	struct ef800_virtual_usb_endpoints endpoints;
	struct ef800_virtual_usb_object_info info;
	struct ef800_virtual_usb_mtp_response response;
	uint32_t storage_id;
	uint32_t transaction_id = 0U;
	int ret;

	if (path == NULL || data == NULL || length == NULL) {
		return -EINVAL;
	}
	*length = 0U;
	ret = k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	ret = mtp_prepare_locked(&udev, &endpoints);
	if (ret != 0) {
		k_mutex_unlock(&peer_mutex);
		return ret;
	}
	ret = mtp_storage_id(storage, &storage_id);
	if (ret == 0) {
		ret = mtp_lookup_path(udev, &endpoints, storage_id, path, &info, NULL);
	}
	if (ret == 0 && info.directory) {
		ret = -EISDIR;
	}
	if (ret == 0 && info.size > EF800_VIRTUAL_USB_MAX_OBJECT_SIZE) {
		ret = -EFBIG;
	}
	if (ret == 0) {
		uint32_t parameters[1];

		parameters[0] = info.handle;
		transaction_id = mtp_next_transaction();
		ret = mtp_send_command(udev, &endpoints,
			EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT, transaction_id,
			parameters, ARRAY_SIZE(parameters));
	}
	if (ret == 0) {
		size_t payload_length = 0U;

		ret = mtp_read_container(udev, endpoints.bulk_in,
			EF800_VIRTUAL_USB_MTP_TYPE_DATA,
			EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT, transaction_id,
			data, capacity, &payload_length, NULL, 0U);
		*length = payload_length;
		if (ret == 0 && payload_length != info.size) {
			ret = -EBADMSG;
		}
		{
			int response_ret = mtp_read_response_code(udev, &endpoints,
				EF800_VIRTUAL_USB_MTP_OP_GET_OBJECT, transaction_id,
				&response);
			if (ret == 0) {
				ret = response_ret;
			}
		}
		if (ret == 0 && response.code != EF800_VIRTUAL_USB_MTP_RESP_OK) {
			ret = -EIO;
		}
	}
	mtp_finish_locked(udev);
	return ret;
}

int ef800_virtual_usb_peer_mtp_upload(enum ef800_virtual_mtp_storage storage,
					const char *path, const uint8_t *data,
					size_t length)
{
	struct usb_device *udev;
	struct ef800_virtual_usb_endpoints endpoints;
	struct ef800_virtual_usb_object_info parent_info;
	struct ef800_virtual_usb_object_info existing_info;
	struct ef800_virtual_usb_mtp_response response;
	uint32_t storage_id;
	uint32_t parent_handle = EF800_VIRTUAL_USB_MTP_ALL_ROOT_OBJECTS;
	uint32_t parameters[2];
	char parent_path[EF800_VIRTUAL_USB_MAX_PATH + 1U];
	const char *filename;
	const char *slash;
	uint8_t object_info[EF800_VIRTUAL_USB_MTP_MAX_INFO_SIZE];
	size_t filename_field_length;
	size_t filename_length;
	uint8_t filename_chars;
	size_t object_info_length;
	int ret;

	if (path == NULL || (length != 0U && data == NULL) ||
		length > EF800_VIRTUAL_USB_MAX_OBJECT_SIZE) {
		return -EINVAL;
	}
	ret = validate_relative_path(path);
	if (ret != 0 || path[0] == '\0') {
		return ret != 0 ? ret : -EINVAL;
	}
	ret = k_mutex_lock(&peer_mutex, K_MSEC(EF800_VIRTUAL_USB_TRANSFER_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}
	ret = mtp_prepare_locked(&udev, &endpoints);
	if (ret != 0) {
		k_mutex_unlock(&peer_mutex);
		return ret;
	}
	ret = mtp_storage_id(storage, &storage_id);
	if (ret != 0) {
		goto cleanup;
	}
	slash = strrchr(path, '/');
	filename = slash == NULL ? path : slash + 1;
	if (*filename == '\0' || strcmp(filename, ".") == 0 ||
		strcmp(filename, "..") == 0) {
		ret = -EINVAL;
		goto cleanup;
	}
	if (slash != NULL) {
		size_t parent_length = (size_t)(slash - path);

		if (parent_length == 0U || parent_length >= sizeof(parent_path)) {
			ret = -EINVAL;
			goto cleanup;
		}
		memcpy(parent_path, path, parent_length);
		parent_path[parent_length] = '\0';
		ret = mtp_lookup_path(udev, &endpoints, storage_id, parent_path,
			&parent_info, &parent_handle);
		if (ret != 0) {
			goto cleanup;
		}
		if (!parent_info.directory) {
			ret = -ENOTDIR;
			goto cleanup;
		}
	}
	ret = mtp_lookup_path(udev, &endpoints, storage_id, path,
		&existing_info, NULL);
	if (ret == 0) {
		ret = -EEXIST;
		goto cleanup;
	}
	if (ret != -ENOENT) {
		goto cleanup;
	}
	ret = utf8_to_mtp_string(filename, object_info, sizeof(object_info),
		&filename_length);
	if (ret != 0) {
		goto cleanup;
	}
	if (53U + filename_length + 3U > sizeof(object_info)) {
		ret = -ENOSPC;
		goto cleanup;
	}
	filename_chars = object_info[0];
	filename_field_length = filename_length - 1U;
	memmove(&object_info[53], &object_info[1], filename_field_length);
	memset(object_info, 0, 53U);
	put_u32(&object_info[0], storage_id);
	put_u16(&object_info[4], EF800_VIRTUAL_USB_MTP_FORMAT_UNDEFINED);
	put_u32(&object_info[8], (uint32_t)length);
	object_info[52] = filename_chars;
	put_u32(&object_info[38], parent_handle ==
		EF800_VIRTUAL_USB_MTP_ALL_ROOT_OBJECTS ? 0U : parent_handle);
	object_info_length = 53U + filename_field_length;
	object_info[object_info_length++] = 0U; /* DateCreated */
	object_info[object_info_length++] = 0U; /* DateModified */
	object_info[object_info_length++] = 0U; /* Keywords */

	parameters[0] = storage_id;
	parameters[1] = parent_handle;
	ret = mtp_command_data_out(udev, &endpoints,
		EF800_VIRTUAL_USB_MTP_OP_SEND_OBJECT_INFO, parameters,
		ARRAY_SIZE(parameters), object_info, object_info_length, &response);
	if (ret != 0) {
		goto cleanup;
	}
	if (response.parameter_count != 3U || response.parameters[0] != storage_id) {
		ret = -EBADMSG;
		goto cleanup;
	}
	ret = mtp_command_data_out(udev, &endpoints,
		EF800_VIRTUAL_USB_MTP_OP_SEND_OBJECT, NULL, 0U, data, length,
		&response);

cleanup:
	mtp_finish_locked(udev);
	return ret;
}
