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
 * Page mapping logic and helper subroutines
 */

/**
 * cr_map_phys_to_virt_set() - map physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_phys_to_virt_set(struct cr_map_phys_node **map_phys_head, struct cr_map_phys_node **map_phys_cur, uintptr_t map_phys_limit, uintptr_t pfn, uintptr_t va)
{
	struct cr_map_phys_node *new, *node;

	for (node = *map_phys_head; node && node->next; node = node->next) {
		if (!node->next) {
			break;
		}
	}
	if ((uintptr_t)(*map_phys_cur + 1) >= map_phys_limit) {
		return -ENOMEM;
	} else {
		new = (*map_phys_cur)++;
		new->pfn = pfn;
		new->va = va;
		new->next = NULL;
	}
	if (!node) {
		return *map_phys_head = new, 0;
	} else {
		return node->next = new, 0;
	}
}

/**
 * cr_map_phys_to_virt_get() - translate physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_phys_to_virt_get(struct cr_map_phys_node *map_phys_head, uintptr_t pfn, uintptr_t *pva)
{
	struct cr_map_phys_node *node;

	for (node = map_phys_head; node; node = node->next) {
		if (node->pfn == pfn) {
			return *pva = node->va, 1;
		}
	}
	return -ENOENT;
}

/**
 * cr_map_init_page_ent() - initialise a single {PML4,PDP,PD,PT} entry
 *
 * Return: Nothing.
 */
void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int page_nx, int level, int map_direct)
{
	struct page_ent_1G *pe_1G;
	struct page_ent_2M *pe_2M;

	memset(pe, 0, sizeof(*pe));
	pe->bits = PE_BIT_PRESENT | PE_BIT_CACHE_DISABLE | extra_bits;
	pe->nx = page_nx;
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
 * cr_map_page() - recursively create one {1G,2M,4K} mapping from VA to PFN in {PML4,PDP,PD,PT}
 * @params:	mapping parameters
 * @va:		virtual address to map at
 * @pfn:	physical address (PFN) to map
 * @page_size:	262144 (1 GB page,) 512 (2 MB page,) or 1 (4 KB page)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 * @pt_cur:	pointer to {PML4,PDP,PD,PT} to map into
 *
 * Create {1G,2M,4K} mapping for pfn at va in the {PML4,PDP,PD,PT}
 * specified by page_size, pt_cur, and level using the supplied extra_bits
 * and page_nx bit. Lower-order page tables are recursively created on
 * demand. Newly created {PDP,PD,PT} are allocated from the map heap in
 * units of the page size (4096) without blocking.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_page(struct cmp_params *params, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, struct page_ent *pt_cur)
{
	int map_direct, err;
	struct page_ent *pt_next;
	unsigned long pt_idx;
	uintptr_t pt_next_pfn, pt_next_va;

	switch (level) {
	case 4: pt_idx = CR_VA_TO_PML4_IDX(*va);
		map_direct = 0;
		break;
	case 3:	pt_idx = CR_VA_TO_PDP_IDX(*va);
		if ((map_direct = (page_size == CR_PDPE_SIZE))) {
			extra_bits |= PE_BIT_PAGE_SIZE;
		}; break;
	case 2:	pt_idx = CR_VA_TO_PD_IDX(*va);
		if ((map_direct = (page_size == CR_PDE_SIZE))) {
			extra_bits |= PE_BIT_PAGE_SIZE;
		}; break;
	case 1:	pt_idx = CR_VA_TO_PT_IDX(*va);
		map_direct = 1;
		break;
	default:
		return -EINVAL;
	}
	if (map_direct) {
		cr_map_init_page_ent(&pt_cur[pt_idx], pfn, extra_bits,
			page_nx, level, map_direct);
		(*va) += PAGE_SIZE * page_size;
		return 0;
	} else
	if (!(pt_cur[pt_idx].bits & PE_BIT_PRESENT)) {
		if (params->map_cur >= params->map_limit) {
			return -ENOMEM;
		} else {
			pt_next = (struct page_ent *)params->map_cur;
			params->map_cur += PAGE_SIZE;
		}
		pt_next_pfn = cr_virt_to_phys((uintptr_t)pt_next);
		err = cr_map_phys_to_virt_set(&params->map_phys_head,
				&params->map_phys_cur, params->map_phys_limit,
				pt_next_pfn, (uintptr_t)pt_next);
		if (err < 0) {
			return err;
		}
		cr_map_init_page_ent(&pt_cur[pt_idx], pt_next_pfn,
			extra_bits, page_nx, level, map_direct);
	} else {
		if (map_direct && (level == 3)) {
			pt_next_pfn = ((struct page_ent_1G *)&pt_cur[pt_idx])->pfn_base;
		} else
		if (map_direct && (level == 2)) {
			pt_next_pfn = ((struct page_ent_2M *)&pt_cur[pt_idx])->pfn_base;
		} else {
			pt_next_pfn = pt_cur[pt_idx].pfn_base;
		}
		if ((err = cr_map_phys_to_virt_get(params->map_phys_head,
				pt_next_pfn, &pt_next_va)) < 0) {
			return err;
		} else {
			pt_next = (struct page_ent *)pt_next_va;
		}
	}
	return cr_map_page(params, va, pfn, page_size,
			extra_bits, page_nx, level - 1, pt_next);
}

/**
 * cr_map_pages_from_va() - create contiguous VA to discontiguous PFN mappings in PML4
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int page_nx)
{
	uintptr_t pfn_block_base, va_cur;
	int err;

	va_cur = va_dst;
	for (size_t npage = 0; npage < npages; npage++, va_src += PAGE_SIZE) {
		pfn_block_base = cr_virt_to_phys(va_src);
		err = cr_map_pages(params, &va_cur, pfn_block_base,
				pfn_block_base + 1, extra_bits, page_nx);
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/**
 * cr_map_pages() - create contiguous VA to PFN mappings in PML4
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry/ies
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry/ies
 *
 * Create {1G,2M,4K} mappings for each PFN within pfn_base..pfn_limit
 * starting at va_base in pml4 using the supplied extra_bits and page_nx
 * bit. {1G,2M} mappings are created whenever {1G,2M}-aligned VA/PFN
 * blocks are encountered; unaligned {1G,2M} VA/PFN blocks are allocated
 * in units of {2M,4K} relative to alignment. The map heap along with
 * most other parameters are passed through to cr_map_page() for each
 * page mapped.
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_map_pages(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx)
{
	int err;
	unsigned long pfn;
	size_t npages;

	for (pfn = pfn_base; pfn < pfn_limit; pfn += npages) {
		npages = pfn_limit - pfn_base;
		if ( ( npages >= CR_PDPE_SIZE)
		&&  ((*va_base & CR_PDPE_MASK) == 0)) {
			npages = CR_PDPE_SIZE;
		} else
		if ( ( npages >= CR_PDE_SIZE)
		&&  ((*va_base & CR_PDE_MASK) == 0)) {
			npages = CR_PDE_SIZE;
		} else {
			npages = 1;
		}
		if ((err = cr_map_page(params, va_base, pfn, npages,
				extra_bits, page_nx, 4, params->pml4)) != 0) {
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

int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit) {
	int err;

	if (!params->filter_last_base
	&&  !params->filter_last_limit) {
		if ((err = cr_pmem_walk_combine(params, &params->filter_last_base,
				&params->filter_last_limit)) < 1) {
			return err;
		}
		params->filter_ncur = 0;
	}
	for (; params->filter_ncur < (params->filter_nmax + 1); params->filter_ncur++) {
		if ((params->filter[params->filter_ncur] <  params->filter_last_base)
		||  (params->filter[params->filter_ncur] >= params->filter_last_limit)) {
			continue;
		} else
		if (params->filter[params->filter_ncur] == params->filter_last_base) {
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
			continue;
		} else {	
			*ppfn_base = params->filter_last_base;
			*ppfn_limit = params->filter[params->filter_ncur];
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
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

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
