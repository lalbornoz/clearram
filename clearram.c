/*
 * clearram.c -- clear system RAM and reboot on demand (for zubwolf)
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

/*
 * {{{ Control Register 3 (CR3) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 5.3.2, pages 130-131.
 */

enum cr3_bits {
	CR3_BIT_WRITE_THROUGH	= 0x008,
	CR3_BIT_CACHE_DISABLE	= 0x010,
};
struct cr3 {
	enum cr3_bits		bits:5;
	unsigned		ign1:7;
	uintptr_t		pml4_pfn_base:40;
	unsigned		ign2:12;
} __attribute__((packed));
#define CR_INIT_CR3(cr3, new_pml4_pfn_base, extra_bits) do {		\
		(cr3)->bits = extra_bits;				\
		(cr3)->ign1 = 0;					\
		(cr3)->pml4_pfn_base = new_pml4_pfn_base;		\
		(cr3)->ign2 = 0;					\
	} while (0)
/* }}} */

/*
 * {{{ {4-Kbyte,2-Mbyte,1-Gbyte} {PML4,PDP,PD,PT}E in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * section 5.3.3, pages 133, 135, 137, and section 5.4.1, pages 138-141.
 */

enum pe_bits {
	PE_BIT_PRESENT		= 0x001,
	PE_BIT_READ_WRITE	= 0x002,
	PE_BIT_USER_SUPERVISOR	= 0x004,
	PE_BIT_WRITE_THROUGH	= 0x008,
	PE_BIT_CACHE_DISABLE	= 0x010,
	PE_BIT_ACCESSED		= 0x020,
	PE_BIT_DIRTY		= 0x040,
	PE_BIT_PAGE_SIZE	= 0x080,
	PE_BIT_GLOBAL		= 0x100,
};
struct page_ent {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	uintptr_t		pfn_base:40;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));
struct page_ent_1G {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1, mbz:18;
	uintptr_t		pfn_base:21;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));
struct page_ent_2M {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1, mbz:8;
	uintptr_t		pfn_base:31;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));

#define CR_INIT_PAGE_ENT(pe, bits_extra, nx_) do {			\
		*((unsigned long *)pe) = 0;				\
		(pe)->bits = PE_BIT_PRESENT | PE_BIT_CACHE_DISABLE |	\
			(bits_extra);					\
		(pe)->nx = (nx_);					\
	} while (0)

/* {PML4,PDP,PD,PT} entry base address masks */
#define CR_PAGE_ENT_IDX_MASK	0x1ff
#define CR_PDPE_MASK		0x3fffffff
#define CR_PDE_MASK		0x1fffff

/* Count of pages mapped per {PDP,PD,PT} entry */
#define CR_PDPE_SIZE		(512 * 512)
#define CR_PDE_SIZE		(512)
#define CR_PTE_SIZE		(1)

/* Convert virtual address to {PML4,PDP,PD,PT} index */
#define CR_VA_TO_PML4_IDX(va)	(((va) >> (9 + 9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PDP_IDX(va)	(((va) >> (9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PD_IDX(va)	(((va) >> (9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PT_IDX(va)	(((va) >> (12)) & CR_PAGE_ENT_IDX_MASK)
/* }}} */

/*
 * {{{ Preprocessor macros and static subroutine prototypes
 */

/* Convert kernel address space VA to 40-bit virtual page number and vice versa */
#define CR_VA_TO_VPN(va)	(((uintptr_t)(va)) >> 12)
#define CR_CANONICAL_KVA(addr)	(((int64_t)(addr) << 16) >> 16)
#define CR_VPN_TO_VA(va)	(CR_CANONICAL_KVA(((uintptr_t)(va)) << 12))

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

/*
 * Page mapping logic and helper subroutines
 */
static uintptr_t cr_virt_to_phys(uintptr_t va);
static void cr_map_set_pfn_base(struct page_ent *pe, int level, uintptr_t pfn_base);
static void *cr_map_translate_pfn_base(struct page_ent *pe, int level);
static void cr_map_translate(struct page_ent *pt_cur, int level);
static int cr_map_page(struct page_ent *pt_cur, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, uintptr_t *map_base, uintptr_t map_limit);
static int cr_map_pages(struct page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx, uintptr_t *map_base, uintptr_t map_limit);

/*
 * Platform-dependent subroutines
 */

#if defined(__linux__)
struct crpw_params {
	unsigned	new_nid:1, restart:1, fini:1;
	int		nid;
	uintptr_t	node_base, node_limit;
	uintptr_t	pfn_cur;
	uintptr_t	last_base, last_limit;
};
#define INIT_CRPW_PARAMS(p) do {		\
		memset((p), 0, sizeof(*(p)));	\
		(p)->restart = 1;		\
		(p)->new_nid = 1;		\
	} while(0)

static int cr_pmem_walk_nocombine(struct crpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit);
static int cr_pmem_walk(struct crpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
static ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)));
#elif defined(__FreeBSD__)
struct crpw_params {
	int		nid;
};
#define INIT_CRPW_PARAMS(p) do {		\
		(p)->nid = 0;			\
	} while(0)

static int cr_pmem_walk(struct crpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
static d_write_t __attribute__((noreturn)) cr_cdev_write;
#endif /* defined(__linux__) || defined(__FreBSD__) */

/*
 * Kernel module {exit,entry} helper subroutines
 */
static int cr_map_pfns_compare(const void *lhs, const void *rhs);
static void *cr_maps_alloc(unsigned long size, void (**map_free)(const void *));
void clearram_exit(void);
int clearram_init(void);

#if defined(__linux__)
module_exit(clearram_exit);
module_init(clearram_init);
MODULE_AUTHOR("Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>");
MODULE_DESCRIPTION("clearram");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("clearram");
#elif defined(__FreeBSD__)
static int clearram_evhand(struct module *m, int what, void *arg);
static moduledata_t clearram_mod = {
	"clearram", clearram_evhand, NULL,
};
MALLOC_DECLARE(M_CLEARRAM);
MALLOC_DEFINE(M_CLEARRAM, "clearram", "buffer for clearram module");
DECLARE_MODULE(clearram, clearram_mod, SI_SUB_KLD, SI_ORDER_ANY);
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/*
 * Setup & zero-fill RAM code and helper subroutines
 */
#if defined(__linux__)
#ifdef CONFIG_SMP
static void cr_stop_cpu(void *info);
static void cr_stop_cpus(void);
#endif /* CONFIG_SMP */
#elif defined(__FreeBSD__)
#endif /* defined(__linux__) || defined(__FreBSD__) */
static void __attribute__((aligned(PAGE_SIZE))) cr_clear(void);
/* }}} */

/*
 * {{{ Static variables
 */

/* Count of and pointer to base of page map. */
static unsigned long cr_map_npages = 0;
static void *cr_map = NULL;

/* Pointer to base of PFN list backing cr_map. */
static uintptr_t *cr_map_pfns = NULL;

#ifdef __linux__
/* Function pointer to either of {vfree,kfree}(), releasing cr_map{,_pfns}. */
static void (*cr_map_free)(const void *) = NULL;
static void (*cr_map_pfns_free)(const void *);
#endif /* __linux__ */

/* Page Map Level 4 */
static struct page_ent *cr_pml4 = NULL;

#if defined(__linux__)
/* Character device node file operations */
static struct file_operations cr_cdev_fops = {
	.write = cr_cdev_write,
};

/* Character device node major number, class, and device pointers. */
static int cr_cdev_major = 0;
struct class *cr_cdev_class = NULL;
EXPORT_SYMBOL(cr_cdev_class);
static struct device *cr_cdev_device = NULL;
#elif defined(__FreeBSD__)
/* Character device node file operations */
static struct cdevsw cr_cdev_fops = {
	.d_version = D_VERSION,
	.d_write = cr_cdev_write,
	.d_name = "clearram",
};

/* Character device pointer. */
static struct cdev *cr_cdev_device = NULL;
#endif /* defined(__linux__) || defined(__FreBSD__) */
/* }}} */

/*
 * {{{ Page mapping logic and helper subroutines
 */

/**
 * cr_virt_to_phys() - translate virtual address to physical address (PFN) using host page tables
 * @va:		Virtual address to translate
 *
 * Return: Physical address (PFN) mapped by virtual address
 */

static uintptr_t cr_virt_to_phys(uintptr_t va)
{
#if defined(__linux__)
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	uintptr_t pfn;

	pgd = pgd_offset(current->mm, va);
	pud = pud_offset(pgd, va);
	if (pud_val(*pud) & _PAGE_PSE) {
		return ((*(unsigned long *)pud) >> 13) & 0xffffffffff;
	}
	pmd = pmd_offset(pud, va);
	if (pmd_val(*pmd) & _PAGE_PSE) {
		return ((*(unsigned long *)pud) >> 13) & 0xffffffffff;
	}
	pte = pte_offset_map(pmd, va);
	pfn = ((*(unsigned long *)pte) >> 12) & 0xffffffffff;
	return pfn;
#elif defined(__FreeBSD__)
	return vtophys(va);
#endif /* defined(__linux__) || defined(__FreeBSD__) */
}

/**
 * cr_map_set_pfn_base() - set {PML4,PDP,PD,PT} entry physical (PFN) base address
 * @pe:		pointer to {PML4,PDP,PD,PT} entry
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 * @pfn_base:	new base physical address (PFN)
 *
 * Return: Nothing
 */

static void cr_map_set_pfn_base(struct page_ent *pe, int level, uintptr_t pfn_base)
{
	struct page_ent_2M *pe_2M;
	struct page_ent_1G *pe_1G;

	switch (level) {
	case 4: case 1:
		pe->pfn_base = pfn_base; break;
	case 3:
		if ((pe->bits & PE_BIT_PAGE_SIZE)) {
			pe_1G = (struct page_ent_1G *)pe;
			pe_1G->pfn_base = pfn_base;
		} else {
			pe->pfn_base = pfn_base;
		}; break;
	case 2:
		if ((pe->bits & PE_BIT_PAGE_SIZE)) {
			pe_2M = (struct page_ent_2M *)pe;
			pe_2M->pfn_base = pfn_base;
		} else {
			pe->pfn_base = pfn_base;
		}; break;
	}
}

/**
 * cr_map_translate_pfn_base() - translate {PML4,PDP,PD,PT} entry bitshifted virtual base address to VA
 * @pe:		pointer to {PML4,PDP,PD,PT} entry
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 *
 * Return: Virtual address corresponding to entry virtual base address
 */

static void *cr_map_translate_pfn_base(struct page_ent *pe, int level)
{
	struct page_ent_2M *pe_2M;
	struct page_ent_1G *pe_1G;
	uintptr_t pfn_base;

	switch (level) {
	case 4:
		pfn_base = pe->pfn_base; break;
	case 3:
		if ((pe->bits & PE_BIT_PAGE_SIZE)) {
			pe_1G = (struct page_ent_1G *)pe;
			pfn_base = pe_1G->pfn_base;
		} else {
			pfn_base = pe->pfn_base;
		}; break;
	case 2:
		if ((pe->bits & PE_BIT_PAGE_SIZE)) {
			pe_2M = (struct page_ent_2M *)pe;
			pfn_base = pe_2M->pfn_base;
		} else {
			pfn_base = pe->pfn_base;
		}; break;
	default:
		return NULL;
	}
	return (void *)CR_VPN_TO_VA(pfn_base);
}

/**
 * cr_map_translate() - translate all {PML4,PDP,PD,PT} entry virtual base addresses to PFN
 * @pe:		pointer to {PML4,PDP,PD,PT} to translate
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 *
 * Translate base addresses for all present mappings in pe according to
 * level, being either a PML4, PDP, PD, or PT, from bitshifted VA to physical
 * addresses (PFN.)
 * There must not be any further calls to cr_map_page{,s}() after this
 * function returns.
 *
 * Return: Nothing
 */

static void cr_map_translate(struct page_ent *pt_cur, int level)
{
	unsigned long pt_idx;
	struct page_ent *pt_next;

	if (level == 1) {
		return;
	} else
	for (pt_idx = 0; pt_idx < 512; pt_idx++) {
		if (!(pt_cur[pt_idx].bits & PE_BIT_PRESENT)) {
			continue;
		} else
		if ((level == 4)
		||  (!(pt_cur[pt_idx].bits & PE_BIT_PAGE_SIZE))) {
			pt_next = cr_map_translate_pfn_base(&pt_cur[pt_idx], level);
			cr_map_translate(pt_next, level - 1);
			cr_map_set_pfn_base(&pt_cur[pt_idx], level,
				cr_virt_to_phys((uintptr_t)pt_next));
		}
	}
}

/**
 * cr_map_page() - recursively create one {1G,2M,4K} mapping from VA to PFN in {PML4,PDP,PD,PT}
 * @pt_cur:	pointer to {PML4,PDP,PD,PT} to map into
 * @va:		virtual address to map at
 * @pfn:	physical address (PFN) to map
 * @page_size:	262144 (1 GB page,) 512 (2 MB page,) or 1 (4 KB page)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 * @map_base:	map heap base address
 * @map_limit:	map heap limit address
 *
 * Create {1G,2M,4K} mapping for pfn at va in the {PML4,PDP,PD,PT}
 * specified by page_size, pt_cur, and level using the supplied extra_bits
 * and page_nx bit. Lower-order page tables are recursively created on
 * demand. Newly created {PDP,PD,PT} are allocated from the map heap in
 * units of the page size (4096) without blocking.
 * The page tables produced by this function must have their base addresses
 * translated from bitshifted VA to physical addresses (PFN) prior to being
 * used.
 *
 * Return: 0 on success, <0 otherwise
 */

static int cr_map_page(struct page_ent *pt_cur, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, uintptr_t *map_base, uintptr_t map_limit)
{
	int map_direct;
	struct page_ent *pt_next;
	unsigned long pt_idx;

	switch (level) {
	case 4: pt_idx = CR_VA_TO_PML4_IDX(*va);
		map_direct = 0;
		break;
	case 3:	pt_idx = CR_VA_TO_PDP_IDX(*va);
		if ((map_direct = (page_size == CR_PDPE_SIZE))) {
			extra_bits |= PE_BIT_PAGE_SIZE;
		}; break;
	case 2:	pt_idx = CR_VA_TO_PD_IDX(*va);
		if ((map_direct = (page_size == CR_PDE_SIZE))) {
			extra_bits |= PE_BIT_PAGE_SIZE;
		}; break;
	case 1:	pt_idx = CR_VA_TO_PT_IDX(*va);
		map_direct = 1;
		break;
	default:
		return -EINVAL;
	}
	if (map_direct) {
		CR_INIT_PAGE_ENT(&pt_cur[pt_idx], extra_bits, page_nx);
		cr_map_set_pfn_base(&pt_cur[pt_idx], level, pfn);
		(*va) += PAGE_SIZE * page_size;
		return 0;
	} else
	if (!(pt_cur[pt_idx].bits & PE_BIT_PRESENT)) {
		if (*map_base >= map_limit) {
			return -ENOMEM;
		} else {
			pt_next = (struct page_ent *)*map_base;
			*map_base += PAGE_SIZE;
		}
		CR_INIT_PAGE_ENT(&pt_cur[pt_idx], extra_bits, page_nx);
		cr_map_set_pfn_base(&pt_cur[pt_idx], level,
			CR_VA_TO_VPN(pt_next));
	} else {
		pt_next = cr_map_translate_pfn_base(&pt_cur[pt_idx], level);
	}
	return cr_map_page(pt_next, va, pfn, page_size, extra_bits,
			page_nx, level - 1, map_base, map_limit);
}

/**
 * cr_map_pages() - create continuous VA to PFN mappings in PML4
 * @pml4:	pointer to PML4 to map into
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry/ies
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry/ies
 * @map_base:	map heap base address
 * @map_limit:	map heap limit address
 *
 * Create {1G,2M,4K} mappings for each PFN within pfn_base..pfn_limit
 * starting at va_base in pml4 using the supplied extra_bits and page_nx
 * bit. {1G,2M} mappings are created whenever {1G,2M}-aligned VA/PFN
 * blocks are encountered; unaligned {1G,2M} VA/PFN blocks are allocated
 * in units of {2M,4K} relative to alignment. The map heap along with
 * most other parameters are passed through to cr_map_page() for each
 * page mapped.
 *
 * Return: 0 on success, <0 otherwise
 */

static int cr_map_pages(struct page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx, uintptr_t *map_base, uintptr_t map_limit)
{
	int err;
	unsigned long pfn;
	size_t npages;

	for (pfn = pfn_base; pfn < pfn_limit; pfn += npages) {
		npages = pfn_limit - pfn_base;
		if ( ( npages >= CR_PDPE_SIZE)
		&&  ((*va_base & CR_PDPE_MASK) == 0)) {
			npages = CR_PDPE_SIZE;
		} else
		if ( ( npages >= CR_PDE_SIZE)
		&&  ((*va_base & CR_PDE_MASK) == 0)) {
			npages = CR_PDE_SIZE;
		} else {
			npages = 1;
		}
		if ((err = cr_map_page(pml4, va_base,
				pfn, npages, extra_bits, page_nx,
				4, map_base, map_limit)) != 0) {
			return err;
		}
	}
	return 0;
}
/* }}} */

/*
 * {{{ Platform-dependent subroutines
 */

#if defined(__linux__)
/**
 * cr_pmem_walk_nocombine() - walk physical memory without combining sections
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

static int cr_pmem_walk_nocombine(struct crpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
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
			||  unlikely(!present_section(ms))) {
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
 * cr_pmem_walk() - walk physical memory, combining sections
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

static int cr_pmem_walk(struct crpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
{
	int err;
	uintptr_t section_base, section_limit;

	if (params->fini) {
		return 0;
	} else
	while ((err = cr_pmem_walk_nocombine(params,
			&section_base, &section_limit)) > 0) {
		if (params->restart) {
			params->restart = 0;
			params->last_base = section_base;
			params->last_limit = section_limit;
		} else
		if (params->last_limit == section_base) {
			params->last_limit = section_limit;
			continue;
		} else {
			*psection_base = params->last_base;
			*psection_limit = section_base;
			params->last_base = section_base;
			params->last_limit = section_limit;
			return 1;
		}
	}
	params->fini = 1;
	if ((err == 0)
	&&  (params->last_limit - params->last_base)) {
		*psection_base = params->last_base;
		*psection_limit = params->last_limit;
		return 1;
	} else {
		return err;
	}
}

/**
 * cr_cdev_write() - character device write(2) file operation subroutine
 * @file:	(unused)
 * @buf:	(unused)
 * @len:	(unused)
 * @ppos:	(unused)
 *
 * Return: number of bytes written, <0 on error
 */

static ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)))
{
	cr_clear();
	__builtin_unreachable();
}
#elif defined(__FreeBSD__)
/**
 * cr_pmem_walk() - walk physical memory
 * @params:		current walk parameters
 * @psection_base:	pointer to base address of next section found
 * @psection_limit:	pointer to limit address of next section found
 *
 * Return: 0 if no physical memory remains, 1 otherwise
 */

static int cr_pmem_walk(struct crpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
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
 * cr_cdev_write() - character device write(2) file operation subroutine
 * @dev:	(unused)
 * @uio:	(unused)
 * @ioflag:	(unused)	
 *
 * Return: number of bytes written, <0 on error
 */

static int __attribute__((noreturn)) cr_cdev_write(struct cdev *dev __unused, struct uio *uio __unused, int ioflag __unused)
{
	cr_clear();
	__builtin_unreachable();
}
#endif /* defined(__linux__) || defined(__FreeBSD__) */
/* }}} */

/*
 * {{{ Kernel module {exit,entry} point and helper subroutines
 */

/**
 * cr_map_pfns_compare() - page map PFN database numeric sort comparison function
 * @lhs:	left-hand side PFN
 * @rhs:	right-hand side PFN
 *
 * Return: -1, 1, or 0 if lhs_pfn is smaller than, greater than, or equal to rhs_pfn
 */

static int cr_map_pfns_compare(const void *lhs, const void *rhs)
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

/**
 * cr_maps_alloc() - allocate and zero-fill map
 * @size:	size of map in bytes
 * @map_free:	pointer to map memory free function
 *
 * Return: pointer to allocated and zero-filled map
 */

static void *cr_maps_alloc(unsigned long size, void (**map_free)(const void *))
{
#if defined(__linux__)
	void *map;

	map = vmalloc(size);
	if (!map) {
		map = kmalloc(size, GFP_KERNEL);
		if (!map) {
			return map;
		} else {
			*map_free = &kfree;
		}
	} else {
		*map_free = &vfree;
	}
	return memset(map, 0, size), map;
#elif defined(__FreeBSD__)
	return malloc(size, M_CLEARRAM, M_WAITOK | M_ZERO);
#endif /* defined(__linux__) || defined(__FreeBSD__) */
}

#ifdef __FreeBSD__
/**
 * clearram_evhand() - handle kernel module event
 * @m:		pointer to this module
 * @what:	MOD_LOAD when loading module, MOD_UNLOAD when unloading module
 * @arg:	(unused)
 *
 * Return: 0 on success, >0 otherwise
 */

static int clearram_evhand(struct module *m, int what, void *arg __unused)
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
#endif /* __FreeBSD__ */

/**
 * clearram_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void clearram_exit(void)
{
#if defined(__linux__)
	if (cr_cdev_device) {
		device_destroy(cr_cdev_class, MKDEV(cr_cdev_major, 0));
	}
	if (cr_cdev_class) {
		class_destroy(cr_cdev_class);
	}
	if (cr_cdev_major) {
		unregister_chrdev(cr_cdev_major, "clearram");
	}
	if (cr_map_pfns) {
		cr_map_pfns_free(cr_map_pfns);
	}
	if (cr_map) {
		cr_map_free(cr_map);
	}
#elif defined(__FreeBSD__)
	if (cr_cdev_device) {
		destroy_dev(cr_cdev_device);
	}
	if (cr_map_pfns) {
		free(cr_map_pfns, M_CLEARRAM);
	}
	if (cr_map) {
		free(cr_map, M_CLEARRAM);
	}
#endif /* defined(__linux__) || defined(__FreeBSD__) */
}

/**
 * clearram_init() - kernel module entry point
 *
 * Initialise the map on the current processor with page tables mapping
 * physical RAM continuously at 0x0ULL, skipping page frames allocated to the
 * map itself and to the code pages spanning cr_clear_base..cr_clear_limit.
 * The code is mapped both at its current VA as well as at the top of VA. This
 * allows cr_clear() to zero-fill its own pages up to a certain point. The GDT,
 * IDT, and stack are left untouched by cr_clear() and are thus not mapped.
 * Create the character device to allow user-mode to trigger calling cl_clear().
 * 
 * Return: 0 on success, <0 on failure
 */

int clearram_init(void)
{
	uintptr_t cr_clear_base;
	extern uintptr_t cr_clear_limit;
	struct crpw_params crpw_params;
	uintptr_t map_cur, map_limit;
	size_t npfn;
	uintptr_t va, pfn_block_base, pfn_block_limit;
	int err;

	/*
	 * Enforce page alignment & max. 2 * PAGE_SIZE size constraint on cr_clear()
	 * Obtain total amount of page frames on host
	 * Derive max. amount of {PML4,PDP,PD,PT} required to map
	 */

	cr_clear_base = (uintptr_t)&cr_clear;
	if ( (cr_clear_base & 0xfff)
	||  ((cr_clear_limit - cr_clear_base) > (2 * PAGE_SIZE))) {
		return -EINVAL;
	};
	INIT_CRPW_PARAMS(&crpw_params);
	while ((err = cr_pmem_walk(&crpw_params, &pfn_block_base,
			&pfn_block_limit)) == 1) {
		cr_map_npages += (pfn_block_limit - pfn_block_base);
	}
	if (err < 0) {
		return err;
	}
	cr_map_npages =
		      (CR_DIV_ROUND_UP_ULL(cr_map_npages, (512)))		/* Page Tables */
		    + (CR_DIV_ROUND_UP_ULL(cr_map_npages, (512 * 512)))		/* Page Directories */
		    + (CR_DIV_ROUND_UP_ULL(cr_map_npages, (512 * 512 * 512)))	/* Page Directory Pointer pages */
		    + (1);							/* Page Map Level 4 */
	cr_map_npages += (2 * (1 + 1 + 1));					/* {PDP,PD,PT} to map code at top of VA */
	cr_map_npages += (2 * (1 + 1 + 1));					/* {PDP,PD,PT} to map code at original VA */

	/*
	 * Initialise map heap
	 * Initialise, fill, and numerically sort map heap PFN list
	 */

#if defined(__linux__)
	cr_map = cr_maps_alloc(cr_map_npages * PAGE_SIZE, &cr_map_free);
#elif defined(__FreeBSD__)
	cr_map = cr_maps_alloc(cr_map_npages * PAGE_SIZE, NULL);
#endif /* defined(__linux__) || defined(__FreeBSD__) */
	if (!cr_map) {
		return clearram_exit(), -ENOMEM;
	};
#if defined(__linux__)
	cr_map_pfns = cr_maps_alloc((cr_map_npages + 1) * sizeof(uintptr_t), &cr_map_pfns_free);
#elif defined(__FreeBSD__)
	cr_map_pfns = cr_maps_alloc((cr_map_npages + 1) * sizeof(uintptr_t), NULL);
#endif /* defined(__linux__) || defined(__FreeBSD__) */
	if (!cr_map_pfns) {
		return clearram_exit(), -ENOMEM;
	} else
	for (npfn = 0; npfn < cr_map_npages; npfn++) {
		cr_map_pfns[npfn] = cr_virt_to_phys((uintptr_t)cr_map + (npfn * PAGE_SIZE));
	}
	cr_map_pfns[npfn] = cr_virt_to_phys(cr_clear_base);
#if defined(__linux__)
	sort(cr_map_pfns, cr_map_npages + 1, sizeof(uintptr_t),
		&cr_map_pfns_compare, NULL);
#elif defined(__FreeBSD__)
	qsort(cr_map_pfns, cr_map_npages + 1, sizeof(uintptr_t),
		&cr_map_pfns_compare);
#endif /* defined(__linux__) || defined(__FreeBSD__) */

	/*
	 * Set VA to 0x0ULL, initialise PML4 from map heap
	 * Walk physical RAM, skipping map heap page frames
	 * Map consecutive ranges of at least 1 page frame to current VA
	 */

	va = 0x0LL;
	cr_pml4 = cr_map;
	map_limit = (uintptr_t)cr_map + (cr_map_npages * PAGE_SIZE);
	map_cur = (uintptr_t)cr_map + PAGE_SIZE;
	INIT_CRPW_PARAMS(&crpw_params);
	while ((err = cr_pmem_walk(&crpw_params, &pfn_block_base,
			&pfn_block_limit)) == 1) {
		for (npfn = 0; npfn < (cr_map_npages + 1); npfn++) {
			if ((cr_map_pfns[npfn] <  pfn_block_base)
			||  (cr_map_pfns[npfn] >= pfn_block_limit)) {
				continue;
			} else
			if (cr_map_pfns[npfn] == pfn_block_base) {
				pfn_block_base = cr_map_pfns[npfn] + 1;
				continue;
			} else {
				err = cr_map_pages(cr_pml4, &va, pfn_block_base,
					cr_map_pfns[npfn], PE_BIT_READ_WRITE, 1,
					&map_cur, map_limit);
				if (err != 0) {
					return clearram_exit(), err;
				}
				pfn_block_base = cr_map_pfns[npfn] + 1;
				if (pfn_block_base >= pfn_block_limit) {
					break;
				}
			}
		}
		if (pfn_block_base < pfn_block_limit) {
			err = cr_map_pages(cr_pml4, &va,
				pfn_block_base, pfn_block_limit,
				PE_BIT_READ_WRITE, 1, &map_cur, map_limit);
			if (err != 0) {
				return clearram_exit(), err;
			}
		}
	}
	if (err < 0) {
		return clearram_exit(), err;
	}

	/*
	 * Map code page(s) at top of VA and at current VA
	 * Translate map VA to PFN
	 */

	pfn_block_base = cr_virt_to_phys(cr_clear_base);
	for (int npage = 0; npage < (cr_clear_limit - cr_clear_base); npage++) {
		pfn_block_base = cr_virt_to_phys(va);
		err = cr_map_pages(cr_pml4, &va, pfn_block_base,
				pfn_block_base + 2, 0, 0, &map_cur, map_limit);
		if (err != 0) {
			return clearram_exit(), err;
		}
	}
	va = cr_clear_base;
	for (int npage = 0; npage < (cr_clear_limit - cr_clear_base); npage++) {
		pfn_block_base = cr_virt_to_phys(va);
		err = cr_map_pages(cr_pml4, &va, pfn_block_base,
				pfn_block_base + 2, 0, 0, &map_cur, map_limit);
		if (err != 0) {
			return clearram_exit(), err;
		}
	}
	cr_map_translate(cr_pml4, 4);

	/*
	 * Create cdev
	 */

#if defined(__linux__)
	cr_cdev_major = register_chrdev(0, "clearram", &cr_cdev_fops);
	if (cr_cdev_major < 0) {
		return clearram_exit(), cr_cdev_major;
	}
	cr_cdev_class = class_create(THIS_MODULE, "clearram");
	if (IS_ERR(cr_cdev_class)) {
		return clearram_exit(), PTR_ERR(cr_cdev_class);
	}
	cr_cdev_device = device_create(cr_cdev_class, NULL, MKDEV(cr_cdev_major, 0), NULL, "clearram");
	if (IS_ERR(cr_cdev_device)) {
		return clearram_exit(), PTR_ERR(cr_cdev_device);
	}
#elif defined(__FreeBSD__)
	err = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		    &cr_cdev_device, &cr_cdev_fops, 0,
		    UID_ROOT, GID_WHEEL, 0600, "clearram");
	if (err != 0) {
		return clearram_exit(), err;
	}
#endif /* defined(__linux__) || defined(__FreeBSD__) */

	return 0;
}
/* }}} */

/*
 * {{{ Zero-fill RAM code and helper subroutines
 */

#if defined(__linux__)
#ifdef CONFIG_SMP
struct csc_params {
	spinlock_t	lock;
	int		ncpus_stopped;
};

/**
 * cr_stop_cpu() - stop single CPU with serialisation
 * @info:	pointer to cr_stop_cpu() parameters
 *
 * Return: Nothing
 */

static void cr_stop_cpu(void *info)
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

/**
 * cr_stop_cpus() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

static void cr_stop_cpus(void)
{
	struct csc_params csc_params;
	int ncpu_this, ncpus, ncpu, ncpus_stopped;

	spin_lock_init(&csc_params.lock);
	csc_params.ncpus_stopped = 0;
	for (ncpu = 0, ncpu_this = smp_processor_id(), ncpus = num_online_cpus();
			ncpu < ncpus; ncpu++) {
		if (ncpu != ncpu_this) {
			smp_call_function_single(ncpu, cr_stop_cpu, &csc_params, 0);
		}
	}
	do {
		spin_lock(&csc_params.lock);
		ncpus_stopped = csc_params.ncpus_stopped;
		spin_unlock(&csc_params.lock);
	} while (ncpus_stopped < (ncpus - 1));
}
#endif /* CONFIG_SMP */
#elif defined(__FreeBSD__)
#endif /* defined(__linux__) || defined(__FreeBSD__) */

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

static void __attribute__((aligned(PAGE_SIZE))) cr_clear(void)
{
#if defined(__linux__)
	int this_cpu;
#elif defined(__FreeBSD__)
#endif /* defined(__linux__) || defined(__FreeBSD__) */
	struct cr3 cr3;

#if defined(__linux__) && defined(CONFIG_SMP)
	this_cpu = get_cpu();
	cr_stop_cpus();
#endif /* defined(__linux__) && defined(CONFIG_SMP) */
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
/* }}} */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw ts=8 tw=120
 */
