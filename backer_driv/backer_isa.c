/*
 * backer_isa
 *
 * Linux 2.4.x driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                   ISA Device I/O & Kernel Interface
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
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/mtio.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"


/*
 * WORKAROUND defined == enable work-arounds & disable experimental code
 */

#define  WORKAROUND


/*
 * ========================================================================
 * Parameters and constants
 * ========================================================================
 */

#define  BKR_NAME               "backer"
#define  BKR_VERSION            "3.1"
#define  BKR_MINOR_BASE         0       /* first minor number */
#define  BKR_PROC_NAME          "driver/"BKR_NAME

#define  MAX_RETRY_FREQ         50      /* max blocked retry rate in Hz */
#define  BKR_MAX_TIMEOUT        30      /* seconds */

#define  BKR_MAX_DEVICES  3             /* init_module()'s limit is 9 */

#if (MAX_UPDATE_FREQ > HZ)
#error "MAX_UPDATE_FREQ is too high"
#endif


/*
 * ========================================================================
 * Module info and options.
 * ========================================================================
 */

MODULE_AUTHOR("Kipp Cannon (kcannon@users.sourceforge.net)");
MODULE_DESCRIPTION("Backer 16 & 32 device driver");
MODULE_SUPPORTED_DEVICE("backer");

static unsigned int dma[BKR_MAX_DEVICES] __initdata    = { };
static unsigned int ioport[BKR_MAX_DEVICES] __initdata = { };
static unsigned int timeout = CONFIG_BACKER_TIMEOUT;

MODULE_PARM(ioport, "1-" __MODULE_STRING(BKR_MAX_DEVICES) "i");
MODULE_PARM_DESC(ioport, "I/O ports");
MODULE_PARM(dma, "1-" __MODULE_STRING(BKR_MAX_DEVICES) "i");
MODULE_PARM_DESC(dma, "DMA channels");
MODULE_PARM(timeout, "i");
MODULE_PARM_DESC(timeout, "Timeout (seconds)");


/*
 * ========================================================================
 *                                 Types
 * ========================================================================
 */

typedef struct
	{
	bkr_device_t  *device;          /* device state descriptor */
	bkr_sector_t  *sector;          /* format state descriptor */
	struct pm_dev  *pm_handle;      /* power management handle */
	int  mode;                      /* this device file's mode */
	int  last_error;                /* Pending error code if != 0 */
	} bkr_private_t;                /* per file descriptor info */

#define  PRIVATE_INITIALIZER  ((bkr_private_t) { NULL, NULL, NULL, 0, 0 })

typedef struct
	{
	int mode;
	char name[4];
	} bkr_modes_t;


/*
 * ========================================================================
 *                          Function prototypes
 * ========================================================================
 */

static int     bkr_isa_probe(bkr_device_t *, int);
static int     open(struct inode *, struct file *);
static int     release(struct inode *, struct file *);
static int     stop_release(struct inode *, struct file *);
static ssize_t start_common(struct file *, bkr_state_t);
static ssize_t start_read(struct file *, char *, size_t, loff_t *);
static ssize_t start_write(struct file *, const char *, size_t, loff_t *);
static ssize_t read(struct file *, char *, size_t, loff_t *);
static ssize_t write(struct file *, const char *, size_t, loff_t *);
static int     ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static loff_t  llseek(struct file *, loff_t, int);
static int     bkr_read_proc(char *, char **, off_t, int, int *, void *);
static int     bkr_pm_callback(struct pm_dev *, pm_request_t, void *);


/*
 * ========================================================================
 *                              Global Data
 * ========================================================================
 */

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
 * Mode descriptions
 */

static bkr_modes_t bkr_modes[] =
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

#define  NUM_MODES  (sizeof(bkr_modes)/sizeof(bkr_modes_t))

/*
 * Device and software state descriptions.
 */

static int bkr_num_devices = 0;
static bkr_device_t bkr_device[BKR_MAX_DEVICES] = { };
static bkr_sector_t bkr_sector[BKR_MAX_DEVICES] = { };

/*
 * devfs handles for directory and device files, proc handle and power
 * management handles.
 */

static devfs_handle_t  dev_directory = NULL;
static devfs_handle_t  dev_entry[BKR_MAX_DEVICES * NUM_MODES] = { };
static struct proc_dir_entry  *proc_entry = NULL;
static struct pm_dev  *bkr_pm_handle[BKR_MAX_DEVICES] = { };


/*
 * ========================================================================
 *                                Macros
 * ========================================================================
 */

#define PRIVATE_PTR(filp)  ((bkr_private_t *) (filp)->private_data)



/*
 * ========================================================================
 *
 *                            ENTRY/EXIT CODE
 *
 * ========================================================================
 */

int __init init_module(void)
{
	int  unit, mode, minor;
	int  result;
	char  name[sizeof("x/xxx")];

	EXPORT_NO_SYMBOLS;
	printk(KERN_INFO BKR_NAME ": Backer 16/32 tape device driver version " BKR_VERSION "\n");

	/*
	 * Find devices.
	 */

	for(unit = 0; unit < BKR_MAX_DEVICES; unit++)
		if((dma[unit] != 0) && (ioport[unit] != 0))
			{
			if(request_region(ioport[unit], 1, BKR_NAME) < 0)
				{
				printk(KERN_INFO BKR_NAME ": I/O port %#x not available\n", ioport[unit]);
				continue;
				}
			bkr_device[bkr_num_devices].hrdwr.type = BKR_ISA_DEVICE;
			bkr_device[bkr_num_devices].hrdwr.isa.dma = dma[unit];
			bkr_device[bkr_num_devices].hrdwr.isa.ioport = ioport[unit];
			printk(KERN_INFO BKR_NAME ": unit%u: using dma=%u ioport=%#x\n", bkr_num_devices, dma[unit], ioport[unit]);
			bkr_num_devices++;
			}
	bkr_num_devices += bkr_isa_probe(bkr_device, bkr_num_devices);
	if(bkr_num_devices == 0)
		{
		printk(KERN_INFO BKR_NAME ": no devices found\n");
		result = -ENODEV;
		goto no_devices;
		}

	/*
	 * Initialize some data.
	 */

	if(timeout > BKR_MAX_TIMEOUT)
		timeout = BKR_MAX_TIMEOUT;
	timeout *= HZ;

	/*
	 * Grab driver-wide resources.
	 */

	result = devfs_register_chrdev(BKR_MAJOR, BKR_NAME, &file_ops[BKR_STOPPED]);
	if(result < 0)
		{
		printk(KERN_INFO BKR_NAME ": can't register device\n");
		goto cant_register_chrdev;
		}
	dev_directory = devfs_mk_dir(NULL, BKR_NAME, NULL);
	if(dev_directory == NULL)
		{
		printk(KERN_INFO BKR_NAME ": can't register device\n");
		result = -ENODEV;
		goto cant_register_directory;
		}
	proc_entry = create_proc_read_entry(BKR_PROC_NAME, 0, 0, bkr_read_proc, NULL);

	/*
	 * Grab per-unit resources.
	 */

	for(unit = 0; unit < bkr_num_devices; unit++)
		{
		spin_lock_init(&bkr_device[unit].lock);

		bkr_device[unit].buffer = (unsigned char *) pci_alloc_consistent(NULL, BKR_BUFFER_SIZE, &bkr_device[unit].hrdwr.isa.phys_addr);
		if(bkr_device[unit].buffer == NULL)
			{
			printk(KERN_INFO BKR_NAME ": unit%u: can't allocate DMA buffer\n", unit);
			result = -ENOMEM;
			goto cant_init_unit;
			}
		for(mode = 0; mode < NUM_MODES; mode++)
			{
			if(bkr_modes[mode].mode < 0)
				continue;
			sprintf(name, "%1u/%3s", unit, bkr_modes[mode].name);
			minor = unit * NUM_MODES + mode;
			dev_entry[minor] =
				devfs_register(dev_directory, name, DEVFS_FL_DEFAULT,
				   BKR_MAJOR, BKR_MINOR_BASE + minor,
				   S_IFCHR | S_IRUGO | S_IWUGO, &file_ops[BKR_STOPPED],
				   NULL);
			if(dev_entry[minor] == NULL)
				{
				printk(KERN_INFO BKR_NAME ": unit%u: can't register device\n", unit);
				result = -ENODEV;
				goto cant_init_unit;
				}
			}
		bkr_pm_handle[unit] = pm_register(PM_ISA_DEV, PM_SYS_UNKNOWN, bkr_pm_callback);
		if(bkr_pm_handle[unit] != NULL)
			bkr_pm_handle[unit]->data = &bkr_device[unit];
		}

	/*
	 * Driver installed.
	 */

	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_init_unit:
		pm_unregister_all(bkr_pm_callback);
		for(minor = 0; minor < bkr_num_devices * NUM_MODES; minor++)
			devfs_unregister(dev_entry[minor]);
		for(unit = 0; unit < bkr_num_devices; unit++)
			if(bkr_device[unit].buffer != NULL)
				pci_free_consistent(NULL, BKR_BUFFER_SIZE, bkr_device[unit].buffer, bkr_device[unit].hrdwr.isa.phys_addr);
		remove_proc_entry(BKR_PROC_NAME, NULL);
		devfs_unregister(dev_directory);
	cant_register_directory:
		devfs_unregister_chrdev(BKR_MAJOR, BKR_NAME);
	cant_register_chrdev:
		for(unit = 0; unit < bkr_num_devices; unit++)
			release_region(bkr_device[unit].hrdwr.isa.ioport, 1);
	no_devices:

	return(result);
}

void __exit cleanup_module(void)
{
	int i;

	pm_unregister_all(bkr_pm_callback);
	for(i = 0; i < bkr_num_devices * NUM_MODES; i++)
		devfs_unregister(dev_entry[i]);
	for(i = 0; i < bkr_num_devices; i++)
		{
		if(bkr_device[i].buffer != NULL)
			pci_free_consistent(NULL, BKR_BUFFER_SIZE, bkr_device[i].buffer, bkr_device[i].hrdwr.isa.phys_addr);
		release_region(bkr_device[i].hrdwr.isa.ioport, 1);
		kfree(bkr_sector[i].buffer);
		}
	remove_proc_entry(BKR_PROC_NAME, NULL);
	devfs_unregister(dev_directory);
	devfs_unregister_chrdev(BKR_MAJOR, BKR_NAME);
}


/*
 * ========================================================================
 *
 *                            FILE OPERATIONS
 *
 * ========================================================================
 */

/*
 * open() --- Arbitrate device open requests.
 *
 * Access to the data stream is only granted once on a first-come /
 * first-serve basis across all descriptors.  A data transfer is started
 * when the first call to a read() or write() method is made after which
 * all read/write attempts from other descriptors fail.
 */

static int open(struct inode *inode, struct file *filp)
{
	int  unit, mode;

	/*
	printk(KERN_INFO "Backer: %s\n", filp->f_dentry->d_iname);
	*/

	unit = (MINOR(inode->i_rdev) - BKR_MINOR_BASE) / NUM_MODES;
	mode = bkr_modes[(MINOR(inode->i_rdev) - BKR_MINOR_BASE) % NUM_MODES].mode;
	if((unit >= bkr_num_devices) || (mode < 0))
		return(-ENODEV);

	filp->private_data = kmalloc(sizeof(bkr_private_t), GFP_KERNEL);
	if(filp->private_data == NULL)
		return(-ENOMEM);

	MOD_INC_USE_COUNT;

	*PRIVATE_PTR(filp) = PRIVATE_INITIALIZER;
	PRIVATE_PTR(filp)->device = &bkr_device[unit];
	PRIVATE_PTR(filp)->sector = &bkr_sector[unit];
	PRIVATE_PTR(filp)->pm_handle = bkr_pm_handle[unit];
	PRIVATE_PTR(filp)->mode = mode;

	return(0);
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
	bkr_device_t *device = PRIVATE_PTR(filp)->device;
	bkr_sector_t *sector = PRIVATE_PTR(filp)->sector;

	spin_lock(&device->lock);

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
				schedule_timeout(HZ/MAX_RETRY_FREQ);
				}
			while(jiffies < bailout);
			}
		do
			{
			if(bkr_device_flush(device) != -EAGAIN)
				break;
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/MAX_RETRY_FREQ);
			}
		while(jiffies < bailout);
		}

	bkr_device_stop_transfer(device);
	pm_dev_idle(PRIVATE_PTR(filp)->pm_handle);

	spin_unlock(&device->lock);

	return(release(inode, filp));
}

static int release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);

	MOD_DEC_USE_COUNT;

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
	int result;
	bkr_sector_t *sector = PRIVATE_PTR(filp)->sector;
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
			return(0);

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
		arg.mtget.mt_dsreg = PRIVATE_PTR(filp)->mode;
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
	bkr_device_t *device = PRIVATE_PTR(filp)->device;
	bkr_sector_t *sector = PRIVATE_PTR(filp)->sector;

	spin_lock(&device->lock);

	if(device->state != BKR_STOPPED)
		{
		result = -EBUSY;
		goto done;
		}

	pm_access(PRIVATE_PTR(filp)->pm_handle);

	device->mode = PRIVATE_PTR(filp)->mode;
	bkr_device_reset(device, direction);
	if((result = bkr_format_reset(device, sector)) >= 0)
		if((result = bkr_device_start_transfer(device, direction, jiffies + timeout)) >= 0)
			{
			filp->f_op = &file_ops[direction];
			goto done;
			}

	pm_dev_idle(PRIVATE_PTR(filp)->pm_handle);

	done:
	spin_unlock(&device->lock);
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
 * While inside these functions, file operations for the current descriptor
 * are reset to their "stopped" state to elliminate re-entrance.
 */

static ssize_t read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result = 0;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	jiffies_t  bailout;
	bkr_device_t *device = PRIVATE_PTR(filp)->device;
	bkr_sector_t *sector = PRIVATE_PTR(filp)->sector;

	filp->f_op = &file_ops[BKR_STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

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
			break;

		result = sector->read(device, sector);
		if(result == 0)
			continue;
		if(result > 0)
			{
			/* EOF */
			result = 0;
			break;
			}
		if((result != -EAGAIN) || (filp->f_flags & O_NONBLOCK))
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/MAX_RETRY_FREQ);
		if(jiffies >= bailout)
			{
			result = -ETIMEDOUT;
			break;
			}
		if(signal_pending(current))
			{
			result = -EINTR;
			break;
			}
		}
	if(moved)
		{
		PRIVATE_PTR(filp)->last_error = result;
		result = moved;
		}

	done:
	filp->f_op = &file_ops[BKR_READING];
	return(result);
}

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result = 0;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	jiffies_t  bailout;
	bkr_device_t *device = PRIVATE_PTR(filp)->device;
	bkr_sector_t *sector = PRIVATE_PTR(filp)->sector;

	filp->f_op = &file_ops[BKR_STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

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
			break;

		result = sector->write(device, sector);
		if(result == 0)
			continue;
		if((result != -EAGAIN) || (filp->f_flags & O_NONBLOCK))
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/MAX_RETRY_FREQ);
		if(jiffies >= bailout)
			{
			result = -ETIMEDOUT;
			break;
			}
		}
	if(moved)
		{
		PRIVATE_PTR(filp)->last_error = result;
		result = moved;
		}

	done:
	filp->f_op = &file_ops[BKR_WRITING];
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
	bkr_device_t *device;
	bkr_sector_t *sector;
	int  unit;

	for(unit = 0; unit < bkr_num_devices; unit++)
		{
		device = &bkr_device[unit];
		sector = &bkr_sector[unit];

		pos += sprintf(pos, "Unit:             %u (", unit);
		switch(device->state)
			{
			case BKR_READING:
			bkr_device_read(device, 0);
			pos += sprintf(pos, "reading");
			break;

			case BKR_WRITING:
			bkr_device_write(device, 0);
			pos += sprintf(pos, "writing");
			break;

			case BKR_STOPPED:
			pos += sprintf(pos, "stopped");
			break;

			case BKR_SUSPENDED:
			pos += sprintf(pos, "suspended");
			break;
			}

		pos += sprintf(pos, ")\n"
		                    "Current Mode:     %u\n"
		                    "Sector Number:    %lu\n"
		                    "Byte Errors:      %u\n"
		                    "In Worst Block:   %u / %u\n"
		                    "Recently:         %u\n"
		                    "Bad Blocks:       %u\n"
		                    "Framing Errors:   %u\n"
		                    "Overrun Errors:   %u\n"
		                    "Underflows:       %u\n"
		                    "Worst Key:        %u\n"
		                    "Closest Non-Key:  %u\n"
		                    "Least Skipped:    %u\n"
		                    "Most Skipped:     %u\n"
		                    "DMA Buffer:       %u / %u\n",
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
			            bytes_in_buffer(device), device->size);
		sector->errors.recent_symbol = 0;
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
	int  result = 0;
	bkr_device_t *device = (bkr_device_t *) dev->data;

	spin_lock(&device->lock);
	switch(rqst)
		{
		case PM_SUSPEND:
		if(device->state != BKR_STOPPED)
			{
			result = -EBUSY;
			break;
			}
		device->state = BKR_SUSPENDED;
		break;

		case PM_RESUME:
		device->state = BKR_STOPPED;
		break;

		default:
		break;
		}
	spin_unlock(&device->lock);

	return(result);
}


/*
 * ========================================================================
 *
 *                           DEVICE-LEVEL I/O
 *
 * ========================================================================
 */

/*
 * ``Local'' constants/macros/etc.
 */

#define  DMA_IO_TO_MEM          0x14    /* dmand transf, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x18    /* dmand transf, inc addr, auto-init */
#define  DMA_HOLD_OFF           786     /* min abs(head-tail) */

#define  MIN_UPDATE_FREQ        3       /* min DMA update rate in Hz */
#define  MIN_SYNC_FREQ          50      /* min sync frequency in Hz */

#if (MIN_UPDATE_FREQ > HZ) || (MIN_SYNC_FREQ > HZ)
#error "One of the FREQ parameters is too high"
#endif

#define  INTERNAL_BUFFER       4

#define  BIT_HIGH_DENSITY      0x01
#define  BIT_DMA_REQUEST       0x02
#define  BIT_DATA              0x04
#define  BIT_SYNC              0x08
#define  BIT_FRAME_BUSY        0x10
#define  BIT_TRANSMIT          0x20
#define  BIT_RECEIVE           0x40
#define  BIT_NTSC_VIDEO        0x80


/*
 * get_dreq_status()
 *
 * Return the status of a DMA channel's DREQ line.  1 == active, 0 ==
 * inactive.
 */

static inline int get_dreq_status(unsigned int dmanr)
{
	int  result;

	if(dmanr <= 3)
		result = dma_inb(DMA1_STAT_REG) >> 4;
	else
		result = dma_inb(DMA2_STAT_REG);

	return((result >> dmanr) & 1);
}


/*
 * update_dma_offset()
 *
 * Retrieves the offset within the DMA buffer at which the next transfer
 * will occur.  During read operations this is the buffer's head; during
 * write operations it is the tail.  Returns 0 on success, -EIO if no DMA
 * activity is detected.
 */

static int update_dma_offset(bkr_device_t *device, bkr_offset_t *offset)
{
	unsigned long  flags;
	jiffies_t  dma_bailout;

	dma_bailout = jiffies + HZ/MIN_SYNC_FREQ;

	while(~inb(device->hrdwr.isa.ioport) & BIT_DMA_REQUEST)
		 if(jiffies >= dma_bailout)
			return(-EIO);

	while(inb(device->hrdwr.isa.ioport) & BIT_DMA_REQUEST)
		if(jiffies >= dma_bailout)
			return(-EIO);

	flags = claim_dma_lock();
	clear_dma_ff(device->hrdwr.isa.dma);
	*offset = device->size - get_dma_residue(device->hrdwr.isa.dma);
	release_dma_lock(flags);

	device->hrdwr.isa.last_update = jiffies;

	return(0);
}


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the device I/O buffer.  Note
 * that space_in_buffer() + bytes_in_buffer() == device.size - 1.
 */

unsigned int space_in_buffer(bkr_device_t *device)
{
	if(device->tail > device->head)
		return(device->tail - device->head - 1);
	return(device->tail+device->size - device->head - 1);
}

unsigned int bytes_in_buffer(bkr_device_t *device)
{
	if(device->tail > device->head)
		return(device->head+device->size - device->tail);
	return(device->head - device->tail);
}


/*
 * bkr_device_reset()
 *
 * Resets the Backer hardware and device I/O layer.  Always succeeds but
 * assumes the mode (passed in the device structure) is valid.
 */

int bkr_device_reset(bkr_device_t *device, bkr_state_t direction)
{
	outb(0, device->hrdwr.isa.ioport);

	device->hrdwr.isa.control = BIT_DMA_REQUEST;

	if(BKR_DENSITY(device->mode) == BKR_HIGH)
		{
		device->hrdwr.isa.control |= BIT_HIGH_DENSITY;
		device->bytes_per_line = BYTES_PER_LINE_HIGH;
		}
	else
		device->bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(device->mode) == BKR_NTSC)
		{
		device->hrdwr.isa.control |= BIT_NTSC_VIDEO;
		device->frame_size = device->bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
		}
	else
		device->frame_size = device->bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device->size = BKR_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		device->size -= BKR_BUFFER_SIZE % device->frame_size;

	return(0);
}


/*
 * bkr_device_start_transfer()
 *
 * Start the tape <---> memory data transfer.
 */

int bkr_device_start_transfer(bkr_device_t *device, bkr_state_t direction, jiffies_t bailout)
{
	unsigned long  flags;
	jiffies_t  short_bailout;

	device->state = direction;

	/*
	 * Set up the DMA channel.
	 */

	if(request_dma(device->hrdwr.isa.dma, BKR_NAME) < 0)
		{
		device->state = BKR_STOPPED;
		return(-EBUSY);
		}

	device->head = 0;
	device->tail = 0;
	memset(device->buffer, 0, BKR_BUFFER_SIZE);

	flags = claim_dma_lock();
	disable_dma(device->hrdwr.isa.dma);
	clear_dma_ff(device->hrdwr.isa.dma);
	set_dma_mode(device->hrdwr.isa.dma, (direction == BKR_WRITING) ? DMA_MEM_TO_IO : DMA_IO_TO_MEM);
	set_dma_addr(device->hrdwr.isa.dma, device->hrdwr.isa.phys_addr);
	set_dma_count(device->hrdwr.isa.dma, device->size);
	enable_dma(device->hrdwr.isa.dma);
	release_dma_lock(flags);

	/*
	 * Work the card's control bits.
	 */

	outb(device->hrdwr.isa.control, device->hrdwr.isa.ioport);
	device->hrdwr.isa.control |= (direction == BKR_WRITING) ? BIT_TRANSMIT : BIT_RECEIVE;
	outb(device->hrdwr.isa.control, device->hrdwr.isa.ioport);

	/*
	 * Do we see a heart beat on the DREQ line?
	 */

	short_bailout = jiffies + HZ/MIN_SYNC_FREQ;
	while(get_dreq_status(device->hrdwr.isa.dma) == 0)
		if(jiffies >= short_bailout)
			{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/MIN_UPDATE_FREQ);
			if(jiffies >= bailout)
				{
				bkr_device_stop_transfer(device);
				return(-EIO);
				}
			if(signal_pending(current))
				{
				bkr_device_stop_transfer(device);
				return(-EINTR);
				}
			short_bailout = jiffies + HZ/MIN_SYNC_FREQ;
			}

	/*
	 * Force a buffer update on the first bkr_device_read/write().
	 */

	device->hrdwr.isa.last_update = jiffies - HZ/MIN_UPDATE_FREQ;

	return(0);
}


/*
 * bkr_device_stop_transfer()
 *
 * Stop the tape <---> memory transfer.  The DMA channel is left fully
 * configured so that if a bug in someone else's code starts a transfer
 * without setting up a proper buffer then at least it will occur in a safe
 * region of memory (our buffer).
 */

void bkr_device_stop_transfer(bkr_device_t *device)
{
	outb(0, device->hrdwr.isa.ioport);

	device->hrdwr.isa.control &= ~(BIT_TRANSMIT | BIT_RECEIVE);
	device->state = BKR_STOPPED;

	free_dma(device->hrdwr.isa.dma);
	return;
}


/*
 * bkr_device_read()
 *
 * Doesn't actually "read" data... just makes sure the requested length of
 * data starting at device.tail is available in the DMA buffer.  Returns 0
 * on success, -EAGAIN if the data isn't available or any error code
 * returned by update_dma_offset().
 */

int bkr_device_read(bkr_device_t *device, unsigned int length)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device->hrdwr.isa.last_update < HZ/MIN_UPDATE_FREQ) &&
	   (bytes_in_buffer(device) >= length))
		return(0);

	result = update_dma_offset(device, &device->head);

	if(((bytes_in_buffer(device) >= length) &&
	    (space_in_buffer(device) > DMA_HOLD_OFF)) || result)
		return(result);

	return(-EAGAIN);
}


/*
 * bkr_device_write()
 *
 * Doesn't actually "write" data... just makes sure the requested amount of
 * space is available starting at the device buffer head.  Returns 0 on
 * success, -EAGAIN if the space isn't available or or any error code
 * returned by update_dma_offset().
 */

int bkr_device_write(bkr_device_t *device, unsigned int length)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device->hrdwr.isa.last_update < HZ/MIN_UPDATE_FREQ) &&
	   (space_in_buffer(device) >= length))
		return(0);

	result = update_dma_offset(device, &device->tail);

	if(((space_in_buffer(device) >= length) &&
	    (bytes_in_buffer(device) > DMA_HOLD_OFF)) || result)
		return(result);

	return(-EAGAIN);
}


/*
 * bkr_device_flush()
 *
 * Pad the data stream to the next full video frame then check that the
 * contents of the DMA buffer have been written to tape.  Returns 0 when
 * the data has been flushed, -EAGAIN if the data cannot be
 * immediately flushed or any error code returned by update_dma_offset() or
 * bkr_device_write().
 */

int bkr_device_flush(bkr_device_t *device)
{
	int  count;
	int  result = 0;

	count = INTERNAL_BUFFER - device->head % device->frame_size;
	if(count != 0)
		{
		if(count < 0)
			count += device->frame_size;
		result = bkr_device_write(device, count);
		if(result < 0)
			return(result);
		if(device->head + count >= device->size)
			{
			memset(device->buffer + device->head, 0, device->size - device->head);
			count -= device->size - device->head;
			device->head = 0;
			}
		memset(device->buffer + device->head, 0, count);
		device->head += count;
		}

#ifdef WORKAROUND
	while((bytes_in_buffer(device) >= device->bytes_per_line) && !result)
#else
	while(bytes_in_buffer(device) && !result)
#endif
		{
		result = update_dma_offset(device, &device->tail);
		if(bytes_in_buffer(device) >= device->frame_size)
			result = -EAGAIN;
		}

	return(result);
}


/*
 * bkr_isa_probe()
 *
 * Autoprobe for ISA Backer cards if the first start elements of the device
 * array are already initialized.  Returns the number of cards found (the
 * number of additional elements initialized).  The total number of devices
 * will not be allowed to excede BKR_MAX_DEVICES.
 */

static int __init bkr_isa_probe(bkr_device_t *device, int start)
{
	unsigned long  flags;
	jiffies_t  bailout;
	unsigned int  i, found;
	unsigned int  ioport;
	unsigned int  dma[2], num_dma = 0;
	unsigned int  count = start;

	/*
	 * Grab and disable DMA channels
	 */

	if(request_dma(1, BKR_NAME) >= 0)
		dma[num_dma++] = 1;
	if(request_dma(3, BKR_NAME) >= 0)
		dma[num_dma++] = 3;

	flags = claim_dma_lock();
	for(i = 0; i < num_dma; i++)
		disable_dma(dma[i]);
	release_dma_lock(flags);

	/*
	 * Try starting a transfer with each possible I/O port while
	 * watching for DREQ activity on DMA channels.
	 */

	for(ioport = 0x300; (ioport <= 0x33c) && (count < BKR_MAX_DEVICES); ioport += 4)
		{
		if(request_region(ioport, 1, BKR_NAME) < 0)
			continue;
		outb(BIT_DMA_REQUEST, ioport);
		outb(BIT_DMA_REQUEST | BIT_TRANSMIT, ioport);
		for(i = found = 0; (i < num_dma) && !found; i++)
			for(bailout = jiffies + HZ/MIN_SYNC_FREQ; jiffies < bailout; )
				if((found = get_dreq_status(dma[i])) != 0)
					{
					outb(0, ioport);
					device[count].hrdwr.type = BKR_ISA_DEVICE;
					device[count].hrdwr.isa.ioport = ioport;
					device[count].hrdwr.isa.dma = dma[i];
					printk(KERN_INFO BKR_NAME ": unit%u: found at dma=%u ioport=%#x\n", count, dma[i], ioport);
					count++;
					break;
					}
		if(!found)
			release_region(ioport, 1);
		}

	for(i = 0; i < num_dma; i++)
		free_dma(dma[i]);

	return(count - start);
}
