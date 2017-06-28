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
#include "clearram.h"

/**
 * cr_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

int __attribute__((noreturn)) cr_cdev_write(struct cdev *dev __unused, struct uio *uio __unused, int ioflag __unused)
{
	cr_clear();
	__builtin_unreachable();
}

/**
 * cr_init_map() - allocate, zero-fill, and map memory
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void *unused)
{
	*pbase = malloc(count, M_CLEARRAM, M_WAITOK | M_ZERO);
	if (!*pbase) {
		return -ENOMEM;
	} else {
		return 0;
	}
}

/**
 * cr_free() - free previously allocated memory
 *
 * Return: Nothing
 */

void cr_free(void *p, void *unused)
{
	free(p, M_CLEARRAM);
}

/**
 * cr_pmem_walk_combine() - walk physical memory
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return next contiguous set of physical memory on the system.
 * The walk parameters establish the context of the iteration and must
 * be initialised prior to each walk.
 *
 * Return: 0 if no physical memory remains, 1 otherwise
 */

int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
{
	if (!phys_avail[params->nid + 1]) {
		*psection_base = *psection_limit = 0;
		return params->nid = 0, 0;
	} else {
		*psection_base = phys_avail[params->nid];
		*psection_limit = phys_avail[params->nid + 1];
		return params->nid += 2, 1;
	}
}

/**
 * cr_virt_to_phys() - translate virtual address to physical address (PFN) using host page tables
 *
 * Return: Physical address (PFN) mapped by virtual address
 */

uintptr_t cr_virt_to_phys(uintptr_t va)
{
	return vtophys(va);
}

/**
 * cr_init_cdev() - create character device node and related structures
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_init_cdev(struct clearram_exit_params *params)
{
	return make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		&params->cdev_device, &cr_cdev_fops, 0,
		UID_ROOT, GID_WHEEL, 0600, "clearram");
}

/**
 * cr_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

void cr_cpu_stop_all(void)
{
}

/**
 * cr_exit() - OS-dependent kernel module exit point
 * 
 * Return: Nothing
 */

void cr_exit(struct clearram_exit_params *params)
{
	if (params->cdev_device) {
		destroy_dev(params->cdev_device);
	}
	if (params->map) {
		cr_free(params->map, params->map_free_fn);
	}
}


/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
