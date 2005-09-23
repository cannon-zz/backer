/*
 * bkrencode
 *
 * Tape data encoder/decoder for the Backer tape device.
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
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "backer.h"
#include "backer_device.h"
#include "backer_fmt.h"
#include "bkr_disp_mode.h"

#define  PROGRAM_NAME    "bkrencode"
#define  DEFAULT_DEVICE  "/dev/backer"

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
	int  tmp;
	char  *devname = DEFAULT_DEVICE;
	struct bkrconfig  config;

	/*
	 * Some setup stuff
	 */

	block.buffer = NULL;
	sector.aux = NULL;
	device.direction = O_WRONLY;

	config.mode = BKR_DEF_MODE;
	config.timeout = BKR_DEF_TIMEOUT;

	if((device.buffer = (unsigned char *) malloc(BKR_DEF_BUFFER_SIZE)) == NULL)
		{
		fputs(PROGRAM_NAME ": cannot allocate memory\n", stderr);
		exit(-1);
		}

	/*
	 * Process command line options
	 */

	while((tmp = getopt(argc, argv, "ad:f:hs:uv:")) != EOF)
		switch(tmp)
			{
			case 'u':
			device.direction = O_RDONLY;
			break;

			case 'a':
			config.mode = -1;
			break;

			case 'f':
			devname = optarg;
			break;

			case 'd':
			if(config.mode == -1)
				break;
			config.mode &= ~BKR_DENSITY(-1);
			switch(tolower(optarg[0]))
				{
				case 'h':
				config.mode |= BKR_HIGH;
				break;
				case 'l':
				config.mode |= BKR_LOW;
				break;
				default:
				config.mode |= BKR_DENSITY(BKR_DEF_MODE);
				break;
				}
			break;

			case 's':
			if(config.mode == -1)
				break;
			config.mode &= ~BKR_SPEED(-1);
			switch(tolower(optarg[0]))
				{
				case 's':
				config.mode |= BKR_SP;
				break;
				case 'e':
				config.mode |= BKR_EP;
				break;
				default:
				config.mode |= BKR_SPEED(BKR_DEF_MODE);
				break;
				}
			break;

			case 'v':
			if(config.mode == -1)
				break;
			config.mode &= ~BKR_VIDEOMODE(-1);
			switch(tolower(optarg[0]))
				{
				case 'n':
				config.mode |= BKR_NTSC;
				break;
				case 'p':
				config.mode |= BKR_PAL;
				break;
				default:
				config.mode |= BKR_VIDEOMODE(BKR_DEF_MODE);
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
	"	-a        Set the format from the current mode of the Backer device\n" \
	"	-f dev    Use the device named dev for the \"-a\" option\n" \
	"	-u        Unencode tape data (default is to encode)\n" \
	"	-h        Display usage message\n", stderr);
			exit(0);
			}

	/*
	 * Do more setup stuff.
	 */

	if(config.mode == -1)
		{
		if((tmp = open(devname, O_RDWR)) < 0)
			{
			perror(PROGRAM_NAME);
			exit(-1);
			}
		if(ioctl(tmp, BKRIOCGETMODE, &config) < 0)
			{
			perror(PROGRAM_NAME);
			exit(-1);
			}
		close(tmp);
		}

	config.mode = (config.mode & ~BKR_FORMAT(-1)) | BKR_FMT;

	fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n",
	        (device.direction == O_RDONLY) ? "DECODING" : "ENCODING");
	bkr_display_mode(config.mode & ~BKR_FORMAT(-1), -1);

	if(bkr_set_parms(config.mode, BKR_DEF_BUFFER_SIZE) < 0)
		{
		perror(PROGRAM_NAME);
		exit(-1);
		}

	bkr_format_reset();
	device.tail = 0;

	/*
	 * Grab interrupt signal so we can write a nice EOR before exiting.
	 */

	signal(SIGINT, sigint_handler);
	got_sigint = 0;

	/*
	 * Transfer data one block at a time until EOF is reached.
	 */

	setbuf(stderr, NULL);
	switch(device.direction)
		{
		case O_RDONLY:
		while(!feof(stdin))
			{
			tmp = block.read(0, 1);
			if(tmp == 0)
				break;
			if(tmp < 0)
				errno = -tmp;
			while(block.offset < block.end)
				block.offset += fwrite(block.offset, 1, block.end - block.offset, stdout);
			if(errno)
				{
				perror(PROGRAM_NAME);
				exit(-1);
				}
			}
		break;

		case O_WRONLY:
		bkr_write_bor(1);
		while(!feof(stdin) & !got_sigint)
			{
			while((block.offset < block.end) && !feof(stdin))
				block.offset += fread(block.offset, 1, block.end - block.offset, stdin);
			tmp = block.write(0, 1);
			if(tmp < 0)
				errno = -tmp;
			if(errno)
				{
				perror(PROGRAM_NAME);
				exit(-1);
				}
			}
		bkr_write_eor(1);
		break;
		}
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
