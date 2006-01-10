/*
 */

#include <gst/gst.h>
#include <backer.h>
#include <bkr_video_out.h>

#define PIXELS_PER_BIT 4
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
	pos = draw_bit_nh(draw_bit_nh(pos, colour), colour);
	return pos;
}


static guint32 *draw_byte(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, guchar byte, guint32 colour)
{
	gint i;

	for(i = 0x80; i; i >>= 1)
		pos = pixel_func(pos, byte & i ? colour : 0);

	return pos;
}


static gint draw(guint32 *(*pixel_func)(guint32 *, guint32), GstBuffer *out, gint width, guchar *in, gint byte_count)
{
	guint32 *pixel = (guint32 *) (GST_BUFFER_DATA(out) + GST_BUFFER_SIZE(out));

	while((GST_BUFFER_SIZE(out) < GST_BUFFER_MAXSIZE(out)) && (byte_count > 0)) {
		if(GST_BUFFER_SIZE(out) % (BYTES_PER_PIXEL * width) == 0) {
			pixel = draw_byte(pixel_func, pixel, 0x45, 0x00ffffff);
		} else {
			pixel = draw_byte(pixel_func, pixel, *in++, 0x00ffffff);
			byte_count--;
		}
		GST_BUFFER_SIZE(out) += BYTES_PER_PIXEL * PIXELS_PER_BIT * 8;
	}

	return byte_count;
}



/*
 * ============================================================================
 *
 *                           GStreamer Support Code
 *
 * ============================================================================
 */

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
	GstBuffer *buf = GST_BUFFER(in);
	gint n = GST_BUFFER_SIZE(buf);

	g_return_if_fail(buf != NULL);

	while(n) {
		if(!filter->outbuf) {
			filter->outbuf = gst_buffer_new_and_alloc(BYTES_PER_PIXEL * filter->width * filter->height);
			g_return_if_fail(filter->outbuf != NULL);
			GST_BUFFER_SIZE(filter->outbuf) = 0;
		}

		n = draw(filter->pixel_func, filter->outbuf, filter->width, GST_BUFFER_DATA(buf) + (GST_BUFFER_SIZE(buf) - n), n);

		if(GST_BUFFER_SIZE(filter->outbuf) >= GST_BUFFER_MAXSIZE(filter->outbuf)) {
			gst_pad_push(filter->srcpad, GST_DATA(filter->outbuf));
			filter->field ^= 1;
			/* FIXME: do I have to unref it? */
			filter->outbuf = NULL;
		}
	}
	gst_buffer_unref(in);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static GstElementClass *parent_class = NULL;

static void class_init(BkrVideoOutClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);

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
	filter->field = 1;	/* fields are counted from 1 */
	filter->outbuf = NULL;
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
