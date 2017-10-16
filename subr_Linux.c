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

/**
 * cr_host_cdev_init() - create character device node and related structures
 *
 * Return: 0 on success, <0 otherwise
 */

int cr_host_cdev_init(struct cr_host_state *state)
{
	CRH_VALID_PTR(state);
	state->host_cdev_major = register_chrdev(0, "clearram",
		&state->host_cdev_fops);
	if (state->host_cdev_major < 0) {
		return state->host_cdev_major;
	}
	state->host_cdev_class = class_create(THIS_MODULE, "clearram");
	if (IS_ERR(state->host_cdev_class)) {
		unregister_chrdev(state->host_cdev_major, "clearram");
		return PTR_ERR(state->host_cdev_class);
	}
	state->host_cdev_device = device_create(state->host_cdev_class,
		NULL, MKDEV(state->host_cdev_major, 0), NULL, "clearram");
	if (IS_ERR(state->host_cdev_device)) {
		unregister_chrdev(state->host_cdev_major, "clearram");
		class_destroy(state->host_cdev_class);
		return PTR_ERR(state->host_cdev_device);
	} else {
		return 0;
	}
}

/**
 * cr_host_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

ssize_t __attribute__((noreturn)) cr_host_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)))
{
	cr_clear_cpu_entry();
	__builtin_unreachable();
}

#if defined(CONFIG_SMP)
/**
 * crp_host_cpu_stop_one() - stop single CPU with serialisation
 * @info:	pointer to cr_cpu_stop_one() parameters
 *
 * Return: Nothing
 */
static void crp_host_cpu_stop_one(void *info) {
	struct crh_stop_cpu_params *params;

	__asm(
		"\t	cli\n");
	params = info;
	spin_lock(&params->lock);
	params->ncpus_stopped++;
	spin_unlock(&params->lock);
	__asm(
		"\t1:	hlt\n"
		"\t	jmp 1b\n");
}
#endif /* defined(CONFIG_SMP) */

/**
 * cr_host_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

void cr_host_cpu_stop_all(void)
{
#if defined(CONFIG_SMP)
	int this_cpu;
	struct crh_stop_cpu_params csc_params;
	int ncpu_this, ncpus, ncpu, ncpus_stopped;

	this_cpu = get_cpu();
	spin_lock_init(&csc_params.lock);
	csc_params.ncpus_stopped = 0;
	for (ncpu = 0, ncpu_this = smp_processor_id(), ncpus = num_online_cpus();
			ncpu < ncpus; ncpu++) {
		if (ncpu != ncpu_this) {
			smp_call_function_single(ncpu, crp_host_cpu_stop_one, &csc_params, 0);
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
 * cr_host_lkm_exit() - kernel module exit point
 *
 * Return: Nothing
 */

void cr_host_lkm_exit(void)
{
	if (cr_host_state.host_cdev_device) {
		device_destroy(cr_host_state.host_cdev_class,
			MKDEV(cr_host_state.host_cdev_major, 0));
	}
	if (cr_host_state.host_cdev_class) {
		class_destroy(cr_host_state.host_cdev_class);
	}
	if (cr_host_state.host_cdev_major) {
		unregister_chrdev(cr_host_state.host_cdev_major, "clearram");
	}
	cr_host_map_free(cr_host_state.clear_pml4, cr_host_vmfree);
}

/**
 * cr_host_vmalloc() - allocate memory items from kernel heap
 *
 * Return: >0 on success, 0 otherwise
 */

void *cr_host_vmalloc(size_t nitems, size_t size)
{
	uintptr_t p;

	CRH_VALID_PTR(nitems);
	CRH_VALID_PTR(size);
	p = (uintptr_t)vmalloc(nitems * size);
	if (p) {
		memset((void *)p, 0, nitems * size);
	}
	return (void *)p;
}

/**
 * cr_host_vmfree() - release memory items from kernel heap
 *
 * Return: >0 on success, 0 otherwise
 */

void cr_host_vmfree(void *p)
{
	vfree(p);
}

/**
 * cr_host_pmap_walk() - walk physical memory
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 * @psection_cur:	optional pointer to current address of section
 *
 * Return next range of continguous physical RAM on the system
 * The walk parameters establish the context of the iteration and must
 * be initialised prior to each walk.
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

int cr_host_pmap_walk(struct crh_pmap_walk_params *params, uintptr_t *psection_base, uintptr_t *psection_limit, uintptr_t *psection_cur)
{
	int err;
	unsigned long flags_mask;

	CRH_VALID_PTR(params);
	CRH_VALID_PTR(psection_base);
	CRH_VALID_PTR(psection_limit);
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
		if ((params->res_cur->flags & flags_mask) == flags_mask) {
			*psection_base = params->res_cur->start >> 12;
			*psection_limit = (params->res_cur->end + 1) >> 12;
			CRH_ASSERT(*psection_limit > *psection_base, "");
			err = 1;
			if (psection_cur) {
				*psection_cur = *psection_base;
			}
			CRH_PRINTK_DEBUG("found RAM section 0x%013lx..0x%013lx\n",
				*psection_base, *psection_limit);
		}
	}
	return err;
}

/**
 * cr_host_virt_to_phys() - translate virtual address to physical address (PFN) using host page tables
 *
 * Return: Physical address (PFN) mapped by virtual address
 */

uintptr_t cr_host_virt_to_phys(uintptr_t va)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	uintptr_t pe_val, pfn;

	pgd = pgd_offset(current->mm, va);
	pud = pud_offset(pgd, va);
	CRH_VALID_PTR(pud);
	pe_val = pud_val(*pud);
	if (pe_val & _PAGE_PSE) {
		pfn = ((struct cra_page_ent_1G *)&pe_val)->pfn_base;
		return (pfn << (9 + 9)) | (CRA_VA_TO_PD_IDX(va) * CRA_PS_2M) | (CRA_VA_TO_PT_IDX(va));
	} else {
		pmd = pmd_offset(pud, va);
		CRH_VALID_PTR(pmd);
		pe_val = pmd_val(*pmd);
	}
	if (pe_val & _PAGE_PSE) {
		pfn = ((struct cra_page_ent_2M *)&pe_val)->pfn_base;
		return (pfn << 9) | (CRA_VA_TO_PT_IDX(va));
	} else {
		pte = pte_offset_map(pmd, va);
		CRH_VALID_PTR(pte);
		pe_val = pte_val(*pte);
	}
	return ((struct cra_page_ent *)&pe_val)->pfn_base;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
