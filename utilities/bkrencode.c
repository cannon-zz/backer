/*
 * bkrencode
 *
 * Tape data encoder/decoder for the Backer tape device.
 *
 * Copyright (C) 2000,2001,2002,2008  Kipp C. Cannon
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


/*
 * ============================================================================
 *
 *                                  Preamble
 *
 * ============================================================================
 */


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>


#include <gst/gst.h>


#include <bkr_disp_mode.h>


#define  PROGRAM_NAME    "bkrencode"


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


enum direction {
	ENCODING,
	DECODING
};


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
			if(tolower(optarg[0]) != 's') {
				usage();
				exit(1);
			}
			options.dump_status_async = 0;
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


static GstElement *encoder_pipeline(enum bkr_videomode videomode, enum bkr_bitdensity bitdensity, enum bkr_sectorformat sectorformat)
{
	/* FIXME: in EP mode, an ecc2 encoder goes between the source and splp
	 * elements. */
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *source = gst_element_factory_make("fdsrc", NULL);
	GstElement *splp = gst_element_factory_make("bkr_splpenc", NULL);
	GstElement *rll = (sectorformat == BKR_EP) ? gst_element_factory_make("bkr_rllenc", NULL) : NULL;
	GstElement *frame = gst_element_factory_make("bkr_frameenc", NULL);
	GstElement *sink = gst_element_factory_make("fdsink", NULL);
	GstCaps *caps = gst_caps_new_simple(
		"application/x-backer",
		"videomode", G_TYPE_INT, videomode,
		"bitdensity", G_TYPE_INT, bitdensity,
		"sectorformat", G_TYPE_INT, sectorformat,
		NULL
	);

	if(!pipeline || !source || !splp || (!rll && sectorformat == BKR_EP) || !frame || !sink || !caps) {
		/* don't bother unref()ing things, because we're going to
		 * exit now anyway */
		fprintf(stderr, "%s: failure building pipeline.\n", PROGRAM_NAME);
		return NULL;
	}

	g_object_set(G_OBJECT(source), "fd", STDIN_FILENO, NULL);
	g_object_set(G_OBJECT(sink), "fd", STDOUT_FILENO, NULL);

	if(sectorformat == BKR_EP) {
		gst_bin_add_many(GST_BIN(pipeline), source, splp, rll, frame, sink, NULL);
		gst_element_link_filtered(source, splp, caps);
		gst_element_link_many(splp, rll, frame, sink, NULL);
	} else {
		gst_bin_add_many(GST_BIN(pipeline), source, splp, frame, sink, NULL);
		gst_element_link_filtered(source, splp, caps);
		gst_element_link_many(splp, frame, sink, NULL);
	}

	return pipeline;
}


static GstElement *decoder_pipeline(enum bkr_videomode videomode, enum bkr_bitdensity bitdensity, enum bkr_sectorformat sectorformat)
{
	/* FIXME: in EP mode, an ecc2 decoder goes between the splp and
	 * sink elements. */
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *source = gst_element_factory_make("fdsrc", NULL);
	GstElement *frame = gst_element_factory_make("bkr_framedec", NULL);
	GstElement *rll = (sectorformat == BKR_EP) ? gst_element_factory_make("bkr_rlldec", NULL) : NULL;
	GstElement *splp = gst_element_factory_make("bkr_splpdec", NULL);
	GstElement *sink = gst_element_factory_make("fdsink", NULL);
	GstCaps *caps = gst_caps_new_simple(
		"application/x-backer",
		"videomode", G_TYPE_INT, videomode,
		"bitdensity", G_TYPE_INT, bitdensity,
		"sectorformat", G_TYPE_INT, sectorformat,
		NULL
	);

	if(!pipeline || !source || !frame || (!rll && sectorformat == BKR_EP) || !splp || !sink || !caps) {
		/* don't bother unref()ing things, because we're going to
		 * exit now anyway */
		fprintf(stderr, "%s: failure building pipeline.\n", PROGRAM_NAME);
		return NULL;
	}

	g_object_set(G_OBJECT(source), "fd", 0, NULL);
	g_object_set(G_OBJECT(sink), "fd", 1, NULL);

	if(sectorformat == BKR_EP) {
		gst_bin_add_many(GST_BIN(pipeline), source, frame, rll, splp, sink, NULL);
		gst_element_link_filtered(source, frame, caps);
		gst_element_link_many(frame, rll, splp, sink, NULL);
	} else {
		gst_bin_add_many(GST_BIN(pipeline), source, frame, splp, sink, NULL);
		gst_element_link_filtered(source, frame, caps);
		gst_element_link_many(frame, splp, sink, NULL);
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
	GMainLoop *loop;
	GstElement *pipeline;


	/*
	 * Init.
	 */


	/* FIXME: gst_init() intercepts --help, how do I get my own usage
	 * message displayed? */
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
	if(!pipeline)
		exit(1);


	/*
	 * Transfer data one sector at a time until EOF is reached.
	 */


	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
#if 0
	while(gst_bin_iterate(GST_BIN(pipeline)) && !got_sigint);
	if(options.direction == ENCODING) {
		/* FIXME: this doesn't work.  How do we send EOS down the
		 * pipeline? */
		gst_element_send_event(pipeline, gst_event_new(GST_EVENT_EOS));
	}
#endif


	/*
	 * Clean up.
	 */


	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(pipeline));
	exit(0);
}
