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
	int  skip_bad = 0;
	bkr_state_t  direction;
	char  *devname = DEFAULT_DEVICE;
	struct mtget  mtget;

	/*
	 * Some setup stuff
	 */

	device.state = STOPPED;
	sector.buffer = NULL;
	direction = WRITING;
	mtget.mt_dsreg = DEFAULT_MODE;

	device.buffer = (unsigned char *) malloc(BKR_BUFFER_SIZE);
	if(device.buffer == NULL)
		{
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(-1);
		}

	/*
	 * Process command line options
	 */

	while((result = getopt(argc, argv, "D:F:f::hsuV:")) != EOF)
		switch(result)
			{
			case 'f':
			mtget.mt_dsreg = -1;
			if(optarg != NULL)
				devname = optarg;
			break;

			case 's':
			skip_bad = 1;
			break;

			case 'u':
			direction = READING;
			break;

			case 'D':
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

			case 'F':
			if(mtget.mt_dsreg == -1)
				break;
			mtget.mt_dsreg &= ~BKR_FORMAT(-1);
			switch(tolower(optarg[0]))
				{
				case 's':
				mtget.mt_dsreg |= BKR_SP;
				break;
				case 'e':
				mtget.mt_dsreg |= BKR_EP;
				break;
				default:
				mtget.mt_dsreg |= BKR_FORMAT(DEFAULT_MODE);
				break;
				}
			break;

			case 'V':
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
	"	-D <h/l>  Set the data rate to high or low\n" \
	"	-F <s/e>  Set the data format to SP or EP\n" \
	"	-V <p/n>  Set the video mode to PAL or NTSC\n" \
	"	-f [dev]  Get the format to use from the Backer device dev\n" \
	"                  (default " DEFAULT_DEVICE ")\n" \
	"	-h        Display usage message\n" \
	"	-s        Skip bad sectors\n" \
	"	-u        Unencode tape data (default is to encode)\n", stderr);
			exit(-1);
			}

	/*
	 * Do more setup stuff.
	 */

	setbuf(stderr, NULL);
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

	fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n",
	        (direction == READING) ? "DECODING" : "ENCODING");
	bkr_display_mode(mtget.mt_dsreg);

	/*
	 * Grab interrupt signal so we can write a nice EOR before exiting
	 * on SIGINT.
	 */

	if(direction == WRITING)
		{
		signal(SIGINT, sigint_handler);
		got_sigint = 0;
		}

	/*
	 * Transfer data one sector at a time until EOF is reached.
	 */

	bkr_device_reset(mtget.mt_dsreg, direction);
	bkr_format_reset(mtget.mt_dsreg, direction);
	bkr_device_start_transfer(direction, 0);
	switch(direction)
		{
		case READING:
		while(1)
			{
			errno = 0;
			result = sector.read();
			if(result > 0)
				{
				/* FIXME: some hacks to let us concat data streams
				sector.need_sequence_reset = 1;
				sector.found_data = 0;
				continue;
				*/
				break;
				}
			if(result < 0)
				{
				if(result == -EPIPE)
					break;
				if((result == -ENODATA) && skip_bad)
					{
					fprintf(stderr, PROGRAM_NAME": skipping bad sector (%d total)\n", skip_bad++);
					continue;
					}
				errno = -result;
				perror(PROGRAM_NAME": stdin");
				exit(-1);
				}
			while(sector.offset < sector.end)
				{
				sector.offset += fwrite(sector.offset, 1, sector.end - sector.offset, stdout);
				if(ferror(stdout))
					{
					perror(PROGRAM_NAME": stdout");
					exit(-1);
					}
				}
			}
		fflush(stdout);
		break;

		case WRITING:
		while(!feof(stdin) & !got_sigint)
			{
			errno = 0;
			while((sector.offset < sector.end) && !feof(stdin))
				{
				sector.offset += fread(sector.offset, 1, sector.end - sector.offset, stdin);
				if(ferror(stdin))
					{
					perror(PROGRAM_NAME": stdin");
					exit(-1);
					}
				}
			result = sector.write();
			if(result < 0)
				{
				errno = -result;
				perror(PROGRAM_NAME": stdout");
				exit(-1);
				}
			}
		do
			result = bkr_sector_write_eor();
		while(result == -EAGAIN);
		do
			result = bkr_device_flush();
		while(result == -EAGAIN);
		break;

		default:
		break;
		}
	bkr_device_stop_transfer();

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
