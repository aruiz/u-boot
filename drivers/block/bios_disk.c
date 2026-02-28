// SPDX-License-Identifier: GPL-2.0+
/*
 * Block driver for x86 BIOS INT 13h disk access
 *
 * A single "bios-disks" device-tree node acts as a controller.  At
 * probe time it enumerates hard drives 0x80..0x87 via INT 13h AH=48h
 * and creates a child UCLASS_BLK device for each one found.  Reads
 * go through INT 13h AH=42h (extended read), one sector at a time,
 * bouncing through a 512-byte buffer in the real-mode area.
 */

#include <blk.h>
#include <bootdev.h>
#include <dm.h>
#include <log.h>
#include <part.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <dm/device_compat.h>

#define REALMODE_BASE		0x600
#define PTR_TO_REAL_MODE(sym)	\
	((void *)(REALMODE_BASE + ((char *)&(sym) - (char *)&asm_realmode_code)))

/*
 * INT 13h extended calls expect DS:SI to point to a packet.
 * prepare_for_irom (bios_asm.S) sets DS = 0x40 and we pass SI = 0,
 * so the packet must sit at physical address 0x40:0 = 0x400.
 */
#define INT13_PACKET_ADDR	0x400

#define BIOS_FIRST_HD		0x80
#define BIOS_MAX_HD		8

extern asmlinkage void (*realmode_interrupt)(u32 intno, u32 eax, u32 ebx,
					     u32 ecx, u32 edx, u32 esi,
					     u32 edi);
extern unsigned char asm_realmode_code, __realmode_interrupt;
#ifdef CONFIG_X86_64
extern unsigned char __realmode_interrupt_64;
#endif
extern unsigned int asm_realmode_code_size;
extern unsigned char asm_realmode_disk_buffer;

static void setup_realmode_trampoline(void)
{
	memcpy((void *)REALMODE_BASE, &asm_realmode_code,
	       asm_realmode_code_size);
#ifdef CONFIG_X86_64
	realmode_interrupt = PTR_TO_REAL_MODE(__realmode_interrupt_64);
#else
	realmode_interrupt = PTR_TO_REAL_MODE(__realmode_interrupt);
#endif
}

/* Per-block-device platform data: the INT 13h drive number.
 * Must be plat (not priv) so it is available before probe. */
struct bios_disk_plat {
	u8 drive;
};

/* INT 13h AH=48h result buffer */
struct int13_ext_params {
	uint16_t buf_size;
	uint16_t flags;
	uint32_t cylinders;
	uint32_t heads;
	uint32_t sectors_per_track;
	uint64_t total_sectors;
	uint32_t sector_size;
} __packed;

/* INT 13h AH=42h Disk Address Packet */
struct int13_dap {
	uint8_t  size;
	uint8_t  reserved;
	uint16_t block_count;
	uint16_t buf_offset;
	uint16_t buf_segment;
	uint64_t lba;
} __packed;

static int bios_disk_ext_read(u8 drive, lbaint_t start, lbaint_t blkcnt,
			      void *buffer)
{
	struct int13_dap *dap = (struct int13_dap *)INT13_PACKET_ADDR;
	void *rm_buf = PTR_TO_REAL_MODE(asm_realmode_disk_buffer);
	uint16_t buf_seg = ((unsigned long)rm_buf) >> 4;
	uint16_t buf_off = ((unsigned long)rm_buf) & 0xf;
	uint8_t *dst = buffer;
	lbaint_t i;

	setup_realmode_trampoline();

	for (i = 0; i < blkcnt; i++) {
		dap->size = sizeof(*dap);
		dap->reserved = 0;
		dap->block_count = 1;
		dap->buf_offset = buf_off;
		dap->buf_segment = buf_seg;
		dap->lba = start + i;

		/* AH=42h extended read, DL=drive, DS:SI=packet (0x40:0) */
		realmode_interrupt(0x13, 0x4200, 0, 0, drive, 0, 0);

		memcpy(dst, rm_buf, 512);
		dst += 512;
	}

	return blkcnt;
}

/* ------------------------------------------------------------------ */
/* UCLASS_BLK child driver                                            */
/* ------------------------------------------------------------------ */

static ulong bios_disk_blk_read(struct udevice *dev, lbaint_t start,
				lbaint_t blkcnt, void *buffer)
{
	struct bios_disk_plat *plat = dev_get_plat(dev);
	struct blk_desc *desc = dev_get_uclass_plat(dev);

	if (start + blkcnt > desc->lba)
		return 0;

	return bios_disk_ext_read(plat->drive, start, blkcnt, buffer);
}

static const struct blk_ops bios_disk_blk_ops = {
	.read	= bios_disk_blk_read,
};

U_BOOT_DRIVER(bios_disk_blk) = {
	.name		= "bios_disk_blk",
	.id		= UCLASS_BLK,
	.ops		= &bios_disk_blk_ops,
	.plat_auto	= sizeof(struct bios_disk_plat),
};

/* ------------------------------------------------------------------ */
/* UCLASS_BIOS_DISK controller – enumerates drives at probe time      */
/* ------------------------------------------------------------------ */

/**
 * bios_disk_get_params() - query drive geometry via INT 13h AH=48h
 *
 * @drive:	BIOS drive number (0x80+)
 * @lba:	returns total sector count
 * @blksz:	returns bytes per sector
 * Return: 0 on success, -ENODEV if the drive is absent or unusable
 */
static int bios_disk_get_params(u8 drive, lbaint_t *lba, uint32_t *blksz)
{
	struct int13_ext_params *p;

	p = (struct int13_ext_params *)INT13_PACKET_ADDR;
	memset(p, 0, sizeof(*p));
	p->buf_size = sizeof(*p);

	/* AH=48h get extended params, DL=drive, DS:SI=buffer (0x40:0) */
	realmode_interrupt(0x13, 0x4800, 0, 0, drive, 0, 0);

	*lba   = (lbaint_t)p->total_sectors;
	*blksz = p->sector_size ? p->sector_size : 512;

	if (*lba == 0)
		return -ENODEV;

	return 0;
}

static int bios_disks_probe(struct udevice *dev)
{
	int devnum = 0;
	u8 drive;

	setup_realmode_trampoline();

	for (drive = BIOS_FIRST_HD; drive < BIOS_FIRST_HD + BIOS_MAX_HD;
	     drive++) {
		struct bios_disk_plat *plat;
		struct udevice *blk;
		struct blk_desc *desc;
		lbaint_t lba;
		uint32_t blksz;
		int ret;

		ret = bios_disk_get_params(drive, &lba, &blksz);
		if (ret)
			continue;

		if (blksz != 512) {
			log_debug("drive 0x%02x: sector size %u not supported\n",
				  drive, blksz);
			continue;
		}

		ret = blk_create_devicef(dev, "bios_disk_blk", "blk",
					 UCLASS_BIOS_DISK, devnum,
					 blksz, lba, &blk);
		if (ret) {
			log_debug("blk_create_devicef failed for 0x%02x: %d\n",
				  drive, ret);
			continue;
		}

		plat = dev_get_plat(blk);
		plat->drive = drive;

		desc = dev_get_uclass_plat(blk);
		desc->removable = 0;
		desc->type = DEV_TYPE_HARDDISK;
		snprintf(desc->vendor, BLK_VEN_SIZE, "BIOS");
		snprintf(desc->product, BLK_PRD_SIZE, "HD%d", devnum);
		snprintf(desc->revision, BLK_REV_SIZE, "1.0");

		ret = blk_probe_or_unbind(blk);
		if (ret) {
			log_debug("probe failed drive 0x%02x: %d\n",
				  drive, ret);
			continue;
		}

		bootdev_setup_for_sibling_blk(blk, "bios_bootdev");

		log_info("BIOS disk 0x%02x: %lu sectors (%lu MiB)\n",
			 drive, (ulong)lba, (ulong)(lba >> 11));
		devnum++;
	}

	if (!devnum) {
		log_debug("no BIOS drives found\n");
		return -ENODEV;
	}

	return 0;
}

static const struct udevice_id bios_disks_ids[] = {
	{ .compatible = "bios-disks" },
	{ }
};

U_BOOT_DRIVER(bios_disks) = {
	.name		= "bios_disks",
	.id		= UCLASS_BIOS_DISK,
	.of_match	= bios_disks_ids,
	.probe		= bios_disks_probe,
};

/* ------------------------------------------------------------------ */
/* Bootdev – lets the standard-boot framework discover BIOS disks     */
/* ------------------------------------------------------------------ */

static const struct bootdev_ops bios_bootdev_ops = {
};

U_BOOT_DRIVER(bios_bootdev) = {
	.name		= "bios_bootdev",
	.id		= UCLASS_BOOTDEV,
	.ops		= &bios_bootdev_ops,
};

/* ------------------------------------------------------------------ */
/* UCLASS_BIOS_DISK uclass driver                                    */
/* ------------------------------------------------------------------ */

UCLASS_DRIVER(bios_disk) = {
	.id		= UCLASS_BIOS_DISK,
	.name		= "bios_disk",
};
