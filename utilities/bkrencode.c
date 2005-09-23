/*
 * bkrencode
 *
 * Tape data encoder/decoder for the Backer tape device.
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
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

#include <backer.h>
#include <bkr_disp_mode.h>
#include <bkr_frame.h>
#include <bkr_proc_io.h>
#include <bkr_stdio_dev.h>
#include <bkr_stream.h>
#include <bkr_splp.h>
#include <bkr_gcr.h>
#include <bkr_ecc2.h>

#define  PROGRAM_NAME    "bkrencode"

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)

static bkr_format_info_t format_tbl[] = BKR_FORMAT_INFO_INITIALIZER;


/*
 *==============================================================================
 *
 *             SIGINT handler for cleanly terminating a recording
 *
 *==============================================================================
 */

static unsigned int  got_sigint = 0;

static void sigint_handler(int num)
{
	got_sigint = 1;
	fputs(PROGRAM_NAME ": SIGINT: aborting, please wait...\n", stderr);
	signal(SIGINT, SIG_IGN);
}


/*
 *==============================================================================
 *
 *                       I/O Activity signalling stuff.
 *
 *==============================================================================
 */

static pthread_mutex_t  io_activity_lock;
static pthread_cond_t  io_activity;

static void callback(void *data)
{
	pthread_mutex_lock(&io_activity_lock);
	pthread_cond_signal(&io_activity);
	pthread_mutex_unlock(&io_activity_lock);
}


/*
 *==============================================================================
 *
 *                               Status Output
 *
 *==============================================================================
 */

static void do_status(struct bkr_stream_t *stream, bkr_frame_private_t *frame_private, bkr_splp_private_t *splp_private)
{
	struct bkr_proc_status_t  status;

	memcpy(status.state, "WRITING", sizeof("WRITING"));
	status.mode = stream->mode;
	status.sector_number = splp_private->sector_number;
	status.total_errors = splp_private->errors.bytes_corrected;
	status.worst_block = splp_private->errors.worst_block;
	status.parity = splp_private->rs_format.parity;
	status.recent_block = splp_private->errors.recent_block;
	status.bad_sectors = splp_private->errors.bad_sectors;
	status.bad_sector_runs = splp_private->errors.lost_runs;
	status.frame_errors = frame_private->frame_warnings;
	status.underrun_errors = splp_private->errors.duplicate_runs;
	status.worst_key = frame_private->worst_key;
	status.max_key_weight = stream->fmt.key_length;
	status.best_nonkey = frame_private->best_nonkey;
	status.smallest_field = frame_private->smallest_field;
	status.largest_field = frame_private->largest_field;
	status.bytes_in_buffer = bkr_stream_bytes(stream);
	status.buffer_size = bkr_stream_size(stream);

	bkr_proc_write_status(stderr, &status);
	splp_private->errors.recent_block = 0;
}


/*
 *==============================================================================
 *
 *                                Decode Loop
 *
 *==============================================================================
 */

static void decode(struct bkr_stream_t *stream, int dump_status, int ignore_bad, int time_only, int verbose)
{
	bkr_splp_private_t  *private = stream->private;
	int  result;
	unsigned long  data_length = 0;
	char  msg[100];

	while(1) {
		errno = 0;

		pthread_mutex_lock(&io_activity_lock);
		result = stream->ops.read(stream);
		if(result == -EAGAIN) {
			pthread_cond_wait(&io_activity, &io_activity_lock);
			pthread_mutex_unlock(&io_activity_lock);
			continue;
		}
		pthread_mutex_unlock(&io_activity_lock);

		if(result > 0) {
			data_length += result;
			ring_lock(stream->ring);
			if(time_only)
				_ring_drain(stream->ring, result);
			else if(fwrite_ring(stream->ring, result, 1, STDOUT_FILENO) < 1) {
				ring_unlock(stream->ring);
				perror(PROGRAM_NAME ": stdout");
				break;
			}
			ring_unlock(stream->ring);
			continue;
		}

		/* EOF */
		if(result == 0)
			break;

		/* error */
		sprintf(msg, PROGRAM_NAME ": stdin: decode failure at sector %+08d", private->sector_number);
		if(result != -EAGAIN) {
			errno = -result;
			perror(msg);
			exit(1);
		}
		if(verbose || !ignore_bad)
			fprintf(stderr, "%s\n", msg);
		if(!ignore_bad)
			exit(1);
	}

	if(time_only)
		fprintf(stdout, PROGRAM_NAME ": decoded recording length: %lu bytes\n", data_length);
}


/*
 *==============================================================================
 *
 *                                Encode Loop
 *
 *==============================================================================
 */

static void encode(struct bkr_stream_t *stream, bkr_frame_private_t *frame_private, bkr_splp_private_t *splp_private, int dump_status, int time_only)
{
	unsigned long  sector_count = 0;
	int  result;
	int  sps;

	while(!got_sigint) {
		errno = 0;

		pthread_mutex_lock(&io_activity_lock);
		result = stream->ops.write(stream);
		if(result == -EAGAIN) {
			pthread_cond_wait(&io_activity, &io_activity_lock);
			pthread_mutex_unlock(&io_activity_lock);
			continue;
		}
		pthread_mutex_unlock(&io_activity_lock);

		if(result < 0) {
			if(errno)
				perror(PROGRAM_NAME": stdout");
			break;
		}

		ring_lock(stream->ring);
		result = fread_ring(stream->ring, result, 1, STDIN_FILENO);
		ring_unlock(stream->ring);
		if(result < 1) {
			if(errno)
				perror(PROGRAM_NAME": stdin");
			break;
		}
		if(dump_status)
			do_status(stream, frame_private, splp_private);
		if(time_only) {
			_ring_fill(stream->ring, 0); /* FIXME: must advance to next sector */
			_ring_drain(stream->ring, _bytes_in_ring(stream->ring));
			sector_count++;
		}
	}

	if(time_only) {
		sps = bkr_fields_per_second(stream->mode);
		sector_count += (BOR_LENGTH + EOR_LENGTH) * sps;
		fprintf(stdout, "%luh", sector_count / 3600 / sps);
		sector_count %= 3600 * sps;
		fprintf(stdout, " %lum", sector_count / 60 / sps);
		sector_count %= 60 * sps;
		fprintf(stdout, " %.1fs\n", (float) sector_count / sps);
	}
}

/*
 *==============================================================================
 *
 *                                ENTRY POINT
 *
 *==============================================================================
 */

int main(int argc, char *argv[])
{
	int  tmp, result;
	int  dump_status = 0, ignore_bad = 0, time_only = 0, verbose = 0;
	int  mode = -1;
	bkr_direction_t  direction = BKR_WRITING;
	struct mtget  mtget;
	struct bkr_stream_t  *stream;
	char  msg[100];
	FILE  *format_file;
	const struct bkr_stream_ops_t  *stdio_ops, *frame_ops, *gcr_ops, *splp_ops, *ecc2_ops;
	bkr_frame_private_t  *frame_private;
	bkr_splp_private_t  *splp_private;

	/*
	 * Some setup stuff
	 */

	pthread_mutex_init(&io_activity_lock, NULL);
	pthread_cond_init(&io_activity, NULL);

	stdio_ops = bkr_stdio_dev_init();
	frame_ops = bkr_frame_codec_init();
	gcr_ops = bkr_gcr_codec_init();
	splp_ops = bkr_splp_codec_init();
	ecc2_ops = bkr_ecc2_codec_init();

	/*
	 * Process command line options
	 */

	while((result = getopt(argc, argv, "D:d::F:f::hsT:tuV:v")) != EOF)
		switch(result) {
			case 'd':
			dump_status = 1;
			if(optarg)
				if(*optarg == 's')
					break;
			fcntl(STDERR_FILENO, F_SETFL, O_NONBLOCK);
			break;

			case 'f':
			if(!optarg)
				optarg = DEFAULT_DEVICE;
			sprintf(msg, PROGRAM_NAME ": %s", optarg);
			if((tmp = open(optarg, O_RDONLY)) >= 0)
				if(ioctl(tmp, MTIOCGET, &mtget) >= 0) {
					close(tmp);
					mode = mtget.mt_dsreg;
					break;
				}
			perror(msg);
			exit(1);

			case 's':
			ignore_bad = 1;
			break;

			case 'T':
			sprintf(msg, PROGRAM_NAME ": cannot read format table from %s", optarg);
			format_file = fopen(optarg, "r");
			if(format_file)
				if(bkr_proc_read_format_table(format_file, format_tbl) >= 0) {
					fclose(format_file);
					break;
				}
			perror(msg);
			exit(1);

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

			case 'h':
			default:
			fputs(
	"Backer tape data encoder/unencoder.\n" \
	"Usage: " PROGRAM_NAME " [options...]\n" \
	"The following options are recognized:\n" \
	"	-Dh      Set the data rate to high\n" \
	"	-Dl      Set the data rate to low\n" \
	"	-Fe      Set the data format to EP\n" \
	"	-Fs      Set the data format to SP/LP\n" \
	"	-Vn      Set the video mode to NTSC\n" \
	"	-Vp      Set the video mode to PAL\n" \
	"	-d[s]    Dump status info to stderr [synchronously]\n" \
	"	-f[dev]  Get the format to use from dev (default " DEFAULT_DEVICE ")\n" \
	"	-Ttbl    Read the format parameter table from file tbl\n" \
	"	-h       Display this usage message\n" \
	"	-s       Skip bad sectors\n" \
	"	-t       Compute time only (do not encode or decode data)\n" \
	"	-u       Unencode tape data (default is to encode)\n" \
	"	-v       Be verbose\n", stderr);
			exit(1);
		}

	/*
	 * Retrieve mode from stdin/stdout if required.
	 */

	if(bkr_mode_to_format(mode) < 0) {
		if(direction == BKR_READING) {
			if(ioctl(STDIN_FILENO, MTIOCGET, &mtget) < 0) {
				perror(PROGRAM_NAME ": stdin");
				exit(1);
			}
		} else {
			if(ioctl(STDOUT_FILENO, MTIOCGET, &mtget) < 0) {
				perror(PROGRAM_NAME ": stdout");
				exit(1);
			}
		}
		if(BKR_DENSITY(mode) == BKR_DENSITY(-1)) {
			mode &= ~BKR_DENSITY(-1);
			mode |= BKR_DENSITY(mtget.mt_dsreg);
		}
		if((BKR_CODEC(mode) == BKR_CODEC(-1)) &&
		   (BKR_CODEC(mtget.mt_dsreg) != BKR_RAW)) {
			mode &= ~BKR_CODEC(-1);
			mode |= BKR_CODEC(mtget.mt_dsreg);
		}
		if(BKR_VIDEOMODE(mode) == BKR_VIDEOMODE(-1)) {
			mode &= ~BKR_VIDEOMODE(-1);
			mode |= BKR_VIDEOMODE(mtget.mt_dsreg);
		}
	}
	if(bkr_mode_to_format(mode) < 0) {
		fputs(PROGRAM_NAME ": ambiguous data stream format\n", stderr);
		exit(1);
	}


	/*
	 * Do more setup stuff.
	 */

	mode &= BKR_MODE_MASK;
	if(dump_status)
		verbose = 0;	/* verbosity interferes with status parsing */
	if(verbose) {
		fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n", (direction == BKR_READING) ? "DECODING" : "ENCODING");
		bkr_display_mode(stderr, mode);
	}

	/*
	 * Grab interrupt signal so we can write a nice EOR before exiting
	 * on SIGINT.
	 */

	if(direction == BKR_WRITING)
		signal(SIGINT, sigint_handler);

	/*
	 * Chain the appropriate codecs.
	 */

	stream = stdio_ops->new(NULL, mode, &format_tbl[bkr_mode_to_format(mode)]);
	stream = frame_ops->new(stream, mode, &format_tbl[bkr_mode_to_format(mode)]);
	frame_private = stream->private;
	if(BKR_CODEC(mode) == BKR_EP)
		stream = gcr_ops->new(stream, mode, &format_tbl[bkr_mode_to_format(mode)]);
	stream = splp_ops->new(stream, mode, &format_tbl[bkr_mode_to_format(mode)]);
	splp_private = stream->private;
	if(BKR_CODEC(mode) == BKR_EP)
		stream = ecc2_ops->new(stream, mode, &format_tbl[bkr_mode_to_format(mode)]);

	if(!stream) {
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(1);
	}

	bkr_stream_set_callback(stream, callback, NULL);

	/*
	 * Transfer data one sector at a time until EOF is reached.
	 */

	stream->ops.start(stream, direction);
	switch(direction) {
		case BKR_READING:
		decode(stream, dump_status, ignore_bad, time_only, verbose);
		break;

		case BKR_WRITING:
		encode(stream, frame_private, splp_private, dump_status, time_only);
		break;

		default:
		break;
	}
	while(stream->ops.release(stream) == -EAGAIN);

	pthread_cond_destroy(&io_activity);
	pthread_mutex_destroy(&io_activity_lock);

	exit(0);
}
