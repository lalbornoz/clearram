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
 * Subroutine prototypes and variables
 */
/* Virtual address of Page Map Level 4 page */
struct page_ent *cr_pml4 = NULL;

/* Virtual addresses of exception debugging IDT page,
 * stack page, and framebuffer pages base */
uintptr_t cr_debug_idt = 0;
uintptr_t cr_debug_stack = 0;
uintptr_t cr_debug_vga = 0;

/* Resources to release when exiting */
static struct clearram_exit_params cr_exit_params = {0,};

#if defined(__FreeBSD__)
int clearram_evhand(struct module *m, int what, void *arg);
#endif /* defined(__FreeBSD__) */
void clearram_exit(void);
int clearram_init(void);

/**
 * OS-specific character device node file operations
 */
#if defined(__linux__)
struct file_operations cr_cdev_fops = {
	.write = cr_cdev_write,
};
#elif defined(__FreeBSD__)
struct cdevsw cr_cdev_fops = {
	.d_version = D_VERSION,
	.d_write = cr_cdev_write,
	.d_name = "clearram",
};
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/**
 * OS-specific LKM fields
 */
#if defined(__linux__)
module_exit(clearram_exit);
module_init(clearram_init);
MODULE_AUTHOR("Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>");
MODULE_DESCRIPTION("clearram");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("clearram");
#elif defined(__FreeBSD__)
moduledata_t clearram_mod = {
	"clearram", clearram_evhand, NULL,
};
MALLOC_DEFINE(M_CLEARRAM, "clearram", "buffer for clearram module");
DECLARE_MODULE(clearram, clearram_mod, SI_SUB_KLD, SI_ORDER_ANY);
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/* 
 * Kernel module {entry,event} point subroutines
 */

#if defined(__FreeBSD__)
/**
 * clearram_evhand() - handle kernel module event
 * @m:		pointer to this module
 * @what:	MOD_LOAD when loading module, MOD_UNLOAD when unloading module
 * @arg:	(unused)
 *
 * Return: 0 on success, >0 otherwise
 */

int clearram_evhand(struct module *m, int what, void *arg __unused)
{
	switch (what) {
	case MOD_LOAD:
		return clearram_init() * -1;
	case MOD_UNLOAD:
		return clearram_exit(), 0;
	default:
		return EOPNOTSUPP;
	}
}
#endif /* defined(__FreeBSD__) */

/**
 * clearram_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void clearram_exit(void)
{
	cr_exit(&cr_exit_params);
}

/**
 * clearram_init() - kernel module entry point
 *
 * Initialise the map on the current processor with page tables mapping
 * physical RAM contiguously at 0x0ULL, skipping page frames allocated to the
 * map itself and to the code pages spanning &cr_clear..cr_clear_limit.
 * The code is mapped both at its current VA as well as at the top of VA. This
 * allows cr_clear() to zero-fill its own pages up to a certain point. The GDT,
 * IDT, and stack are left untouched by cr_clear() and are thus not mapped.
 * Create the character device to allow user-mode to trigger calling cl_clear().
 * 
 * Return: 0 on success, <0 on failure
 */

int clearram_init(void)
{
	int err;
	struct cpw_params cpw_params;
	uintptr_t map_npages, map_npages_max;
	uintptr_t code_base;
	extern uintptr_t cr_clear_limit;
	static struct cmp_params cmp_params;
	size_t npfn;
	uintptr_t va, pfn_block_base, pfn_block_limit;
	int level;

	/*
	 * Initialise parameters
	 * Obtain total amount of page frames on host
	 * Derive max. amount of {PML4,PDP,PD,PT} required to map
	 */

	INIT_CMP_PARAMS(&cmp_params);
	INIT_CPW_PARAMS(&cpw_params);
	map_npages = 0;
	code_base = (uintptr_t)&cr_clear;
	while ((err = cr_pmem_walk_combine(&cpw_params, &pfn_block_base,
			&pfn_block_limit)) == 1) {
		map_npages += (pfn_block_limit - pfn_block_base);
	}
	if (err < 0) {
		goto fail;
	}
	map_npages_max
		    = (CR_DIV_ROUND_UP_ULL(map_npages, (512)))				/* Page Tables */
		    + (CR_DIV_ROUND_UP_ULL(map_npages, (512 * 512)))			/* Page Directories */
		    + (CR_DIV_ROUND_UP_ULL(map_npages, (512 * 512 * 512)))		/* Page Directory Pointer pages */
		    + (1)								/* Page Map Level 4 */
		    + (((cr_clear_limit - code_base) / PAGE_SIZE) * (1 + 1 + 1))	/* {PDP,PD,PT} to map code at top of VA */
		    + (((cr_clear_limit - code_base) / PAGE_SIZE) * (1 + 1 + 1))	/* {PDP,PD,PT} to map code at original VA */
#if defined(DEBUG)
		    + ((1 + 1 + 8 + 1) * (1 + 1 + 1))					/* {PDP,PD,PT} to map {IDT,stack,framebuffer,exception debugging code} page(s) */
#endif /* defined(DEBUG) */
	;

	/*
	 * Initialise map, map filter PFN list, and phys-to-virt map list
	 * Initialise, fill, and numerically sort map filter PFN list
	 */

	err = cr_map_init((void **)&cmp_params.map_base,
		(void **)&cmp_params.map_cur, &cmp_params.map_limit,
		map_npages_max * PAGE_SIZE, &cr_exit_params.map_free_fn);
	if (err < 0) {
		goto fail;
	}
	if ((err = cr_map_init((void **)&cpw_params.filter, NULL, NULL,
			(map_npages_max + 1) * sizeof(uintptr_t), NULL)) < 0) {
		goto fail;
	} else
	for (npfn = 0; npfn < map_npages_max; npfn++) {
		cpw_params.filter[npfn] = cr_virt_to_phys(
			(uintptr_t)cmp_params.map_base + (npfn * PAGE_SIZE));
	}
	cpw_params.filter[npfn] = cr_virt_to_phys(code_base);
	cpw_params.filter_nmax = map_npages_max;
	CR_SORT(cpw_params.filter, map_npages_max + 1, sizeof(uintptr_t),
		&cr_init_pfns_compare);
	err = cr_map_init((void **)&cmp_params.map_phys.map_base,
		(void **)&cmp_params.map_phys.map_cur,
		&cmp_params.map_phys.map_limit,
		map_npages_max * sizeof(struct cr_map_phys_node), NULL);
	if (err < 0) {
		goto fail;
	}

	/*
	 * Set VA to 0x0ULL, initialise PML4 from map heap
	 * Walk physical RAM, skipping map heap page frames
	 * Map consecutive ranges of at least 1 page frame to current VA
	 */

	va = 0x0LL;
	cmp_params.pml4 = cr_pml4 = (struct page_ent *)cmp_params.map_cur;
	cmp_params.map_cur += PAGE_SIZE;
	for (level = CMP_LVL_PDP; level > 0; level--) {
		INIT_CPW_PARAMS(&cpw_params);
		while ((err = cr_pmem_walk_filter(&cpw_params, &pfn_block_base,
				&pfn_block_limit)) > 0) {
			if ((err = cr_map_pages_auto(&cmp_params, &va,
					pfn_block_base, pfn_block_limit,
					PE_BIT_READ_WRITE, CMP_BIT_NX_ENABLE,
					level)) != 0) {
				break;
			}
		}
		if (err < 0) {
			goto fail;
		}
	}

	/*
	 * Map code page(s) at top of VA and at current VA
	 * Create cdev & return
	 */

	if ((err = cr_map_pages_from_va(&cmp_params, code_base, va,
			(cr_clear_limit - code_base) / PAGE_SIZE,
			0, CMP_BIT_NX_DISABLE)) < 0) {
		goto fail;
	} else
	if ((err = cr_map_pages_from_va(&cmp_params, code_base, code_base,
			(cr_clear_limit - code_base) / PAGE_SIZE,
			0, CMP_BIT_NX_DISABLE)) < 0) {
		goto fail;
	} else
	if ((err = cr_init_cdev(&cr_exit_params)) < 0) {
		goto fail;
	} else {
		cr_exit_params.map = (void *)cmp_params.map_base;
		err = 0;
	}
#if defined(DEBUG)
	cr_debug_stack = va;
	cr_debug_idt = cr_debug_stack + PAGE_SIZE;
	cr_debug_vga = cr_debug_idt + PAGE_SIZE;
	if ((err = cr_debug_init(&cmp_params, cr_pml4,
			cr_debug_idt, cr_debug_stack, cr_debug_vga)) < 0) {
		goto fail;
	}
#endif /* defined(DEBUG) */

out:	if (cpw_params.filter) {
		cr_free(cpw_params.filter, NULL);
	}
	if (cmp_params.map_phys.map_base) {
		cr_free(cmp_params.map_phys.map_base, NULL);
	}
	return err;

fail:	if (cmp_params.map_base) {
		cr_free((void *)cmp_params.map_base,
			cr_exit_params.map_free_fn);
	}
	goto out;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
