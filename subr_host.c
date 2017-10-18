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
 * Host environment subroutines
 */

/**
 * XXX
 */
static int crp_host_map_link_page(enum crh_ptl_type type, uintptr_t pfn, uintptr_t va) {
	int err, level;
	uintptr_t idx;
	struct crh_pages_tree_node **node;
	struct crh_pages_tree_leaf **leaf;
	struct crh_lrsvd_item *item;

	for (level = 4, node = NULL, leaf = NULL; level > 0; level--) {
		switch (level) {
		case 4: idx = (pfn >> (9 + 9 + 9)) & 0x1ff;
			node = &cr_host_state.host_pages_tree.u.node[idx];
			break;
		case 3: idx = (pfn >> (9 + 9)) & 0x1ff;
			node = &((*node)->u.node[idx]); break;
		case 2: idx = (pfn >> (9)) & 0x1ff;
			node = &((*node)->u.node[idx]); break;
		case 1: idx = (pfn) & 0x1ff;
			leaf = &((*node)->u.leaf[idx]); break;
		}
		if (level > 1) {
			if (!(*node)) {
				if (!((*node) = cr_host_vmalloc(1, sizeof(**node)))) {
					return -ENOMEM;
				} else {
					CRH_INIT_PAGES_TREE_NODE((*node));
				}
			}
		} else {
			break;
		}
	}
	if (!(*leaf)) {
		if (!((*leaf) = cr_host_malloc(
				&cr_host_state.host_malloc_state,
				1, sizeof(**leaf)))) {
			return -ENOMEM;
		}
		CRH_INIT_PAGES_TREE_LEAF((*leaf), 0, 0, 0, 0);
	}
	if (type & CRH_PTL_PAGE_TABLE) {
		(*leaf)->type |= CRH_PTL_PAGE_TABLE;
		(*leaf)->va_pt = va;
		if ((err = cr_host_list_append(&cr_host_state.host_lrsvd,
				(void **)&item)) < 0) {
			return err;
		} else {
			CRH_LRSVD_ITEM_INIT(item, pfn);
		}
	}
	if (type & CRH_PTL_RAM_PAGE) {
		(*leaf)->type |= CRH_PTL_RAM_PAGE;
		(*leaf)->va_ram = va;
	}
	if (type & CRH_PTL_RSVD_PAGE) {
		(*leaf)->type |= CRH_PTL_RSVD_PAGE;
		(*leaf)->va_rsvd = va;
	}
	return 0;
}

/**
 * XXX
 */

int cr_host_list_append(struct crh_list *list, void **pitem)
{
	size_t isize_aligned;
	uintptr_t p;
	struct crh_litem *li, *li_last;

	isize_aligned = sizeof(*li) + list->item_size;
	if (isize_aligned % 8) {
		isize_aligned &= 0xf8;
	}
	if (!list->head
	||  (isize_aligned > (PAGE_SIZE - list->page_off))) {
		if (!(p = (uintptr_t)cr_host_vmalloc(1, PAGE_SIZE))) {
			return -ENOMEM;
		} else {
			list->page_cur = p, list->page_off = 0;
		}
	}
	li = (struct crh_litem *)(list->page_cur + list->page_off);
	CRH_LITEM_INIT(li);
	list->page_off += isize_aligned;
	if (!list->head) {
		list->head = li;
	} else
	for (li_last = list->head; li_last; li_last = li_last->next) {
		if (!li_last->next) {
			li_last->next = li;
			break;
		}
	}
	return *pitem = &li->item, 0;
}

/**
 * XXX
 */

void cr_host_list_free(struct crh_list *list)
{
	struct crh_litem *li, *li_next;

	for (li = li_next = list->head; li;
			li = li_next) {
		li_next = li->next;
		if (!li_next
		||  (((uintptr_t)li & -PAGE_SIZE) !=
				((uintptr_t)li_next & -PAGE_SIZE))) {
			cr_host_vmfree(li);
		}
	}
}

/**
 * XXX
 */

int cr_host_map_alloc_pt(struct cra_page_ent *pml4, uintptr_t va, enum cra_pe_bits extra_bits, int pages_nx, int level, int map_direct, struct cra_page_ent *pe, struct cra_page_ent **ppt_next)
{
	int err;
	void *pt;
	uintptr_t pt_next_pfn;

	if (!(pt = cr_host_vmalloc(1, PAGE_SIZE))) {
		return -ENOMEM;
	} else {
		(*ppt_next) = (struct cra_page_ent *)pt;
		pt_next_pfn = cr_host_virt_to_phys((uintptr_t)(*ppt_next));
		cr_amd64_init_page_ent(pe, pt_next_pfn,
			extra_bits, pages_nx, level, map_direct);
	}
	if ((err = crp_host_map_link_page(CRH_PTL_PAGE_TABLE,
			pt_next_pfn, (uintptr_t)pt)) < 0) {
		return err;
	} else {
		return 0;
	}
}

/**
 * cr_host_map_free() - release map memory back to OS
 *
 * Return: Nothing
 */

void cr_host_map_free(struct cra_page_ent *pml4, void (*vmfree)(void *))
{
	/* XXX */
}

/**
 * XXX
 */

int cr_host_map_link_ram_page(uintptr_t pfn, uintptr_t va)
{
	return crp_host_map_link_page(CRH_PTL_RAM_PAGE, pfn, va);
}

/**
 * XXX
 */

int cr_host_map_link_rsvd_page(uintptr_t pfn, uintptr_t va)
{
	return crp_host_map_link_page(CRH_PTL_RSVD_PAGE, pfn, va);
}

/**
 * XXX
 */

int cr_host_map_xlate_pfn(enum crh_ptl_type type, uintptr_t pfn, uintptr_t *pva)
{
	int level;
	uintptr_t idx;
	struct crh_pages_tree_node **node;
	struct crh_pages_tree_leaf **leaf;

	for (level = 4, node = NULL, leaf = NULL; level > 0; level--) {
		switch (level) {
		case 4: idx = (pfn >> (9 + 9 + 9)) & 0x1ff;
			node = &cr_host_state.host_pages_tree.u.node[idx];
			break;
		case 3: idx = (pfn >> (9 + 9)) & 0x1ff;
			node = &((*node)->u.node[idx]); break;
		case 2: idx = (pfn >> (9)) & 0x1ff;
			node = &((*node)->u.node[idx]); break;
		case 1: idx = (pfn) & 0x1ff;
			leaf = &((*node)->u.leaf[idx]); break;
		}
		if (level > 1) {
			if (!(*node)) {
				return -ESRCH;
			}
		} else {
			break;
		}
	}
	if (!(*leaf)) {
		return -ESRCH;
	} else
	switch ((*leaf)->type & type) {
	case CRH_PTL_PAGE_TABLE:
		return (*pva) = (*leaf)->va_pt, 0;
	case CRH_PTL_RAM_PAGE:
		return (*pva) = (*leaf)->va_ram, 0;
	case CRH_PTL_RSVD_PAGE:
		return (*pva) = (*leaf)->va_rsvd, 0;
	default:
		return -EINVAL;
	}
}

/**
 * XXX
 */

void cr_host_soft_assert_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

/**
 * XXX
 */

void *cr_host_malloc(struct crh_malloc_state *mstate, size_t nitems, size_t size)
{
	size_t count;
	void *p;

	if ((count = nitems * size) >= PAGE_SIZE) {
		return NULL;
	} else
	if (count > mstate->rem) {
		if (!(mstate->tail = (uintptr_t)
				cr_host_vmalloc(1, mstate->rem = PAGE_SIZE))) {
			return NULL;
		}
	}
	p = (void *)mstate->tail;
	if (count < mstate->rem) {
		mstate->rem -= count;
		mstate->tail += count;
	} else {
		mstate->rem = 0;
	}
	return p;
}

/**
 * XXX
 */

void cr_host_mfree(struct crh_malloc_state *mstate, void *p)
{
	/* XXX */
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
