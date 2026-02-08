// SPDX-License-Identifier: GPL-2.0+
/*
 * PC (BIOS) RAM size from CMOS
 *
 * BIOS POST stores extended memory size in standard CMOS locations.
 * Low memory: 0x34-0x35 (extended memory in KB, same encoding as QEMU/SeaBIOS).
 * High memory (above 16MB): 0x5b, 0x5c, 0x5d.
 */

#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/bios_pc.h>
#include <asm/post.h>
#include <linux/sizes.h>
#include <linux/types.h>

DECLARE_GLOBAL_DATA_PTR;

static u32 bios_pc_get_low_memory_size(void)
{
	u32 ram;

	outb(CMOS_HIGH_RAM_ADDR, CMOS_ADDR_PORT);
	ram = ((u32)inb(CMOS_DATA_PORT)) << 14;
	outb(CMOS_LOW_RAM_ADDR, CMOS_ADDR_PORT);
	ram |= ((u32)inb(CMOS_DATA_PORT)) << 6;
	ram += 16 * 1024;

	return ram * 1024;
}

static u64 bios_pc_get_high_memory_size(void)
{
	u64 ram;

	outb(CMOS_HIGH_HIGHRAM_ADDR, CMOS_ADDR_PORT);
	ram = ((u64)inb(CMOS_DATA_PORT)) << 22;
	outb(CMOS_MID_HIGHRAM_ADDR, CMOS_ADDR_PORT);
	ram |= ((u64)inb(CMOS_DATA_PORT)) << 14;
	outb(CMOS_LOW_HIGHRAM_ADDR, CMOS_ADDR_PORT);
	ram |= ((u64)inb(CMOS_DATA_PORT)) << 6;

	return ram * 1024;
}

int dram_init(void)
{
	gd->ram_size = bios_pc_get_low_memory_size();
	gd->ram_size += bios_pc_get_high_memory_size();

	/* Sanity: if CMOS is empty/zero, use a minimal default */
	if (gd->ram_size < SZ_16M)
		gd->ram_size = SZ_128M;

	post_code(POST_DRAM);

	return 0;
}

int dram_init_banksize(void)
{
	u64 high_mem_size;

	gd->bd->bi_dram[0].start = 0;
	gd->bd->bi_dram[0].size = bios_pc_get_low_memory_size();

	high_mem_size = bios_pc_get_high_memory_size();
	if (high_mem_size) {
		gd->bd->bi_dram[1].start = SZ_4G;
		gd->bd->bi_dram[1].size = high_mem_size;
	}

	return 0;
}

phys_addr_t board_get_usable_ram_top(phys_size_t total_size)
{
	return bios_pc_get_low_memory_size();
}
