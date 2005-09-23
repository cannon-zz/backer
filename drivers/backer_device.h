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


/*
 * The meaning of some of the variables used in sector formating is as
 * follows.  The diagram shows the layout for a complete frame (two
 * sectors) starting on an odd field of video.
 *
 *    --       --  +-------------+
 *    ^   leader   |   leader    |
 *    |        --  +-------------+  --
 *    |            |     A       |   ^
 *    |            |     C       |   |
 *    |            |     T R     |   |
 *    |            |     I E     |   |
 *  field_size     |     V G     |   active_size
 *    |            |     E I     |   |
 *    |            |       O     |   |
 *    |            |       N     |   |
 *    |            |             |   v
 *    |        --  +-------------+  --
 *    v  trailer   |   trailer   |
 *    --       --  +-------------+  <-- one extra line in NTSC
 *                 |   leader    |
 *                 +-------------+
 *                 |     A       |
 *                 |     C       |
 *                 |     T R     |
 *                 |     I E     |
 *                 |     V G     |
 *                 |     E I     |
 *                 |       O     |
 *                 |       N     |
 *                 |             |
 *                 +-------------+
 *                 |   trailer   |
 *                 +-------------+
 *
 * The "extra line" is a consequence of the interleaving of the image and
 * is a 1/2 width line occuring in the odd video field of the card's NTSC
 * signal.  The formating code handles this by inserting a dummy line.  PAL
 * also has a 1/2 width line but in this case its presence makes both video
 * fields have the same number of lines so it's just absorbed into the
 * trailer.  (Note:  although they are 1/2 width lines, a full line of data
 * is output by the Backer hardware).
 *
 * Zoomed in on the active area of a single sector, the meaning of a few
 * more variables is shown below.  pos always points to the next byte to be
 * read/written and if equal to end then the sector is empty/full
 * respectively.
 *
 * If the sector contains less than its maximum capacity of data, then the
 * actual number of bytes in the sector is represented as a 12 bit number
 * whose high 8 bits are placed in the last available byte of the data area
 * and low 4 bits are stored in the header.  The number thusly represented
 * is not simply the count of bytes in the sector but the count of bytes
 * encoded in such a way that the low 4 bits are never all 0 which allows
 * the 0 value to be used to indicate that the sector is full.
 *
 * Logically, the data portion of the sector (including the header) is
 * divided into blocks.  Associated with each block is a group of parity
 * symbols used for error control.  The parity symbols for all the blocks
 * are grouped together at the top end of the sector's active area.
 *
 * The active area of the sector is not written linearly to tape but is
 * instead interleaved in order to distribute burst errors among separate
 * blocks.  The final distance, in bytes, between adjacent bytes of a given
 * block when written to tape is given by the interleave parameter.
 *
 *             --  +-------------+ -- <-- buffer (pointer)
 *             ^   |             |  ^
 *             |   |             |  |
 *             |   |             |  |
 *             |   |             |  |
 *             |   |      D      |  |
 *     data_size   |      A      |  |
 *             |   |      T      |  buffer_size
 *             |   |      A      |  |
 *             |   |             |  |
 *             |   |             |  | <--- end (pointer as appropriate)
 *             |   |             |  |
 *             |   +- hi 8 bits -+  |
 *             |   +-------------+  |
 *             v   |    Header   |  |
 *             --  +-------------+  |
 *             ^   |             |  |
 *   parity_size   |    Parity   |  |
 *             v   |             |  v
 *             --  +-------------+ --
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

struct bkr_device_t
	{
	unsigned char  *io_buffer;      /* location of I/O buffer */
	unsigned int  io_size;          /* amount of buffer in use */
	unsigned int  io_head;          /* offset of next write transfer */
	unsigned int  io_tail;          /* offset of next read transfer */
	unsigned char  control;         /* hardware control byte */
	unsigned int  frame_size;       /* bytes in a full video frame */
	int  odd_field;                 /* current video field is odd */
	int  mode;                      /* mode (see backer.h) */
	bkr_state_t  state;             /* current device state */
	struct bkr_device_ops_t
		{
		struct module  *owner;
		int   (*start)(struct bkr_device_t *, bkr_state_t);
		void  (*stop)(struct bkr_device_t *);
		int   (*read)(struct bkr_device_t *, unsigned int);
		int   (*write)(struct bkr_device_t *, unsigned int);
		int   (*flush)(struct bkr_device_t *);
		} *ops;                 /* device interface functions */
	bkr_format_info_t format_info[BKR_NUM_FORMATS];
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
