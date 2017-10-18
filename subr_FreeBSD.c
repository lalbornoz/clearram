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
	return make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		&state->cdev_device, &cr_cdev_fops, 0,
		UID_ROOT, GID_WHEEL, 0600, "clearram");
}

/**
 * cr_host_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

int __attribute__((noreturn)) cr_host_cdev_write(struct cdev *dev __unused, struct uio *uio __unused, int ioflag __unused)
{
	cr_clear_clear();
	__builtin_unreachable();
}

/**
 * cr_host_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

void cr_host_cpu_stop_all(void)
{
#if defined(SMP)
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);
	stop_cpus(other_cpus);
#endif /* !defined(SMP) */
}

/**
 * cr_host_lkm_exit() - OS-dependent kernel module exit point
 *
 * Return: Nothing
 */

void cr_host_lkm_exit(struct cr_host_state *state)
{
	if (state->host_cdev_device) {
		destroy_dev(params->host_cdev_device);
	}
}

/**
 * cr_host_vmalloc() - allocate memory items from kernel heap
 *
 * Return: >0 on success, 0 otherwise
 */

uintptr_t cr_host_vmalloc(size_t nitems, size_t size)
{
	uintptr_t p;

	p = (uintptr_t)malloc(nitems * size, M_CLEARRAM, M_ZERO | M_WAITOK);
	return p;
}

/**
 * cr_host_vmfree() - release memory items from kernel heap
 *
 * Return: >0 on success, 0 otherwise
 */

void cr_host_vmfree(void *p)
{
	free(p, M_CLEARRAM);
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

int cr_host_pmap_walk(struct crp_walk_params *params, uintptr_t *psection_base, uintptr_t *psection_limit, uintptr_t *psection_cur)
{
	if (params->restart) {
		params->nid = 0;
	}
	if (phys_avail[params->nid + 1]) {
		*psection_base = phys_avail[params->nid];
		*psection_limit = phys_avail[params->nid + 1];
		if (psection_cur) {
			*psection_cur = *psection_base;
		}
		return params->nid += 2, 1;
	} else {
		return params->restart = 1, 0;
	}
}

/**
 * cr_host_virt_to_phys() - translate virtual address to physical address (PFN) using host page tables
 *
 * Return: Physical address (PFN) mapped by virtual address
 */

uintptr_t cr_host_virt_to_phys(uintptr_t va)
{
	return vtophys(va);
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
