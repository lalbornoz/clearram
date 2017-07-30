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

#ifndef _OSDEF_H_
#define _OSDEF_H_

/* 
 * OS-dependent data structures
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
 * OS-dependent preprocessor macros and subroutines
 */

/**
 * Platform- & type-independent qsort
 */
#if defined(__linux__)
#define CR_SORT(a, b, c, d)	sort(a, b, c, d, NULL)
#elif defined(__FreeBSD__)
#define CR_SORT(a, b, c, d)	qsort(a, b, c, d)
#endif /* defined(__linux__) || defined(__FreBSD__) */

/**
 * Platform-independent {assert,printk}()
 */
#if defined(__linux__)
#define CR_ASSERT(x, ...)	BUG_ON(!(x))
#define CR_ASSERT_CHKRNGE(base, limit, cur)\
				CR_ASSERT(((uintptr_t)(cur) >= (uintptr_t)(base)) && ((uintptr_t)(cur) < (uintptr_t)(limit)))
#define CR_ASSERT_ISALIGN(base, block_size)\
				CR_ASSERT((block_size) && !((uintptr_t)(base) & ((block_size) - 1)))
#define CR_ASSERT_NOTNULL(x)	CR_ASSERT((x))
#define CR_ASSERT_TRYADD(base, limit, offset)\
				CR_ASSERT(((uintptr_t)(limit) >= (uintptr_t)(base)) && ((uintptr_t)(limit) - (uintptr_t)(base)) >= (uintptr_t)(offset))
#define CR_ASSERT_TRYSUB(base, cur, delta)\
				CR_ASSERT(((uintptr_t)(cur) >= (uintptr_t)(base)) && ((uintptr_t)(cur) - (uintptr_t)(base)) >= (uintptr_t)(delta))
#define CR_PRINTK(x, ...)	printk(KERN_INFO (x), ##__VA_ARGS__)
#elif defined(__FreeBSD__)
#define CR_ASSERT(x, y)		KASSERT((x), (y))
#define CR_ASSERT_CHKRNGE(base, limit, cur)\
				CR_ASSERT(((uintptr_t)(cur) >= (uintptr_t)(base)) && ((uintptr_t)(cur) < (uintptr_t)(limit)),\
					("%s: base=%p, limit=%p, cur=%p", (uintptr_t)(base), (uintptr_t)(limit), (uintptr_t)(cur)))
#define CR_ASSERT_ISALIGN(base, block_size)\
				CR_ASSERT((block_size) && !((uintptr_t)(base) & ((block_size) - 1)),\
					("%s: base=%p, block_size=%p", (uintptr_t)(base), (uintptr_t)(block_size)))
#define CR_ASSERT_NOTNULL(x)	CR_ASSERT((x), ("%s: !"#(x), func, (x)))
#define CR_ASSERT_TRYADD(base, limit, offset)\
				CR_ASSERT(((uintptr_t)(limit) >= (uintptr_t)(base)) && ((uintptr_t)(limit) - (uintptr_t)(base)) >= (uintptr_t)(offset),\
					("%s: base=%p - limit=%p < offset=%p", func, (uintptr_t)(base), (uintptr_t)(limit), (uintptr_t)(offset)
#define CR_ASSERT_TRYSUB(base, cur, delta)\
				CR_ASSERT(((uintptr_t)(cur) >= (uintptr_t)(base)) && ((uintptr_t)(cur) - (uintptr_t)(base)) >= (uintptr_t)(delta),\
					("%s: cur=%p - base=%p < delta=%p", func, (uintptr_t)(cur), (uintptr_t)(base), (uintptr_t)(delta)
#define CR_PRINTK(x, ...)	printf((x), ##__VA_ARGS__)
#endif /* defined(__linux__) || defined(__FreeBSD__) */

#if defined(__linux__)
ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)));
#ifdef CONFIG_SMP
void cr_cpu_stop_one(void *info);
#endif /* CONFIG_SMP */
void cr_free(void *p, void (*pfree)(const void *));
int cr_map_init(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *));
#elif defined(__FreeBSD__)
d_write_t __attribute__((noreturn)) cr_cdev_write;
void cr_free(void *p, void *unused);
int cr_map_init(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void *unused);
#endif /* defined(__linux__) || defined(__FreBSD__) */

void cr_cpu_stop_all(void);
void cr_exit(struct clearram_exit_params *params);
int cr_init_cdev(struct clearram_exit_params *params);
int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit);
uintptr_t cr_virt_to_phys(uintptr_t va);

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
#endif /* _OSDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
