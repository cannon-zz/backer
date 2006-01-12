/*
 */

#include <gst/gst.h>
#include <backer.h>
#include <bkr_video_out.h>

#define BYTES_PER_PIXEL 4

#define DEFAULT_VIDMODE BKR_NTSC
#define DEFAULT_DENSITY BKR_HIGH


/*
 * ============================================================================
 *
 *                                 Codec Code
 *
 * ============================================================================
 */

static guint32 *draw_bit_h(guint32 *pos, guint32 colour)
{
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	return pos;
}


static guint32 *draw_bit_l(guint32 *pos, guint32 colour)
{
	return draw_bit_h(draw_bit_h(pos, colour), colour);
}


static guint32 *draw_byte(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, guchar byte, guint32 colour)
{
	gint i;

	for(i = 0x80; i; i >>= 1)
		pos = pixel_func(pos, byte & i ? colour : 0);

	return pos;
}


static guint32 *draw_line(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, const guchar *data, gint n, guint32 colour)
{
	pos = draw_byte(pixel_func, pos, 0x45, colour);
	while(n--)
		pos = draw_byte(pixel_func, pos, *data++, colour);

	return pos;
}


static void draw_field(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *dest, gint bytes_per_line, gint lines, const guchar *data)
{
	while(lines--) {
		dest = draw_line(pixel_func, dest, data, bytes_per_line, 0x00ffffff);
		data += bytes_per_line;
	}
}



/*
 * ============================================================================
 *
 *                           GStreamer Support Code
 *
 * ============================================================================
 */

/*
 * Element properties
 */

enum property {
	VIDEOMODE,
	DENSITY
};


static void set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);
}


static void get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);
}


/*
 * Source pad getcaps function.
 */

static GstCaps *src_getcaps(GstPad *pad)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));
	GstCaps *caps = gst_caps_new_simple(
		"video/x-raw-rgb",
		"width", G_TYPE_INT, filter->width,
		"height", G_TYPE_INT, filter->height,
		"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
		"bpp", G_TYPE_INT, 32,
		"depth", G_TYPE_INT, 24,
		"framerate", G_TYPE_DOUBLE, 60.0,
		NULL
	);

	return caps;
}


/*
 * Source pad link function.
 */

static GstPadLinkReturn src_link(GstPad *pad, const GstCaps *caps)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));

	/* FIXME:  wtf am I supposed to do? */
	return GST_PAD_LINK_OK;
}


/*
 * Pad chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void chain(GstPad *pad, GstData *in)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(GST_OBJECT_PARENT(pad));
	GstBuffer *inbuf = GST_BUFFER(in);
	GstBuffer *odd, *even;
	gint lines;
	gint oddlines = filter->height;
	gint evenlines = filter->height - 10;	/* FIXME: 10 = bytes_per_line */
	const guchar *odddata, *evendata;

	g_return_if_fail(inbuf != NULL);

	/* compute number of lines to be drawn in each field */
	lines = GST_BUFFER_SIZE(inbuf) / 10;	/* FIXME: 10 = bytes_per_line */
	if(lines < oddlines) {
		oddlines = lines;
		evenlines = 0;
	} else if(lines < oddlines + evenlines)
		evenlines = lines - oddlines;

	/* set pointers to source data */
	odddata = GST_BUFFER_DATA(inbuf);
	evendata = odddata + (oddlines * 10);	/* FIXME: 10 = bytes_per_line */

	/* allocate output buffers */
	odd = gst_buffer_new_and_alloc(BYTES_PER_PIXEL * filter->width * filter->height);
	even = gst_buffer_new_and_alloc(BYTES_PER_PIXEL * filter->width * filter->height);
	g_return_if_fail((odd != NULL) && (even != NULL));

	/* draw bytes in output buffers */
	draw_field(filter->pixel_func, (guint32 *) GST_BUFFER_DATA(odd), 10, oddlines, odddata);	/* FIXME: 10 = bytes_per_line */
	draw_field(filter->pixel_func, (guint32 *) GST_BUFFER_DATA(even), 10, evenlines, evendata);	/* FIXME: 10 = bytes_per_line */
	gst_buffer_unref(in);

	/* send buffers on their way */
	gst_pad_push(filter->srcpad, GST_DATA(odd));
	gst_pad_push(filter->srcpad, GST_DATA(even));
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static GstElementClass *parent_class = NULL;

static void class_init(BkrVideoOutClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void base_init(BkrVideoOutClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer Video Out",
		"Codec/Encoder/Video",
		"Simulates a Backer's byte-stream to video conversion",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void instance_init(BkrVideoOut *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_pad_set_getcaps_function(filter->srcpad, src_getcaps);
	/*gst_pad_set_link_function(filter->srcpad, src_link);*/
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

	/* internal state */
	filter->vidmode = DEFAULT_VIDMODE;
	filter->density = DEFAULT_DENSITY;

	switch(filter->vidmode) {
	case BKR_NTSC:
		filter->height = 253;
		break;

	case BKR_PAL:
		filter->height = 305;
		break;
	}
	switch(filter->density) {
	case BKR_HIGH:
		filter->width = 4 * 8 * 11; /* pixels/bit * bits/byte * bytes/line */
		filter->pixel_func = draw_bit_h;
		break;

	case BKR_LOW:
		filter->width = 8 * 8 * 5; /* pixels/bit * bits/byte * bytes/line */
		filter->pixel_func = draw_bit_l;
		break;
	}
}


/*
 * Entry point.
 */

GType bkr_video_out_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrVideoOutClass),
			.class_init = (GClassInitFunc) class_init,
			.base_init = (GBaseInitFunc) base_init,
			.instance_size = sizeof(BkrVideoOut),
			.instance_init = (GInstanceInitFunc) instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrVideoOut", &info, 0);
	}
	return type;
}


static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "bkr_video_out", GST_RANK_NONE, BKR_VIDEO_OUT_TYPE);
}


/*
 * This is the structure that gst-register looks for.
 */

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "bkr_video_out", "Backer Compatible Byte-stream to Video Converter", plugin_init, PACKAGE_VERSION, "GPL", PACKAGE_NAME, "http://linbacker.sf.net")
