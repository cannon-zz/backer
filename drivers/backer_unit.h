/*
 * backer_unit.h
 * 
 * Unit description for Backer device driver.
 *
 * Copyright (C) 2000,2001  Kipp C. Cannon
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef  _BACKER_UNIT_H
#define  _BACKER_UNIT_H

#include <asm/semaphore.h>

#include <linux/devfs_fs_kernel.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/wait.h>

#include "backer_device.h"
#include "backer_fmt.h"

#define  BKR_NAME_LENGTH  3             /* including trailing \0 */

typedef struct
	{
	struct ctl_table_header  *header;
	ctl_table  dev_dir[2];
	ctl_table  driver_dir[2];
	ctl_table  unit_dir[2];
	ctl_table  entries[3];
	}  bkr_sysctl_table_t;

typedef struct
	{
	struct list_head  list;         /* next/prev in list */
	char  name[BKR_NAME_LENGTH];    /* name */
	unsigned int  number;           /* unit number */
	struct semaphore  state_lock;   /* device state locking semaphore */
	struct semaphore  io_lock;      /* I/O locking semaphore */
	bkr_device_t  device;           /* device state descriptor */
	bkr_stream_t  stream;           /* format state descriptor */
	wait_queue_head_t  io_queue;    /* I/O wait queue */
	struct pm_dev  *pm_handle;      /* power management handle */
	devfs_handle_t  devfs_handle;   /* devfs directory handle */
	bkr_sysctl_table_t  sysctl;     /* sysctl interface table */
	} bkr_unit_t;                   /* unit information */

extern struct list_head  bkr_unit_list;

extern bkr_unit_t  *bkr_unit_register(pm_dev_t, bkr_device_ops_t *, int);
extern void  bkr_unit_unregister(bkr_unit_t *);

#endif /* _BACKER_UNIT_H */
