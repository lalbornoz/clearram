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

#if defined(__linux__)
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>
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
#else
#error Only Linux and FreeBSD are supported at present.
#endif /* defined(__linux__) || defined(__FreBSD__) */
#ifndef __LP64__
#error Only x86_64 is supported at present.
#elif PAGE_SIZE != 0x1000
#error Only 4 KB pages are supported at present.
#else
#include "amd64def.h"
#include "mapdef.h"
#include "rtldef.h"
#include "osdef.h"
#endif /* !__LP64__ || PAGE_SIZE != 4096 */

/* 
 * Public subroutines and variables
 */
void __attribute__((aligned(PAGE_SIZE))) cr_clear(void);

/* Virtual address of Page Map Level 4 page */
extern struct page_ent *cr_pml4;

/* Virtual addresses of exception debugging IDT page,
 * stack page, and framebuffer pages base */
extern uintptr_t cr_debug_idt;
extern uintptr_t cr_debug_stack;
extern uintptr_t cr_debug_vga;

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
