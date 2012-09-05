/*
 * bkr_screen
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "bkr_screen.h"
#include "backer.h"


/*
 * Video functions
 */

static guint *draw_bit_nh(guint *pos, guint colour)
{
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	return(pos);
}


static guint *draw_bit_ph(guint *pos, guint colour)
{
	pos = draw_bit_nh(pos, colour);
	*pos++ = colour;
	return(pos);
}


static guint *draw_bit_nl(guint *pos, guint colour)
{
	pos = draw_bit_nh(draw_bit_nh(pos, colour), colour);
	return(pos);
}


static guint *draw_bit_pl(guint *pos, guint colour)
{
	pos = draw_bit_ph(draw_bit_ph(pos, colour), colour);
	return(pos);
}


static guint *draw_byte(guint *(*pixel_func)(guint *, guint), guint *pos, unsigned char byte, guint colour)
{
	gint i;

	for(i = 0x80; i; i >>= 1)
		pos = pixel_func(pos, byte & i ? colour : 0);

	return(pos);
}


/*
 * Event handlers
 */

static void expose(BkrScreen *widget, gpointer *nul)
{
	gchar *data = widget->data;
	guint *buff, *pos;
	gint width, height, x, y;

	gtk_widget_get_size_request(GTK_WIDGET(widget), &width, &height);
	pos = buff = malloc(width * height * sizeof(*buff));

	for(y = 0; y < height; y++) {
		pos = draw_byte(widget->pixel_func, pos, 0x45, 0x00ffffff);
		for(x = 0; x < widget->bytes_per_line; x++)
			pos = draw_byte(widget->pixel_func, pos, *data++, 0x00ffffff);
	}

	gdk_draw_rgb_32_image(GTK_WIDGET(widget)->window, GTK_WIDGET(widget)->style->fg_gc[GTK_STATE_NORMAL], 0, 0, width, height, GDK_RGB_DITHER_NONE, (void *) buff, 4 * width);

	free(buff);
}


/*
 * Widget class infrastructure
 */

static void class_init(BkrScreenClass *class)
{
}


static void set_mode(BkrScreen *, gint);
static void object_init(BkrScreen *widget)
{
	widget->mode = ~(BKR_NTSC | BKR_HIGH);	/* force mode set */
	widget->data = NULL;
	set_mode(widget, BKR_NTSC | BKR_HIGH);
}


guint bkr_screen_get_type(void)
{
	static GType type = 0;
	static const GTypeInfo info = {
		.class_size = sizeof(BkrScreenClass),
		.class_init = (GClassInitFunc) class_init,
		.instance_size = sizeof(BkrScreen),
		.n_preallocs = 0,
		.instance_init = (GInstanceInitFunc) object_init
	};

	if(!type)
		type = g_type_register_static(GTK_TYPE_DRAWING_AREA, "BkrScreen", &info, 0);

	return(type);
}


/*
 * Application interface:  set screen contents
 */


static void set_data(BkrScreen *widget, const gchar *data)
{
	if(data)
		memcpy(widget->data, data, widget->data_length);
	else
		memset(widget->data, 0, widget->data_length);
}

void bkr_screen_set_data(BkrScreen *widget, const gchar *data)
{
	g_return_if_fail(IS_BKRSCREEN(widget));

	set_data(widget, data);
}


/*
 * Application interface:  set the screen's video mode
 */

static void set_mode(BkrScreen *widget, gint mode)
{
	gint vid_mode = BKR_VIDEOMODE(mode);
	gint density = BKR_DENSITY(mode);
	gint width, height;

	if(widget->mode == mode)
		return;

	widget->mode = mode;

	free(widget->data);

	if(vid_mode == BKR_NTSC) {
		if(density == BKR_HIGH) {
			widget->pixel_func = draw_bit_nh;
			widget->bytes_per_line = 10;
			/* pixels/bit * bits/byte * bytes/line */
			width = 4 * 8 * 11;
			height = 253;
		} else {
			widget->pixel_func = draw_bit_nl;
			widget->bytes_per_line = 4;
			width = 8 * 8 * 5;
			height = 253;
		}
	} else {
		if(density == BKR_HIGH) {
			widget->pixel_func = draw_bit_ph;
			widget->bytes_per_line = 10;
			width = 5 * 8 * 11;
			height = 305;
		} else {
			widget->pixel_func = draw_bit_pl;
			widget->bytes_per_line = 4;
			width = 10 * 8 * 5;
			height = 305;
		}
	}

	gtk_widget_set_size_request(GTK_WIDGET(widget), width, height);

	widget->data_length = widget->bytes_per_line * height;
	widget->data = malloc(widget->data_length);

	set_data(widget, NULL);
}


void bkr_screen_set_vid_mode(BkrScreen *widget, gint mode)
{
	g_return_if_fail(IS_BKRSCREEN(widget));

	set_mode(widget, mode);
}


/*
 * Application interface:  create a new screen
 */

GtkWidget *bkr_screen_new(gint mode)
{
	GtkWidget *widget = GTK_WIDGET(g_object_new(BKRSCREEN_TYPE, NULL));

	if(widget) {
		BKRSCREEN(widget)->mode = ~mode;	/* force mode set */
		set_mode(BKRSCREEN(widget), mode);
	}
	g_signal_connect(G_OBJECT(widget), "expose_event", G_CALLBACK(expose), NULL);

	return(widget);
}
