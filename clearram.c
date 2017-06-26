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

/* {{{ CPU data structures
 */

/*
 * Control Register 3 (CR3) in Long Mode, as per:
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

/*
 * {4-Kbyte,2-Mbyte,1-Gbyte} {PML4,PDP,PD,PT}E in Long Mode, as per:
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
	unsigned		pat:1;
	unsigned		mbz:18;
	uintptr_t		pfn_base:21;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));
struct page_ent_2M {
	enum pe_bits		bits:9;
	unsigned		avl0_2:3;
	unsigned		pat:1;
	unsigned		mbz:8;
	uintptr_t		pfn_base:31;
	unsigned		avl3_12:11, nx:1;
} __attribute__((packed));

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
/* {{{ Data structures
 */

/**
 * cr_{map,xlate}_phys_to_virt() PFN-VA mapping
 */

struct cr_map_phys_node {
	uintptr_t			pfn;
	uintptr_t			va;
	struct cr_map_phys_node *	next;
};

/**
 * cr_pmem_walk_*() parameters
 */

#if defined(__linux__)
struct cpw_params {
	unsigned	new_nid:1, restart:1;
	int		nid;
	uintptr_t	node_base, node_limit;
	uintptr_t	pfn_cur;

	unsigned	combine_fini:1;
	uintptr_t	combine_last_base, combine_last_limit;

	uintptr_t *	filter;
	uintptr_t	filter_ncur, filter_nmax;
	uintptr_t	filter_last_base, filter_last_limit;
};
#define INIT_CPW_PARAMS(p) do {			\
		(p)->new_nid = 1;		\
		(p)->restart = 1;		\
		(p)->nid = 0;			\
		(p)->combine_fini = 0;		\
		(p)->filter_last_base = 0;	\
		(p)->filter_last_limit = 0;	\
	} while(0)
#elif defined(__FreeBSD__)
struct cpw_params {
	int		nid;
	uintptr_t *	filter;
	uintptr_t	filter_ncur, filter_nmax;
	uintptr_t	filter_last_base, filter_last_limit;
};
#define INIT_CPW_PARAMS(p) do {			\
		(p)->nid = 0;			\
		(p)->filter_last_base = 0;	\
		(p)->filter_last_limit = 0;	\
	} while(0)
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/**
 * cr_map_page{,s}() parameters
 */

struct cmp_params {
	struct page_ent *		pml4;
	uintptr_t			map_base, map_cur, map_limit;
	struct cr_map_phys_node *	map_phys_base, *map_phys_cur;
	uintptr_t			map_phys_limit;
	struct cr_map_phys_node *	map_phys_head;
};
#define INIT_CMP_PARAMS(p) do {			\
		memset((p), 0, sizeof(*(p)));	\
	} while(0)

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
/* }}} */
/* {{{ Static subroutine prototypes and preprocessor macros
 */

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
static int cr_pmem_walk_nocombine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit);
static int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
static uintptr_t cr_virt_to_phys(uintptr_t va);
static ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)));
static int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *));
static int cr_init_cdev(struct clearram_exit_params *exit_params);
void clearram_exit(void);
#ifdef CONFIG_SMP
static void cr_cpu_stop_one(void *info);
#endif /* CONFIG_SMP */
static void cr_cpu_stop_all(void);
static void cr_free(void *p, void (*pfree)(const void *));
#define CR_SORT(a, b, c, d)	sort(a, b, c, d, NULL)
#elif defined(__FreeBSD__)
static int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
static uintptr_t cr_virt_to_phys(uintptr_t va);
static d_write_t __attribute__((noreturn)) cr_cdev_write;
static int clearram_evhand(struct module *m, int what, void *arg);
static int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void *unused);
static int cr_init_cdev(struct clearram_exit_params *exit_params);
void clearram_exit(void);
static void cr_cpu_stop_all(void);
static void cr_free(void *p, void *unused);
#define CR_SORT(a, b, c, d)	qsort(a, b, c, d)
#endif /* defined(__linux__) || defined(__FreBSD__) */

/*
 * Page mapping logic and helper subroutines
 */
static int cr_map_phys_to_virt_set(struct cr_map_phys_node **map_phys_head, struct cr_map_phys_node **map_phys_cur, uintptr_t map_phys_limit, uintptr_t pfn, uintptr_t va);
static int cr_map_phys_to_virt_get(struct cr_map_phys_node *map_phys_cur, uintptr_t pfn, uintptr_t *pva);
static void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int page_nx, int level, int map_direct);
static int cr_map_page(struct cmp_params *params, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, struct page_ent *pt_cur);
static int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int page_nx);
static int cr_map_pages(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx);

/*
 * Kernel module {entry,event} point subroutines
 */
static int cr_init_pfns_compare(const void *lhs, const void *rhs);
static int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
int clearram_init(void);
static void __attribute__((aligned(PAGE_SIZE))) cr_clear(void);
/* }}} */
/* {{{ Static variables
 */
/* Virtual address of Page Map Level 4 page */
static struct page_ent *cr_pml4 = NULL;

/* Resources to release when exiting */
static struct clearram_exit_params cr_exit_params = {0,};

#if defined(__linux__)
/* Character device node file operations */
static struct file_operations cr_cdev_fops = {
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
static struct cdevsw cr_cdev_fops = {
	.d_version = D_VERSION,
	.d_write = cr_cdev_write,
	.d_name = "clearram",
};

static moduledata_t clearram_mod = {
	"clearram", clearram_evhand, NULL,
};
MALLOC_DECLARE(M_CLEARRAM);
MALLOC_DEFINE(M_CLEARRAM, "clearram", "buffer for clearram module");
DECLARE_MODULE(clearram, clearram_mod, SI_SUB_KLD, SI_ORDER_ANY);
#endif /* defined(__linux__) || defined(__FreeBSD__) */
/* }}} */

/* {{{ OS-dependent subroutines
 */

#if defined(__linux__)
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

static int cr_pmem_walk_nocombine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
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

static int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
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

static uintptr_t cr_virt_to_phys(uintptr_t va)
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
 * cr_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

static ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)))
{
	cr_clear();
	__builtin_unreachable();
}

/**
 * cr_init_map() - allocate, zero-fill, and map memory
 *
 * Return: 0 on success, >0 otherwise
 */

static int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void (**pfree)(const void *))
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

/**
 * cr_init_cdev() - create character device node and related structures
 *
 * Return: 0 on success, >0 otherwise
 */

static int cr_init_cdev(struct clearram_exit_params *exit_params)
{
	exit_params->cdev_major = register_chrdev(0, "clearram", &cr_cdev_fops);
	if (exit_params->cdev_major < 0) {
		return exit_params->cdev_major;
	}
	exit_params->cdev_class = class_create(THIS_MODULE, "clearram");
	if (IS_ERR(exit_params->cdev_class)) {
		unregister_chrdev(exit_params->cdev_major, "clearram");
		return PTR_ERR(exit_params->cdev_class);
	}
	exit_params->cdev_device = device_create(exit_params->cdev_class,
		NULL, MKDEV(exit_params->cdev_major, 0), NULL, "clearram");
	if (IS_ERR(exit_params->cdev_device)) {
		unregister_chrdev(exit_params->cdev_major, "clearram");
		class_destroy(exit_params->cdev_class);
		return PTR_ERR(exit_params->cdev_device);
	} else {
		return 0;
	}
}

/**
 * clearram_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void clearram_exit(void)
{
	if (cr_exit_params.cdev_device) {
		device_destroy(cr_exit_params.cdev_class, MKDEV(cr_exit_params.cdev_major, 0));
	}
	if (cr_exit_params.cdev_class) {
		class_destroy(cr_exit_params.cdev_class);
	}
	if (cr_exit_params.cdev_major) {
		unregister_chrdev(cr_exit_params.cdev_major, "clearram");
	}
	if (cr_exit_params.map) {
		cr_free(cr_exit_params.map, cr_exit_params.map_free_fn);
	}
}

#ifdef CONFIG_SMP
/**
 * cr_cpu_stop_one() - stop single CPU with serialisation
 * @info:	pointer to cr_cpu_stop_one() parameters
 *
 * Return: Nothing
 */

static void cr_cpu_stop_one(void *info)
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
 * cr_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

static void cr_cpu_stop_all(void)
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
 * cr_free() - free previously allocated memory
 *
 * Return: Nothing
 */

static void cr_free(void *p, void (*pfree)(const void *))
{
	if (pfree) {
		pfree(p);
	} else {
		kzfree(p);
	}
}
#elif defined(__FreeBSD__)
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

static int cr_pmem_walk_combine(struct cpw_params *params, uintptr_t *psection_base, uintptr_t *psection_limit)
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

static uintptr_t cr_virt_to_phys(uintptr_t va)
{
	return vtophys(va);
}

/**
 * cr_cdev_write() - character device write(2) file operation subroutine
 *
 * Call cr_clear() upon write(2) to the character device node, which will
 * not return.
 *
 * Return: number of bytes written, <0 on error
 */

static int __attribute__((noreturn)) cr_cdev_write(struct cdev *dev __unused, struct uio *uio __unused, int ioflag __unused)
{
	cr_clear();
	__builtin_unreachable();
}

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

/**
 * cr_init_map() - allocate, zero-fill, and map memory
 *
 * Return: 0 on success, >0 otherwise
 */

static int cr_init_map(void **pbase, void **pcur, uintptr_t *plimit, size_t count, void *unused)
{
	*pbase = malloc(count, M_CLEARRAM, M_WAITOK | M_ZERO);
	if (!*pbase) {
		return -ENOMEM;
	} else {
		return 0;
	}
}

/**
 * cr_init_cdev() - create character device node and related structures
 *
 * Return: 0 on success, >0 otherwise
 */

static int cr_init_cdev(struct clearram_exit_params *exit_params)
{
	return make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		&exit_params->cdev_device, &cr_cdev_fops, 0,
		UID_ROOT, GID_WHEEL, 0600, "clearram");
}

/**
 * clearram_exit() - kernel module exit point
 * 
 * Return: Nothing
 */

void clearram_exit(void)
{
	if (cr_exit_params.cdev_device) {
		destroy_dev(cr_exit_params.cdev_device);
	}
	if (cr_exit_params.map) {
		cr_free(cr_exit_params.map, cr_exit_params.map_free_fn);
	}
}

/**
 * cr_cpu_stop_all() - stop all CPUs with serialisation
 *
 * Return: Nothing
 */

static void cr_cpu_stop_all(void)
{
}

/**
 * cr_free() - free previously allocated memory
 *
 * Return: Nothing
 */

static void cr_free(void *p, void *unused)
{
	free(p, M_CLEARRAM);
}
#endif /* defined(__linux__) || defined(__FreeBSD__) */
/* }}} */
/* {{{ Page mapping logic and helper subroutines
 */

/**
 * cr_map_phys_to_virt_set() - map physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

static int cr_map_phys_to_virt_set(struct cr_map_phys_node **map_phys_head, struct cr_map_phys_node **map_phys_cur, uintptr_t map_phys_limit, uintptr_t pfn, uintptr_t va)
{
	struct cr_map_phys_node *new, *node;

	for (node = *map_phys_head; node && node->next; node = node->next) {
		if (!node->next) {
			break;
		}
	}
	if ((uintptr_t)(*map_phys_cur + 1) >= map_phys_limit) {
		return -ENOMEM;
	} else {
		new = (*map_phys_cur)++;
		new->pfn = pfn;
		new->va = va;
		new->next = NULL;
	}
	if (!node) {
		return *map_phys_head = new, 0;
	} else {
		return node->next = new, 0;
	}
}

/**
 * cr_map_phys_to_virt_get() - translate physical address (PFN) to virtual address
 *
 * Return: 0 on success, <0 otherwise
 */

static int cr_map_phys_to_virt_get(struct cr_map_phys_node *map_phys_head, uintptr_t pfn, uintptr_t *pva)
{
	struct cr_map_phys_node *node;

	for (node = map_phys_head; node; node = node->next) {
		if (node->pfn == pfn) {
			return *pva = node->va, 1;
		}
	}
	return -ENOENT;
}

/**
 * cr_map_init_page_ent() - initialise a single {PML4,PDP,PD,PT} entry
 *
 * Return: Nothing.
 */
static void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int page_nx, int level, int map_direct)
{
	struct page_ent_1G *pe_1G;
	struct page_ent_2M *pe_2M;

	memset(pe, 0, sizeof(*pe));
	pe->bits = PE_BIT_PRESENT | PE_BIT_CACHE_DISABLE | extra_bits;
	pe->nx = page_nx;
	if (map_direct && (level == 3)) {
		pe_1G = (struct page_ent_1G *)pe;
		pe_1G->pfn_base = pfn_base;
	} else
	if (map_direct && (level == 2)) {
		pe_2M = (struct page_ent_2M *)pe;
		pe_2M->pfn_base = pfn_base;
	} else {
		pe->pfn_base = pfn_base;
	}
}

/**
 * cr_map_page() - recursively create one {1G,2M,4K} mapping from VA to PFN in {PML4,PDP,PD,PT}
 * @params:	mapping parameters
 * @va:		virtual address to map at
 * @pfn:	physical address (PFN) to map
 * @page_size:	262144 (1 GB page,) 512 (2 MB page,) or 1 (4 KB page)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry
 * @level:	4 if PML4, 3 if PDP, 2 if PD, 1 if PT
 * @pt_cur:	pointer to {PML4,PDP,PD,PT} to map into
 *
 * Create {1G,2M,4K} mapping for pfn at va in the {PML4,PDP,PD,PT}
 * specified by page_size, pt_cur, and level using the supplied extra_bits
 * and page_nx bit. Lower-order page tables are recursively created on
 * demand. Newly created {PDP,PD,PT} are allocated from the map heap in
 * units of the page size (4096) without blocking.
 *
 * Return: 0 on success, <0 otherwise
 */

static int cr_map_page(struct cmp_params *params, uintptr_t *va, uintptr_t pfn, size_t page_size, enum pe_bits extra_bits, int page_nx, int level, struct page_ent *pt_cur)
{
	int map_direct, err;
	struct page_ent *pt_next;
	unsigned long pt_idx;
	uintptr_t pt_next_pfn, pt_next_va;

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
		cr_map_init_page_ent(&pt_cur[pt_idx], pfn, extra_bits,
			page_nx, level, map_direct);
		(*va) += PAGE_SIZE * page_size;
		return 0;
	} else
	if (!(pt_cur[pt_idx].bits & PE_BIT_PRESENT)) {
		if (params->map_cur >= params->map_limit) {
			return -ENOMEM;
		} else {
			pt_next = (struct page_ent *)params->map_cur;
			params->map_cur += PAGE_SIZE;
		}
		pt_next_pfn = cr_virt_to_phys((uintptr_t)pt_next);
		err = cr_map_phys_to_virt_set(&params->map_phys_head,
				&params->map_phys_cur, params->map_phys_limit,
				pt_next_pfn, (uintptr_t)pt_next);
		if (err < 0) {
			return err;
		}
		cr_map_init_page_ent(&pt_cur[pt_idx], pt_next_pfn,
			extra_bits, page_nx, level, map_direct);
	} else {
		if (map_direct && (level == 3)) {
			pt_next_pfn = ((struct page_ent_1G *)&pt_cur[pt_idx])->pfn_base;
		} else
		if (map_direct && (level == 2)) {
			pt_next_pfn = ((struct page_ent_2M *)&pt_cur[pt_idx])->pfn_base;
		} else {
			pt_next_pfn = pt_cur[pt_idx].pfn_base;
		}
		if ((err = cr_map_phys_to_virt_get(params->map_phys_head,
				pt_next_pfn, &pt_next_va)) < 0) {
			return err;
		} else {
			pt_next = (struct page_ent *)pt_next_va;
		}
	}
	return cr_map_page(params, va, pfn, page_size,
			extra_bits, page_nx, level - 1, pt_next);
}

/**
 * cr_map_pages_from_va() - create contiguous VA to discontiguous PFN mappings in PML4
 *
 * Return: 0 on success, >0 otherwise
 */

static int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int page_nx)
{
	uintptr_t pfn_block_base, va_cur;
	int err;

	va_cur = va_dst;
	for (size_t npage = 0; npage < npages; npage++, va_src += PAGE_SIZE) {
		pfn_block_base = cr_virt_to_phys(va_src);
		err = cr_map_pages(params, &va_cur, pfn_block_base,
				pfn_block_base + 1, extra_bits, page_nx);
		if (err != 0) {
			return err;
		}
	}
	return 0;
}

/**
 * cr_map_pages() - create contiguous VA to PFN mappings in PML4
 * @params:	mapping parameters
 * @va_base:	base virtual address to map at
 * @pfn_base:	base physical address (PFN) to map
 * @pfn_limit:	physical address limit (PFN)
 * @extra_bits:	extra bits to set in {PML4,PDP,PD,PT} entry/ies
 * @page_nx:	NX bit to set or clear in {PML4,PDP,PD,PT} entry/ies
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

static int cr_map_pages(struct cmp_params *params, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, enum pe_bits extra_bits, int page_nx)
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
		if ((err = cr_map_page(params, va_base, pfn, npages,
				extra_bits, page_nx, 4, params->pml4)) != 0) {
			return err;
		}
	}
	return 0;
}
/* }}} */
/* {{{ Kernel module {entry,event} point subroutines
 */

/**
 * cr_init_pfns_compare() - page map PFN database numeric sort comparison function
 * @lhs:	left-hand side PFN
 * @rhs:	right-hand side PFN
 *
 * Return: -1, 1, or 0 if lhs_pfn is smaller than, greater than, or equal to rhs_pfn
 */

static int cr_init_pfns_compare(const void *lhs, const void *rhs)
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
 * cr_pmem_walk_filter() - walk physical memory, combining sections with a list of reserved PFN
 *
 * Return: 0 if no physical memory sections remain, 1 otherwise
 */

static int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit) {
	int err;

	if (!params->filter_last_base
	&&  !params->filter_last_limit) {
		if ((err = cr_pmem_walk_combine(params, &params->filter_last_base,
				&params->filter_last_limit)) < 1) {
			return err;
		}
		params->filter_ncur = 0;
	}
	for (; params->filter_ncur < (params->filter_nmax + 1); params->filter_ncur++) {
		if ((params->filter[params->filter_ncur] <  params->filter_last_base)
		||  (params->filter[params->filter_ncur] >= params->filter_last_limit)) {
			continue;
		} else
		if (params->filter[params->filter_ncur] == params->filter_last_base) {
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
			continue;
		} else {	
			*ppfn_base = params->filter_last_base;
			*ppfn_limit = params->filter[params->filter_ncur];
			params->filter_last_base = params->filter[params->filter_ncur] + 1;
			params->filter_ncur++;
			return 1;
		}
	}
	if (params->filter_last_base < params->filter_last_limit) {
		*ppfn_base = params->filter_last_base;
		*ppfn_limit = params->filter_last_limit;
		params->filter_last_base = params->filter_last_limit = 0;
		return 1;
	} else {
		params->filter_last_base = params->filter_last_limit = 0;
		return cr_pmem_walk_filter(params, ppfn_base, ppfn_limit);
	}
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

static void __attribute__((aligned(PAGE_SIZE))) cr_clear(void)
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
/* }}} */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw ts=8 tw=120
 */
