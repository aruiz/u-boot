/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PC (BIOS) platform definitions
 *
 * Standard PC CMOS/RTC locations for memory size (BIOS POST fills these).
 */

#ifndef _ARCH_BIOS_PC_H_
#define _ARCH_BIOS_PC_H_

/* CMOS (RTC) I/O ports */
#define CMOS_ADDR_PORT		0x70
#define CMOS_DATA_PORT		0x71

/* Extended memory size (above 1MB) in KB - bytes 0x34, 0x35 */
#define CMOS_LOW_RAM_ADDR	0x34
#define CMOS_HIGH_RAM_ADDR	0x35

/* Extended memory above 16MB - bytes 0x5b, 0x5c, 0x5d */
#define CMOS_LOW_HIGHRAM_ADDR	0x5b
#define CMOS_MID_HIGHRAM_ADDR	0x5c
#define CMOS_HIGH_HIGHRAM_ADDR	0x5d

#endif /* _ARCH_BIOS_PC_H_ */
