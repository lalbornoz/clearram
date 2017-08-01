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
 * OS-specific LKM fields & machine state
 */
#if defined(__linux__)
void clearram_exit(void);
module_init(cr_host_lkm_init);
module_exit(clearram_exit);
MODULE_AUTHOR("Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>");
MODULE_DESCRIPTION("clearram");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("clearram");

struct cr_host_state cr_host_state = {
	.host_cdev_fops = {.write = cr_host_cdev_write}
};

void clearram_exit(void) {
	cr_host_lkm_exit();
}
#elif defined(__FreeBSD__)
int cr_host_evhand(struct module *m, int what, void *arg __unused);
moduledata_t clearram_mod = { "clearram", cr_host_evhand, NULL, };
DECLARE_MODULE(clearram, clearram_mod, SI_SUB_KLD, SI_ORDER_ANY);
MALLOC_DEFINE(M_CLEARRAM, "clearram", "buffer for clearram module");

struct cr_host_state cr_host_state = {
	.host_cdev_fops = {
		.d_version = D_VERSION,
		.d_write = cr_host_cdev_write,
		.d_name = "clearram"
	}
};

/**
 * cr_host_evhand() - handle kernel module event
 * @m:		pointer to this module
 * @what:	MOD_LOAD when loading module, MOD_UNLOAD when unloading module
 * @arg:	(unused)
 *
 * Return: 0 on success, >0 otherwise
 */

int cr_host_evhand(struct module *m, int what, void *arg __unused)
{
	switch (what) {
	case MOD_LOAD:
		return cr_host_lkm_init() * -1;
	case MOD_UNLOAD:
		return cr_host_lkm_exit(), 0;
	default:
		return EOPNOTSUPP;
	}
}
#endif /* defined(__FreeBSD__) */

/**
 * cr_host_lkm_init() - kernel module entry point
 *
 * Return: 0 on success, <0 on failure
 */

int cr_host_lkm_init(void)
{
	int err, level;
	uintptr_t pfn_block_base, pfn_block_limit, va_vga, va, va_lrsvd_pages;
	size_t npage;
	struct cra_page_tbl_desc *item;
	struct crh_litem *litem, *litem_next;

	/*
	 * Initialise image {base address,page count} range
	 * Initialise PML4 self-mapping at 0xfffff80000000000
	 * Initialise list of reserved pages
	 * Walk and map physical RAM at cr_host_state.clear_va_top, in sizes and order of 1G, 2M, and 4K
	 * Clone image pages at 4K page granularity
	 * Append image pages to list of reserved pages
	 * Map VGA framebuffer pages into image at cr_host_state.clear_vga
	 * Map and translate list of reserved pages at 0xfffff78000000000
	 * Initialise GDT and IDT
	 * Initialise character device node
	 */
#if defined(__linux__)
	cr_host_state.clear_image_base = (uintptr_t)THIS_MODULE->core_layout.base;
	cr_host_state.clear_image_npages = THIS_MODULE->core_layout.size / PAGE_SIZE;
	if (THIS_MODULE->core_layout.size % PAGE_SIZE) {
		cr_host_state.clear_image_npages++;
	}
#elif defined(__FreeBSD__)
#error XXX
#endif /* defined(__linux__) || defined(__FreeBSD__) */
	cr_amd64_init_page_ent(&cr_host_state.clear_pml4[0x1f0],
		cr_host_virt_to_phys((uintptr_t)cr_host_state.clear_pml4),
		CRA_PE_READ_WRITE | CRA_PE_WRITE_THROUGH, CRA_NX_ENABLE, CRA_LVL_PML4, 0);
	CRH_LIST_INIT(&cr_host_state.host_lpage_tbl_desc,
		sizeof(struct cra_page_tbl_desc));
	cr_host_state.clear_va_top = 0;
	for (err = 0, level = CRA_LVL_PDP; level >= CRA_LVL_PT; level--) {
		CRH_INIT_PMAP_WALK_PARAMS(&cr_host_state.host_pmap_walk_params);
		while ((err == 0) && (err = cr_host_pmap_walk(
					&cr_host_state.host_pmap_walk_params,
					&pfn_block_base, &pfn_block_limit, NULL)) > 0) {
			err = cr_amd64_map_pages_unaligned(cr_host_state.clear_pml4,
				&cr_host_state.clear_va_top,
				pfn_block_base, pfn_block_limit,
				CRA_PE_READ_WRITE | CRA_PE_CACHE_DISABLE,
				CRA_NX_ENABLE, level,
				cr_host_map_alloc_pt, cr_host_map_xlate_pfn);
		}
		if (err < 0) {
			goto fail;
		}
	}
	if ((err = cr_amd64_map_pages_clone4K(cr_host_state.clear_pml4,
			cr_host_state.clear_image_base, NULL,
			CRA_PE_READ_WRITE | CRA_PE_WRITE_THROUGH, CRA_NX_DISABLE,
			cr_host_state.clear_image_npages,
			cr_host_map_alloc_pt, cr_host_map_xlate_pfn)) < 0) {
		goto fail;
	} else
	for (npage = 0; npage < cr_host_state.clear_image_npages; npage++) {
		va = CRA_VA_INCR(cr_host_state.clear_image_base, (npage * PAGE_SIZE));
		if ((err = cr_host_list_append(
				&cr_host_state.host_lpage_tbl_desc, (void **)&item)) < 0) {
			goto fail;
		} else {
			CRA_INIT_PAGE_TBL_DESC(item, va, 0);
		}
	}
	va_vga = (uintptr_t)cr_host_state.clear_vga;
	if ((err = cr_amd64_map_pages_unaligned(cr_host_state.clear_pml4, &va_vga,
			CRHS_VGA_PFN_BASE, CRHS_VGA_PFN_BASE + CRHS_VGA_PAGES,
			CRA_PE_READ_WRITE | CRA_PE_CACHE_DISABLE, CRA_NX_ENABLE, CRA_PS_4K,
			cr_host_map_alloc_pt, cr_host_map_xlate_pfn)) < 0) {
		goto fail;
	}
	for (litem = cr_host_state.host_lpage_tbl_desc.head; litem; litem = litem_next) {
		litem_next = litem->next;
		item = (struct cra_page_tbl_desc *)&litem->item;
		if (!litem_next
		||  ((uintptr_t)litem & -PAGE_SIZE) !=
				((uintptr_t)litem_next & -PAGE_SIZE)) {
			va_lrsvd_pages = (uintptr_t)litem & -PAGE_SIZE;
			pfn_block_base = cr_host_virt_to_phys(va_lrsvd_pages);
			pfn_block_limit = pfn_block_limit + 1;
			if ((err = cr_amd64_map_pages_unaligned(cr_host_state.clear_pml4,
					&va_lrsvd_pages, pfn_block_base, pfn_block_limit,
					CRA_PE_READ_WRITE | CRA_PE_WRITE_THROUGH, CRA_NX_ENABLE,
					CRA_PS_4K, cr_host_map_alloc_pt, cr_host_map_xlate_pfn)) < 0) {
				goto fail;
			}
		}
		if ((err = cr_amd64_map_translate(cr_host_state.clear_pml4,
				(uintptr_t)item->pfn, &item->va_hi, CRA_LVL_PML4,
				cr_host_map_xlate_pfn)) < 0) {
			goto fail;
		}
		litem_next = litem->next;
	}
	cr_host_list_msort(&cr_host_state.host_lpage_tbl_desc.head, cr_amd64_map_cmp_desc);
	cr_host_state.clear_va_lrsvd = (uintptr_t)cr_host_state.host_lpage_tbl_desc.head;
	if ((err = cr_amd64_init_gdt(&cr_host_state)) < 0) {
		goto fail;
	} else
	if ((err = cr_amd64_init_idt(&cr_host_state)) < 0) {
		goto fail;
	}
	if ((err = cr_host_cdev_init(&cr_host_state)) < 0) {
		goto fail;
	}
out:	if (err < 0) {
		CRH_PRINTK_ERR("finished, err=%d", err);
	} else {
		CRH_PRINTK_DEBUG("finished, err=%d", err);
	}
	return err;
fail:	cr_amd64_map_free(cr_host_state.clear_pml4, cr_host_vmfree);
	goto out;
}

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
