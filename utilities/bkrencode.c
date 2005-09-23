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
#include "stdio_dev.h"

#define  PROGRAM_NAME    "bkrencode"

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)

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
	int  skip_bad = 0, time_only = 0, dump_status = 0, verbose = 0;
	bkr_state_t  direction;
	char  *devname = DEFAULT_DEVICE;
	struct mtget  mtget = { mt_dsreg : DEFAULT_MODE };
	bkr_device_t  device;
	bkr_stream_t  stream;
	char  msg[100];
	unsigned long  data_length = 0;

	/*
	 * Some setup stuff
	 */

	device.state = BKR_STOPPED;
	device.ops = &bkr_stdio_ops;
	memcpy(device.format_info, BKR_FORMAT_INFO_INITIALIZER, sizeof(BKR_FORMAT_INFO_INITIALIZER));
	stream.buffer = NULL;
	direction = BKR_WRITING;

	/*
	 * Process command line options
	 */

	while((result = getopt(argc, argv, "d::f::hstuvD:F:V:")) != EOF)
		switch(result)
			{
			case 'd':
			dump_status = 1;
			if(optarg != NULL)
				if(*optarg == 's')
					break;
			fcntl(STDERR_FILENO, F_SETFL, O_NONBLOCK);
			break;

			case 'f':
			mtget.mt_dsreg = -1;
			if(optarg != NULL)
				devname = optarg;
			break;

			case 's':
			skip_bad = 1;
			break;

			case 't':
			time_only = 1;
			break;

			case 'u':
			direction = BKR_READING;
			break;

			case 'v':
			verbose = 1;
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
	"	-D<h/l>   Set the data rate to high or low\n" \
	"	-F<s/e>   Set the data format to SP or EP\n" \
	"	-V<p/n>   Set the video mode to PAL or NTSC\n" \
	"	-d[s]     Dump unencoding status info to stderr [synchronously]\n" \
	"	-f [dev]  Get the format to use from the Backer device dev\n" \
	"	          (default " DEFAULT_DEVICE ")\n" \
	"	-h        Display usage message\n" \
	"	-s        Skip bad sectors\n" \
	"	-t        Compute time only (do not encode or decode data)\n" \
	"	-u        Unencode tape data (default is to encode)\n" \
	"	-v        Be verbose\n", stderr);
			exit(1);
			}

	/*
	 * Do more setup stuff.
	 */

	if(mtget.mt_dsreg == -1)
		{
		sprintf(msg, PROGRAM_NAME ": %s", devname);
		if((tmp = open(devname, O_RDONLY)) < 0)
			{
			perror(msg);
			exit(1);
			}
		if(ioctl(tmp, MTIOCGET, &mtget) < 0)
			{
			perror(msg);
			exit(1);
			}
		close(tmp);
		}
	device.mode = mtget.mt_dsreg;

	if(!dump_status && verbose)
		{
		fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n",
		        (direction == BKR_READING) ? "DECODING" : "ENCODING");
		bkr_display_mode(stderr, device.mode);
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

	bkr_format_reset(&device, &stream, device.mode, direction);
	device.ops->start(&device, direction);
	switch(direction)
		{
		case BKR_READING:
		while(1)
			{
			errno = 0;
			result = stream.read(&device, &stream);
			if(result < 0)
				{
				if(result == -EPIPE)
					break;
				if((result == -ENODATA) && skip_bad)
					{
					if(verbose)
						fprintf(stderr, PROGRAM_NAME ": skipping bad sector (%d total)\n", skip_bad++);
					continue;
					}
				sprintf(msg, PROGRAM_NAME ": stdin (at sector %+08d)", stream.header.number);
				errno = -result;
				perror(msg);
				exit(1);
				}
			if(dump_status)
				{
				fprintf(stderr,
				        "Current State   : READING\n"
				        "Current Mode    : %u\n"
				        "Sector Number   : %+08d\n"
				        "Byte Errors     : %u\n"
				        "In Worst Block  : %u / %u\n"
				        "Recently        : %u\n"
				        "Bad Blocks      : %u\n"
				        "Framing Errors  : %u\n"
				        "Overrun Errors  : %u\n"
				        "Underflows      : %u\n"
				        "Worst Key       : %u / "STRINGIFY(BKR_MAX_KEY_WEIGHT)"\n"
				        "Closest Non-Key : %u\n"
				        "Smallest Field  : %u\n"
				        "Largest Field   : %u\n"
				        "I/O Buffer      : %u / %u\n",
				        device.mode,
				        stream.header.number,
				        stream.health.total_errors,
				        stream.errors.symbol, stream.rs_format.parity,
				        stream.errors.recent_symbol,
				        stream.errors.block,
				        stream.errors.frame,
				        stream.errors.overrun,
				        stream.errors.underrun,
				        stream.health.worst_key,
				        stream.health.best_nonkey,
				        stream.health.smallest_field,
				        stream.health.largest_field,
				        bytes_in_buffer(device.io_head, device.io_tail, device.io_size), device.io_size);
				stream.errors.recent_symbol = 0;
				}
			result = stream.end - stream.pos;
			if(result == 0)	/* EOF? */
				break;
			data_length += result;
			if(!time_only)
				while(stream.pos < stream.end)
					{
					stream.pos += fwrite(stream.pos, 1, stream.end - stream.pos, stdout);
					if(ferror(stdout))
						{
						perror(PROGRAM_NAME": stdout");
						exit(1);
						}
					}
			}
		if(time_only)
			fprintf(stdout, PROGRAM_NAME ": decoded recording length: %lu bytes\n", data_length);
		fflush(stdout);
		break;

		case BKR_WRITING:
		while(!feof(stdin) & !got_sigint)
			{
			errno = 0;
			while((stream.pos < stream.end) && !feof(stdin))
				{
				stream.pos += fread(stream.pos, 1, stream.end - stream.pos, stdin);
				if(ferror(stdin))
					{
					perror(PROGRAM_NAME": stdin");
					break;
					}
				}
			if(!time_only)
				{
				result = stream.write(&device, &stream);
				if(result < 0)
					{
					errno = -result;
					perror(PROGRAM_NAME": stdout");
					exit(1);
					}
				}
			else
				stream.pos = stream.buffer;
			data_length++;
			}
		if(!time_only)
			{
			while(bkr_sector_write_eor(&device, &stream) == -EAGAIN);
			while(device.ops->flush(&device) == -EAGAIN);
			}

		tmp = (BKR_VIDEOMODE(device.mode) == BKR_PAL) ? 50 : 60;
		data_length += (BOR_LENGTH + EOR_LENGTH) * tmp;
		if(time_only)
			{
			fprintf(stdout, "%luh", data_length / 3600 / tmp);
			data_length %= 3600 * tmp;
			fprintf(stdout, " %lum", data_length / 60 / tmp);
			data_length %= 60 * tmp;
			fprintf(stdout, " %.1fs\n", (float) data_length / tmp);
			}
		break;

		default:
		break;
		}
	device.ops->stop(&device);

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
