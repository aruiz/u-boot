// SPDX-License-Identifier: GPL-2.0+
/*
 * VESA video driver for BIOS-based PCs
 *
 * Sets a VESA graphics mode via real-mode INT 10h VBE calls.
 * This is a device-tree driven driver that reserves framebuffer
 * memory at bind time, avoiding allocation failures that occur
 * when PCI video devices are discovered after relocation.
 *
 * The linear framebuffer is registered with the VIDEO uclass,
 * which creates a vidconsole and exposes EFI GOP.
 */

#include <dm.h>
#include <log.h>
#include <pci.h>
#include <string.h>
#include <vesa.h>
#include <video.h>
#include <linux/linkage.h>
#include <asm/mtrr.h>

extern asmlinkage void (*realmode_interrupt)(u32 intno, u32 eax, u32 ebx,
					     u32 ecx, u32 edx, u32 esi,
					     u32 edi);
extern unsigned char asm_realmode_code;
extern unsigned int asm_realmode_code_size;
extern unsigned char asm_realmode_call;
extern unsigned char __realmode_interrupt;
extern unsigned char asm_realmode_buffer;

#define REALMODE_BASE	0x600
#define PTR_TO_REAL_MODE(sym) \
	(void *)(REALMODE_BASE + ((char *)&(sym) - (char *)&asm_realmode_code))

extern asmlinkage void (*realmode_call)(u32 addr, u32 eax, u32 ebx,
					u32 ecx, u32 edx, u32 esi, u32 edi);

static void setup_realmode_trampoline(void)
{
	memcpy((void *)REALMODE_BASE, &asm_realmode_code,
	       asm_realmode_code_size);
	realmode_call = PTR_TO_REAL_MODE(asm_realmode_call);
	realmode_interrupt = PTR_TO_REAL_MODE(__realmode_interrupt);
}

static int vesa_bios_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct vesa_mode_info info;
	int vesa_mode = CONFIG_FRAMEBUFFER_VESA_MODE;
	u16 buffer_seg, buffer_adr;
	ulong fbbase;
	char *buffer;
	int ret;

	setup_realmode_trampoline();

	/* VBE Get Mode Info (INT 10h AX=4F01h) */
	buffer = PTR_TO_REAL_MODE(asm_realmode_buffer);
	buffer_seg = (((unsigned long)buffer) >> 4) & 0xff00;
	buffer_adr = ((unsigned long)buffer) & 0xffff;
	realmode_interrupt(0x10, VESA_GET_MODE_INFO, 0x0000,
			   vesa_mode, 0x0000, buffer_seg, buffer_adr);
	memcpy(&info, buffer, sizeof(info));

	if (!info.phys_base_ptr) {
		log_debug("VBE: mode %#x not available\n", vesa_mode);
		return -ENODEV;
	}

	/* VBE Set Mode (INT 10h AX=4F02h), bit 14 = linear framebuffer */
	realmode_interrupt(0x10, VESA_SET_MODE, vesa_mode | (1 << 14),
			   0x0000, 0x0000, 0x0000, 0x0000);

	ret = vesa_setup_video_priv(&info, info.phys_base_ptr, uc_priv, plat);
	if (ret)
		return ret;

	fbbase = IS_ENABLED(CONFIG_VIDEO_COPY) ? plat->copy_base : plat->base;
	if (fbbase)
		mtrr_set_next_var(MTRR_TYPE_WRCOMB, fbbase, 256 << 20);

	printf("Video: %dx%dx%d\n", uc_priv->xsize, uc_priv->ysize,
	       info.bits_per_pixel);

	return 0;
}

static int vesa_bios_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = 1024 * 768 * 4;
	return 0;
}

static const struct udevice_id vesa_bios_ids[] = {
	{ .compatible = "vesa-bios" },
	{ }
};

U_BOOT_DRIVER(vesa_bios) = {
	.name	= "vesa_bios",
	.id	= UCLASS_VIDEO,
	.of_match = vesa_bios_ids,
	.bind	= vesa_bios_bind,
	.probe	= vesa_bios_probe,
};
