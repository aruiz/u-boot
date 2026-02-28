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
#include <dm/uclass.h>
#include <init.h>
#include <log.h>
#include <acpi/acpi_table.h>
#include <smbios.h>
#include <asm/global_data.h>
#include <asm/post.h>
#include <asm/tables.h>
#include <asm/u-boot-x86.h>

DECLARE_GLOBAL_DATA_PTR;

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

/**
 * bios_find_acpi_rsdp() - scan BIOS ROM area for ACPI RSDP
 *
 * Scans 0xF0000-0xFFFFF for the "RSD PTR " signature placed by
 * the BIOS firmware during POST.
 *
 * Return: physical address of RSDP, or 0 if not found
 */
static ulong bios_find_acpi_rsdp(void)
{
	const char *p;

	for (p = (const char *)ROM_TABLE_ADDR;
	     p < (const char *)(ROM_TABLE_ADDR + CONFIG_ROM_TABLE_SIZE);
	     p += 16) {
		if (strncmp(p, RSDP_SIG, sizeof(RSDP_SIG) - 1) != 0)
			continue;
		if (table_compute_checksum((void *)p, 20) != 0)
			continue;
		return (ulong)p;
	}
	return 0;
}

/**
 * bios_find_smbios() - scan BIOS ROM area for SMBIOS entry point
 *
 * Scans 0xF0000-0xFFFFF for "_SM3_" (SMBIOS 3.x 64-bit) or
 * "_SM_" (SMBIOS 2.x) signature placed by the BIOS firmware.
 * Prefers 3.x if both are present.
 *
 * Return: physical address of SMBIOS entry, or 0 if not found
 */
static ulong bios_find_smbios(void)
{
	ulong addr2 = 0;
	const char *p;

	for (p = (const char *)ROM_TABLE_ADDR;
	     p < (const char *)(ROM_TABLE_ADDR + CONFIG_ROM_TABLE_SIZE);
	     p += 16) {
		/* Prefer SMBIOS 3.x (64-bit) entry */
		if (!strncmp(p, "_SM3_", 5))
			return (ulong)p;
		/* Remember SMBIOS 2.x entry as fallback */
		if (!addr2 && !strncmp(p, "_SM_", 4))
			addr2 = (ulong)p;
	}
	return addr2;
}

int arch_early_init_r(void)
{
	ulong addr;

	/*
	 * Scan the BIOS ROM area (0xF0000-0xFFFFF) for ACPI and SMBIOS
	 * tables placed by the firmware during POST. Register them so
	 * the EFI subsystem can expose them as configuration tables.
	 *
	 * This runs before cpu_init_r() -> write_tables(), so any
	 * BIOS-provided tables are discovered first.
	 *
	 * When no BIOS has run (e.g. QEMU -kernel mode), these scans
	 * find nothing and the fields stay 0. The EFI tolerance fixes
	 * in efi_acpi_register() and efi_smbios_register() handle
	 * that case gracefully.
	 */
	addr = bios_find_acpi_rsdp();
	if (addr) {
		log_info("BIOS ACPI RSDP found at %lx\n", addr);
		gd_set_acpi_start(addr);
	}

	addr = bios_find_smbios();
	if (addr) {
		log_info("BIOS SMBIOS found at %lx\n", addr);
		gd_set_smbios_start(addr);
	}

	/*
	 * Probe the BIOS disk controller so that INT 13h drives are
	 * enumerated early.  This creates block devices (and partitions)
	 * that the standard-boot framework and filesystem commands can use.
	 */
	if (IS_ENABLED(CONFIG_BIOS_DISK)) {
		struct udevice *dev;

		uclass_first_device(UCLASS_BIOS_DISK, &dev);
	}

	return 0;
}
