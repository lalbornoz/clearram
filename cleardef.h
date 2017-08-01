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

#ifndef _CLEARDEF_H_
#define _CLEARDEF_H_

/**
 * Exception CPU registers and register names
 * Contains Interrupt Stack Frame in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 8.9.3, pages 249-251.
 */
struct crc_cpu_regs {
	uintptr_t	cr0, cr2, cr3, cr4,				/* 000..020 */
			ds, es, fs, gs,					/* 020..040 */
			rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp,		/* 040..080 */
			r8, r9, r10, r11, r12, r13, r14, r15,		/* 080..100 */
			unused0,					/* 100..108 */
			vecno, errno,					/* 108..118 */
			orig_rip, orig_cs, orig_cflags, orig_ss,	/* 118..138 */
			unused1;					/* 138..140 */
} __attribute__((packed));

#define CRC_CPU_REGS_NAMES					\
	"CR0", "CR2", "CR3", "CR4",				\
	"DS", "ES", "FS", "GS",					\
	"RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",	\
	"R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",	\
	"",							\
	"VECNO", "ERRNO",					\
	"ORIG_RIP", "ORIG_CS", "ORIG_RFLAGS", "ORIG_SS",	\
	"",

/**
 * XXX
 */
void cr_clear_clear(void);
void cr_clear_cpu_dump_regs(struct crc_cpu_regs *cpu_regs);
void cr_clear_cpu_entry(void);
void cr_clear_cpu_exception(struct crc_cpu_regs *cpu_regs);
void cr_clear_cpu_setup(void (*fn)(void));
void cr_clear_vga_clear(void);
void cr_clear_vga_print_cstr(uintptr_t *pva_vga_cur, const char *str, unsigned char attr, size_t align);
void cr_clear_vga_print_reg(uintptr_t *pva_vga_cur, uintptr_t reg, unsigned char attr, size_t align);
void cr_clear_vga_reset(void);
#endif /* !_CLEARDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
