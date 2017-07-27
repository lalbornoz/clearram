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

#ifndef _MAPDEF_H_
#define _MAPDEF_H_

/*
 * Subroutine data structures and bits
 */

/**
 * cr_map_phys_to_virt_[gs]et() PFN-VA hash table
 */
struct cr_map_phys_node {
	uintptr_t			pfn;
	uintptr_t			va;
	struct cr_map_phys_node *	next;
};
struct cr_map_phys {
	uintptr_t			pfn;
	uintptr_t			va;
	struct cr_map_phys_node *	map_base, *map_cur;
	uintptr_t			map_limit;
#define CR_MAP_PHYS_TBL_BITS		(1 << 10)
	struct cr_map_phys_node *	tbl[CR_MAP_PHYS_TBL_BITS];
};

/**
 * cr_map_page{,s}() parameters and bits
 */
struct cmp_params {
	struct page_ent *		pml4;
	uintptr_t			map_base, map_cur, map_limit;
	struct cr_map_phys		map_phys;
};
#define INIT_CMP_PARAMS(p) do {			\
		memset((p), 0, sizeof(*(p)));	\
	} while(0)
#define CMP_BIT_NX_DISABLE	0
#define CMP_BIT_NX_ENABLE	1
#define CMP_LVL_PT		1
#define CMP_LVL_PD		2
#define CMP_LVL_PDP		3
#define CMP_LVL_PML4		4
#define CMP_PS_4K		1
#define CMP_PS_2M		512
#define CMP_PS_1G		262144
#define CMP_PS_512G		134217728

/*
 * Page mapping logic and helper subroutines
 */

int cr_map_phys_to_virt_set(struct cr_map_phys *map_phys, uintptr_t pfn, uintptr_t va);
int cr_map_phys_to_virt_get(struct cr_map_phys *map_phys, uintptr_t pfn, uintptr_t *pva);
int cr_map_pages_direct(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int pages_nx, int level, size_t page_size, struct page_ent *pt_root);
int cr_map_pages_auto(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int pages_nx, int level);

#endif /* !_MAPDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
