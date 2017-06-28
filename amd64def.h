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

#ifndef _AMD64DEF_H_
#define _AMD64DEF_H_

/* 
 * CPU data structures
 */

/*
 * Control Register 3 (CR3) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 5.3.2, pages 130-131.
 */
enum cr3_bits {
	CR3_BIT_WRITE_THROUGH	= 0x008,
	CR3_BIT_CACHE_DISABLE	= 0x010,
};
struct cr3 {
	enum cr3_bits		bits:5;
	unsigned		ign1:7;
	uintptr_t		pml4_pfn_base:40;
	unsigned		ign2:12;
} __attribute__((packed));
#define CR_INIT_CR3(cr3, new_pml4_pfn_base, extra_bits) do {		\
		(cr3)->bits = extra_bits;				\
		(cr3)->ign1 = 0;					\
		(cr3)->pml4_pfn_base = new_pml4_pfn_base;		\
		(cr3)->ign2 = 0;					\
	} while (0)

/*
 * {4-Kbyte,2-Mbyte,1-Gbyte} {PML4,PDP,PD,PT}E in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * section 5.3.3, pages 133, 135, 137, and section 5.4.1, pages 138-141.
 */
enum pe_bits {
	PE_BIT_PRESENT		= 0x001,
	PE_BIT_READ_WRITE	= 0x002,
	PE_BIT_USER_SUPERVISOR	= 0x004,
	PE_BIT_WRITE_THROUGH	= 0x008,
	PE_BIT_CACHE_DISABLE	= 0x010,
	PE_BIT_ACCESSED		= 0x020,
	PE_BIT_DIRTY		= 0x040,
	PE_BIT_PAGE_SIZE	= 0x080,
	PE_BIT_GLOBAL		= 0x100,
};
struct page_ent {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	uintptr_t		pfn_base:40;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));
struct page_ent_1G {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1;
	unsigned		mbz:18;
	uintptr_t		pfn_base:21;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));
struct page_ent_2M {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1;
	unsigned		mbz:8;
	uintptr_t		pfn_base:31;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));

/* {PML4,PDP,PD,PT} entry base address masks */
#define CR_PAGE_ENT_IDX_MASK	0x1ff
#define CR_PDPE_MASK		0x3fffffff
#define CR_PDE_MASK		0x1fffff

/* Count of pages mapped per {PDP,PD,PT} entry */
#define CR_PDPE_SIZE		(512 * 512)
#define CR_PDE_SIZE		(512)
#define CR_PTE_SIZE		(1)

/* Convert virtual address to {PML4,PDP,PD,PT} index */
#define CR_VA_TO_PML4_IDX(va)	(((va) >> (9 + 9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PDP_IDX(va)	(((va) >> (9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PD_IDX(va)	(((va) >> (9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PT_IDX(va)	(((va) >> (12)) & CR_PAGE_ENT_IDX_MASK)

/**
 * cr_{map,xlate}_phys_to_virt() PFN-VA mapping
 */
struct cr_map_phys_node {
	uintptr_t			pfn;
	uintptr_t			va;
	struct cr_map_phys_node *	next;
};

/**
 * cr_pmem_walk_*() parameters
 */
#if defined(__linux__)
struct cpw_params {
	unsigned	new_nid:1, restart:1;
	int		nid;
	uintptr_t	node_base, node_limit;
	uintptr_t	pfn_cur;

	unsigned	combine_fini:1;
	uintptr_t	combine_last_base, combine_last_limit;

	uintptr_t *	filter;
	uintptr_t	filter_ncur, filter_nmax;
	uintptr_t	filter_last_base, filter_last_limit;
};
#define INIT_CPW_PARAMS(p) do {			\
		(p)->new_nid = 1;		\
		(p)->restart = 1;		\
		(p)->nid = 0;			\
		(p)->combine_fini = 0;		\
		(p)->filter_last_base = 0;	\
		(p)->filter_last_limit = 0;	\
	} while(0)
#elif defined(__FreeBSD__)
struct cpw_params {
	int		nid;
	uintptr_t *	filter;
	uintptr_t	filter_ncur, filter_nmax;
	uintptr_t	filter_last_base, filter_last_limit;
};
#define INIT_CPW_PARAMS(p) do {			\
		(p)->nid = 0;			\
		(p)->filter_last_base = 0;	\
		(p)->filter_last_limit = 0;	\
	} while(0)
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/**
 * cr_map_page{,s}() parameters
 */
struct cmp_params {
	struct page_ent *		pml4;
	uintptr_t			map_base, map_cur, map_limit;
	struct cr_map_phys_node *	map_phys_base, *map_phys_cur;
	uintptr_t			map_phys_limit;
	struct cr_map_phys_node *	map_phys_head;
};
#define INIT_CMP_PARAMS(p) do {			\
		memset((p), 0, sizeof(*(p)));	\
	} while(0)

/*
 * Page mapping logic and helper subroutines
 */

int cr_map_phys_to_virt_set(struct cr_map_phys_node **map_phys_head, struct cr_map_phys_node **map_phys_cur, uintptr_t map_phys_limit, uintptr_t pfn, uintptr_t va);
int cr_map_phys_to_virt_get(struct cr_map_phys_node *map_phys_cur, uintptr_t pfn, uintptr_t *pva);
void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int page_nx, int level, int map_direct);
int cr_map_page(struct cmp_params *params, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, struct page_ent *pt_cur);
int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int page_nx);
int cr_map_pages(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx);
int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);

#endif /* !_AMD64DEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
