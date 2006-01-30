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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <gst/gst.h>

#include <backer.h>
#include <bkr_splp.h>

#define  PROGRAM_NAME    "bkrencode"


/*
 * ============================================================================
 *
 *                                 Parameters
 *
 * ============================================================================
 */

enum direction {
	ENCODING,
	DECODING
};


/*
 * ============================================================================
 *
 *             SIGINT handler for cleanly terminating a recording
 *
 * ============================================================================
 */

static unsigned int  got_sigint = 0;

static void sigint_handler(int num)
{
	got_sigint = 1;
	fputs(PROGRAM_NAME ": SIGINT: aborting, please wait...\n", stderr);
	signal(SIGINT, SIG_IGN);
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
	int time_only;
	int ignore_bad;
	int dump_status;
	int dump_status_async;
	enum direction direction;
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
};


static struct options default_options(void)
{
	struct options defaults = {
		.verbose = 0,
		.time_only = 0,
		.ignore_bad = 0,
		.dump_status = 0,
		.dump_status_async = 1,
		.direction = ENCODING,
		.videomode = BKR_NTSC,
		.bitdensity = BKR_HIGH,
		.sectorformat = BKR_SP
	};

	return defaults;
}


static void usage(void)
{
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
	"	-h       Display this usage message\n" \
	"	-s       Skip bad sectors\n" \
	"	-t       Compute time only (do not encode or decode data)\n" \
	"	-u       Unencode tape data (default is to encode)\n" \
	"	-v       Be verbose\n", stderr);
}


static struct options parse_command_line(int *argc, char **argv[])
{
	struct options options = default_options();
	struct option long_options[] = {
		{"bit-density",	required_argument,	NULL,	'D'},
		{"dump-status",	optional_argument,	NULL,	'd'},
		{"sector-format",	required_argument,	NULL,	'F'},
		{"help",	no_argument,	NULL,	'h'},
		{"skip-bad-sectors",	no_argument,	NULL,	's'},
		{"time-only",	no_argument,	NULL,	't'},
		{"unencode",	no_argument,	NULL,	'u'},
		{"video-mode",	required_argument,	NULL,	'V'},
		{"verbose",	no_argument,	NULL,	'v'},
		{NULL,	0,	NULL,	0}
	};
	int c, index;

	opterr = 1;	/* enable error messages */
	do switch(c = getopt_long(*argc, *argv, "D:d::F:hstuV:v", long_options, &index)) {
	case 'd':
		options.dump_status = 1;
		if(optarg) {
			if(tolower(optarg[0]) == 's')
				options.dump_status_async = 0;
			else {
				usage();
				exit(1);
			}
		}
		break;

	case 'h':
		usage();
		exit(1);

	case 's':
		options.ignore_bad = 1;
		break;

	case 't':
		options.time_only = 1;
		break;

	case 'u':
		options.direction = DECODING;
		break;

	case 'v':
		options.verbose = 1;
		break;

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

	if(options.dump_status)
		/* verbosity interferes with status parsing */
		options.verbose = 0;

	/* remove parsed arguments */
	*argc -= optind;
	*argv += optind;

	return options;
}


/*
 * ============================================================================
 *
 *                           Pipeline Construction
 *
 * ============================================================================
 */

static GstElement *encoder_pipeline(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	/* FIXME: in EP mode, an ecc2 encoder goes between the source and splp
	 * elements. */
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *source = gst_element_factory_make("fdsrc", "source");
	GstElement *splp = gst_element_factory_make("bkr_splpenc", "splp");
	GstElement *gcr = (f == BKR_EP) ? gst_element_factory_make("bkr_gcrenc", "gcr") : NULL;
	GstElement *frame = gst_element_factory_make("bkr_frameenc", "frame");
	GstElement *sink = gst_element_factory_make("fdsink", "sink");

	g_object_set(G_OBJECT(source), "fd", 0, NULL);
	g_object_set(G_OBJECT(sink), "fd", 1, NULL);

	g_object_set(G_OBJECT(frame), "videomode", v, NULL);
	g_object_set(G_OBJECT(frame), "bitdensity", d, NULL);
	g_object_set(G_OBJECT(frame), "sectorformat", f, NULL);

	if(f == BKR_EP) {
		g_object_set(G_OBJECT(gcr), "videomode", v, NULL);
		g_object_set(G_OBJECT(gcr), "bitdensity", d, NULL);
	}

	g_object_set(G_OBJECT(splp), "videomode", v, NULL);
	g_object_set(G_OBJECT(splp), "bitdensity", d, NULL);
	g_object_set(G_OBJECT(splp), "sectorformat", f, NULL);

	g_object_set(G_OBJECT(source), "blocksize", BKR_SPLPENC(splp)->format.capacity, NULL);

	if(f == BKR_EP) {
		gst_element_link_many(source, splp, gcr, frame, sink, NULL);
		gst_bin_add_many(GST_BIN(pipeline), source, splp, gcr, frame, sink, NULL);
	} else {
		gst_element_link_many(source, splp, frame, sink, NULL);
		gst_bin_add_many(GST_BIN(pipeline), source, splp, frame, sink, NULL);
	}

	return pipeline;
}


static GstElement *decoder_pipeline(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
{
	/* FIXME: in EP mode, an ecc2 encoder goes between the splp and sink
	 * elements. */
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *source = gst_element_factory_make("fdsrc", "source");
	GstElement *frame = gst_element_factory_make("bkr_framedec", "frame");
	GstElement *gcr = (f == BKR_EP) ? gst_element_factory_make("bkr_gcrdec", "gcr") : NULL;
	GstElement *splp = gst_element_factory_make("bkr_splpdec", "splp");
	GstElement *sink = gst_element_factory_make("fdsink", "sink");

	g_object_set(G_OBJECT(source), "fd", 0, NULL);
	g_object_set(G_OBJECT(sink), "fd", 1, NULL);

	g_object_set(G_OBJECT(frame), "videomode", v, NULL);
	g_object_set(G_OBJECT(frame), "bitdensity", d, NULL);
	g_object_set(G_OBJECT(frame), "sectorformat", f, NULL);

	if(f == BKR_EP) {
		g_object_set(G_OBJECT(gcr), "videomode", v, NULL);
		g_object_set(G_OBJECT(gcr), "bitdensity", d, NULL);
	}

	g_object_set(G_OBJECT(splp), "videomode", v, NULL);
	g_object_set(G_OBJECT(splp), "bitdensity", d, NULL);
	g_object_set(G_OBJECT(splp), "sectorformat", f, NULL);

	if(f == BKR_EP) {
		gst_element_link_many(source, frame, gcr, splp, sink, NULL);
		gst_bin_add_many(GST_BIN(pipeline), source, frame, gcr, splp, sink, NULL);
	} else {
		gst_element_link_many(source, frame, splp, sink, NULL);
		gst_bin_add_many(GST_BIN(pipeline), source, frame, splp, sink, NULL);
	}

	return pipeline;
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
	GstElement *pipeline;

	/*
	 * Init.
	 */

	gst_init(&argc, &argv);
	options = parse_command_line(&argc, &argv);

	if(options.direction == ENCODING)
		signal(SIGINT, sigint_handler);

	if(options.dump_status && options.dump_status_async)
		fcntl(STDERR_FILENO, F_SETFL, O_NONBLOCK);

	if(options.verbose) {
		fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n", (options.direction == DECODING) ? "DECODING" : "ENCODING");
		bkr_display_mode(stderr, options.videomode, options.bitdensity, options.sectorformat);
	}


	/*
	 * Construct the pipeline.
	 */

	if(options.direction == ENCODING) {
		pipeline = encoder_pipeline(options.videomode, options.bitdensity, options.sectorformat);
	} else {
		pipeline = decoder_pipeline(options.videomode, options.bitdensity, options.sectorformat);
	}


	/*
	 * Transfer data one sector at a time until EOF is reached.
	 */

	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	while(gst_bin_iterate(GST_BIN(pipeline)) && !got_sigint);
	if((options.direction == ENCODING) && (feof(stdin) || got_sigint)) {
		/* FIXME: send EOS event to top of stream */
	}


	/*
	 * Clean up.
	 */

	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(pipeline));
	exit(0);
}
