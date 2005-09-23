/*
 * bkrmonitor
 *
 * Graphical display of Backer device driver status.
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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include <backer.h>
#include <bkr_proc_io.h>


#define  PROGRAM_NAME    "bkrmonitor"
#define  DEFAULT_UPDATE  100                    /* milliseconds */
#define  MIN_UPDATE      20                     /* milliseconds */
#define  DECAY_INTERVAL  60                     /* sectors */
#define	 WIDGET_SPACING  7                      /* pixels */
#define  PROC_DIR        "/proc/sys/dev/backer"

#define  __STRINGIFY(x)  #x
#define  STRINGIFY(x)    __STRINGIFY(x)

struct unit_t {
	FILE  *file;
	struct {
		GtkWidget  *state;
		GtkWidget  *vmode, *density, *format;
		GtkWidget  *sector;
		GtkWidget  *total;
		GtkWidget  *symbol, *sectors, *sector_runs, *frame, *underrun;
		GtkWidget  *worst, *best, *smallest, *largest;
		GtkWidget  *buffer_status;
	} widget;
	struct {
		GtkWidget  *widget;
		int  rate;
		int  last_sector;
	} error_rate;
};

/*
 * Global data
 */

static int  update_interval = DEFAULT_UPDATE;


/*
 * Update the contents of a unit's notebook page.
 */

gint update_status(gpointer data)
{
	struct unit_t  *unit = (struct unit_t *) data;
	struct bkr_proc_status_t  proc_status;
	char  text[20];

	if(bkr_proc_read_status(unit->file, &proc_status) < 0)
		return(TRUE);

	gtk_label_set_text(GTK_LABEL(unit->widget.state), proc_status.state);

	switch(BKR_VIDEOMODE(proc_status.mode)) {
		case BKR_NTSC:
		gtk_label_set_text(GTK_LABEL(unit->widget.vmode), "NTSC");
		break;

		case BKR_PAL:
		gtk_label_set_text(GTK_LABEL(unit->widget.vmode), "PAL");
		break;

		default:
		gtk_label_set_text(GTK_LABEL(unit->widget.vmode), "???");
		break;
	}

	switch(BKR_DENSITY(proc_status.mode)) {
		case BKR_HIGH:
		gtk_label_set_text(GTK_LABEL(unit->widget.density), "HIGH");
		break;

		case BKR_LOW:
		gtk_label_set_text(GTK_LABEL(unit->widget.density), "LOW");
		break;

		default:
		gtk_label_set_text(GTK_LABEL(unit->widget.vmode), "???");
		break;
	}

	switch(BKR_CODEC(proc_status.mode)) {
		case BKR_RAW:
		gtk_label_set_text(GTK_LABEL(unit->widget.format), "RAW");
		break;

		case BKR_SP:
		gtk_label_set_text(GTK_LABEL(unit->widget.format), "SP/LP");
		break;

		case BKR_EP:
		gtk_label_set_text(GTK_LABEL(unit->widget.format), "EP");
		break;

		default:
		gtk_label_set_text(GTK_LABEL(unit->widget.vmode), "???");
		break;
	}

	gtk_progress_configure(GTK_PROGRESS(unit->error_rate.widget), 0, 0, proc_status.parity/2*DECAY_INTERVAL);
	gtk_progress_configure(GTK_PROGRESS(unit->widget.buffer_status), 0, 0, proc_status.buffer_size);

	proc_status.recent_block *= DECAY_INTERVAL;
	if(proc_status.recent_block > unit->error_rate.rate)
		unit->error_rate.rate = proc_status.recent_block;
	else if(unit->error_rate.rate > 0) {
		unit->error_rate.rate -= proc_status.sector_number - unit->error_rate.last_sector;
		if(unit->error_rate.rate < 0)
			unit->error_rate.rate = 0;
	}
	unit->error_rate.last_sector = proc_status.sector_number;

	gtk_progress_set_value(GTK_PROGRESS(unit->error_rate.widget), unit->error_rate.rate);
	gtk_progress_set_value(GTK_PROGRESS(unit->widget.buffer_status), proc_status.bytes_in_buffer);

	sprintf(text, "%+010d", proc_status.sector_number);
	gtk_label_set_text(GTK_LABEL(unit->widget.sector), text);

	sprintf(text, "%u", proc_status.total_errors);
	gtk_label_set_text(GTK_LABEL(unit->widget.total), text);

	sprintf(text, "%u of %u", proc_status.worst_block, proc_status.parity/2);
	gtk_label_set_text(GTK_LABEL(unit->widget.symbol), text);

	sprintf(text, "%u", proc_status.bad_sectors);
	gtk_label_set_text(GTK_LABEL(unit->widget.sectors), text);

	sprintf(text, "%u", proc_status.bad_sector_runs);
	gtk_label_set_text(GTK_LABEL(unit->widget.sector_runs), text);

	sprintf(text, "%u", proc_status.frame_errors);
	gtk_label_set_text(GTK_LABEL(unit->widget.frame), text);

	sprintf(text, "%u", proc_status.underrun_errors);
	gtk_label_set_text(GTK_LABEL(unit->widget.underrun), text);

	sprintf(text, "%3u/%-3u", proc_status.worst_key, proc_status.max_key_weight);
	gtk_label_set_text(GTK_LABEL(unit->widget.worst), text);

	sprintf(text, "%3u/%-3u", proc_status.best_nonkey, proc_status.max_key_weight);
	gtk_label_set_text(GTK_LABEL(unit->widget.best), text);

	sprintf(text, "%u", proc_status.smallest_field);
	gtk_label_set_text(GTK_LABEL(unit->widget.smallest), text);

	sprintf(text, "%u", proc_status.largest_field);
	gtk_label_set_text(GTK_LABEL(unit->widget.largest), text);

	return(TRUE);
}


/*
 * Generate the contents of a unit's notebook page.
 */

GtkWidget *create_unit_page(struct unit_t *unit)
{
	GtkWidget  *vbox, *hbox;
	GtkWidget  *table;
	GtkWidget  *widget;

	unit->error_rate.rate = 0;
	unit->error_rate.last_sector = 0;

	vbox = gtk_vbox_new(FALSE, WIDGET_SPACING);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), WIDGET_SPACING);

	/* Mode indicators */

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);

	unit->widget.state = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), unit->widget.state);

	hbox = gtk_hbox_new(TRUE, WIDGET_SPACING);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	unit->widget.vmode = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), unit->widget.vmode);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	unit->widget.density = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), unit->widget.density);

	widget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(widget), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);

	unit->widget.format = gtk_label_new("???");
	gtk_container_add(GTK_CONTAINER(widget), unit->widget.format);

	/* Error counts */

	table = gtk_table_new(7, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Sector Number");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("Total Errors Corrected");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);
	widget = gtk_label_new("Errors in Worst Block");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 2, 3);
	widget = gtk_label_new("Bad Sectors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 3, 4);
	widget = gtk_label_new("Bad Sector Runs");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 4, 5);
	widget = gtk_label_new("Framing Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 5, 6);
	widget = gtk_label_new("Under-runs");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 6, 7);

	unit->widget.sector = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.sector, 1, 2, 0, 1);

	unit->widget.total = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.total, 1, 2, 1, 2);

	unit->widget.symbol = gtk_label_new("0 of 0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.symbol, 1, 2, 2, 3);

	unit->widget.sectors = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.sectors, 1, 2, 3, 4);

	unit->widget.sector_runs = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.sector_runs, 1, 2, 4, 5);

	unit->widget.frame = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.frame, 1, 2, 5, 6);

	unit->widget.underrun = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.underrun, 1, 2, 6, 7);

	/* Health information */

	table = gtk_table_new(4, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Worst Key");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("Closest Non-Key");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);
	widget = gtk_label_new("Smallest Video Field");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 2, 3);
	widget = gtk_label_new("Largest Video Field");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 3, 4);

	unit->widget.worst = gtk_label_new("  0/???");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.worst, 1, 2, 0, 1);

	unit->widget.best = gtk_label_new("  0/???");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.best, 1, 2, 1, 2);

	unit->widget.smallest = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.smallest, 1, 2, 2, 3);

	unit->widget.largest = gtk_label_new("0");
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.largest, 1, 2, 3, 4);

	/* Errors and I/O buffer bar graphs */

	table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table), 10);
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

	widget = gtk_label_new("Recent Errors");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 0, 1);
	widget = gtk_label_new("I/O Buffer");
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, 1, 2);

	unit->error_rate.widget = gtk_progress_bar_new();
	gtk_table_attach_defaults(GTK_TABLE(table), unit->error_rate.widget, 1, 2, 0, 1);
	unit->widget.buffer_status = gtk_progress_bar_new();
	gtk_table_attach_defaults(GTK_TABLE(table), unit->widget.buffer_status, 1, 2, 1, 2);

	return(vbox);
}


/*
 * Handle unit page changes.
 */

gint switch_page(GtkNotebook *notebook, GtkNotebookPage *page, gint page_num, gpointer data)
{
	struct unit_t  *unit = (struct unit_t *) data;
	static int  handle = 0;

	if(handle)
		gtk_timeout_remove(handle);

	handle = gtk_timeout_add(update_interval, update_status, &unit[page_num]);

	return(TRUE);
}


/*
 * Entry point
 */

int main(int argc, char *argv[])
{
	int  i;
	char  *include_file = NULL;
	char  message[50];
	struct unit_t  *unit = NULL;
	GtkWidget  *window;
	GtkWidget  *notebook;
	GtkWidget  *vbox;
	GtkWidget  *widget;
	DIR  *dir;
	struct dirent  *dirent;

	/*
	 * Parse command line.
	 */

	gtk_init(&argc, &argv);
	while((i = getopt(argc, argv, "f::ht:")) != EOF)
		switch(i) {
			case 'f':
			if(optarg)
				include_file = optarg;
			else
				include_file = "";
			break;

			case 't':
			update_interval = atoi(optarg);
			break;

			case 'h':
			default:
			fputs(
	"Usage:  " PROGRAM_NAME " [options]\n" \
	"the following options are recognized:\n" \
	"	-f[file] Also read status input from file (default = stdin)\n" \
	"	-t num   Set the update interval to num milliseconds\n" \
	"	-h       Display this usage message\n", stderr);
			exit(1);
		}
	if(update_interval < MIN_UPDATE)
		update_interval = MIN_UPDATE;

	/*
	 * Create the program's window
	 */

	/* window */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
	gtk_window_set_title(GTK_WINDOW(window), "Backer Monitor");
	gtk_container_set_border_width(GTK_CONTAINER(window), WIDGET_SPACING);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_exit), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	vbox = gtk_vbox_new(FALSE, WIDGET_SPACING);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* Notebook */

	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	/* Close button */

	widget = gtk_button_new_with_label("Close");
	gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(widget), "clicked", GTK_SIGNAL_FUNC(gtk_exit), NULL);

	/*
	 * Scan for units and create a page for each.
	 */

	i = 0;
	dir = opendir(PROC_DIR);
	if(dir) {
		readdir(dir); readdir(dir);	/* skip "." and ".." */
		for(i = 0; (dirent = readdir(dir)); ) {
			sprintf(message, PROC_DIR"/%s/status", dirent->d_name);
			unit = realloc(unit, (i + 1) * sizeof(*unit));
			unit[i].file = fopen(message, "r");
			if(!unit[i].file) {
				unit = realloc(unit, i * sizeof(*unit));
				continue;
			}
			sprintf(message, "Unit %s", dirent->d_name);
			widget = create_unit_page(&unit[i]);
			gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget, gtk_label_new(message));
			i++;
		}
		closedir(dir);
	}

	if(include_file) {
		unit = realloc(unit, (i + 1) * sizeof(*unit));
		if(include_file[0]) {
			unit[i].file = fopen(include_file, "r");
			sprintf(message, "%s", include_file);
		} else {
			unit[i].file = stdin;
			sprintf(message, "stdin");
		}
		if(unit[i].file) {
			widget = create_unit_page(&unit[i]);
			gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget, gtk_label_new(message));
			i++;
		} else {
			fprintf(stderr, PROGRAM_NAME ": unable to open %s\n", message);
			unit = realloc(unit, i * sizeof(*unit));
		}
	}

	if(!i) {
		sprintf(message, "No units found in " PROC_DIR);
		widget = gtk_label_new(message);
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget, gtk_label_new("Error"));
	} else
		gtk_signal_connect(GTK_OBJECT(notebook), "switch-page", GTK_SIGNAL_FUNC(switch_page), unit);

	/*
	 * And away we go...
	 */

	gtk_widget_show_all(window);
	gtk_main();
	exit(0);
}

