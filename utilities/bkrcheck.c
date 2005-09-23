/*
 * bkrcheck
 *
 * Command line utility for checking the health of the Backer device.
 *
 * Copyright (C) 2000,2001,2002  Kipp C. Cannon
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
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/timeb.h>
#include <sys/termios.h>

#include <backer.h>
#include <bkr_disp_mode.h>
#include <bkr_proc_io.h>
#include <bkr_puts.h>
#include <bkr_splp.h>
#include <bkr_splp_randomize.h>

#define  PROGRAM_NAME  "bkrcheck"


/*
 * Generate the text message
 */

void  generate_message(int mode, unsigned char *data, bkr_format_info_t *fmt)
{
	if(BKR_DENSITY(mode) == BKR_HIGH)
		bkr_puts("THIS IS A TEST", data, 104, 8, fmt);
	else {
		bkr_puts("THIS", data, 90, 8, fmt);
		bkr_puts(" IS ", data, 104, 8, fmt);
		bkr_puts("  A ", data, 118, 8, fmt);
		bkr_puts("TEST", data, 132, 8, fmt);
	}
}


/*
 * Formated mode test
 */

void gen_formated(int verbose, int mode, bkr_format_info_t *fmt, unsigned char **data, int *length)
{
	int  sector_capacity;

	sector_capacity = bkr_sector_capacity(*fmt);

	if(verbose)
		fprintf(stderr,
		        "\n" PROGRAM_NAME ": format parameters:\n" \
		        "\tVideo field size:  %u bytes\n" \
		        "\tSector capacity:   %u bytes (%4.1f%% net efficiency)\n" \
		        "\tData rate:         %u bytes/second\n\n",
		        fmt->field_size,
		        sector_capacity, 100.0*sector_capacity/fmt->field_size,
		        sector_capacity * ((BKR_VIDEOMODE(mode) == BKR_NTSC) ? 60 : 50));

	*length = sector_capacity;
	*data = (unsigned char *) malloc(*length);
	memset(*data, 0, *length);

	generate_message(mode, *data, fmt);
}

int write_formated_frame(FILE *file, void *data, int length, int sector_number)
{
	int result;

	bkr_splp_sector_randomize((u_int32_t *) data, length, sector_number);
	result = fwrite(data, length, 1, file);
	bkr_splp_sector_randomize((u_int32_t *) data, length, sector_number++);

	bkr_splp_sector_randomize((u_int32_t *) data, length, sector_number);
	result = fwrite(data, length, 1, file);
	bkr_splp_sector_randomize((u_int32_t *) data, length, sector_number);

	return(result);
}


/*
 * Raw mode test
 */

void gen_raw(int verbose, int mode, bkr_format_info_t *fmt, unsigned char **data, int *length)
{
	int i, j;

	if(verbose)
		fprintf(stderr,
		        "\n" PROGRAM_NAME ": format parameters:\n" \
		        "\tVideo frame size:  %u bytes\n" \
		        "\tData rate:         %u bytes/second\n\n",
		        fmt->frame_size,
		        fmt->frame_size * ((BKR_VIDEOMODE(mode) == BKR_NTSC) ? 30 : 25));

	*length = fmt->frame_size;
	*data = (unsigned char *) malloc(*length);
	switch(BKR_VIDEOMODE(mode)) {
		case BKR_PAL:
		for(i = 0; i < (*length)/2; i++)
			(*data)[i] = i/fmt->bytes_per_line + 1;
		for(j = 0; i < *length; i++, j++)
			(*data)[i] = j/fmt->bytes_per_line + 1;
		break;

		case BKR_NTSC:
		for(i = 0; i < ((*length)+fmt->bytes_per_line)/2; i++)
			(*data)[i] = i/fmt->bytes_per_line + 1;
		for(j = 0; i < *length; i++, j++)
			(*data)[i] = j/fmt->bytes_per_line + 1;
		break;
	}

	generate_message(mode, *data, fmt);
	generate_message(mode, *data + fmt->field_size + fmt->interlace, fmt);
}

int write_raw_frame(FILE *file, void *data, int length, int sector_number)
{
	return(fwrite(data, length, 1, file));
}


/*
 * Set the terminal for non-blocking input.
 */

struct termios  oldterm;

void adjust_terminal(void)
{
	struct termios  newterm;

	ioctl(STDIN_FILENO, TCGETS, &oldterm);
	newterm = oldterm;
	newterm.c_lflag &= ~(ICANON | ECHO);
	newterm.c_cc[VMIN] = newterm.c_cc[VTIME] = 0;
	ioctl(STDIN_FILENO, TCSETS, &newterm);
}

void restore_terminal(void)
{
	ioctl(STDIN_FILENO, TCSETS, &oldterm);
}



/*
 * Entry point.
 */

int main(int argc, char *argv[])
{
	int  i;
	int  length;
	int  verbose = 0;
	unsigned char  *data;
	int  (*write_func)(FILE *, void *, int, int);
	char  msg[100];
	struct mtget  mtget;
	int  mode = -1;
	FILE  *format_file;
	static bkr_format_info_t  format_info[] = BKR_FORMAT_INFO_INITIALIZER;
	bkr_format_info_t  *fmt;
	struct timeb  start, stop;
	float  framerate;

	/*
	 * Process command line options
	 */

	while((i = getopt(argc, argv, "D:F:f::hT:V:v")) != EOF)
		switch(i) {
			case 'f':
			if(optarg == NULL)
				optarg = DEFAULT_DEVICE;
			sprintf(msg, PROGRAM_NAME ": %s", optarg);
			if((i = open(optarg, O_RDONLY)) >= 0)
				if(ioctl(i, MTIOCGET, &mtget) >= 0) {
					close(i);
					mode = mtget.mt_dsreg;
					break;
				}
			perror(msg);
			exit(1);

			case 'T':
			sprintf(msg, PROGRAM_NAME ": cannot read format table from %s", optarg);
			format_file = fopen(optarg, "r");
			if(format_file != NULL)
				if(bkr_proc_read_format_table(format_file, format_info) >= 0) {
					fclose(format_file);
					break;
				}
			perror(msg);
			exit(1);

			case 'D':
			mode &= ~BKR_DENSITY(-1);
			switch(tolower(optarg[0])) {
				case 'h':
				mode |= BKR_HIGH;
				break;
				case 'l':
				mode |= BKR_LOW;
				break;
				default:
				mode |= BKR_DENSITY(-1);
				break;
			}
			break;

			case 'F':
			mode &= ~BKR_CODEC(-1);
			switch(tolower(optarg[0])) {
				case 'e':
				mode |= BKR_EP;
				break;
				case 'r':
				mode |= BKR_RAW;
				break;
				case 's':
				mode |= BKR_SP;
				break;
				default:
				mode |= BKR_CODEC(-1);
				break;
			}
			break;

			case 'V':
			mode &= ~BKR_VIDEOMODE(-1);
			switch(tolower(optarg[0])) {
				case 'n':
				mode |= BKR_NTSC;
				break;
				case 'p':
				mode |= BKR_PAL;
				break;
				default:
				mode |= BKR_VIDEOMODE(-1);
				break;
			}
			break;

			case 'v':
			verbose = 1;
			break;

			case 'h':
			default:
			fputs(
	"Usage:  bkrcheck [options]\n" \
	"the following options are recognized:\n" \
	"	-Dh      Set the data rate to high\n" \
	"	-Dl      Set the data rate to low\n" \
	"	-Fe      Set the data format to EP\n" \
	"	-Fs      Set the data format to SP\n" \
	"	-Vn      Set the video mode to NTSC\n" \
	"	-Vp      Set the video mode to PAL\n" \
	"	-f[dev]  Get the format to use from dev (default " DEFAULT_DEVICE ")\n" \
	"	-h       Display this usage message\n" \
	"	-Ttbl    Read format parameter table from file tbl\n" \
	"	-v       Be verbose\n", stderr);
			exit(1);
			break;
		}

	/*
	 * Retrieve the data format from stdout if needed.
	 */

	if(bkr_mode_to_format(mode) < 0) {
		if(ioctl(STDOUT_FILENO, MTIOCGET, &mtget) < 0) {
			perror(PROGRAM_NAME ": stdout");
			exit(1);
		}
		if(BKR_DENSITY(mode) == BKR_DENSITY(-1)) {
			mode &= ~BKR_DENSITY(-1);
			mode |= BKR_DENSITY(mtget.mt_dsreg);
		}
		if(BKR_CODEC(mode) == BKR_CODEC(-1)) {
			mode &= ~BKR_CODEC(-1);
			mode |= BKR_CODEC(mtget.mt_dsreg);
		}
		if(BKR_VIDEOMODE(mode) == BKR_VIDEOMODE(-1)) {
			mode &= ~BKR_VIDEOMODE(-1);
			mode |= BKR_VIDEOMODE(mtget.mt_dsreg);
		}
	}

	/*
	 * Opening banner
	 */

	if(verbose) {
		fputs(PROGRAM_NAME ": device mode:\n", stderr);
		bkr_display_mode(stderr, mode);
	}

	/*
	 * Generate test pattern
	 */

	fmt = &format_info[bkr_mode_to_format(mode)];
	if(BKR_CODEC(mode) == BKR_RAW) {
		gen_raw(verbose, mode, fmt, &data, &length);
		write_func = write_raw_frame;
	} else {
		gen_formated(verbose, mode, fmt, &data, &length);
		write_func = write_formated_frame;
	}

	/*
	 * Dump data until 'q' is pressed.
	 */

	adjust_terminal();
	fputs(PROGRAM_NAME ": writing test pattern...  press 'q' to quit.\n", stderr);
	ftime(&start);
	for(i = 0; fgetc(stdin) != 'q'; i += 2)
		if(write_func(stdout, data, length, i) < 1) {
			perror(PROGRAM_NAME ": stdout");
			restore_terminal();
			exit(1);
		}
	ftime(&stop);
	restore_terminal();

	if(BKR_CODEC(mode) != BKR_RAW)
		stop.time -= BOR_LENGTH + EOR_LENGTH;
	framerate = i/2 / (stop.time - start.time + (stop.millitm - start.millitm)*1e-3);

	/*
	 * Clean up and quit
	 */

	fprintf(stderr, PROGRAM_NAME ": device frame rate was %.1f frames/second\n", framerate);
	fputs(PROGRAM_NAME ": please wait...\n", stderr);
	free(data);
	exit(0);
}
