/*
 * backer_main
 *
 * Linux device driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                           Kernel Interface
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
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/devfs_fs_kernel.h>
#include <linux/errno.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/mtio.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/uaccess.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_unit.h"
#include "backer_fmt.h"


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver");
MODULE_SUPPORTED_DEVICE("backer");

#define MODULE_NAME  "backer"

static unsigned int timeout = CONFIG_BACKER_TIMEOUT;
MODULE_PARM(timeout, "i");
MODULE_PARM_DESC(timeout, "Timeout (seconds)");

EXPORT_SYMBOL(bkr_device_register);
EXPORT_SYMBOL(bkr_device_unregister);
EXPORT_SYMBOL(bkr_unit_list);


/*
 * ========================================================================
 *                          Parameters and constants
 * ========================================================================
 */

#define  BKR_VERSION            "3.100"
#define  BKR_PROC_NAME          "driver/"MODULE_NAME
#define  MAX_POLL_FREQ          20      /* poll frequency in Hz */
#define  BKR_MAX_TIMEOUT        30      /* seconds */
#define  MODE_INITIALIZER       (BKR_NTSC | BKR_LOW | BKR_RAW)

#if (MAX_UPDATE_FREQ > HZ)
#error "MAX_UPDATE_FREQ is too high"
#endif


/*
 * ========================================================================
 *                          Function prototypes
 * ========================================================================
 */

static int     open(struct inode *, struct file *);
static int     release(struct inode *, struct file *);
static int     stop_release(struct inode *, struct file *);
static ssize_t start_read(struct file *, char *, size_t, loff_t *);
static ssize_t start_write(struct file *, const char *, size_t, loff_t *);
static ssize_t read(struct file *, char *, size_t, loff_t *);
static ssize_t write(struct file *, const char *, size_t, loff_t *);
static int     ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static loff_t  llseek(struct file *, loff_t, int);


/*
 * ========================================================================
 *                              Global Data
 * ========================================================================
 */

typedef struct
	{
	int mode;
	char name[sizeof("xxx\0")];
	} bkr_modes_t;

static bkr_modes_t  bkr_modes[] =
	{ { BKR_NTSC | BKR_HIGH | BKR_EP,  "nhe\0" },
	  { BKR_NTSC | BKR_HIGH | BKR_RAW, "nhr\0" },
	  { BKR_NTSC | BKR_HIGH | BKR_SP,  "nhs\0" },
	  { BKR_NTSC | BKR_LOW  | BKR_EP,  "nle\0" },
	  { BKR_NTSC | BKR_LOW  | BKR_RAW, "nlr\0" },
	  { BKR_NTSC | BKR_LOW  | BKR_SP,  "nls\0" },
	  { BKR_PAL  | BKR_HIGH | BKR_EP,  "phe\0" },
	  { BKR_PAL  | BKR_HIGH | BKR_RAW, "phr\0" },
	  { BKR_PAL  | BKR_HIGH | BKR_SP,  "phs\0" },
	  { BKR_PAL  | BKR_LOW  | BKR_EP,  "ple\0" },
	  { BKR_PAL  | BKR_LOW  | BKR_RAW, "plr\0" },
	  { BKR_PAL  | BKR_LOW  | BKR_SP,  "pls\0" } };

#define NUM_MODES  (sizeof(bkr_modes)/sizeof(bkr_modes_t))

typedef struct
	{
	bkr_unit_t  *unit;              /* this descriptor's unit */
	int  mode;                      /* this device file's mode */
	int  last_error;                /* Pending error code if != 0 */
	} bkr_descriptor_t;             /* per-file-descriptor info */

#define  DESCRIPTOR_INITIALIZER  ((bkr_descriptor_t) { NULL, 0, 0 })

LIST_HEAD(bkr_unit_list);                       /* list of installed units */
typedef unsigned long  jiffies_t;               /* type for jiffies */
static devfs_handle_t  bkr_dev_directory;       /* root of dev tree */

/*
 * File operations array.  Order must match bkr_state_t.
 */

#define STOPPED_OPS                                                  \
	{ THIS_MODULE, llseek, start_read, start_write, NULL, NULL,  \
	  ioctl, NULL, open, NULL,      release }

#define READING_OPS                                                  \
	{ THIS_MODULE, llseek,       read, start_write, NULL, NULL,  \
	  ioctl, NULL, open, NULL, stop_release }

#define WRITING_OPS                                                  \
	{ THIS_MODULE, llseek, start_read,       write, NULL, NULL,  \
	  ioctl, NULL, open, NULL, stop_release }

static struct file_operations file_ops[] =
	{
	STOPPED_OPS,
	READING_OPS,
	WRITING_OPS
	};


/*
 * ========================================================================
 *
 *                               /PROC I/O
 *
 * ========================================================================
 */

static int bkr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char  *pos = page;
	bkr_unit_t  *unit;
	spinlock_t  *lock;
	bkr_device_t  *device;
	bkr_sector_t  *sector;
	struct list_head  *curr;

	list_for_each(curr, &bkr_unit_list)
		{
		unit = list_entry(curr, bkr_unit_t, list);
		lock = &unit->lock;
		device = &unit->device;
		sector = &unit->sector;
		spin_lock(lock);

		pos += sprintf(pos, "Unit            : %u ", unit->number);
		switch(device->state)
			{
			case BKR_READING:
			device->ops->read(device, 0);
			pos += sprintf(pos, "READING");
			break;

			case BKR_WRITING:
			device->ops->write(device, 0);
			pos += sprintf(pos, "WRITING");
			break;

			case BKR_STOPPED:
			pos += sprintf(pos, "STOPPED");
			break;

			case BKR_SUSPENDED:
			pos += sprintf(pos, "SUSPENDED");
			break;
			}

		pos += sprintf(pos, "\n"
		                    "Current Mode    : %u\n"
		                    "Sector Number   : %u\n"
		                    "Byte Errors     : %u\n"
		                    "In Worst Block  : %u / %u\n"
		                    "Recently        : %u\n"
		                    "Bad Blocks      : %u\n"
		                    "Framing Errors  : %u\n"
		                    "Overrun Errors  : %u\n"
		                    "Underflows      : %u\n"
		                    "Worst Key       : %u\n"
		                    "Closest Non-Key : %u\n"
		                    "Least Skipped   : %u\n"
		                    "Most Skipped    : %u\n"
		                    "I/O Buffer      : %u / %u\n",
		                    device->mode,
		                    sector->header.number,
		                    sector->health.total_errors,
		                    sector->errors.symbol, sector->rs_format.parity,
		                    sector->errors.recent_symbol,
		                    sector->errors.block,
		                    sector->errors.frame,
		                    sector->errors.overrun,
		                    sector->errors.underflow,
		                    sector->health.worst_key,
		                    sector->health.best_nonkey,
		                    sector->health.least_skipped,
		                    sector->health.most_skipped,
			            bytes_in_buffer(device->head, device->tail, device->size), device->size);
		sector->errors.recent_symbol = 0;
		spin_unlock(lock);
		}

	*start = page + off;
	if(pos > *start)
		return(pos - *start);
	*eof = 1;
	return(0);
}


/*
 * ========================================================================
 *
 *                           POWER MANAGEMENT
 *
 * ========================================================================
 */

static int  bkr_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	bkr_unit_t  *unit = (bkr_unit_t *) data;
	int  result = 0;

	spin_lock(&unit->lock);
	switch(rqst)
		{
		case PM_SUSPEND:
		if(unit->device.state != BKR_STOPPED)
			result = -EBUSY;
		else
			unit->device.state = BKR_SUSPENDED;
		break;

		case PM_RESUME:
		unit->device.state = BKR_STOPPED;
		break;

		default:
		break;
		}
	spin_unlock(&unit->lock);

	return(result);
}


/*
 * ========================================================================
 *
 *                            ENTRY/EXIT CODE
 *
 * ========================================================================
 */

int __init backer_init(void)
{
	int  result;

	printk(KERN_INFO MODULE_NAME ": Backer 16/32 tape device driver version " BKR_VERSION "\n");

	/*
	 * Initialize some data.
	 */

	if(timeout > BKR_MAX_TIMEOUT)
		timeout = BKR_MAX_TIMEOUT;
	timeout *= HZ;

	/*
	 * Grab driver-wide resources.
	 */

	result = devfs_register_chrdev(BKR_MAJOR, MODULE_NAME, &file_ops[BKR_STOPPED]);
	if(result < 0)
		{
		printk(KERN_WARNING MODULE_NAME ": can't register device\n");
		goto cant_register_chrdev;
		}
	bkr_dev_directory = devfs_mk_dir(NULL, MODULE_NAME, NULL);
	if(bkr_dev_directory == NULL)
		{
		printk(KERN_WARNING MODULE_NAME ": can't register device\n");
		result = -EBUSY;
		goto cant_register_directory;
		}
	create_proc_read_entry(BKR_PROC_NAME, 0, 0, bkr_read_proc, NULL);

	/*
	 * Driver installed.
	 */

	request_module("backer_lowlevel");
	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_register_directory:
		devfs_unregister_chrdev(BKR_MAJOR, MODULE_NAME);
	cant_register_chrdev:

	return(result);
}

void __exit backer_exit(void)
{
	pm_unregister_all(bkr_pm_callback);
	remove_proc_entry(BKR_PROC_NAME, NULL);
	devfs_unregister(bkr_dev_directory);
	devfs_unregister_chrdev(BKR_MAJOR, MODULE_NAME);
}

module_init(backer_init);
module_exit(backer_exit);


/*
 * ========================================================================
 *
 *                     DEVICE REGISTER / UNREGISTER
 *
 * ========================================================================
 */

/*
 * These functions are called by the device I/O modules to register and
 * unregister the devices they find.
 */

bkr_unit_t *bkr_device_register(bkr_device_type_t type, bkr_device_ops_t *ops)
{
	struct list_head  *curr;
	int  i, minor;
	bkr_unit_t  *new;

	/*
	 * Make new unit descriptor.
	 */

	new = (bkr_unit_t *) kmalloc(sizeof(bkr_unit_t), GFP_KERNEL);
	if(new == NULL)
		{
		printk(KERN_WARNING MODULE_NAME ": can't allocate unit descriptor\n");
		goto no_mem;
		}

	/*
	 * Find lowest available number.
	 */

	new->number = 0;
	list_for_each(curr, &bkr_unit_list)
		{
		if(list_entry(curr, bkr_unit_t, list)->number != new->number)
			break;
		new->number++;
		}
	list_add_tail(&new->list, curr);
	sprintf(new->name, "%u", new->number);

	/*
	 * Create unit's device files.
	 */

	new->devfs_handle = devfs_mk_dir(bkr_dev_directory, new->name, NULL);
	if(new->devfs_handle == NULL)
		{
		printk(KERN_WARNING MODULE_NAME ": %s: can't create devfs entries\n", new->name);
		goto no_dev_directory;
		}
	for(i = 0; i < NUM_MODES; i++)
		{
		minor = new->number*NUM_MODES + i;
		if((minor > 255) || (bkr_modes[i].mode < 0))
			continue;
		devfs_register(new->devfs_handle, bkr_modes[i].name, DEVFS_FL_DEFAULT, BKR_MAJOR, minor, S_IFCHR | S_IRUGO | S_IWUGO, &file_ops[BKR_STOPPED], NULL);
		}

	/*
	 * Initialize unit.
	 */

	spin_lock_init(&new->lock);
	new->device.state = BKR_STOPPED;
	new->device.type = type;
	new->device.ops = ops;
	new->device.private = NULL;
	new->sector.buffer = NULL;
	bkr_format_reset(&new->device, &new->sector, MODE_INITIALIZER, BKR_STOPPED);
	switch(type)
		{
		case BKR_ISA_DEVICE:
		new->pm_handle = pm_register(PM_ISA_DEV, PM_SYS_UNKNOWN, bkr_pm_callback);
		break;

		case BKR_PARPORT_DEVICE:
		new->pm_handle = pm_register(PM_UNKNOWN_DEV, PM_SYS_UNKNOWN, bkr_pm_callback);
		break;
		}
	if(new->pm_handle != NULL)
		new->pm_handle->data = new;

	/*
	 * Success.
	 */

	return(new);

	/*
	 * There was a problem.
	 */

	no_dev_directory:
		list_del(&new->list);
		kfree(new);
	no_mem:
		return(NULL);
}


void bkr_device_unregister(bkr_unit_t *unit)
{
	if(unit != NULL)
		{
		list_del(&unit->list);
		devfs_unregister(unit->devfs_handle);
		pm_unregister(unit->pm_handle);
		kfree(unit->device.private);
		kfree(unit->sector.buffer);
		kfree(unit);
		}
}


/*
 * ========================================================================
 *
 *                            FILE OPERATIONS
 *
 * ========================================================================
 *
 * Access to the data stream is only granted once on a first-come /
 * first-serve basis across all descriptors.  A data transfer is started
 * when the first call to a read() or write() method is made after which
 * all read/write attempts from other descriptors fail.
 */

/*
 * open() --- Arbitrate device open requests.
 */

static int open(struct inode *inode, struct file *filp)
{
	bkr_descriptor_t  *private;
	struct list_head  *curr;
	int  number, mode;

	/*
	printk(KERN_INFO "Backer: %s\n", filp->f_dentry->d_iname);
	*/

	number = MINOR(inode->i_rdev) / NUM_MODES;
	mode = bkr_modes[MINOR(inode->i_rdev) % NUM_MODES].mode;
	if(mode < 0)
		return(-ENODEV);

	private = (bkr_descriptor_t *) kmalloc(sizeof(bkr_descriptor_t), GFP_KERNEL);
	if(private == NULL)
		return(-ENOMEM);
	filp->private_data = private;

	*private = DESCRIPTOR_INITIALIZER;
	private->mode = mode;
	list_for_each(curr, &bkr_unit_list)
		{
		private->unit = list_entry(curr, bkr_unit_t, list);
		if(private->unit->number == number)
			{
			private->unit->device.ops->inc_use_count();
			return(0);
			}
		}

	kfree(private);
	return(-ENODEV);
}


/*
 * stop_release(), release()
 *
 * Handle device close requests.  stop_release, additionally, terminates
 * the current transfer.
 */

static int stop_release(struct inode *inode, struct file *filp)
{
	jiffies_t  bailout;
	bkr_unit_t  *unit = ((bkr_descriptor_t *) filp->private_data)->unit;
	spinlock_t  *lock = &unit->lock;
	bkr_device_t  *device = &unit->device;
	bkr_sector_t  *sector = &unit->sector;

	spin_lock(lock);

	if(device->state == BKR_WRITING)
		{
		bailout = jiffies + timeout;
		if(BKR_FORMAT(device->mode) != BKR_RAW)
			{
			do
				{
				if(bkr_sector_write_eor(device, sector) != -EAGAIN)
					break;
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ/MAX_POLL_FREQ);
				}
			while(time_before(jiffies, bailout));
			}
		do
			{
			if(device->ops->flush(device) != -EAGAIN)
				break;
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/MAX_POLL_FREQ);
			}
		while(time_before(jiffies, bailout));
		}

	device->ops->stop(device);
	pm_dev_idle(unit->pm_handle);

	spin_unlock(lock);

	return(release(inode, filp));
}

static int release(struct inode *inode, struct file *filp)
{
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;

	private->unit->device.ops->dec_use_count();

	kfree(private);

	return(0);
}


/*
 * ioctl()
 *
 * The general intent is for this driver to provide a standard magnetic
 * tape interface.  To this end we try to implement as many of the standard
 * mtio.h ioctl's as make sense for this device.
 */

static int ioctl(struct inode *inode, struct file *filp, unsigned int op, unsigned long argument)
{
	int  result;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_sector_t  *sector = &private->unit->sector;
	union
		{
		struct mtop  mtop;
		struct mtget  mtget;
		struct mtpos  mtpos;
		} arg;

	switch(op)
		{
		case MTIOCTOP:
		copy_from_user(&arg.mtop, (void *) argument, sizeof(struct mtop));
		switch(arg.mtop.mt_op)
			{
			case MTNOP:
			case MTRESET:
			return(0);

			case MTSETDRVBUFFER:
			result = arg.mtop.mt_count & ~MT_ST_OPTIONS;
			if( ((arg.mtop.mt_count & MT_ST_OPTIONS) == MT_ST_TIMEOUTS) &&
			    (result <= BKR_MAX_TIMEOUT) )
				{
				timeout = result*HZ;
				return(0);
				}
			return(-EINVAL);

			default:
			return(-EINVAL);
			}

		case MTIOCGET:
		arg.mtget.mt_type = MT_ISUNKNOWN;
		arg.mtget.mt_resid = sector->end - sector->offset;
		arg.mtget.mt_dsreg = private->mode;
		arg.mtget.mt_gstat = GMT_ONLINE(-1L);
		arg.mtget.mt_erreg = sector->health.total_errors;
		arg.mtget.mt_fileno = 0;
		arg.mtget.mt_blkno = sector->header.number;
		copy_to_user((void *) argument, &arg.mtget, sizeof(struct mtget));
		return(0);

		case MTIOCPOS:
		arg.mtpos.mt_blkno = sector->header.number;
		copy_to_user((void *) argument, &arg.mtpos, sizeof(struct mtpos));
		return(0);

		default:
		return(-EINVAL);
		}
}


/*
 * start_common(), start_read(), start_write()
 *
 * These functions start data transfers when the first call to a read() or
 * write() method is made.
 */

static ssize_t start_common(struct file *filp, bkr_state_t direction)
{
	ssize_t  result;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_unit_t  *unit = private->unit;
	bkr_device_t  *device = &unit->device;
	bkr_sector_t  *sector = &unit->sector;

	spin_lock(&unit->lock);

	if(device->state != BKR_STOPPED)
		result = -EBUSY;
	else
		{
		do
			{
			pm_access(unit->pm_handle);
			if((result = bkr_format_reset(device, sector, private->mode, direction)) >= 0)
				if((result = device->ops->start(device, direction)) >= 0)
					{
					filp->f_op = &file_ops[direction];
					break;
					}
			pm_dev_idle(unit->pm_handle);
			}
		while(0);
		}

	spin_unlock(&unit->lock);
	return(result);
}

static ssize_t start_read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(filp, BKR_READING);
	if(result >= 0)
		result = filp->f_op->read(filp, buff, count, posp);

	return(result);
}

static ssize_t start_write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(filp, BKR_WRITING);
	if(result >= 0)
		result = filp->f_op->write(filp, buff, count, posp);

	return(result);
}


/*
 * read(), write()
 *
 * These functions allow re-entrance but only because they sleep at a safe
 * spot.  They are not, generally, re-entrance safe so on SMP machines we
 * make sure to grab the unit lock.
 */

static ssize_t read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result = 0;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	jiffies_t  bailout;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	spinlock_t  *lock = &private->unit->lock;
	bkr_device_t  *device = &private->unit->device;
	bkr_sector_t  *sector = &private->unit->sector;

	spin_lock(lock);

	if(private->last_error != 0)
		{
		result = private->last_error;
		private->last_error = 0;
		}
	else
		{
		bailout = jiffies + timeout;
		while(1)
			{
			chunk_size = sector->end - sector->offset;
			if(chunk_size > count)
				chunk_size = count;

			copy_to_user(buff, sector->offset, chunk_size);

			sector->offset += chunk_size;
			buff += chunk_size;
			moved += chunk_size;
			count -= chunk_size;

			if(count == 0)
				{
				/* done */
				result = moved;
				break;
				}

			result = sector->read(device, sector);
			if(result == 0)
				/* got a sector */
				continue;
			if(result > 0)
				{
				/* EOF */
				result = moved;
				break;
				}

			/* can't read data */
			if(time_after_eq(jiffies, bailout))
				result = -ETIMEDOUT;
			if(result != -EAGAIN)
				{
				/* error */
				if(moved)
					{
					/* save for later */
					private->last_error = result;
					result = moved;
					}
				break;
				}

			/* operation needs to block */
			if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY)))
				{
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ/MAX_POLL_FREQ);

				if(!signal_pending(current))
					continue;
				result = -EINTR;
				}
			/* non-blocking I/O */
			if(moved || !(filp->f_flags & O_NONBLOCK))
				result = moved;
			break;
			}
		}

	spin_unlock(lock);
	return(result);
}

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result = 0;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	jiffies_t  bailout;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	spinlock_t  *lock = &private->unit->lock;
	bkr_device_t  *device = &private->unit->device;
	bkr_sector_t  *sector = &private->unit->sector;

	spin_lock(lock);

	if(private->last_error != 0)
		{
		result = private->last_error;
		private->last_error = 0;
		}
	else
		{
		bailout = jiffies + timeout;
		while(1)
			{
			chunk_size = sector->end - sector->offset;
			if(chunk_size > count)
				chunk_size = count;

			copy_from_user(sector->offset, buff, chunk_size);

			sector->offset += chunk_size;
			buff += chunk_size;
			moved += chunk_size;
			count -= chunk_size;

			if(count == 0)
				{
				/* done */
				result = moved;
				break;
				}

			result = sector->write(device, sector);
			if(result >= 0)
				continue;

			/* can't write data */
			if(time_after_eq(jiffies, bailout))
				result = -ETIMEDOUT;
			if(result != -EAGAIN)
				{
				/* error */
				if(moved)
					{
					/* save for later */
					private->last_error = result;
					result = moved;
					}
				break;
				}

			/* operation needs to block */
			if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY)))
				{
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ/MAX_POLL_FREQ);

				if(!signal_pending(current))
					continue;
				result = -EINTR;
				}
			/* non-blocking I/O */
			if(moved || !(filp->f_flags & O_NONBLOCK))
				result = moved;
			break;
			}
		}

	spin_unlock(lock);
	return(result);
}


/*
 * llseek()
 *
 * Since Backer provides an unseekable data stream, we must explicitly
 * return an error code otherwise the kernel's default handler will behave
 * as though seeking is possible.
 */

static loff_t llseek(struct file *filp, loff_t offset, int whence)
{
	return(-ESPIPE);
}
