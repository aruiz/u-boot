// SPDX-License-Identifier: GPL-2.0+
/*
 * PC (BIOS) platform init
 *
 * U-Boot is entered in 32-bit protected mode after BIOS POST.
 * We do not reprogram chipset (no PAM, etc.); we only set up
 * CPU features, identity, MTRR, and let interrupt_init() set up
 * the 8259 PIC and IDT.
 */

#include <cpu_func.h>
#include <dm.h>
#include <init.h>
#include <asm/post.h>
#include <asm/u-boot-x86.h>

/* We run 32-bit init when loaded by BIOS (no X86_RESET_VECTOR, so no X86_32BIT_INIT) */
#if CONFIG_IS_ENABLED(BIOS_PC)
int arch_cpu_init(void)
{
	post_code(POST_CPU_INIT);

	return x86_cpu_init_f();
}

int checkcpu(void)
{
	return 0;
}
#endif

int arch_early_init_r(void)
{
	struct udevice *dev;

	/*
	 * Probe IDE/AHCI controllers now so their block devices exist
	 * before initr_env() tries to load the environment from FAT.
	 * PCI scan (SYS_EARLY_PCI_INIT) has already bound these devices;
	 * we just need to trigger the probe to create the block children.
	 */
	uclass_first_device(UCLASS_IDE, &dev);
	uclass_first_device(UCLASS_AHCI, &dev);

	return 0;
}
