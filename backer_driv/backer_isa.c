/*
 * backer_isa
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
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

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/mtio.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"


/*
 * Config
 *
 * If I decide to submit this thing to the kernel source tree, these are
 * the options that will be moved to the build scripts.  In the meantime,
 * adjust them by hand here.  The timeout is in seconds.
 */

#define  CONFIG_BACKER_IOPORT   0x300
#define  CONFIG_BACKER_DMA      3
#define  CONFIG_BACKER_TIMEOUT  10      /* seconds */


/*
 * Parameters and constants
 *
 * The fall-back mode MUST be valid!
 */

#define  BKR_NAME               "backer"
#define  BKR_VERSION            "1.104"
#define  BKR_BUFFER_SIZE        65500   /* bytes */

#define  BKR_FALLBACK_MODE      (BKR_FMT | BKR_SP | BKR_LOW  | BKR_NTSC)
#define  BKR_MAX_TIMEOUT        120

#define  DMA_IO_TO_MEM          0x14    /* demand transfer, inc addr, auto-init */
#define  DMA_MEM_TO_IO          0x18    /* demand transfer, inc addr, auto-init */
#define  DMA_HOLD_OFF           512     /* stay this far back from transfer point */

#define  MIN_UPDATE_FREQ        3       /* minimum DMA status update rate in Hz */
#define  MAX_UPDATE_FREQ        50      /* maximum DMA status update rate in Hz */
#define  MIN_SYNC_FREQ          50      /* minimum sync frequency in Hz */

#if (MIN_UPDATE_FREQ > HZ) || (MAX_UPDATE_FREQ > HZ) || (MIN_SYNC_FREQ > HZ)
#error "One of the *_FREQ parameters is too high"
#endif


/*
 * Function prototypes
 */

static int     open(struct inode *, struct file *);
static int     release(struct inode *, struct file *);
static int     stop_release(struct inode *, struct file *);
static ssize_t start_common(int, direction_t);
static ssize_t start_read(struct file *, char *, size_t, loff_t *);
static ssize_t start_write(struct file *, const char *, size_t, loff_t *);
static ssize_t read(struct file *, char *, size_t, loff_t *);
static ssize_t write(struct file *, const char *, size_t, loff_t *);
static int     ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static loff_t  llseek(struct file *, loff_t, int);
static int     update_dma_offset(unsigned int *);
static int     get_dreq_status(unsigned int);


/*
 * Module info and options.
 */

MODULE_AUTHOR("Kipp Cannon");
MODULE_DESCRIPTION("Backer 16 & 32 tape device driver");
MODULE_SUPPORTED_DEVICE("backer");

static unsigned int ioport  = CONFIG_BACKER_IOPORT;
static unsigned int dma     = CONFIG_BACKER_DMA;
static unsigned int buffer  = BKR_BUFFER_SIZE;
static unsigned int timeout = CONFIG_BACKER_TIMEOUT;

MODULE_PARM(ioport, "i");
MODULE_PARM_DESC(ioport, "I/O port");
MODULE_PARM(dma, "i");
MODULE_PARM_DESC(dma, "DMA channel");
MODULE_PARM(buffer, "i");
MODULE_PARM_DESC(buffer, "DMA buffer size");
MODULE_PARM(timeout, "i");
MODULE_PARM_DESC(timeout, "Timeout (seconds)");


/*
 * Types
 */

typedef struct
	{
	int  mode;                      /* mode for this device */
	int  last_error;                /* Pending error code if != 0 */
	} bkr_private_t;

#define  PRIVATE_INITIALIZER  ((bkr_private_t) { BKR_FALLBACK_MODE, 0 })


/*
 * Global Data
 *
 * for file_operations the funcs are:
 * { owner, llseek, read, write, readdir, poll,
 *   ioctl, mmap, open, flush, release }
 */

static unsigned int  owner;                     /* owner's user id */
static unsigned int  open_count = 0;            /* count of open's */
static struct
	{
	jiffies_t  last_update;                 /* jiffies at time of last update */
	unsigned char  control;                 /* control byte for card */
	dma_addr_t  phys_addr;
	} device_isa;                           /* device layer private data */

#define STOPPED_OPS                                                  \
	{ THIS_MODULE, llseek, start_read, start_write, NULL, NULL,  \
	  ioctl, NULL, open, NULL,      release, }

#define READING_OPS                                                  \
	{ THIS_MODULE, llseek,       read, start_write, NULL, NULL,  \
	  ioctl, NULL, open, NULL, stop_release, }

#define WRITING_OPS                                                  \
	{ THIS_MODULE, llseek, start_read,       write, NULL, NULL,  \
	  ioctl, NULL, open, NULL, stop_release, }

static struct file_operations file_ops[] =      /* file I/O functions */
	{
	STOPPED_OPS,                            /* for order see direction_t */
	READING_OPS,
	WRITING_OPS
	};


/*
 * Macros
 */

#define PRIVATE_PTR(filp)  ((bkr_private_t *) (filp)->private_data)



/*
 * ================================================================================================
 *
 *                                       KERNEL INTERFACE
 *
 * ================================================================================================
 */

/*
 * init_module(), cleanup_module()
 *
 * Module start-up and shut-down code.
 */

int __init init_module(void)
{
	int  result;

	EXPORT_NO_SYMBOLS;
	printk(KERN_INFO BKR_NAME ": Backer 16/32 driver version " BKR_VERSION "\n");

	/*
	 * Initialize some data.
	 */

	device.direction = STOPPED;
	device.alloc_size = buffer;
	sector.buffer = NULL;

	if(timeout > BKR_MAX_TIMEOUT)
		timeout = BKR_MAX_TIMEOUT;
	timeout *= HZ;

	/*
	 * Grab resources and register device.
	 */

	if((device.buffer = (unsigned char *) pci_alloc_consistent(NULL, device.alloc_size, &device_isa.phys_addr)) == NULL)
		{
		printk(KERN_ERR BKR_NAME ":   can't get %u byte DMA buffer", device.alloc_size);
		result = -ENOMEM;
		goto no_dmamem;
		}
	if(request_region(ioport, 1, BKR_NAME) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   I/O port %#x in use", ioport);
		result = -EBUSY;
		goto ioport_busy;
		}
	if((result = register_chrdev(BKR_MAJOR, BKR_NAME, &file_ops[STOPPED])) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   can't get device major %u", BKR_MAJOR);
		goto cant_register;
		}

	/*
	 * Driver installed.
	 */

	printk(KERN_INFO BKR_NAME ": dma=%u ioport=%#x buffer=%u timeout=%u\n",
	       dma, ioport, device.alloc_size, timeout/HZ);
	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_register:
		release_region(ioport, 1);
	ioport_busy:
		pci_free_consistent(NULL, device.alloc_size, device.buffer, device_isa.phys_addr);
	no_dmamem:
		printk(" --- driver not loaded\n");

	return(result);
}

void cleanup_module(void)
{
	kfree(sector.buffer);
	release_region(ioport, 1);
	pci_free_consistent(NULL, device.alloc_size, device.buffer, device_isa.phys_addr);
	unregister_chrdev(BKR_MAJOR, BKR_NAME);
}


/*
 * open() --- Arbitrate device open requests.
 *
 * The device can be opened any number of times but only by one user at a
 * time (unless by a super user) and access to the data stream is only
 * granted once on a first-come first-serve basis.  A data transfer is
 * started when the first call to a read() or write() method is made after
 * which all read/write attempts from other file descriptors fail.
 */

static int open(struct inode *inode, struct file *filp)
{
	kdev_t  minor;
	int  minor_to_mode[] =
		{
		BKR_FMT | BKR_SP | BKR_LOW  | BKR_NTSC,
		BKR_FMT | BKR_SP | BKR_LOW  | BKR_PAL,
		BKR_FMT | BKR_SP | BKR_HIGH | BKR_NTSC,
		BKR_FMT | BKR_SP | BKR_HIGH | BKR_PAL,
		BKR_FMT | BKR_EP | BKR_LOW  | BKR_NTSC,
		BKR_FMT | BKR_EP | BKR_LOW  | BKR_PAL,
		BKR_FMT | BKR_EP | BKR_HIGH | BKR_NTSC,
		BKR_FMT | BKR_EP | BKR_HIGH | BKR_PAL,
		BKR_RAW | BKR_SP | BKR_LOW  | BKR_NTSC,
		BKR_RAW | BKR_SP | BKR_LOW  | BKR_PAL,
		BKR_RAW | BKR_SP | BKR_HIGH | BKR_NTSC,
		BKR_RAW | BKR_SP | BKR_HIGH | BKR_PAL
		};

	if(open_count == 0)
		owner = current->uid;
	else if((owner != current->uid) && (owner != current->euid) && !suser())
		return(-EBUSY);

	filp->private_data = kmalloc(sizeof(bkr_private_t), GFP_KERNEL);
	if(filp->private_data == NULL)
		return(-ENOMEM);

	MOD_INC_USE_COUNT;
	open_count++;

	*PRIVATE_PTR(filp) = PRIVATE_INITIALIZER;

	minor = MINOR(inode->i_rdev);
	if(minor < 12)
		PRIVATE_PTR(filp)->mode = minor_to_mode[minor];

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

	if(device.direction == WRITING)
		{
		bailout = jiffies + timeout;

		if(BKR_FORMAT(PRIVATE_PTR(filp)->mode) == BKR_FMT)
			bkr_write_eor(bailout);

		bkr_device_flush(bailout);
		}

	bkr_device_stop_transfer();

	return(release(inode, filp));
}

static int release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);

	open_count--;
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
		struct bkrstatus  bkrstatus;
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
		arg.mtget.mt_type = MT_ISUNKNOWN;        /* maybe we'll have our own one day... */
		arg.mtget.mt_resid = sector.end - sector.offset; /* residual count */
		arg.mtget.mt_dsreg = PRIVATE_PTR(filp)->mode;    /* device mode */
		arg.mtget.mt_gstat = GMT_ONLINE(-1L);    /* device-independant status */
		arg.mtget.mt_erreg = 0;                  /* not implemented */
		arg.mtget.mt_fileno = 0;                 /* not implemented */
		arg.mtget.mt_blkno = sector.header.state.parts.number;   /* sector number */
		copy_to_user((void *) argument, &arg.mtget, sizeof(struct mtget));
		return(0);

		case MTIOCPOS:
		arg.mtpos.mt_blkno = sector.header.state.parts.number;   /* sector number */
		copy_to_user((void *) argument, &arg.mtpos, sizeof(struct mtpos));
		return(0);

		case BKRIOCGETSTATUS:
		if(jiffies - device_isa.last_update >= HZ/MIN_UPDATE_FREQ)
			switch(device.direction)
				{
				case WRITING:
				update_dma_offset(&device.tail);
				break;

				case READING:
				update_dma_offset(&device.head);
				break;

				default:
				}
		arg.bkrstatus.bytes = bytes_in_buffer();
		arg.bkrstatus.errors = errors;
		arg.bkrstatus.health = health;
		copy_to_user((void *) argument, &arg.bkrstatus, sizeof(struct bkrstatus));
		errors.recent_symbol = 0;
		return(0);

		case BKRIOCGETFORMAT:
		if(device.direction == STOPPED)
			{
			bkr_device_reset(PRIVATE_PTR(filp)->mode);
			bkr_format_reset(PRIVATE_PTR(filp)->mode, STOPPED);
			}
		arg.bkrformat.buffer_size = device.size;
		arg.bkrformat.sector_size = sector.video_size;
		arg.bkrformat.leader = sector.leader;
		arg.bkrformat.trailer = sector.trailer;
		arg.bkrformat.interleave = sector.interleave;
		arg.bkrformat.block_size = sector.rs_format.n;
		arg.bkrformat.block_parity = sector.rs_format.parity;
		arg.bkrformat.sector_capacity = sector.data_size - sizeof(sector_header_t);
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

static ssize_t start_common(int mode, direction_t direction)
{
	ssize_t  result;

	if(device.direction != STOPPED)
		result = -EBUSY;
	else if((result = bkr_device_reset(mode)) < 0)
		{
		}
	else if((result = bkr_format_reset(mode, direction)) < 0)
		{
		}
	else
		result = bkr_device_start_transfer(direction);

	return(result);
}

static ssize_t start_read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(PRIVATE_PTR(filp)->mode, READING);
	if(result < 0)
		return(result);

	filp->f_op = &file_ops[READING];

	return(filp->f_op->read(filp, buff, count, posp));
}

static ssize_t start_write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;

	result = start_common(PRIVATE_PTR(filp)->mode, WRITING);
	if(result < 0)
		return(result);

	if(BKR_FORMAT(PRIVATE_PTR(filp)->mode) == BKR_FMT)
		{
		result = bkr_write_bor(jiffies + timeout);
		if(result < 0)
			{
			bkr_device_stop_transfer();
			return(result);
			}
		}

	filp->f_op = &file_ops[WRITING];

	return(filp->f_op->write(filp, buff, count, posp));
}


/*
 * read(), write()
 *
 * While inside these functions, I/O operations for the current file
 * descriptor are reset to their "stopped" state.  Hopefully this is
 * effective at elliminating re-entrance.
 */

static ssize_t read(struct file *filp, char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;
	jiffies_t  bailout;
	unsigned int  moved, chunk_size;

	filp->f_op = &file_ops[STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

	bailout = jiffies + timeout;
	for(moved = 0; count; buff += chunk_size, count -= chunk_size)
		{
		chunk_size = sector.end - sector.offset;
		if(chunk_size > count)
			chunk_size = count;

		copy_to_user(buff, sector.offset, chunk_size);

		moved += chunk_size;
		sector.offset += chunk_size;
		if(sector.offset == sector.end)
			{
			result = sector.read(filp->f_flags, bailout);
			if(result <= 0)
				{
				if(!moved)
					goto done;
				PRIVATE_PTR(filp)->last_error = result;
				break;
				}
			}
		}
	result = moved;

	done:
	filp->f_op = &file_ops[READING];
	return(result);
}

static ssize_t write(struct file *filp, const char *buff, size_t count, loff_t *posp)
{
	ssize_t  result;
	jiffies_t  bailout;
	unsigned int  moved, chunk_size;

	filp->f_op = &file_ops[STOPPED];

	if(PRIVATE_PTR(filp)->last_error != 0)
		{
		result = PRIVATE_PTR(filp)->last_error;
		PRIVATE_PTR(filp)->last_error = 0;
		goto done;
		}

	bailout = jiffies + timeout;
	for(moved = 0; count; buff += chunk_size, count -= chunk_size)
		{
		chunk_size = sector.end - sector.offset;
		if(chunk_size > count)
			chunk_size = count;

		copy_from_user(sector.offset, buff, chunk_size);

		moved += chunk_size;
		sector.offset += chunk_size;
		if(sector.offset == sector.end)
			{
			result = sector.write(filp->f_flags, bailout);
			if(result < 0)
				{
				if(!moved)
					goto done;
				PRIVATE_PTR(filp)->last_error = result;
				break;
				}
			}
		}
	result = moved;

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
 *                                       DEVICE-LEVEL I/O
 *
 * ================================================================================================
 */

/*
 * bkr_device_reset()
 *
 * Resets the Backer hardware and device I/O layer.  The return code
 * indicates success or failure.  On failure, device.size is left = 0 so
 * this can also be used to check for a failure after the fact.
 */

int bkr_device_reset(int mode)
{
	outb(0, ioport);

	device_isa.control = BIT_DMA_REQUEST;
	device.size = 0;

	switch(BKR_DENSITY(mode))
		{
		case BKR_HIGH:
		device_isa.control |= BIT_HIGH_DENSITY;
		device.bytes_per_line = BYTES_PER_LINE_HIGH;
		break;

		case BKR_LOW:
		device.bytes_per_line = BYTES_PER_LINE_LOW;
		break;

		default:
		return(-ENXIO);
		}

	switch(BKR_VIDEOMODE(mode))
		{
		case BKR_NTSC:
		device_isa.control |= BIT_NTSC_VIDEO;
		device.frame_size = device.bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
		break;

		case BKR_PAL:
		device.frame_size = device.bytes_per_line * LINES_PER_FIELD_PAL * 2;
		break;

		default:
		return(-ENXIO);
		}

	device.size = device.alloc_size - device.alloc_size % device.frame_size;

	return(0);
}


/*
 * bkr_device_start_transfer()
 *
 * Start the tape <---> memory data transfer.
 */

int bkr_device_start_transfer(direction_t direction)
{
	unsigned long  flags;
	jiffies_t  bailout;

	device.direction = direction;

	/*
	 * Set up the DMA channel.
	 */

	if(request_dma(dma, BKR_NAME) < 0)
		{
		device.direction = STOPPED;
		return(-EBUSY);
		}

	device.head = 0;
	device.tail = 0;
	memset(device.buffer, 0, device.alloc_size);

	flags = claim_dma_lock();
	disable_dma(dma);
	clear_dma_ff(dma);
	set_dma_mode(dma, (direction == WRITING) ? DMA_MEM_TO_IO : DMA_IO_TO_MEM);
	set_dma_addr(dma, device_isa.phys_addr);
	set_dma_count(dma, device.size);
	release_dma_lock(flags);
	enable_dma(dma);

	/*
	 * Work the card's control bits.
	 */

	outb(device_isa.control, ioport);
	device_isa.control |= (device.direction == WRITING) ? BIT_TRANSMIT : BIT_RECEIVE;
	outb(device_isa.control, ioport);

	/*
	 * Do we see a heart beat on the DREQ line?
	 */

	bailout = jiffies + HZ/MIN_SYNC_FREQ;
	while(get_dreq_status(dma) == 0)
		if(jiffies >= bailout)
			{
			bkr_device_stop_transfer();
			return(-EIO);
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
	device.direction = STOPPED;

	free_dma(dma);
	return;
}


/*
 * bkr_device_read()
 *
 * Doesn't actually "read" data... just waits until the requested length of
 * data starting at device.tail becomes available in the DMA buffer.
 * Returns 0 on success, -EINTR on interrupt, -ETIMEDOUT on timeout,
 * -EWOULDBLOCK if the operation would block (and we don't want it to) or
 * any error code returned by update_dma_offset().
 */

int bkr_device_read(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device_isa.last_update < HZ/MIN_UPDATE_FREQ) && (bytes_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.head);

	if(((bytes_in_buffer() >= length) && (space_in_buffer() > DMA_HOLD_OFF)) || result)
		return(result);
	if(f_flags & O_NONBLOCK)
		return(-EWOULDBLOCK);

	do
		{
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/MAX_UPDATE_FREQ);
		if(jiffies >= bailout)
			result = -ETIMEDOUT;
		else if(signal_pending(current))
			result = -EINTR;
		else
			result = update_dma_offset(&device.head);
		}
	while(((bytes_in_buffer() < length) || (space_in_buffer() <= DMA_HOLD_OFF)) && !result);

	return(result);
}


/*
 * bkr_device_write()
 *
 * Doesn't actually "write" data... just waits until the requested amount
 * of space has become available starting at the device head.  Returns 0 on
 * success, -ETIMEDOUT on timeout, -EWOULDBLOCK if the operation would
 * block (and we don't want it to) or any error code returned by
 * update_dma_offset().
 */

int bkr_device_write(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device_isa.last_update < HZ/MIN_UPDATE_FREQ) && (space_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.tail);

	if(((space_in_buffer() >= length) && (bytes_in_buffer() > DMA_HOLD_OFF)) || result)
		return(result);
	if(f_flags & O_NONBLOCK)
		return(-EWOULDBLOCK);

	do
		{
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/MAX_UPDATE_FREQ);
		if(jiffies >= bailout)
			result = -ETIMEDOUT;
		else
			result = update_dma_offset(&device.tail);
		}
	while(((space_in_buffer() < length) || (bytes_in_buffer() <= DMA_HOLD_OFF)) && !result);

	return(result);
}


/*
 * bkr_device_flush()
 *
 * Make sure we've completed a full video frame then wait until the
 * contents of the DMA buffer have been written to tape.  Returns 0 on
 * success, -ETIMEDOUT on timeout or any error code returned by
 * update_dma_offset() or bkr_device_write().
 */

int bkr_device_flush(jiffies_t bailout)
{
	int  count, result;

	count = device.head % device.frame_size;
	if(count)
		{
		count = device.frame_size - count;
		bkr_device_write(count, 0, bailout);
		memset(device.buffer + device.head, 0, count);
		device.head += count;
		if(device.head == device.size)	/* guaranteed to not go past end */
			device.head = 0;
		}

	bkr_device_write(INTERNAL_BUFFER, 0, bailout);
	device.head += INTERNAL_BUFFER;	/* guaranteed to not loop around */

	result = bkr_device_write(device.size - 2*sector.video_size, 0, bailout);

	/*
	 * FIXME: what the hell's with the off-by-one-line bug!!
	while((bytes_in_buffer() > device.bytes_per_line) && !result)
	 */
	while(bytes_in_buffer() && !result)
		{
		result = update_dma_offset(&device.tail);
		if(jiffies >= bailout)
			result = -ETIMEDOUT;
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

static int update_dma_offset(unsigned int *offset)
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
	clear_dma_ff(dma);
	*offset = device.size - get_dma_residue(dma);
	release_dma_lock(flags);

	device_isa.last_update = jiffies;

	return(0);
}


/*
 * get_dreq_status()
 *
 * Return the status of a DMA channel's DREQ line.  1 = active, 0 =
 * inactive.
 */

static int get_dreq_status(unsigned int dmanr)
{
	int  result;

	if(dmanr <= 3)
		result = dma_inb(DMA1_STAT_REG) >> (4 + dmanr);
	else
		result = dma_inb(DMA2_STAT_REG) >> dmanr;

	return(result & 1);
}
