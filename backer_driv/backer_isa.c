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
 * Parameters and constants
 */

#define  BKR_NAME               "backer"
#define  BKR_VERSION            "3.0"
#define  BKR_MINOR_BASE         0       /* first minor number */
#define  BKR_PROC_NAME          "driver/"BKR_NAME

#define  MAX_RETRY_FREQ         50      /* max blocking retry rate in Hz */
#define  BKR_MAX_TIMEOUT        30      /* seconds */

#if (MAX_UPDATE_FREQ > HZ)
#error "MAX_UPDATE_FREQ is too high"
#endif


/*
 * Module info and options.
 */

MODULE_AUTHOR("Kipp Cannon");
MODULE_DESCRIPTION("Backer 16 & 32 device driver");
MODULE_SUPPORTED_DEVICE("backer");

static unsigned int ioport __initdata = CONFIG_BACKER_IOPORT;
static unsigned int dma __initdata    = CONFIG_BACKER_DMA;
static unsigned int timeout           = CONFIG_BACKER_TIMEOUT;

MODULE_PARM(ioport, "i");
MODULE_PARM_DESC(ioport, "I/O port");
MODULE_PARM(dma, "i");
MODULE_PARM_DESC(dma, "DMA channel");
MODULE_PARM(timeout, "i");
MODULE_PARM_DESC(timeout, "Timeout (seconds)");


/*
 * Function prototypes
 */

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
 * Types
 */

typedef struct
	{
	int  mode;                      /* mode for this descriptor */
	int  last_error;                /* Pending error code if != 0 */
	} bkr_private_t;

#define  PRIVATE_INITIALIZER  ((bkr_private_t) { 0, 0 })


/*
 * Global Data
 */

static struct
	{
	jiffies_t  last_update;         /* jiffies at time of last update */
	unsigned char  control;         /* control byte for card */
	unsigned int  ioport;           /* I/O port */
	unsigned int  dma;              /* DMA channel number */
	dma_addr_t  phys_addr;          /* DMA buffer's bus address */
	} device_isa;                   /* device layer private data */

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
	STOPPED_OPS,                    /* order must match bkr_state_t */
	READING_OPS,
	WRITING_OPS
	};

typedef struct
	{
	int mode;
	char name[4];
	} bkr_modes_t;

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

static devfs_handle_t  dev_directory = NULL;    /* root of device tree */
static devfs_handle_t  dev_entry[NUM_MODES];    /* devfs handles */
static struct proc_dir_entry  *proc_entry = NULL;	/* proc handle */
static struct pm_dev  *bkr_pm_dev = NULL;       /* power management handle */
static spinlock_t  bkr_state_lock;              /* locking for SMP machines */


/*
 * Macros
 */

#define PRIVATE_PTR(filp)  ((bkr_private_t *) (filp)->private_data)



/*
 * ================================================================================================
 *
 *                                       ENTRY/EXIT CODE
 *
 * ================================================================================================
 */

int __init init_module(void)
{
	int  i, result;
	char  name[6];

	EXPORT_NO_SYMBOLS;
	printk(KERN_INFO BKR_NAME ": Backer 16/32 tape device driver version " BKR_VERSION ": ");

	/*
	 * Initialize some data.
	 */

	device_isa.ioport = ioport;
	device_isa.dma = dma;

	if(timeout > BKR_MAX_TIMEOUT)
		timeout = BKR_MAX_TIMEOUT;
	timeout *= HZ;

	device.state = STOPPED;
	sector.buffer = NULL;

	memset(dev_entry, 0, NUM_MODES * sizeof(devfs_handle_t));

	spin_lock_init(&bkr_state_lock);

	/*
	 * Grab resources and register device.
	 */

	device.buffer = (unsigned char *) pci_alloc_consistent(NULL, BKR_BUFFER_SIZE, &device_isa.phys_addr);
	if(device.buffer == NULL)
		{
		printk("can't get %u byte DMA buffer\n", BKR_BUFFER_SIZE);
		result = -ENOMEM;
		goto no_dmamem;
		}
	if(request_region(device_isa.ioport, 1, BKR_NAME) < 0)
		{
		printk("I/O port %#x not available\n", device_isa.ioport);
		result = -EBUSY;
		goto ioport_busy;
		}
	dev_directory = devfs_mk_dir(NULL, BKR_NAME, NULL);
	if(dev_directory == NULL)
		{
		printk("can't register device\n");
		result = -ENODEV;
		goto cant_register_chrdev;
		}
	for(i = 0; i < NUM_MODES; i++)
		{
		if(bkr_modes[i].mode < 0)
			continue;
		sprintf(name, "%u/%s", 0, bkr_modes[i].name);
		dev_entry[i] = devfs_register(dev_directory, name, DEVFS_FL_DEFAULT, BKR_MAJOR,
		                 BKR_MINOR_BASE + i, S_IFCHR | S_IRUGO | S_IWUGO,
		                 &file_ops[STOPPED], NULL);
		if(dev_entry[i] == NULL)
			{
			printk("can't register device\n");
			result = -ENODEV;
			goto cant_register_chrdev;
			}
		}
	result = devfs_register_chrdev(BKR_MAJOR, BKR_NAME, &file_ops[STOPPED]);
	if(result < 0)
		{
		printk("can't register device\n");
		goto cant_register_chrdev;
		}
	proc_entry = create_proc_read_entry(BKR_PROC_NAME, 0, 0, bkr_read_proc, NULL);
	bkr_pm_dev = pm_register(PM_ISA_DEV, PM_SYS_UNKNOWN, bkr_pm_callback);

	/*
	 * Driver installed.
	 */

	printk("OK\n" KERN_INFO BKR_NAME ": dma=%u ioport=%#x timeout=%u\n",
	       device_isa.dma, device_isa.ioport, timeout/HZ);
	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_register_chrdev:
		for(i = 0; i < NUM_MODES; i++)
			devfs_unregister(dev_entry[i]);
		devfs_unregister(dev_directory);
		release_region(ioport, 1);
	ioport_busy:
		pci_free_consistent(NULL, BKR_BUFFER_SIZE, device.buffer, device_isa.phys_addr);
	no_dmamem:

	return(result);
}

void __exit cleanup_module(void)
{
	int i;

	kfree(sector.buffer);
	pm_unregister_all(bkr_pm_callback);
	remove_proc_entry(BKR_PROC_NAME, NULL);
	devfs_unregister_chrdev(BKR_MAJOR, BKR_NAME);
	for(i = 0; i < NUM_MODES; i++)
		devfs_unregister(dev_entry[i]);
	devfs_unregister(dev_directory);
	release_region(ioport, 1);
	pci_free_consistent(NULL, BKR_BUFFER_SIZE, device.buffer, device_isa.phys_addr);
}


/*
 * ================================================================================================
 *
 *                                       FILE OPERATIONS
 *
 * ================================================================================================
 */

/*
 * open() --- Arbitrate device open requests.
 *
 * Each file can be opened any number of times but only by one user at a
 * time (unless by a super user).  Access to the data stream is only
 * granted once on a first-come first-serve basis across all files.  A data
 * transfer is started when the first call to a read() or write() method is
 * made after which all read/write attempts from other files fail.
 */

static int open(struct inode *inode, struct file *filp)
{
	kdev_t  minor;

	/*
	printk(KERN_INFO "Backer: %s\n", filp->f_dentry->d_iname);
	*/

	minor = MINOR(inode->i_rdev) - BKR_MINOR_BASE;
	if(minor >= NUM_MODES)
		return(-ENODEV);
	if(bkr_modes[minor].mode < 0)
		return(-ENODEV);

	filp->private_data = kmalloc(sizeof(bkr_private_t), GFP_KERNEL);
	if(filp->private_data == NULL)
		return(-ENOMEM);

	MOD_INC_USE_COUNT;

	*PRIVATE_PTR(filp) = PRIVATE_INITIALIZER;
	PRIVATE_PTR(filp)->mode = bkr_modes[minor].mode;

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
	ssize_t  result;
	jiffies_t  bailout;

	if(device.state == WRITING)
		{
		bailout = jiffies + timeout;

		if(BKR_FORMAT(PRIVATE_PTR(filp)->mode) != BKR_RAW)
			while(1)
				{
				result = bkr_sector_write_eor();
				if(result != -EAGAIN)
					break;
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ/MAX_RETRY_FREQ);
				if(jiffies >= bailout)
					{
					result = -ETIMEDOUT;
					break;
					}
				}

		while(1)
			{
			result = bkr_device_flush();
			if(result != -EAGAIN)
				break;
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/MAX_RETRY_FREQ);
			if(jiffies >= bailout)
				{
				result = -ETIMEDOUT;
				break;
				}
			}
		}

	bkr_device_stop_transfer();
	pm_dev_idle(bkr_pm_dev);

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
 * mtio.h ioctl's as make sense for this device.  Any of our own features
 * that don't fit into the mtio stuff are implemented as new calls.
 */

static int ioctl(struct inode *inode, struct file *filp, unsigned int op, unsigned long argument)
{
	int result;
	union
		{
		struct mtop  mtop;
		struct mtget  mtget;
		struct mtpos  mtpos;
		struct bkrformat  bkrformat;
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
			if((arg.mtop.mt_count & MT_ST_OPTIONS) == MT_ST_TIMEOUTS)
				{
				result = arg.mtop.mt_count & ~MT_ST_OPTIONS;
				if(result <= BKR_MAX_TIMEOUT)
					{
					timeout = result*HZ;
					return(0);
					}
				}
			return(-EINVAL);

			default:
			return(-EINVAL);
			}

		case MTIOCGET:
		arg.mtget.mt_type = MT_ISUNKNOWN;
		arg.mtget.mt_resid = sector.end - sector.offset;
		arg.mtget.mt_dsreg = PRIVATE_PTR(filp)->mode;
		arg.mtget.mt_gstat = GMT_ONLINE(-1L);
		arg.mtget.mt_erreg = health.total_errors;
		arg.mtget.mt_fileno = 0;
		arg.mtget.mt_blkno = sector.header.number;
		copy_to_user((void *) argument, &arg.mtget, sizeof(struct mtget));
		return(0);

		case MTIOCPOS:
		arg.mtpos.mt_blkno = sector.header.number;
		copy_to_user((void *) argument, &arg.mtpos, sizeof(struct mtpos));
		return(0);

		case BKRIOCGETFORMAT:
		if(device.state == STOPPED)
			{
			bkr_device_reset(PRIVATE_PTR(filp)->mode, STOPPED);
			bkr_format_reset(PRIVATE_PTR(filp)->mode, STOPPED);
			}
		arg.bkrformat.buffer_size = device.size;
		arg.bkrformat.video_size = sector.video_size;
		arg.bkrformat.interleave = sector.interleave;
		arg.bkrformat.block_size = sector.rs_format.n;
		arg.bkrformat.block_parity = sector.rs_format.parity;
		arg.bkrformat.sector_capacity = sector.data_size - sizeof(bkr_sector_header_t);
		copy_to_user((void *) argument, &arg.bkrformat, sizeof(struct bkrformat));
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

	spin_lock(&bkr_state_lock);

	if(device.state != STOPPED)
		{
		result = -EBUSY;
		goto done;
		}

	pm_access(bkr_pm_dev);
	bkr_device_reset(PRIVATE_PTR(filp)->mode, direction);
	if((result = bkr_format_reset(PRIVATE_PTR(filp)->mode, direction)) >= 0)
		if((result = bkr_device_start_transfer(direction, jiffies + timeout)) >= 0)
			{
			filp->f_op = &file_ops[direction];
			goto done;
			}
	pm_dev_idle(bkr_pm_dev);

	done:
	spin_unlock(&bkr_state_lock);
	return(result);
}

static ssize_t start_read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(filp, READING);
	if(result >= 0)
		result = filp->f_op->read(filp, buff, count, posp);

	return(result);
}

static ssize_t start_write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(filp, WRITING);
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

	filp->f_op = &file_ops[STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

	bailout = jiffies + timeout;
	while(1)
		{
		chunk_size = sector.end - sector.offset;
		if(chunk_size > count)
			chunk_size = count;

		copy_to_user(buff, sector.offset, chunk_size);

		sector.offset += chunk_size;
		buff += chunk_size;
		moved += chunk_size;
		count -= chunk_size;

		if(count == 0)
			break;

		result = sector.read();
		if(result == 0)
			continue;
		if(result > 0)
			{
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
	filp->f_op = &file_ops[READING];
	return(result);
}

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result = 0;
	unsigned int  moved = 0;
	unsigned int  chunk_size;
	jiffies_t  bailout;

	filp->f_op = &file_ops[STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

	bailout = jiffies + timeout;
	while(1)
		{
		chunk_size = sector.end - sector.offset;
		if(chunk_size > count)
			chunk_size = count;

		copy_from_user(sector.offset, buff, chunk_size);

		sector.offset += chunk_size;
		buff += chunk_size;
		moved += chunk_size;
		count -= chunk_size;

		if(count == 0)
			break;

		result = sector.write();
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
	filp->f_op = &file_ops[WRITING];
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
 * ================================================================================================
 *
 *                                          /PROC I/O
 *
 * ================================================================================================
 */

static int bkr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char  *pos;

	/*
	 * Sync the buffer head/tail
	 */

	switch(device.state)
		{
		case STOPPED:
		break;

		case READING:
		bkr_device_read(0);
		break;

		case WRITING:
		bkr_device_write(0);
		break;

		case SUSPENDED:
		*eof = 1;
		return(0);
		}

	/*
	 * Output current status
	 * If you change this, remember to update bkrmonitor!
	 */

	pos = page;
	pos += sprintf(pos, "Operating Mode:          %u\n"
	                    "Sector Number:           %lu\n"
	                    "Total Errors Corrected:  %u\n"
	                    "Errors in Worst Block:   %u / %u\n"
	                    "Recent Worst Block:      %u\n"
	                    "Uncorrectable Blocks:    %u\n"
	                    "Framing Errors:          %u\n"
	                    "Overrun Errors:          %u\n"
	                    "Underflows Detected:     %u\n"
	                    "Worst Key:               %u\n"
	                    "Closest Non-Key:         %u\n"
	                    "Least Skipped:           %u\n"
	                    "Most Skipped:            %u\n"
	                    "DMA Buffer:              %u / %u\n",
	                    sector.mode,
	                    sector.header.number,
	                    health.total_errors,
	                    errors.symbol, sector.rs_format.parity,
	                    errors.recent_symbol,
	                    errors.block,
	                    errors.frame,
	                    errors.overrun,
	                    errors.underflow,
	                    health.worst_key,
	                    health.best_nonkey,
	                    health.least_skipped,
	                    health.most_skipped,
		            bytes_in_buffer(), device.size);
	errors.recent_symbol = 0;

	*start = page + off;

	if(pos > *start)
		return(pos - *start);
	*eof = 1;
	return(0);
}


/*
 * ================================================================================================
 *
 *                                       POWER MANAGEMENT
 *
 * ================================================================================================
 */

static int  bkr_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	spin_lock(&bkr_state_lock);
	switch(rqst)
		{
		case PM_SUSPEND:
		if(device.state != STOPPED)
			return(-EBUSY);
		device.state = SUSPENDED;
		break;

		case PM_RESUME:
		device.state = STOPPED;
		break;

		default:
		break;
		}
	spin_unlock(&bkr_state_lock);

	return(0);
}


/*
 * ================================================================================================
 *
 *                                       DEVICE-LEVEL I/O
 *
 * ================================================================================================
 */

/*
 * Local constants
 */

#define  DMA_IO_TO_MEM          0x14    /* dmand transf, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x18    /* dmand transf, inc addr, auto-init */
#define  DMA_HOLD_OFF           786     /* min abs(head-tail) */

#define  MIN_UPDATE_FREQ        3       /* min DMA update rate in Hz */
#define  MIN_SYNC_FREQ          50      /* min sync frequency in Hz */

#if (MIN_UPDATE_FREQ > HZ) || (MIN_SYNC_FREQ > HZ)
#error "One of the *_FREQ parameters is too high"
#endif


/*
 * Local function declarations
 */

static int  update_dma_offset(bkr_offset_t *);
static __inline__ int  get_dreq_status(unsigned int);


/*
 * bkr_device_reset()
 *
 * Resets the Backer hardware and device I/O layer.  Always succeeds but
 * assumes the mode is valid.
 */

int bkr_device_reset(int mode, bkr_state_t direction)
{
	outb(0, ioport);

	device_isa.control = BIT_DMA_REQUEST;

	if(BKR_DENSITY(mode) == BKR_HIGH)
		{
		device_isa.control |= BIT_HIGH_DENSITY;
		device.bytes_per_line = BYTES_PER_LINE_HIGH;
		}
	else
		device.bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		{
		device_isa.control |= BIT_NTSC_VIDEO;
		device.frame_size = device.bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
		}
	else
		device.frame_size = device.bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device.size = BKR_BUFFER_SIZE;
	if(direction == WRITING)
		device.size -= BKR_BUFFER_SIZE % device.frame_size;

	return(0);
}


/*
 * bkr_device_start_transfer()
 *
 * Start the tape <---> memory data transfer.
 */

int bkr_device_start_transfer(bkr_state_t direction, jiffies_t bailout)
{
	unsigned long  flags;
	jiffies_t  short_bailout;

	device.state = direction;

	/*
	 * Set up the DMA channel.
	 */

	if(request_dma(device_isa.dma, BKR_NAME) < 0)
		{
		device.state = STOPPED;
		return(-EBUSY);
		}

	device.head = 0;
	device.tail = 0;
	memset(device.buffer, 0, BKR_BUFFER_SIZE);

	flags = claim_dma_lock();
	disable_dma(device_isa.dma);
	clear_dma_ff(device_isa.dma);
	set_dma_mode(device_isa.dma, (direction == WRITING) ? DMA_MEM_TO_IO : DMA_IO_TO_MEM);
	set_dma_addr(device_isa.dma, device_isa.phys_addr);
	set_dma_count(device_isa.dma, device.size);
	enable_dma(device_isa.dma);
	release_dma_lock(flags);

	/*
	 * Work the card's control bits.
	 */

	outb(device_isa.control, ioport);
	device_isa.control |= (direction == WRITING) ? BIT_TRANSMIT : BIT_RECEIVE;
	outb(device_isa.control, ioport);

	/*
	 * Do we see a heart beat on the DREQ line?
	 */

	short_bailout = jiffies + HZ/MIN_SYNC_FREQ;
	while(get_dreq_status(device_isa.dma) == 0)
		if(jiffies >= short_bailout)
			{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/MIN_UPDATE_FREQ);
			if(jiffies >= bailout)
				{
				bkr_device_stop_transfer();
				return(-EIO);
				}
			if(signal_pending(current))
				{
				bkr_device_stop_transfer();
				return(-EINTR);
				}
			short_bailout = jiffies + HZ/MIN_SYNC_FREQ;
			}

	/*
	 * Force a buffer update on the first bkr_device_read/write().
	 */

	device_isa.last_update = jiffies - HZ/MIN_UPDATE_FREQ;

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

void bkr_device_stop_transfer(void)
{
	outb(0, ioport);

	device_isa.control &= ~(BIT_TRANSMIT | BIT_RECEIVE);
	device.state = STOPPED;

	free_dma(device_isa.dma);
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

int bkr_device_read(unsigned int length)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device_isa.last_update < HZ/MIN_UPDATE_FREQ) && (bytes_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.head);

	if(((bytes_in_buffer() >= length) && (space_in_buffer() > DMA_HOLD_OFF)) || result)
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

int bkr_device_write(unsigned int length)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device_isa.last_update < HZ/MIN_UPDATE_FREQ) && (space_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.tail);

	if(((space_in_buffer() >= length) && (bytes_in_buffer() > DMA_HOLD_OFF)) || result)
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

int bkr_device_flush(void)
{
	int  count;
	int  result = 0;

	count = INTERNAL_BUFFER - device.head % device.frame_size;
	if(count != 0)
		{
		if(count < 0)
			count += device.frame_size;
		result = bkr_device_write(count);
		if(result >= 0)
			{
			memset(device.buffer + device.head, 0, count);
			device.head += count;
			if(device.head == device.size)
				device.head = 0;
			}
		}

#ifdef WORKAROUND
	while((bytes_in_buffer() >= device.bytes_per_line) && !result)
#else
	while(bytes_in_buffer() && !result)
#endif
		{
		result = update_dma_offset(&device.tail);
		if(bytes_in_buffer() >= device.frame_size)
			result = -EAGAIN;
		}

	return(result);
}


/*
 * update_dma_offset()
 *
 * Retrieves the offset within the DMA buffer at which the next transfer
 * will occur.  During read operations this is the buffer's head; during
 * write operations it is the tail.  Returns 0 on success, -EIO if no DMA
 * activity is detected.
 */

static int update_dma_offset(bkr_offset_t *offset)
{
	unsigned long  flags;
	jiffies_t  dma_bailout;

	dma_bailout = jiffies + HZ/MIN_SYNC_FREQ;

	while(~inb(ioport) & BIT_DMA_REQUEST)
		 if(jiffies >= dma_bailout)
			return(-EIO);

	while(inb(ioport) & BIT_DMA_REQUEST)
		if(jiffies >= dma_bailout)
			return(-EIO);

	flags = claim_dma_lock();
	clear_dma_ff(device_isa.dma);
	*offset = device.size - get_dma_residue(device_isa.dma);
	release_dma_lock(flags);

	device_isa.last_update = jiffies;

	return(0);
}


/*
 * get_dreq_status()
 *
 * Return the status of a DMA channel's DREQ line.  1 == active, 0 ==
 * inactive.
 */

static __inline__ int get_dreq_status(unsigned int dmanr)
{
	int  result;

	if(dmanr <= 3)
		result = dma_inb(DMA1_STAT_REG) >> 4;
	else
		result = dma_inb(DMA2_STAT_REG);

	return((result >> dmanr) & 1);
}
