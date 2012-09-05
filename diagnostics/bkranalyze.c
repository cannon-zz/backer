#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <gtk/gtk.h>

#include "backer.h"
#include "bkr_proc_io.h"
#include "bkr_ring_buffer.h"
#include "bkr_splp.h"
#include "bkr_splp_randomize.h"
#include "bkr_gcr.h"
#include "bkr_frame.h"
#include "bkr_screen.h"


/*
 * Constants, parameters, etc.
 */

#define  PROGRAM_NAME  "bkranalyze"
#define  DEFAULT_DEVICE  "/dev/tape"
#define  DEFAULT_MODE  (BKR_NTSC | BKR_HIGH | BKR_SP)


/*
 * Global data
 */

struct {
	struct {
		GtkWidget  *mode, *density, *format;
		GtkWidget  *screen;
		GtkAdjustment  *scroll_adj;
		GtkWidget  *scroll_bar;
		GtkWidget  *correlation;
	} video;
} widgets;



/*
 *=========================================================================
 *
 *                                Macros
 *
 *=========================================================================
 */

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)


/*
 *=========================================================================
 *
 *                             UI Call-Backs
 *
 *=========================================================================
 */


/*
 *=========================================================================
 *
 *                            Codec Callback
 *
 *=========================================================================
 */

static void codec_callback(void *data)
{
	struct bkr_stream_t *stream = data;
	struct ring ring;
	int  result;
	gchar  buff[3000];

	result = stream->ops.read(stream);
	if(result > 0)
		ring_drain(stream->ring, result);

	ring_lock(stream->source->ring);
	ring = *stream->source->ring;
	ring_unlock(stream->source->ring);

	ring_offset_dec(&ring, ring.tail, stream->fmt.active_size + stream->fmt.leader);
	memcpy_from_ring(buff, &ring, stream->fmt.field_size);

	gdk_threads_enter();
	bkr_screen_set_data(BKRSCREEN(widgets.video.screen), buff);
	gdk_threads_leave();
}


/*
 *=========================================================================
 *
 *                              Entry Point
 *
 *=========================================================================
 */

int main(int argc, char *argv[])
{
	int  result, tmp;
	GtkWidget  *window;
	GtkWidget  *widget, *vbox, *hbox;
	GtkWidget  *notebook;
	int  mode = -1;
	bkr_direction_t  direction = BKR_WRITING;
	static bkr_format_info_t format_tbl[] = BKR_FORMAT_INFO_INITIALIZER;
	struct mtget  mtget;
	char  msg[100];
	const struct bkr_stream_ops_t *stdio_ops, *frame_ops;
	struct bkr_stream_t  *stream;
	struct bkr_stream_t  *dev_stream = NULL;
	struct bkr_stream_t  *frame_stream = NULL;
	bkr_frame_private_t  *frame_private;

	/*
	 * Setup GTK
	 */

	g_thread_init(NULL);
	gdk_threads_init();
	gtk_init(&argc, &argv);
	gdk_rgb_init();

	/*
	 * Setup codecs
	 */

	stdio_ops = bkr_stdio_dev_init();
	frame_ops = bkr_frame_codec_init();


	/*
	 * Parse command line.
	 */

	while((result = getopt(argc, argv, "D:F:f::hV:")) != EOF)
		switch(result) {
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
	"	-f[dev]  Get the format to use from dev (default " DEFAULT_DEVICE ")\n" \
	"	-h       Display this usage message\n", stderr);
			exit(1);
		}


	/*
	 * Retrieve mode from stdin if required.
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
	 * Construct the codec chain
	 */

	stream = dev_stream = stdio_ops->new(NULL, mode, &format_tbl[bkr_mode_to_format(mode)]);
	stream = frame_stream = frame_ops->new(stream, mode, &format_tbl[bkr_mode_to_format(mode)]);
	if(!stream) {
		errno = ENOMEM;
		perror(PROGRAM_NAME);
		exit(1);
	}
	frame_private = frame_stream->private;
	bkr_stream_set_callback(stream, codec_callback, stream);


	/*
	 * Create window.
	 */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
	gtk_window_set_title(GTK_WINDOW(window), "Backer Record Analyzer");
	gtk_container_set_border_width(GTK_CONTAINER(window), 5);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_exit), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

	gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);

	/* Close button */

	widget = gtk_button_new_with_label("Close");
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(widget), "clicked", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	/* Video window page */

	vbox = gtk_vbox_new(FALSE, 5);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, gtk_label_new("Video Screen"));
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

	hbox = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);
	widgets.video.mode = gtk_label_new(NULL);
	gtk_container_add(GTK_CONTAINER(widget), widgets.video.mode);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);
	widgets.video.density = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), widgets.video.density);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);
	widgets.video.format = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), widgets.video.format);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);
	widgets.video.screen = bkr_screen_new(mode);
	gtk_container_add(GTK_CONTAINER(widget), widgets.video.screen);

	widgets.video.scroll_adj = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, dev_stream->ring->size, 1, 100, 0));
	widgets.video.scroll_bar = gtk_hscrollbar_new(widgets.video.scroll_adj);
	gtk_box_pack_start(GTK_BOX(vbox), widgets.video.scroll_bar, FALSE, FALSE, 0);
#if 0
	gtk_signal_connect(GTK_OBJECT(widgets.video.scroll_adj), "value_changed", GTK_SIGNAL_FUNC(ring_tail_changed), NULL);
#endif

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Correlation:  "), FALSE, FALSE, 0);
	widgets.video.correlation = gtk_label_new(" 0");
	gtk_box_pack_start(GTK_BOX(hbox), widgets.video.correlation, FALSE, FALSE, 0);
	sprintf(msg, "/%d", stream->fmt.key_length);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(msg), FALSE, FALSE, 0);

	/* Done */

	gtk_widget_show_all(window);


	/*
	 * Do some final initialization...
	 */

	if(BKR_VIDEOMODE(mode) == BKR_NTSC)
		gtk_label_set_text(GTK_LABEL(widgets.video.mode), "NTSC");
	else
		gtk_label_set_text(GTK_LABEL(widgets.video.mode), "PAL");
	if(BKR_DENSITY(mode) == BKR_HIGH)
		gtk_label_set_text(GTK_LABEL(widgets.video.density), "HIGH");
	else
		gtk_label_set_text(GTK_LABEL(widgets.video.density), "LOW");
	if(BKR_CODEC(mode) == BKR_EP)
		gtk_label_set_text(GTK_LABEL(widgets.video.format), "EP");
	else if(BKR_CODEC(mode) == BKR_RAW)
		gtk_label_set_text(GTK_LABEL(widgets.video.format), "RAW");
	else
		gtk_label_set_text(GTK_LABEL(widgets.video.format), "SP");

	widgets.video.scroll_adj->upper = dev_stream->ring->size;
	widgets.video.scroll_adj->page_increment = dev_stream->fmt.field_size;
	widgets.video.scroll_adj->page_size = dev_stream->fmt.field_size;
	gtk_adjustment_set_value(widgets.video.scroll_adj, (gfloat) space_in_ring(dev_stream->ring));
	gtk_signal_emit_by_name(GTK_OBJECT(widgets.video.scroll_adj), "value_changed");


	/*
	 * Away we go...
	 */

	stream->ops.start(stream, BKR_READING);
	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();
	stream->ops.release(stream);

	exit(0);
}
