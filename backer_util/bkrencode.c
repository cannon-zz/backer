/*
 * bkrencode
 *
 * Tape data encoder/decoder for the Backer tape device.
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"
#include "bkr_disp_mode.h"

#define  PROGRAM_NAME    "bkrencode"
#define  DEFAULT_MODE    (BKR_NTSC | BKR_LOW | BKR_FMT | BKR_SP)
#define  BUFFER_SIZE     16384

/*
 * Function prototypes
 */

void sigint_handler(int);


/*
 * Global data
 */

unsigned int  got_sigint;


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  tmp, result;
	direction_t  direction;
	char  *devname = DEFAULT_DEVICE;
	struct mtget  mtget;

	/*
	 * Some setup stuff
	 */

	device.direction = STOPPED;
	sector.data = NULL;
	direction = WRITING;
	mtget.mt_dsreg = DEFAULT_MODE;

	if((device.buffer = (unsigned char *) malloc(BUFFER_SIZE)) == NULL)
		{
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(-1);
		}

	/*
	 * Process command line options
	 */

	while((result = getopt(argc, argv, "ad:f:hs:uv:")) != EOF)
		switch(result)
			{
			case 'u':
			direction = READING;
			break;

			case 'a':
			mtget.mt_dsreg = -1;
			break;

			case 'f':
			devname = optarg;
			break;

			case 'd':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_DENSITY(-1);
			switch(tolower(optarg[0]))
				{
				case 'h':
				mtget.mt_dsreg |= BKR_HIGH;
				break;
				case 'l':
				mtget.mt_dsreg |= BKR_LOW;
				break;
				default:
				mtget.mt_dsreg |= BKR_DENSITY(DEFAULT_MODE);
				break;
				}
			break;

			case 's':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_SPEED(-1);
			switch(tolower(optarg[0]))
				{
				case 's':
				mtget.mt_dsreg |= BKR_SP;
				break;
				case 'e':
				mtget.mt_dsreg |= BKR_EP;
				break;
				default:
				mtget.mt_dsreg |= BKR_SPEED(DEFAULT_MODE);
				break;
				}
			break;

			case 'v':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_VIDEOMODE(-1);
			switch(tolower(optarg[0]))
				{
				case 'n':
				mtget.mt_dsreg |= BKR_NTSC;
				break;
				case 'p':
				mtget.mt_dsreg |= BKR_PAL;
				break;
				default:
				mtget.mt_dsreg |= BKR_VIDEOMODE(DEFAULT_MODE);
				break;
				}
			break;

			case 'h':
			default:
			fputs(
	"Backer tape data encoder/unencoder.\n" \
	"Usage: " PROGRAM_NAME " [options...]\n" \
	"The following options are recognized:\n" \
	"	-v <p/n>  Set the video mode to PAL or NTSC\n" \
	"	-d <h/l>  Set the data rate to high or low\n" \
	"	-s <s/e>  Set the tape speed to SP or EP\n" \
	"	-a        Get the format from the current mode of the Backer device\n" \
	"	-f dev    Use device dev for the \"-a\" option (default " DEFAULT_DEVICE ")\n" \
	"	-u        Unencode tape data (default is to encode)\n" \
	"	-h        Display usage message\n", stderr);
			exit(0);
			}

	/*
	 * Do more setup stuff.
	 */

	if(mtget.mt_dsreg == -1)
		{
		if((tmp = open(devname, O_RDONLY)) < 0)
			{
			perror(PROGRAM_NAME);
			exit(-1);
			}
		if(ioctl(tmp, MTIOCGET, &mtget) < 0)
			{
			perror(PROGRAM_NAME);
			exit(-1);
			}
		close(tmp);
		}

	mtget.mt_dsreg = (mtget.mt_dsreg & ~BKR_FORMAT(-1)) | BKR_FMT;

	fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n",
	        (direction == READING) ? "DECODING" : "ENCODING");
	bkr_display_mode(mtget.mt_dsreg & ~BKR_FORMAT(-1));

	/*
	 * Grab interrupt signal so we can write a nice EOR before exiting
	 * on SIGINT.
	 */

	signal(SIGINT, sigint_handler);
	got_sigint = 0;

	/*
	 * Transfer data one block at a time until EOF is reached.
	 */

	setbuf(stderr, NULL);
	bkr_device_reset(mtget.mt_dsreg, BUFFER_SIZE);
	switch(direction)
		{
		case READING:
		bkr_format_reset(mtget.mt_dsreg, READING);
		bkr_device_start_transfer(READING);
		while(!feof(stdin))
			{
			errno = 0;
			result = block.read(0, 1);
			if(result == 0)
				break;
			if(result < 0)
				errno = -result;
			while((block.offset < block.end) && !errno)
				block.offset += fwrite(block.offset, 1, block.end - block.offset, stdout);
			if(errno)
				{
				perror(PROGRAM_NAME);
				exit(-1);
				}
			}
		break;

		case WRITING:
		bkr_format_reset(mtget.mt_dsreg, WRITING);
		bkr_device_start_transfer(WRITING);
		bkr_write_bor(1);
		while(!feof(stdin) & !got_sigint)
			{
			while((block.offset < block.end) && !feof(stdin))
				block.offset += fread(block.offset, 1, block.end - block.offset, stdin);
			result = block.write(0, 1);
			if(result < 0)
				errno = -result;
			if(errno)
				{
				perror(PROGRAM_NAME);
				exit(-1);
				}
			}
		bkr_write_eor(1);
		bkr_device_flush(1);
		break;

		default:
		}
	bkr_device_stop_transfer();
	fclose(stdout);

	exit(0);
}


/*
 * sigint_handler()
 *
 * SIGINT handler for cleanly terminating a recording.
 */

void sigint_handler(int num)
{
	got_sigint = 1;
}


/*
 * ============================================================================
 *
 *         "Device" I/O functions --- read stdin and write stdout.
 *
 * ============================================================================
 *
 * The read function must ensure that at least length bytes are available
 * starting at device.tail.  The write function must ensure that at least
 * length bytes of free space is available starting at device.head.  The
 * flush function function must ensure the buffer has been completely
 * commited to the output stream.
 *
 * These functions pass on any error codes returned by the file system.
 */

int  bkr_device_reset(int mode, unsigned max_buffer)
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

	device.size = max_buffer - max_buffer % device.frame_size;

	return(0);
}


int  bkr_device_start_transfer(direction_t direction)
{
	device.direction = direction;
	device.head = 0;
	device.tail = 0;
	memset(device.buffer, 0, BUFFER_SIZE);
	return(0);
}


void bkr_device_stop_transfer(void)
{
	device.direction = STOPPED;
	return;
}


int bkr_device_read(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	if(bytes_in_buffer() >= length)
		return(0);
	length -= bytes_in_buffer();

	if(device.head + length >= device.size)
		{
		result = fread(device.buffer + device.head, 1, device.size - device.head, stdin);
		if(result < device.size - device.head)
			return(-errno);
		length -= device.size - device.head;
		device.head = 0;
		}
	result = fread(device.buffer + device.head, 1, length, stdin);
	if(result < length)
		return(-errno);
	device.head += length;

	return(0);
}

int bkr_device_write(unsigned int length, f_flags_t f_flags, jiffies_t bailout)
{
	int  result;

	if(bytes_in_buffer() < length)
		length = bytes_in_buffer();

	if(device.tail + length >= device.size)
		{
		result = fwrite(device.buffer + device.tail, 1, device.size - device.tail, stdout);
		if(result < device.size - device.tail)
			return(-errno);
		length -= device.size - device.tail;
		device.tail = 0;
		}
	result = fwrite(device.buffer + device.tail, 1, length, stdout);
	if(result < length)
		return(-errno);
	device.tail += length;

	return(0);
}

int bkr_device_flush(jiffies_t bailout)
{
	bkr_device_write(bytes_in_buffer(), 0, bailout);
	fflush(stdout);
	return(0);
}
