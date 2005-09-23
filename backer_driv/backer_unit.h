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

#include <linux/devfs_fs_kernel.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/spinlock.h>

#include "backer_device.h"
#include "backer_fmt.h"

typedef struct
	{
	struct list_head  list;         /* next/prev in list */
	char  name[9];                  /* name ("backerXX") */
	unsigned int  number;           /* unit number */
	spinlock_t  lock;               /* unit lock */
	bkr_device_t  device;           /* device state descriptor */
	bkr_sector_t  sector;           /* format state descriptor */
	struct pm_dev  *pm_handle;      /* power management handle */
	devfs_handle_t  devfs_handle;   /* devfs directory handle */
	} bkr_unit_t;                   /* unit information */

extern struct list_head  bkr_unit_list;

extern bkr_unit_t  *bkr_device_register(bkr_device_type_t, bkr_device_ops_t *);
extern void  bkr_device_unregister(bkr_unit_t *);

#endif /* _BACKER_UNIT_H */
