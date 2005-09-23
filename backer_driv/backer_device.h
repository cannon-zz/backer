/*
 * backer_device.h
 *
 * Linux 2.0.xx driver for Danmere's Backer 16/32 video tape backup cards.
 *
 * Any code making use of the formating layer code must be sure to export to
 * that code the symbols defined in this header file.  See the utility
 * bkrencode for an example of implementing a device layer.
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

#ifndef BACKER_DEVICE_H
#define BACKER_DEVICE_H

/*
 * Functions exported to formating layer
 */

void  bkr_device_read(unsigned int, unsigned long);
void  bkr_device_write(unsigned int, unsigned long);
void  bkr_device_flush(unsigned long);


/*
 * Data exported to formating layer
 */

struct
	{
	unsigned char *buffer;          /* location of DMA buffer */
	unsigned int  size;             /* amount of buffer in use */
	unsigned int  head;             /* offset of next write transfer */
	unsigned int  tail;             /* offset of next read transfer */
	unsigned int  bytes_per_line;   /* width of one line of video */
	unsigned int  oddfield;         /* current video field is odd */
	unsigned long  last_update;     /* jiffies at time of last update */
	unsigned int  mode;             /* as in bkrconfig */
	unsigned int  owner;            /* owner's user id */
	unsigned int  direction;        /* current transfer direction */
	unsigned char command;          /* command byte for card */
	} device;

#endif /* BACKER_DEVICE_H */
