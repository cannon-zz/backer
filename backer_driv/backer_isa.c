/*
 * backer_isa
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                   ISA Device I/O & Kernel Interface
 *
 * Copyright (C) 2000  Kipp C. Cannon
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

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/mtio.h>
#include <linux/string.h>

#include <asm/dma.h>
#include <asm/io.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"


/*
 * Function prototypes
 */

int   open(struct inode *, struct file *);
void  close(struct inode *, struct file *);
int   read(struct inode *, struct file *, char *, int);
int   write(struct inode *, struct file *, const char *, int);
int   ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int   lseek(struct inode *, struct file *, off_t, int);
static int   get_dreq_status(unsigned int);
static void  bkr_device_reset(void);
static int   update_dma_offset(unsigned int *);
static int   start_transfer(void);
static void  stop_transfer(void);
static void  bkr_dma_reset(void);


/*
 * Global Data
 */

unsigned int  ioport = 0;                       /* I/O port */
unsigned int  dma    = 0;                       /* DMA channel */
unsigned int  buffer = 0;                       /* allocated DMA buffer size */
unsigned int  timeout = BKR_MAX_TIMEOUT+1;      /* length of time to wait on I/O events */

struct file_operations file_ops =
	{
	lseek,
	read,
	write,
	NULL,					/* readdir() */
	NULL,					/* select() */
	ioctl,
	NULL,					/* mmap() */
	open,
	close,
	NULL,					/* fsync() */
	NULL,					/* fasync() */
	NULL,					/* check_media_change() */
	NULL					/* revalidate() */
	};



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

int init_module(void)
{
	int  result;

	/*
	 * Don't export any symbols;  print version.
	 */

	register_symtab(NULL);
	printk(KERN_INFO BKR_NAME ": Backer 16/32 driver version " BKR_VERSION "\n");

	/*
	 * Initialize some structure members.
	 */

	block.buffer = NULL;
	sector.buffer = NULL;
	device.direction = O_RDWR;

	/*
	 * Check options and set defaults if necessary then grab resources,
	 * initialize device parameters, and register device.
	 */

	if(ioport == 0)
		ioport = DEFAULT_IOPORT;
	if(dma == 0)
		dma = DEFAULT_DMA_CHANNEL;
	if(buffer == 0)
		buffer = DEFAULT_BUFFER_SIZE;
	if(timeout > BKR_MAX_TIMEOUT)
		timeout = DEFAULT_TIMEOUT;
	timeout *= HZ;

	if((device.buffer = (unsigned char *) kmalloc(buffer, GFP_KERNEL | GFP_DMA)) == NULL)
		{
		printk(KERN_ERR BKR_NAME ":   can't get %u byte DMA buffer", buffer);
		result = -ENOMEM;
		goto no_dmamem;
		}
	if(check_region(ioport, 1) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   I/O port %#x in use", ioport);
		result = -EBUSY;
		goto ioport_busy;
		}
	request_region(ioport, 1, BKR_NAME);
	if((result = bkr_set_parms(DEFAULT_MODE, buffer)) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   can't allocate memory");
		goto no_bufmem;
		}
	if((result = register_chrdev(BKR_MAJOR, BKR_NAME, &file_ops)) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   can't get device major %u", BKR_MAJOR);
		goto cant_register;
		}

	/*
	 * Driver installed.
	 */

	bkr_device_reset();
	printk(KERN_INFO BKR_NAME ": dma=%u ioport=%#x buffer=%u mode=%#06x\n",
	       dma, ioport, buffer, device.mode);
	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_register:
		kfree(sector.buffer);
		kfree(block.buffer);
	no_bufmem:
		release_region(ioport, 1);
	ioport_busy:
		kfree(device.buffer);
	no_dmamem:
		printk(" --- driver not installed\n");

	return(result);
}

void cleanup_module(void)
{
	bkr_device_reset();
	kfree(sector.buffer);
	kfree(block.buffer);
	release_region(ioport, 1);
	kfree(device.buffer);
	unregister_chrdev(BKR_MAJOR, BKR_NAME);
}


/*
 * open() --- Arbitrate device open requests.
 *
 * The device can only be opened for reading/writing once at a time since
 * there is only one data stream to interact with but the device can be
 * opened any number of times if a direction is not given (i.e. O_RDWR).
 * Furthermore, to protect the contents of the data stream only one user at
 * a time is given access to the device (super users are always given
 * access).  A data transfer is started when the device is opened with a
 * specific transfer direction (i.e. either O_RDONLY or O_WRONLY) with the
 * transfer proceding in the direction requested.
 */

int open(struct inode *inode, struct file *filp)
{
	int  result;

	/*
	 * Access control.
	 */

	if(!MOD_IN_USE)
		device.owner = current->uid;
	else if((device.owner != current->uid) && (device.owner != current->euid) && !suser())
		return(-EBUSY);
	if((device.direction != O_RDWR) && ((filp->f_flags & O_ACCMODE) != O_RDWR))
		return(-EBUSY);

	MOD_INC_USE_COUNT;

	if((filp->f_flags & O_ACCMODE) == O_RDWR)
		return(0);

	/*
	 * If we've made it this far then this open request is defining the
	 * transfer direction so some additional stuff has to be done.
	 */

	if((sector.buffer == NULL) || (block.buffer == NULL))
		{
		result = -ENXIO;
		goto mode_not_set;
		}

	if(request_dma(dma, BKR_NAME) < 0)
		{
		printk(KERN_ERR BKR_NAME ":  dma %u not available\n", dma);
		result = -EBUSY;
		goto cant_get_dma;
		}

	device.direction = filp->f_flags & O_ACCMODE;
	bkr_dma_reset();
	bkr_format_reset();
	result = start_transfer();
	if(result < 0)
		goto cant_start;

	if((device.direction == O_WRONLY) && (BKR_FORMAT(device.mode) == BKR_FMT))
		{
		result = bkr_write_bor(jiffies + timeout);
		if(result < 0)
			goto cant_write_bor;
		}

	filp->private_data = 0;	/* FIXME: find a better way */
	return(0);

	cant_write_bor:
		stop_transfer();
	cant_start:
		free_dma(dma);
	cant_get_dma:
	mode_not_set:
		MOD_DEC_USE_COUNT;
		return(result);
}


/*
 * close()
 *
 * Handle device close requests
 */

void close(struct inode *inode, struct file *filp)
{
	jiffies_t  bailout;

	MOD_DEC_USE_COUNT;

	/*
	 * Do nothing if this open didn't define the transfer direction.
	 */

	if((filp->f_flags & O_ACCMODE) == O_RDWR)
		return;

	/*
	 * Write an EOR mark if needed then flush the DMA buffer.
	 */

	if(device.direction == O_WRONLY)
		{
		bailout = jiffies + timeout;

		if(BKR_FORMAT(device.mode) == BKR_FMT)
			bkr_write_eor(bailout);

		bkr_device_flush(bailout);
		}

	stop_transfer();
	free_dma(dma);

	return;
}


/*
 * ioctl()
 *
 * The general intent is for this driver to provide a standard magnetic
 * tape interface.  To this end we try to implement as many of the standard
 * mtio.h ioctl's as make sense for this device.  Any of our own features
 * that don't fit into the mtio stuff are implemented as new calls.
 */

int ioctl(struct inode *inode, struct file *filp, unsigned int op, unsigned long argument)
{
	int result;
	union
		{
		struct mtop  mtop;
		struct mtget  mtget;
		struct mtpos  mtpos;
		struct bkrstatus  bkrstatus;
		struct bkrconfig  bkrconfig;
		struct bkrformat  bkrformat;
		} arg;

	if(_IOC_SIZE(op) != 0)
		{
		if((result = verify_area((_IOC_DIR(op) == _IOC_READ) ? VERIFY_WRITE : VERIFY_READ,
		                         (void *) argument, _IOC_SIZE(op))) < 0)
			return(result);
		}
	else if((op == BKRIOCGETAUX) || (op == BKRIOCSETAUX))
		{
		if((result = verify_area((_IOC_DIR(op) == _IOC_READ) ? VERIFY_WRITE : VERIFY_READ,
		                         (void *) argument, sector.aux_length)) < 0)
			return(result);
		if(sector.buffer == NULL)
			return(-ENXIO);
		}

	switch(op)
		{
		case MTIOCTOP:
		memcpy_fromfs(&arg.mtop, (void *) argument, sizeof(struct mtop));
		switch(arg.mtop.mt_op)
			{
			case MTNOP:
			return(0);

			case MTRESET:
			return(0);

			default:
			return(-EINVAL);
			}

		case MTIOCGET:
		arg.mtget.mt_type = MT_ISUNKNOWN;        /* maybe we'll have our own one day... */
		arg.mtget.mt_resid = 0;                  /* not implemented */
		arg.mtget.mt_dsreg = device.mode;        /* device-dependant status bits */
		arg.mtget.mt_gstat = GMT_ONLINE(-1L);    /* device-independant status bits */
		arg.mtget.mt_erreg = 0;                  /* not implemented */
		arg.mtget.mt_fileno = 0;                 /* not implemented */
		arg.mtget.mt_blkno = 0;                  /* not implemented */
		memcpy_tofs((void *) argument, &arg.mtget, sizeof(struct mtget));
		return(0);

		case MTIOCPOS:
		arg.mtpos.mt_blkno = 0;                  /* not implemented */
		memcpy_tofs((void *) argument, &arg.mtpos, sizeof(struct mtpos));
		return(0);

		case BKRIOCGETSTATUS:
		/*
		 * ADDME
		if(jiffies - device.last_update >= HZ/MIN_UPDATE_FREQ)
			switch(device.direction)
				{
				case O_WRONLY:
				update_dma_offset(&device.tail);
				break;

				case O_RDONLY:
				update_dma_offset(&device.head);
				break;

				case O_RDWR:
				}
		 */
		arg.bkrstatus.bytes = bytes_in_buffer();
		arg.bkrstatus.errors = errors;
		arg.bkrstatus.worst_match = worst_match;
		arg.bkrstatus.best_nonmatch = best_nonmatch;
		arg.bkrstatus.least_skipped = least_skipped;
		arg.bkrstatus.most_skipped = most_skipped;
		memcpy_tofs((void *) argument, &arg.bkrstatus, sizeof(struct bkrstatus));
		return(0);

		case BKRIOCGETMODE:
		arg.bkrconfig.mode = device.mode;
		arg.bkrconfig.timeout = timeout/HZ;
		memcpy_tofs((void *) argument, &arg.bkrconfig, sizeof(struct bkrconfig));
		return(0);

		case BKRIOCSETMODE:
		if(device.direction != O_RDWR)
			return(-EBUSY);
		memcpy_fromfs(&arg.bkrconfig, (void *) argument, sizeof(struct bkrconfig));
		if(arg.bkrconfig.timeout > BKR_MAX_TIMEOUT)
			return(-EINVAL);
		timeout = arg.bkrconfig.timeout*HZ;
		return(bkr_set_parms(arg.bkrconfig.mode, buffer));

		case BKRIOCGETFORMAT:
		arg.bkrformat.bytes_per_line = device.bytes_per_line;
		arg.bkrformat.buffer_size = device.size;
		arg.bkrformat.sector_size = sector.size;
		arg.bkrformat.header_length = sector.header_length;
		arg.bkrformat.footer_length = sector.footer_length;
		arg.bkrformat.aux_offset = sector.aux - sector.buffer;
		arg.bkrformat.aux_length = sector.aux_length;
		arg.bkrformat.block_size = block.size;
		arg.bkrformat.block_capacity = block.size - block.parity - sizeof(header_t);
		arg.bkrformat.block_parity = block.parity;
		memcpy_tofs((void *) argument, &arg.bkrformat, sizeof(struct bkrformat));
		return(0);

		case BKRIOCGETAUX:
		memcpy_tofs((void *) argument, sector.aux, sector.aux_length);
		return(0);

		case BKRIOCSETAUX:
		memcpy_fromfs(sector.aux, (void *) argument, sector.aux_length);
		return(0);

		default:
		return(-EINVAL);
		}
}


/*
 * read(), write()
 */

int read(struct inode *inode, struct file *filp, char *buff, int count)
{
	jiffies_t  bailout;
	int  moved, chunk_size, result = 0;

	if((filp->f_flags & O_ACCMODE) != O_RDONLY)
		return(-EPERM);
	if(filp->private_data != 0)	/* FIXME */
		{
		result = (int) filp->private_data;
		filp->private_data = 0;
		return(result);
		}

	bailout = jiffies + timeout;
	for(moved = 0; count; buff += chunk_size, count -= chunk_size)
		{
		chunk_size = block.end - block.offset;
		if(chunk_size > count)
			chunk_size = count;

		memcpy_tofs(buff, block.offset, chunk_size);

		moved += chunk_size;
		block.offset += chunk_size;
		if(block.offset == block.end)
			{
			result = block.read(filp->f_flags, bailout);
			if(result <= 0)
				{
				if(moved)	/* FIXME */
					filp->private_data = (void *) result;
				return(moved ? moved : result);
				}
			}
		}

	return(moved);
}

int write(struct inode *inode, struct file *filp, const char *buff, int count)
{
	jiffies_t  bailout;
	int  moved, chunk_size, result;

	if((filp->f_flags & O_ACCMODE) != O_WRONLY)
		return(-EPERM);
	if(filp->private_data != 0)	/* FIXME */
		{
		result = (int) filp->private_data;
		filp->private_data = 0;
		return(result);
		}

	bailout = jiffies + timeout;
	for(moved = 0; count; buff += chunk_size, count -= chunk_size)
		{
		chunk_size = block.end - block.offset;
		if(chunk_size > count)
			chunk_size = count;

		memcpy_fromfs(block.offset, buff, chunk_size);

		moved += chunk_size;
		block.offset += chunk_size;
		if(block.offset == block.end)
			{
			result = block.write(filp->f_flags, bailout);
			if(result < 0)
				{
				if(moved)	/* FIXME */
					filp->private_data = (void *) result;
				return(moved ? moved : result);
				}
			}
		}

	return(moved);
}


/*
 * lseek()
 *
 * Since Backer provides an unseekable data stream, we must explicitly
 * return an error code otherwise the kernel's default handler will behave
 * as though seeking is possible.
 */

int lseek(struct inode *inode, struct file *filp, off_t offset, int whence)
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
 * bkr_device_read()
 *
 * Doesn't actually "read" data... just waits until the requested length of
 * data starting at device.tail becomes available in the DMA buffer.
 * Returns 0 on success, -EINTR on interrupt, -ETIMEDOUT on timeout or any
 * error code returned by update_dma_offset().
 */

int bkr_device_read(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device.last_update < HZ/MIN_UPDATE_FREQ) && (bytes_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.head);

	if(((bytes_in_buffer() >= length) && (space_in_buffer() > DMA_HOLD_OFF)) || result)
		return(result);
	if(f_flags & O_NONBLOCK)
		return(-EWOULDBLOCK);

	do
		{
		current->timeout = jiffies + HZ/MAX_UPDATE_FREQ;
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if(jiffies >= bailout)
			result = -ETIMEDOUT;
		else if(current->signal & ~current->blocked)  /* FIXME: what's correct here? */
			result = -EINTR;
		else
			result = update_dma_offset(&device.head);
		}
	while(((bytes_in_buffer() < length) || (space_in_buffer() <= DMA_HOLD_OFF)) && !result);

	current->timeout = 0;
	return(result);
}


/*
 * bkr_device_write()
 *
 * Doesn't actually "write" data... just waits until the requested amount
 * of space has become available starting at the device head.  Returns 0 on
 * success, -ETIMEDOUT on timeout or any error code returned by
 * update_dma_offset().
 */

int bkr_device_write(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	length += DMA_HOLD_OFF;

	if((jiffies - device.last_update < HZ/MIN_UPDATE_FREQ) && (space_in_buffer() >= length))
		return(0);

	result = update_dma_offset(&device.tail);

	if(((space_in_buffer() >= length) && (bytes_in_buffer() > DMA_HOLD_OFF)) || result)
		return(result);
	if(f_flags & O_NONBLOCK)
		return(-EWOULDBLOCK);

	do
		{
		current->timeout = jiffies + HZ/MAX_UPDATE_FREQ;
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if(jiffies >= bailout)
			result = -ETIMEDOUT;
		else
			result = update_dma_offset(&device.tail);
		}
	while(((space_in_buffer() < length) || (bytes_in_buffer() <= DMA_HOLD_OFF)) && !result);
	current->timeout = 0;

	return(result);
}

/*
 * bkr_device_flush()
 *
 * Wait until the contents of the DMA buffer have been written to tape.
 * Returns 0 on success, -ETIMEDOUT on timeout or any error code returned
 * by update_dma_offset();
 */

int bkr_device_flush(jiffies_t bailout)
{
	int  result;

	bkr_device_write(INTERNAL_BUFFER, 0, bailout);

	device.head += INTERNAL_BUFFER;
	if(device.head >= device.size)
		device.head -= device.size;

	result = bkr_device_write(device.size - 2*sector.size, 0, bailout);

	while(bytes_in_buffer() && (jiffies < bailout) && !result)
		result = update_dma_offset(&device.tail);

	if(jiffies >= bailout)
		return(-ETIMEDOUT);
	return(result);
}


/*
 * update_dma_offset()
 *
 * Retrieves the offset within the DMA buffer at which the next transfer
 * will occur.  During read operations this is the buffer's head; during
 * write operations it is the tail.  Returns 0 on success, -EIO if no video
 * signal is present on the input.
 */

static int update_dma_offset(unsigned int *offset)
{
	unsigned long  flags;
	jiffies_t  bailout;

	bailout = jiffies + HZ/MIN_SYNC_FREQ;

	while(~inb(ioport) & BIT_SYNC)
		 if(jiffies >= bailout)
			return(-EIO);
	while(inb(ioport) & BIT_SYNC)
		if(jiffies >= bailout)
			return(-EIO);

	save_flags(flags);
	cli();
	clear_dma_ff(dma);
	*offset = device.size - get_dma_residue(dma);
	restore_flags(flags);

	device.last_update = jiffies;

	return(0);
}


/*
 * bkr_device_reset()
 *
 * Reset the Backer device to its power-on state.
 */

static void bkr_device_reset(void)
{
	outb(0, ioport);
}


/*
 * start_transfer()
 *
 * Start the tape <---> memory data transfer.
 */

static int start_transfer(void)
{
	memset(device.buffer, 0, buffer);

	outb(device.control, ioport);

	device.control |= (device.direction == O_WRONLY) ? BIT_TRANSMIT : BIT_RECEIVE;
	outb(device.control, ioport);

	outb(device.control | BIT_DMA_REQUEST, ioport);

	if(device.direction == O_WRONLY)
		if(get_dreq_status(dma) == 0)
			{
			stop_transfer();
			return(-ENXIO);
			}

	enable_dma(dma);

	device.last_update = jiffies;

	return(0);
}


/*
 * stop_transfer()
 *
 * Stop the tape <---> memory transfer.  The DMA controller is left fully
 * configured so if someone else's code accidentally initiates a transfer
 * it will occur in a safe region of memory (i.e. our buffer) but the
 * buffer is cleared.
 */

static void stop_transfer(void)
{
	outb(device.control, ioport);

	disable_dma(dma);

	device.control &= ~(BIT_TRANSMIT | BIT_RECEIVE);
	outb(device.control, ioport);

	device.direction = O_RDWR;

	memset(device.buffer, 0, buffer);
}


/*
 * bkr_dma_reset()
 *
 * Resets the DMA channel.
 */

static void bkr_dma_reset(void)
{
	unsigned long  flags;

	save_flags(flags);
	cli();
	disable_dma(dma);
	clear_dma_ff(dma);
	switch(device.direction)
		{
		case O_WRONLY:
		set_dma_mode(dma, DMA_MEM_TO_IO);
		break;

		case O_RDONLY:
		set_dma_mode(dma, DMA_IO_TO_MEM);
		break;
		}
	set_dma_addr(dma, virt_to_bus(device.buffer));
	set_dma_count(dma, device.size);
	restore_flags(flags);
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
