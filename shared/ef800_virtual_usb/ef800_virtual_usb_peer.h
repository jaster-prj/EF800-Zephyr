/* SPDX-License-Identifier: Apache-2.0 */
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
int ef800_virtual_usb_peer_init(void);
int ef800_virtual_usb_peer_wait_for_device(uint32_t timeout_ms);
void ef800_virtual_usb_peer_invalidate_session(void);

/* Raw, bounded CDC ACM bulk operations for the native-sim host peer. */
int ef800_virtual_usb_peer_cdc_write(const uint8_t *data, size_t length);
int ef800_virtual_usb_peer_cdc_read(uint8_t *data, size_t capacity,
				    size_t *length, uint32_t timeout_ms);

/* MainApplication MTP operations use only Data, Plugins, and FWU. Paths are
 * relative to the selected storage root and use forward slashes. */
int ef800_virtual_usb_peer_mtp_list(enum ef800_virtual_mtp_storage storage,
				    const char *path,
				    struct ef800_virtual_mtp_entry *entries,
				    size_t capacity, size_t *count);
int ef800_virtual_usb_peer_mtp_download(enum ef800_virtual_mtp_storage storage,
					const char *path, uint8_t *data,
					size_t capacity, size_t *length);
int ef800_virtual_usb_peer_mtp_upload(enum ef800_virtual_mtp_storage storage,
					const char *path, const uint8_t *data,
					size_t length);

#endif /* EF800_VIRTUAL_USB_PEER_H_ */
