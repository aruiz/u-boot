/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PC (BIOS) - no CAR; loader provides stack.
 */

.section .text

.globl car_init
car_init:
	jmp	car_init_ret
