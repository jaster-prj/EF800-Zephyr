/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/class/usbd_mtp.h>
#include <zephyr/usb/usbd.h>

#define TEST_DISK "SPI"
#define TEST_MOUNT "/0:"
#define TEST_FAT_VOLUME "0:"
#define TEST_UDC_NODE DT_NODELABEL(zephyr_udc0)
#define TEST_USB_VID 0x2fe3
#define TEST_USB_PID 0xef86

BUILD_ASSERT(DT_NODE_HAS_STATUS(TEST_UDC_NODE, okay),
	"test-usb-mtp requires a USB device controller");

USBD_DEVICE_DEFINE(test_mtp_usbd, DEVICE_DT_GET(TEST_UDC_NODE),
	TEST_USB_VID, TEST_USB_PID);
USBD_DESC_LANG_DEFINE(test_mtp_lang);
USBD_DESC_MANUFACTURER_DEFINE(test_mtp_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(test_mtp_product, "EF800 USB MTP Test");
USBD_DESC_STRING_DEFINE(test_mtp_serial_number,
	"EF800MTPTEST0001", USBD_DUT_STRING_SERIAL_NUMBER);
USBD_DESC_CONFIG_DEFINE(test_mtp_config_desc, "MTP");
USBD_CONFIGURATION_DEFINE(test_mtp_config, USB_SCD_SELF_POWERED, 125,
	&test_mtp_config_desc);
USBD_MTP_DEFINE_INSTANCE(test_mtp,
	"EF800", "USB MTP Test", "1.0", "EF800000000000000000000000000001",
	USBD_MTP_STORAGE_ENTRY_NAMED(TEST_MOUNT "/data", "data", false),
	USBD_MTP_STORAGE_ENTRY_NAMED(TEST_MOUNT "/plugins", "plugins", false),
	USBD_MTP_STORAGE_ENTRY_NAMED(TEST_MOUNT "/fwu", "fwu", false));

USBD_DEVICE_DEFINE(test_msc_usbd, DEVICE_DT_GET(TEST_UDC_NODE),
	TEST_USB_VID, TEST_USB_PID + 1);
USBD_DESC_LANG_DEFINE(test_msc_lang);
USBD_DESC_MANUFACTURER_DEFINE(test_msc_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(test_msc_product, "EF800 USB MSC Test");
USBD_DESC_STRING_DEFINE(test_msc_serial_number,
	"EF800MSCTEST0001", USBD_DUT_STRING_SERIAL_NUMBER);
USBD_DESC_CONFIG_DEFINE(test_msc_config_desc, "MSC");
USBD_CONFIGURATION_DEFINE(test_msc_config, USB_SCD_SELF_POWERED, 125,
	&test_msc_config_desc);
USBD_DEFINE_MSC_LUN(test_msc_lun, TEST_DISK, "EF800", "SPI Flash", "1.00");

USBD_DEVICE_DEFINE(test_cdc_usbd, DEVICE_DT_GET(TEST_UDC_NODE),
	TEST_USB_VID, TEST_USB_PID + 2);
USBD_DESC_LANG_DEFINE(test_cdc_lang);
USBD_DESC_MANUFACTURER_DEFINE(test_cdc_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(test_cdc_product, "EF800 USB CDC Test");
USBD_DESC_STRING_DEFINE(test_cdc_serial_number,
	"EF800CDCTEST0001", USBD_DUT_STRING_SERIAL_NUMBER);
USBD_DESC_CONFIG_DEFINE(test_cdc_config_desc, "CDC");
USBD_CONFIGURATION_DEFINE(test_cdc_config, USB_SCD_SELF_POWERED, 125,
	&test_cdc_config_desc);

static FATFS fat_fs;
static const char *const mtp_roots[] = {
	TEST_MOUNT "/data",
	TEST_MOUNT "/plugins",
	TEST_MOUNT "/fwu",
};
static struct fs_mount_t mount = {
	.type = FS_FATFS,
	.mnt_point = TEST_MOUNT,
	.fs_data = &fat_fs,
	.flags = 0,
};

enum usb_mode { USB_NONE, USB_CDC, USB_MTP, USB_MSC };
static enum usb_mode active_mode;
static bool mtp_configured;
static bool msc_configured;
static bool cdc_configured;

static void configure_mtp_device_descriptor(void)
{
	/* A single MTP/PTP interface is not an IAD composite device. */
	fs_desc_test_mtp_usbd.bDeviceClass = 0;
	fs_desc_test_mtp_usbd.bDeviceSubClass = 0;
	fs_desc_test_mtp_usbd.bDeviceProtocol = 0;
#if USBD_SUPPORTS_HIGH_SPEED
	hs_desc_test_mtp_usbd.bDeviceClass = 0;
	hs_desc_test_mtp_usbd.bDeviceSubClass = 0;
	hs_desc_test_mtp_usbd.bDeviceProtocol = 0;
#endif
}

static const char *mode_name(enum usb_mode mode)
{
	switch (mode) {
	case USB_CDC: return "cdc";
	case USB_MTP: return "mtp";
	case USB_MSC: return "msc";
	default: return "none";
	}
}

static int add_common_descriptors(struct usbd_context *ctx,
	struct usbd_desc_node *lang, struct usbd_desc_node *manufacturer,
	struct usbd_desc_node *product, struct usbd_desc_node *serial,
	struct usbd_config_node *config,
	const char *const *blocklist, bool *configured)
{
	int ret;

	if (!*configured) {
		ret = usbd_add_descriptor(ctx, lang);
		if (ret) return ret;
		ret = usbd_add_descriptor(ctx, manufacturer);
		if (ret) return ret;
		ret = usbd_add_descriptor(ctx, product);
		if (ret) return ret;
		ret = usbd_add_descriptor(ctx, serial);
		if (ret) return ret;
		ret = usbd_add_configuration(ctx, USBD_SPEED_FS, config);
		if (ret) return ret;
		*configured = true;
	}
	ret = usbd_register_all_classes(ctx, USBD_SPEED_FS, 1, blocklist);
	if (ret) return ret;
	return usbd_init(ctx);
}

static int stop_usb(void)
{
	struct usbd_context *ctx = NULL;
	int ret;

	switch (active_mode) {
	case USB_MTP: ctx = &test_mtp_usbd; break;
	case USB_MSC: ctx = &test_msc_usbd; break;
	case USB_CDC: ctx = &test_cdc_usbd; break;
	default: return 0;
	}
	ret = usbd_disable(ctx);
	if (ret != 0 && ret != -EALREADY) return ret;
	ret = usbd_shutdown(ctx);
	if (ret == 0) active_mode = USB_NONE;
	return ret;
}

static int start_usb(enum usb_mode mode)
{
	static const char *const mtp_block[] = {"cdc_acm_0", "msc_0", NULL};
	static const char *const msc_block[] = {"cdc_acm_0", "mtp_0", NULL};
	static const char *const cdc_block[] = {"msc_0", "mtp_0", NULL};
	struct usbd_context *ctx;
	int ret;

	if (mode == USB_NONE) return 0;
	if (mode == USB_MSC && active_mode == USB_NONE) {
		ret = fs_unmount(&mount);
		if (ret) return ret;
	}
	if (mode != USB_MSC && active_mode == USB_MSC) return -EAGAIN;
	if (mode == USB_MTP) {
		ctx = &test_mtp_usbd;
		configure_mtp_device_descriptor();
		ret = add_common_descriptors(ctx, &test_mtp_lang, &test_mtp_manufacturer,
			&test_mtp_product, &test_mtp_serial_number, &test_mtp_config,
			mtp_block, &mtp_configured);
	} else if (mode == USB_MSC) {
		ctx = &test_msc_usbd;
		ret = add_common_descriptors(ctx, &test_msc_lang, &test_msc_manufacturer,
			&test_msc_product, &test_msc_serial_number, &test_msc_config,
			msc_block, &msc_configured);
	} else {
		ctx = &test_cdc_usbd;
		ret = add_common_descriptors(ctx, &test_cdc_lang, &test_cdc_manufacturer,
			&test_cdc_product, &test_cdc_serial_number, &test_cdc_config,
			cdc_block, &cdc_configured);
	}
	if (ret == 0) {
		ret = usbd_enable(ctx);
		if (ret == 0) active_mode = mode;
	}
	return ret;
}

static int cmd_usb(const struct shell *shell, size_t argc, char **argv)
{
	static const struct { const char *name; enum usb_mode mode; } modes[] = {
		{"none", USB_NONE}, {"cdc", USB_CDC}, {"mtp", USB_MTP}, {"msc", USB_MSC},
	};
	int ret;

	if (argc != 2) return -EINVAL;
	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strcmp(argv[1], modes[i].name) == 0) {
			if (modes[i].mode != USB_MSC && active_mode == USB_MSC) {
				shell_error(shell, "MSC exit requires reboot");
				return -EAGAIN;
			}
			ret = stop_usb();
			if (ret == 0) ret = start_usb(modes[i].mode);
			shell_print(shell, "usb %s active=%s result=%d", argv[1],
				mode_name(active_mode), ret);
			return ret;
		}
	}
	return -EINVAL;
}

static int cmd_usb_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(shell, "mode=%s", mode_name(active_mode));
	return 0;
}

static int cmd_disk_diag(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	uint32_t sectors = 0;
	uint32_t sector_size = 0;
	static uint8_t sector[4096];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = disk_access_init(TEST_DISK);
	int init_ret = ret;
	if (ret == 0) {
		ret = disk_access_ioctl(TEST_DISK, DISK_IOCTL_GET_SECTOR_COUNT, &sectors);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(TEST_DISK, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	}
	int read_ret = ret == 0 ? disk_access_read(TEST_DISK, sector, 0, 1) : ret;
	shell_print(shell, "disk=%s init=%d sectors=%u sector_size=%u result=%d",
		TEST_DISK, init_ret, sectors, sector_size, ret);
	shell_print(shell, "sector0 read=%d bytes=%02x %02x %02x %02x",
		read_ret, sector[0], sector[1], sector[2], sector[3]);
	return ret;
}

static int cmd_mount_diag(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = fs_mount(&mount);
	shell_print(shell, "mount=%s result=%d", TEST_MOUNT, ret);
	if (ret == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(mtp_roots); i++) {
			int mkdir_ret = fs_mkdir(mtp_roots[i]);
			shell_print(shell, "mkdir %s result=%d", mtp_roots[i], mkdir_ret);
		}
	}
	return ret;
}

static int cmd_flash_partition_test(const struct shell *shell,
	size_t argc, char **argv)
{
	const struct flash_area *area;
	uint8_t tx[256];
	uint8_t rx[sizeof(tx)];
	int ret;
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(test_spi_flash));
	uint8_t direct_tx[16];
	uint8_t direct_rx[sizeof(direct_tx)];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	for (size_t i = 0; i < sizeof(direct_tx); i++) {
		direct_tx[i] = (uint8_t)(0x3cU ^ i);
	}
	ret = flash_erase(flash, 0, 4096);
	shell_print(shell, "direct erase= %d", ret);
	k_msleep(100);
	if (ret == 0) {
		ret = flash_read(flash, 0, direct_rx, sizeof(direct_rx));
		shell_print(shell, "post-erase read=%d first=%02x %02x %02x %02x",
			ret, direct_rx[0], direct_rx[1], direct_rx[2], direct_rx[3]);
	}
	if (ret == 0) {
		ret = flash_write(flash, 0, direct_tx, sizeof(direct_tx));
		shell_print(shell, "direct write=%d", ret);
		k_msleep(100);
	}
	if (ret == 0) {
		ret = flash_read(flash, 0, direct_rx, sizeof(direct_rx));
		shell_print(shell, "direct read=%d compare=%d", ret,
			ret == 0 ? memcmp(direct_tx, direct_rx, sizeof(direct_tx)) : -1);
		shell_hexdump(shell, direct_rx, sizeof(direct_rx));
	}
	if (ret != 0) {
		return ret;
	}
	for (size_t i = 0; i < sizeof(tx); i++) {
		tx[i] = (uint8_t)(0xa5U ^ i);
	}

	ret = flash_area_open(PARTITION_ID(spi_test_storage), &area);
	if (ret != 0) {
		shell_error(shell, "partition open failed=%d", ret);
		return ret;
	}
	shell_print(shell, "partition id=%u offset=0x%lx size=0x%x",
		(unsigned int)area->fa_id, (long)area->fa_off,
		(unsigned int)area->fa_size);
	ret = flash_area_erase(area, 0, area->fa_size);
	shell_print(shell, "partition erase=%d", ret);
	if (ret == 0) {
		ret = flash_area_write(area, 0, tx, sizeof(tx));
		shell_print(shell, "partition write=%d", ret);
	}
	if (ret == 0) {
		ret = flash_area_read(area, 0, rx, sizeof(rx));
		shell_print(shell, "partition read=%d compare=%d",
			ret, ret == 0 ? memcmp(tx, rx, sizeof(tx)) : -1);
	}
	flash_area_close(area);
	return ret;
}

static int cmd_sd_format(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = fs_unmount(&mount);
	if (ret != 0 && ret != -EINVAL) {
		shell_error(shell, "unmount before format failed=%d", ret);
		return ret;
	}
	ret = fs_mkfs(FS_FATFS, (uintptr_t)TEST_FAT_VOLUME, NULL, 0);
	shell_print(shell, "sd format result=%d", ret);
	if (ret == 0) {
		ret = disk_access_ioctl(TEST_DISK, DISK_IOCTL_CTRL_SYNC, NULL);
		shell_print(shell, "disk sync after format=%d", ret);
	}
	if (ret == 0) {
		ret = fs_mount(&mount);
		shell_print(shell, "sd mount after format=%d", ret);
		if (ret == 0) {
			for (size_t i = 0; i < ARRAY_SIZE(mtp_roots); i++) {
				int mkdir_ret = fs_mkdir(mtp_roots[i]);
				shell_print(shell, "mkdir %s result=%d", mtp_roots[i], mkdir_ret);
			}
		}
	}
	return ret;
}

SHELL_CMD_REGISTER(usb, NULL, "Switch USB mode: none|cdc|mtp|msc", cmd_usb);
SHELL_CMD_REGISTER(usb_status, NULL, "Show USB mode", cmd_usb_status);
SHELL_CMD_REGISTER(disk_diag, NULL, "Check SPI flash disk", cmd_disk_diag);
SHELL_CMD_REGISTER(mount_diag, NULL, "Mount SPI FAT volume", cmd_mount_diag);
SHELL_CMD_REGISTER(flash_partition_test, NULL,
	"Directly erase/write/read the SPI partition", cmd_flash_partition_test);
SHELL_CMD_REGISTER(sd_format, NULL, "Erase and format SPI FAT volume", cmd_sd_format);

int main(void)
{
	int ret;

	printk("test-usb-mtp starting SPI-NOR storage\n");
	ret = disk_access_init(TEST_DISK);
	printk("disk init %s result=%d\n", TEST_DISK, ret);
	if (ret) { return ret; }
	ret = fs_mount(&mount);
	if (ret) {
		printk("FAT mount failed=%d; formatting test volume\n", ret);
		ret = fs_mkfs(FS_FATFS, (uintptr_t)TEST_FAT_VOLUME, NULL, 0);
		printk("format result=%d\n", ret);
		if (ret == 0) {
			(void)disk_access_ioctl(TEST_DISK, DISK_IOCTL_CTRL_SYNC, NULL);
			ret = fs_mount(&mount);
		}
		if (ret) {
			printk("FAT mount after format failed=%d\n", ret);
			return ret;
		}
	}
	k_msleep(100);
	for (size_t i = 0; i < ARRAY_SIZE(mtp_roots); i++) {
		int mkdir_ret = fs_mkdir(mtp_roots[i]);
		printk("mkdir %s result=%d\n", mtp_roots[i], mkdir_ret);
	}
	printk("test-usb-mtp ready; console USART3 115200 8N1\n");
	printk("SPI-NOR FAT mounted at %s; commands: usb none|cdc|mtp|msc, usb_status\n", TEST_MOUNT);
	ret = start_usb(USB_MTP);
	printk("default USB MTP start result=%d\n", ret);
	return 0;
}
