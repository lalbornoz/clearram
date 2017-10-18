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

#ifndef __LP64__
#error Only x86_64 is supported at present.
#elif PAGE_SIZE != 0x1000
#error Only 4 KB pages are supported at present.
#endif /* !__LP64__ || PAGE_SIZE != 4096 */

/**
 * cr_amd64_cpuid_max_page_size() CPUID bits
 */
enum cra_cpuid_bits {
	CRA_CPUID_FUNC_BASIC_HIGHEST	= 0x00000000,
	CRA_CPUID_FUNC_BASIC_FEATURES	= 0x00000001,
	CRA_CPUID_FUNC_EXT_HIGHEST	= 0x80000000,
	CRA_CPUID_FUNC_EXT_FEATURES	= 0x80000001,
	CRA_CPUID_FEAT_BASIC_PSE	= 0x00000008,
	CRA_CPUID_FEAT_EXT_PDPE1G	= 0x04000000,
};

/*
 * CPU data structures
 */

/**
 * Control Register 3 (CR3) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 5.3.2, pages 130-131.
 */
enum cra_cr3_bits {
	CRA_CR3_WRITE_THROUGH	= 0x008,
	CRA_CR3_CACHE_DISABLE	= 0x010,
};
struct cra_cr3 {
	enum cra_cr3_bits	bits:5;
	unsigned		ign1:7;
	uintptr_t		pml4_pfn_base:40;
	unsigned		ign2:12;
} __attribute__((packed));
#define CRA_INIT_CR3(cr3, _bits, _pml4_pfn_base) do {		\
		(cr3)->bits = (_bits);				\
		(cr3)->ign1 = 0;				\
		(cr3)->pml4_pfn_base = (_pml4_pfn_base);	\
		(cr3)->ign2 = 0;				\
	} while (0)

/**
 * Code-Segment Descriptor (GDT entry) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Sections 4.8, pages 88-90
 */
enum cra_gdte_attr {
	CRA_GDTE_ATTR_CONFORM	= 0x04,
	CRA_GDTE_ATTR_CODE	= 0x18,
	CRA_GDTE_ATTR_DATA	= 0x10,
	CRA_GDTE_ATTR_DPL00	= 0x00,
	CRA_GDTE_ATTR_DPL01	= 0x20,
	CRA_GDTE_ATTR_DPL02	= 0x40,
	CRA_GDTE_ATTR_DPL03	= 0x60,
	CRA_GDTE_ATTR_PRESENT	= 0x80,
};
enum cra_gdte_flags {
	CRA_GDTE_FLAG_AVL	= 0x01,
	CRA_GDTE_FLAG_LONG	= 0x02,
};
struct cra_gdt_ent {
	unsigned		unused0_15:16;
	unsigned		base0_23:24;
	enum cra_gdte_attr	attr:8;
	unsigned		unused16_19:4;
	enum cra_gdte_flags	flags:4;
	unsigned		base24_31:8;
} __attribute__((packed));
#define CRA_INIT_GDTE_CODE(gdte, _base, _attr, _avl) do {	\
		(gdte)->base0_23 = (_base) & 0xffffff;		\
		(gdte)->attr = (_attr) | CRA_GDTE_ATTR_CODE |	\
			(_avl ? CRA_GDTE_FLAG_AVL : 0);		\
		(gdte)->flags = CRA_GDTE_FLAG_LONG;		\
		(gdte)->base24_31 = (_base) >> 24;		\
	} while (0)
#define CRA_INIT_GDTE_DATA(gdte, _base, _attr, _avl) do {	\
		(gdte)->base0_23 = (_base) & 0xffffff;		\
		(gdte)->attr = (_attr) | CRA_GDTE_ATTR_DATA |	\
			(_avl ? CRA_GDTE_FLAG_AVL : 0);		\
		(gdte)->flags = CRA_GDTE_FLAG_LONG | (_avl);	\
		(gdte)->base24_31 = (_base) >> 24;		\
	} while (0)

/**
 * Global Descriptor-Table Register (GDTR) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 4.6.2, pages 74-75
 */
struct cra_gdtr_bits {
	unsigned		limit:16;
	unsigned long		base:64;
} __attribute__((packed));
#define CRA_INIT_GDTR(gdtr, _base, _limit) do {			\
		(gdtr)->limit = (_limit);			\
		(gdtr)->base = (_base);				\
	} while (0)

/**
 * Interrupt-descriptor table (IDT) entry in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Sections 4.8.3, pages 90-91, 4.8.4, pages 92-93, and 8.9.1, page 247.
 */
enum cra_idte_attr {
	CRA_IDTE_ATTR_INT_GATE	= 0x0e,
	CRA_IDTE_ATTR_TRAP_GATE	= 0x0f,
	CRA_IDTE_ATTR_DPL00	= 0x00,
	CRA_IDTE_ATTR_DPL01	= 0x20,
	CRA_IDTE_ATTR_DPL02	= 0x40,
	CRA_IDTE_ATTR_DPL03	= 0x60,
	CRA_IDTE_ATTR_PRESENT	= 0x80,
};
struct cra_idt_ent {
	unsigned		offset0_15:16;
	unsigned		selector:16;
	unsigned		ist:3, zero0_4:5;
	enum cra_idte_attr	attr:8;
	unsigned long		offset16_63:48;
	unsigned		zero5_36:32;
} __attribute__((packed));
#define CRA_INIT_IDTE(idte, _offset, _selector, _ist, _attr) do {\
		(idte)->offset0_15 = (_offset) & 0xffff;	\
		(idte)->selector = (_selector);			\
		(idte)->ist = (_ist);				\
		(idte)->zero0_4 = 0;				\
		(idte)->attr = _attr;				\
		(idte)->offset16_63 = (_offset) >> 16;		\
		(idte)->zero5_36 = 0;				\
	} while (0)

/**
 * Interrupt-Descriptor-Table Register (IDTR) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 8.9.2, page 248.
 */
struct cra_idtr_bits {
	unsigned		limit:16;
	unsigned long		base:64;
} __attribute__((packed));
#define CRA_INIT_IDTR(idtr, _base, _limit) do {			\
		(idtr)->limit = (_limit);			\
		(idtr)->base = (_base);				\
	} while (0)

/**
 * Exception low-level vector 0x{00-12} wrapper macros
 */
#define CRA_DECL_EXC_HANDLER(vec_hex)				\
	"\n"							\
	"\t.section	.text\n"				\
	"\t.align	0x10\n"					\
	"\t.global	cr_amd64_exception_"#vec_hex"\n"	\
	"\tcr_amd64_exception_"#vec_hex":\n"			\
	"\tpushq	$"#vec_hex"\n"				\
	"\tcallq	cr_amd64_exception\n"			\
	"\taddq		$0x10,	%rsp\n"				\
	"\tiretq\n"
#define CRA_DECL_EXC_HANDLER_NOCODE(vec_hex)			\
	"\n"							\
	"\t.section	.text\n"				\
	"\t.align	0x10\n"					\
	"\t.global	cr_amd64_exception_"#vec_hex"\n"	\
	"\tcr_amd64_exception_"#vec_hex":\n"			\
	"\tpushq	$0x00\n"				\
	"\tpushq	$"#vec_hex"\n"				\
	"\tcallq	cr_amd64_exception\n"			\
	"\taddq		$0x10,	%rsp\n"				\
	"\tiretq\n"
#define CRA_DEF_EXC_HANDLER(vec_hex)				\
	void cr_amd64_exception_##vec_hex(void)



/*
 * AMD64-specific logic
 */

size_t cr_amd64_cpuid_page_size_from_level(int level);
unsigned char cr_amd64_inb(unsigned short port);
int cr_amd64_init_gdt(struct cr_host_state *state);
int cr_amd64_init_idt(struct cr_host_state *state);
void cr_amd64_init_page_ent(struct cra_page_ent *pe, uintptr_t pfn_base, enum cra_pe_bits extra_bits, int pages_nx, int level, int map_direct);
void cr_amd64_msleep(unsigned ns);
void cr_amd64_outb(unsigned short port, unsigned char byte);
#endif /* !_AMD64DEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
