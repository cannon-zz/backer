/*
 * bkr_screen
 *
 * Copyright (C) 2000 Kipp C. Cannon
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
 * Simulated Backer video display widget description
 */

#ifndef _BKR_SCREEN_H_
#define _BKR_SCREEN_H_

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#define BKRSCREEN_TYPE			(bkr_screen_get_type())
#define BKRSCREEN(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKRSCREEN_TYPE, BkrScreen))
#define BKRSCREEN_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKRSCREEN_TYPE, BkrScreenClass))
#define IS_BKRSCREEN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKRSCREEN_TYPE))
#define IS_BKRSCREEN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((kclass), BKRSCREEN_TYPE))

typedef struct _BkrScreen	BkrScreen;
typedef struct _BkrScreenClass	BkrScreenClass;

struct _BkrScreenClass {
	GtkDrawingAreaClass  parent_class;
};

struct _BkrScreen {
	GtkDrawingArea drawing_area;

	gint mode;
	guint bytes_per_line;
	guint *(*draw_bit)(guint *, guint);
	guint *data;
	guint data_length;
};

guint bkr_screen_get_type(void);
GtkWidget* bkr_screen_new(gint);
void bkr_screen_set_mode(BkrScreen *, gint);
void bkr_screen_set_data(BkrScreen *, const gchar *);

#endif /* _BKR_SCREEN_H_ */
