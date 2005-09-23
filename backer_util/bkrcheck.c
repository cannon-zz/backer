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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/termios.h>

#include "backer.h"
#include "backer_fmt.h"
#include "bkr_disp_mode.h"

#define  PROGRAM_NAME  "bkrcheck"

/*
 * Function prototypes
 */

void  gen_formated(void);
void  gen_raw(void);

/*
 * To hell with it... just make stuff global.
 */

unsigned int  length;
unsigned char  *data;
struct mtget mtget;
struct bkrformat format;
char  *devname = DEFAULT_DEVICE;


int main(int argc, char *argv[])
{
	int  i;
	struct termios oldterm, newterm;
	long  size = -1;
	double  time;
	int  term, outfile;

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
	"Usage:  bkrcheck [options] [filname]\n" \
	"the following options are recognized:\n" \
	"	-f devname   Use device devname (default " DEFAULT_DEVICE ")\n" \
	"	-h           Display usage\n" \
	"	filename     Calculate tape required for filename (incl. BOR + EOR)");
			exit(0);
			break;
			}
	if(optind < argc)
		{
		outfile = open(argv[optind], O_RDONLY);
		if(outfile < 0)
			{
			perror(PROGRAM_NAME);
			exit(0);
			}
		size = lseek(outfile, 0, SEEK_END);
		close(outfile);
		}

	/*
	 * Open device and retrieve current mode and format
	 */

	outfile = open(devname, O_WRONLY);
	if(outfile < 0)
		{
		perror(PROGRAM_NAME);
		exit(0);
		}
	ioctl(outfile, MTIOCGET, &mtget);
	ioctl(outfile, BKRIOCGETFORMAT, &format);

	puts("\nCurrent Device Mode:");
	bkr_display_mode(mtget.mt_dsreg);

	/*
	 * If we are checking a transfer time, do so and quit.
	 */

	if(size >= 0)
		{
		time = rint((double) size / format.sector_capacity / ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 60 : 50));
		time += BOR_LENGTH + EOR_LENGTH;
		printf("\n%s will occupy %dh:", argv[optind], (int) floor(time / 3600.0));
		time = fmod(time, 3600.0);
		printf("%02dm:", (int) floor(time / 60.0));
		time = fmod(time, 60.0);
		printf("%02ds of tape.\n\n", (int) rint(time));
		exit(0);
		}

	/*
	 * Adjust terminal mode
	 */

	if((term = open("/dev/tty", O_RDWR)) < 0)
		{
		perror(PROGRAM_NAME);
		exit(0);
		}
	ioctl(term, TCGETS, &oldterm);
	newterm = oldterm;
	newterm.c_lflag &= ~(ICANON | ECHO);
	newterm.c_cc[VMIN] = newterm.c_cc[VTIME] = 0;
	ioctl(term, TCSETS, &newterm);

	/*
	 * Generate test pattern
	 */

	switch(BKR_FORMAT(mtget.mt_dsreg))
		{
		case BKR_RAW:
		gen_raw();
		break;

		default:
		gen_formated();
		break;
		}

	/*
	 * Dump data until 'q' is pressed.
	 */

	puts("Press 'q' to quit.");
	do
		i = write(outfile, data, length);
	while((i >= 0) && (fgetc(stdin) != 'q'));
	if(i < 0)
		perror(PROGRAM_NAME);

	/*
	 * Clean up and quit
	 */

	ioctl(term, TCSETS, &oldterm);
	close(term);
	close(outfile);
	free(data);
	exit(0);
}


/*
 * Formated mode test
 */

void gen_formated()
{
	printf("\nFormat Parameters:\n" \
	       "\tBuffer usage:      %u bytes (%u frames)\n" \
	       "\tVideo field size:  %u bytes\n" \
	       "\tInterleave ratio:  %u:1\n" \
	       "\tBlock size:        %u bytes\n" \
	       "\tParity bytes:      %u (max %u errors per block)\n" \
	       "\tSector capacity:   %u bytes (%4.1f%% net efficiency)\n" \
	       "\tData rate:         %u bytes/second\n\n" \
	       "Writing '\\0's to %s...  ",
	       format.buffer_size, format.buffer_size/format.video_size/2,
	       format.video_size,
	       format.interleave,
	       format.block_size,
	       format.block_parity, format.block_parity/2,
	       format.sector_capacity, 100.0*format.sector_capacity/format.video_size,
	       format.sector_capacity * ((BKR_VIDEOMODE(mtget.mt_dsreg) == BKR_NTSC) ? 60 : 50),
	       devname);

	length = format.sector_capacity;
	data = (unsigned char *) malloc(length);
	memset(data, 0x55, length);
}


/*
 * Raw mode test
 */

void gen_raw(void)
{
	int i, j;
	int  bytes_per_line;

	if(BKR_DENSITY(mtget.mt_dsreg) == BKR_HIGH)
		bytes_per_line = BYTES_PER_LINE_HIGH;
	else
		bytes_per_line = BYTES_PER_LINE_LOW;

	printf("\nFormat Parameters:\n" \
	       "\tBuffer usage:      %u bytes (%u frames)\n" \
	       "\tVideo frame size:  %u bytes\n",
	       format.buffer_size, format.buffer_size/format.video_size,
	       format.video_size);

	length = format.video_size;
	data = (unsigned char *) malloc(length);
	switch(BKR_VIDEOMODE(mtget.mt_dsreg))
		{
		case BKR_PAL:
		for(i = 0; i < length/2; i++)
			data[i] = i/bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/bytes_per_line + 1;
		break;

		case BKR_NTSC:
		for(i = 0; i < (length+bytes_per_line)/2; i++)
			data[i] = i/bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/bytes_per_line + 1;
		break;
		}
}
