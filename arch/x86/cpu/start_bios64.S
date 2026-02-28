/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * 64-bit U-Boot entry for BIOS-loaded systems
 *
 * The BIOS (or SeaBIOS in QEMU) loads us in 32-bit protected mode.
 * We set up identity-mapped page tables, enable long mode, and jump
 * to 64-bit code that continues with the normal U-Boot init path.
 */

#include <config.h>
#include <asm/msr-index.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>

/*
 * Page-table area: 6 pages (24 KiB) at a fixed low-memory address.
 *   PML4  at PAGETABLE_BASE + 0x0000
 *   PDPT  at PAGETABLE_BASE + 0x1000
 *   PD[]  at PAGETABLE_BASE + 0x2000 .. + 0x5FFF
 * Identity-maps the first 4 GiB with 2 MiB pages.
 */
#define PAGETABLE_BASE	0x80000
#define PAGETABLE_SIZE	(6 * 4096)

#define PTE_P		0x01	/* Present */
#define PTE_RW		0x02	/* Read/Write */
#define PTE_US		0x04	/* User */
#define PTE_A		0x20	/* Accessed */
#define PTE_D		0x40	/* Dirty */
#define PTE_PS		0x80	/* Page Size (2 MiB) */
#define PTE_DIR		(PTE_P | PTE_RW | PTE_US | PTE_A)
#define PTE_PAGE	(PTE_DIR | PTE_D | PTE_PS)

#define SERIAL_PORT	0x3f8

.macro	serial_char c
	movb	$\c, %al
	movw	$SERIAL_PORT, %dx
	outb	%al, %dx
.endm

.section .text.start
.code32
.globl _start
.type _start, @function
.globl _x86boot_start
_x86boot_start:
	cli
	cld

	/* Turn off cache (to be safe) */
	movl	%cr0, %eax
	orl	$(X86_CR0_NW | X86_CR0_CD), %eax
	movl	%eax, %cr0
	wbinvd

	movl	$0, %ebp		/* BIST = 0 */
	jmp	1f

	.align	4
	.long	0x12345678
_start:
	movl	%eax, %ebp		/* save BIST */
1:
	serial_char 'A'

	/* Load our GDT that has 32-bit, 16-bit, and 64-bit segments */
	lgdt	gdt_ptr

	/* Load 32-bit data segments */
	movl	$(X86_GDT_ENTRY_32BIT_DS * X86_GDT_ENTRY_SIZE), %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	/* Reload CS via a far jump */
	ljmp	$(X86_GDT_ENTRY_32BIT_CS * X86_GDT_ENTRY_SIZE), $2f
2:
	serial_char 'B'

	/* Clear IDT */
	lidt	blank_idt_ptr

	/*
	 * Keep the stack the BIOS/loader gave us.  SeaBIOS leaves
	 * ESP in a safe region well above the page-table area
	 * (0x80000-0x85FFF) and real-mode blob (0x600).
	 */

	/* ----- build identity-mapped page tables ----- */

	/* Zero 24 KiB */
	movl	$PAGETABLE_BASE, %edi
	xorl	%eax, %eax
	movl	$(PAGETABLE_SIZE / 4), %ecx
	rep stosl

	/* PML4[0] -> PDPT */
	movl	$PAGETABLE_BASE, %edi
	movl	$(PAGETABLE_BASE + 0x1000 + PTE_DIR), (%edi)

	/* PDPT[0..3] -> PD[0..3] */
	addl	$0x1000, %edi
	movl	$(PAGETABLE_BASE + 0x2000 + PTE_DIR), %eax
	movl	$4, %ecx
3:	movl	%eax, (%edi)
	addl	$0x1000, %eax
	addl	$8, %edi
	loop	3b

	/* PD: 2048 entries, each mapping 2 MiB */
	movl	$(PAGETABLE_BASE + 0x2000), %edi
	movl	$PTE_PAGE, %eax
	movl	$2048, %ecx
4:	movl	%eax, (%edi)
	addl	$0x200000, %eax
	addl	$8, %edi
	loop	4b

	serial_char 'C'

	/* ----- enter long mode ----- */

	/* Load page-table base into CR3 */
	movl	$PAGETABLE_BASE, %eax
	movl	%eax, %cr3

	/* Enable PAE */
	movl	%cr4, %eax
	orl	$X86_CR4_PAE, %eax
	movl	%eax, %cr4

	/* Set EFER.LME (Long Mode Enable) */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	serial_char 'D'

	/* Enable paging -> activates Long Mode */
	movl	%cr0, %eax
	orl	$X86_CR0_PG, %eax
	movl	%eax, %cr0

	serial_char 'E'

	/* Far jump to 64-bit code segment */
	ljmp	$(X86_GDT_ENTRY_64BIT_CS * X86_GDT_ENTRY_SIZE), $start64

/* ------------------------------------------------------------------ */
.code64
start64:
	/* Re-enable caches now that paging is on */
	movq	%cr0, %rax
	andq	$~(X86_CR0_NW | X86_CR0_CD), %rax
	movq	%rax, %cr0

	/*
	 * ESP is still valid (zero-extended to RSP by the mode switch).
	 * Use it as the initial 64-bit stack.
	 */

	/* Debug: 'F' on serial = reached 64-bit mode */
	movb	$'F', %al
	movw	$SERIAL_PORT, %dx
	outb	%al, %dx

	/* Standard 64-bit U-Boot init (mirrors start64.S) */
	mov	%rsp, %rdi
	call	board_init_f_alloc_reserve
	mov	%rax, %rsp
	mov	%rsp, %rdi
	call	board_init_f_init_reserve

#ifdef CONFIG_DEBUG_UART
	call	debug_uart_init
#endif

	xor	%rdi, %rdi
	call	board_init_f
	call	board_init_f_r

	/* should not return */
	jmp	.

.globl board_init_f_r_trampoline64
.type board_init_f_r_trampoline64, @function
board_init_f_r_trampoline64:
	/*
	 * SDRAM has been initialised, U-Boot code has been copied into
	 * RAM, BSS has been cleared and relocation adjustments have been
	 * made.  Jump into the relocated copy.
	 *
	 * %rsi = Address of top of new stack
	 * %rdi = New gd
	 */
	movq	%rsi, %rsp
	call	board_init_f_r

die:
	hlt
	jmp	die

/* ------------------------------------------------------------------ */
/* Multiboot header so that SeaBIOS (via QEMU -kernel) can load us    */
/* ------------------------------------------------------------------ */
	.code32
	.p2align	2
multiboot_header:
	/* magic */
	.long	0x1badb002
	/* flags */
	.long	(1 << 16)
	/* checksum */
	.long	-0x1BADB002 - (1 << 16)
	/* header addr */
	.long	multiboot_header - _x86boot_start + CONFIG_TEXT_BASE
	/* load addr */
	.long	CONFIG_TEXT_BASE
	/* load end addr */
	.long	0
	/* bss end addr */
	.long	0
	/* entry addr */
	.long	CONFIG_TEXT_BASE

/* ------------------------------------------------------------------ */
/* GDT with 32-bit, 16-bit, and 64-bit segments                      */
/* Matches the entry numbering in asm/processor.h so that the         */
/* real-mode trampoline (bios_asm.S) works unchanged.                 */
/* ------------------------------------------------------------------ */

/*
 * Weak stubs for symbols referenced by bios_asm.S macros.
 * In 64-bit mode the real-mode trampoline's enter_protected_mode macro
 * does "lidt idt_ptr" and __interrupt_handler_16bit calls interrupt_handler.
 * Interrupts are off during the trampoline so the IDT content is irrelevant;
 * the 16-bit interrupt handler is never invoked in the BIOS-disk path.
 */
	.weak	idt_ptr
	.weak	interrupt_handler
	.code32
idt_ptr:
	.word	0
	.long	0

interrupt_handler:
	ret

	.align	16
blank_idt_ptr:
	.word	0
	.long	0

gdt_ptr:
	.word	gdt_end - gdt_table - 1
	.long	gdt_table

	.align	16
.globl gdt_table
gdt_table:
	/* 0 (0x00) NULL */
	.quad	0x0000000000000000
	/* 1 (0x08) unused */
	.quad	0x0000000000000000
	/* 2 (0x10) 32-bit code  CS: base 0, limit 4G, DPL 0, G D */
	.quad	0x00CF9B000000FFFF
	/* 3 (0x18) 32-bit data  DS: base 0, limit 4G, DPL 0, G D */
	.quad	0x00CF93000000FFFF
	/* 4 (0x20) 32-bit FS    (same as DS) */
	.quad	0x00CF93000000FFFF
	/* 5 (0x28) 16-bit code  CS: base 0, limit 64K */
	.quad	0x00009B000000FFFF
	/* 6 (0x30) 16-bit data  DS: base 0, limit 64K */
	.quad	0x000093000000FFFF
	/* 7 (0x38) 16-bit flat code CS: base 0, limit 4G, G */
	.quad	0x008F9B000000FFFF
	/* 8 (0x40) 16-bit flat data DS: base 0, limit 4G, G */
	.quad	0x008F93000000FFFF
	/* 9 (0x48) 64-bit code  CS: L=1 D=0 */
	.quad	0x00AF9A000000FFFF
	/* 10-11 (0x50-0x58) 64-bit TSS (filled by interrupt_init) */
	.quad	0x0000000000000000
	.quad	0x0000000000000000
gdt_end:
