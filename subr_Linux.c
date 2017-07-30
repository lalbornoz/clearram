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

#if defined(CONFIG_SMP)
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
	CR_ASSERT_NOTNULL(params);
	spin_lock(&params->lock);
	params->ncpus_stopped++;
	spin_unlock(&params->lock);
	__asm volatile(
		"\t1:	hlt\n"
		"\t	jmp 1b\n");
}
#endif /* defined(CONFIG_SMP) */

/**
 * cr_free() - free previously allocated memory
 *
 * Return: Nothing
 */

void cr_free(void *p, void (*pfree)(const void *))
{
	CR_ASSERT_NOTNULL(p);
	if (pfree) {
		pfree(p);
	} else {
		kzfree(p);
	}
}

/**
 * cr_map_init() - allocate, zero-fill, and map memory
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_map_init(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *))
{
	CR_ASSERT_NOTNULL(pbase);
	CR_ASSERT_NOTNULL(count);
	*pbase = kzalloc(count, GFP_KERNEL | __GFP_NOWARN);
	if (!*pbase) {
		if (pfree) {
			*pfree = &vfree;
			*pbase = vmalloc(count);
			memset(*pbase, 0, count);
			if (pcur) {
				*pcur = *pbase;
			}
			if (plimit) {
				CR_ASSERT_TRYADD(*pbase, (uintptr_t)-1, count);
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
		CR_ASSERT_TRYADD(*pbase, (uintptr_t)-1, count);
		*plimit = (uintptr_t)*pbase + count;
	}
	return 0;
}


/**
 * cr_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

void cr_cpu_stop_all(void)
{
#if defined(CONFIG_SMP)
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
#endif /* defined(CONFIG_SMP) */
}

/**
 * cr_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void cr_exit(struct clearram_exit_params *params)
{
	if (!params) {
		return;
	}
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

/**
 * cr_init_cdev() - create character device node and related structures
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_init_cdev(struct clearram_exit_params *params)
{
	CR_ASSERT_NOTNULL(params);
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
 * cr_pmem_walk_combine() - walk physical memory, combining sections
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return next range of continguous physical RAM on the system.
 * The walk parameters establish the context of the iteration and must
 * be initialised prior to each walk.
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
{
	int err;
	unsigned long flags_mask;

	CR_ASSERT_NOTNULL(params);
	CR_ASSERT_NOTNULL(psection_base);
	CR_ASSERT_NOTNULL(psection_limit);
	if (params->restart) {
		params->res_cur = iomem_resource.child;
		params->restart = 0;
	}
	if (!params->res_cur) {
		return 0;
	} else
	for (err = 0; params->res_cur && !err;
			params->res_cur = params->res_cur->sibling) {
		flags_mask = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		CR_ASSERT_TRYADD(params->res_cur->end, (uintptr_t)-1, 1);
		if ((params->res_cur->flags & flags_mask) == flags_mask) {
			*psection_base = params->res_cur->start >> 12;
			*psection_limit = (params->res_cur->end + 1) >> 12;
			err = 1;
			CR_PRINTK("found RAM section 0x%013lx..0x%013lx\n",
				*psection_base, *psection_limit);
		}
	}
	return err;
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
	uintptr_t pe_val, pfn;

	pgd = pgd_offset(current->mm, va);
	pud = pud_offset(pgd, va);
	CR_ASSERT_NOTNULL(pud);
	pe_val = pud_val(*pud);
	if (pe_val & _PAGE_PSE) {
		pfn = ((struct page_ent_1G *)&pe_val)->pfn_base;
		return (pfn << (9 + 9)) | (CR_VA_TO_PD_IDX(va) * CMP_PS_2M) | (CR_VA_TO_PT_IDX(va));
	} else {
		pmd = pmd_offset(pud, va);
		CR_ASSERT_NOTNULL(pmd);
		pe_val = pmd_val(*pmd);
	}
	if (pe_val & _PAGE_PSE) {
		pfn = ((struct page_ent_2M *)&pe_val)->pfn_base;
		return (pfn << 9) | (CR_VA_TO_PT_IDX(va));
	} else {
		pte = pte_offset_map(pmd, va);
		CR_ASSERT_NOTNULL(pte);
		pe_val = pte_val(*pte);
	}
	return ((struct page_ent *)&pe_val)->pfn_base;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
