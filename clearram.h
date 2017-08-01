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

#ifndef _CLEARRAM_H_
#define _CLEARRAM_H_

#if defined(__linux__)
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>
#include <stdarg.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/smp.h>
#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <stdarg.h>
#else
#error Only Linux and FreeBSD are supported at present.
#endif /* defined(__linux__) || defined(__FreBSD__) */
struct cr_host_state;
#include "mapdef.h"
#include "amd64def.h"
#include "cleardef.h"
#include "hostdef.h"

/**
 * LKM state
 */
#define CRHS_GDT_PAGES		1
#define CRHS_IDT_PAGES		1
#define CRHS_STACK_PAGES	4
#define CRHS_VGA_PFN_BASE	0xb8
#define CRHS_VGA_PAGES		8
struct cr_host_state {
	/* [GI]DT, PML4, stack, and VGA framebuffer VA layout page(s) */
	struct cra_gdt_ent	clear_gdt[CRHS_GDT_PAGES * (PAGE_SIZE / sizeof(struct cra_gdt_ent))] __attribute__((aligned(PAGE_SIZE)));
	struct cra_idt_ent	clear_idt[CRHS_IDT_PAGES * (PAGE_SIZE / sizeof(struct cra_idt_ent))] __attribute__((aligned(PAGE_SIZE)));
	struct cra_page_ent	clear_pml4[512] __attribute__((aligned(PAGE_SIZE)));
	uintptr_t		clear_stack[CRHS_STACK_PAGES * (PAGE_SIZE / sizeof(uintptr_t))] __attribute__((aligned(PAGE_SIZE)));
	unsigned char		clear_vga[CRHS_VGA_PAGES * (PAGE_SIZE / sizeof(unsigned char))] __attribute__((aligned(PAGE_SIZE)));

	/* CR3, [GI]DTR registers, and exception wrappers base VA */
	struct cra_cr3		clear_cr3 __attribute__((aligned(0x8)));
	struct cra_gdtr_bits	clear_gdtr __attribute__((aligned(0x10)));
	struct cra_idtr_bits	clear_idtr __attribute__((aligned(0x10)));
	uintptr_t		clear_exc_handlers_base;

	/* VA range of ELF image in-core and current top VA in map */
	uintptr_t		clear_image_base;
	size_t			clear_image_npages;
	uintptr_t		clear_va_top;

	/* VA base address of reserved PFN list */
	uintptr_t		clear_va_lrsvd;

#if defined(__linux__)
	/* Character device node class, device pointer, and major number */
	struct class *		host_cdev_class;
	struct device *		host_cdev_device;
	int			host_cdev_major;

 	/* OS-specific character device node file operations */
	struct file_operations	host_cdev_fops;
#elif defined(__FreeBSD__)
	/* Character device pointer. */
	struct cdev *		host_cdev_device;

 	/* OS-specific character device node file operations */
	struct cdevsw		host_cdev_fops;
#endif /* defined(__linux__) || defined(__FreBSD__) */

	/* cr_pmap_walk() parameters */
	struct crh_pmap_walk_params
				host_pmap_walk_params;

	/* List of page table descriptors */
	struct crh_list		host_lpage_tbl_desc;
};
extern struct cr_host_state	cr_host_state;
#endif /* !_CLEARRAM_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
