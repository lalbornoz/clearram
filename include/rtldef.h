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

#ifndef _RTLDEF_H_
#define _RTLDEF_H_

/*
 * Subroutine data structures and bits
 */

/**
 * cr_cpuid_max_page_size() CPUID bits
 */
enum cpuid_bits {
	CPUID_EAX_FUNC_FEATURES	= 0x00000001,
	CPUID_EDX_BIT_PSE	= 0x00000008,
	CPUID_EDX_BIT_PDPE1G	= 0x04000000,
};

/**
 * cr_pmem_walk_*() parameters
 */
struct cpw_params {
	int		restart;
#if defined(__linux__)
	struct resource	*res_cur;
#elif defined(__FreeBSD__)
	int		nid;
#endif /* defined(__linux__) || defined(__FreeBSD__) */
	uintptr_t *	filter;
	uintptr_t	filter_ncur, filter_nmax;
	uintptr_t	filter_last_base, filter_last_limit;
};
#if defined(__linux__)
#define INIT_CPW_PARAMS_OSDEP(p) do {		\
		(p)->restart = 1;		\
		(p)->res_cur = NULL;		\
	} while(0)
#elif defined(__FreeBSD__)
#define INIT_CPW_PARAMS_OSDEP(p) do {		\
		(p)->nid = 0;			\
	} while(0)
#endif /* defined(__linux__) || defined(__FreeBSD__) */
#define INIT_CPW_PARAMS(p) do {			\
		INIT_CPW_PARAMS_OSDEP(p);	\
		(p)->filter_ncur = 0;		\
		(p)->filter_nmax = 0;		\
		(p)->filter_last_base = 0;	\
		(p)->filter_last_limit = 0;	\
	} while(0)

/*
 * Helper preprocessor macros and subroutines
 */

/**
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
	({ unsigned long long _t0 = (ll)+(d)-1; cr_do_div(_t0, (d)); _t0; })
#define CR_DIV_ROUND_DOWN_ULL(ll,d) \
	({ unsigned long long _t0 = (ll)%(d); (ll) - _t0; })

size_t cr_cpuid_page_size_from_level(int level);
int cr_init_pfns_compare(const void *lhs, const void *rhs);
void cr_map_init_page_ent(struct page_ent *pe, uintptr_t pfn_base, enum pe_bits extra_bits, int pages_nx, int level, int map_direct);
int cr_map_pages_from_va(struct cmp_params *params, uintptr_t va_src, uintptr_t va_dst, size_t npages, enum pe_bits extra_bits, int pages_nx);
int cr_pmem_walk_filter(struct cpw_params *params, uintptr_t *ppfn_base, uintptr_t *ppfn_limit);
#endif /* !_RTLDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
