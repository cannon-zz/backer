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
 * The read function ensures that the "DMA" buffer is about 1/2 full while
 * write keeps it as empty as possible.  The flush function flushes first
 * the buffer and then stdout.
 *
 * These functions pass on any error codes returned by the file system.
 */

int bkr_device_reset(int mode, bkr_state_t direction)
{
	if(BKR_DENSITY(mode) == BKR_HIGH)
		device.bytes_per_line = BYTES_PER_LINE_HIGH;
	else
		device.bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		device.frame_size = device.bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
	else
		device.frame_size = device.bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device.size = BKR_BUFFER_SIZE;
	if(direction == WRITING)
		device.size -= BKR_BUFFER_SIZE % device.frame_size;

	return(0);
}


int bkr_device_start_transfer(bkr_state_t direction, jiffies_t bailout)
{
	device.state = direction;
	device.head = 0;
	device.tail = 0;
	memset(device.buffer, 0, BKR_BUFFER_SIZE);
	return(0);
}


void bkr_device_stop_transfer(void)
{
	device.state = STOPPED;
	return;
}


int bkr_device_read(unsigned int length)
{
	int  tmp, desired;

	desired = device.size / 2 - bytes_in_buffer();
	if((int) desired <= 0)
		return(0);

	if(device.head + desired > device.size)
		{
		tmp = fread(device.buffer + device.head, 1, device.size - device.head, stdin);
		device.head += tmp;
		if(device.head != 0)
			goto done;
		desired -= tmp;
		}
	device.head += fread(device.buffer + device.head, 1, desired, stdin);

	done:
	if(bytes_in_buffer() >= length)
		return(0);
	if(feof(stdin))
		return(-EPIPE);
	if(ferror(stdin))
		return(-errno);
	return(-EAGAIN);
}


int bkr_device_write(unsigned int length)
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
	if(space_in_buffer() >= length)
		return(0);
	if(ferror(stdout))
		return(-errno);
	return(-EAGAIN);
}


int bkr_device_flush(void)
{
	bkr_device_write(0);
	fflush(stdout);
	return(0);
}
