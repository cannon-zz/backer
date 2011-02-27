/*
 * Linux device driver for Danmere's Backer 16/32 video tape backup devices.
 *
 *                           Kernel Interface
 *
 * Copyright (C) 2000,2001,2002,2010  Kipp C. Cannon
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

#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/mtio.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/wait.h>

#include <linux/semaphore.h>
#include <asm/uaccess.h>

#include <backer.h>
#include <bkr_unit.h>
#include <bkr_ring_buffer.h>

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)

#define IPRINTK(format, args...) { printk(KERN_INFO MODULE_NAME ": " format, ##args); }
#define WPRINTK(format, args...) { printk(KERN_WARNING MODULE_NAME ": " format, ##args); }


/*
 * ========================================================================
 *                          Module info and options
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16/32 device driver v" BKR_VERSION);
MODULE_SUPPORTED_DEVICE("backer");
MODULE_LICENSE("GPL");

#define  MODULE_NAME  "backer"

EXPORT_SYMBOL_GPL(bkr_unit_register);
EXPORT_SYMBOL_GPL(bkr_unit_unregister);
EXPORT_SYMBOL_GPL(bkr_unit_list);
EXPORT_SYMBOL_GPL(bkr_unit_list_lock);
EXPORT_SYMBOL_GPL(memset_ring);


/*
 * ========================================================================
 *                          Function prototypes
 * ========================================================================
 */

static int open(struct inode *, struct file *);
static int release(struct inode *, struct file *);
static ssize_t start_read(struct file *, char *, size_t, loff_t *);
static ssize_t start_write(struct file *, const char *, size_t, loff_t *);
static ssize_t read(struct file *, char *, size_t, loff_t *);
static ssize_t write(struct file *, const char *, size_t, loff_t *);
static unsigned int poll(struct file *, struct poll_table_struct *);
static int ioctl(struct inode *, struct file *, unsigned int, unsigned long);


/*
 * ========================================================================
 *                              Global Data
 * ========================================================================
 */

LIST_HEAD(bkr_unit_list);               /* list of installed units */
struct semaphore  bkr_unit_list_lock;   /* unit list locking semaphore */

/*
 * File operations array.  Order MUST match bkr_direction_t.
 */

#define STOPPED_OPS {         \
	.owner = THIS_MODULE, \
	.read = start_read,   \
	.write = start_write, \
	.poll = poll,         \
	.ioctl = ioctl,       \
	.open = open,         \
	.release = release    \
}

#define READING_OPS {         \
	.owner = THIS_MODULE, \
	.read = read,         \
	.write = start_write, \
	.poll = poll,         \
	.ioctl = ioctl,       \
	.open = open,         \
	.release = release    \
}

#define WRITING_OPS {         \
	.owner = THIS_MODULE, \
	.read = start_read,   \
	.write = write,       \
	.poll = poll,         \
	.ioctl = ioctl,       \
	.open = open,         \
	.release = release    \
}

static struct file_operations file_ops[] = {
	STOPPED_OPS,
	READING_OPS,
	WRITING_OPS
};


/*
 * ========================================================================
 *                            ENTRY/EXIT CODE
 * ========================================================================
 */

static int __init bkr_init(void)
{
	int  result;

	IPRINTK("Backer 16/32 tape device driver version " BKR_VERSION "\n");

	result = register_chrdev(BKR_MAJOR, MODULE_NAME, &file_ops[BKR_STOPPED]);
	if(result < 0) {
		WPRINTK("can't register device\n");
		return result;
	}

	init_MUTEX(&bkr_unit_list_lock);
	request_module("backer_lowlevel");
	return 0;
}

static void __exit bkr_exit(void)
{
	unregister_chrdev(BKR_MAJOR, MODULE_NAME);
}

module_init(bkr_init);
module_exit(bkr_exit);


/*
 * ========================================================================
 *                               /PROC I/O
 * ========================================================================
 */

#define  MAX_PROC_MSG_LENGTH  1024      /* ASCII characters */

/* FIXME: what's ppos for ? */
static int bkr_do_status(struct ctl_table *table, int write, void __user *buf, size_t *len, loff_t *ppos)
{
	char  message[MAX_PROC_MSG_LENGTH], *pos = message;
	struct bkr_unit_t  *unit = table->data;
	struct bkr_stream_t  *stream = unit->stream;

	if(!stream) {
		*len = 0;
		return 0;
	}

	pos += sprintf(pos, "Current State   : ");
	switch(stream->direction) {
	case BKR_READING:
		pos += sprintf(pos, "READING");
		break;

	case BKR_WRITING:
		pos += sprintf(pos, "WRITING");
		break;

	case BKR_STOPPED:
		pos += sprintf(pos, "STOPPED");
		break;
	}
	pos += sprintf(pos, "\nCurrent Mode    : %u\nI/O Buffer      : ", stream->mode);
	if(stream->ring) {
		ring_lock(stream->ring);
		pos += sprintf(pos, "%zu / %zu\n", bytes_in_ring(stream->ring), stream->ring->size);
		ring_unlock(stream->ring);
	} else
		pos += sprintf(pos, "0 / 0\n");

	if(pos - message < *len)
		*len = pos - message;

	return copy_to_user(buf, message, *len) ? -EFAULT : 0;
}


/*
 * ========================================================================
 *                          UNIT CLAIM/RELEASE
 * ========================================================================
 */

/*
 * These are used to claim/release a hardware unit.
 */

static int bkr_unit_claim(struct bkr_unit_t *unit)
{
	int  result = 0;

	down(&bkr_unit_list_lock);
	if(!try_module_get(unit->owner)) {
		result = -ENODEV;
	} else if(down_trylock(&unit->lock)) {
		module_put(unit->owner);
		result = -EBUSY;
	}
	up(&bkr_unit_list_lock);

	return result;
}

static void bkr_unit_release(struct bkr_unit_t *unit)
{
	down(&bkr_unit_list_lock);
	up(&unit->lock);
	module_put(unit->owner);
	up(&bkr_unit_list_lock);
}


/*
 * ========================================================================
 *                     DEVICE REGISTER / UNREGISTER
 * ========================================================================
 */

/*
 * Initialize a unit's sysctl tables.
 */

static void bkr_unit_sysctl_init(struct bkr_unit_t *unit)
{
	struct bkr_sysctl_table_t initializer = {
		.dev_dir = {
			{.procname = "dev", .mode = 0555, .child = unit->sysctl.driver_dir},
			{0}
		},
		.driver_dir = {
			{.procname = "backer", .mode = 0555, .child = unit->sysctl.unit_dir},
			{0}
		},
		.unit_dir = {
			{.procname = unit->name, .mode = 0555, .child = unit->sysctl.entries},
			{0}
		},
		.entries = {
			{.procname = "status", .mode = 0444, .proc_handler = bkr_do_status},
			{.procname = "format_table", .mode = 0644, .data = unit->format_tbl, .maxlen = sizeof(unit->format_tbl), .proc_handler = proc_dointvec},
			{0}
		}
	};

	unit->sysctl = initializer;
}


/*
 * These functions are called by the device I/O modules to register and
 * unregister the devices they find.  The calling procedure MUST hold the
 * list lock upon entry into both.
 */

struct bkr_unit_t *bkr_unit_register(struct bkr_stream_t *stream)
{
	struct list_head  *curr;
	struct bkr_unit_t  *unit;

	/*
	 * Make new unit descriptor.
	 */

	unit = kmalloc(sizeof(*unit), GFP_KERNEL);
	if(!unit)
		goto no_mem;

	/*
	 * Find lowest available number.
	 * FIXME: should I use the "IDR" facility?
	 * Should the numbers be deterministic?  eg. base I/O address?
	 */

	unit->number = 0;
	list_for_each(curr, &bkr_unit_list) {
		if(list_entry(curr, struct bkr_unit_t, list)->number != unit->number)
			break;
		unit->number++;
	}
	if(snprintf(unit->name, BKR_NAME_LENGTH, "%u", unit->number) < 0)
		goto no_number;
	list_add_tail(&unit->list, curr);

	/*
	 * Initialize unit.
	 */

	init_MUTEX(&unit->lock);
	init_waitqueue_head(&unit->queue);
	memcpy(unit->format_tbl, BKR_FORMAT_INFO_INITIALIZER, sizeof(BKR_FORMAT_INFO_INITIALIZER));
	unit->stream = stream;
	bkr_unit_sysctl_init(unit);
	unit->sysctl.header = register_sysctl_table(unit->sysctl.dev_dir);

	/*
	 * Success.
	 */

	return unit;

	/*
	 * There was a problem.
	 */

	no_number:
		kfree(unit);
	no_mem:
		WPRINTK("no memory creating unit descriptor\n");
		return NULL;
}


void bkr_unit_unregister(struct bkr_unit_t *unit)
{
	list_del(&unit->list);
	if(unit->sysctl.header)
		unregister_sysctl_table(unit->sysctl.header);
	kfree(unit);
}


/*
 * ========================================================================
 *                            FILE OPERATIONS
 * ========================================================================
 */

/*
 * Generic event notification for the kernel interface.
 */

static void io_callback(void *data)
{
	wake_up_interruptible(data);
}


/*
 * Device open method.
 */

static int open(struct inode *inode, struct file *filp)
{
	/* FIXME: put minor numbers in the same order as the format table */
	static const int minor_to_mode[BKR_NUM_FORMATS] = {
		BKR_NTSC | BKR_HIGH,
		BKR_NTSC | BKR_LOW,
		BKR_PAL  | BKR_HIGH,
		BKR_PAL  | BKR_LOW
	};
	struct bkr_unit_t  *unit = NULL;
	int  number = iminor(inode) / BKR_NUM_FORMATS;
	int  mode = minor_to_mode[iminor(inode) % BKR_NUM_FORMATS];
	int  result;
	struct list_head  *curr;

	down(&bkr_unit_list_lock);
	list_for_each(curr, &bkr_unit_list) {
		unit = list_entry(curr, struct bkr_unit_t, list);
		if(unit->number == number)
			break;
	}
	up(&bkr_unit_list_lock);
	if(!unit || unit->number != number)
		return -ENODEV;

	filp->private_data = unit;

	result = bkr_unit_claim(unit);
	if(result < 0)
		return result;

	/* FIXME: lock format table during copy */
	if(!unit->stream->ops.ready(unit->stream, mode, &unit->format_tbl[bkr_mode_to_format(mode)])) {
		bkr_unit_release(unit);
		return -EBUSY;
	}

	unit->last_error = 0;
	bkr_stream_set_callback(unit->stream, io_callback, &unit->queue);
	nonseekable_open(inode, filp);

	return 0;
}


/*
 * Device release (close) method.
 * FIXME: should we return -EAGAIN for a non-blocking release()?
 */

static int release(struct inode *inode, struct file *filp)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;

	while(stream->ops.release(stream) == -EAGAIN) {
		/* FIXME: anthing that takes us out of this loop other than a
		 * success report from the stream's release() method causes a
		 * memory leak */
#if 1
		if(!interruptible_sleep_on_timeout(&unit->queue, stream->timeout)) {
			printk(KERN_INFO " a\n");
			break;	/* timeout */
		}
		if(signal_pending(current)) {
			printk(KERN_INFO " b\n");
			break;	/* signal received */
		}
#else
		sleep_on(&unit->queue);
#endif
	}

	bkr_unit_release(unit);

	return 0;
}


/*
 * Device poll method.
 */

static unsigned int poll(struct file *filp, struct poll_table_struct *wait)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	unsigned int  status = 0;

	poll_wait(filp, stream->callback_data, wait);

	ring_lock(stream->ring);
	switch(stream->direction) {
	case BKR_READING:
		if(bytes_in_ring(stream->ring))
			status |= POLLIN | POLLRDNORM;
		break;

	case BKR_WRITING:
		if(space_in_ring(stream->ring))
			status |= POLLOUT | POLLWRNORM;
		break;

	default:
		break;
	}
	ring_unlock(stream->ring);

	if(unit->last_error)
		status |= POLLERR;

	return status;
}


/*
 * The general intent is for this driver to provide a standard magnetic
 * tape interface.  To this end we try to implement as many of the standard
 * mtio.h ioctl's as make sense for this device.
 */

static int ioctl(struct inode *inode, struct file *filp, unsigned int op, unsigned long argument)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	void  *p = (void *) argument;
	union {
		struct mtop  mtop;
		struct mtget  mtget;
		struct mtpos  mtpos;
	} arg;

	switch(op) {
	case MTIOCTOP:
		if(copy_from_user(&arg.mtop, p, sizeof(arg.mtop)))
			return -EFAULT;
		switch(arg.mtop.mt_op) {
		case MTNOP:
		case MTRESET:
			return 0;

		default:
			return -EINVAL;
		}

	case MTIOCGET:
		arg.mtget = (struct mtget) {
			.mt_type = MT_ISUNKNOWN,
#if 0
			/* FIXME */
			.mt_resid = stream->ops.movable(stream),
#else
			.mt_resid = 0,
#endif
			.mt_dsreg = stream->mode,
			.mt_gstat = GMT_ONLINE(-1L),
			.mt_erreg = 0,
			.mt_fileno = 0,
			.mt_blkno = 0
		};
		if(copy_to_user(p, &arg.mtget, sizeof(arg.mtget)))
			return -EFAULT;
		return 0;

	default:
		return -EINVAL;
	}
}


/*
 * These functions start data transfers when the first call to a read() or
 * write() method is made.
 */

static ssize_t start_read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	ssize_t  result;

	if((result = stream->ops.start(stream, BKR_READING)) >= 0) {
		filp->f_op = &file_ops[BKR_READING];
		result = filp->f_op->read(filp, buff, count, posp);
	}

	return result;
}

static ssize_t start_write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	ssize_t  result;

	if((result = stream->ops.start(stream, BKR_WRITING)) >= 0) {
		filp->f_op = &file_ops[BKR_WRITING];
		result = filp->f_op->write(filp, buff, count, posp);
	}

	return result;
}


/*
 * Device read method.
 */

static ssize_t read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	ssize_t  result;
	size_t  moved = 0;
	size_t  chunk_size;

	/* Is there an error from an earlier call? */
	result = unit->last_error;
	unit->last_error = 0;
	/* No, so move data */
	if(!result) while(1) {
		ring_lock(stream->ring);
		result = stream->ops.read(stream);
		ring_unlock(stream->ring);

		if(result > 0) {
			chunk_size = min(count, (size_t) result);
			ring_lock(stream->ring);
			/* FIXME: check return value */
			copy_to_user_from_ring(buff, stream->ring, chunk_size);
			ring_unlock(stream->ring);
			buff += chunk_size;
			moved += chunk_size;
			count -= chunk_size;
			/* Are we done? */
			if(!count) {
				result = moved;
				break;
			}
			continue;
		}

		/* Need to block? */
		if(result == -EAGAIN) {
			/* Have we moved anything? */
#if O_NONBLOCK == O_NDELAY
			if(moved)
				result = moved;
#else
			if(moved || ((filp->f_flags & (O_NONBLOCK | O_NDELAY)) == O_NDELAY))
				result = moved;
#endif
			/* Block if its OK to do so */
			else if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY))) {
				if(!interruptible_sleep_on_timeout(&unit->queue, stream->timeout))
					result = -ETIMEDOUT;
				else if(signal_pending(current))
					result = -EINTR;
				else
					continue;
			}
			break;
		}

		/* EOF? */
		if(result == 0) {
			result = moved;
			break;
		}

		/* It's a real error! */
		/* If we've moved anything, save error code for later */
		if(moved) {
			unit->last_error = result;
			result = moved;
		}
		break;
	}

	return result;
}


/*
 * Device write method.
 */

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	struct bkr_unit_t  *unit = filp->private_data;
	struct bkr_stream_t  *stream = unit->stream;
	ssize_t  result;
	size_t  moved = 0;
	size_t  chunk_size;

	/* Is there an error from an earlier call? */
	result = unit->last_error;
	unit->last_error = 0;
	/* No, so move data */
	if(!result) while(1) {
		ring_lock(stream->ring);
		result = stream->ops.write(stream);
		ring_unlock(stream->ring);

		if(result >= 0) {
			chunk_size = min(count, (size_t) result);
			ring_lock(stream->ring);
			/* FIXME: check return value */
			copy_to_ring_from_user(stream->ring, buff, chunk_size);
			ring_unlock(stream->ring);
			buff += chunk_size;
			moved += chunk_size;
			count -= chunk_size;
			/* Are we done? */
			if(!count) {
				result = moved;
				break;
			}
			continue;
		}

		/* Need to block? */
		if(result == -EAGAIN) {
			/* Have we moved anything? */
#if O_NONBLOCK == O_NDELAY
			if(moved)
				result = moved;
#else
			if(moved || ((filp->f_flags & (O_NONBLOCK | O_NDELAY)) == O_NDELAY))
				result = moved;
#endif
			/* Is it OK to block? */
			else if(!(filp->f_flags & (O_NONBLOCK | O_NDELAY))) {
				if(!interruptible_sleep_on_timeout(&unit->queue, stream->timeout))
					result = -ETIMEDOUT;
				else if(signal_pending(current))
					result = -EINTR;
				else
					continue;
			}
			break;
		}

		/* It's a real error! */
		/* If we've moved anything, save error code for later */
		if(moved) {
			unit->last_error = result;
			result = moved;
		}
		break;
	}

	return result;
}
