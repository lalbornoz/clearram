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
	"\taddq		$0x10,	%rsp\n"
	"\tandq		$-0x10,	%rsp\n"
	"\tcallq	cr_clear_cpu_exception\n"
	"\t1:		hlt\n"
	"\t		jmp	1b\n");

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
		cs_offset = cr_host_state.clear_exc_handlers_base + (nidte + 0x10);
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
	} else
	if (map_direct && (level == 2)) {
		pe_2M = (struct cra_page_ent_2M *)pe;
		pe_2M->pfn_base = pfn_base;
	} else {
		pe->pfn_base = pfn_base;
	}
}

/**
 * XXX
 */

int cr_amd64_map_cmp_desc(struct crh_litem *left, struct crh_litem *right)
{
	struct cra_page_tbl_desc *item_left, *item_right;
	item_left = (struct cra_page_tbl_desc *)left->item;
	item_right = (struct cra_page_tbl_desc *)right->item;
	if (item_left->va_hi <= item_right->va_hi) {
		return -1;
	} else
	if (item_left->va_hi > item_right->va_hi) {
		return 1;
	} else {
		return -1;
	}
}

/**
 * cr_amd64_map_pages_aligned() functions
 *
 * Return: 0 on success, <0 otherwise
 */
static int crp_amd64_link_table(struct cra_page_ent *pml4, uintptr_t va, enum cra_pe_bits extra_bits, int pages_nx, int level, int map_direct, struct cra_page_ent *pe, struct cra_page_ent **ppt_next, struct cra_page_ent *(*alloc_pt)(int, uintptr_t)) {
	uintptr_t pt_next_pfn;
	if (!(*ppt_next = alloc_pt(level - 1, va))) {
		return -ENOMEM;
	} else {
		pt_next_pfn = cr_host_virt_to_phys((uintptr_t)*ppt_next);
		cr_amd64_init_page_ent(pe, pt_next_pfn,
			extra_bits, pages_nx, level, map_direct);
	}
	return 0;
}
static int crp_amd64_get_table(struct cra_page_ent *pml4, int level, int map_direct, struct cra_page_ent *pe, struct cra_page_ent **ppt_next, int (*xlate_pfn)(uintptr_t, uintptr_t *)) {
	uintptr_t pt_next_pfn;
	if (map_direct && (level == CRA_LVL_PDP)) {
		pt_next_pfn = ((struct cra_page_ent_1G *)pe)->pfn_base;
	} else
	if (map_direct && (level == CRA_LVL_PD)) {
		pt_next_pfn = ((struct cra_page_ent_2M *)pe)->pfn_base;
	} else {
		pt_next_pfn = pe->pfn_base;
	}
	return xlate_pfn(pt_next_pfn, (uintptr_t *)ppt_next);
}
static void crp_amd64_fill_table(uintptr_t *va_base, uintptr_t *ppfn_cur, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, size_t page_size, int level, int map_direct, struct cra_page_ent *pt_cur, uintptr_t *ppt_idx) {
	while (*ppfn_cur < pfn_limit) {
		cr_amd64_init_page_ent(&pt_cur[*ppt_idx], *ppfn_cur,
			extra_bits | (map_direct ? CRA_PE_PAGE_SIZE : 0),
			pages_nx, level, map_direct);
		*ppfn_cur += page_size;
		*va_base = CRA_VA_INCR(*va_base, page_size * PAGE_SIZE);
		if (++(*ppt_idx) >= 512) {
			break;
		}
	}
}

/**
 * cr_amd64_map_pages_aligned() - create {1G,2M,4K} mappings from aligned VA to aligned PFN range in {PML4,PDP,PD,PT}
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry
 * @pages_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry
 * @page_size:	one of CRA_PS_{1G,2M,4K}
 *
 * Create {1G,2M,4K} mapping(s) for each PFN within pfn_base..pfn_limit
 * starting at va_base in pt_next using the supplied extra_bits, pages_nx
 * bit, and page_size. Lower-order page tables are created on demand.
 * Newly created {PDP,PD,PT} are allocated from the map heap in units of
 * the page size (0x1000) without blocking.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_amd64_map_pages_aligned(struct cra_page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, size_t page_size, struct cra_page_ent *(*alloc_pt)(int, uintptr_t), int (*xlate_pfn)(uintptr_t, uintptr_t *))
{
	int err, level, level_delta, map_direct;
	uintptr_t pt_idx, pfn_cur;
	struct cra_page_ent *pt_cur[CRA_LVL_PML4 + 1], *pt_next;

	CRH_VALID_PTR(pml4);
	CRH_VALID_PTR(va_base);
	CRH_VALID_BASE(pfn_base, page_size);
	CRH_VALID_BASE(pfn_limit, page_size);
	CRH_VALID_RANGE(CRA_PS_4K, CRA_PS_1G + 1, page_size);
	CRH_VALID_BASE(*va_base, (page_size * PAGE_SIZE));
	CRH_PRINTK_DEBUG("mapping 0x%016lx to 0x%013lx..0x%013lx (extra_bits=0x%04x, pages_nx=%u, page_size=%lu)",
		*va_base, pfn_base, pfn_limit, extra_bits, pages_nx, page_size);
	pfn_cur = pfn_base;
	pt_cur[CRA_LVL_PML4] = pml4;
	for (level = CRA_LVL_PML4, level_delta = 1;
			level >= CRA_LVL_PT;
			level -= level_delta, level_delta = 1) {
		switch (level) {
		case CRA_LVL_PT: map_direct = (page_size == CRA_SIZE_PTE); break;
		case CRA_LVL_PD: map_direct = (page_size == CRA_SIZE_PDE); break;
		case CRA_LVL_PDP: map_direct = (page_size == CRA_SIZE_PDPE); break;
		case CRA_LVL_PML4: map_direct = (0); break;
		}
		pt_idx = CRA_VA_TO_PE_IDX(*va_base, level);
		if (!map_direct) {
			if (!(pt_cur[level][pt_idx].bits & CRA_PE_PRESENT)) {
				err = crp_amd64_link_table(pml4, *va_base, extra_bits,
					pages_nx, level, map_direct, &pt_cur[level][pt_idx],
					&pt_next, alloc_pt);
			} else {
				err = crp_amd64_get_table(pml4, level, map_direct,
					&pt_cur[level][pt_idx], &pt_next, xlate_pfn);
			}
			if (err < 0) {
				return err;
			} else {
				pt_cur[level - 1] = pt_next;
			}
		} else {
			crp_amd64_fill_table(va_base, &pfn_cur, pfn_limit,
				extra_bits, pages_nx, page_size, level,
				map_direct, pt_cur[level], &pt_idx);
			if (pfn_cur < pfn_limit) {
				level_delta = -1;
			} else {
				break;
			}
		}
	}
	return 0;
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
