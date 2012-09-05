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


#include <getopt.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/timeb.h>


#include <backer.h>
#include <bkr_disp_mode.h>
#include <bkr_puts.h>
#include <bkr_splp.h>
#include <bkr_splp_randomize.h>


#define  PROGRAM_NAME  "bkrcheck"


/*
 * ============================================================================
 *
 *                             Format Information
 *
 * ============================================================================
 */


struct format {
	int field_size;
	int interlace;
	int sector_capacity;
	int bytes_per_line;
};


static struct format get_format(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct format) {1012,  4,  712,  4};
			case BKR_SP:
				return (struct format) {1012,  4,  826,  4};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct format) {1012,  4, 1012,  4};*/
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct format) {1220,  0,  884,  4};
			case BKR_SP:
				return (struct format) {1220,  0,  976,  4};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct format) {1220,  0, 1220,  4};*/
			}
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct format) {2530, 10, 1844, 10};
			case BKR_SP:
				return (struct format) {2530, 10, 2156, 10};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct format) {2530, 10, 2530, 10};*/
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct format) {3050,  0, 2284, 10};
			case BKR_SP:
				return (struct format) {3050,  0, 2614, 10};
			/* FIXME:  what to do about this? */
			/*case BKR_RAW:
				return (struct format) {3050,  0, 3050, 10};*/
			}
		}
	}

	return (struct format) {0,};
}


/*
 * ============================================================================
 *
 *                                Command Line
 *
 * ============================================================================
 */


struct options {
	int verbose;
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
};


static struct options default_options(void)
{
	struct options defaults = {
		.verbose = 0,
		.videomode = BKR_NTSC,
		.bitdensity = BKR_HIGH,
		.sectorformat = BKR_SP
	};

	return defaults;
}


static void usage(void)
{
	fputs(
	"Backer device output test utility.\n" \
	"Usage: " PROGRAM_NAME " [options]\n" \
	"the following options are recognized:\n" \
	"	-Dh      Set the data rate to high\n" \
	"	-Dl      Set the data rate to low\n" \
	"	-Fe      Set the data format to EP\n" \
	"	-Fs      Set the data format to SP\n" \
	"	-Vn      Set the video mode to NTSC\n" \
	"	-Vp      Set the video mode to PAL\n" \
	"	-h       Display this usage message\n" \
	"	-v       Be verbose\n", stderr);
}


static struct options parse_command_line(int *argc, char **argv[])
{
	struct options options = default_options();
	struct option long_options[] = {
		{"bit-density",	required_argument,	NULL,	'D'},
		{"sector-format",	required_argument,	NULL,	'F'},
		{"help",	no_argument,	NULL,	'h'},
		{"video-mode",	required_argument,	NULL,	'V'},
		{"verbose",	no_argument,	NULL,	'v'},
		{NULL,	0,	NULL,	0}
	};
	int c, index;

	opterr = 1;	/* enable error messages */
	do switch(c = getopt_long(*argc, *argv, "D:F:hV:v", long_options, &index)) {
	case 'D':
		switch(tolower(optarg[0])) {
		case 'h':
			options.bitdensity = BKR_HIGH;
			break;
		case 'l':
			options.bitdensity = BKR_LOW;
			break;
		default:
			usage();
			exit(1);
		}
		break;

	case 'F':
		switch(tolower(optarg[0])) {
		case 'e':
			options.sectorformat = BKR_EP;
			break;
		/* FIXME:  what to do about this? */
		/*case 'r':
			options.sectorformat = BKR_RAW;
			break;*/
		case 's':
			options.sectorformat = BKR_SP;
			break;
		default:
			usage();
			exit(1);
		}
		break;

	case 'V':
		switch(tolower(optarg[0])) {
		case 'n':
			options.videomode = BKR_NTSC;
			break;
		case 'p':
			options.videomode = BKR_PAL;
			break;
		default:
			usage();
			exit(1);
		}
		break;

	case 'h':
		usage();
		exit(1);

	case 'v':
		options.verbose = 1;
		break;

	case 0:
		/* option sets a flag */
		break;

	case -1:
		/* end of arguments */
		break;

	case '?':
		/* unrecognized option */
		usage();
		exit(1);

	case ':':
		/* missing argument for an option */
		usage();
		exit(1);

	default:
		/* FIXME: print bug warning */
		break;
	} while(c != -1);

	/* remove parsed arguments */
	*argc += optind;
	*argv += optind;

	return options;
}


/*
 * ============================================================================
 *
 *                                Text Message
 *
 * ============================================================================
 */


static void generate_message(struct options options, unsigned char *data, struct format format)
{
	struct bkr_puts_format puts_fmt = bkr_puts_get_format(options.videomode, options.bitdensity, options.sectorformat);

	if(options.bitdensity == BKR_HIGH)
		bkr_puts("THIS IS A TEST", data, 104, 8, &puts_fmt);
	else {
		bkr_puts("THIS", data, 90, 8, &puts_fmt);
		bkr_puts(" IS ", data, 104, 8, &puts_fmt);
		bkr_puts("  A ", data, 118, 8, &puts_fmt);
		bkr_puts("TEST", data, 132, 8, &puts_fmt);
	}
}


/*
 * ============================================================================
 *
 *                               Formated Mode
 *
 * ============================================================================
 */


static unsigned char *gen_formated(struct options options, struct format format, int *length)
{
	unsigned char *data;

	if(options.verbose)
		fprintf(stderr,
		        "\n" PROGRAM_NAME ": format parameters:\n" \
		        "\tVideo field size:  %u bytes\n" \
		        "\tSector capacity:   %u bytes (%4.1f%% net efficiency)\n" \
		        "\tData rate:         %u bytes/second\n\n",
		        format.field_size,
		        format.sector_capacity, 100.0*format.sector_capacity/format.field_size,
		        format.sector_capacity * ((options.videomode == BKR_NTSC) ? 60 : 50));

	*length = format.sector_capacity;
	data = malloc(*length);
	memset(data, 0, *length);

	generate_message(options, data, format);

	return data;
}


static int write_formated_frame(FILE *file, const unsigned char *data, int length, int sector_number)
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
 * ============================================================================
 *
 *                                  Raw Mode
 *
 * ============================================================================
 */


static unsigned char *gen_raw(struct options options, struct format format, int *length)
{
	unsigned char *data;
	int i, j;
	int frame_size = 2 * format.field_size + format.interlace;

	if(options.verbose)
		fprintf(stderr,
		        "\n" PROGRAM_NAME ": format parameters:\n" \
		        "\tVideo frame size:  %u bytes\n" \
		        "\tData rate:         %u bytes/second\n\n",
		        frame_size,
		        frame_size * ((options.videomode == BKR_NTSC) ? 30 : 25));

	data = malloc(frame_size);
	*length = frame_size;
	switch(options.videomode) {
	case BKR_PAL:
		for(i = 0; i < (*length)/2; i++)
			data[i] = i/format.bytes_per_line + 1;
		for(j = 0; i < *length; i++, j++)
			data[i] = j/format.bytes_per_line + 1;
		break;

	case BKR_NTSC:
		for(i = 0; i < ((*length)+format.bytes_per_line)/2; i++)
			data[i] = i/format.bytes_per_line + 1;
		for(j = 0; i < *length; i++, j++)
			data[i] = j/format.bytes_per_line + 1;
		break;
	}

	generate_message(options, data, format);
	generate_message(options, data + format.field_size + format.interlace, format);
	return data;
}


static int write_raw_frame(FILE *file, const unsigned char *data, int length, int sector_number)
{
	return(fwrite(data, length, 1, file));
}


/*
 * ============================================================================
 *
 *                              Terminal Control
 *
 * ============================================================================
 */


static struct termios oldterm;


static void adjust_terminal(void)
{
	struct termios newterm;

	ioctl(STDIN_FILENO, TCGETS, &oldterm);
	newterm = oldterm;
	newterm.c_lflag &= ~(ICANON | ECHO);
	newterm.c_cc[VMIN] = newterm.c_cc[VTIME] = 0;
	ioctl(STDIN_FILENO, TCSETS, &newterm);
}


static void restore_terminal(void)
{
	ioctl(STDIN_FILENO, TCSETS, &oldterm);
}


/*
 * ============================================================================
 *
 *                                Entry Point
 *
 * ============================================================================
 */


int main(int argc, char *argv[])
{
	struct options options;
	struct format format;
	struct timeb start, stop;
	int field;
	float framerate;
	unsigned char *data;
	int length;
	int (*write_func)(FILE *, const unsigned char *, int, int);


	/*
	 * Init
	 */


	options = parse_command_line(&argc, &argv);
	format = get_format(options.videomode, options.bitdensity, options.sectorformat);

	if(options.verbose) {
		fputs(PROGRAM_NAME ": device mode:\n", stderr);
		bkr_display_mode(stderr, options.videomode, options.bitdensity, options.sectorformat);
	}


	/*
	 * Generate test pattern
	 */


	/* FIXME: what to do about this? */
	if(/*options.sectorformat == BKR_RAW*/ 0) {
		data = gen_raw(options, format, &length);
		write_func = write_raw_frame;
	} else {
		data = gen_formated(options, format, &length);
		write_func = write_formated_frame;
	}


	/*
	 * Dump data until 'q' is pressed.
	 */


	fputs(PROGRAM_NAME ": writing test pattern...  press 'q' to quit.\n", stderr);
	adjust_terminal();
	ftime(&start);
	for(field = 0; fgetc(stdin) != 'q'; field += 2)
		if(write_func(stdout, data, length, field) < 1) {
			perror(PROGRAM_NAME ": stdout");
			restore_terminal();
			exit(1);
		}
	ftime(&stop);
	restore_terminal();

	framerate = field/2 / (stop.time - start.time + (stop.millitm - start.millitm)*1e-3);


	/*
	 * Clean up and quit
	 */


	/* FIXME:  what to do about this? */
	if(options.verbose /*&& (options.sectorformat == BKR_RAW)*/)
		fprintf(stderr, PROGRAM_NAME ": device frame rate was %.1f frames/second\n", framerate);
	free(data);
	exit(0);
}
