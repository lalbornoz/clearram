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
#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#else
#error Only Linux and FreeBSD are supported at present.
#endif /* defined(__linux__) || defined(__FreBSD__) */

#ifndef __LP64__
#error Only x86_64 is supported at present.
#endif /* !__LP64__ */
#if PAGE_SIZE != 4096
#error Only 4 KB pages are supported at present.
#endif /* PAGE_SIZE != 4096 */

#include "amd64def.h"

/* 
 * Data structures
 */

/*
 * cr_exit() parameters
 */
struct clearram_exit_params {
	void *map;
#if defined(__linux__)
	/* Function pointer to either of {vfree,kfree}(), releasing cr_map{,_pfns}. */
	void (*map_free_fn)(const void *);

	/* Character device node major number, class, and device pointers. */
	int cdev_major;
	struct class *cdev_class;
	struct device *cdev_device;
#elif defined(__FreeBSD__)
	/* (unused) */
	void *map_free_fn;

	/* Character device pointer. */
	struct cdev *cdev_device;
#endif /* defined(__linux__) || defined(__FreBSD__) */
};

/*
 * cr_stop_cpu() parameters
 */
#if defined(__linux__) && defined(CONFIG_SMP)
struct csc_params {
	spinlock_t	lock;
	int		ncpus_stopped;
};
#endif /* defined(__linux__) && defined(CONFIG_SMP) */

/*
 * OS-dependent subroutines
 */

/*
 * Round up 64-bit integer ll to next multiple of 32-bit integer d.
 * (from linux-source-4.7/include/{asm-generic/div64.h,linux/kernel.h}.
 */
#define cr_do_div(n,base) ({					\
	uint32_t __base = (base);				\
	uint32_t __rem;						\
	__rem = ((uint64_t)(n)) % __base;			\
	(n) = ((uint64_t)(n)) / __base;				\
	__rem;							\
 })
#define CR_DIV_ROUND_UP_ULL(ll,d) \
	({ unsigned long long _tmp = (ll)+(d)-1; cr_do_div(_tmp, d); _tmp; })
#if defined(__linux__)
int cr_pmem_walk_nocombine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit);
ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)));
int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *));
#ifdef CONFIG_SMP
void cr_cpu_stop_one(void *info);
#endif /* CONFIG_SMP */
void cr_free(void *p, void (*pfree)(const void *));
#define CR_SORT(a, b, c, d)	sort(a, b, c, d, NULL)
#elif defined(__FreeBSD__)
d_write_t __attribute__((noreturn)) cr_cdev_write;
int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void *unused);
void cr_free(void *p, void *unused);
#define CR_SORT(a, b, c, d)	qsort(a, b, c, d)
#endif /* defined(__linux__) || defined(__FreBSD__) */
int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
uintptr_t cr_virt_to_phys(uintptr_t va);
int cr_init_cdev(struct clearram_exit_params *params);
void cr_cpu_stop_all(void);
void cr_exit(struct clearram_exit_params *params);

/* 
 * Kernel module {entry,event} point subroutines
 */
void __attribute__((aligned(PAGE_SIZE))) cr_clear(void);

/* 
 * Public variables
 */

#if defined(__linux__)
/* Character device node file operations */
extern struct file_operations cr_cdev_fops;
#elif defined(__FreeBSD__)
/* Character device node file operations */
extern struct cdevsw cr_cdev_fops;

/* Declare malloc(9) type */
MALLOC_DECLARE(M_CLEARRAM);
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
