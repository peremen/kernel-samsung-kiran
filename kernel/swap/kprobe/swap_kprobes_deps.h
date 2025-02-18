/**
 * @file kprobe/swap_kprobes_deps.h
 * @author Alexey Gerenkov <a.gerenkov@samsung.com> User-Space Probes initial implementation;
 * Support x86/ARM/MIPS for both user and kernel spaces.
 * @author Ekaterina Gorelkina <e.gorelkina@samsung.com>: redesign module for separating core and arch parts
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * @section COPYRIGHT
 *
 * Copyright (C) Samsung Electronics, 2006-2010
 *
 * @section DESCRIPTION
 *
 * SWAP kprobe kernel-dependent dependencies.
 */

#ifndef _SWAP_KPROBES_DEPS_H
#define _SWAP_KPROBES_DEPS_H

#include <linux/version.h>	// LINUX_VERSION_CODE, KERNEL_VERSION()
#include <linux/hugetlb.h>
#include <linux/mempolicy.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <ksyms/ksyms.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
#define DECLARE_NODE_PTR_FOR_HLIST(var_name)
#define swap_hlist_for_each_entry_rcu(tpos, pos, head, member) hlist_for_each_entry_rcu(tpos, head, member)
#define swap_hlist_for_each_entry_safe(tpos, pos, n, head, member) hlist_for_each_entry_safe(tpos, n, head, member)
#define swap_hlist_for_each_entry(tpos, pos, head, member) hlist_for_each_entry(tpos, head, member)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */
#define DECLARE_NODE_PTR_FOR_HLIST(var_name) struct hlist_node *var_name
#define swap_hlist_for_each_entry_rcu(tpos, pos, head, member) hlist_for_each_entry_rcu(tpos, pos, head, member)
#define swap_hlist_for_each_entry_safe(tpos, pos, n, head, member) hlist_for_each_entry_safe(tpos, pos, n, head, member)
#define swap_hlist_for_each_entry(tpos, pos, head, member) hlist_for_each_entry(tpos, pos, head, member)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 12))
#define synchronize_sched	synchronize_kernel
#endif

//--------------------- Declaration of module dependencies ------------------------//

#define DECLARE_MOD_FUNC_DEP(name, ret, ...) ret(*__ref_##name)(__VA_ARGS__)
#define DECLARE_MOD_CB_DEP(name, ret, ...) ret(*name)(__VA_ARGS__)


//----------------- Implementation of module dependencies wrappers -----------------//

#define DECLARE_MOD_DEP_WRAPPER(name, ret, ...) ret name(__VA_ARGS__)
#define IMP_MOD_DEP_WRAPPER(name, ...) \
{ \
	return __ref_##name(__VA_ARGS__); \
}


//---------------------- Module dependencies initialization --------------------//

#define INIT_MOD_DEP_VAR(dep, name) \
{ \
	__ref_##dep = (void *) swap_ksyms (#name); \
	if (!__ref_##dep) \
	{ \
		DBPRINTF (#name " is not found! Oops. Where is it?"); \
		return -ESRCH; \
	} \
}

#define INIT_MOD_DEP_CB(dep, name) \
{ \
	dep = (void *) swap_ksyms (#name); \
	if (!dep) \
	{ \
		DBPRINTF (#name " is not found! Oops. Where is it?"); \
		return -ESRCH; \
	} \
}


int init_module_dependencies(void);

int access_process_vm_atomic(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);

#define read_proc_vm_atomic(tsk, addr, buf, len)	access_process_vm_atomic (tsk, addr, buf, len, 0)
#define write_proc_vm_atomic(tsk, addr, buf, len)	access_process_vm_atomic (tsk, addr, buf, len, 1)
int page_present (struct mm_struct *mm, unsigned long addr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
unsigned long swap_do_mmap_pgoff(struct file *file, unsigned long addr,
				 unsigned long len, unsigned long prot,
				 unsigned long flags, unsigned long pgoff,
				 unsigned long *populate);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
unsigned long swap_do_mmap_pgoff(struct file *file, unsigned long addr,
				 unsigned long len, unsigned long prot,
				 unsigned long flags, unsigned long pgoff);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0) */


#endif /* _SWAP_KPROBES_DEPS_H */
