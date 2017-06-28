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

/* 
 * Kernel module {entry,event} point subroutines
 */

/* Virtual address of Page Map Level 4 page */
struct page_ent *cr_pml4 = NULL;

/* Resources to release when exiting */
static struct clearram_exit_params cr_exit_params = {0,};

/* Static subroutine prototypes */
static int cr_init_pfns_compare(const void *lhs, const void *rhs);
#if defined(__FreeBSD__)
int clearram_evhand(struct module *m, int what, void *arg);
#endif /* defined(__FreeBSD__) */
void clearram_exit(void);
int clearram_init(void);

#if defined(__linux__)
/* Character device node file operations */
struct file_operations cr_cdev_fops = {
	.write = cr_cdev_write,
};

module_exit(clearram_exit);
module_init(clearram_init);
MODULE_AUTHOR("Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>");
MODULE_DESCRIPTION("clearram");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("clearram");
#elif defined(__FreeBSD__)
/* Character device node file operations */
struct cdevsw cr_cdev_fops = {
	.d_version = D_VERSION,
	.d_write = cr_cdev_write,
	.d_name = "clearram",
};

moduledata_t clearram_mod = {
	"clearram", clearram_evhand, NULL,
};
MALLOC_DEFINE(M_CLEARRAM, "clearram", "buffer for clearram module");
DECLARE_MODULE(clearram, clearram_mod, SI_SUB_KLD, SI_ORDER_ANY);
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/**
 * cr_init_pfns_compare() - page map PFN database numeric sort comparison function
 * @lhs:	left-hand side PFN
 * @rhs:	right-hand side PFN
 *
 * Return: -1, 1, or 0 if lhs_pfn is smaller than, greater than, or equal to rhs_pfn
 */

int cr_init_pfns_compare(const void *lhs, const void *rhs)
{
	uintptr_t lhs_pfn, rhs_pfn;

	lhs_pfn = *(const uintptr_t *)lhs;
	rhs_pfn = *(const uintptr_t *)rhs;
	if (lhs_pfn < rhs_pfn) {
		return -1;
	} else
	if (lhs_pfn > rhs_pfn) {
		return 1;
	} else {
		return 0;
	}
}

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
	uintptr_t map_npages;
	uintptr_t code_base;
	extern uintptr_t cr_clear_limit;
	struct cmp_params cmp_params;
	size_t npfn;
	uintptr_t va, pfn_block_base, pfn_block_limit;

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
	map_npages =  (CR_DIV_ROUND_UP_ULL(map_npages, (512)))				/* Page Tables */
		    + (CR_DIV_ROUND_UP_ULL(map_npages, (512 * 512)))			/* Page Directories */
		    + (CR_DIV_ROUND_UP_ULL(map_npages, (512 * 512 * 512)))		/* Page Directory Pointer pages */
		    + (1)								/* Page Map Level 4 */
		    + (((cr_clear_limit - code_base) / PAGE_SIZE) * (1 + 1 + 1))	/* {PDP,PD,PT} to map code at top of VA */
		    + (((cr_clear_limit - code_base) / PAGE_SIZE) * (1 + 1 + 1));	/* {PDP,PD,PT} to map code at original VA */

	/*
	 * Initialise map, map filter PFN list, and phys-to-virt map list
	 * Initialise, fill, and numerically sort map filter PFN list
	 */

	err = cr_init_map((void **)&cmp_params.map_base,
		(void **)&cmp_params.map_cur, &cmp_params.map_limit,
		map_npages * PAGE_SIZE, &cr_exit_params.map_free_fn);
	if (err < 0) {
		goto fail;
	}
	if ((err = cr_init_map((void **)&cpw_params.filter, NULL, NULL,
			(map_npages + 1) * sizeof(uintptr_t), NULL)) < 0) {
		goto fail;
	} else
	for (npfn = 0; npfn < map_npages; npfn++) {
		cpw_params.filter[npfn] = cr_virt_to_phys(
			(uintptr_t)cmp_params.map_base + (npfn * PAGE_SIZE));
	}
	cpw_params.filter[npfn] = cr_virt_to_phys(code_base);
	cpw_params.filter_nmax = map_npages;
	CR_SORT(cpw_params.filter, map_npages + 1, sizeof(uintptr_t),
		&cr_init_pfns_compare);
	err = cr_init_map((void **)&cmp_params.map_phys_base,
		(void **)&cmp_params.map_phys_cur,
		&cmp_params.map_phys_limit,
		map_npages * sizeof(struct cr_map_phys_node), NULL);
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
	INIT_CPW_PARAMS(&cpw_params);
	while ((err = cr_pmem_walk_filter(&cpw_params, &pfn_block_base,
			&pfn_block_limit)) > 0) {
		if ((err = cr_map_pages(&cmp_params, &va, pfn_block_base,
				pfn_block_limit, PE_BIT_READ_WRITE, 1)) != 0) {
			break;
		}
	}
	if (err < 0) {
		goto fail;
	}

	/*
	 * Map code page(s) at top of VA and at current VA
	 * Create cdev & return
	 */

	if ((err = cr_map_pages_from_va(&cmp_params, code_base, va,
			(cr_clear_limit - code_base) / PAGE_SIZE, 0, 0)) < 0) {
		goto fail;
	} else
	if ((err = cr_map_pages_from_va(&cmp_params, code_base, code_base,
			(cr_clear_limit - code_base) / PAGE_SIZE, 0, 0)) < 0) {
		goto fail;
	} else
	if ((err = cr_init_cdev(&cr_exit_params)) < 0) {
		goto fail;
	} else {
		cr_exit_params.map = (void *)cmp_params.map_base;
		err = 0;
	}

out:	if (cpw_params.filter) {
		cr_free(cpw_params.filter, NULL);
	}
	if (cmp_params.map_phys_base) {
		cr_free(cmp_params.map_phys_base, NULL);
	}
	return err;

fail:	if (cmp_params.map_base) {
		cr_free((void *)cmp_params.map_base,
			cr_exit_params.map_free_fn);
	}
	goto out;
}

/**
 * cr_clear() - setup CPU(s) and zero-fill RAM
 *
 * Disable preemption on the current CPU and stop all other CPUs, if
 * any, which may block briefly. Setup CR3 with our PML4, flush the TLB,
 * and zero-fill physical memory using REP STOSQ. As the code pages are
 * mapped at the top of VA, this will eventually cause a trigger fault
 * whilst still zero-filling as much as possible.
 *
 * Return: Nothing
 */

void __attribute__((aligned(PAGE_SIZE))) cr_clear(void)
{
	struct cr3 cr3;

	cr_cpu_stop_all();
	CR_INIT_CR3(&cr3, cr_virt_to_phys((uintptr_t)cr_pml4), CR3_BIT_WRITE_THROUGH);
	__asm volatile(
		"\tcld\n"
		"\tcli\n"
		"\tmovq		%0,		%%rcx\n"	/* New CR3 value */
		"\tmovq		%%cr4,		%%rax\n"
		"\tmovq		%%rax,		%%rbx\n"
		"\tandb		$0x7f,		%%al\n"
		"\tmovq		%%rax,		%%cr4\n"	/* Disable PGE */
		"\tmovq		%%rcx,		%%cr3\n"	/* Set CR3 */
		"\tmovq		%%rbx,		%%cr4\n"	/* Enable PGE */
		"\txorq		%%rcx,		%%rcx\n"
		"\tdecq		%%rcx\n"			/* Count = 0xFFFFFFFFFFFFFFFFLL */
		"\txorq		%%rax,		%%rax\n"	/* Store = 0x0000000000000000LL */
		"\txorq		%%rdi,		%%rdi\n"	/* Dest. = 0x0000000000000000LL */
		"\trep		stosq\n"			/* Zero-fill & triple fault */
		"\tud2\n"
		"\t.align	0x1000\n"
		"\tcr_clear_limit:\n"
		"\t.quad	.\n"
		:: "r"(cr3)
		: "r8", "rax", "rbx", "rcx", "rdx",
		  "rdi", "flags", "memory");
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
