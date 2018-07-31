/*
 * (C) Copyright 2008 - 2009
 * Windriver, <www.windriver.com>
 * Tom Rix <Tom.Rix@windriver.com>
 *
 * Copyright 2011 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * Copyright 2014 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <config.h>
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/compiler.h>
#include <version.h>
#include <g_dnl.h>
#include <asm/arch/cpu.h>
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
#include <fb_mmc.h>
#include <fb_storage.h>
#include <fb_fastboot.h>
#include <emmc_partitions.h>
#endif
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
#include <fb_nand.h>
#endif
#include <partition_table.h>
#include <android_image.h>
#include <image.h>

DECLARE_GLOBAL_DATA_PTR;

#define FASTBOOT_VERSION		"0.4"

#define FASTBOOT_INTERFACE_CLASS	0xff
#define FASTBOOT_INTERFACE_SUB_CLASS	0x42
#define FASTBOOT_INTERFACE_PROTOCOL	0x03

#define RX_ENDPOINT_MAXIMUM_PACKET_SIZE_2_0  (0x0200)
#define RX_ENDPOINT_MAXIMUM_PACKET_SIZE_1_1  (0x0040)
#define TX_ENDPOINT_MAXIMUM_PACKET_SIZE      (0x0040)

#ifdef CONFIG_DEVICE_PRODUCT
#define DEVICE_PRODUCT	CONFIG_DEVICE_PRODUCT
#endif
#define DEVICE_SERIAL	"1234567890"

/* The 64 defined bytes plus \0 */

#define EP_BUFFER_SIZE	4096

struct f_fastboot {
	struct usb_function usb_function;

	/* IN/OUT EP's and corresponding requests */
	struct usb_ep *in_ep, *out_ep;
	struct usb_request *in_req, *out_req;
};

static inline struct f_fastboot *func_to_fastboot(struct usb_function *f)
{
	return container_of(f, struct f_fastboot, usb_function);
}

static struct f_fastboot *fastboot_func;
static unsigned int download_size;
static unsigned int download_bytes;

static struct usb_endpoint_descriptor fs_ep_in = {
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = USB_DIR_IN,
	.bmAttributes       = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     = RX_ENDPOINT_MAXIMUM_PACKET_SIZE_2_0,
	.bInterval          = 0x00,
};

static struct usb_endpoint_descriptor fs_ep_out = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= RX_ENDPOINT_MAXIMUM_PACKET_SIZE_1_1,
	.bInterval		= 0x00,
};

static struct usb_endpoint_descriptor hs_ep_out = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= RX_ENDPOINT_MAXIMUM_PACKET_SIZE_2_0,
	.bInterval		= 0x00,
};

static struct usb_interface_descriptor interface_desc = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0x00,
	.bAlternateSetting	= 0x00,
	.bNumEndpoints		= 0x02,
	.bInterfaceClass	= FASTBOOT_INTERFACE_CLASS,
	.bInterfaceSubClass	= FASTBOOT_INTERFACE_SUB_CLASS,
	.bInterfaceProtocol	= FASTBOOT_INTERFACE_PROTOCOL,
};

static struct usb_descriptor_header *fb_runtime_descs[] = {
	(struct usb_descriptor_header *)&interface_desc,
	(struct usb_descriptor_header *)&fs_ep_in,
	(struct usb_descriptor_header *)&hs_ep_out,
	NULL,
};

/*
 * static strings, in UTF-8
 */
static const char fastboot_name[] = "Android Fastboot";

static struct usb_string fastboot_string_defs[] = {
	[0].s = fastboot_name,
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_fastboot = {
	.language	= 0x0409,	/* en-us */
	.strings	= fastboot_string_defs,
};

static struct usb_gadget_strings *fastboot_strings[] = {
	&stringtab_fastboot,
	NULL,
};

#define DRAM_UBOOT_RESERVE		0x01000000
unsigned int ddr_size_usable(unsigned int addr_start)
{
	unsigned int ddr_size=0;
	unsigned int free_size = 0;
	int i;

	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++)
		ddr_size += gd->bd->bi_dram[i].size;

	free_size = (ddr_size - DRAM_UBOOT_RESERVE - addr_start - CONFIG_SYS_MALLOC_LEN - CONFIG_SYS_MEM_TOP_HIDE);
#if defined CONFIG_FASTBOOT_MAX_DOWN_SIZE
	if (free_size > CONFIG_FASTBOOT_MAX_DOWN_SIZE)
		free_size = CONFIG_FASTBOOT_MAX_DOWN_SIZE;
#endif
	return free_size;
}

static void rx_handler_command(struct usb_ep *ep, struct usb_request *req);

static char *response_str;

void fastboot_fail(const char *s)
{
	strncpy(response_str, "FAIL", 4);
	strncat(response_str, s, RESPONSE_LEN - 4 - 1);
}

void fastboot_okay(const char *s)
{
	strncpy(response_str, "OKAY", 4);
	strncat(response_str, s, RESPONSE_LEN - 4 - 1);
}

static void fastboot_complete(struct usb_ep *ep, struct usb_request *req)
{
	int status = req->status;

	if (!status)
		return;
	printf("status: %d ep '%s' trans: %d\n", status, ep->name, req->actual);
}

static int fastboot_bind(struct usb_configuration *c, struct usb_function *f)
{
	int id;
	struct usb_gadget *gadget = c->cdev->gadget;
	struct f_fastboot *f_fb = func_to_fastboot(f);

	/* DYNAMIC interface numbers assignments */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	interface_desc.bInterfaceNumber = id;

	id = usb_string_id(c->cdev);
	if (id < 0)
		return id;
	fastboot_string_defs[0].id = id;
	interface_desc.iInterface = id;

	f_fb->in_ep = usb_ep_autoconfig(gadget, &fs_ep_in);
	if (!f_fb->in_ep)
		return -ENODEV;
	f_fb->in_ep->driver_data = c->cdev;

	f_fb->out_ep = usb_ep_autoconfig(gadget, &fs_ep_out);
	if (!f_fb->out_ep)
		return -ENODEV;
	f_fb->out_ep->driver_data = c->cdev;

	hs_ep_out.bEndpointAddress = fs_ep_out.bEndpointAddress;

	return 0;
}

static void fastboot_unbind(struct usb_configuration *c, struct usb_function *f)
{
	memset(fastboot_func, 0, sizeof(*fastboot_func));
}

static void fastboot_disable(struct usb_function *f)
{
	struct f_fastboot *f_fb = func_to_fastboot(f);

	usb_ep_disable(f_fb->out_ep);
	usb_ep_disable(f_fb->in_ep);

	if (f_fb->out_req) {
		free(f_fb->out_req->buf);
		usb_ep_free_request(f_fb->out_ep, f_fb->out_req);
		f_fb->out_req = NULL;
	}
	if (f_fb->in_req) {
		free(f_fb->in_req->buf);
		usb_ep_free_request(f_fb->in_ep, f_fb->in_req);
		f_fb->in_req = NULL;
	}
}

static struct usb_request *fastboot_start_ep(struct usb_ep *ep)
{
	struct usb_request *req;

	req = usb_ep_alloc_request(ep, 0);
	if (!req)
		return NULL;

	req->length = EP_BUFFER_SIZE;
	req->buf = memalign(CONFIG_SYS_CACHELINE_SIZE, EP_BUFFER_SIZE);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	memset(req->buf, 0, req->length);
	return req;
}

static int fastboot_set_alt(struct usb_function *f,
			    unsigned interface, unsigned alt)
{
	int ret;
	struct f_fastboot *f_fb = func_to_fastboot(f);

	debug("%s: func: %s intf: %d alt: %d\n",
	      __func__, f->name, interface, alt);

	/* make sure we don't enable the ep twice */
	ret = usb_ep_enable(f_fb->out_ep, &hs_ep_out);
	if (ret) {
		puts("failed to enable out ep\n");
		return ret;
	}

	f_fb->out_req = fastboot_start_ep(f_fb->out_ep);
	if (!f_fb->out_req) {
		puts("failed to alloc out req\n");
		ret = -EINVAL;
		goto err;
	}
	f_fb->out_req->complete = rx_handler_command;

	ret = usb_ep_enable(f_fb->in_ep, &fs_ep_in);
	if (ret) {
		puts("failed to enable in ep\n");
		goto err;
	}

	f_fb->in_req = fastboot_start_ep(f_fb->in_ep);
	if (!f_fb->in_req) {
		puts("failed alloc req in\n");
		ret = -EINVAL;
		goto err;
	}
	f_fb->in_req->complete = fastboot_complete;

	ret = usb_ep_queue(f_fb->out_ep, f_fb->out_req, 0);
	if (ret)
		goto err;

	return 0;
err:
	fastboot_disable(f);
	return ret;
}

static int  fastboot_setup(struct usb_function *f,
	const struct usb_ctrlrequest *ctrl)
{
	int value = -EOPNOTSUPP;
	struct f_fastboot *f_fb = func_to_fastboot(f);

	/* composite driver infrastructure handles everything; interface
	 * activation uses set_alt().
	 */
	if (((ctrl->bRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPOINT)
		&& (ctrl->bRequest == USB_REQ_CLEAR_FEATURE)
		&& (ctrl->wValue== USB_ENDPOINT_HALT)) {
		switch (ctrl->wIndex & 0xfe) {
		case USB_DIR_OUT:
			value = ctrl->wLength;
			usb_ep_clear_halt(f_fb->out_ep);
			break;

		case USB_DIR_IN:
			value = ctrl->wLength;
			usb_ep_clear_halt(f_fb->in_ep);
			break;
		default:
			printf("unknown usb_ctrlrequest\n");
			break;
		}
	}

	return value;
}

static int fastboot_add(struct usb_configuration *c)
{
	struct f_fastboot *f_fb = fastboot_func;
	int status;

	if (!f_fb) {
		f_fb = memalign(CONFIG_SYS_CACHELINE_SIZE, sizeof(*f_fb));
		if (!f_fb)
			return -ENOMEM;

		fastboot_func = f_fb;
		memset(f_fb, 0, sizeof(*f_fb));
	}

	f_fb->usb_function.name = "f_fastboot";
	f_fb->usb_function.hs_descriptors = fb_runtime_descs;
	f_fb->usb_function.bind = fastboot_bind;
	f_fb->usb_function.unbind = fastboot_unbind;
	f_fb->usb_function.set_alt = fastboot_set_alt;
	f_fb->usb_function.disable = fastboot_disable;
	f_fb->usb_function.strings = fastboot_strings;
	f_fb->usb_function.setup = fastboot_setup;

	status = usb_add_function(c, &f_fb->usb_function);
	if (status) {
		free(f_fb);
		fastboot_func = f_fb;
	}

	return status;
}
DECLARE_GADGET_BIND_CALLBACK(usb_dnl_fastboot, fastboot_add);

static int fastboot_tx_write(const char *buffer, unsigned int buffer_size)
{
	struct usb_request *in_req = fastboot_func->in_req;
	int ret;

	memcpy(in_req->buf, buffer, buffer_size);
	in_req->length = buffer_size;
	ret = usb_ep_queue(fastboot_func->in_ep, in_req, 0);
	if (ret)
		printf("Error %d on queue\n", ret);
	return 0;
}

static int fastboot_tx_write_str(const char *buffer)
{
	return fastboot_tx_write(buffer, strlen(buffer));
}

static void compl_do_reset(struct usb_ep *ep, struct usb_request *req)
{
	do_reset(NULL, 0, 0, NULL);
}

static void compl_do_reboot_bootloader(struct usb_ep *ep, struct usb_request *req)
{
	run_command("reboot fastboot", 0);
}


static void cb_reboot(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;

	printf("cmd cb_reboot is %s\n", cmd);

	strsep(&cmd, "-");
	if (!cmd) {
		fastboot_func->in_req->complete = compl_do_reset;
		fastboot_tx_write_str("OKAY");
		return;
	}

	fastboot_func->in_req->complete = compl_do_reboot_bootloader;
	fastboot_tx_write_str("OKAY");
}

static int strcmp_l1(const char *s1, const char *s2)
{
	if (!s1 || !s2)
		return -1;
	return strncmp(s1, s2, strlen(s1));
}

static void cb_getvar(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[RESPONSE_LEN];
	char *s;
	char *s1;
	char *s2;
	char *s3;
	size_t chars_left;

	strcpy(response, "OKAY");
	chars_left = sizeof(response) - strlen(response) - 1;

	strsep(&cmd, ":");
	printf("cb_getvar: %s\n", cmd);
	if (!cmd) {
		error("missing variable\n");
		fastboot_tx_write_str("FAILmissing var");
		return;
	}

	if (!strcmp_l1("version", cmd)) {
		strncat(response, FASTBOOT_VERSION, chars_left);
	} else if (!strcmp_l1("bootloader-version", cmd)) {
		strncat(response, U_BOOT_VERSION, chars_left);
	} else if (!strcmp_l1("downloadsize", cmd) ||
		!strcmp_l1("max-download-size", cmd)) {
		char str_num[12];

		sprintf(str_num, "0x%08x", ddr_size_usable(CONFIG_USB_FASTBOOT_BUF_ADDR));
		strncat(response, str_num, chars_left);
	} else if (!strcmp_l1("serialno", cmd)) {
		//s = getenv("serial");
		s = get_usid_string();
		if (s)
			strncat(response, s, chars_left);
		else
			strncat(response, DEVICE_SERIAL, chars_left);
	} else if (!strcmp_l1("product", cmd)) {
#ifdef DEVICE_PRODUCT
		s1 = DEVICE_PRODUCT;
		printf("DEVICE_PRODUCT: %s\n", s1);
#else
		s1 = getenv("device_product");
		printf("device_product: %s\n", s1);
#endif
		strncat(response, s1, chars_left);
	} else if (!strcmp_l1("slot-count", cmd)) {
		strncat(response, "2", chars_left);
	} else if (!strcmp_l1("slot-suffixes", cmd)) {
		s2 = getenv("slot-suffixes");
		printf("slot-suffixes: %s\n", s2);
		if (s2)
			strncat(response, s2, chars_left);
		else
			strncat(response, "0", chars_left);
	} else if (!strcmp_l1("current-slot", cmd)) {
		s3 = getenv("active_slot");
		printf("active_slot: %s\n", s3);
		strncat(response, s3, chars_left);
	} else if (!strcmp_l1("has-slot:bootloader", cmd)) {
		printf("do not has slot bootloader\n");
		strncat(response, "no", chars_left);
	} else if (!strcmp_l1("has-slot:boot", cmd)) {
		if (has_boot_slot == 1) {
			printf("has boot slot\n");
			strncat(response, "yes", chars_left);
		} else
			strncat(response, "no", chars_left);
	} /*else if (!strcmp_l1("partition-type:boot", cmd)) {
		char *s2, *s3;
		s2 = getenv("active_slot");
		printf("active_slot: %s\n", s2);
		s3 = "boot";
		printf("s3  1: %s\n", s3);
		strcat(s3, s2);
		printf("s3  2: %s\n", s3);
		strncat(response, s3, chars_left);
	} */else if (!strcmp_l1("has-slot:system", cmd)) {
		if (has_system_slot == 1) {
			printf("has system slot\n");
			strncat(response, "yes", chars_left);
		} else
			strncat(response, "no", chars_left);
	} else if (!strcmp_l1("has-slot:vendor", cmd)) {
		if (has_boot_slot == 1) {
			printf("has vendor slot\n");
			strncat(response, "yes", chars_left);
		} else
			strncat(response, "no", chars_left);
	} else if (!strcmp_l1("has-slot:odm", cmd)) {
		if (has_boot_slot == 1) {
			printf("has odm slot\n");
			strncat(response, "yes", chars_left);
		} else
			strncat(response, "no", chars_left);
	} else if (!strncmp("partition-size", cmd, strlen("partition-size"))) {
		char str_num[20];
		struct partitions *pPartition;
		uint64_t sz;
		strsep(&cmd, ":");
		printf("partition is %s\n", cmd);
		if (strcmp(cmd, "userdata") == 0) {
			strcpy(cmd, "data");
			printf("partition is %s\n", cmd);
		}
		if (!strncmp("mbr", cmd, strlen("mbr"))) {
			strcpy(response, "FAILVariable not implemented");
		} else {
			if (!strncmp("bootloader-", cmd, strlen("bootloader-"))) {
				strsep(&cmd, "-");
				mmc_boot_size(cmd, &sz);
				printf("size:%016llx\n", sz);
				sprintf(str_num, "%016llx", sz);
			} else {
				pPartition = find_mmc_partition_by_name(cmd);
				if (pPartition) {
					printf("size:%016llx\n", pPartition->size);
					if (strcmp(cmd, "data") == 0) {
						printf("reserve 0x4000 for fde data\n");
						sz = pPartition->size - 0x4000;
						printf("data size :%016llx\n", sz);
						sprintf(str_num, "%016llx", sz);
					} else {
						sprintf(str_num, "%016llx", pPartition->size);
					}
				} else {
					printf("find_mmc_partition_by_name fail\n");
					sprintf(str_num, "get fail");
				}
			}
			strncat(response, str_num, chars_left);
		}
	} else if (!strcmp_l1("partition-type:cache", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:data", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:userdata", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:system", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:vendor", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:odm", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strcmp_l1("partition-type:tee", cmd)) {
		strncat(response, "ext4", chars_left);
	} else if (!strncmp("partition-type", cmd, strlen("partition-type"))) {
		strncat(response, "raw", chars_left);
	} else if (!strcmp_l1("erase-block-size", cmd) ||
		!strcmp_l1("logical-block-size", cmd)) {
		strncat(response, "2000", chars_left);
	} else {
		error("unknown variable: %s\n", cmd);
		strcpy(response, "FAILVariable not implemented");
	}
	fastboot_tx_write_str(response);
}

static unsigned int rx_bytes_expected(void)
{
	int rx_remain = download_size - download_bytes;
	if (rx_remain < 0)
		return 0;
	if (rx_remain > EP_BUFFER_SIZE)
		return EP_BUFFER_SIZE;
	return rx_remain;
}

#define BYTES_PER_DOT	0x20000
static void rx_handler_dl_image(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	unsigned int transfer_size = download_size - download_bytes;
	const unsigned char *buffer = req->buf;
	unsigned int buffer_size = req->actual;
	unsigned int pre_dot_num, now_dot_num;

	if (req->status != 0) {
		printf("Bad status: %d\n", req->status);
		return;
	}

	if (buffer_size < transfer_size)
		transfer_size = buffer_size;

	memcpy((void *)CONFIG_USB_FASTBOOT_BUF_ADDR + download_bytes,
	       buffer, transfer_size);

	pre_dot_num = download_bytes / BYTES_PER_DOT;
	download_bytes += transfer_size;
	now_dot_num = download_bytes / BYTES_PER_DOT;

	if (pre_dot_num != now_dot_num) {
		putc('.');
		if (!(now_dot_num % 74))
			putc('\n');
	}

	/* Check if transfer is done */
	if (download_bytes >= download_size) {
		/*
		 * Reset global transfer variable, keep download_bytes because
		 * it will be used in the next possible flashing command
		 */
		download_size = 0;
		req->complete = rx_handler_command;
		req->length = EP_BUFFER_SIZE;

		sprintf(response, "OKAY");
		fastboot_tx_write_str(response);

		printf("\ndownloading of %d bytes finished\n", download_bytes);
	} else {
		req->length = rx_bytes_expected();
		if (req->length < ep->maxpacket)
			req->length = ep->maxpacket;
	}

	req->actual = 0;
	usb_ep_queue(ep, req, 0);
}

static void cb_download(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[RESPONSE_LEN];

	printf("cmd cb_download is %s\n", cmd);

	strsep(&cmd, ":");
	download_size = simple_strtoul(cmd, NULL, 16);
	download_bytes = 0;

	printf("Starting download of %d bytes\n", download_size);

	if (0 == download_size) {
		sprintf(response, "FAILdata invalid size");
	} else if (download_size > ddr_size_usable(CONFIG_USB_FASTBOOT_BUF_ADDR)) {
		download_size = 0;
		sprintf(response, "FAILdata too large");
	} else {
		sprintf(response, "DATA%08x", download_size);
		req->complete = rx_handler_dl_image;
		req->length = rx_bytes_expected();
		if (req->length < ep->maxpacket)
			req->length = ep->maxpacket;
	}
	fastboot_tx_write_str(response);
}

typedef struct andr_img_hdr boot_img_hdr;

static void do_bootm_on_complete(struct usb_ep *ep, struct usb_request *req)
{
	char boot_addr_start[12];
	unsigned    kernel_size;
	unsigned    ramdisk_size;
	boot_img_hdr *hdr_addr = NULL;
	int genFmt = 0;
	unsigned actualBootImgSz = 0;
	unsigned dtbSz = 0;
	unsigned char* loadaddr = 0;

	puts("Booting kernel...\n");

	sprintf(boot_addr_start, "bootm 0x%lx", load_addr);
	printf("boot_addr_start %s\n", boot_addr_start);

	loadaddr = (unsigned char*)CONFIG_USB_FASTBOOT_BUF_ADDR;
	hdr_addr = (boot_img_hdr*)loadaddr;

	genFmt = genimg_get_format(hdr_addr);
	if (IMAGE_FORMAT_ANDROID != genFmt) {
		printf("Fmt unsupported!genFmt 0x%x != 0x%x\n", genFmt, IMAGE_FORMAT_ANDROID);
		return;
	}

	kernel_size     =(hdr_addr->kernel_size + (hdr_addr->page_size-1)+hdr_addr->page_size)&(~(hdr_addr->page_size -1));
	ramdisk_size    =(hdr_addr->ramdisk_size + (hdr_addr->page_size-1))&(~(hdr_addr->page_size -1));
	dtbSz           = hdr_addr->second_size;
	actualBootImgSz = kernel_size + ramdisk_size + dtbSz;
	printf("kernel_size 0x%x, page_size 0x%x, totalSz 0x%x\n", hdr_addr->kernel_size, hdr_addr->page_size, kernel_size);
	printf("ramdisk_size 0x%x, totalSz 0x%x\n", hdr_addr->ramdisk_size, ramdisk_size);
	printf("dtbSz 0x%x, Total actualBootImgSz 0x%x\n", dtbSz, actualBootImgSz);

	memcpy((void *)load_addr, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR, actualBootImgSz);

	flush_cache(load_addr,(unsigned long)actualBootImgSz);

	run_command(boot_addr_start, 0);

	/* This only happens if image is somehow faulty so we start over */
	do_reset(NULL, 0, 0, NULL);
}

static void cb_boot(struct usb_ep *ep, struct usb_request *req)
{
	fastboot_func->in_req->complete = do_bootm_on_complete;
	fastboot_tx_write_str("OKAY");
}

static void do_exit_on_complete(struct usb_ep *ep, struct usb_request *req)
{
	puts("Booting kernel..\n");
	run_command("run storeboot", 0);

	/* This only happens if image is somehow faulty so we start over */
	do_reset(NULL, 0, 0, NULL);
}


static void cb_continue(struct usb_ep *ep, struct usb_request *req)
{
	fastboot_func->in_req->complete = do_exit_on_complete;
	fastboot_tx_write_str("OKAY");
}

#ifdef CONFIG_FASTBOOT_FLASH
static void cb_flash(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	char response[RESPONSE_LEN];

	printf("cmd cb_flash is %s\n", cmd);

	strsep(&cmd, ":");
	if (!cmd) {
		error("missing partition name\n");
		fastboot_tx_write_str("FAILmissing partition name");
		return;
	}

	printf("partition is %s\n", cmd);
	if (strcmp(cmd, "userdata") == 0) {
		strcpy(cmd, "data");
		printf("partition is %s\n", cmd);
	}

	response_str = response;
	//strcpy(response, "FAILno flash device defined");
	if (is_mainstorage_emmc()) {
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
		fb_mmc_flash_write(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR,
				   download_bytes);
#endif
	} else if (is_mainstorage_nand()) {
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
		fb_nand_flash_write(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR,
					download_bytes);
#else
		fastboot_fail("not support nftl\n");
#endif
	} else {
		printf("error: no valid fastboot device\n");
		fastboot_fail("no vaild device\n");
	}
	fastboot_tx_write_str(response);
}
#endif

static void cb_set_active(struct usb_ep *ep, struct usb_request *req)
{
	char *cmd = req->buf;
	//char response[RESPONSE_LEN];
	int ret = 0;
	char str[128];

	printf("cmd cb_set_active is %s\n", cmd);
	strsep(&cmd, ":");
	if (!cmd) {
		error("missing slot name\n");
		fastboot_tx_write_str("FAILmissing slot name");
		return;
	}

	sprintf(str, "set_active_slot %s", cmd);
	printf("command:    %s\n", str);
	ret = run_command(str, 0);
	printf("ret = %d\n", ret);
	if (ret == 0)
		fastboot_tx_write_str("OKAY");
	else
		fastboot_tx_write_str("FAILset slot error");
}

static void cb_flashall(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	char *cmd = req->buf;

	printf("cmd cb_flashall is %s\n", cmd);

	response_str = response;
	//strcpy(response, "FAILno flash device defined");
	if (is_mainstorage_emmc()) {
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
		fb_mmc_flash_write(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR,
				   download_bytes);
#endif
	} else if (is_mainstorage_nand()) {
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
		fb_nand_flash_write(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR,
					download_bytes);
#else
		fastboot_fail("not support nftl\n");
#endif
	} else {
		printf("error: no valid fastboot device\n");
		fastboot_fail("no vaild device\n");
	}
	fastboot_tx_write_str(response);
}

static void cb_erase(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	char *cmd = req->buf;

	printf("cmd cb_erase is %s\n", cmd);

	strsep(&cmd, ":");
	if (!cmd) {
		error("missing partition name\n");
		fastboot_tx_write_str("FAILmissing partition name");
		return;
	}

	printf("partition is %s\n", cmd);
	if (strcmp(cmd, "userdata") == 0) {
		strcpy(cmd, "data");
		printf("partition is %s\n", cmd);
	}

	response_str = response;

	//strcpy(response, "FAILno erase device defined");
	if (is_mainstorage_emmc()) {
#ifdef CONFIG_FASTBOOT_FLASH_MMC_DEV
		fb_mmc_erase_write(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR);
#endif
	} else if (is_mainstorage_nand()) {
#ifdef CONFIG_FASTBOOT_FLASH_NAND_DEV
		fb_nand_erase(cmd, (void *)CONFIG_USB_FASTBOOT_BUF_ADDR);
#else
		fastboot_fail("not support nftl\n");
#endif
	} else {
		printf("error: no valid fastboot device\n");
		fastboot_fail("no vaild device\n");
	}
	fastboot_tx_write_str(response);
}

static void cb_devices(struct usb_ep *ep, struct usb_request *req)
{
	char response[RESPONSE_LEN];
	char *cmd = req->buf;

	printf("cmd is %s\n", cmd);

	strcpy(response, "AMLOGIC");

	fastboot_tx_write_str(response);
}

struct cmd_dispatch_info {
	char *cmd;
	void (*cb)(struct usb_ep *ep, struct usb_request *req);
};

static const struct cmd_dispatch_info cmd_dispatch_info[] = {
	{
		.cmd = "reboot",
		.cb = cb_reboot,
	}, {
		.cmd = "getvar:",
		.cb = cb_getvar,
	}, {
		.cmd = "download:",
		.cb = cb_download,
	}, {
		.cmd = "boot",
		.cb = cb_boot,
	}, {
		.cmd = "continue",
		.cb = cb_continue,
	},
#ifdef CONFIG_FASTBOOT_FLASH
	{
		.cmd = "flash",
		.cb = cb_flash,
	},
#endif
	{
		.cmd = "update",
		.cb = cb_download,
	},
	{
		.cmd = "flashall",
		.cb = cb_flashall,
	},
	{
		.cmd = "erase",
		.cb = cb_erase,
	},
	{
		.cmd = "devices",
		.cb = cb_devices,
	},
	{
		.cmd = "reboot-bootloader",
		.cb = cb_reboot,
	},
	{
		.cmd = "set_active",
		.cb = cb_set_active,
	},
};

static void rx_handler_command(struct usb_ep *ep, struct usb_request *req)
{
	char *cmdbuf = req->buf;
	void (*func_cb)(struct usb_ep *ep, struct usb_request *req) = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_dispatch_info); i++) {
		if (!strcmp_l1(cmd_dispatch_info[i].cmd, cmdbuf)) {
			func_cb = cmd_dispatch_info[i].cb;
			break;
		}
	}

	if (!func_cb) {
		error("unknown command: %s\n", cmdbuf);
		fastboot_tx_write_str("FAILunknown command");
	} else {
		if (req->actual < req->length) {
			u8 *buf = (u8 *)req->buf;
			buf[req->actual] = 0;
			func_cb(ep, req);
		} else {
			error("buffer overflow\n");
			fastboot_tx_write_str("FAILbuffer overflow");
		}
	}

	if (req->status == 0) {
		*cmdbuf = '\0';
		req->actual = 0;
		usb_ep_queue(ep, req, 0);
	}
}
