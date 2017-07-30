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

/*
 * Helper subroutines
 */

/**
 * cr_cpuid_page_size_from_level() - get largest page size supported at a page table level
 *
 * Return: 262144, 512, or 1 if 1 GB, 2 MB, or only 4 KB are supported at level
 */

size_t cr_cpuid_page_size_from_level(int level)
{
	unsigned long eax, ebx, ecx, edx;

	eax = CPUID_EAX_FUNC_FEATURES;
	__asm volatile(
		"\tcpuid\n"
		:"=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		:"0"(eax)
		:"memory");
	switch (level) {
	case 3:	if (edx & CPUID_EDX_BIT_PDPE1G) {
			return CMP_PS_1G;
		}
	case 2:	if (edx & CPUID_EDX_BIT_PSE) {
			return CMP_PS_2M;
		}
	default:
	case 1:	return CMP_PS_4K;
	}
}

/**
 * cr_init_pfns_compare() - page map PFN database numeric sort comparison function
 * @lhs:	left-hand side PFN
 * @rhs:	right-hand side PFN
 *
 * Return: -1, 1, or 0 if lhs_pfn is smaller than, greater than, or equal to rhs_pfn
 */

int cr_init_pfns_compare(const void *lhs, const void *rhs)
{
	uintptr_t lhs_pfn, rhs_pfn;

	CR_ASSERT_NOTNULL(lhs);
	CR_ASSERT_NOTNULL(rhs);
	lhs_pfn = *(const uintptr_t *)lhs;
	rhs_pfn = *(const uintptr_t *)rhs;
	if (lhs_pfn < rhs_pfn) {
		return -1;
	} else
	if (lhs_pfn > rhs_pfn) {
		return 1;
	} else {
		return 0;
	}
}

/**
 * cr_map_init_page_ent() - initialise a single {PML4,PDP,PD,PT} entry
 *
 * Return: Nothing.
 */

void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int pages_nx, int level, int map_direct)
{
	struct page_ent_1G *pe_1G;
	struct page_ent_2M *pe_2M;

	CR_ASSERT_NOTNULL(pe);
	memset(pe, 0, sizeof(*pe));
	pe->bits = PE_BIT_PRESENT | PE_BIT_CACHE_DISABLE | extra_bits;
	pe->nx = pages_nx;
	if (map_direct && (level == 3)) {
		pe_1G = (struct page_ent_1G *)pe;
		pe_1G->pfn_base = pfn_base;
	} else
	if (map_direct && (level == 2)) {
		pe_2M = (struct page_ent_2M *)pe;
		pe_2M->pfn_base = pfn_base;
	} else {
		pe->pfn_base = pfn_base;
	}
}

/**
 * cr_map_pages_from_va() - create contiguous VA to discontiguous PFN mappings in PML4
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int pages_nx)
{
	uintptr_t pfn_block_base, va_cur;
	int err;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_TRYADD(va_src, (uintptr_t)-1, npages * PAGE_SIZE);
	CR_ASSERT_TRYADD(va_dst, (uintptr_t)-1, npages * PAGE_SIZE);
	va_cur = va_dst;
	for (size_t npage = 0; npage < npages; npage++, va_src += PAGE_SIZE) {
		pfn_block_base = cr_virt_to_phys(va_src);
		CR_ASSERT_TRYADD(pfn_block_base, (uintptr_t)-1, 1);
		err = cr_map_pages_auto(params, &va_cur, pfn_block_base,
				pfn_block_base + 1, extra_bits, pages_nx,
				CMP_LVL_PT);
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/**
 * cr_pmem_walk_filter() - walk physical memory, combining sections with a list of reserved PFN
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit)
{
	int err;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_NOTNULL(params->filter);
	CR_ASSERT_NOTNULL(ppfn_base);
	CR_ASSERT_NOTNULL(ppfn_limit);
	if (!params->filter_last_base
	&&  !params->filter_last_limit) {
		if ((err = cr_pmem_walk_combine(params, &params->filter_last_base,
				&params->filter_last_limit)) < 1) {
			return err;
		}
		params->filter_ncur = 0;
	}
	CR_ASSERT_TRYADD(params->filter_nmax, (uintptr_t)-1, 1);
	for (; params->filter_ncur < (params->filter_nmax + 1); params->filter_ncur++) {
		if ((params->filter[params->filter_ncur] <  params->filter_last_base)
		||  (params->filter[params->filter_ncur] >= params->filter_last_limit)) {
			CR_ASSERT_TRYADD(params->filter_ncur, (uintptr_t)-1, 1);
			continue;
		} else
		if (params->filter[params->filter_ncur] == params->filter_last_base) {
			CR_ASSERT_TRYADD(params->filter_ncur, (uintptr_t)-1, 1);
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
			continue;
		} else {	
			*ppfn_base = params->filter_last_base;
			*ppfn_limit = params->filter[params->filter_ncur];
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
			CR_ASSERT_TRYADD(params->filter_ncur, (uintptr_t)-1, 1);
			params->filter_ncur++;
			return 1;
		}
	}
	if (params->filter_last_base < params->filter_last_limit) {
		*ppfn_base = params->filter_last_base;
		*ppfn_limit = params->filter_last_limit;
		params->filter_last_base = params->filter_last_limit = 0;
		return 1;
	} else {
		params->filter_last_base = params->filter_last_limit = 0;
		return cr_pmem_walk_filter(params, ppfn_base, ppfn_limit);
	}
}

#if defined(DEBUG)
/*
 * Exception debugging subroutines
 */

/**
 * cr_debug_init() - map exception debugging pages
 *
 * Allocates and maps IDT page, one (1) stack page, the framebuffer
 * page, and the exception debugging handler cr_debug_low() code page.
 * The IDT entries are initialised with cr_debug_low() as ISR. 
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_debug_init(struct cmp_params *cmp_params, struct page_ent *pml4, uintptr_t va_idt, uintptr_t va_stack, uintptr_t va_vga)
{
	int err, nidte;
	struct idt_ent *idt;
	uintptr_t pfn, va_this, va_mapped;

	CR_ASSERT_NOTNULL(cmp_params);
	CR_ASSERT_NOTNULL(cmp_params->map_base);
	CR_ASSERT_NOTNULL(cmp_params->map_limit);
	CR_ASSERT_CHKRNGE(cmp_params->map_base, cmp_params->map_limit, cmp_params->map_cur);
	CR_ASSERT_NOTNULL(pml4);
	CR_ASSERT((va_idt & va_stack & va_vga), ("%s: va_idt=%p, va_stack=%p, va_vga=%p", va_idt, va_stack, va_vga));

	/*
	 * Map and initialise IDT at va_idt
	 */
	if (cmp_params->map_cur >= cmp_params->map_limit) {
		return -ENOMEM;
	} else {
		va_this = cmp_params->map_cur;
		va_mapped = va_idt;
		pfn = cr_virt_to_phys(va_this);
		CR_ASSERT_TRYADD(pfn, (uintptr_t)-1, 1);
		CR_ASSERT_TRYADD((uintptr_t)cmp_params->map_cur, cmp_params->map_limit, PAGE_SIZE);
		cmp_params->map_cur += PAGE_SIZE;
		if ((err = cr_map_pages_direct(cmp_params, &va_mapped,
				pfn, pfn + 1, 0, CMP_BIT_NX_DISABLE,
				CMP_LVL_PML4, CMP_PS_4K, pml4)) != 0) {
			return err;
		} else {
			idt = (struct idt_ent *)va_this;
			pfn = cr_virt_to_phys((uintptr_t)cr_debug_low);
			for (nidte = 0; nidte < 256; nidte++) {
				CR_INIT_IDTE(&idt[nidte], pfn, 0, 0, 0);
			}
		}
	}

	/*
	 * Map stack page at va_stack
	 */
	if (cmp_params->map_cur >= cmp_params->map_limit) {
		return -ENOMEM;
	} else {
		va_mapped = va_stack;
		pfn = cr_virt_to_phys(cmp_params->map_cur);
		CR_ASSERT_TRYADD(pfn, (uintptr_t)-1, 1);
		CR_ASSERT_TRYADD((uintptr_t)cmp_params->map_cur, cmp_params->map_limit, PAGE_SIZE);
		cmp_params->map_cur += PAGE_SIZE;
		if ((err = cr_map_pages_direct(cmp_params, &va_mapped,
				pfn, pfn + 1, 0, CMP_BIT_NX_DISABLE,
				CMP_LVL_PML4, CMP_PS_4K, pml4)) != 0) {
			return err;
	}

	/*
	 * Map 8 framebuffer pages at va_vga
	 */
	va_mapped = va_vga;
	pfn = 0xb8;
	CR_ASSERT_TRYADD(pfn, (uintptr_t)-1, 8);
	if ((err = cr_map_pages_direct(cmp_params, &va_mapped,
			pfn, pfn + 8, 0, CMP_BIT_NX_DISABLE,
			CMP_LVL_PML4, CMP_PS_4K, pml4)) != 0) {
		return err;
	}

	/*
	 * Map cr_debug_low() page at identical VA
	 */
	va_mapped = (uintptr_t)cr_debug_low;
	if ((err = cr_map_pages_from_va(cmp_params, va_mapped,
			va_mapped, 1, 0, CMP_BIT_NX_DISABLE)) < 0) {
		return err;
	} else
		return 0;
	}
}

/**
 * cr_debug_low() - XXX
 *
 * Return: Nothing
 */

__asm(
	"\t.global		cr_debug_low\n"
	"\tcr_debug_low:\n"
	"\t			hlt\n"
	"\t.align		0x1000\n"
	"\tcr_debug_low_limit:\n"
	"\t.quad		.\n");
#endif /* defined(DEBUG) */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
