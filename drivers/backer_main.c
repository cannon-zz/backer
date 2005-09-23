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

#include <linux/config.h>
#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/init.h>

#include <linux/devfs_fs_kernel.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/mtio.h>
#include <linux/pm.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/wait.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_unit.h"
#include "backer_fmt.h"

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver");
MODULE_SUPPORTED_DEVICE("backer");
MODULE_LICENSE("GPL");

#define  MODULE_NAME  "backer"

EXPORT_SYMBOL(bkr_unit_register);
EXPORT_SYMBOL(bkr_unit_unregister);
EXPORT_SYMBOL(bkr_unit_list);


/*
 * ========================================================================
 *                          Parameters and constants
 * ========================================================================
 */

#define  BKR_VERSION            "3.101"
#define  BKR_MIN_TIMEOUT        15      /* seconds */
#define  BKR_MAX_TIMEOUT        90      /* seconds */
#define  MODE_INITIALIZER       (BKR_NTSC | BKR_LOW | BKR_SP)
#define  DEFAULT_DEV_MODE       (S_IRUGO | S_IWUGO)
#define  MAX_PROC_MSG_LENGTH    1024    /* ASCII characters */
#define  DEVICE_TIMEOUT         (20*HZ)


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
static unsigned int poll(struct file *, struct poll_table_struct *);
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

typedef struct
	{
	bkr_unit_t  *unit;              /* this descriptor's unit */
	int  mode;                      /* this device file's mode */
	int  last_error;                /* Pending error code if != 0 */
	} bkr_descriptor_t;             /* per-file-descriptor info */

#define  DESCRIPTOR_INITIALIZER  ((bkr_descriptor_t) { NULL, 0, 0 })

LIST_HEAD(bkr_unit_list);                       /* list of installed units */
static devfs_handle_t  bkr_dev_directory;       /* root of dev tree */

/*
 * File operations array.  Order must match bkr_state_t.
 */

#define STOPPED_OPS                                                  \
	{ THIS_MODULE, llseek, start_read, start_write, NULL, poll,  \
	  ioctl, NULL, open, NULL,      release }

#define READING_OPS                                                  \
	{ THIS_MODULE, llseek,       read, start_write, NULL, poll,  \
	  ioctl, NULL, open, NULL, stop_release }

#define WRITING_OPS                                                  \
	{ THIS_MODULE, llseek, start_read,       write, NULL, poll,  \
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

static int bkr_do_status(ctl_table *table, int write, struct file *filp, void *buf, size_t *len)
{
	char  message[MAX_PROC_MSG_LENGTH], *pos = message;
	bkr_unit_t  *unit = (bkr_unit_t *) table->data;
	bkr_device_t  *device = &unit->device;
	bkr_stream_t  *stream = &unit->stream;

	if(filp->f_pos)
		{
		*len = 0;
		return(0);
		}

	pos += sprintf(pos, "Current State   : ");
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
	                    "Sector Number   : %+09d\n"
	                    "Byte Errors     : %u\n"
	                    "In Worst Block  : %u / %u\n"
	                    "Recently        : %u\n"
	                    "Bad Blocks      : %u\n"
	                    "Framing Errors  : %u\n"
	                    "Overrun Errors  : %u\n"
	                    "Underflows      : %u\n"
	                    "Worst Key       : %u / "STRINGIFY(BKR_MAX_KEY_WEIGHT)"\n"
	                    "Closest Non-Key : %u\n"
	                    "Smallest Field  : %u\n"
	                    "Largest Field   : %u\n"
	                    "I/O Buffer      : %u / %u\n",
	                    device->mode,
	                    stream->header.number,
	                    stream->health.total_errors,
	                    stream->errors.symbol, stream->rs_format.parity,
	                    stream->errors.recent_symbol,
	                    stream->errors.block,
	                    stream->errors.frame,
	                    stream->errors.overrun,
	                    stream->errors.underrun,
	                    stream->health.worst_key,
	                    stream->health.best_nonkey,
	                    stream->health.smallest_field,
	                    stream->health.largest_field,
		            bytes_in_buffer(device->io_head, device->io_tail, device->io_size), device->io_size);
	stream->errors.recent_symbol = 0;

	if(pos - message < *len)
		*len = pos - message;
	filp->f_pos += *len;

	return(copy_to_user(buf, message, *len) ? -EFAULT : 0);
}


static bkr_sysctl_table_t bkr_sysctl_template =
	{ NULL,
	  { { CTL_DEV, "dev", NULL, 0, 0555, NULL },
	    { 0 } },
	  { { DEV_BACKER, "backer", NULL, 0, 0555, NULL },
	    { 0 } },
	  { { 0, NULL, NULL, 0, 0555, NULL },
	    { 0 } },
	  { { BACKER_STATUS, "status", NULL, 0, 0444, NULL, bkr_do_status },
	    { BACKER_FORMAT, "format_table", NULL, sizeof(((bkr_device_t *) NULL)->format_info), 0644, NULL, proc_dointvec },
	    { 0 } } };


static void  bkr_proc_register(bkr_unit_t *unit)
{
	bkr_sysctl_table_t  *sysctl = &unit->sysctl;

	*sysctl = bkr_sysctl_template;

	sysctl->dev_dir[0].child     = sysctl->driver_dir;
	sysctl->driver_dir[0].child  = sysctl->unit_dir;
	sysctl->unit_dir[0].ctl_name = unit->number + 1;	/* can't use 0 */
	sysctl->unit_dir[0].procname = unit->name;
	sysctl->unit_dir[0].child    = sysctl->entries;
	sysctl->entries[0].data = unit;
	sysctl->entries[1].data = unit->device.format_info;

	sysctl->header = register_sysctl_table(sysctl->dev_dir, 0);
}


static void  bkr_proc_unregister(bkr_unit_t *unit)
{
	bkr_sysctl_table_t  *sysctl = &unit->sysctl;

	if(sysctl->header)
		{
		unregister_sysctl_table(sysctl->header);
		sysctl->header = NULL;
		}
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

	down(&unit->state_lock);
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
	up(&unit->state_lock);

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

bkr_unit_t *bkr_unit_register(pm_dev_t pm_type, bkr_device_ops_t *ops, int private_size)
{
	struct list_head  *curr;
	bkr_unit_t  *new;
	int  i;
	char  msg[100];

	/*
	 * Make new unit descriptor.
	 */

	sprintf(msg, "can't create unit descriptor");
	new = (bkr_unit_t *) kmalloc(sizeof(bkr_unit_t), GFP_KERNEL);
	if(new == NULL)
		goto no_mem;
	if(private_size)
		{
		new->device.private = kmalloc(private_size, GFP_KERNEL);
		if(new->device.private == NULL)
			goto no_private_mem;
		}
	else
		new->device.private = NULL;

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
	if(snprintf(new->name, BKR_NAME_LENGTH, "%u", new->number) < 0)
		goto no_number;
	list_add_tail(&new->list, curr);

	/*
	 * Create unit's device files.
	 */

	sprintf(msg, "%s: can't create devfs entires", new->name);
	new->devfs_handle = devfs_mk_dir(bkr_dev_directory, new->name, NULL);
	if(new->devfs_handle == NULL)
		goto no_dev_directory;
	for(i = 0; i < BKR_NUM_FORMATS; i++)
		devfs_register(new->devfs_handle, bkr_modes[i].name, DEVFS_FL_DEFAULT, BKR_MAJOR, new->number * BKR_NUM_FORMATS + i, S_IFCHR | DEFAULT_DEV_MODE, &file_ops[BKR_STOPPED], NULL);

	/*
	 * Initialize unit.
	 */

	init_MUTEX(&new->state_lock);
	init_MUTEX(&new->io_lock);
	new->device.state = BKR_STOPPED;
	new->device.ops = ops;
	memcpy(new->device.format_info, BKR_FORMAT_INFO_INITIALIZER, sizeof(BKR_FORMAT_INFO_INITIALIZER));
	new->stream.buffer = NULL;
	init_waitqueue_head(&new->io_queue);
	bkr_format_reset(&new->device, &new->stream, MODE_INITIALIZER, BKR_STOPPED);
	new->pm_handle = pm_register(pm_type, PM_SYS_UNKNOWN, bkr_pm_callback);
	if(new->pm_handle != NULL)
		new->pm_handle->data = new;
	bkr_proc_register(new);

	/*
	 * Success.
	 */

	return(new);

	/*
	 * There was a problem.
	 */

	no_dev_directory:
		list_del(&new->list);
	no_number:
		kfree(new->device.private);
	no_private_mem:
		kfree(new);
	no_mem:
		printk(KERN_WARNING MODULE_NAME ": %s\n", msg);
		return(NULL);
}


void bkr_unit_unregister(bkr_unit_t *unit)
{
	if(unit != NULL)
		{
		list_del(&unit->list);
		devfs_unregister(unit->devfs_handle);
		bkr_proc_unregister(unit);
		pm_unregister(unit->pm_handle);
		kfree(unit->device.private);
		kfree(unit->stream.buffer);
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

	number = MINOR(inode->i_rdev) / BKR_NUM_FORMATS;
	mode = bkr_modes[MINOR(inode->i_rdev) % BKR_NUM_FORMATS].mode;

	private = (bkr_descriptor_t *) kmalloc(sizeof(bkr_descriptor_t), GFP_KERNEL);
	if(private == NULL)
		return(-ENOMEM);

	*private = DESCRIPTOR_INITIALIZER;
	private->mode = mode;
	list_for_each(curr, &bkr_unit_list)
		{
		private->unit = list_entry(curr, bkr_unit_t, list);
		if(private->unit->number == number)
			{
			if(private->unit->device.ops->owner)
				__MOD_INC_USE_COUNT(private->unit->device.ops->owner);
			filp->private_data = private;
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
	bkr_unit_t  *unit = ((bkr_descriptor_t *) filp->private_data)->unit;
	struct semaphore  *lock = &unit->state_lock;
	bkr_device_t  *device = &unit->device;
	bkr_stream_t  *stream = &unit->stream;

	down(lock);

	if(device->state == BKR_WRITING)
		{
		if(BKR_FORMAT(device->mode) != BKR_RAW)
			{
			do
				if(bkr_sector_write_eor(device, stream) != -EAGAIN)
					break;
			while(interruptible_sleep_on_timeout(&unit->io_queue, DEVICE_TIMEOUT));
			}
		do
			if(device->ops->flush(device) != -EAGAIN)
				break;
		while(interruptible_sleep_on_timeout(&unit->io_queue, DEVICE_TIMEOUT));
		}

	device->ops->stop(device);
	pm_dev_idle(unit->pm_handle);

	up(lock);

	return(release(inode, filp));
}

static int release(struct inode *inode, struct file *filp)
{
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;

	if(private->unit->device.ops->owner)
		__MOD_DEC_USE_COUNT(private->unit->device.ops->owner);

	kfree(private);

	return(0);
}


/*
 * poll()
 */

static unsigned int poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int  status = 0;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_unit_t  *unit = private->unit;
	bkr_device_t  *device = &unit->device;
	bkr_stream_t  *stream = &unit->stream;

	poll_wait(filp, &unit->io_queue, wait);

	if(device->state == BKR_READING)
		{
		if((stream->pos < stream->end) ||
		   (bytes_in_buffer(device->io_head, device->io_tail, device->io_size) > stream->fmt.field_size))
			status |= POLLIN | POLLRDNORM;
		}
	else if(device->state == BKR_WRITING)
		{
		if((stream->pos < stream->end) ||
		   (space_in_buffer(device->io_head, device->io_tail, device->io_size) > stream->fmt.field_size))
			status |= POLLOUT | POLLWRNORM;
		}
	if(private->last_error)
		status |= POLLERR;

	return(status);
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
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_stream_t  *stream = &private->unit->stream;
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

			default:
			return(-EINVAL);
			}

		case MTIOCGET:
		arg.mtget.mt_type = MT_ISUNKNOWN;
		arg.mtget.mt_resid = stream->end - stream->pos;
		arg.mtget.mt_dsreg = private->mode;
		arg.mtget.mt_gstat = GMT_ONLINE(-1L);
		arg.mtget.mt_erreg = stream->health.total_errors;
		arg.mtget.mt_fileno = 0;
		arg.mtget.mt_blkno = stream->header.number;
		copy_to_user((void *) argument, &arg.mtget, sizeof(struct mtget));
		return(0);

		case MTIOCPOS:
		arg.mtpos.mt_blkno = stream->header.number;
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
	bkr_stream_t  *stream = &unit->stream;

	down(&unit->state_lock);

	if(device->state != BKR_STOPPED)
		result = -EBUSY;
	else if((result = bkr_format_reset(device, stream, private->mode, direction)) < 0)
		{
		}
	else if((result = device->ops->start(device, direction)) < 0)
		{
		}
	else
		{
		pm_access(unit->pm_handle);
		filp->f_op = &file_ops[direction];
		}

	up(&unit->state_lock);
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
 * These functions do not allow re-entrance.
 */

static ssize_t read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_unit_t  *unit = private->unit;
	struct semaphore  *lock = &unit->io_lock;
	bkr_device_t  *device = &unit->device;
	bkr_stream_t  *stream = &unit->stream;

	if(down_trylock(lock))
		return(-EBUSY);

	result = private->last_error;
	if(result)
		private->last_error = 0;
	else while(1)
		{
		chunk_size = stream->end - stream->pos;
		if(chunk_size > count)
			chunk_size = count;

		copy_to_user(buff, stream->pos, chunk_size);

		stream->pos += chunk_size;
		buff += chunk_size;
		moved += chunk_size;
		count -= chunk_size;

		if(count == 0)  /* complete? */
			result = moved;
		else if((result = stream->read(device, stream)) >= 0)  /* read success? */
			{
			if(stream->pos != stream->end)  /* not EOF? */
				continue;  /* keep going */
			result = moved;
			}
		else if(result != -EAGAIN)  /* real error? */
			{
			if(moved)  /* partially complete? */
				{
				private->last_error = result;  /* save for later */
				result = moved;
				}
			}
		else if(moved)  /* partially complete? */
			result = moved;
		else if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY)))  /* OK to block? */
			{
			if(!interruptible_sleep_on_timeout(&unit->io_queue, DEVICE_TIMEOUT))
				result = -ETIMEDOUT;
			else if(signal_pending(current))
				result = -EINTR;
			else
				continue;
			}
		else if(!(filp->f_flags & O_NONBLOCK))  /* return 0 or -EAGAIN? */
			result = 0;
		break;
		}

	up(lock);
	return(result);
}

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	bkr_descriptor_t  *private = (bkr_descriptor_t *) filp->private_data;
	bkr_unit_t  *unit = private->unit;
	struct semaphore  *lock = &unit->io_lock;
	bkr_device_t  *device = &unit->device;
	bkr_stream_t  *stream = &unit->stream;

	if(down_trylock(lock))
		return(-EBUSY);

	result = private->last_error;
	if(result)
		private->last_error = 0;
	else while(1)
		{
		chunk_size = stream->end - stream->pos;
		if(chunk_size > count)
			chunk_size = count;

		copy_from_user(stream->pos, buff, chunk_size);

		stream->pos += chunk_size;
		buff += chunk_size;
		moved += chunk_size;
		count -= chunk_size;

		if(count == 0)  /* complete? */
			result = moved;
		else if((result = stream->write(device, stream)) >= 0)  /* write success? */
			continue;
		else if(result != -EAGAIN)  /* real error? */
			{
			if(moved)  /* partially complete? */
				{
				private->last_error = result;  /* save for later */
				result = moved;
				}
			}
		else if(moved)  /* partially complete? */
			result = moved;
		else if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY)))  /* OK to block? */
			{
			if(!interruptible_sleep_on_timeout(&unit->io_queue, DEVICE_TIMEOUT))
				result = -ETIMEDOUT;
			else if(signal_pending(current))
				result = -EINTR;
			else
				continue;
			}
		else if(!(filp->f_flags & O_NONBLOCK))  /* return 0 or -EAGAIN? */
			result = 0;
		break;
		}

	up(lock);
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
