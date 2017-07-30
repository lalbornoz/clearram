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
#if defined(DEBUG)
/**
 * Exception low-level vector 0x{00-12} wrappers
 */
void cr_debug_low_00(void);
void cr_debug_low_01(void);
void cr_debug_low_02(void);
void cr_debug_low_03(void);
void cr_debug_low_04(void);
void cr_debug_low_05(void);
void cr_debug_low_06(void);
void cr_debug_low_07(void);
void cr_debug_low_08(void);
void cr_debug_low_09(void);
void cr_debug_low_0a(void);
void cr_debug_low_0b(void);
void cr_debug_low_0c(void);
void cr_debug_low_0d(void);
void cr_debug_low_0e(void);
void cr_debug_low_0f(void);
void cr_debug_low_10(void);
void cr_debug_low_11(void);
void cr_debug_low_12(void);

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
	uintptr_t pfn, va_this, va_mapped, cs_sel, cs_offset;

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
			memset(idt, 0, PAGE_SIZE);
			__asm volatile(
				"\tmovq	%%cs,	%0\n"
				:"=r"(cs_sel));
			for (nidte = 0; nidte <= 0x12; nidte++) {
				switch (nidte) {
				case 0x00: cs_offset = (uintptr_t)cr_debug_low_00; break;
				case 0x01: cs_offset = (uintptr_t)cr_debug_low_01; break;
				case 0x02: cs_offset = (uintptr_t)cr_debug_low_02; break;
				case 0x03: cs_offset = (uintptr_t)cr_debug_low_03; break;
				case 0x04: cs_offset = (uintptr_t)cr_debug_low_04; break;
				case 0x05: cs_offset = (uintptr_t)cr_debug_low_05; break;
				case 0x06: cs_offset = (uintptr_t)cr_debug_low_06; break;
				case 0x07: cs_offset = (uintptr_t)cr_debug_low_07; break;
				case 0x08: cs_offset = (uintptr_t)cr_debug_low_08; break;
				case 0x09: cs_offset = (uintptr_t)cr_debug_low_09; break;
				case 0x0a: cs_offset = (uintptr_t)cr_debug_low_0a; break;
				case 0x0b: cs_offset = (uintptr_t)cr_debug_low_0b; break;
				case 0x0c: cs_offset = (uintptr_t)cr_debug_low_0c; break;
				case 0x0d: cs_offset = (uintptr_t)cr_debug_low_0d; break;
				case 0x0e: cs_offset = (uintptr_t)cr_debug_low_0e; break;
				case 0x0f: cs_offset = (uintptr_t)cr_debug_low_0f; break;
				case 0x10: cs_offset = (uintptr_t)cr_debug_low_10; break;
				case 0x11: cs_offset = (uintptr_t)cr_debug_low_11; break;
				case 0x12: cs_offset = (uintptr_t)cr_debug_low_12; break;
				}
				CR_INIT_IDTE(&idt[nidte],
					cs_offset, cs_sel, 0,
					IE_TYPE_ATTR_TRAP_GATE	|
					IE_TYPE_ATTR_DPL00	|
					IE_TYPE_ATTR_PRESENT);
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
 * cr_debug_low{,_00-12}() - exception low-level handler and vector 0x{00-12} wrappers
 *
 * Return: Nothing
 */

__asm(
	"\t.align	0x1000\n"
	"\t.global	cr_debug_low\n"
	"\tcr_debug_low:\n"
	"\t1:		hlt\n"
	"\t		jmp	1b\n"
	"\n"
	"\t.global	cr_debug_low_00\n"
	"\tcr_debug_low_00:\n"
	"\tpushq	$0x00\n"
	"\n"
	"\t.global	cr_debug_low_01\n"
	"\tcr_debug_low_01:\n"
	"\tpushq	$0x01\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_02\n"
	"\tcr_debug_low_02:\n"
	"\tpushq	$0x02\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_03\n"
	"\tcr_debug_low_03:\n"
	"\tpushq	$0x03\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_04\n"
	"\tcr_debug_low_04:\n"
	"\tpushq	$0x04\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_05\n"
	"\tcr_debug_low_05:\n"
	"\tpushq	$0x05\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_06\n"
	"\tcr_debug_low_06:\n"
	"\tpushq	$0x06\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_07\n"
	"\tcr_debug_low_07:\n"
	"\tpushq	$0x07\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_08\n"
	"\tcr_debug_low_08:\n"
	"\tpushq	$0x08\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_09\n"
	"\tcr_debug_low_09:\n"
	"\tpushq	$0x09\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0a\n"
	"\tcr_debug_low_0a:\n"
	"\tpushq	$0x0a\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0b\n"
	"\tcr_debug_low_0b:\n"
	"\tpushq	$0x0b\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0c\n"
	"\tcr_debug_low_0c:\n"
	"\tpushq	$0x0c\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0d\n"
	"\tcr_debug_low_0d:\n"
	"\tpushq	$0x0d\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0e\n"
	"\tcr_debug_low_0e:\n"
	"\tpushq	$0x0e\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_0f\n"
	"\tcr_debug_low_0f:\n"
	"\tpushq	$0x0f\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_10\n"
	"\tcr_debug_low_10:\n"
	"\tpushq	$0x10\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_11\n"
	"\tcr_debug_low_11:\n"
	"\tpushq	$0x11\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.global	cr_debug_low_12\n"
	"\tcr_debug_low_12:\n"
	"\tpushq	$0x12\n"
	"\tcallq	cr_debug_low\n"
	"\n"
	"\t.align	0x1000\n"
	"\t.global	cr_debug_low_limit\n"
	"\tcr_debug_low_limit:\n"
	"\t.quad	.\n");
#endif /* defined(DEBUG) */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
