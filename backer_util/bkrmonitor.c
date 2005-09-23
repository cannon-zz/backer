/*
 * bkrmonitor
 *
 * Graphical display of Backer device driver status.
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

#include "backer.h"


#ifdef GTK_CONFIG

#include <gtk/gtk.h>
void  create_gtk_window(void);
#define  ONCE_INITIALIZER  0

#else

typedef  int  gint;
typedef  void  *gpointer;
typedef  void  GtkWidget;
#define  gtk_init(x,y)           do { } while(0)
#define  gtk_timeout_add(x,y,z)  do { } while(0)
#define  gtk_main()              do { } while(0)
#define  create_gtk_window()     do { } while(0)
#define  TRUE   1
#define  FALSE  0
#define  ONCE_INITIALIZER  1

#endif /* GTK_CONFIG */


#define  PROGRAM_NAME    "bkrmonitor"
#define  DEFAULT_UPDATE  50                     /* milliseconds */
#define  MIN_UPDATE      10                     /* milliseconds */
#define  DECAY_INTERVAL  60                     /* sectors */

/*
 * Function prototypes
 */

gint  update_status(gpointer);


/*
 * Global data
 */

int  devfile;
int  update_interval = MIN_UPDATE - 1;
struct
	{
	GtkWidget  *ntsc, *pal;
	GtkWidget  *high, *low;
	GtkWidget  *raw, *sp, *ep;
	GtkWidget  *sector;
	GtkWidget  *total;
	GtkWidget  *symbol, *block, *frame, *overrun, *underflow;
	GtkWidget  *worst, *best, *least, *most;
	GtkWidget  *buffer_status;
	} widgets;
struct
	{
	GtkWidget  *widget;
	int  rate;
	int  last_block;
	} error_rate;
struct  bkrstatus  status;
struct  bkrformat  format;
struct  mtget      mtget;
struct  mtpos      pos;


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  i;
	int  once = ONCE_INITIALIZER;
	char  *devname = DEFAULT_DEVICE;

	/*
	 * Parse command line.
	 */

	gtk_init(&argc, &argv);
	while((i = getopt(argc, argv, "f:hot:")) != EOF)
		switch(i)
			{
			case 'f':
			devname = optarg;
			break;

			case 't':
			update_interval = atoi(optarg);
			break;

			case 'o':
			once = 1;
			break;

			case 'h':
			default:
			puts(
	"Usage:  " PROGRAM_NAME " [options]\n" \
	"the following options are recognized:\n" \
	"       -f devname  Use device devname (default " DEFAULT_DEVICE ")\n" \
	"       -t num      Set the update interval to num milliseconds\n" \
	"       -o          Print status to stdout and quit\n" \
	"       -h          Display usage");
			exit(0);
			}
	if(update_interval < MIN_UPDATE)
		update_interval = DEFAULT_UPDATE;

	/*
	 * Open device file and get info.  If in once-only mode, print the
	 * results and quit.
	 */

	devfile = open(devname, O_RDONLY);
	if(devfile < 0)
		{
		perror(PROGRAM_NAME);
		exit(-1);
		}
	ioctl(devfile, MTIOCGET, &mtget);
	ioctl(devfile, BKRIOCGETFORMAT, &format);

	if(once)
		{
		ioctl(devfile, MTIOCPOS, &pos);
		ioctl(devfile, BKRIOCGETSTATUS, &status);
		
		printf("Current Status of %s\n", devname);
		printf("Sector Number:  %lu\n", pos.mt_blkno);
		printf("Total Errors Corrected:  %u\n", status.health.total_errors);
		printf("Errors in Worst Block:   %u\n", status.errors.symbol);
		printf("Uncorrectable Blocks:    %u\n", status.errors.block);
		printf("Framing Errors:       %u\n", status.errors.frame);
		printf("Overrun Errors:       %u\n", status.errors.overrun);
		printf("Underflows Detected:  %u\n", status.errors.underflow);
		printf("Worst Key:        %u\n", status.health.worst_key);
		printf("Closest Non-Key:  %u\n", status.health.best_nonkey);
		printf("Least Skipped:    %u\n", status.health.least_skipped);
		printf("Most Skipped:     %u\n", status.health.most_skipped);
		printf("DMA Buffer:  %u/%u\n", status.bytes, format.buffer_size);

		exit(0);
		}

	/*
	 * Create the window and go.
	 */

	create_gtk_window();

	gtk_timeout_add(update_interval, update_status, NULL);

	error_rate.rate = 0;
	error_rate.last_block = 0;

	gtk_main();

	exit(0);
}


/*
 * create_gtk_window()
 */

#ifdef GTK_CONFIG
void create_gtk_window(void)
{
	GtkWidget  *window;
	GtkWidget  *vbox;
	GtkWidget  *table;
	GtkWidget  *widget;

	/* window */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
	gtk_window_set_title(GTK_WINDOW(window), "Backer Monitor");
	gtk_container_set_border_width(GTK_CONTAINER(window), 7);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_exit), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* Mode indicators */

	table = gtk_table_new(3, 3, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widgets.ntsc = gtk_radio_button_new_with_label(NULL, "NTSC");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.ntsc, 0, 1, 0, 1);
	widgets.pal = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.ntsc)), "PAL");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.pal, 0, 1, 1, 2);

	widgets.high = gtk_radio_button_new_with_label(NULL, "High");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.high, 1, 2, 0, 1);
	widgets.low = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.high)), "Low");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.low, 1, 2, 1, 2);

	widgets.raw = gtk_radio_button_new_with_label(NULL, "RAW");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.raw, 2, 3, 0, 1);
	widgets.sp = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.raw)), "SP/LP");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.sp, 2, 3, 1, 2);
	widgets.ep = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.sp)), "EP");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.ep, 2, 3, 2, 3);

	gtk_widget_set_sensitive(GTK_WIDGET(table), FALSE);

	switch(BKR_VIDEOMODE(mtget.mt_dsreg))
		{
		case BKR_NTSC:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.ntsc), TRUE);
		break;

		case BKR_PAL:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.pal), TRUE);
		break;
		}

	switch(BKR_DENSITY(mtget.mt_dsreg))
		{
		case BKR_HIGH:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.high), TRUE);
		break;

		case BKR_LOW:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.low), TRUE);
		break;
		}

	switch(BKR_FORMAT(mtget.mt_dsreg))
		{
		case BKR_RAW:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.raw), TRUE);
		break;

		case BKR_SP:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.sp), TRUE);
		break;

		case BKR_EP:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.ep), TRUE);
		break;
		}

	/* Error counts */

	table = gtk_table_new(7, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Sector Number");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("Total Errors Corrected");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);
	widget = gtk_label_new("Errors in Worst Block");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 2, 3);
	widget = gtk_label_new("Uncorrectable Blocks");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 3, 4);
	widget = gtk_label_new("Framing Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 4, 5);
	widget = gtk_label_new("Overrun Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 5, 6);
	widget = gtk_label_new("Underflows Detected");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 6, 7);

	widgets.sector = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.sector, 1, 2, 0, 1);

	widgets.total = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.total, 1, 2, 1, 2);

	widgets.symbol = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.symbol, 1, 2, 2, 3);

	widgets.block = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.block, 1, 2, 3, 4);

	widgets.frame = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.frame, 1, 2, 4, 5);

	widgets.overrun = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.overrun, 1, 2, 5, 6);

	widgets.underflow = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.underflow, 1, 2, 6, 7);

	/* Health information */

	table = gtk_table_new(4, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Worst Key");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("Closest Non-Key");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);
	widget = gtk_label_new("Least Skipped");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 2, 3);
	widget = gtk_label_new("Most Skipped");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 3, 4);

	widgets.worst = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.worst, 1, 2, 0, 1);

	widgets.best = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.best, 1, 2, 1, 2);

	widgets.least = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.least, 1, 2, 2, 3);

	widgets.most = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.most, 1, 2, 3, 4);

	/* Errors and DMA bar graphs */

	table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Recent Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("DMA Buffer");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);

	error_rate.widget = gtk_progress_bar_new();
	gtk_progress_configure(GTK_PROGRESS(error_rate.widget), 0, 0, format.block_parity/2*DECAY_INTERVAL);
	gtk_table_attach_defaults(GTK_TABLE(table), error_rate.widget, 1, 2, 0, 1);
	widgets.buffer_status = gtk_progress_bar_new();
	gtk_progress_configure(GTK_PROGRESS(widgets.buffer_status), 0, 0, format.buffer_size);
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.buffer_status, 1, 2, 1, 2);

	/* Close button */

	widget = gtk_button_new_with_label("Close");
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(widget), "clicked", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	/* Done */

	gtk_widget_show_all(window);
}
#endif /* GTK_CONFIG */


/*
 * Status update call-back function
 */

gint update_status(gpointer data)
{
	char  text[20];

	ioctl(devfile, MTIOCPOS, &pos);
	ioctl(devfile, BKRIOCGETSTATUS, &status);

	if(status.errors.recent_symbol != 0)
		error_rate.rate = status.errors.recent_symbol * DECAY_INTERVAL;
	else if(error_rate.rate > 0)
		{
		error_rate.rate -= pos.mt_blkno - error_rate.last_block;
		if(error_rate.rate < 0)
			error_rate.rate = 0;
		}
	error_rate.last_block = pos.mt_blkno;

#ifdef GTK_CONFIG
	gtk_progress_set_value(GTK_PROGRESS(error_rate.widget), error_rate.rate);
	gtk_progress_set_value(GTK_PROGRESS(widgets.buffer_status), status.bytes);

	sprintf(text, "%lu", pos.mt_blkno);
	gtk_label_set_text(GTK_LABEL(widgets.sector), text);

	sprintf(text, "%u", status.health.total_errors);
	gtk_label_set_text(GTK_LABEL(widgets.total), text);

	sprintf(text, "%u", status.errors.symbol);
	gtk_label_set_text(GTK_LABEL(widgets.symbol), text);

	sprintf(text, "%u", status.errors.block);
	gtk_label_set_text(GTK_LABEL(widgets.block), text);

	sprintf(text, "%u", status.errors.frame);
	gtk_label_set_text(GTK_LABEL(widgets.frame), text);

	sprintf(text, "%u", status.errors.overrun);
	gtk_label_set_text(GTK_LABEL(widgets.overrun), text);

	sprintf(text, "%u", status.errors.underflow);
	gtk_label_set_text(GTK_LABEL(widgets.underflow), text);

	sprintf(text, "%u", status.health.worst_key);
	gtk_label_set_text(GTK_LABEL(widgets.worst), text);

	sprintf(text, "%u", status.health.best_nonkey);
	gtk_label_set_text(GTK_LABEL(widgets.best), text);

	sprintf(text, "%u", status.health.least_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.least), text);

	sprintf(text, "%u", status.health.most_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.most), text);
#endif  /* GTK_CONFIG */

	return(TRUE);
}
