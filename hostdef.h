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

#ifndef _HOSTDEF_H_
#define _HOSTDEF_H_

#if defined(__FreeBSD__)
/**
 * malloc(9) type
 */
MALLOC_DECLARE(M_CLEARRAM);
#endif /* defined(__FreeBSD__) */

/*
 * Host environment subroutine data structures
 */

/**
 * cr_host_l{append,free}() list & list item
 */
struct crh_list {
	struct crh_litem *	head;
	size_t			item_size;
	uintptr_t		page_cur, page_off;
} __attribute__((packed));
struct crh_litem {
	struct crh_litem *	next;
	unsigned char		item[];
} __attribute__((packed));
#define CRH_LIST_INIT(p, _item_size) do {				\
		(p)->head = NULL;					\
		(p)->item_size = (_item_size);				\
		(p)->page_cur = 0;					\
		(p)->page_off = 0;					\
	} while (0)
#define CRH_LITEM_INIT(li) do {						\
		(li)->next = NULL;					\
	} while (0)

/**
 * XXX
 */
struct crh_lrsvd_item {
	uintptr_t	pfn;
} __attribute__((packed));
#define CRH_LRSVD_ITEM_INIT(li, _pfn) do {				\
		(li)->pfn = (_pfn);					\
	} while (0)

/**
 * XXX
 */
struct crh_malloc_state {
	uintptr_t	tail;
	size_t		rem;
};
#define CRH_INIT_MALLOC_STATE(p, _tail, _rem) do {			\
		(p)->tail = (_tail);					\
		(p)->rem = (_rem);					\
	} while (0)

/**
 * XXX
 */
enum crh_ptl_type {
	CRH_PTL_PAGE_TABLE	= 0x01,
	CRH_PTL_RAM_PAGE	= 0x02,
	CRH_PTL_RSVD_PAGE	= 0x04,
};
struct crh_pages_tree_leaf {
	enum crh_ptl_type	type;
	uintptr_t		va_pt, va_ram, va_rsvd;
} __attribute__((packed));
struct crh_pages_tree_node {
	union {
		struct crh_pages_tree_leaf *leaf[512];
		struct crh_pages_tree_node *node[512];
	} u;
} __attribute__((packed));
#define CRH_INIT_PAGES_TREE_LEAF(p, _type, _va_pt, _va_ram, _va_rsvd) do {\
		(p)->type = (_type);					\
		(p)->va_pt = (_va_pt);					\
		(p)->va_ram = (_va_ram);				\
		(p)->va_rsvd = (_va_rsvd);				\
	} while (0)
#define CRH_INIT_PAGES_TREE_NODE(p) do {				\
		memset((p), 0, sizeof((*p)));				\
	} while (0)

/**
 * cr_host_pmap_walk() parameters
 */
struct crh_pmap_walk_params {
	int			restart;
#if defined(__linux__)
	struct resource	*	res_cur;
#elif defined(__FreeBSD__)
	int			nid;
#endif /* defined(__linux__) || defined(__FreeBSD__) */
};
#define CRH_INIT_PMAP_WALK_PARAMS(p) do {				\
		memset((p), 0, sizeof(*(p)));				\
		(p)->restart = 1;					\
	} while (0)

#if defined(__linux__) && defined(CONFIG_SMP)
/**
 * cr_host_stop_cpu() parameters
 */
struct crh_stop_cpu_params {
	spinlock_t		lock;
	int			ncpus_stopped;
};
#endif /* defined(__linux__) && defined(CONFIG_SMP) */

/**
 * CRH_ASSERT() - evaluate soft or hard assertion and return -EINVAL or bugcheck
 */
#if defined(DEBUG)
# if defined(__linux__)
#  define CRH_ASSERT(x, ...)	BUG_ON(!(x))
# elif defined(__FreeBSD__)	KASSERT((x), (y))
# endif /* defined(__linux__) || defined(__FreeBSD__) */
#else
# define CRH_ASSERT(x, y...) do {					\
		if (unlikely(!(x))) {					\
			cr_host_soft_assert_fail(KERN_ERR y);		\
			return -EINVAL;					\
		}							\
	} while (0)
#endif /* defined(DEBUG) */

/**
 * CRH_PRINTK_{DEBUG,ERR,INFO}() - print string to kernel ring buffer at level {DEBUG,ERR,INFO}
 */
#if defined(__linux__)
# if defined(DEBUG)
#  define CRH_PRINTK_DEBUG(x, ...)					\
	printk(KERN_DEBUG "%s: "x"\n", __func__, ##__VA_ARGS__)
# else
#  define CRH_PRINTK_DEBUG(x, ...)
# endif /* defined(DEBUG) */
# define CRH_PRINTK_ERR(x, ...)						\
	printk(KERN_ERR "%s: "x"\n", __func__, ##__VA_ARGS__)
# define CRH_PRINTK_INFO(x, ...)					\
	printk(KERN_INFO "%s: "x"\n", __func__, ##__VA_ARGS__)
#elif defined(__FreeBSD__)
# if defined(DEBUG)
#  define CRH_PRINTK_DEBUG(x, ...)					\
	printf("%s: "x"\n", __func__, ##__VA_ARGS__)
# else
#  define CRH_PRINTK_DEBUG(x, ...)
# endif /* defined(DEBUG) */
# define CRH_PRINTK_ERR(x, ...)						\
	printf("%s: "x"\n", __func__, ##__VA_ARGS__)
# define CRH_PRINTK_INFO(x, ...)					\
	printf("%s: "x"\n", __func__, ##__VA_ARGS__)
#endif /* defined(__linux__) || defined(__FreeBSD__) */

/**
 * CRH_SAFE_{ADD,SUB}() - validate and perform {addition,substraction} on ranged uintptr_t
 */
#define CRH_SAFE_ADD(base, limit, offset) ({				\
	CRH_ASSERT((uintptr_t)(limit) >= (uintptr_t)(base)) && (((uintptr_t)(limit) - (uintptr_t)(base)) >= (uintptr_t)(offset)),\
		"%s: base=%p - limit=%p < offset=%p", __func__, (uintptr_t)(base), (uintptr_t)(limit), (uintptr_t)(offset))\
			(base) += (offset); })
#define CRH_SAFE_SUB(base, cur, delta) ({				\
	CRH_ASSERT((uintptr_t)(cur) >= (uintptr_t)(base)) && (((uintptr_t)(cur) - (uintptr_t)(base)) >= (uintptr_t)(delta)),\
		"%s: cur=%p - base=%p < delta=%p", __func__, (uintptr_t)(cur), (uintptr_t)(base), (uintptr_t)(delta))\
			(base) -= (delta); })

/**
 * CRH_VALID_{BASE,PTR,RANGE}() - validate base address alignment, pointer, or ranged uintptr_t
 */
#define CRH_VALID_BASE(base, block_size)				\
	CRH_ASSERT(block_size && !((uintptr_t)(base) & ((block_size) - 1)),\
		"%s: base=%p, block_size=%p", (uintptr_t)(base), (uintptr_t)(block_size))
#define CRH_VALID_PTR(x)						\
	CRH_ASSERT((x),							\
		"%s: !"#x, __func__, (x))
#define CRH_VALID_RANGE(base, limit, cur)				\
	CRH_ASSERT(((uintptr_t)(cur) >= (uintptr_t)(base)) && ((uintptr_t)(cur) < (uintptr_t)(limit)),\
		"%s: base=%p, limit=%p, cur=%p", (uintptr_t)(base), (uintptr_t)(limit), (uintptr_t)(cur))

/*
 * Host environment subroutines
 */
int cr_host_cdev_init(struct cr_host_state *state);
#if defined(__linux__)
ssize_t __attribute__((noreturn)) cr_host_cdev_write(struct file *file __attribute__((unused)), const char __user *buf __attribute__((unused)), size_t len, loff_t *ppos __attribute__((unused)));
#elif defined(__FreeBSD__)
d_write_t __attribute__((noreturn)) cr_host_cdev_write;
#endif /* defined(__linux__) || defined(__FreeBSD__) */
void cr_host_cpu_stop_all(void);
int cr_host_list_append(struct crh_list *list, void **pitem);
void cr_host_list_free(struct crh_list *list);
void cr_host_lkm_exit(void);
int cr_host_lkm_init(void);
int cr_host_map_alloc_pt(struct cra_page_ent *pml4, uintptr_t va, enum cra_pe_bits extra_bits, int pages_nx, int level, int map_direct, struct cra_page_ent *pe, struct cra_page_ent **ppt_next);
void cr_host_map_free(struct cra_page_ent *pml4, void (*vmfree)(void *));
int cr_host_map_link_ram_page(uintptr_t pfn, uintptr_t va);
int cr_host_map_link_rsvd_page(uintptr_t pfn, uintptr_t va);
int cr_host_map_xlate_pfn(enum crh_ptl_type type, uintptr_t pfn, uintptr_t *pva);
int cr_host_pmap_walk(struct crh_pmap_walk_params *params, uintptr_t *psection_base, uintptr_t *psection_limit, uintptr_t *psection_cur);
void cr_host_soft_assert_fail(const char *fmt, ...);
uintptr_t cr_host_virt_to_phys(uintptr_t va);
void *cr_host_malloc(struct crh_malloc_state *mstate, size_t nitems, size_t size);
void cr_host_mfree(struct crh_malloc_state *mstate, void *p);
void *cr_host_vmalloc(size_t nitems, size_t size);
void cr_host_vmfree(void *p);
#endif /* !_HOSTDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
