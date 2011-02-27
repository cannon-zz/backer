/*
 * bkrencode
 *
 * Tape data encoder/decoder for the Backer tape device.
 *
 * Copyright (C) 2000--2011  Kipp C. Cannon
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
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>


#include <gst/gst.h>


#include <backer.h>
#include <bkr_disp_mode.h>


#define  PROGRAM_NAME    "bkrencode"


/*
 * ============================================================================
 *
 *             SIGINT handler for cleanly terminating a recording
 *
 * ============================================================================
 */


static struct {
	GstElement *pipeline;
} sigint_data;


static void sigint_handler(int num)
{
	signal(SIGINT, SIG_IGN);
	gst_element_send_event(sigint_data.pipeline, gst_event_new_eos());
	gst_object_unref(sigint_data.pipeline);
	sigint_data.pipeline = NULL;
}


/*
 * ============================================================================
 *
 *                                Command Line
 *
 * ============================================================================
 */


struct options {
	gboolean verbose;
	gboolean time_only;
	gboolean ignore_bad;
	gboolean inject_noise;
	gboolean decode;
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;
};


static struct options default_options(void)
{
	struct options defaults = {
		.verbose = FALSE,
		.time_only = FALSE,
		.ignore_bad = FALSE,
		.inject_noise = FALSE,
		.decode = FALSE,
		.videomode = BKR_NTSC,
		.bitdensity = BKR_HIGH,
		.sectorformat = BKR_SP
	};

	return defaults;
}


static struct options parse_command_line(int *argc, char **argv[])
{
	struct options options = default_options();
	gchar *bitdensity = NULL, *sectorformat = NULL, *videomode = NULL;
	GOptionEntry entries[] = {
		{"bit-density", 'D', 0, G_OPTION_ARG_STRING, &bitdensity, "Set the data rate to high or low", "{h,l}"},
		{"sector-format", 'F', 0, G_OPTION_ARG_STRING, &sectorformat, "Set the data format to EP or SP/LP", "{e,s}"},
		{"video-mode", 'V', 0, G_OPTION_ARG_STRING, &videomode, "Set the video mode to NTSC or PAL", "{n,p}"},
		{"skip-bad-sectors", 's', 0, G_OPTION_ARG_NONE, &options.ignore_bad, "Skip bad sectors", NULL},
		{"inject-noise", 'n', 0, G_OPTION_ARG_NONE, &options.inject_noise, "Inject simulated tape noise (only during encode)", NULL},
		{"time-only", 't', 0, G_OPTION_ARG_NONE, &options.time_only, "Compute time only (do not encode or decode data)", NULL},
		{"unencode", 'u', 0, G_OPTION_ARG_NONE, &options.decode, "Unencode tape data (default is to encode)", NULL},
		{"verbose", 'v', 0, G_OPTION_ARG_NONE, &options.verbose, "Be verbose", NULL},
		{NULL}
	};
	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new("- Backer tape data encoder/unencoder");

	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, gst_init_get_option_group());

	if(!g_option_context_parse(context, argc, argv, &error)) {
		fprintf(stderr, PROGRAM_NAME ": error: %s\n", error->message);
		exit(1);
	}

	g_option_context_free(context);

	if(bitdensity)
		switch(tolower(bitdensity[0])) {
		case 'h':
			options.bitdensity = BKR_HIGH;
			break;
		case 'l':
			options.bitdensity = BKR_LOW;
			break;
		default:
			fprintf(stderr, PROGRAM_NAME ": error: Unknown bit density %s\n", bitdensity);
			exit(1);
		}
	if(sectorformat)
		switch(tolower(sectorformat[0])) {
		case 'e':
			options.sectorformat = BKR_EP;
			break;
		case 's':
			options.sectorformat = BKR_SP;
			break;
		default:
			fprintf(stderr, PROGRAM_NAME ": error: Unknown sector format %s\n", sectorformat);
			exit(1);
		}
	if(videomode)
		switch(tolower(videomode[0])) {
		case 'n':
			options.videomode = BKR_NTSC;
			break;
		case 'p':
			options.videomode = BKR_PAL;
			break;
		default:
			fprintf(stderr, PROGRAM_NAME ": error: Unknown video mode %s\n", videomode);
			exit(1);
		}

	if(options.inject_noise && options.decode)
		fprintf(stderr, PROGRAM_NAME ": warning: ignoring --inject-noise\n");

	return options;
}


/*
 * ============================================================================
 *
 *                           Pipeline Construction
 *
 * ============================================================================
 */


static gboolean message_handler(GstBus *bus, GstMessage *msg, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	switch(GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		g_main_loop_quit(loop);
		break;

	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *err;

		gst_message_parse_error(msg, &err, &debug);
		g_free(debug);

		fprintf(stderr, PROGRAM_NAME ": error: %s\n", err->message);
		g_error_free(err);

		g_main_loop_quit(loop);
		break;
	}

	default:
		break;
	}

	return TRUE;
}


static GstElement *encoder_pipeline(enum bkr_videomode videomode, enum bkr_bitdensity bitdensity, enum bkr_sectorformat sectorformat, gboolean inject_noise)
{
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *head;
	GstElement *source = gst_element_factory_make("fdsrc", NULL);
	GstElement *splp = gst_element_factory_make("bkr_splpenc", NULL);
	GstElement *frame = gst_element_factory_make("bkr_frameenc", NULL);
	GstElement *sink = gst_element_factory_make("fdsink", NULL);
	GstCaps *caps = gst_caps_new_simple(
		"application/x-backer",
		"videomode", G_TYPE_INT, videomode,
		"bitdensity", G_TYPE_INT, bitdensity,
		"sectorformat", G_TYPE_INT, sectorformat,
		NULL
	);

	/*
	 * in error paths, we don't bother unref()ing things, because we're
	 * going to exit the program anyway
	 */

	if(!pipeline || !source || !splp || !frame || !sink || !caps)
		return NULL;

	g_object_set(G_OBJECT(source), "fd", STDIN_FILENO, NULL);
	g_object_set(G_OBJECT(sink), "fd", STDOUT_FILENO, NULL);
	g_object_set(G_OBJECT(frame), "inject_noise", inject_noise, NULL);

	gst_bin_add_many(GST_BIN(pipeline), source, splp, frame, sink, NULL);
	if(sectorformat == BKR_EP) {
		GstElement *ecc2 = gst_element_factory_make("bkr_ecc2enc", NULL);
		GstElement *rll = gst_element_factory_make("bkr_rllenc", NULL);
		if(!ecc2 || !rll)
			return NULL;
		gst_bin_add_many(GST_BIN(pipeline), ecc2, rll, NULL);
		gst_element_link_filtered(source, ecc2, caps);
		gst_element_link_many(ecc2, splp, rll, frame, NULL);
		head = frame;
	} else {
		gst_element_link_filtered(source, splp, caps);
		gst_element_link_many(splp, frame, NULL);
		head = frame;
	}
	gst_element_link_many(head, sink, NULL);

	gst_caps_unref(caps);
	return pipeline;
}


static GstElement *decoder_pipeline(enum bkr_videomode videomode, enum bkr_bitdensity bitdensity, enum bkr_sectorformat sectorformat)
{
	GstElement *pipeline = gst_pipeline_new("pipeline");
	GstElement *head;
	GstElement *source = gst_element_factory_make("fdsrc", NULL);
	GstElement *frame = gst_element_factory_make("bkr_framedec", NULL);
	GstElement *splp = gst_element_factory_make("bkr_splpdec", NULL);
	GstElement *sink = gst_element_factory_make("fdsink", NULL);
	GstCaps *caps = gst_caps_new_simple(
		"application/x-backer",
		"videomode", G_TYPE_INT, videomode,
		"bitdensity", G_TYPE_INT, bitdensity,
		"sectorformat", G_TYPE_INT, sectorformat,
		NULL
	);

	if(!pipeline || !source || !frame || !splp || !sink || !caps) {
		/* don't bother unref()ing things, because we're going to
		 * exit now anyway */
		return NULL;
	}

	g_object_set(G_OBJECT(source), "fd", STDIN_FILENO, NULL);
	g_object_set(G_OBJECT(sink), "fd", STDOUT_FILENO, NULL);

	gst_bin_add_many(GST_BIN(pipeline), source, frame, splp, sink, NULL);
	gst_element_link_filtered(source, frame, caps);
	if(sectorformat == BKR_EP) {
		GstElement *rll = gst_element_factory_make("bkr_rlldec", NULL);
		GstElement *ecc2 = gst_element_factory_make("bkr_ecc2dec", NULL);
		if(!rll || !ecc2) {
			/* don't bother unref()ing things, because we're
			 * going to exit now anyway */
			return NULL;
		}
		gst_element_link_many(frame, rll, splp, ecc2, NULL);
		head = ecc2;
	} else {
		gst_element_link_many(frame, splp, NULL);
		head = splp;
	}
	gst_element_link_many(head, sink, NULL);

	gst_caps_unref(caps);
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
	GstBus *bus;


	/*
	 * Init.
	 */


	if(!g_thread_supported())
		g_thread_init(NULL);
	options = parse_command_line(&argc, &argv);
	gst_init(NULL, NULL);

	if(options.verbose) {
		fprintf(stderr, PROGRAM_NAME ": %s tape format selected:\n", options.decode ? "DECODING" : "ENCODING");
		bkr_display_mode(stderr, options.videomode, options.bitdensity, options.sectorformat);
	}


	/*
	 * Construct the pipeline.
	 */


	loop = g_main_loop_new(NULL, FALSE);
	if(options.decode)
		pipeline = decoder_pipeline(options.videomode, options.bitdensity, options.sectorformat);
	else
		pipeline = encoder_pipeline(options.videomode, options.bitdensity, options.sectorformat, options.inject_noise);
	if(!pipeline) {
		fprintf(stderr, PROGRAM_NAME ": failure building pipeline.\n");
		exit(1);
	}
	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_bus_add_watch(bus, message_handler, loop);
	gst_object_unref(bus);


	/*
	 * Trap SIGINT when encoding to generate clean EOS marker
	 */


	if(!options.decode) {
		gst_object_ref(pipeline);
		sigint_data.pipeline = pipeline;
		signal(SIGINT, sigint_handler);
	}


	/*
	 * Transcode data.
	 */


	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	g_main_loop_run(loop);


	/*
	 * Clean up.
	 */


	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(pipeline));
	signal(SIGINT, SIG_IGN);
	if(sigint_data.pipeline) {
		gst_object_unref(GST_OBJECT(pipeline));
		sigint_data.pipeline = NULL;
	}
	exit(0);
}
