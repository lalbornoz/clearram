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
 * cr_amd64_map_pages_clone4K() - clone 4K mappings from aligned VA in PT(s)
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_amd64_map_pages_clone4K(struct cra_page_ent *pml4, uintptr_t va_src, uintptr_t *pva_dst, enum cra_pe_bits extra_bits, int pages_nx, size_t npages, struct cra_page_ent *(*alloc_pt)(int, uintptr_t), int (*xlate_pfn)(uintptr_t, uintptr_t *))
{
	int err;
	uintptr_t pfn_block_base, va_cur, va_dst;

	CRH_VALID_PTR(pml4);
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
				alloc_pt, xlate_pfn);
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

int cr_amd64_map_pages_unaligned(struct cra_page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, int level, struct cra_page_ent *(*alloc_pt)(int, uintptr_t), int (*xlate_pfn)(uintptr_t, uintptr_t *))
{
	int err;
	uintptr_t pfn_block_base, pfn_block_limit, pfn_block_base_offset;
	size_t page_size, block_size, align_size, npages;

	CRH_VALID_PTR(pml4);
	CRH_VALID_PTR(va_base);
	CRH_ASSERT(pfn_limit > pfn_base, "%s: pfn_limit=%p, pfn_base=%p", pfn_limit, pfn_base);
	CRH_VALID_RANGE(CRA_LVL_PT, CRA_LVL_PML4, level);
	CRH_ASSERT((level >= CRA_LVL_PT) && (level < CRA_LVL_PML4), "%s: level=%d", __func__, level);
	page_size = cr_amd64_cpuid_page_size_from_level(level);
	CRH_VALID_RANGE(CRA_PS_4K, CRA_PS_1G + 1, page_size);
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
				alloc_pt, xlate_pfn)) == 0) {
			continue;
		} else {
			return err;
		}
	}
	return 0;
}

/**
 * cr_amd64_map_free() - release map memory back to OS
 *
 * Return: Nothing
 */

void cr_amd64_map_free(struct cra_page_ent *pml4, void (*mfree)(void *))
{
	/* XXX */
}

/**
 * cr_amd64_map_translate() - translate PFN to VA using map
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_amd64_map_translate(struct cra_page_ent *pt, uintptr_t pfn, uintptr_t *va, int level, int (*xlate_pfn)(uintptr_t, uintptr_t *))
{
	int err, map_direct, found;
	size_t nent;
	struct cra_page_ent *pt_next;
	struct cra_page_ent_1G *pt1G;
	struct cra_page_ent_2M *pt2M;

	for (nent = 0; nent < 512; nent++) {
		if (!(pt[nent].bits & CRA_PE_PRESENT)) {
			continue;
		} else
		switch (level) {
		case CRA_LVL_PT: map_direct = 1; break;
		case CRA_LVL_PD: map_direct = (pt[nent].bits & CRA_PE_PAGE_SIZE); break;
		case CRA_LVL_PDP: map_direct = (pt[nent].bits & CRA_PE_PAGE_SIZE); break;
		case CRA_LVL_PML4: map_direct = 0; *va = 0; break;
		}
		if (!map_direct) {
			if ((err = xlate_pfn(pt[nent].pfn_base, (uintptr_t *)&pt_next)) < 0) {
				return err;
			} else
			if ((err = cr_amd64_map_translate(pt_next, pfn, va,
					level - 1, xlate_pfn)) < 0) {
				return err;
			} else {
				found = 1;
			}
		} else
		switch (level) {
		case CRA_LVL_PT:
			found = (pt[nent].pfn_base == pfn); break;
		case CRA_LVL_PD:
			if (pt[nent].bits & CRA_PE_PAGE_SIZE) {
				pt2M = (struct cra_page_ent_2M *)&pt[nent];
				found = (pt2M->pfn_base == (pfn & -CRA_PS_2M));
			} else {
				found = (pt[nent].pfn_base == pfn);
			}; break;
		case CRA_LVL_PDP:
			if (pt[nent].bits & CRA_PE_PAGE_SIZE) {
				pt1G = (struct cra_page_ent_1G *)&pt[nent];
				found = (pt1G->pfn_base == (pfn & -CRA_PS_1G));
			} else {
				found = (pt[nent].pfn_base == pfn);
			}; break;
		case CRA_LVL_PML4:
			found = 0; break;
		}
		if (found) {
			*va |= (nent << (((level - 1) * 9) + 12));
			switch (level) {
			case CRA_LVL_PD:
				if (pt[nent].bits & CRA_PE_PAGE_SIZE) {
					*va |= ((CRA_PS_2M - (pfn % CRA_PS_2M)) << (((level - 1 - 1) * 9) + 12));
				}; break;
			case CRA_LVL_PDP:
				if (pt[nent].bits & CRA_PE_PAGE_SIZE) {
					*va |= ((CRA_PS_1G - (pfn % CRA_PS_1G)) << (((level - 1 - 1) * 9) + 12));
				}; break;
			}
			return 0;
		}
	}
	return -ESRCH;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
