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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "backer.h"


#define  PROGRAM_NAME    "bkrmonitor"
#define  PROC_ENTRY      "/proc/driver/backer"
#define  DEFAULT_UPDATE  50                     /* milliseconds */
#define  MIN_UPDATE      20                     /* milliseconds */
#define  DECAY_INTERVAL  60                     /* sectors */
#define  MAX_UNITS       3


/*
 * Function prototypes
 */

gint  update_status(gpointer);
void  read_proc(FILE *);


/*
 * Global data
 */

FILE  *procfile = NULL;
int  update_interval = -1;
int  monitor_device = 0;
struct
	{
	GtkWidget  *state;
	GtkWidget  *vmode, *density, *format;
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
struct
	{
	char  state[sizeof("suspended")];
	unsigned int   mode;
	unsigned long  sector_number;
	unsigned int   total_errors;
	unsigned int   worst_block;
	unsigned int   parity;
	unsigned int   recent_block;
	unsigned int   bad_blocks;
	unsigned int   frame_errors;
	unsigned int   overrun_errors;
	unsigned int   underflow_errors;
	unsigned int   worst_key;
	unsigned int   best_nonkey;
	unsigned int   least_skipped;
	unsigned int   most_skipped;
	unsigned int   bytes_in_buffer;
	unsigned int   buffer_size;
	} proc_data;


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  i;
	char  message[50];
	GtkWidget  *window;
	GtkWidget  *vbox, *hbox;
	GtkWidget  *table;
	GtkWidget  *widget;

	/*
	 * Parse command line.
	 */

	gtk_init(&argc, &argv);
	while((i = getopt(argc, argv, "d:ht:")) != EOF)
		switch(i)
			{
			case 'd':
			if(optarg[0] == 's')
				{
				procfile = stdin;
				monitor_device = 0;
				}
			else
				{
				procfile = NULL;
				monitor_device = atoi(optarg);
				}
			break;

			case 't':
			update_interval = atoi(optarg);
			break;

			case 'h':
			default:
			puts(
	"Usage:  " PROGRAM_NAME " [options]\n" \
	"the following options are recognized:\n" \
	"       -d num      Monitor device number num (default 0, \"s\" = stdin)\n" \
	"       -t num      Set the update interval to num milliseconds\n" \
	"       -h          Display usage");
			exit(1);
			}
	if(update_interval < MIN_UPDATE)
		update_interval = DEFAULT_UPDATE;

	/*
	 * Open proc file and get initial data.
	 */

	if(procfile == NULL)
		procfile = fopen(PROC_ENTRY, "r");
	if(procfile == NULL)
		{
		perror(PROGRAM_NAME " : " PROC_ENTRY);
		exit(1);
		}
	read_proc(procfile);

	/*
	 * Create the program's window
	 */

	/* window */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
	sprintf(message, "Backer Monitor (Unit %u)", monitor_device);
	gtk_window_set_title(GTK_WINDOW(window), message);
	gtk_container_set_border_width(GTK_CONTAINER(window), 7);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_exit), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* Mode indicators */

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	widgets.state = gtk_label_new(NULL);
	gtk_container_add(GTK_CONTAINER(widget), widgets.state);

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	widgets.vmode = gtk_label_new(NULL);
	gtk_container_add(GTK_CONTAINER(widget), widgets.vmode);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	widgets.density = gtk_label_new(NULL);
	gtk_container_add(GTK_CONTAINER(widget), widgets.density);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	widgets.format = gtk_label_new(NULL);
	gtk_container_add(GTK_CONTAINER(widget), widgets.format);

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

	widgets.symbol = gtk_label_new("0 of 0");
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

	/* Errors and I/O buffer bar graphs */

	table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Recent Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("I/O Buffer");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);

	error_rate.widget = gtk_progress_bar_new();
	gtk_table_attach_defaults(GTK_TABLE(table), error_rate.widget, 1, 2, 0, 1);
	widgets.buffer_status = gtk_progress_bar_new();
	gtk_table_attach_defaults(GTK_TABLE(table), widgets.buffer_status, 1, 2, 1, 2);

	/* Close button */

	widget = gtk_button_new_with_label("Close");
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(widget), "clicked", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	/* Done */

	gtk_widget_show_all(window);


	/*
	 * And away we go...
	 */

	error_rate.rate = 0;
	error_rate.last_block = 0;

	gtk_timeout_add(update_interval, update_status, NULL);

	gtk_main();

	exit(0);
}


/*
 * Status update call-back function
 */

gint update_status(gpointer data)
{
	char  text[20];

	read_proc(procfile);

	gtk_label_set_text(GTK_LABEL(widgets.state), proc_data.state);

	switch(BKR_VIDEOMODE(proc_data.mode))
		{
		case BKR_NTSC:
		gtk_label_set_text(GTK_LABEL(widgets.vmode), "NSTC");
		break;

		case BKR_PAL:
		gtk_label_set_text(GTK_LABEL(widgets.vmode), "PAL");
		break;
		}

	switch(BKR_DENSITY(proc_data.mode))
		{
		case BKR_HIGH:
		gtk_label_set_text(GTK_LABEL(widgets.density), "HIGH");
		break;

		case BKR_LOW:
		gtk_label_set_text(GTK_LABEL(widgets.density), "LOW");
		break;
		}

	switch(BKR_FORMAT(proc_data.mode))
		{
		case BKR_RAW:
		gtk_label_set_text(GTK_LABEL(widgets.format), "RAW");
		break;

		case BKR_SP:
		gtk_label_set_text(GTK_LABEL(widgets.format), "SP/LP");
		break;

		case BKR_EP:
		gtk_label_set_text(GTK_LABEL(widgets.format), "EP");
		break;
		}

	gtk_progress_configure(GTK_PROGRESS(error_rate.widget), 0, 0, proc_data.parity/2*DECAY_INTERVAL);
	gtk_progress_configure(GTK_PROGRESS(widgets.buffer_status), 0, 0, proc_data.buffer_size);

	proc_data.recent_block *= DECAY_INTERVAL;
	if(proc_data.recent_block > error_rate.rate)
		error_rate.rate = proc_data.recent_block;
	else if(error_rate.rate > 0)
		{
		error_rate.rate -= proc_data.sector_number - error_rate.last_block;
		if(error_rate.rate < 0)
			error_rate.rate = 0;
		}
	error_rate.last_block = proc_data.sector_number;

	gtk_progress_set_value(GTK_PROGRESS(error_rate.widget), error_rate.rate);
	gtk_progress_set_value(GTK_PROGRESS(widgets.buffer_status), proc_data.bytes_in_buffer);

	sprintf(text, "%lu", proc_data.sector_number);
	gtk_label_set_text(GTK_LABEL(widgets.sector), text);

	sprintf(text, "%u", proc_data.total_errors);
	gtk_label_set_text(GTK_LABEL(widgets.total), text);

	sprintf(text, "%u of %u", proc_data.worst_block, proc_data.parity/2);
	gtk_label_set_text(GTK_LABEL(widgets.symbol), text);

	sprintf(text, "%u", proc_data.bad_blocks);
	gtk_label_set_text(GTK_LABEL(widgets.block), text);

	sprintf(text, "%u", proc_data.frame_errors);
	gtk_label_set_text(GTK_LABEL(widgets.frame), text);

	sprintf(text, "%u", proc_data.overrun_errors);
	gtk_label_set_text(GTK_LABEL(widgets.overrun), text);

	sprintf(text, "%u", proc_data.underflow_errors);
	gtk_label_set_text(GTK_LABEL(widgets.underflow), text);

	sprintf(text, "%u", proc_data.worst_key);
	gtk_label_set_text(GTK_LABEL(widgets.worst), text);

	sprintf(text, "%u", proc_data.best_nonkey);
	gtk_label_set_text(GTK_LABEL(widgets.best), text);

	sprintf(text, "%u", proc_data.least_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.least), text);

	sprintf(text, "%u", proc_data.most_skipped);
	gtk_label_set_text(GTK_LABEL(widgets.most), text);

	return(TRUE);
}


/*
 * read_proc()
 *
 * Parse the contents of the /proc file.
 */

void read_proc(FILE *file)
{
	int unit;

	while(!feof(file) && !ferror(file))
		{
		fscanf(file, "%*17c%u %s\n"
		             "%*17c%u\n"
		             "%*17c%lu\n"
		             "%*17c%u\n"
		             "%*17c%u / %u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u\n"
		             "%*17c%u / %u\n",
		       &unit, proc_data.state,
		       &proc_data.mode,
		       &proc_data.sector_number,
		       &proc_data.total_errors,
		       &proc_data.worst_block, &proc_data.parity,
		       &proc_data.recent_block,
		       &proc_data.bad_blocks,
		       &proc_data.frame_errors,
		       &proc_data.overrun_errors,
		       &proc_data.underflow_errors,
		       &proc_data.worst_key,
		       &proc_data.best_nonkey,
		       &proc_data.least_skipped,
		       &proc_data.most_skipped,
		       &proc_data.bytes_in_buffer, &proc_data.buffer_size);
		if(unit == monitor_device)
			break;
		}
	fseek(file, 0L, SEEK_SET);

	return;
}
