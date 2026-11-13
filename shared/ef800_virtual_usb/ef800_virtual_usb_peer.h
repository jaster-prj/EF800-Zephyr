/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Simulate USB peripheral enumeration and peer requests. */
#ifndef EF800_VIRTUAL_USB_PEER_H_
#define EF800_VIRTUAL_USB_PEER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EF800_VIRTUAL_USB_MAX_OBJECTS 12U
#define EF800_VIRTUAL_USB_MAX_PATH 128U
#define EF800_VIRTUAL_USB_MAX_NAME 96U
#define EF800_VIRTUAL_USB_MAX_OBJECT_SIZE (2U * 1024U * 1024U)

enum ef800_virtual_mtp_storage {
	EF800_VIRTUAL_MTP_DATA = 1,
	EF800_VIRTUAL_MTP_PLUGINS = 2,
	EF800_VIRTUAL_MTP_FWU = 3,
};

struct ef800_virtual_mtp_entry {
	uint32_t handle;
	uint32_t size;
	bool directory;
	char name[EF800_VIRTUAL_USB_MAX_NAME + 1U];
};

/* Start the virtual UHC/USB host once and wait for an enabled Main USB class. */

/** @brief Start the virtual USB host and initialize class discovery.
 * @return 0 on startup; negative errno if host initialization fails.
 */
int ef800_virtual_usb_peer_init(void);

/** @brief Wait for an enabled MainApplication USB class/device.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return 0 when found; negative errno on timeout or host error.
 */
int ef800_virtual_usb_peer_wait_for_device(uint32_t timeout_ms);

/** @brief Invalidate cached enumeration/session state after disconnect.
 * @return None.
 */
void ef800_virtual_usb_peer_invalidate_session(void);

/* Raw, bounded CDC ACM bulk operations for the native-sim host peer. */

/** @brief Send bounded CDC ACM data through the virtual USB host.
 * @param data Bytes to send.
 * @param length Byte count.
 * @return 0 on transfer; negative errno on disconnection or USB error.
 */
int ef800_virtual_usb_peer_cdc_write(const uint8_t *data, size_t length);

/** @brief Receive CDC ACM data with a bounded timeout.
 * @param data Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives bytes read.
 * @param timeout_ms Maximum wait time.
 * @return 0 on receive; negative errno on timeout or USB failure.
 */
int ef800_virtual_usb_peer_cdc_read(uint8_t *data, size_t capacity,
				    size_t *length, uint32_t timeout_ms);

/* MainApplication MTP operations use only Data, Plugins, and FWU. Paths are
 * relative to the selected storage root and use forward slashes. */

/** @brief List objects under a relative MTP storage path.
 * @param storage Exported storage root.
 * @param path Relative path within the root.
 * @param entries Destination entry array.
 * @param capacity Number of entries that fit.
 * @param count Receives entries written.
 * @return 0 on list; negative errno for invalid path/storage or MTP failure.
 */
int ef800_virtual_usb_peer_mtp_list(enum ef800_virtual_mtp_storage storage,
				    const char *path,
				    struct ef800_virtual_mtp_entry *entries,
				    size_t capacity, size_t *count);

/** @brief Download one object into a bounded buffer.
 * @param storage Exported storage root.
 * @param path Relative object path.
 * @param data Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives downloaded size.
 * @return 0 on download; negative errno if missing, too large, or transport fails.
 */
int ef800_virtual_usb_peer_mtp_download(enum ef800_virtual_mtp_storage storage,
					const char *path, uint8_t *data,
					size_t capacity, size_t *length);

/** @brief Upload one object from a bounded data buffer.
 * @param storage Exported storage root.
 * @param path Relative destination path.
 * @param data Object bytes.
 * @param length Object size.
 * @return 0 on upload; negative errno for invalid path, size, or transport failure.
 */
int ef800_virtual_usb_peer_mtp_upload(enum ef800_virtual_mtp_storage storage,
					const char *path, const uint8_t *data,
					size_t length);

#endif /* EF800_VIRTUAL_USB_PEER_H_ */
