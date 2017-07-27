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
	unsigned		mbz:17;
	uintptr_t		pfn_base:22;
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

/* Count of pages mapped per {PDP,PD,PT} entry */
#define CR_PTE_SIZE		(1)
#define CR_PDE_SIZE		(512)
#define CR_PDPE_SIZE		(512 * 512)

/* Convert virtual address to {PML4,PDP,PD,PT} index */
#define CR_PT_IDX_MASK		0x1ff
#define CR_VA_TO_PT_IDX(va)	(((va) >> (12)) & CR_PT_IDX_MASK)
#define CR_VA_TO_PD_IDX(va)	(((va) >> (9 + 12)) & CR_PT_IDX_MASK)
#define CR_VA_TO_PDP_IDX(va)	(((va) >> (9 + 9 + 12)) & CR_PT_IDX_MASK)
#define CR_VA_TO_PML4_IDX(va)	(((va) >> (9 + 9 + 9 + 12)) & CR_PT_IDX_MASK)
#define CR_VA_TO_PE_IDX(va, level) ({					\
	uintptr_t _va = (va), _level = (level);				\
	_level == 4 ? CR_VA_TO_PML4_IDX(_va) :				\
		_level == 3 ? CR_VA_TO_PDP_IDX(_va) :			\
			_level == 2 ? CR_VA_TO_PD_IDX(_va) :		\
					CR_VA_TO_PT_IDX(_va); })

/**
 * Interrupt-descriptor table (IDT) entry in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Sections 4.8.3, pages 90-91, 4.8.4, pages 92-93, and 8.9.1, page 247.
 */
enum ie_type_attr {
	IE_TYPE_ATTR_INT_GATE	= 0x0e,
	IE_TYPE_ATTR_TRAP_GATE	= 0x0f,
	IE_TYPE_ATTR_DPL00	= 0x00,
	IE_TYPE_ATTR_DPL01	= 0x20,
	IE_TYPE_ATTR_DPL02	= 0x40,
	IE_TYPE_ATTR_DPL03	= 0x60,
	IE_TYPE_ATTR_PRESENT	= 0x80,
};
struct idt_ent {
	unsigned		offset0_15:16;
	unsigned		selector:16;
	unsigned		ist:3, zero0_4:5;
	enum ie_type_attr	type_attr:8;
	unsigned long		offset16_63:48;
	unsigned		zero5_36:32;
} __attribute__((packed));
#define CR_INIT_IDTE(idte, _offset, _selector, _ist, _type_attr) do {	\
		(idte)->offset0_15 = (_offset) & 0xffff;		\
		(idte)->selector = (_selector);				\
		(idte)->ist = (_ist);					\
		(idte)->zero0_4 = 0;					\
		(idte)->type_attr = _type_attr;				\
		(idte)->offset16_63 = (_offset) >> 16;			\
		(idte)->zero5_36 = 0;					\
	} while (0)

/**
 * Interrupt-Descriptor-Table Register (IDTR) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 8.9.2, page 248.
 */
struct idtr_bits {
	unsigned		limit:16;
	unsigned long		offset:64;
} __attribute__((packed));
#define CR_INIT_IDTR(idtr, _limit, _offset) do {			\
		(idtr)->limit = (_limit);				\
		(idtr)->offset = (_offset);				\
	} while (0)

/**
 * Interrupt Stack Frame in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 8.9.3, pages 249-251.
 */
struct int_frame {
	unsigned long		return_rip:64;
	unsigned long		return_cs:16, zero0:48;
	unsigned long		return_rflags:64;
	unsigned long		return_rsp:64;
	unsigned long		return_ss:16, zero1:48;
} __attribute__((packed));

#endif /* !_AMD64DEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
