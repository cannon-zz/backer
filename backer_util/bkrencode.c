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
	int  skip_bad = 0, dump_status = 0;
	bkr_state_t  direction;
	char  *devname = DEFAULT_DEVICE;
	struct mtget  mtget = { mt_dsreg : DEFAULT_MODE };
	bkr_device_t  device;
	bkr_sector_t  sector;

	/*
	 * Some setup stuff
	 */

	device.state = BKR_STOPPED;
	sector.buffer = NULL;
	direction = BKR_WRITING;

	device.buffer = (unsigned char *) malloc(BKR_BUFFER_SIZE);
	if(device.buffer == NULL)
		{
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(1);
		}

	/*
	 * Process command line options
	 */

	while((result = getopt(argc, argv, "D:F:f::hsuV:")) != EOF)
		switch(result)
			{
			case 'd':
			dump_status = 1;
			break;

			case 'f':
			mtget.mt_dsreg = -1;
			if(optarg != NULL)
				devname = optarg;
			break;

			case 's':
			skip_bad = 1;
			break;

			case 'u':
			direction = BKR_READING;
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
	/*
	"	-d        Dump status info to stderr (unencoding only)\n" \
	*/
	"	-f [dev]  Get the format to use from the Backer device dev\n" \
	"                  (default " DEFAULT_DEVICE ")\n" \
	"	-h        Display usage message\n" \
	"	-s        Skip bad sectors\n" \
	"	-u        Unencode tape data (default is to encode)\n", stderr);
			exit(1);
			}

	/*
	 * Do more setup stuff.
	 */

	if(mtget.mt_dsreg == -1)
		{
		if((tmp = open(devname, O_RDONLY)) < 0)
			{
			perror(PROGRAM_NAME);
			exit(1);
			}
		if(ioctl(tmp, MTIOCGET, &mtget) < 0)
			{
			perror(PROGRAM_NAME);
			exit(1);
			}
		close(tmp);
		}
	device.mode = mtget.mt_dsreg;

	if(!dump_status)
		{
		fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n",
		        (direction == BKR_READING) ? "DECODING" : "ENCODING");
		bkr_display_mode(device.mode);
		}

	/*
	 * Grab interrupt signal so we can write a nice EOR before exiting
	 * on SIGINT.
	 */

	if(direction == BKR_WRITING)
		{
		signal(SIGINT, sigint_handler);
		got_sigint = 0;
		}

	/*
	 * Transfer data one sector at a time until EOF is reached.
	 */

	bkr_device_reset(&device, direction);
	bkr_format_reset(&device, &sector);
	bkr_device_start_transfer(&device, direction);
	switch(direction)
		{
		case BKR_READING:
		while(1)
			{
			errno = 0;
			result = sector.read(&device, &sector);
			if(result > 0)
				{
				/* EOF */
				break;
				}
			if(dump_status)
				{
				fprintf(stderr, "Unit            : 0 (reading)\n"
				                "Current Mode    : %u\n"
				                "Sector Number   : %u\n"
				                "Byte Errors     : %u\n"
				                "In Worst Block  : %u / %u\n"
				                "Recently        : %u\n"
				                "Bad Blocks      : %u\n"
				                "Framing Errors  : %u\n"
				                "Overrun Errors  : %u\n"
				                "Underflows      : %u\n"
				                "Worst Key       : %u\n"
				                "Closest Non-Key : %u\n"
				                "Least Skipped   : %u\n"
				                "Most Skipped    : %u\n"
				                "DMA Buffer      : %u / %u\n",
				                device.mode,
				                sector.header.number,
				                sector.health.total_errors,
				                sector.errors.symbol, sector.rs_format.parity,
				                sector.errors.recent_symbol,
				                sector.errors.block,
				                sector.errors.frame,
				                sector.errors.overrun,
				                sector.errors.underflow,
				                sector.health.worst_key,
				                sector.health.best_nonkey,
				                sector.health.least_skipped,
				                sector.health.most_skipped,
				                bytes_in_buffer(&device), device.size);
				sector.errors.recent_symbol = 0;
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
				exit(1);
				}
			while(sector.offset < sector.end)
				{
				sector.offset += fwrite(sector.offset, 1, sector.end - sector.offset, stdout);
				if(ferror(stdout))
					{
					perror(PROGRAM_NAME": stdout");
					exit(1);
					}
				}
			}
		fflush(stdout);
		break;

		case BKR_WRITING:
		while(!feof(stdin) & !got_sigint)
			{
			errno = 0;
			while((sector.offset < sector.end) && !feof(stdin))
				{
				sector.offset += fread(sector.offset, 1, sector.end - sector.offset, stdin);
				if(ferror(stdin))
					{
					perror(PROGRAM_NAME": stdin");
					exit(1);
					}
				}
			result = sector.write(&device, &sector);
			if(result < 0)
				{
				errno = -result;
				perror(PROGRAM_NAME": stdout");
				exit(1);
				}
			}
		do
			result = bkr_sector_write_eor(&device, &sector);
		while(result == -EAGAIN);
		do
			result = bkr_device_flush(&device);
		while(result == -EAGAIN);
		break;

		default:
		break;
		}
	bkr_device_stop_transfer(&device);

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
