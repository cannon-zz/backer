/*
 * backer_isa
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                            ISA Device I/O
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
static void  bkr_device_reset(void);
static void  update_device_offset(unsigned int *, unsigned long bailout);
static void  start_transfer(void);
static void  stop_transfer(void);
static void  bkr_reset_dma(void);


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
	int  result_code;

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
		printk(KERN_ERR BKR_NAME ":   can't get %u byte dma buffer\n", buffer);
		result_code = -ENOMEM;
		goto no_dmamem;
		}
	if(check_region(ioport, 1) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   port %#x in use\n", ioport);
		result_code = -EBUSY;
		goto ioport_busy;
		}
	request_region(ioport, 1, BKR_NAME);
	if((result_code = bkr_set_parms(DEFAULT_MODE, buffer)) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   can't get internal buffers\n");
		goto no_bufmem;
		}
	if((result_code = register_chrdev(BKR_MAJOR, BKR_NAME, &file_ops)) < 0)
		{
		printk(KERN_ERR BKR_NAME ":   can't get device major %u\n", BKR_MAJOR);
		goto cant_register;
		}

	/*
	 * Done.
	 */

	bkr_device_reset();
	printk(KERN_INFO BKR_NAME ": dma=%u ioport=%#x buffer=%u mode=%#06x\n",
	       dma, ioport, buffer, device.mode);
	return(0);

	/*
	 * There was a problem.  Release resources as needed.
	 */

	cant_register:	kfree(sector.buffer);
	                kfree(block.buffer);
	no_bufmem:	release_region(ioport, 1);
	ioport_busy:	kfree(device.buffer);
	no_dmamem:	printk(KERN_ERR BKR_NAME ":   driver not installed\n");
	return(result_code);
}

void cleanup_module(void)
{
	bkr_device_reset();
	kfree(sector.buffer);
	kfree(block.buffer);
	kfree(device.buffer);
	release_region(ioport, 1);
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
	unsigned long  bailout;

	/*
	 * Access control.
	 */

	if(!MOD_IN_USE)
		{
		device.owner = current->uid;
		memset(device.buffer, 0, buffer);
		}
	else if((device.owner != current->uid) && (device.owner != current->euid) && !suser())
		return(-EBUSY);
	else if((device.direction != O_RDWR) && ((filp->f_flags & O_ACCMODE) != O_RDWR))
		return(-EBUSY);

	MOD_INC_USE_COUNT;

	if((filp->f_flags & O_ACCMODE) == O_RDWR)
		return(0);

	/*
	 * If we've made it this far then this open request is defining the
	 * transfer direction so some additional setup stuff has to be
	 * done.
	 */

	if(request_dma(dma, BKR_NAME) < 0)
		{
		printk(KERN_ERR BKR_NAME ":  dma %u not available\n", dma);
		MOD_DEC_USE_COUNT;
		return(-EBUSY);
		}

	device.direction = filp->f_flags & O_ACCMODE;
	bkr_reset_dma();
	bkr_format_reset();
	start_transfer();

	if((device.direction == O_WRONLY) && (BKR_FORMAT(device.mode) == BKR_FMT))
		{
		bailout = jiffies + timeout;
		bkr_write_bor(bailout);
		if(jiffies >= bailout)
			{
			stop_transfer();
			MOD_DEC_USE_COUNT;
			return(-ETIMEDOUT);
			}
		}

	return(0);
}


/*
 * close()
 *
 * Handle device close requests
 */

void close(struct inode *inode, struct file *filp)
{
	MOD_DEC_USE_COUNT;

	/*
	 * Do nothing if this open didn't define the transfer direction.
	 */

	if((filp->f_flags & O_ACCMODE) == O_RDWR)
		return;

	/*
	 * Write an EOR mark if needed then stop the DMA transfer.
	 */

	if((device.direction == O_WRONLY) && (BKR_FORMAT(device.mode) == BKR_FMT))
		bkr_write_eor(jiffies + timeout);

	stop_transfer();

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

	if(_IOC_SIZE(op) == 0)
		{
		if((op == BKRIOCGETAUX) || (op == BKRIOCSETAUX))
			if((result = verify_area((_IOC_DIR(op) == _IOC_READ) ?
			             VERIFY_WRITE : VERIFY_READ, (void *) argument,
			             sector.aux_length)) < 0)
				return(result);
		}
	else if((result = verify_area((_IOC_DIR(op) == _IOC_READ) ?
	                  VERIFY_WRITE : VERIFY_READ, (void *) argument, _IOC_SIZE(op))) < 0)
		return(result);

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
		arg.bkrstatus.bytes = bytes_in_buffer();
		arg.bkrstatus.space = space_in_buffer();
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
 *
 * During reads, timeout checking is done in block.read().
 */

int read(struct inode *inode, struct file *filp, char *buff, int count)
{
	unsigned long  bailout;
	int  moved, chunk_size;

	if((filp->f_flags & O_ACCMODE) != O_RDONLY)
		return(-EPERM);

	bailout = jiffies + timeout;
	for(moved = 0; count; count -= chunk_size, buff += chunk_size)
		{
		chunk_size = block.end - block.offset;
		if(chunk_size > count)
			chunk_size = count;

		memcpy_tofs(buff, block.offset, chunk_size);

		moved += chunk_size;
		if((block.offset += chunk_size) == block.end)
			if(block.read(bailout) == EOR_BLOCK)
				break;
		}

	if(jiffies >= bailout)
		return(moved ? moved : -ETIMEDOUT);
	return(moved);
}

int write(struct inode *inode, struct file *filp, const char *buff, int count)
{
	unsigned long  bailout;
	int  moved, chunk_size;

	if((filp->f_flags & O_ACCMODE) != O_WRONLY)
		return(-EPERM);

	bailout = jiffies + timeout;
	for(moved = 0; count; count -= chunk_size, buff += chunk_size)
		{
		chunk_size = block.end - block.offset;
		if(chunk_size > count)
			chunk_size = count;

		memcpy_fromfs(block.offset, buff, chunk_size);

		moved += chunk_size;
		if((block.offset += chunk_size) == block.end)
			{
			block.write(bailout);
			if(jiffies >= bailout)
				return(moved ? moved : -ETIMEDOUT);
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
 */

void bkr_device_read(unsigned int length, unsigned long  bailout)
{
	length += DMA_HOLD_OFF;

	if((jiffies - device.last_update < HZ/MIN_UPDATE_FREQ) && (bytes_in_buffer() >= length))
		return;

	update_device_offset(&device.head, bailout);
	device.last_update = jiffies;

	if((bytes_in_buffer() >= length) && (space_in_buffer() > DMA_HOLD_OFF))
		return;

	current->timeout = jiffies + HZ/50;
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	current->timeout = 0;

	do
		update_device_offset(&device.head, bailout);
	while((bytes_in_buffer() < length) && (jiffies < bailout));
	device.last_update = jiffies;

	return;
}


/*
 * bkr_device_write()
 *
 * Doesn't actually "write" data... just waits until the requested amount
 * of space has become available starting at the device head.
 */

void bkr_device_write(unsigned int length, unsigned long bailout)
{
	length += DMA_HOLD_OFF;

	if((jiffies - device.last_update < HZ/MIN_UPDATE_FREQ) && (space_in_buffer() >= length))
		return;

	update_device_offset(&device.tail, bailout);
	device.last_update = jiffies;

	if((space_in_buffer() >= length) && (bytes_in_buffer() > DMA_HOLD_OFF))
		return;

	current->timeout = jiffies + HZ/50;
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	current->timeout = 0;

	do
		update_device_offset(&device.tail, bailout);
	while((space_in_buffer() < length) && (jiffies < bailout));
	device.last_update = jiffies;

	return;
}

/*
 * bkr_device_flush()
 *
 * Wait until the contents of the DMA buffer have been written to tape.
 */

void bkr_device_flush(unsigned long bailout)
{
	if((device.head += INTERNAL_BUFFER) >= device.size)
		device.head -= device.size;
	while(bytes_in_buffer() && (jiffies < bailout))
		update_device_offset(&device.tail, bailout);
}


/*
 * update_device_offset()
 *
 * Retrieves the offset within the DMA buffer at which the next transfer
 * will occur.  During read operations this is the buffer's head; during
 * write operations it is the tail.
 */

static void update_device_offset(unsigned int *offset, unsigned long bailout)
{
	while(!(inb(ioport) & BIT_SYNC) && (jiffies < bailout));
	while((inb(ioport) & BIT_SYNC) && (jiffies < bailout));

	clear_dma_ff(dma);
	*offset = device.size - get_dma_residue(dma);

	if(need_resched)
		schedule();
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

static void start_transfer(void)
{
	device.command |= (device.direction == O_WRONLY) ? BIT_TRANSMIT : BIT_RECEIVE;
	outb(device.command | BIT_DMA_REQUEST, ioport);
	device.last_update = jiffies;
}


/*
 * stop_transfer()
 *
 * Stop the tape <---> memory transfer and release DMA channel.  The DMA
 * controller is left fully configured so if someone else's code
 * accidentally initiates a transfer it will occur in a safe region of
 * memory (i.e. our buffer) but the buffer is cleared.
 */

static void stop_transfer(void)
{
	device.command &= ~(BIT_TRANSMIT | BIT_RECEIVE);
	outb(device.command, ioport);

	device.direction = O_RDWR;

	disable_dma(dma);

	memset(device.buffer, 0, buffer);

	free_dma(dma);
}


/*
 * bkr_reset_dma()
 *
 * Resets the DMA channel.
 */

static void bkr_reset_dma(void)
{
	unsigned long  flags;

	bkr_device_reset();

	outb(device.command, ioport);

	save_flags(flags);
	cli();
	disable_dma(dma);
	clear_dma_ff(dma);
	if(device.direction == O_WRONLY)
		set_dma_mode(dma, DMA_MEM_TO_IO);
	else
		set_dma_mode(dma, DMA_IO_TO_MEM);
	set_dma_addr(dma, virt_to_bus(device.buffer));
	set_dma_count(dma, device.size);
	enable_dma(dma);
	restore_flags(flags);
}
