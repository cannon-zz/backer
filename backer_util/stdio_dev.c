/*
 * stdio_dev.c
 *
 * Implements a Backer device layer on stdin/stdout.
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
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"

/*
 * The read function ensures that the "DMA" buffer is as full as possible
 * while write keeps it as empty as possible.  The flush function flushes
 * first the buffer and then stdout.
 *
 * These functions pass on any error codes returned by the file system.
 */

int  bkr_device_reset(int mode)
{
	device.size = 0;

	switch(BKR_DENSITY(mode))
		{
		case BKR_HIGH:
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


int  bkr_device_start_transfer(direction_t direction)
{
	device.direction = direction;
	device.head = 0;
	device.tail = 0;
	memset(device.buffer, 0, device.alloc_size);
	return(0);
}


void bkr_device_stop_transfer(void)
{
	device.direction = STOPPED;
	return;
}


int bkr_device_read(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	if(feof(stdin) && (length > bytes_in_buffer()))
		return(-EPIPE);

	if(device.head + space_in_buffer() >= device.size)
		{
		device.head += fread(device.buffer + device.head, 1, device.size - device.head, stdin);
		if(device.head < device.size)
			goto done;
		device.head = 0;
		}

	device.head += fread(device.buffer + device.head, 1, space_in_buffer(), stdin);

	done:
	if(ferror(stdin))
		return(-errno);
	return(0);
}


int bkr_device_write(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	if(device.tail + bytes_in_buffer() >= device.size)
		{
		device.tail += fwrite(device.buffer + device.tail, 1, device.size - device.tail, stdout);
		if(device.tail < device.size)
			goto done;
		device.tail = 0;
		}

	device.tail += fwrite(device.buffer + device.tail, 1, bytes_in_buffer(), stdout);

	done:
	if(ferror(stdout))
		return(-errno);
	return(0);
}


int bkr_device_flush(jiffies_t bailout)
{
	bkr_device_write(1, 0, bailout);
	fflush(stdout);
	return(0);
}
