/*
 * backer_device.h
 *
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * See backer_fmt.h for a description of how to use the formating and
 * device layers together.
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

#ifndef BACKER_DEVICE_H
#define BACKER_DEVICE_H

/*
 * Transfer directions.
 */

typedef enum
	{
	STOPPED = 0,
	READING,
	WRITING
	} direction_t;


/*
 * Functions exported by the device layer.
 */

int   bkr_device_reset(int);
int   bkr_device_start_transfer(direction_t, jiffies_t);
void  bkr_device_stop_transfer(void);
int   bkr_device_read(unsigned int, f_flags_t, jiffies_t);
int   bkr_device_write(unsigned int, f_flags_t, jiffies_t);
int   bkr_device_flush(jiffies_t);


/*
 * Data shared with formating and I/O layer.
 */

struct
	{
	unsigned char *buffer;          /* location of I/O buffer */
	unsigned int  size;             /* amount of buffer in use */
	unsigned int  head;             /* offset of next write transfer */
	unsigned int  tail;             /* offset of next read transfer */
	unsigned int  bytes_per_line;   /* width of one line of video */
	unsigned int  frame_size;       /* bytes in a full video frame */
	direction_t  direction;         /* current transfer direction */
	unsigned int  alloc_size;       /* allocated buffer size */
	} device;

#endif /* BACKER_DEVICE_H */
