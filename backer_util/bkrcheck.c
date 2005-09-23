/*
 * bkrcheck
 *
 * Command line utility for checking the health of the Backer device.
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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/ioctl.h>
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
struct bkrformat format;
struct bkrconfig config;


int main(int argc, char *argv[])
{
	int  i;
	struct termios oldterm, newterm;
	char  *device = "/dev/backer";
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
			device = optarg;
			break;

			case 'h':
			default:
			puts(
	"Usage:  bkrcheck [options] [filname]\n" \
	"the following options are recognized:\n" \
	"	-f devname   Use device devname (default /dev/backer)\n" \
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

	if(size >= 0)
		outfile = open(device, O_RDWR);
	else
		outfile = open(device, O_WRONLY);
	if(outfile < 0)
		{
		perror(PROGRAM_NAME);
		exit(0);
		}
	ioctl(outfile, BKRIOCGETMODE, &config);
	ioctl(outfile, BKRIOCGETFORMAT, &format);

	puts("\nCurrent Device Mode:");
	bkr_display_mode(config.mode, config.timeout);

	/*
	 * If we are checking a transfer time, do so and quit.
	 */

	if(size >= 0)
		{
		time = rint((double) size / format.sector_capacity / ((BKR_VIDEOMODE(config.mode) == BKR_NTSC) ? 60 : 50));
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

	switch(BKR_FORMAT(config.mode))
		{
		case BKR_FMT:
		gen_formated();
		break;

		case BKR_RAW:
		gen_raw();
		break;
		}

	/*
	 * Dump data until 'q' is pressed.
	 */

	puts("Press 'q' to quit.");
	do
		write(outfile, data, length);
	while(fgetc(stdin) != 'q');

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
	int i;

	printf("\nFormat Parameters:\n" \
	       "\tBuffer size:       %u bytes (%u sectors)\n" \
	       "\tSector size:       %u bytes\n" \
	       "\tSector leader:     %u bytes\n" \
	       "\tSector trailer:    %u bytes\n" \
	       "\tInterleave ratio:  %u:1\n" \
	       "\tBlock size:        %u bytes\n" \
	       "\tParity size:       %u bytes (max %u errors per block)\n" \
	       "\tBlock capacity:    %u bytes\n" \
	       "\tSector capacity:   %u bytes (%4.1f%% net efficiency)\n" \
	       "\tData rate:         %u bytes/second\n\n" \
	       "Writing '\\0's.  ",
	       format.buffer_size, format.buffer_size/format.sector_size,
	       format.sector_size,
	       format.leader,
	       format.trailer,
	       format.interleave,
	       format.block_size,
	       format.block_parity, format.block_parity/2,
	       format.block_capacity,
	       format.sector_capacity, 100.0*format.sector_capacity/format.sector_size,
	       format.sector_capacity * ((BKR_VIDEOMODE(config.mode) == BKR_NTSC) ? 60 : 50));

	length = format.sector_capacity;

	data = (unsigned char *) malloc(length);

	for(i = 0; i < length; i++)
		data[i] = 0;
}


/*
 * Raw mode test
 */

void gen_raw(void)
{
	int i, j;
	int  bytes_per_line;

	if(BKR_DENSITY(config.mode) == BKR_HIGH)
		bytes_per_line = BYTES_PER_LINE_HIGH;
	else
		bytes_per_line = BYTES_PER_LINE_LOW;

	printf("\nFormat Parameters:\n" \
	       "\tBuffer size:       %u bytes (%u sectors)\n" \
	       "\tSector size:       %u bytes\n",
	       format.buffer_size, format.buffer_size/format.sector_size,
	       format.sector_size);

	switch(BKR_VIDEOMODE(config.mode))
		{
		case BKR_PAL:
		length = LINES_PER_FIELD_PAL * bytes_per_line * 2;
		data = (unsigned char *) malloc(length);
		for(i = 0; i < length/2; i++)
			data[i] = i/bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/bytes_per_line + 1;
		break;

		case BKR_NTSC:
		length = LINES_PER_FIELD_NTSC * bytes_per_line * 2 + bytes_per_line;
		data = (unsigned char *) malloc(length);
		for(i = 0; i < (length+bytes_per_line)/2; i++)
			data[i] = i/bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/bytes_per_line + 1;
		break;
		}
}
