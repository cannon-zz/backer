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
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "backer.h"
#include "backer_device.h"


/*
 * The read function ensures that the I/O buffer is about 1/2 full while
 * write keeps it as empty as possible.  The flush function flushes first
 * the buffer and then stdout.
 *
 * These functions pass on any error codes returned by the file system.
 */

static int bkr_stdio_start(bkr_device_t *device, bkr_state_t direction)
{
	device->buffer = (unsigned char *) malloc(device->size);
	if(device->buffer == NULL)
		return(-ENOMEM);

	device->state = direction;

	device->head = 0;
	device->tail = 0;
	memset(device->buffer, 0, device->size);

	return(0);
}


static void bkr_stdio_stop(bkr_device_t *device)
{
	device->state = BKR_STOPPED;
	free(device->buffer);
	return;
}


static int bkr_stdio_read(bkr_device_t *device, unsigned int length)
{
	int  tmp, desired;

	desired = device->size / 2 - bytes_in_buffer(device->head, device->tail, device->size);
	if(desired <= 0)
		return(0);

	if(desired > device->size - device->head)
		{
		tmp = fread(device->buffer + device->head, 1, device->size - device->head, stdin);
		device->head = (device->head + tmp) & BKR_OFFSET_MASK;
		if(device->head != 0)
			goto done;
		desired -= tmp;
		}
	device->head = (device->head + fread(device->buffer + device->head, 1, desired, stdin)) & BKR_OFFSET_MASK;

	done:
	if(bytes_in_buffer(device->head, device->tail, device->size) >= length)
		return(0);
	if(feof(stdin))
		return(-EPIPE);
	if(ferror(stdin))
		return(-errno);
	return(-EAGAIN);
}


static int bkr_stdio_write(bkr_device_t *device, unsigned int length)
{
	if(device->tail + bytes_in_buffer(device->head, device->tail, device->size) >= device->size)
		{
		device->tail += fwrite(device->buffer + device->tail, 1, device->size - device->tail, stdout);
		if(device->tail < device->size)
			goto done;
		device->tail = 0;
		}
	device->tail += fwrite(device->buffer + device->tail, 1, bytes_in_buffer(device->head, device->tail, device->size), stdout);

	done:
	if(space_in_buffer(device->head, device->tail, device->size) >= length)
		return(0);
	if(ferror(stdout))
		return(-errno);
	return(-EAGAIN);
}


static int bkr_stdio_flush(bkr_device_t *device)
{
	bkr_stdio_write(device, 0);
	fflush(stdout);
	return(0);
}


/*
 * Device operations.
 */

bkr_device_ops_t  stdio_ops =
	{
	NULL, NULL,
	bkr_stdio_start,
	bkr_stdio_stop,
	bkr_stdio_read,
	bkr_stdio_write,
	bkr_stdio_flush
	};
