/*
 * bkrcheck
 *
 * Command line utility for checking the health of the Backer device.
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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>

#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/poll.h>
#include <sys/termios.h>

#include "backer.h"
#include "backer_fmt.h"
#include "bkr_disp_mode.h"

#define  PROGRAM_NAME  "bkrcheck"

/*
 * To hell with it... just make stuff global.
 */

int  length;
unsigned char  *data;
struct mtget  mtget;
bkr_format_info_t  format_info[] = BKR_FORMAT_INFO_INITIALIZER;
bkr_format_info_t  *fmt;
char  *devname = DEFAULT_DEVICE;


/*
 * Formated mode test
 */

void gen_formated(void)
{
	int  frames, sector_capacity;

	frames = BKR_BUFFER_SIZE/fmt->frame_size;
	sector_capacity = bkr_sector_capacity(fmt);

	printf("\nFormat Parameters:\n" \
	       "\tBuffer capacity:   %u frames (%.1f seconds)\n" \
	       "\tVideo field size:  %u bytes\n" \
	       "\tSector capacity:   %u bytes (%4.1f%% net efficiency)\n" \
	       "\tData rate:         %u bytes/second\n" \
	       "\nWriting '\\0's to %s...  ",
	       frames, frames / ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 30.0 : 25.0),
	       fmt->field_size,
	       sector_capacity, 100.0*sector_capacity/fmt->field_size,
	       sector_capacity * ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 60 : 50),
	       devname);

	length = sector_capacity;
	data = (unsigned char *) malloc(length);
	memset(data, 0, length);
}


/*
 * Raw mode test
 */

void gen_raw(void)
{
	int i, j;

	i = BKR_BUFFER_SIZE/fmt->frame_size;

	printf("\nFormat Parameters:\n" \
	       "\tBuffer capacity:   %u frames (%.1f seconds)\n" \
	       "\tVideo frame size:  %u bytes\n" \
	       "\tData rate:         %u bytes/second\n" \
	       "\nWriting test pattern to %s...  ",
	       i, i / ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 30.0 : 25.0),
	       fmt->frame_size,
	       fmt->frame_size * ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 30 : 25),
	       devname);

	length = fmt->frame_size;
	data = (unsigned char *) malloc(length);
	switch(BKR_VIDEOMODE(mtget.mt_dsreg))
		{
		case BKR_PAL:
		for(i = 0; i < length/2; i++)
			data[i] = i/fmt->bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/fmt->bytes_per_line + 1;
		break;

		case BKR_NTSC:
		for(i = 0; i < (length+fmt->bytes_per_line)/2; i++)
			data[i] = i/fmt->bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/fmt->bytes_per_line + 1;
		break;
		}
}


/*
 * Entry point.
 */

int main(int argc, char *argv[])
{
	int  i;
	struct termios oldterm, newterm;
	int  term, outfile;
	char  msg[100];

	/*
	 * Opening banner
	 */

	puts("Backer Health Check.");

	/*
	 * Process command line options
	 */

	while((i = getopt(argc, argv, "f:h")) != EOF)
		switch(i)
			{
			case 'f':
			devname = optarg;
			break;

			case 'h':
			default:
			puts(
	"Usage:  bkrcheck [options]\n" \
	"the following options are recognized:\n" \
	"	-f devname   Use device devname (default " DEFAULT_DEVICE ")\n" \
	"	-h           Display usage");
			exit(1);
			break;
			}

	/*
	 * Open device and retrieve mode
	 */

	outfile = open(devname, O_WRONLY);
	if(outfile < 0)
		{
		sprintf(msg, PROGRAM_NAME ": %s", devname);
		perror(msg);
		exit(1);
		}
	ioctl(outfile, MTIOCGET, &mtget);

	fmt = &format_info[bkr_mode_to_format(mtget.mt_dsreg)];

	puts("\nDevice Mode:");
	bkr_display_mode(stderr, mtget.mt_dsreg);


	/*
	 * Adjust terminal mode
	 */

	if((term = open("/dev/tty", O_RDWR)) < 0)
		{
		perror(PROGRAM_NAME ": /dev/tty");
		exit(1);
		}
	ioctl(term, TCGETS, &oldterm);
	newterm = oldterm;
	newterm.c_lflag &= ~(ICANON | ECHO);
	newterm.c_cc[VMIN] = newterm.c_cc[VTIME] = 0;
	ioctl(term, TCSETS, &newterm);

	/*
	 * Generate test pattern
	 */

	if(BKR_FORMAT(mtget.mt_dsreg) == BKR_RAW)
		gen_raw();
	else
		gen_formated();

	/*
	 * Dump data until 'q' is pressed.
	 */

	sprintf(msg, PROGRAM_NAME ": %s", devname);
	puts("Press 'q' to quit.");
	do
		{
		if(write(outfile, data, length) < 0)
			{
			perror(msg);
			break;
			}
		}
	while(fgetc(stdin) != 'q');

	/*
	 * Clean up and quit
	 */

	puts("Please wait...");
	ioctl(term, TCSETS, &oldterm);
	close(term);
	close(outfile);
	free(data);
	exit(0);
}
