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

static const char *crp_clear_cpu_reg_names[] = {CRC_CPU_REGS_NAMES};

/*
 * XXX
 */

/**
 * cr_clear_clear() - zero-fill RAM
 *
 * Return: Nothing
 */

static void crp_clear_clear_block(uintptr_t va_base, size_t qwords) {
	uintptr_t clear_qword;

	clear_qword = 0x0ULL;
	__asm volatile(
		"\tcld\n"
		"\tmovq		%[qword],	%%rax\n"
		"\tmovq		%[count],	%%rcx\n"
		"\tmovq		%[va],		%%rdi\n"
		"\trep		stosq\n"
		:: [qword] "r"(clear_qword), [count] "r"(qwords), [va] "r"(va_base)
		:  "rax", "rcx","rdi", "flags");
}

static void crp_clear_halt(uintptr_t va_vga_cur) {
#if defined(DEBUG)
	cr_clear_vga_print_cstr(&va_vga_cur, "halting CPU#0.", 0x1f, 1);
	__asm(
		"\t1:	hlt\n"
		"\t	jmp	1b\n");
#else
	cr_clear_vga_print_cstr(&va_vga_cur, "resetting CPU#0.", 0x1f, 1);
	__asm(
		"\tud2\n");
#endif /* defined(DEBUG) */
}

void cr_clear_clear(void)
{
	uintptr_t va_cur, va_vga_cur;
	struct crh_litem *litem, *litem_next;
	struct cra_page_tbl_desc *item;

	va_vga_cur = (uintptr_t)cr_host_state.clear_vga;
	va_cur = 0x0ULL;

	for (litem = (struct crh_litem *)cr_host_state.clear_va_lrsvd;
			litem; litem = litem_next) {
		litem_next = litem->next;
		item = (struct cra_page_tbl_desc *)&litem->item;
		if (item->va_hi > va_cur) {
			crp_clear_clear_block(va_cur, (item->va_hi - va_cur) / 8);
		}
		va_cur = item->va_hi + 1;
		cr_clear_vga_print_cstr(&va_vga_cur, ".", 0x1f, 1);
	}
	if (va_cur < cr_host_state.clear_va_top) {
		crp_clear_clear_block(va_cur, (cr_host_state.clear_va_top - va_cur) / 8);
		cr_clear_vga_print_cstr(&va_vga_cur, ".", 0x1f, 1);
	}
	cr_clear_vga_print_cstr(&va_vga_cur, "done, ", 0x1f, 1);
	/* XXX clear rsvd pages here! */
	crp_clear_halt(va_vga_cur);
}

/**
 * cr_clear_cpu_dump_regs() - generic exception handler
 *
 * Return: Nothing
 */

void cr_clear_cpu_dump_regs(struct crc_cpu_regs *cpu_regs)
{
	uintptr_t va_vga_cur;
	uintptr_t *preg;
	const char **preg_name;
	size_t nrow, ncol;

	preg = (uintptr_t *)cpu_regs;
	preg_name = crp_clear_cpu_reg_names;
	va_vga_cur = (uintptr_t)cr_host_state.clear_vga;
	for (nrow = 0; nrow < 25; nrow++) {
		for (ncol = 0; ncol < 80; ncol += 8) {
			cr_clear_vga_print_cstr(&va_vga_cur, "        ", 0x1f, 8);
		}
	}
	va_vga_cur = (uintptr_t)cr_host_state.clear_vga;
	for (nrow = 0; nrow < (sizeof(*cpu_regs) / sizeof(uintptr_t));
			nrow += 4) {
		for (ncol = 0; ncol < 4; ncol++) {
			if (!preg_name[nrow + ncol][0]) {
				va_vga_cur += 20 * 2;
			} else {
				cr_clear_vga_print_cstr(&va_vga_cur, preg_name[nrow + ncol], 0x1f, 20);
			}
		}
		if ((va_vga_cur & 0xfff) % (80 * 2)) {
			va_vga_cur = (va_vga_cur & ~0xfff) |
				((va_vga_cur & 0xfff) +
					((80 * 2) - ((va_vga_cur & 0xfff) % (80 * 2))));
		}
		for (ncol = 0; ncol < 4; ncol++) {
			if (!preg_name[nrow + ncol][0]) {
				va_vga_cur += 20 * 2;
			} else {
				cr_clear_vga_print_reg(&va_vga_cur, preg[nrow + ncol], 0x1f, 20);
			}
		}
		if ((va_vga_cur & 0xfff) % (80 * 2)) {
			va_vga_cur = (va_vga_cur & ~0xfff) |
				((va_vga_cur & 0xfff) +
					((80 * 2) - ((va_vga_cur & 0xfff) % (80 * 2))));
		}
	}
}

/**
 * XXX
 *
 * Return: Nothing
 */

static void crp_clear_cpu_entry(void) {
	cr_clear_vga_reset();
	cr_clear_vga_clear();
	cr_clear_clear();
}
void cr_clear_cpu_entry(void)
{
	cr_host_cpu_stop_all();
	cr_clear_cpu_setup(crp_clear_cpu_entry);
}

/**
 * cr_clear_cpu_exception() - generic exception handler
 *
 * Return: Nothing
 */

void cr_clear_cpu_exception(struct crc_cpu_regs *cpu_regs)
{
	cr_clear_cpu_dump_regs(cpu_regs);
}

/**
 * cr_clear_cpu_setup() - setup CPU
 *
 * Return: Nothing
 */

void cr_clear_cpu_setup(void (*fn)(void))
{
	CRA_INIT_CR3(&cr_host_state.clear_cr3, CRA_CR3_WRITE_THROUGH,
		cr_host_virt_to_phys((uintptr_t)cr_host_state.clear_pml4));
	CRA_INIT_GDTR(&cr_host_state.clear_gdtr, (uintptr_t)cr_host_state.clear_gdt,
		sizeof(struct cra_gdt_ent) * 3);
	CRA_INIT_IDTR(&cr_host_state.clear_idtr, (uintptr_t)cr_host_state.clear_idt,
		PAGE_SIZE - 1);
	__asm volatile(
		/*
		 * %rax:	cr_host_state.clear_cr3
		 * %rbx:	&cr_host_state.clear_gdtr
		 * %rcx:	&cr_host_state.clear_idtr
		 * %rdx:	&cr_host_state.clear_stack[-1]
		 * %rsi:	fn
		 * %r8:		%cr4; [DEFG]S segment selector 1
		 * %r9:		%cr4 &= ~(PGE bit)
		 */
		"\tcli\n"					/* Disable interrupts */
		"\tcld\n"					/* Clear direction flag */
		"\tmovq		%[cr3],		%%rax\n"
		"\tmovq		%[gdtr],	%%rbx\n"
		"\tmovq		%[idtr],	%%rcx\n"
		"\tmovq		%[stack_top],	%%rdx\n"
		"\tmovq		%[fn_next],	%%rsi\n"
		"\tmovq		%%cr4,		%%r8\n"
		"\tmovq		%%r8,		%%r9\n"		/* Copy original CR4 value */
		"\tandb		$0x7f,		%%r9b\n"	/* Clear PGE bit */
		"\tmovq		%%r9,		%%cr4\n"	/* Disable PGE */
		"\tmovq		%%rax,		%%cr3\n"	/* Set CR3 */
		"\tmovq		%%r8,		%%cr4\n"	/* Enable PGE */
		"\tlgdtq	(%%rbx)\n"
		"\tmovq		$0x00,		%%r8\n"
		"\tmovq		%%r8,		%%ds\n"
		"\tmovq		%%r8,		%%es\n"
		"\tmovq		%%r8,		%%fs\n"
		"\tmovq		%%r8,		%%gs\n"
		"\tmovq		%%r8,		%%ss\n"
		"\tmovq		%%rdx,		%%rsp\n"
		"\tpushq	$0x00\n"
		"\tpushq	%%rdx\n"
		"\tpushq	$0x10\n"
		"\tpushq	$crp_clear_start\n"
		"\tlretq\n"
		"crp_clear_start:\n"
		"\tlidtq	(%%rcx)\n"
		"\taddq		$0x10,		%%rsp\n"
		"\tandq		$-0x10,		%%rsp\n"
		"\tcallq	*%%rsi\n"
		:: [cr3] "r"(cr_host_state.clear_cr3),
		   [gdtr] "r"(&cr_host_state.clear_gdtr),
		   [idtr] "r"(&cr_host_state.clear_idtr),
		   [stack_top] "r"(&cr_host_state.clear_stack[CRHS_STACK_PAGES * (PAGE_SIZE / sizeof(cr_host_state.clear_stack[0]))]),
		   [fn_next] "r"(fn)
		: "rax", "rbx", "rcx", "rdx", "rsi", "memory");
}

/**
 * XXX
 */

void cr_clear_vga_clear(void)
{
	uintptr_t va_vga_cur, va_vga_limit;
	for (va_vga_cur = (uintptr_t)cr_host_state.clear_vga,
			va_vga_limit = va_vga_cur + (80 * 25 * 2);
			va_vga_cur < va_vga_limit;
			va_vga_cur += 8) {
		*(unsigned long *)va_vga_cur = 0x1f001f001f001f00;
	}
}

/**
 * XXX
 */

void cr_clear_vga_print_cstr(uintptr_t *pva_vga_cur, const char *str, unsigned char attr, size_t align)
{
	unsigned char *p = (unsigned char *)*pva_vga_cur;
	while (*str) {
		*p++ = *str++;
		*p++ = attr;
	}
	if (((uintptr_t)p & 0xfff) % (align * 2)) {
		p = (unsigned char *)(((uintptr_t)p & ~0xfff) |
			(((uintptr_t)p & 0xfff) +
				((align * 2) - (((uintptr_t)p & 0xfff) % (align * 2)))));
	}
	*pva_vga_cur = (uintptr_t)p;
}

/**
 * XXX
 */

void cr_clear_vga_print_reg(uintptr_t *pva_vga_cur, uintptr_t reg, unsigned char attr, size_t align)
{
	unsigned char *p = (unsigned char *)*pva_vga_cur;
	size_t nbyte;
	unsigned char byte;
	static unsigned char crdpr_tbl[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f',
	};
	for (nbyte = 8; nbyte > 0; nbyte--) {
		byte = (reg >> ((nbyte - 1) * 8)) & 0xff;
		*p++ = crdpr_tbl[(byte >> 4) & 0xf];
		*p++ = attr;
		*p++ = crdpr_tbl[(byte >> 0) & 0xf];
		*p++ = attr;
	}
	if (((uintptr_t)p & 0xfff) % (align * 2)) {
		p = (unsigned char *)(((uintptr_t)p & ~0xfff) |
			(((uintptr_t)p & 0xfff) +
				((align * 2) - (((uintptr_t)p & 0xfff) % (align * 2)))));
	}
	*pva_vga_cur = (uintptr_t)p;
}

/**
 * XXX
 */

void cr_clear_vga_reset(void)
{
	/*
	 * Write zero (0) framebuffer offset
	 * [LM]SB to CRTC registers 12-13, resp.
	 */
	cr_amd64_outb(0x3d4, 0x0c);
	cr_amd64_outb(0x3d5, 0x00);
	cr_amd64_outb(0x3d4, 0x0d);
	cr_amd64_outb(0x3d5, 0x00);
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
