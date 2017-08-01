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

int cr_host_list_append(struct crh_list *list, void **pitem)
{
	size_t isize_aligned;
	uintptr_t p;
	struct crh_litem *li, *li_last;

	CRH_VALID_PTR(list);
	CRH_VALID_PTR(list->item_size);
	isize_aligned = sizeof(*li) + list->item_size;
	if (isize_aligned % 8) {
		isize_aligned &= 0xf8;
	}
	if (!list->head
	||  (isize_aligned > (PAGE_SIZE - list->page_off))) {
		if (!(p = cr_host_vmalloc(1, PAGE_SIZE))) {
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
static struct crh_litem *crp_host_list_msort_merge(struct crh_litem *left, struct crh_litem *right, int (*cmp_func)(struct crh_litem *, struct crh_litem *)) {
	struct crh_litem *next, *head, *tail;

	head = NULL;
	while (left || right) {
		if (!right) {
			next = left;
			left = left->next;
		} else
		if (!left) {
			next = right;
			right = right->next;
		} else
		if (cmp_func(left, right) <= 0) {
			next = left;
			left = left->next;
		} else {
			next = right;
			right = right->next;
		}
		if (!head) {
			head = next;
		} else {
			tail->next = next;
		}
		tail = next;
	}
	return head;
}

static int cr_host_list_msort_split(struct crh_litem **list, struct crh_litem **left, struct crh_litem **right) {
	struct crh_litem *last, *next;

	*left = *right = last = next = *list;
	if ((*left) && (*left)->next) {
		while (next && next->next) {
			/* Advance right once and next twice. */
			last = *right;
			(*right) = (*right)->next;
			next = next->next->next;
		}
		/* Split list. */
		return last->next = NULL, 1;
	} else {
		return 0;
	}
}

/**
 * XXX
 */

void cr_host_list_msort(struct crh_litem **list, int (*cmp_func)(struct crh_litem *, struct crh_litem *))
{
	struct crh_litem *left, *right;

	if (cr_host_list_msort_split(list, &left, &right)) {
		cr_host_list_msort(&left, cmp_func);
		cr_host_list_msort(&right, cmp_func);
		*list = crp_host_list_msort_merge(left, right, cmp_func);
	}
}

/**
 * XXX
 */

struct cra_page_ent *cr_host_map_alloc_pt(int level, uintptr_t va)
{
	struct cra_page_ent *p;
	struct cra_page_tbl_desc *item;
	
	if (!(p = (struct cra_page_ent *)cr_host_vmalloc(1, PAGE_SIZE))) {
		return NULL;
	} else
	if (cr_host_list_append(&cr_host_state.host_lpage_tbl_desc,
				(void **)&item) < 0) {
		return NULL;
	} else {
		CRA_INIT_PAGE_TBL_DESC(item, (uintptr_t)p, 0);
		return p;
	}
}

/**
 * XXX
 */

int cr_host_map_xlate_pfn(uintptr_t pfn, uintptr_t *pva)
{
	struct crh_litem *litem;
	struct cra_page_tbl_desc *item;
	for (litem = cr_host_state.host_lpage_tbl_desc.head; litem; litem = litem->next) {
		item = (struct cra_page_tbl_desc *)&litem->item;
		if (item->pfn == pfn) {
			return *pva = item->va_host;
		}
	}
	return -ENOENT;
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

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
