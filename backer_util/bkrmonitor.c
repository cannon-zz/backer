/*
 * bkrmonitor
 *
 * Graphical display of Backer device driver status.
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <gtk/gtk.h>
#include "backer.h"

#define  PROGRAM_NAME    "bkrmonitor"
#define  DEFAULT_DEVICE  "/dev/backer"
#define  DEFAULT_UPDATE  50                     /* milliseconds */
#define  MIN_UPDATE      10                     /* milliseconds */

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
	GtkWidget  *ep, *sp;
	GtkWidget  *symbol, *block, *sector, *overrun, *underflow;
	GtkWidget  *worst, *best, *least, *most;
	GtkWidget  *buffer_status;
	} widgets;
struct  bkrstatus  status;
struct  bkrformat  format;
struct  bkrconfig  config;


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  i;
	char  *devname = DEFAULT_DEVICE;
	GtkWidget  *window;
	GtkWidget  *vbox, *hbox;
	GtkWidget  *table;
	GtkWidget  *widget;

	/*
	 * Parse command line.
	 */

	gtk_init(&argc, &argv);
	while((i = getopt(argc, argv, "f:ht:")) != EOF)
		switch(i)
			{
			case 'f':
			devname = optarg;
			break;

			case 't':
			update_interval = atoi(optarg);
			break;

			case 'h':
			default:
			puts(
	"Usage:  " PROGRAM_NAME " [options]\n" \
	"the following options are recognized:\n" \
	"       -f devname  Use device devname (default " DEFAULT_DEVICE ")\n" \
	"       -t num      Set the update interval to num milliseconds\n" \
	"       -h          Display usage");
			exit(0);
			}
	if(update_interval < MIN_UPDATE)
		update_interval = DEFAULT_UPDATE;

	/*
	 * Open device file and get info.
	 */

	devfile = open(devname, O_RDWR);
	if(devfile < 0)
		{
		perror(PROGRAM_NAME);
		exit(-1);
		}
	ioctl(devfile, BKRIOCGETMODE, &config);
	ioctl(devfile, BKRIOCGETFORMAT, &format);

	/*
	 * Create window.
	 */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
	gtk_window_set_title(GTK_WINDOW(window), "Backer Monitor");
	gtk_container_set_border_width(GTK_CONTAINER(window), 5);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_exit), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* Mode indicators */

	table = gtk_table_new(2, 3, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widgets.ntsc = gtk_radio_button_new_with_label(NULL, "NTSC");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.ntsc, 0, 1, 0, 1);
	widgets.pal = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.ntsc)), "PAL");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.pal, 0, 1, 1, 2);

	widgets.high = gtk_radio_button_new_with_label(NULL, "High");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.high, 1, 2, 0, 1);
	widgets.low = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.high)), "Low");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.low, 1, 2, 1, 2);

	widgets.sp = gtk_radio_button_new_with_label(NULL, "SP/LP");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.sp, 2, 3, 0, 1);
	widgets.ep = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widgets.sp)), "EP");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.ep, 2, 3, 1, 2);

	gtk_widget_set_sensitive(GTK_WIDGET(table), FALSE);

	switch(BKR_VIDEOMODE(config.mode))
		{
		case BKR_NTSC:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.ntsc), TRUE);
		break;

		case BKR_PAL:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.pal), TRUE);
		break;
		}

	switch(BKR_DENSITY(config.mode))
		{
		case BKR_HIGH:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.high), TRUE);
		break;

		case BKR_LOW:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.low), TRUE);
		break;
		}

	switch(BKR_SPEED(config.mode))
		{
		case BKR_SP:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.sp), TRUE);
		break;

		case BKR_EP:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.ep), TRUE);
		break;
		}

	/* Error counts */

	table = gtk_table_new(5, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Most Symbol Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("Uncorrectable Blocks");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);
	widget = gtk_label_new("Framing Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 2, 3);
	widget = gtk_label_new("Over-run Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 3, 4);
	widget = gtk_label_new("Underflow Warnings");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 4, 5);

	widgets.symbol = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.symbol, 1, 2, 0, 1);

	widgets.block = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.block, 1, 2, 1, 2);

	widgets.sector = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.sector, 1, 2, 2, 3);

	widgets.overrun = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.overrun, 1, 2, 3, 4);

	widgets.underflow = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.underflow, 1, 2, 4, 5);

	/* Debuging information */

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

	/* DMA Buffer status */

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	widget = gtk_label_new("DMA Buffer");
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 10);

	widgets.buffer_status = gtk_progress_bar_new();
	gtk_progress_configure(GTK_PROGRESS(widgets.buffer_status), 0, 0, format.buffer_size);
	gtk_box_pack_start(GTK_BOX(hbox), widgets.buffer_status, TRUE, TRUE, 0);

	/* Close button */

	widget = gtk_button_new_with_label("Close");
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(widget), "clicked", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	/* Done */

	gtk_widget_show_all(window);


	/*
	 * Add update_status() as a timer call-back
	 */

	gtk_timeout_add(update_interval, update_status, NULL);


	/*
	 * Start
	 */

	gtk_main();

	exit(0);
}


/*
 * Status update call-back function
 */

gint update_status(gpointer data)
{
	char  text[20];

	ioctl(devfile, BKRIOCGETSTATUS, &status);

	gtk_progress_set_value(GTK_PROGRESS(widgets.buffer_status), status.bytes);

	sprintf(text, "%u", status.errors.symbol);
	gtk_label_set_text(GTK_LABEL(widgets.symbol), text);

	sprintf(text, "%u", status.errors.block);
	gtk_label_set_text(GTK_LABEL(widgets.block), text);

	sprintf(text, "%u", status.errors.sector);
	gtk_label_set_text(GTK_LABEL(widgets.sector), text);

	sprintf(text, "%u", status.errors.overrun);
	gtk_label_set_text(GTK_LABEL(widgets.overrun), text);

	sprintf(text, "%u", status.errors.underflow);
	gtk_label_set_text(GTK_LABEL(widgets.underflow), text);

	sprintf(text, "%u", status.worst_match);
	gtk_label_set_text(GTK_LABEL(widgets.worst), text);

	sprintf(text, "%u", status.best_nonmatch);
	gtk_label_set_text(GTK_LABEL(widgets.best), text);

	sprintf(text, "%u", status.least_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.least), text);

	sprintf(text, "%u", status.most_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.most), text);

	return(TRUE);
}
