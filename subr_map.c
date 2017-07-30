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
 * Page mapping logic and helper subroutines
 */

/**
 * cmptv_hash_pfn() - hash single PFN
 *
 * Return: hash of pfn
 */

static unsigned long cmptv_hash_pfn(uintptr_t pfn)
{
	unsigned long hash;
	size_t nbyte;

	for (hash = 0, nbyte = 0; nbyte < sizeof(pfn); nbyte++) {
		hash *= 0x100000001B3ULL;
		hash ^= *((uint8_t *)&pfn + nbyte);
	}
	return hash;
}

/**
 * cr_map_phys_to_virt_set() - map physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_phys_to_virt_set(struct cr_map_phys *map_phys, uintptr_t pfn, uintptr_t va)
{
	unsigned long pfn_hash;
	struct cr_map_phys_node *new, *node;

	CR_ASSERT_NOTNULL(map_phys);
	pfn_hash = cmptv_hash_pfn(pfn) & (CR_MAP_PHYS_TBL_BITS - 1);
	for (node = map_phys->tbl[pfn_hash]; node && node->next; node = node->next) {
		if (!node->next) {
			break;
		}
	}
	CR_ASSERT_TRYADD((uintptr_t)map_phys->map_cur, (uintptr_t)-1, 1);
	if (((uintptr_t)map_phys->map_cur + 1) >= map_phys->map_limit) {
		return -ENOMEM;
	} else {
		new = map_phys->map_cur++;
		new->pfn = pfn;
		new->va = va;
		new->next = NULL;
	}
	if (!node) {
		return map_phys->tbl[pfn_hash] = new, 0;
	} else {
		return node->next = new, 0;
	}
}

/**
 * cr_map_phys_to_virt_get() - translate physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_phys_to_virt_get(struct cr_map_phys *map_phys, uintptr_t pfn, uintptr_t *pva)
{
	unsigned long pfn_hash;
	struct cr_map_phys_node *node;

	CR_ASSERT_NOTNULL(map_phys);
	CR_ASSERT_NOTNULL(pva);
	CR_ASSERT_NOTNULL(CR_MAP_PHYS_TBL_BITS);
	pfn_hash = cmptv_hash_pfn(pfn) & (CR_MAP_PHYS_TBL_BITS - 1);
	for (node = map_phys->tbl[pfn_hash]; node; node = node->next) {
		if (node->pfn == pfn) {
			return *pva = node->va, 1;
		}
	}
	return -ENOENT;
}

static int cmpd_link_table(struct cmp_params *params, enum pe_bits extra_bits, int pages_nx, int level, int map_direct, struct page_ent *pe, struct page_ent **ppt_next)
{
	int err;
	struct page_ent *pt_next;
	uintptr_t pt_next_pfn, pt_next_va;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_NOTNULL(params->map_base);
	CR_ASSERT_NOTNULL(params->map_limit);
	CR_ASSERT_CHKRNGE(params->map_base, params->map_limit, params->map_cur);
	CR_ASSERT_NOTNULL(pe);
	CR_ASSERT_NOTNULL(ppt_next);
	if (!(pe->bits & PE_BIT_PRESENT)) {
		if (params->map_cur >= params->map_limit) {
			return -ENOMEM;
		} else {
			pt_next = (struct page_ent *)params->map_cur;
			CR_ASSERT_TRYADD((uintptr_t)params->map_cur, params->map_limit, PAGE_SIZE);
			params->map_cur += PAGE_SIZE;
		}
		pt_next_pfn = cr_virt_to_phys((uintptr_t)pt_next);
		if ((err = cr_map_phys_to_virt_set(&params->map_phys,
				pt_next_pfn, (uintptr_t)pt_next)) < 0) {
			return err;
		} else {
			cr_map_init_page_ent(pe, pt_next_pfn,
				extra_bits, pages_nx, level, map_direct);
		}
	} else {
		switch (level) {
		case CMP_LVL_PDP: case CMP_LVL_PD:
			if (map_direct) {
				switch (level) {
				case CMP_LVL_PDP:
					pt_next_pfn = ((struct page_ent_1G *)pe)->pfn_base;
					break;
				case CMP_LVL_PD:
					pt_next_pfn = ((struct page_ent_2M *)pe)->pfn_base;
					break;
				}
				break;
			}
		case CMP_LVL_PML4: case CMP_LVL_PT:
			pt_next_pfn = pe->pfn_base;
			break;
		}
		if ((err = cr_map_phys_to_virt_get(&params->map_phys,
				pt_next_pfn, &pt_next_va)) < 0) {
			return err;
		} else {
			pt_next = (struct page_ent *)pt_next_va;
		}
	}
	*ppt_next = pt_next;
	return 0;
}

/**
 * cr_map_pages_direct() - create {1G,2M,4K} mappings from VA to PFN in {PML4,PDP,PD,PT}
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry
 * @pages_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry
 * @level:	one of CMP_LVL_{PML4,PDP,PD,PT}
 * @page_size:	one of CMP_PS_{1G,2M,4K}
 * @pt_root:	pointer to {PML4,PDP,PD,PT} to map into
 *
 * Create {1G,2M,4K} mapping(s) for each PFN within pfn_base..pfn_limit
 * starting at va_base in pt_next using the supplied extra_bits, pages_nx
 * bit, and page_size. Lower-order page tables are created on demand.
 * Newly created {PDP,PD,PT} are allocated from the map heap in units of
 * the page size (0x1000) without blocking.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_pages_direct(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int pages_nx, int level, size_t page_size, struct page_ent *pt_root)
{
	int err, level_cur, level_delta, map_direct;
	uintptr_t pt_idx, pfn_cur;
	struct page_ent *pt_cur[4], *pt_next;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_NOTNULL(va_base);
	CR_ASSERT_CHKRNGE(CMP_LVL_PT, CMP_LVL_PML4 + 1, level);
	pt_cur[4] = pt_root;
	for (level_cur = level, level_delta = 1;
			level_cur >= CMP_LVL_PT;
			level_cur -= level_delta, level_delta = 1) {
		switch (level_cur) {
		case CMP_LVL_PT: map_direct = (page_size == CR_PTE_SIZE); break;
		case CMP_LVL_PD: map_direct = (page_size == CR_PDE_SIZE); break;
		case CMP_LVL_PDP: map_direct = (page_size == CR_PDPE_SIZE); break;
		case CMP_LVL_PML4: map_direct = (0); break;
		}
		pt_idx = CR_VA_TO_PE_IDX(*va_base, level_cur);
		if (!map_direct) {
			if ((err = cmpd_link_table(params, extra_bits, pages_nx,
					level_cur, map_direct, &pt_cur[level_cur][pt_idx],
					&pt_next)) < 0) {
				return err;
			} else {
				pt_cur[level_cur - 1] = pt_next;
			}
		} else {
			for (pfn_cur = pfn_base; pfn_cur < pfn_limit;
					pfn_cur += page_size,
					(*va_base) += page_size * PAGE_SIZE) {
				cr_map_init_page_ent(&pt_cur[level_cur][pt_idx], pfn_cur,
					extra_bits | (map_direct ? PE_BIT_PAGE_SIZE : 0),
					pages_nx, level_cur, map_direct);
				if (++pt_idx >= 512) {
					break;
				}
				CR_ASSERT_TRYADD(pfn_cur, pfn_limit, page_size);
				CR_ASSERT_TRYADD(*va_base, -1, page_size * PAGE_SIZE);
			}
			if (pfn_cur < pfn_limit) {
				level_delta = -1;
			} else {
				break;
			}
		}
	}
	return 0;
}

static int cmpa_first_unmapped_block(uintptr_t *ppfn_block_base, uintptr_t pfn_limit, int level, size_t block_size)
{
	uintptr_t pfn_block_cur;

	CR_ASSERT_NOTNULL(ppfn_block_base);
	CR_ASSERT(pfn_limit > *ppfn_block_base);
	CR_ASSERT_CHKRNGE(CMP_LVL_PT, CMP_LVL_PML4, level);
	CR_ASSERT_CHKRNGE(CMP_PS_4K, CMP_PS_1G + 1, block_size);
	if (level == CMP_LVL_PDP) {
		return 0;
	} else
	for (pfn_block_cur = *ppfn_block_base;
			pfn_block_cur < pfn_limit;
			pfn_block_cur += block_size) {
		if (((pfn_block_cur & (block_size - 1)) == 0)
		&&  ((pfn_limit - pfn_block_cur) >= block_size)) {
			CR_ASSERT_TRYADD(pfn_block_cur, pfn_limit, block_size);
			continue;
		} else {
			*ppfn_block_base = pfn_block_cur;
			return 0;
		}
		CR_ASSERT_TRYADD(pfn_block_cur, pfn_limit, block_size);
	}
	return -ENOENT;
}

static int cmpa_align_pfn_range(uintptr_t *ppfn_block_base, uintptr_t *ppfn_block_limit, int level, size_t block_size)
{
	uintptr_t pfn_block_offset, pfn_block_offset_delta, pfn_block_limit;

	CR_ASSERT_NOTNULL(ppfn_block_base);
	CR_ASSERT_NOTNULL(ppfn_block_limit);
	CR_ASSERT(*ppfn_block_limit > *ppfn_block_base);
	CR_ASSERT_CHKRNGE(CMP_LVL_PT, CMP_LVL_PML4, level);
	CR_ASSERT_CHKRNGE(CMP_PS_4K, CMP_PS_1G + 1, block_size);
	pfn_block_offset = *ppfn_block_base & (block_size - 1);
	if (pfn_block_offset == 0) {
		pfn_block_limit = CR_DIV_ROUND_DOWN_ULL(*ppfn_block_limit, block_size);
		if (pfn_block_limit == *ppfn_block_base) {
			return -ENOMEM;
		} else {
			*ppfn_block_limit = pfn_block_limit;
		}
	} else
	if (level == CMP_LVL_PT) {
		*ppfn_block_limit = *ppfn_block_base +
			min(*ppfn_block_limit - *ppfn_block_base,
				(512 - (*ppfn_block_base % 512)));
	} else {
		pfn_block_offset_delta = block_size - pfn_block_offset;
		if ((*ppfn_block_limit - *ppfn_block_base) >= pfn_block_offset_delta) {
			CR_ASSERT_TRYADD(*ppfn_block_base, -1, pfn_block_offset_delta);
			*ppfn_block_base += pfn_block_offset_delta;
			*ppfn_block_limit = CR_DIV_ROUND_DOWN_ULL(*ppfn_block_limit, block_size);
		} else {
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * cr_map_pages_auto() - map PFN range to VA in blocks of {1G,2M,4K}
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry/ies
 * @pages_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry/ies
 * @level:      only map CMP_PS_{1G,2M,4K} sized blocks irrespective
 *		of the page size(s) employed
 *
 * Create {1G,2M,4K} mapping(s) for each PFN within pfn_base..pfn_limit
 * starting at va_base in params->pml4 using the supplied extra_bits
 * and pages_nx bit. {1G,2M} mappings are created whenever {1G,2M}-aligned
 * VA/PFN blocks are encountered; unaligned {1G,2M} VA/PFN blocks are
 * allocated in units of {2M,4K} relative to alignment. The map heap along
 * with most other parameters is passed through to cr_map_page() for each
 * page mapped.
 * The caller _must_ ensure that successive calls to cr_map_pages_auto()
 * occur in order of level=3..1, that the set of pfn_{base,limit} ranges
 * does not change between levels, and that *va_base is aligned to {1G,2M,4K}
 * at levels {3,2,1}.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_pages_auto(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int pages_nx, int level)
{
	int err;
	uintptr_t pfn_block_base, pfn_block_limit;
	size_t page_size, block_size;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_NOTNULL(va_base);
	CR_ASSERT(pfn_limit > pfn_base);
	CR_ASSERT_CHKRNGE(CMP_LVL_PT, CMP_LVL_PML4, level);
	CR_ASSERT((level >= CMP_LVL_PT) && (level < CMP_LVL_PML4), ("%s: level=%d", func, level));
	page_size = cr_cpuid_page_size_from_level(level);
	CR_ASSERT_CHKRNGE(CMP_PS_4K, CMP_PS_1G + 1, page_size);
	for (pfn_block_base = pfn_base, pfn_block_limit = pfn_limit;
			pfn_block_base < pfn_limit;
			pfn_block_base = pfn_block_limit, pfn_block_limit = pfn_limit) {
		switch (level) {
		case CMP_LVL_PT: block_size = CMP_PS_2M; break;
		case CMP_LVL_PD: block_size = CMP_PS_1G; break;
		case CMP_LVL_PDP: block_size = CMP_PS_1G; break;
		}
#if defined(DEBUG)
		if (level > 1) {
			CR_ASSERT_ISALIGN(*va_base, (block_size * PAGE_SIZE));
		}
#endif /* defined(DEBUG) */
		CR_ASSERT_ISALIGN(*va_base, (CMP_PS_4K * PAGE_SIZE));
		if (cmpa_align_pfn_range(&pfn_block_base,
				&pfn_block_limit, level, block_size) == -ENOMEM) {
			continue;
		} else
		if ((err = cmpa_first_unmapped_block(&pfn_block_base,
				pfn_block_limit, level, block_size)) == -ENOENT) {
			continue;
		} else
		if ((err = cr_map_pages_direct(params, va_base, pfn_block_base,
				pfn_block_limit, extra_bits, pages_nx,
				CMP_LVL_PML4, page_size, params->pml4)) == 0) {
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
