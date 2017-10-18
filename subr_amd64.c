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

#include "clearram.h"

/*
 * AMD64-specific logic
 */

/**
 * cr_amd64_cpuid_page_size_from_level() - get largest page size supported at a page table level
 *
 * Return: 262144, 512, or 1 if 1 GB, 2 MB, or only 4 KB are supported at level
 */

size_t cr_amd64_cpuid_page_size_from_level(int level)
{
	unsigned long eax, ebx, ecx;
	unsigned long features_basic, features_ext;

	eax = CRA_CPUID_FUNC_BASIC_FEATURES;
	__asm volatile(
		"\tcpuid\n"
		:"=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(features_basic)
		:"0"(eax));
	eax = CRA_CPUID_FUNC_EXT_FEATURES;
	__asm volatile(
		"\tcpuid\n"
		:"=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(features_ext)
		:"0"(eax));
	features_ext &= ~CRA_CPUID_FEAT_EXT_PDPE1G;
	features_basic &= ~CRA_CPUID_FEAT_BASIC_PSE;
	switch (level) {
	case 3:	if (features_ext & CRA_CPUID_FEAT_EXT_PDPE1G) {
			return CRA_PS_1G;
		}
	case 2:	if (features_basic & CRA_CPUID_FEAT_BASIC_PSE) {
			return CRA_PS_2M;
		}
	default:
	case 1:	return CRA_PS_4K;
	}
}

/**
 * cr_amd64_exception() - XXX
 *
 * Return: Nothing
 */

__asm(
	"\t.section	.text\n"
	"\t.global	cr_amd64_exception\n"
	"\tcr_amd64_exception:\n"
	"\tpushq	%r15\n"
	"\tpushq	%r14\n"
	"\tpushq	%r13\n"
	"\tpushq	%r12\n"
	"\tpushq	%r11\n"
	"\tpushq	%r10\n"
	"\tpushq	%r9\n"
	"\tpushq	%r8\n"
	"\tmovq		%rsp,	%r8\n"
	"\taddq		(16*8),	%r8\n"
	"\tpushq	%r8\n"
	"\tpushq	%rbp\n"
	"\tpushq	%rdi\n"
	"\tpushq	%rsi\n"
	"\tpushq	%rdx\n"
	"\tpushq	%rcx\n"
	"\tpushq	%rbx\n"
	"\tpushq	%rax\n"
	"\tsubq		$0x20,	%rsp\n"
	"\tmovw		%gs,	0x0(%rsp)\n"
	"\tmovw		%fs,	0x8(%rsp)\n"
	"\tmovw		%es,	0x10(%rsp)\n"
	"\tmovw		%ds,	0x18(%rsp)\n"
	"\tmovq		%cr4,	%rax\n"
	"\tpushq	%rax\n"
	"\tmovq		%cr3,	%rax\n"
	"\tpushq	%rax\n"
	"\tmovq		%cr2,	%rax\n"
	"\tpushq	%rax\n"
	"\tmovq		%cr0,	%rax\n"
	"\tpushq	%rax\n"
	"\tmovq		%rsp,	%rdi\n"
	"\tmovq		%rdi,	%rbx\n"
	"\tsubq		$0x10,	%rsp\n"
	"\tandq		$-0x10,	%rsp\n"
	"\tcallq	cr_clear_cpu_exception\n"
	"\ttest		%rax,	%rax\n"
	"\tjz		1f\n"
	"\tjns		2f\n"
	"\t3:\n"			/* unhandled exception */
#if defined(DEBUG)
	"\t	hlt\n"			/*  DEBUG: halt */
	"\t	jmp	3b\n"
#else
	"\t	ud2\n"			/* !DEBUG: triple fault */
#endif /* defined(DEBUG) */
	"\t1:\n"			/* skip instruction */
	"\t2:\n"			/* restart instruction */
	"\t	movq	%rbx,	%rsp\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%cr0\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%cr2\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%cr3\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%cr4\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%ds\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%es\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%fs\n"
	"\t	popq	%rax\n"
	"\t	movq	%rax,	%gs\n"
	"\t	popq	%rax\n"
	"\t	popq	%rbx\n"
	"\t	popq	%rcx\n"
	"\t	popq	%rdx\n"
	"\t	popq	%rsi\n"
	"\t	popq	%rdi\n"
	"\t	popq	%rbp\n"
	"\t	addq	$0x08,	%rsp\n"
	"\t	popq	%r8\n"
	"\t	popq	%r9\n"
	"\t	popq	%r10\n"
	"\t	popq	%r11\n"
	"\t	popq	%r12\n"
	"\t	popq	%r13\n"
	"\t	popq	%r14\n"
	"\t	popq	%r15\n"
	"\t	ret\n"
);

/**
 * cr_amd64_exception{00-12}() - exception vector 0x{00-12} wrappers
 *
 * Return: Nothing
 */

__asm(
CRA_DECL_EXC_HANDLER_NOCODE(0x00)
CRA_DECL_EXC_HANDLER_NOCODE(0x01)
CRA_DECL_EXC_HANDLER_NOCODE(0x02)
CRA_DECL_EXC_HANDLER_NOCODE(0x03)
CRA_DECL_EXC_HANDLER_NOCODE(0x04)
CRA_DECL_EXC_HANDLER_NOCODE(0x05)
CRA_DECL_EXC_HANDLER_NOCODE(0x06)
CRA_DECL_EXC_HANDLER_NOCODE(0x07)
CRA_DECL_EXC_HANDLER(0x08)
CRA_DECL_EXC_HANDLER_NOCODE(0x09)
CRA_DECL_EXC_HANDLER(0x0a)
CRA_DECL_EXC_HANDLER(0x0b)
CRA_DECL_EXC_HANDLER(0x0c)
CRA_DECL_EXC_HANDLER(0x0d)
CRA_DECL_EXC_HANDLER(0x0e)
CRA_DECL_EXC_HANDLER_NOCODE(0x0f)
CRA_DECL_EXC_HANDLER_NOCODE(0x10)
CRA_DECL_EXC_HANDLER(0x11)
CRA_DECL_EXC_HANDLER_NOCODE(0x12)
);

/**
 * XXX
 */

unsigned char cr_amd64_inb(unsigned short port)
{
	unsigned short byte;

	__asm volatile(
		"\tmovw		%[port],	%%dx\n"
		"\tinb		%%dx\n"
		:"=a"(byte) : [port] "r"(port) : "dx");
	return byte;
}

/**
 * XXX
 */

int cr_amd64_init_gdt(struct cr_host_state *state)
{
	struct cra_gdt_ent *gdt;
	gdt = state->clear_gdt;
	memset(gdt, 0, PAGE_SIZE);
	CRA_INIT_GDTE_CODE(&gdt[0], 0x0ULL,
		CRA_GDTE_ATTR_DPL00	|
		CRA_GDTE_ATTR_PRESENT, 0);
	CRA_INIT_GDTE_CODE(&gdt[2], 0x0ULL,
		CRA_GDTE_ATTR_DPL00	|
		CRA_GDTE_ATTR_PRESENT, 0);
	return 0;
}

/**
 * XXX
 */

CRA_DEF_EXC_HANDLER(0x00);
int cr_amd64_init_idt(struct cr_host_state *state)
{
	uintptr_t cs_sel, cs_offset;
	size_t nidte;
	struct cra_idt_ent *idt;
	idt = state->clear_idt;
	memset(idt, 0, PAGE_SIZE);
	cs_sel = 0x10;
	cr_host_state.clear_exc_handlers_base = (uintptr_t)&cr_amd64_exception_0x00;
	for (nidte = 0; nidte <= 0x12; nidte++) {
		cs_offset = cr_host_state.clear_exc_handlers_base + (nidte * 0x10);
		CRA_INIT_IDTE(&idt[nidte],
			cs_offset, cs_sel, 0,
			CRA_IDTE_ATTR_TRAP_GATE	|
			CRA_IDTE_ATTR_DPL00	|
			CRA_IDTE_ATTR_PRESENT);
	}
	return 0;
}

/**
 * cr_amd64_init_page_ent() - initialise a single {PML4,PDP,PD,PT} entry
 *
 * Return: Nothing.
 */

void cr_amd64_init_page_ent(struct cra_page_ent *pe, uintptr_t pfn_base, enum cra_pe_bits extra_bits, int pages_nx, int level, int map_direct)
{
	struct cra_page_ent_1G *pe_1G;
	struct cra_page_ent_2M *pe_2M;

	memset(pe, 0, sizeof(*pe));
	pe->bits = CRA_PE_PRESENT | extra_bits;
	pe->nx = pages_nx;
	if (map_direct && (level == 3)) {
		pe_1G = (struct cra_page_ent_1G *)pe;
		pe_1G->pfn_base = pfn_base;
		pe_1G->bits |= CRA_PE_PAGE_SIZE;
	} else
	if (map_direct && (level == 2)) {
		pe_2M = (struct cra_page_ent_2M *)pe;
		pe_2M->pfn_base = pfn_base;
		pe_2M->bits |= CRA_PE_PAGE_SIZE;
	} else {
		pe->pfn_base = pfn_base;
	}
}

/**
 * XXX
 */

void cr_amd64_msleep(unsigned ms)
{
	unsigned long ns;
	unsigned short lo, hi;
	unsigned int count[2], delta, niter;

	ns = ms * 1000 * 1000;
	do {
		cr_amd64_outb(0x43, 0x30);
		cr_amd64_outb(0x40, 0x00);
		cr_amd64_outb(0x40, 0x00);
		count[0] = count[1] = 0x10000;
		for (niter = 0; niter < 8192; niter += delta) {
			count[0] = count[1];
			cr_amd64_outb(0x43, 0x00);	/* Latch count value */
			lo = cr_amd64_inb(0x40);
			hi = cr_amd64_inb(0x40);
			count[1] = ((hi << 8) | lo);
			if ((delta = (count[0] - count[1]))) {
				if (ns >= 838) {
					ns -= 838;
				}
				if (ns < 838) {
					break;
				}
			}
		}
		(void)ns;
	} while (ns >= 838);
}

/**
 * XXX
 */

void cr_amd64_outb(unsigned short port, unsigned char byte)
{
	__asm volatile(
		"\tmovb		%[byte],	%%al\n"
		"\tmovw		%[port],	%%dx\n"
		"\toutb		%%al,		%%dx\n"
		:: [port] "r"(port), [byte] "r"(byte)
		:  "al", "dx");
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
