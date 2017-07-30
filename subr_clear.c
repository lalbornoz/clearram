/*
 * clearram -- clear system RAM and reboot on demand (for zubwolf)
 * Copyright (C) 2017 by Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "include/clearram.h"

/**
 * cr_clear() - setup CPU(s) and zero-fill RAM
 *
 * Disable preemption on the current CPU and stop all other
 * CPUs, if any, which may block briefly. Setup CR3 with our
 * PML4, flush the TLB, and zero-fill physical memory using
 * REP STOSQ. As the code pages are mapped at the top of VA,
 * this will eventually cause a trigger fault whilst still
 * zero-filling as much as possible.
 *
 * The caller _must_ ensure that cr_pml4 points to a valid
 * hierarchy of page tables, mapping cr_clear_limit - cr_clear
 * and physical RAM.
 *
 * If built w/ -DDEBUG, %rsp and the IDTR are initialised with
 * cr_debug_stack and debug_idtr, respectively, the framebuffer
 * offset if reset to 0 to permit text output, and zero-filling
 * is split up into blocks of 256 MB. After each successful 256
 * MB block zero-fill, a light green-on-black single period is
 * printed to the framebuffer. As exceptions will be handled by
 * subr_debug.c, control is handed off there implicitly once an
 * exception is generated as part of the (otherwise infinite)
 * zero-filling loop.
 *
 * The caller _must_ ensure that cr_debug_{idt,stack,va} point
 * to a valid IDT, stack page, and cr_debug_limit - cr_debug,
 * respectively.
 *
 * Return: Nothing
 */

void __attribute__((aligned(PAGE_SIZE))) cr_clear(void)
{
	struct cr3 cr3;
#if defined(DEBUG)
	struct idtr_bits debug_idtr = {0};
#endif /* defined(DEBUG) */

	cr_cpu_stop_all();
	CR_INIT_CR3(&cr3, cr_virt_to_phys((uintptr_t)cr_pml4), CR3_BIT_WRITE_THROUGH);
#if defined(DEBUG)
	CR_ASSERT_TRYADD((uintptr_t)cr_debug_idt, (uintptr_t)-1, PAGE_SIZE);
	CR_INIT_IDTR(&debug_idtr, cr_debug_idt + PAGE_SIZE, cr_debug_idt);
#endif /* defined(DEBUG) */
	__asm volatile(
		/*
		 * %rax:	cr3
		 * %rbx:	debug_idtr	(-DDEBUG)
		 * %rcx:	cr_debug_stack	(-DDEBUG)
		 * %rdx:	cr_debug_vga	(-DDEBUG)
		 * %r8:		%cr4
		 * %r9:		%cr4 &= ~(PGE bit)
		 */
		"\tcli\n"					/* Disable interrupts */
		"\tcld\n"					/* Clear direction flag */
		"\tmovq		%0,		%%rax\n"
#if defined(DEBUG)
		"\tmovq		%1,		%%rbx\n"
		"\tmovq		%2,		%%rcx\n"
		"\tmovq		%3,		%%rdx\n"
#endif /* defined(DEBUG) */
		"\tmovq		%%cr4,		%%r8\n"
		"\tmovq		%%r8,		%%r9\n"		/* Copy original CR4 value */
		"\tandb		$0x7f,		%%r9b\n"	/* Clear PGE bit */
		"\tmovq		%%r9,		%%cr4\n"	/* Disable PGE */
		"\tmovq		%%rax,		%%cr3\n"	/* Set CR3 */
		"\tmovq		%%r8,		%%cr4\n"	/* Enable PGE */
#if defined(DEBUG)
		"\n"
		/*
		 * %rbx:	debug_idtr
		 * %rcx:	cr_debug_stack
		 */
		"\tmovq		%%rcx,		%%rsp\n"	/* Set exception debugging stack page */
		"\tlidtq	(%%rbx)\n"			/* Load IDTR with exception debugging IDT */
		"\n"
		/*
		 * % al:	CRTC register input
		 * % dx:	CRTC register
		 */
		"\tmovq		%%rdx,		%%rbx\n"	/* Store %rdx */
		"\tmovb		$0x0c,		%%al\n"
		"\tmovw		$0x3d4,		%%dx\n"
		"\toutb		%%al,		%%dx\n"		/* Select CRTC register 12 */
		"\tmovb		$0x00,		%%al\n"
		"\tincw		%%dx\n"
		"\toutb		%%al,		%%dx\n"		/* Reset framebuffer offset LSB */
		"\tmovb		$0x0d,		%%al\n"
		"\tmovw		$0x3d4,		%%dx\n"
		"\toutb		%%al,		%%dx\n"		/* Select CRTC register 13 */
		"\tmovb		$0x00,		%%al\n"
		"\tincw		%%dx\n"
		"\toutb		%%al,		%%dx\n"		/* Reset framebuffer offset MSB */
		"\tmovq		%%rbx,		%%rdx\n"	/* Restore %rdx */
		"\n"
		/*
		 * %rbx:	cr_debug_vga
		 * %rdx:	cr_debug_vga mask
		 */
		"\tmovq		%%rdx,		%%rbx\n"	/* %rbx  = cr_debug_vga */
		"\torw		$0xfff,		%%dx\n"		/* %rdx |= 0xfff */
#endif /* defined(DEBUG) */
		"\n"
		/*
		 * %rbx:	Current framebuffer VA and offset
		 * % cx:	Framebuffer colour and char word
		 * %rdx:	cr_debug_vga mask
		 *
		 * %rax:	Store quadword
		 * %rcx:	Store count
		 * %rdi:	Store destination VA
		 */
		"\txorq		%%rax,		%%rax\n"	/* Store = 0x0ULL */
		"\txorq		%%rdi,		%%rdi\n"	/* Dest. = 0x0ULL */
#if defined(DEBUG)
		"1:\n"
		"\tmovq		$0x2000000,	%%rcx\n"	/* Count = 256 MB */
		"\trep		stosq\n"			/* Zero-fill */
		"\tmovw		$0x0a2e,	%%cx\n"		/* Print light green on black, single period */
		"\tmovw		%%cx,		(%%rbx)\n"	/* Print to framebuffer... */
		"\taddw		$2,		%%bx\n"		/* ...advance to next cell... */
		"\tandl		%%edx,		%%ebx\n"	/* ...and wrap to cr_debug_vga. */
		"\tjmp		1b\n"
#else
		"\txorq		%%rcx,		%%rcx\n"	/* Count = 0x0ULL */
		"\tdecq		%%rcx\n"			/* Count = 0x0ULL - 1 */
		"\trep		stosq\n"			/* Zero-fill & triple-fault */
#endif /* defined(DEBUG) */
		"\t		ud2\n"				/* Pretty please? */
		"\n"
		"\t.align	0x1000\n"
		"\t.global	cr_clear_limit\n"
		"\tcr_clear_limit:\n"
		"\t.quad	.\n"
#if defined(DEBUG)
		:: "r"(cr3), "r"(&debug_idtr), "r"(cr_debug_stack), "r"(cr_debug_vga)
#else
		:: "r"(cr3)
#endif /* defined(DEBUG) */
		: "rax", "rbx", "rcx", "rdx", "rdi", "r8", "r9", "flags", "memory");
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
