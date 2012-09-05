/*
 * bkrtest
 *
 * Command line utility for generating test patterns on the Backer device.
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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

#include "backer.h"
#include "bkr_aux_puts.h"
#include "bkr_disp_mode.h"

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
	unsigned int  offset = 0;
	struct termios oldterm, newterm;
	char  *aux;
	char  *device = "/dev/backer";
	int  term, outfile;
	char string[] = " TEST PATTERN " \
	                " !\"#$%&'()*+,-./0123456789:;<=>?" \
	                "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_" \
	                "`abcdefghijklmnopqrstuvwxyz{|}~";

	/*
	 * Opening banner
	 */

	puts("Backer test pattern generator.");

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
	"Usage:  bkrtest [options]\n" \
	"the following options are recognized:\n" \
	"	-f devname   Use device devname (default /dev/backer)\n" \
	"	-h           Display uages");
			exit(0);
			break;
			}

	/*
	 * Adjust terminal mode
	 */

	if((term = open("/dev/tty", O_RDWR)) < 0)
		exit(0);
	ioctl(term, TCGETS, &oldterm);
	newterm = oldterm;
	newterm.c_lflag &= ~(ICANON | ECHO);
	newterm.c_cc[VMIN] = newterm.c_cc[VTIME] = 0;
	ioctl(term, TCSETS, &newterm);

	/*
	 * Open device and retrieve current mode and format
	 */

	if((outfile = open(device, O_WRONLY)) < 0)
		{
		ioctl(term, TCSETS, &oldterm);
		exit(0);
		}
	ioctl(outfile, BKRIOCGETMODE, &config);
	ioctl(outfile, BKRIOCGETFORMAT, &format);

	puts("\nCurrent Device Mode:");
	bkr_display_mode(config.mode, config.timeout);

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

	aux = (char *) malloc(format.aux_length);

	bkr_aux_puts(&string[offset], aux, &format);
	ioctl(outfile, BKRIOCSETAUX, aux);

	do
		{
		switch(i = fgetc(stdin))
			{
			case 'j':
			offset = (offset == 0) ? strlen(string) : offset-1;
			break;

			case 'k':
			offset = (offset == strlen(string)) ? 0 : offset+1;
			break;
			}
		if(i > 0)
			{
			bkr_aux_puts(&string[offset], aux, &format);
			ioctl(outfile, BKRIOCSETAUX, aux);
			}
		write(outfile, data, length);
		}
	while(i != 'q');

	/*
	 * Clean up and quit
	 */

	ioctl(term, TCSETS, &oldterm);
	close(term);
	close(outfile);
	free(aux);
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
	       "\tBytes per line:    %u\n" \
	       "\tHeader length:     %u bytes\n" \
	       "\tFooter length:     %u bytes\n" \
	       "\tAux offset:        %u bytes from sector start\n" \
	       "\tAux length:        %u bytes\n" \
	       "\tSector efficiency: %4.1f%%\n" \
	       "\tBlock size:        %u bytes\n" \
	       "\tBlock capacity:    %u bytes\n" \
	       "\tECC size:          %u bytes\n"\
	       "\tData rate:         %u bytes/second (%4.1f%% net efficiency)\n\n" \
	       "Press 'j', 'k' to scroll text and 'q' to quit.\n",
	       format.buffer_size, format.buffer_size/format.sector_size,
	       format.sector_size,
	       format.bytes_per_line,
	       format.header_length,
	       format.footer_length,
	       format.aux_offset,
	       format.aux_length,
	       100.0 - 100.0*(format.header_length + format.footer_length + format.aux_length)/format.sector_size,
	       format.block_size,
	       format.block_capacity,
	       format.block_parity,
	       (format.sector_size - format.header_length - format.footer_length - format.aux_length)/format.block_size*format.block_capacity * ((BKR_VIDEOMODE(config.mode) == BKR_NTSC)?60:50),
	       100.0*(format.sector_size - format.header_length - format.footer_length - format.aux_length)/format.block_size*format.block_capacity /
	       format.sector_size);

	length = format.block_capacity * format.bytes_per_line * 2 - KEY_LENGTH;

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

	printf("\nFormat Parameters:\n" \
	       "\tBuffer size:       %u bytes (%u sectors)\n" \
	       "\tSector size:       %u bytes\n" \
	       "Press 'q' to quit.\n",
	       format.buffer_size, format.buffer_size/format.sector_size,
	       format.sector_size);

	switch(BKR_VIDEOMODE(config.mode))
		{
		case BKR_PAL:
		length = LINES_PER_FIELD_PAL * format.bytes_per_line * 2;
		data = (unsigned char *) malloc(length);
		for(i = 0; i < length; i++)
			data[i] = i/format.bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/format.bytes_per_line + 1;
		break;

		case BKR_NTSC:
		length = LINES_PER_FIELD_NTSC * format.bytes_per_line * 2 + format.bytes_per_line;
		data = (unsigned char *) malloc(length);
		for(i = 0; i < (length+format.bytes_per_line)/2; i++)
			data[i] = i/format.bytes_per_line + 1;
		for(j = 0; i < length; i++, j++)
			data[i] = j/format.bytes_per_line + 1;
		break;
		}
}
