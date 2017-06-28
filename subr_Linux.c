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

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>
#include "clearram.h"

/**
 * cr_pmem_walk_nocombine() - walk physical memory without combining sections
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return next contiguous section of physical memory on the system,
 * up to PAGES_PER_SECTION in size, from the current or next available
 * node given a NUMA configuration. Absent and non-RAM sections are skipped.
 * The walk parameters establish the context of the iteration and must
 * be initialised prior to each walk.
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

int cr_pmem_walk_nocombine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
{
	struct mem_section *ms;

	while (params->nid < MAX_NUMNODES) {
#ifdef CONFIG_NUMA
		if (!NODE_DATA(params->nid)) {
			params->nid++;
			params->new_nid = 1;
			continue;
		}
#endif /* CONFIG_NUMA */
		if (params->new_nid) {
			params->new_nid = 0;
			params->node_base =
			params->pfn_cur = node_start_pfn(params->nid);
			params->node_limit = params->node_base +
				+ node_spanned_pages(params->nid);
		}
		for (; params->pfn_cur < params->node_limit; params->pfn_cur += PAGES_PER_SECTION) {
			ms = __pfn_to_section(params->pfn_cur);
			if (unlikely(!valid_section(ms))
			||  unlikely(!present_section(ms))
			||  unlikely(!page_is_ram(params->pfn_cur))) {
				continue;
			} else {
				*psection_base = params->pfn_cur;
				*psection_limit = min(params->node_limit,
					params->pfn_cur + PAGES_PER_SECTION);
				params->pfn_cur = *psection_limit;
				return 1;
			}
		}
		params->nid++;
		params->new_nid = 1;
	}
	return 0;
}

/**
 * cr_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)))
{
	cr_clear();
	__builtin_unreachable();
}

/**
 * cr_init_map() - allocate, zero-fill, and map memory
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *))
{
	*pbase = kzalloc(count, GFP_KERNEL | __GFP_NOWARN);
	if (!*pbase) {
		if (pfree) {
			*pfree = &vfree;
			*pbase = vmalloc(count);
			if (pcur) {
				*pcur = *pbase;
			}
			if (plimit) {
				*plimit = (uintptr_t)*pbase + count;
			}
		} else {
			return -ENOMEM;
		}
	} else
	if (pfree) {
		*pfree = &kzfree;
	}
	if (pcur) {
		*pcur = *pbase;
	}
	if (plimit) {
		*plimit = (uintptr_t)*pbase + count;
	}
	memset(*pbase, 0, count);
	return 0;
}

#ifdef CONFIG_SMP
/**
 * cr_cpu_stop_one() - stop single CPU with serialisation
 * @info:	pointer to cr_cpu_stop_one() parameters
 *
 * Return: Nothing
 */

void cr_cpu_stop_one(void *info)
{
	struct csc_params *params;

	__asm volatile(
		"\t	cli\n");
	params = info;
	spin_lock(&params->lock);
	params->ncpus_stopped++;
	spin_unlock(&params->lock);
	__asm volatile(
		"\t1:	hlt\n"
		"\t	jmp 1b\n");
}
#endif /* CONFIG_SMP */

/**
 * cr_free() - free previously allocated memory
 *
 * Return: Nothing
 */

void cr_free(void *p, void (*pfree)(const void *))
{
	if (pfree) {
		pfree(p);
	} else {
		kzfree(p);
	}
}

/**
 * cr_pmem_walk_combine() - walk physical memory, combining sections
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return next contiguous set of physical memory on the system, combining
 * multiple contiguous sections returned by successive cr_pmem_walk_nocombine()
 * calls into one single set.
 * The walk parameters establish the context of the iteration and must
 * be initialised prior to each walk.
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
{
	int err;
	uintptr_t section_base, section_limit;

	if (params->combine_fini) {
		return 0;
	} else
	while ((err = cr_pmem_walk_nocombine(params,
			&section_base, &section_limit)) > 0) {
		if (params->restart) {
			params->restart = 0;
			params->combine_last_base = section_base;
			params->combine_last_limit = section_limit;
		} else
		if (params->combine_last_limit == section_base) {
			params->combine_last_limit = section_limit;
			continue;
		} else {
			*psection_base = params->combine_last_base;
			*psection_limit = section_base;
			params->combine_last_base = section_base;
			params->combine_last_limit = section_limit;
			return 1;
		}
	}
	params->combine_fini = 1;
	if ((err == 0)
	&&  (params->combine_last_limit - params->combine_last_base)) {
		*psection_base = params->combine_last_base;
		*psection_limit = params->combine_last_limit;
		return 1;
	} else {
		return err;
	}
}

/**
 * cr_virt_to_phys() - translate virtual address to physical address (PFN) using host page tables
 *
 * Return: Physical address (PFN) mapped by virtual address
 */

uintptr_t cr_virt_to_phys(uintptr_t va)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct page_ent_1G *pdpe_1G;
	struct page_ent_2M *pde_2M;
	struct page_ent *pe;
	uintptr_t pfn, offset;

	pgd = pgd_offset(current->mm, va);
	pud = pud_offset(pgd, va);
	if (pud_val(*pud) & _PAGE_PSE) {
		pdpe_1G = (struct page_ent_1G *)pud;
		pfn = (uintptr_t)pdpe_1G->pfn_base << 18;
		offset = CR_VA_TO_PD_IDX(va) * 512;
		offset += CR_VA_TO_PT_IDX(va);
		return pfn + offset;
	}
	pmd = pmd_offset(pud, va);
	if (pmd_val(*pmd) & _PAGE_PSE) {
		pde_2M = (struct page_ent_2M *)pmd;
		pfn = (uintptr_t)pde_2M->pfn_base << 9;
		offset = CR_VA_TO_PT_IDX(va);
		return pfn + offset;
	}
	pte = pte_offset_map(pmd, va);
	pe = (struct page_ent *)pte;
	return pe->pfn_base;
}

/**
 * cr_init_cdev() - create character device node and related structures
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_init_cdev(struct clearram_exit_params *params)
{
	params->cdev_major = register_chrdev(0, "clearram", &cr_cdev_fops);
	if (params->cdev_major < 0) {
		return params->cdev_major;
	}
	params->cdev_class = class_create(THIS_MODULE, "clearram");
	if (IS_ERR(params->cdev_class)) {
		unregister_chrdev(params->cdev_major, "clearram");
		return PTR_ERR(params->cdev_class);
	}
	params->cdev_device = device_create(params->cdev_class,
		NULL, MKDEV(params->cdev_major, 0), NULL, "clearram");
	if (IS_ERR(params->cdev_device)) {
		unregister_chrdev(params->cdev_major, "clearram");
		class_destroy(params->cdev_class);
		return PTR_ERR(params->cdev_device);
	} else {
		return 0;
	}
}

/**
 * cr_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

void cr_cpu_stop_all(void)
{
#ifdef CONFIG_SMP
	int this_cpu;
	struct csc_params csc_params;
	int ncpu_this, ncpus, ncpu, ncpus_stopped;

	this_cpu = get_cpu();
	spin_lock_init(&csc_params.lock);
	csc_params.ncpus_stopped = 0;
	for (ncpu = 0, ncpu_this = smp_processor_id(), ncpus = num_online_cpus();
			ncpu < ncpus; ncpu++) {
		if (ncpu != ncpu_this) {
			smp_call_function_single(ncpu, cr_cpu_stop_one, &csc_params, 0);
		}
	}
	do {
		spin_lock(&csc_params.lock);
		ncpus_stopped = csc_params.ncpus_stopped;
		spin_unlock(&csc_params.lock);
	} while (ncpus_stopped < (ncpus - 1));
#endif /* CONFIG_SMP */
}

/**
 * cr_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void cr_exit(struct clearram_exit_params *params)
{
	if (params->cdev_device) {
		device_destroy(params->cdev_class, MKDEV(params->cdev_major, 0));
	}
	if (params->cdev_class) {
		class_destroy(params->cdev_class);
	}
	if (params->cdev_major) {
		unregister_chrdev(params->cdev_major, "clearram");
	}
	if (params->map) {
		cr_free(params->map, params->map_free_fn);
	}
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
