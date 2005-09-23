/*
 * backer_device.h
 *
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * See backer_fmt.h for a description of how to use the formating and
 * device layers together.
 *
 * For anyone interested in working on support for other I/O layers (eg.
 * fb/video4linux) the semantics for the read and write functions are as
 * follows.  Firstly, they generally don't actually "read" or "write"
 * anything.  It is assumed that there exists some sort of I/O buffer (in
 * the case of Backer this is the DMA buffer) which some other thing is
 * magically putting data into or taking data out of all on its own.  All
 * these functions do is check to ensure that the buffer either contains at
 * least as much data as requested (read) or as much empty space as
 * requested (write) and then update the buffer tail/head appropriately.
 * Upon returning from these functions, the calling code (the formating
 * layer) then itself does the actual transfer of data to/from the buffer
 * and updating of the head/tail as appropriate.
 *
 * In the case of the Backer device, the DMA hardware is the thing that is
 * magically moving data into/out of the buffer but in the case of
 * bkrencode the read and write functions actually do have to handle it
 * themselves.  However, since the length parameter is just a minimum
 * requested amount they are free to excede it which in bkrencode's case
 * they do:  every time write is called it just completely empties the
 * buffer while read fills the buffer to 50% (which is assumed to always be
 * more than requested).
 *
 * For the other functions that need to be defined by the device layer
 * (reset, start/stop, etc.) there are some minimum tasks that they are
 * required to perform.  See stdio_dev.c for the minimal implementations.
 * 
 * Copyright (C) 2000,2001  Kipp C. Cannon
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef BACKER_DEVICE_H
#define BACKER_DEVICE_H

#ifdef __KERNEL__

#include <linux/types.h>

#else

#include <sys/types.h>
typedef int spinlock_t;

#endif

/*
 * Types
 */

typedef  unsigned long  jiffies_t;      /* type for jiffies */

typedef u_int16_t  bkr_offset_t;        /* unsigned 16 bit integer */

#define  BKR_BUFFER_SIZE  65536         /* must match bkr_offset_t */

typedef enum                            /* hardware state */
	{
	BKR_STOPPED = 0,
	BKR_READING,
	BKR_WRITING,
	BKR_SUSPENDED
	} bkr_state_t;

typedef enum
	{
	BKR_ISA_DEVICE = 0,
	BKR_PARPORT_DEVICE,
	BKR_STDIO_DEVICE
	} bkr_device_type_t;

typedef struct
	{
	unsigned char *buffer;          /* location of I/O buffer */
	unsigned int  size;             /* amount of buffer in use */
	bkr_offset_t  head;             /* offset of next write transfer */
	bkr_offset_t  tail;             /* offset of next read transfer */
	unsigned int  bytes_per_line;   /* width of one line of video */
	unsigned int  frame_size;       /* bytes in a full video frame */
	int  mode;                      /* mode (see backer.h) */
	bkr_state_t  state;             /* current device state */
	spinlock_t  lock;               /* spinlock for SMP machines */
	union
		{
		bkr_device_type_t  type;
#ifdef __KERNEL__
		struct
			{
			bkr_device_type_t  type;
			jiffies_t  last_update; /* jiffies at time of last update */
			unsigned char  control; /* control byte for card */
			unsigned int  ioport;   /* I/O port */
			unsigned int  dma;      /* DMA channel number */
			dma_addr_t  phys_addr;  /* DMA buffer's bus address */
			} isa;
		struct
			{
			bkr_device_type_t  type;
			} parport;
#endif
		} hrdwr;
	} bkr_device_t;


/*
 * Functions exported by the device layer.
 */

int   bkr_device_reset(bkr_device_t *, bkr_state_t);
int   bkr_device_start_transfer(bkr_device_t *, bkr_state_t, jiffies_t);
void  bkr_device_stop_transfer(bkr_device_t *);
int   bkr_device_read(bkr_device_t *, unsigned int);
int   bkr_device_write(bkr_device_t *, unsigned int);
int   bkr_device_flush(bkr_device_t *);
unsigned int  space_in_buffer(bkr_device_t *);
unsigned int  bytes_in_buffer(bkr_device_t *);


#endif /* BACKER_DEVICE_H */
