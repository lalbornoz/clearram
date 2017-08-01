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
 * Mapping-related CPU data structures
 */

/**
 * {4-Kbyte,2-Mbyte,1-Gbyte} {PML4,PDP,PD,PT}E in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * section 5.3.3, pages 133, 135, 137, and section 5.4.1, pages 138-141.
 */
enum cra_pe_bits {
	CRA_PE_PRESENT		= 0x001,
	CRA_PE_READ_WRITE	= 0x002,
	CRA_PE_USER_SUPERVISOR	= 0x004,
	CRA_PE_WRITE_THROUGH	= 0x008,
	CRA_PE_CACHE_DISABLE	= 0x010,
	CRA_PE_ACCESSED		= 0x020,
	CRA_PE_DIRTY		= 0x040,
	CRA_PE_PAGE_SIZE	= 0x080,
	CRA_PE_GLOBAL		= 0x100,
};
enum cra_pe_nx {
	CRA_NX_DISABLE		= 0,
	CRA_NX_ENABLE		= 1,
};
struct cra_page_ent {
	enum cra_pe_bits	bits:9;
	unsigned		avl0_2:3;
	uintptr_t		pfn_base:40;
	unsigned		avl3_12:11;
	enum cra_pe_nx		nx:1;
} __attribute__((packed));
struct cra_page_ent_1G {
	enum cra_pe_bits	bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1;
	unsigned		mbz:17;
	uintptr_t		pfn_base:22;
	unsigned		avl3_12:11;
	enum cra_pe_nx		nx:1;
} __attribute__((packed));
struct cra_page_ent_2M {
	enum cra_pe_bits	bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1;
	unsigned		mbz:8;
	uintptr_t		pfn_base:31;
	unsigned		avl3_12:11;
	enum cra_pe_nx		nx:1;
} __attribute__((packed));

/**
 * {PML4,PDP,PD,PT} descriptor
 */
struct cra_page_tbl_desc {
	uintptr_t	pfn, va_host, va_hi;
} __attribute__((packed));
#define CRA_INIT_PAGE_TBL_DESC(p, _va_host, _va_hi) do {	\
		(p)->pfn = cr_host_virt_to_phys(		\
			(uintptr_t)(_va_host));			\
		(p)->va_host = (uintptr_t)(_va_host);		\
		(p)->va_hi = (uintptr_t)(_va_hi);		\
	} while (0)

/**
 * PFN and VA manipulation constants
 */
#define CRA_IDX_MASK		0x1ff
#define CRA_LVL_PT		(1)
#define CRA_PS_4K		(1)
#define CRA_SIZE_PTE		(1)
#define CRA_LVL_PD		(2)
#define CRA_PS_2M		(512)
#define CRA_SIZE_PDE		(512)
#define CRA_LVL_PDP		(3)
#define CRA_PS_1G		(512 * 512)
#define CRA_SIZE_PDPE		(512 * 512)
#define CRA_LVL_PML4		(4)
#define CRA_PS_512G		(512 * 512 * 512)
#define CRA_SIZE_PML4E		(512 * 512 * 512)
#define CRA_VA_INCR(va, incr)					\
	((((int64_t)((va) + (incr))) << 16) >> 16)

/**
 * Convert {PT,PD,PDP,PML4} index to VA
 */
#define CRA_PT_IDX_TO_VA(idx)	(((idx) & CRA_IDX_MASK) << (12))
#define CRA_PD_IDX_TO_VA(idx)	(((idx) & CRA_IDX_MASK) << (9 + 12))
#define CRA_PDP_IDX_TO_VA(idx)	(((idx) & CRA_IDX_MASK) << (9 + 9 + 12))
#define CRA_PML4_IDX_TO_VA(idx)	(((idx) & CRA_IDX_MASK) << (9 + 9 + 9 + 12))
#define CRA_PE_IDX_TO_VA(idx, level) ({				\
	uintptr_t _idx = (idx), _level = (level);		\
	_level == 4 ? CRA_PML4_IDX_TO_idx(_idx) :		\
		_level == 3 ? CRA_PDP_IDX_TO_idx(_idx) :	\
			_level == 2 ? CRA_PD_IDX_TO_idx(_idx) :	\
					CRA_PT_IDX_TO_idx(_idx); })

/**
 * Convert VA to {PT,PD,PDP,PML4} index
 */
#define CRA_VA_TO_PT_IDX(va)	(((va) >> (12)) & CRA_IDX_MASK)
#define CRA_VA_TO_PD_IDX(va)	(((va) >> (9 + 12)) & CRA_IDX_MASK)
#define CRA_VA_TO_PDP_IDX(va)	(((va) >> (9 + 9 + 12)) & CRA_IDX_MASK)
#define CRA_VA_TO_PML4_IDX(va)	(((va) >> (9 + 9 + 9 + 12)) & CRA_IDX_MASK)
#define CRA_VA_TO_PE_IDX(va, level) ({				\
	uintptr_t _va = (va), _level = (level);			\
	_level == 4 ? CRA_VA_TO_PML4_IDX(_va) :			\
		_level == 3 ? CRA_VA_TO_PDP_IDX(_va) :		\
			_level == 2 ? CRA_VA_TO_PD_IDX(_va) :	\
					CRA_VA_TO_PT_IDX(_va); })

/**
 * Page mapping logic
 */
int cr_amd64_map_pages_clone4K(struct cra_page_ent *pml4, uintptr_t va_src, uintptr_t *pva_dst, enum cra_pe_bits extra_bits, int pages_nx, size_t npages, struct cra_page_ent *(*alloc_pt)(int, uintptr_t), int (*xlate_pfn)(uintptr_t, uintptr_t *));
int cr_amd64_map_pages_unaligned(struct cra_page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum cra_pe_bits extra_bits, int pages_nx, int level, struct cra_page_ent *(*alloc_pt)(int, uintptr_t), int (*xlate_pfn)(uintptr_t, uintptr_t *));
void cr_amd64_map_free(struct cra_page_ent *pml4, void (*mfree)(void *));
int cr_amd64_map_translate(struct cra_page_ent *pt, uintptr_t pfn, uintptr_t *va, int level, int (*xlate_pfn)(uintptr_t, uintptr_t *));
#endif /* !_MAPDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
