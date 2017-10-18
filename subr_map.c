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
 * Page mapping logic
 */

/**
 * cr_amd64_map_pages_aligned() functions
 *
 * Return: 0 on success, <0 otherwise
 */
static int crp_amd64_get_table(struct cra_page_ent *pml4, int level, int map_direct, struct cra_page_ent *pe, struct cra_page_ent **ppt_next, int (*xlate_pfn)(enum crh_ptl_type, uintptr_t, uintptr_t *)) {
	uintptr_t pt_next_pfn;
	if (map_direct && (level == CRA_LVL_PDP)) {
		pt_next_pfn = ((struct cra_page_ent_1G *)pe)->pfn_base;
	} else
	if (map_direct && (level == CRA_LVL_PD)) {
		pt_next_pfn = ((struct cra_page_ent_2M *)pe)->pfn_base;
	} else {
		pt_next_pfn = pe->pfn_base;
	}
	return xlate_pfn(CRH_PTL_PAGE_TABLE, pt_next_pfn, (uintptr_t *)ppt_next);
}
static int crp_amd64_fill_table(uintptr_t *va_base, uintptr_t *ppfn_cur, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, size_t page_size, int level, int map_direct, struct cra_page_ent *pt_cur, uintptr_t *ppt_idx, int (*link_ram_page)(uintptr_t, uintptr_t)) {
	int err;
	while (*ppfn_cur < pfn_limit) {
		if ((err = link_ram_page(*ppfn_cur, *va_base)) < 0) {
			return err;
		} else {
			cr_amd64_init_page_ent(&pt_cur[*ppt_idx], *ppfn_cur,
				extra_bits, pages_nx, level, map_direct);
		}
		*ppfn_cur += page_size;
		*va_base = CRA_VA_INCR(*va_base, page_size * PAGE_SIZE);
		if (++(*ppt_idx) >= 512) {
			break;
		}
	}
	return 0;
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

int cr_amd64_map_pages_aligned(struct cra_page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, size_t page_size, int (*alloc_pt)(struct cra_page_ent *, uintptr_t, enum cra_pe_bits, int, int, int, struct cra_page_ent *, struct cra_page_ent **), int (*link_ram_page)(uintptr_t, uintptr_t), int (*xlate_pfn)(enum crh_ptl_type, uintptr_t, uintptr_t *))
{
	int err, level, level_delta, map_direct;
	uintptr_t pt_idx, pfn_cur, va_last;
	struct cra_page_ent *pt_cur[CRA_LVL_PML4 + 1], *pt_next;

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
				err = alloc_pt(pml4, *va_base, extra_bits,
					pages_nx, level, map_direct, &pt_cur[level][pt_idx], &pt_next);
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
			va_last = (*va_base);
			if ((err = crp_amd64_fill_table(va_base, &pfn_cur, pfn_limit,
					extra_bits, pages_nx, page_size, level,
					map_direct, pt_cur[level], &pt_idx, link_ram_page)) < 0) {
				return err;
			} else {
				if (pfn_cur >= pfn_limit) {
					break;
				} else
				if ((va_last & (0x1ff << (9 + 9 + 12))) !=
						((*va_base) & (0x1ff << (9 + 9 + 12)))) {
					level_delta = -3;
				} else
				if ((va_last & (0x1ff << (9 + 12))) !=
						((*va_base) & (0x1ff << (9 + 12)))) {
					level_delta = -2;
				}
			}
		}
	}
	return 0;
}

/**
 * cr_amd64_map_pages_clone4K() - clone 4K mappings from aligned VA in PT(s)
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_amd64_map_pages_clone4K(struct cra_page_ent *pml4, uintptr_t va_src, uintptr_t *pva_dst, enum cra_pe_bits extra_bits, int pages_nx, size_t npages, int (*alloc_pt)(struct cra_page_ent *, uintptr_t, enum cra_pe_bits, int, int, int, struct cra_page_ent *, struct cra_page_ent **), int (*link_ram_page)(uintptr_t, uintptr_t), int (*xlate_pfn)(enum crh_ptl_type, uintptr_t, uintptr_t *))
{
	int err;
	uintptr_t pfn_block_base, va_cur, va_dst;

	if (pva_dst) {
		va_dst = *pva_dst;
	} else {
		va_dst = va_src;
	}
	va_cur = va_src;
	for (size_t npage = 0; npage < npages; npage++,
			va_cur = CRA_VA_INCR(va_cur, PAGE_SIZE)) {
		pfn_block_base = cr_host_virt_to_phys(va_cur);
		err = cr_amd64_map_pages_aligned(pml4, &va_dst, pfn_block_base,
				pfn_block_base + 1, extra_bits, pages_nx, CRA_PS_4K,
				alloc_pt, link_ram_page, xlate_pfn);
		if (err != 0) {
			return err;
		}
	}
	if (pva_dst) {
		*pva_dst = va_dst;
	}
	return 0;
}

/**
 * cr_amd64_map_pages_unaligned() functions
 *
 * Return: 1 on success, 0 otherwise
 */
static inline int crp_amd64_get_block_first(uintptr_t *ppfn_block_base, uintptr_t *ppfn_block_limit, size_t block_size, uintptr_t pfn_block_offset, size_t npages) {
	size_t npages_pfx, npages_min;
	uintptr_t pfn_block_limit;
	npages_pfx = block_size - pfn_block_offset;
	npages_min = npages_pfx + block_size;
	pfn_block_limit = *ppfn_block_limit;
	if (npages >= npages_min) {
		pfn_block_limit = *ppfn_block_base + min(
			npages, npages_pfx);
	}
	if (pfn_block_limit > *ppfn_block_base) {
		*ppfn_block_limit = pfn_block_limit;
		return 1;
	} else {
		return 0;
	}
}
static inline int crp_amd64_get_block_last(uintptr_t *ppfn_block_base, uintptr_t *ppfn_block_limit, size_t block_size, uintptr_t pfn_block_offset, size_t npages) {
	uintptr_t pfn_block_base = *ppfn_block_base;
	pfn_block_base = *ppfn_block_base;
	if (npages >= block_size) {
		pfn_block_base = *ppfn_block_limit & -block_size;
	}
	if (*ppfn_block_limit > pfn_block_base) {
		*ppfn_block_base = pfn_block_base;
		return 1;
	} else {
		return 0;
	}
}
static inline int crp_amd64_align_block_limit(uintptr_t *ppfn_block_base, uintptr_t *ppfn_block_limit, size_t align_size, size_t *pnpages) {
	uintptr_t pfn_block_limit, pfn_block_base_offset;
	pfn_block_base_offset = *ppfn_block_base & (align_size - 1);
	if (pfn_block_base_offset != 0) {
		if ((align_size - pfn_block_base_offset) >
				(*ppfn_block_limit - *ppfn_block_base)) {
			return 0;
		} else {
			*ppfn_block_base += (align_size - pfn_block_base_offset);
		}
	}
	pfn_block_limit = *ppfn_block_limit & -align_size;
	if (pfn_block_limit > *ppfn_block_base) {
		*ppfn_block_limit = pfn_block_limit;
		*pnpages = *ppfn_block_limit - *ppfn_block_base;
		return 1;
	} else {
		return 0;
	}
}

/**
 * cr_amd64_map_pages_unaligned() - create {1G,2M,4K} mappings from aligned VA to potentially unaligned PFN range in {PDP,PD,PT}
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry/ies
 * @pages_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry/ies
 * @level:      only map CRA_PS_{1G,2M,4K} sized blocks irrespective
 *		of the page size(s) employed
 *
 * Create {1G,2M,4K} mapping(s) for each PFN within pfn_base..pfn_limit
 * starting at va_base in params->pml4 using the supplied extra_bits
 * and pages_nx bit. {1G,2M} mappings are created whenever {1G,2M}-aligned
 * VA/PFN blocks are encountered; unaligned {1G,2M} VA/PFN blocks are
 * allocated in units of {2M,4K} relative to alignment. The map heap along
 * with most other parameters is passed through to cr_amd64_map_page() for each
 * page mapped.
 * The caller _must_ ensure that successive calls to cr_amd64_map_pages_auto()
 * occur in order of level=3..1, that the set of pfn_{base,limit} ranges
 * does not change between levels, and that *va_base is aligned to {1G,2M,4K}
 * at levels {3,2,1}.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_amd64_map_pages_unaligned(struct cra_page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, size_t page_size, int level, int (*alloc_pt)(struct cra_page_ent *, uintptr_t, enum cra_pe_bits, int, int, int, struct cra_page_ent *, struct cra_page_ent **), int (*link_ram_page)(uintptr_t, uintptr_t), int (*xlate_pfn)(enum crh_ptl_type, uintptr_t, uintptr_t *))
{
	int err;
	uintptr_t pfn_block_base, pfn_block_limit, pfn_block_base_offset;
	size_t block_size, align_size, npages;

	for (pfn_block_base = pfn_base, pfn_block_limit = pfn_limit;
			pfn_block_base < pfn_limit;
			pfn_block_base = pfn_block_limit, pfn_block_limit = pfn_limit) {
		switch (level) {
		case CRA_LVL_PT: block_size = CRA_PS_2M; align_size = CRA_PS_4K; break;
		case CRA_LVL_PD: block_size = CRA_PS_1G; align_size = CRA_PS_2M; break;
		case CRA_LVL_PDP: block_size = align_size = CRA_PS_1G; break;
		}
		if (!crp_amd64_align_block_limit(&pfn_block_base, &pfn_block_limit,
				align_size, &npages)) {
			continue;
		} else {
			pfn_block_base_offset = pfn_block_base & (block_size - 1);
		}
		if ((level < CRA_LVL_PDP && (pfn_block_base_offset == 0))
		&&  !crp_amd64_get_block_last(&pfn_block_base, &pfn_block_limit,
				block_size, pfn_block_base_offset, npages)) {
			continue;
		} else
		if ((level < CRA_LVL_PDP && (pfn_block_base_offset >  0))
		&&  !crp_amd64_get_block_first(&pfn_block_base, &pfn_block_limit,
				block_size, pfn_block_base_offset, npages)) {
			continue;
		} else
		if ((err = cr_amd64_map_pages_aligned(pml4, va_base,
				pfn_block_base, pfn_block_limit,
				extra_bits, pages_nx, page_size,
				alloc_pt, link_ram_page, xlate_pfn)) == 0) {
			continue;
		} else {
			return err;
		}
	}
	return 0;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
