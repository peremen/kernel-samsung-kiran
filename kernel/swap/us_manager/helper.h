/**
 * @file us_manager/helper.h
 * @author Vyacheslav Cherkashin <v.cherkashin@samsung.com>
 *
 * @section LICENSE
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
 * Copyright (C) Samsung Electronics, 2013
 */

#ifndef _HELPER_H
#define _HELPER_H

#include <linux/sched.h>

static inline int is_kthread(struct task_struct *task)
{
	return !task->mm;
}

int init_helper(void);
void uninit_helper(void);

int register_helper(void);
void unregister_helper_top(void);
void unregister_helper_bottom(void);

#endif /* _HELPER_H */
