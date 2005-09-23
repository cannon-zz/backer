/*
 * backer_device.h
 *
 * Driver for Danmere's Backer 16/32 video tape backup cards.
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

#include "backer.h"


/*
 * Parameters.
 */

#ifndef BKR_BUFFER_SIZE                 /* allow make to override */
#define BKR_BUFFER_SIZE  (1 << 19)      /* must be a power of 2 */
#endif
#define BKR_OFFSET_MASK  (BKR_BUFFER_SIZE - 1)


/*
 * Types
 */

typedef enum                            /* hardware state */
	{
	BKR_STOPPED = 0,
	BKR_READING,
	BKR_WRITING,
	BKR_SUSPENDED
	} bkr_state_t;

typedef enum
	{
	BKR_ISA_DEVICE,
	BKR_PARPORT_DEVICE
	} bkr_device_type_t;

struct bkr_device_t
	{
	unsigned char  *buffer;         /* location of I/O buffer */
	unsigned int  size;             /* amount of buffer in use */
	unsigned int  head;             /* offset of next write transfer */
	unsigned int  tail;             /* offset of next read transfer */
	unsigned int  bytes_per_line;   /* width of one line of video */
	unsigned int  frame_size;       /* bytes in a full video frame */
	unsigned char  control;         /* hardware control byte */
	int  mode;                      /* mode (see backer.h) */
	bkr_state_t  state;             /* current device state */
	bkr_device_type_t  type;        /* device type */
	struct bkr_device_ops_t
		{
		void  (*inc_use_count)(void);
		void  (*dec_use_count)(void);
		int   (*start)(struct bkr_device_t *, bkr_state_t);
		void  (*stop)(struct bkr_device_t *);
		int   (*read)(struct bkr_device_t *, unsigned int);
		int   (*write)(struct bkr_device_t *, unsigned int);
		int   (*flush)(struct bkr_device_t *);
		} *ops;                 /* device interface functions */
	void  *private;                 /* device specific data storage */
	};

typedef struct bkr_device_ops_t  bkr_device_ops_t;
typedef struct bkr_device_t  bkr_device_t;


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the device I/O buffer.
 *
 * space_in_buffer() + bytes_in_buffer() == size - 1
 * tail == head ---> bytes_in_buffer() == 0
 * head == tail - 1 ---> space_in_buffer() == 0
 */

static unsigned int space_in_buffer(unsigned int head, unsigned int tail, unsigned int size)
{
	int  result = tail - 1 - head;
	if(result < 0)
		result += size;
	return(result);
}

static unsigned int bytes_in_buffer(unsigned int head, unsigned int tail, unsigned int size)
{
	int  result = head - tail;
	if(result < 0)
		result += size;
	return(result);
}


#endif /* BACKER_DEVICE_H */
