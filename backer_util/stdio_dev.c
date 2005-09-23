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


/*
 * space_in_buffer(), bytes_in_buffer()
 *
 * Return the space and bytes available in the device I/O buffer.  Note
 * that space_in_buffer() + bytes_in_buffer() == device.size - 1.
 */

unsigned int space_in_buffer(bkr_device_t *device)
{
	if(device->tail > device->head)
		return(device->tail - device->head - 1);
	return(device->tail+device->size - device->head - 1);
}

unsigned int bytes_in_buffer(bkr_device_t *device)
{
	if(device->tail > device->head)
		return(device->head+device->size - device->tail);
	return(device->head - device->tail);
}


/*
 * The read function ensures that the "DMA" buffer is about 1/2 full while
 * write keeps it as empty as possible.  The flush function flushes first
 * the buffer and then stdout.
 *
 * These functions pass on any error codes returned by the file system.
 */

int bkr_device_reset(bkr_device_t *device, bkr_state_t direction)
{
	if(BKR_DENSITY(device->mode) == BKR_HIGH)
		device->bytes_per_line = BYTES_PER_LINE_HIGH;
	else
		device->bytes_per_line = BYTES_PER_LINE_LOW;

	if(BKR_VIDEOMODE(device->mode) == BKR_NTSC)
		device->frame_size = device->bytes_per_line * (LINES_PER_FIELD_NTSC * 2 + 1);
	else
		device->frame_size = device->bytes_per_line * LINES_PER_FIELD_PAL * 2;

	device->size = BKR_BUFFER_SIZE;
	if(direction == BKR_WRITING)
		device->size -= BKR_BUFFER_SIZE % device->frame_size;

	return(0);
}


int bkr_device_start_transfer(bkr_device_t *device, bkr_state_t direction, jiffies_t bailout)
{
	device->state = direction;
	device->head = 0;
	device->tail = 0;
	memset(device->buffer, 0, BKR_BUFFER_SIZE);
	return(0);
}


void bkr_device_stop_transfer(bkr_device_t *device)
{
	device->state = BKR_STOPPED;
	return;
}


int bkr_device_read(bkr_device_t *device, unsigned int length)
{
	int  tmp, desired;

	desired = device->size / 2 - bytes_in_buffer(device);
	if((int) desired <= 0)
		return(0);

	if(device->head + desired > device->size)
		{
		tmp = fread(device->buffer + device->head, 1, device->size - device->head, stdin);
		device->head += tmp;
		if(device->head != 0)
			goto done;
		desired -= tmp;
		}
	device->head += fread(device->buffer + device->head, 1, desired, stdin);

	done:
	if(bytes_in_buffer(device) >= length)
		return(0);
	if(feof(stdin))
		return(-EPIPE);
	if(ferror(stdin))
		return(-errno);
	return(-EAGAIN);
}


int bkr_device_write(bkr_device_t *device, unsigned int length)
{
	if(device->tail + bytes_in_buffer(device) >= device->size)
		{
		device->tail += fwrite(device->buffer + device->tail, 1, device->size - device->tail, stdout);
		if(device->tail < device->size)
			goto done;
		device->tail = 0;
		}
	device->tail += fwrite(device->buffer + device->tail, 1, bytes_in_buffer(device), stdout);

	done:
	if(space_in_buffer(device) >= length)
		return(0);
	if(ferror(stdout))
		return(-errno);
	return(-EAGAIN);
}


int bkr_device_flush(bkr_device_t *device)
{
	bkr_device_write(device, 0);
	fflush(stdout);
	return(0);
}
