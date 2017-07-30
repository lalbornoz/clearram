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

#ifndef _DEBUGDEF_H_
#define _DEBUGDEF_H_
#ifdef DEBUG
/*
 * Exception debugging subroutines
 */
int cr_debug_init(struct cmp_params *cmp_params, struct page_ent *pml4, uintptr_t va_idt, uintptr_t va_stack, uintptr_t va_vga);
void cr_debug_low(struct int_frame *frame, unsigned exc_code);
#endif /* DEBUG */
#endif /* !_DEBUGDEF_H_ */

/*
 * vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
 */
