/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_backup_usb_mtp.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/class/usbd_mtp.h>
#include <zephyr/usb/usbd.h>

#define EF800_BACKUP_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_BACKUP_USB_UDC_NODE, okay),
	"BackupApplication recovery MTP requires the fitted USB controller");

USBD_DEVICE_DEFINE(ef800_backup_mtp_usbd,
	DEVICE_DT_GET(EF800_BACKUP_USB_UDC_NODE),
	CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);
USBD_DESC_LANG_DEFINE(ef800_backup_mtp_lang);
USBD_DESC_MANUFACTURER_DEFINE(ef800_backup_mtp_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(ef800_backup_mtp_product, "EF800 Recovery");
USBD_DESC_CONFIG_DEFINE(ef800_backup_mtp_config_desc, "EF800 Recovery MTP");

USBD_CONFIGURATION_DEFINE(ef800_backup_mtp_config,
	(IS_ENABLED(CONFIG_EF800_USB_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0U),
	125, &ef800_backup_mtp_config_desc);

USBD_MTP_DEFINE_INSTANCE(ef800_backup,
	"EF800", "EF800 Recovery", "1.0", "EF800000000000000000000000000002",
	USBD_MTP_STORAGE_ENTRY_NAMED("/EF800:/.configuration", "Configuration", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/data", "Data", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/plugins", "Plugins", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/fwu", "FWU", false));

static bool mtp_configured;
static bool mtp_active;

static void configure_mtp_device_descriptor(void)
{
	/* Keep this a single MTP/PTP interface for desktop MTP host compatibility. */
	fs_desc_ef800_backup_mtp_usbd.bDeviceClass = 0;
	fs_desc_ef800_backup_mtp_usbd.bDeviceSubClass = 0;
	fs_desc_ef800_backup_mtp_usbd.bDeviceProtocol = 0;
	fs_desc_ef800_backup_mtp_usbd.bMaxPacketSize0 = 64;
#if USBD_SUPPORTS_HIGH_SPEED
	hs_desc_ef800_backup_mtp_usbd.bDeviceClass = 0;
	hs_desc_ef800_backup_mtp_usbd.bDeviceSubClass = 0;
	hs_desc_ef800_backup_mtp_usbd.bDeviceProtocol = 0;
	hs_desc_ef800_backup_mtp_usbd.bMaxPacketSize0 = 64;
#endif
}

int ef800_backup_usb_mtp_start(void)
{
	int ret;

	if (mtp_active) {
		return 0;
	}
	configure_mtp_device_descriptor();
	if (!mtp_configured) {
		ret = usbd_add_descriptor(&ef800_backup_mtp_usbd, &ef800_backup_mtp_lang);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_descriptor(&ef800_backup_mtp_usbd,
			&ef800_backup_mtp_manufacturer);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_descriptor(&ef800_backup_mtp_usbd, &ef800_backup_mtp_product);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_configuration(&ef800_backup_mtp_usbd, USBD_SPEED_FS,
			&ef800_backup_mtp_config);
		if (ret != 0) {
			return ret;
		}
		mtp_configured = true;
	}
	ret = usbd_register_all_classes(&ef800_backup_mtp_usbd, USBD_SPEED_FS,
		1, NULL);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_init(&ef800_backup_mtp_usbd);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_enable(&ef800_backup_mtp_usbd);
	if (ret == 0) {
		mtp_active = true;
	}
	return ret;
}

int ef800_backup_usb_mtp_stop(void)
{
	int ret;

	if (!mtp_active) {
		return 0;
	}
	ret = usbd_disable(&ef800_backup_mtp_usbd);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	ret = usbd_shutdown(&ef800_backup_mtp_usbd);
	if (ret == 0) {
		mtp_active = false;
	}
	return ret;
}

bool ef800_backup_usb_mtp_is_active(void)
{
	return mtp_active;
}
