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

#ifndef __linux__
#error Only Linux is supported at present.
#else
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

#ifndef CONFIG_X86_64
#error Only x86_64 is supported at present.
#endif /* !CONFIG_X86_64 */
#if PAGE_SIZE != 0x1000
#error Only 4 KB pages are supported at present.
#endif /* PAGE_SIZE != 0x1000 */
#endif /* __linux__ */

/*
 * Control Register 3 (CR3) in Long Mode, as per:
 * AMD64 Architecture Programmer’s Manual, Volume 2: System Programming
 * Section 5.3.2, pages 130-131.
 */

struct cr3 {
	enum cr3_bits {
		CR3_BIT_WRITE_THROUGH	= 0x008,
		CR3_BIT_CACHE_DISABLE	= 0x010,
	}			bits:5;
	unsigned		ign1:7;
	uintptr_t		pml4_pfn_base:40;
	unsigned		ign2:12;
} __packed;
#define CR_INIT_CR3(cr3, new_pml4_pfn_base)				\
	do {								\
		(cr3)->bits = CR3_BIT_WRITE_THROUGH;			\
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
struct __packed page_ent {
enum pe_bits			bits:9;
unsigned			avl0_2:3;
uintptr_t			pfn_base:40;
unsigned			avl3_12:11, nx:1;
};
struct __packed page_ent_1G {
enum pe_bits			bits:9;
unsigned			avl0_2:3;
unsigned			pat:1, mbz:18;
uintptr_t			pfn_base:21;
unsigned			avl3_12:11, nx:1;
};
struct __packed page_ent_2M {
enum pe_bits			bits:9;
unsigned			avl0_2:3;
unsigned			pat:1, mbz:8;
uintptr_t			pfn_base:31;
unsigned			avl3_12:11, nx:1;
};

#define CR_INIT_PAGE_ENT(pe, bits_extra)				\
	do {								\
		*((unsigned long long *)((pe))) = 0ULL;			\
		(pe)->bits = PE_BIT_PRESENT | PE_BIT_READ_WRITE |	\
			PE_BIT_USER_SUPERVISOR |			\
			PE_BIT_WRITE_THROUGH | (bits_extra);		\
	} while (0)
#define CR_PAGE_ENT_IDX_MASK	0x1ff
#define CR_VA_TO_PML4_IDX(va)	(((va) >> (9 + 9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_VA_TO_PDP_IDX(va)	(((va) >> (9 + 9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_PDPE_MASK		0x3fffffff
#define CR_PDPE_SIZE		(512 * 512)
#define CR_VA_TO_PD_IDX(va)	(((va) >> (9 + 12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_PDE_MASK		0x1fffff
#define CR_PDE_SIZE		(512)
#define CR_VA_TO_PT_IDX(va)	(((va) >> (12)) & CR_PAGE_ENT_IDX_MASK)
#define CR_PTE_SIZE		(1)
#define CR_CANONICAL_KVA(addr)	(((int64_t)(addr) << 16) >> 16)
#define CR_VA_TO_VPN(va)	(((uintptr_t)(va)) >> 12)
#define CR_VPN_TO_VA(va)	(CR_CANONICAL_KVA(((uintptr_t)(va)) << 12))

/*
 * Static subroutine prototypes
 */

/* Page mapping logic and helper subroutines */
static uintptr_t cr_virt_to_phys(uintptr_t va);
static void cr_map_set_pfn_base( struct page_ent *pe, int level, uintptr_t pfn_base);
static void *cr_map_translate_pfn_base( struct page_ent *pe, int level);
static void cr_map_translate(struct page_ent *pt_cur, int level);
static int cr_map_page(struct page_ent *pt_cur, uintptr_t *va, uintptr_t pfn, size_t page_size, int level, uintptr_t *map_base, uintptr_t map_limit);
static int cr_map_pages(struct page_ent *pml4, uintptr_t *va_base, uintptr_t pfn_base, uintptr_t pfn_limit, uintptr_t *map_base, uintptr_t map_limit);

/* Kernel module {exit,entry} point and helper subroutines */
void clearram_exit(void);
static ssize_t __attribute__((noreturn)) cr_cdev_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);
static int cr_map_pfns_compare(const void *lhs, const void *rhs);
static void *cr_maps_alloc(unsigned long size, void (**map_free)(const void *));
static int cr_node_iterate(size_t nid, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
int clearram_init(void);

/* Zero-fill RAM code and helper subroutines */
#ifdef CONFIG_SMP
static void cr_stop_cpu(void *info);
static void cr_stop_cpus(void);
#endif /* CONFIG_SMP */
static void __attribute__((aligned(PAGE_SIZE))) cr_clear(void);

/*
 * Static variables
 */

/* Count of and pointer to base of page map. */
static
unsigned long		cr_map_npages = 0;
static
void *			cr_map = NULL;

/* Pointer to base of PFN list backing cr_map. */
static
uintptr_t *		cr_map_pfns = NULL;

/* Function pointer to either of {vfree,kfree}(), releasing cr_map{,_pfns}. */
static
void			(*cr_map_free)(const void *) = NULL;
static
void			(*cr_map_pfns_free)(const void *);

/* Page Map Level 4 */
static
struct page_ent *	cr_pml4 = NULL;

/* Character device node file operations */
static
struct file_operations	cr_cdev_fops = {
	.write		= cr_cdev_write,
};

/* Character device node major number, class, and device pointers. */
static
int			cr_cdev_major = 0;
static
struct class *		cr_cdev_class = NULL;
static
struct device *		cr_cdev_device = NULL;

/*
 * Page mapping logic and helper subroutines
 */

static
uintptr_t
cr_virt_to_phys(
	uintptr_t	va
)
{
	pgd_t *		pgd;
	pud_t *		pud;
	pmd_t *		pmd;
	pte_t *		pte;

	if (!(pgd = pgd_offset(current->mm, va))
	||  !(pud = pud_offset(pgd, va))
	||  !(pmd = pmd_offset(pud, va))
	||  !(pte = pte_offset_map(pmd, va))) {
		return -EINVAL;
	} else {
		return ((*(unsigned long *)pte) >> 12) & 0xffffffffff;
	};
}

static
void
cr_map_set_pfn_base(
	struct page_ent *	pe,
	int			level,
	uintptr_t		pfn_base
)
{
	struct page_ent_2M *	pe_2M;
	struct page_ent_1G *	pe_1G;

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
	};
}

static
void *
cr_map_translate_pfn_base(
	struct page_ent *	pe,
	int			level
)
{
	struct page_ent_2M *	pe_2M;
	struct page_ent_1G *	pe_1G;
	uintptr_t		pfn_base;

	switch (level) {
	case 4:
		pfn_base =pe->pfn_base; break;
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
	};
	return (void *)CR_VPN_TO_VA(pfn_base);
}

static
void
cr_map_translate(
	struct page_ent *	pt_cur,
	int			level
)
{
	unsigned long		pt_idx;
	struct page_ent *	pt_next;

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
		};
	};
}

static
int
cr_map_page(
	struct page_ent *	pt_cur,
	uintptr_t *		va,
	uintptr_t		pfn,
	size_t			page_size,
	int			level,
	uintptr_t *		map_base,
	uintptr_t		map_limit
)
{
	int			map_direct, extra_bits;
	struct page_ent *	pt_next;
	unsigned long		pt_idx;

	extra_bits = 0;
	switch (level) {
	case 4: pt_idx = CR_VA_TO_PML4_IDX(*va);
		map_direct = 0;
		break;
	case 3:	pt_idx = CR_VA_TO_PDP_IDX(*va);
		if ((map_direct = (page_size == CR_PDPE_SIZE))) {
			extra_bits = PE_BIT_PAGE_SIZE;
		};
		break;
	case 2:	pt_idx = CR_VA_TO_PD_IDX(*va);
		if ((map_direct = (page_size == CR_PDE_SIZE))) {
			extra_bits = PE_BIT_PAGE_SIZE;
		};
		break;
	case 1:	pt_idx = CR_VA_TO_PT_IDX(*va);
		map_direct = 1;
		break;
	default:
		return -EINVAL;
	};
	if (map_direct) {
		CR_INIT_PAGE_ENT(&pt_cur[pt_idx], extra_bits);
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
		};
		CR_INIT_PAGE_ENT(&pt_cur[pt_idx], 0);
		cr_map_set_pfn_base(&pt_cur[pt_idx], level,
			CR_VA_TO_VPN(pt_next));
	} else {
		pt_next = cr_map_translate_pfn_base(&pt_cur[pt_idx], level);
	};
	return cr_map_page(pt_next, va, pfn, page_size, level - 1,
			map_base, map_limit);
}

static
int
cr_map_pages(
	struct page_ent *	pml4,
	uintptr_t *		va_base,
	uintptr_t		pfn_base,
	uintptr_t		pfn_limit,
	uintptr_t *		map_base,
	uintptr_t		map_limit
)
{
	int			err;
	unsigned long		pfn;
	size_t			npages;

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
		};
		if ((err = cr_map_page(pml4, va_base,
				pfn, npages, 4,
				map_base, map_limit)) != 0) {
			return err;
		};
	};
	return 0;
}

/*
 * Kernel module {exit,entry} point and helper subroutines
 */

module_exit(clearram_exit);
module_init(clearram_init);
MODULE_AUTHOR("Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>");
MODULE_DESCRIPTION("clearram");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("clearram");

void
clearram_exit(
	void
)
{
	if (cr_cdev_device) {
		device_destroy(cr_cdev_class, MKDEV(cr_cdev_major, 0));
	};
	if (cr_cdev_class) {
		class_destroy(cr_cdev_class);
	};
	if (cr_cdev_major) {
		unregister_chrdev(cr_cdev_major, "clearram");
	};
	if (cr_map_pfns) {
		cr_map_pfns_free(cr_map_pfns);
	};
	if (cr_map) {
		cr_map_free(cr_map);
	};
}

static
ssize_t
__attribute__((noreturn))
cr_cdev_write(struct file *file,
	const char __user *	buf,
	size_t			len,
	loff_t *		ppos
)
{
	cr_clear();
	__builtin_unreachable();
}

static
int
cr_map_pfns_compare(
	const void *	lhs,
	const void *	rhs
)
{
	uintptr_t	lhs_pfn, rhs_pfn;

	lhs_pfn = *(uintptr_t *)lhs;
	rhs_pfn = *(uintptr_t *)rhs;
	if (lhs_pfn < rhs_pfn) {
		return -1;
	} else
	if (lhs_pfn > rhs_pfn) {
		return 1;
	} else {
		return 0;
	};
}

static
void *
cr_maps_alloc(
	unsigned long	size,
	void		(**map_free)(const void *)
)
{
	void *	map;

	map = vmalloc(size);
	if (!map) {
		map = kmalloc(size, GFP_KERNEL);
		if (!map) {
			return map;
		} else {
			*map_free = &kfree;
		};
	} else {
		*map_free = &vfree;
	};
	return memset(map, 0, size), map;
}

static
int
cr_node_iterate(
	size_t		nid,
	uintptr_t *	ppfn_base,
	uintptr_t *	ppfn_limit
)
{
	static uintptr_t	pfn_node_start = 0,
				pfn_node_limit = 0,
				pfn = 0;

	if (nid >= MAX_NUMNODES) {
		return -ENOENT;
	} else
	if (!pfn) {
		pfn_node_start = node_start_pfn(nid);
		pfn_node_limit = pfn_node_start + node_spanned_pages(nid);
		if (pfn_node_limit <= pfn_node_start) {
			return -EINVAL;
		} else {
			pfn = pfn_node_start;
			*ppfn_base = pfn_node_start; *ppfn_limit = 0;
		};
	};
	for (pfn = pfn; pfn < pfn_node_limit; pfn += PAGES_PER_SECTION) {
		if (!present_section_nr(pfn_to_section_nr(pfn))) {
			*ppfn_limit = pfn;
			return pfn = *ppfn_limit + 1, 1;
		} else
		if ((pfn + PAGES_PER_SECTION) >= pfn_node_limit) {
			*ppfn_limit = pfn_node_limit;
			return pfn = *ppfn_limit + 1, 1;
		} else {
			continue;
		};
	};
	pfn_node_start = pfn_node_limit = pfn = 0;
	*ppfn_base = *ppfn_limit = 0;
	return 0;
}

int
clearram_init(
	void
)
{
	uintptr_t			cr_clear_base;
	extern uintptr_t		cr_clear_limit;
	uintptr_t			map_cur, map_limit;
	size_t				npfn, nid;
	uintptr_t			va, pfn_block_base, pfn_block_limit;
	int				err;

	/*
	 * Enforce max. 4 KB size constraint on cr_clear()
	 * Obtain total amount of page frames on host
	 * Derive amount of PT, PD, and PDP required to map
	 * Add 1 PT, 1 PD, and 1 PDP to amount of page frames required to map .clearram
	 */
	cr_clear_base = (uintptr_t)&cr_clear;
	if ( (cr_clear_base & 0xfff)
	||  ((cr_clear_limit - cr_clear_base) > PAGE_SIZE)) {
		return -EINVAL;
	};
	for (nid = 0, cr_map_npages = 0; nid < MAX_NUMNODES; nid++) {
		while ((err = cr_node_iterate(nid, &pfn_block_base,
				&pfn_block_limit)) == 1) {
			cr_map_npages += (pfn_block_limit - pfn_block_base);
		};
		if (err < 0) {
			return err;
		};
	};
	cr_map_npages =
		      (DIV_ROUND_UP_ULL(cr_map_npages, (512)))			/* Page Tables */
		    + (DIV_ROUND_UP_ULL(cr_map_npages, (512 * 512)))		/* Page Directories */
		    + (DIV_ROUND_UP_ULL(cr_map_npages, (512 * 512 * 512)))	/* Page Directory Pointer pages */
		    + (1);							/* Page Map Level 4 */
	cr_map_npages += (1 + 1 + 1);						/* {PDP,PD,PT} to map code at top of VA */
	cr_map_npages += (1 + 1 + 1);						/* {PDP,PD,PT} to map code at original VA */

	/*
	 * Allocate paging-related tables page frames & PFN list
	 * Initialise map
	 * Create & sort PFN list
	 */
	cr_map = cr_maps_alloc(cr_map_npages * PAGE_SIZE, &cr_map_free);
	if (!cr_map) {
		return clearram_exit(), -ENOMEM;
	} else {
		map_limit = (uintptr_t)cr_map + (cr_map_npages * PAGE_SIZE);
		map_cur = (uintptr_t)cr_map + PAGE_SIZE;
		cr_pml4 = cr_map;
	};
	cr_map_pfns = cr_maps_alloc((cr_map_npages + 1) * sizeof(uintptr_t), &cr_map_pfns_free);
	if (!cr_map_pfns) {
		return clearram_exit(), -ENOMEM;
	} else
	for (npfn = 0; npfn < cr_map_npages; npfn++) {
		cr_map_pfns[npfn] = cr_virt_to_phys((uintptr_t)cr_map + (npfn * PAGE_SIZE));
	};
	cr_map_pfns[npfn] = cr_virt_to_phys(cr_clear_base);
	sort(cr_map_pfns, cr_map_npages + 1, sizeof(uintptr_t),
		&cr_map_pfns_compare, NULL);

	/*
	 * Iterate over consecutive page frame nodes
	 * Given PFN list match, map prefix, otherwise, map everything
	 */
	for (nid = 0, va = 0x0LL; nid < MAX_NUMNODES; nid++) {
		while ((err = cr_node_iterate(nid, &pfn_block_base,
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
						cr_map_pfns[npfn], &map_cur, map_limit);
					if (err != 0) {
						return clearram_exit(), err;
					};
					pfn_block_base = cr_map_pfns[npfn] + 1;
					if (pfn_block_base >= pfn_block_limit) {
						break;
					};
				};
			};
			if (pfn_block_base < pfn_block_limit) {
				err = cr_map_pages(cr_pml4, &va, pfn_block_base, pfn_block_limit,
					&map_cur, map_limit);
				if (err != 0) {
					return clearram_exit(), err;
				};
			};
		};
		if (err < 0) {
			return clearram_exit(), err;
		};
	};

	/*
	 * Map code page(s)
	 * Translate VA to PFN
	 */
	pfn_block_base = cr_virt_to_phys(cr_clear_base);
	err = cr_map_pages(cr_pml4, &va, pfn_block_base,
			pfn_block_base + 1, &map_cur, map_limit);
	if (err != 0) {
		return clearram_exit(), err;
	};

	va = cr_clear_base;
	err = cr_map_pages(cr_pml4, &va, pfn_block_base,
			pfn_block_base + 1, &map_cur, map_limit);
	if (err != 0) {
		return clearram_exit(), err;
	};

	cr_map_translate(cr_pml4, 4);

	/*
	 * Create cdev
	 */
	cr_cdev_major = register_chrdev(0, "clearram", &cr_cdev_fops);
	if (cr_cdev_major < 0) {
		return clearram_exit(), cr_cdev_major;
	};
	cr_cdev_class = class_create(THIS_MODULE, "clearram");
	if (IS_ERR(cr_cdev_class)) {
		return clearram_exit(), PTR_ERR(cr_cdev_class);
	};
	cr_cdev_device = device_create(cr_cdev_class, NULL, MKDEV(cr_cdev_major, 0), NULL, "clearram");
	if (IS_ERR(cr_cdev_device)) {
		return clearram_exit(), PTR_ERR(cr_cdev_device);
	};

	return 0;
}

/*
 * Zero-fill RAM code and helper subroutines
 */

#ifdef CONFIG_SMP
static
void
cr_stop_cpu(
	void *	info
)
{
	struct semaphore *	stop_cpus_sem;

	stop_cpus_sem = info;
	__asm volatile(
		"\t	cli\n");
	up(stop_cpus_sem);
	__asm volatile(
		"\t1:	hlt\n"
		"\t	jmp 1b\n");
}

static
void
cr_stop_cpus(
	void
)
{
	struct semaphore	stop_cpus_sem;
	int			ncpu_cur, ncpus, ncpu;

	sema_init(&stop_cpus_sem, num_online_cpus());
	ncpu_cur = smp_processor_id();
	for (ncpu = 0, ncpus = num_online_cpus(); ncpu < ncpus; ncpu++) {
		if (ncpu != ncpu_cur) {
			smp_call_function_single(ncpu, cr_stop_cpu, &stop_cpus_sem, 0);
		};
	};
	down(&stop_cpus_sem);
}
#endif /* CONFIG_SMP */

static
void
__attribute__((aligned(PAGE_SIZE)))
cr_clear(
	void
)
{
	int		this_cpu;
	struct cr3	cr3;

	/*
	 * Disable interrupts on all CPUs and halt all other CPUs.
	 * Disable paging, initialise %%cr3 from the PFN of the PML4 while paging
	 * is disabled, and then re-enable paging in order to effect a TLB flush
	 * (including global paging tables entries.)
	 * Zero-fill from 0x0L onwards, eventually causing a triple fault and
	 * thus, CPU reset.
	 */
	this_cpu = get_cpu();
#ifdef CONFIG_SMP
	cr_stop_cpus();
#endif /* CONFIG_SMP */
	CR_INIT_CR3(&cr3, cr_virt_to_phys((uintptr_t)cr_pml4));
	__asm volatile(
		"\tcli\n"
		"\n"
		"\tmovq		%%cr4,		%%rax\n"
		"\tmovq		%%rax,		%%rbx\n"
		"\tandb		$0x7f,		%%al\n"		/* Clear PGE (Page Global Enabled) bit */
		"\tmovq		%%rax,		%%cr4\n"	/* Disable paging */
		"\tmovq		%0,		%%r8\n"
		"\tmovq		%%r8,		%%cr3\n"	/* Write PML4 PFN */
		"\tmovq		%%rbx,		%%cr4\n"	/* Re-enable paging */
		"\n"
		"\tcld\n"
		"\txorq		%%rcx,		%%rcx\n"
		"\tdecq		%%rcx\n"			/* Count = 0xFFFFFFFFFFFFFFFFLL */
		"\txorq		%%rax,		%%rax\n"	/* Store = 0x0000000000000000LL */
		"\txorq		%%rdi,		%%rdi\n"	/* Dest. = 0x0000000000000000LL */
		"\trep		stosq\n"			/* Zero-fill & triple fault */
		"\t1:		hlt\n"
		"\t		jmp 1b\n"
		"\n"
		"\t.align	0x1000\n"
		"\tcr_clear_limit:\n"
		"\t.quad	.\n"
		:: "r"(cr3)
		: "r8", "rax", "rbx", "rcx", "rdi", "flags");
}

/*
 * vim:ts=8 sw=8 tw=120 noexpandtab
 */
