/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Demonstrate USB device functionality on the EF800 platform. */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>
#include <ff.h>
#include <stm32l4xx.h>

LOG_MODULE_REGISTER(test_usb_sample, LOG_LEVEL_INF);

#define EF800_DISK_NAME "RAM"
#define EF800_MOUNT_POINT "/RAM:"
#define EF800_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

#if DT_NODE_EXISTS(DT_NODELABEL(zephyr_udc0))
#undef EF800_USB_UDC_NODE
#define EF800_USB_UDC_NODE DT_NODELABEL(zephyr_udc0)
#endif

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_USB_UDC_NODE, okay),
	"EF800 USB sample requires an enabled USB device controller");

USBD_DEVICE_DEFINE(test_usb_sample_usbd,
	DEVICE_DT_GET(EF800_USB_UDC_NODE),
	CONFIG_TEST_USB_SAMPLE_VID, CONFIG_TEST_USB_SAMPLE_PID);
USBD_DESC_LANG_DEFINE(test_usb_sample_lang);
USBD_DESC_MANUFACTURER_DEFINE(test_usb_sample_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(test_usb_sample_product,
	"EF800 USB Mass Storage Sample");
USBD_DESC_CONFIG_DEFINE(test_usb_sample_config_desc, "EF800 MSC");
USBD_CONFIGURATION_DEFINE(test_usb_sample_config, 0, 125,
	&test_usb_sample_config_desc);
USBD_DEFINE_MSC_LUN(test_usb_sample_lun, EF800_DISK_NAME, "EF800",
	"USB Disk", "1.00");

static FATFS fat_fs;
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.mnt_point = EF800_MOUNT_POINT,
	.fs_data = &fat_fs,
	.flags = IS_ENABLED(CONFIG_DISK_DRIVER_RAM) ? 0 : FS_MOUNT_FLAG_NO_FORMAT,
};

static void setup_flash_mount(void)
{
}

static int setup_storage(void)
{
	int ret;

	printk("test_usb_sample: storage init disk=%s mount=%s\r\n",
		EF800_DISK_NAME, EF800_MOUNT_POINT);
	setup_flash_mount();

	ret = disk_access_init(EF800_DISK_NAME);
	if (ret != 0) {
		printk("test_usb_sample: disk init failed=%d\r\n", ret);
		LOG_ERR("disk init failed: %d", ret);
		return ret;
	}

#if CONFIG_TEST_USB_SAMPLE_WIPE_STORAGE && CONFIG_DISK_DRIVER_FLASH
	const struct flash_area *area;

	ret = flash_area_open(PARTITION_ID(qspi_storage_partition), &area);
	if (ret == 0) {
		LOG_INF("erasing QSPI storage partition (%u bytes)",
			(unsigned int)area->fa_size);
		ret = flash_area_erase(area, 0, area->fa_size);
		flash_area_close(area);
	}
	if (ret != 0) {
		LOG_ERR("QSPI erase failed: %d", ret);
		return ret;
	}
#endif

	ret = fs_mount(&mount);
	if (ret != 0) {
		printk("test_usb_sample: FAT mount failed=%d\r\n", ret);
		LOG_ERR("FAT mount failed: %d (no format attempted)", ret);
		return ret;
	}

	LOG_INF("FAT mounted at %s", mount.mnt_point);
	printk("test_usb_sample: FAT mounted\r\n");
	return 0;
}

static int setup_usb(void)
{
	int ret;

	printk("test_usb_sample: USB descriptor setup\r\n");
	ret = usbd_add_descriptor(&test_usb_sample_usbd,
		&test_usb_sample_lang);
	if (ret != 0) { printk("test_usb_sample: lang failed=%d\r\n", ret); return ret; }
	ret = usbd_add_descriptor(&test_usb_sample_usbd,
		&test_usb_sample_manufacturer);
	if (ret != 0) { printk("test_usb_sample: manufacturer failed=%d\r\n", ret); return ret; }
	ret = usbd_add_descriptor(&test_usb_sample_usbd,
		&test_usb_sample_product);
	if (ret != 0) { printk("test_usb_sample: product failed=%d\r\n", ret); return ret; }
	ret = usbd_add_configuration(&test_usb_sample_usbd, USBD_SPEED_FS,
		&test_usb_sample_config);
	if (ret != 0) { printk("test_usb_sample: config failed=%d\r\n", ret); return ret; }
	ret = usbd_register_all_classes(&test_usb_sample_usbd, USBD_SPEED_FS,
		1, NULL);
	if (ret != 0) { printk("test_usb_sample: class registration failed=%d\r\n", ret); return ret; }
	ret = usbd_init(&test_usb_sample_usbd);
	if (ret != 0) { printk("test_usb_sample: USB init failed=%d\r\n", ret); return ret; }
	printk("test_usb_sample: USB enable\r\n");
	ret = usbd_enable(&test_usb_sample_usbd);
	printk("test_usb_sample: USB enable result=%d\r\n", ret);
	printk("test_usb_sample: USB caps vbus=%s\r\n",
		usbd_can_detect_vbus(&test_usb_sample_usbd) ? "yes" : "no");
	printk("test_usb_sample: GPIOA IDR=0x%08x MODER=0x%08x AFR1=0x%08x\r\n",
		(unsigned int)GPIOA->IDR, (unsigned int)GPIOA->MODER,
		(unsigned int)GPIOA->AFR[1]);
	printk("test_usb_sample: RCC CCIPR=0x%08x AHB2ENR=0x%08x\r\n",
		(unsigned int)RCC->CCIPR, (unsigned int)RCC->AHB2ENR);
	printk("test_usb_sample: USB GAHBCFG=0x%08x GUSBCFG=0x%08x\r\n",
		(unsigned int)USB_OTG_FS->GAHBCFG,
		(unsigned int)USB_OTG_FS->GUSBCFG);
	return ret;
}

int main(void)
{
	const struct device *udc = DEVICE_DT_GET(EF800_USB_UDC_NODE);
	int ret;

	if (!device_is_ready(udc)) {
		printk("test_usb_sample: USB controller not ready\r\n");
		LOG_ERR("USB controller is not ready");
		return -ENODEV;
	}

	ret = setup_storage();
	if (ret != 0) {
		return ret;
	}

	ret = setup_usb();
	if (ret != 0) {
		LOG_ERR("USB MSC setup failed: %d", ret);
		return ret;
	}

	LOG_INF("EF800 USB MSC enabled; host may connect");
	printk("test_usb_sample: MSC active, connect host\r\n");
	while (true) {
		k_sleep(K_FOREVER);
	}
}
