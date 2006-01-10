/*
 */

#include <gst/gst.h>
#include "bkr_video_out.h"

#define WIDTH 352
#define HEIGHT 253
#define PIXELS_PER_BIT 4
#define BYTES_PER_LINE 11


/*
 * ============================================================================
 *
 *                                 Codec Code
 *
 * ============================================================================
 */

static guint32 *draw_bit_nh(guint32 *pos, guint32 colour)
{
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	return(pos);
}


static guint32 *draw_bit_ph(guint32 *pos, guint32 colour)
{
	pos = draw_bit_nh(pos, colour);
	*pos++ = colour;
	return(pos);
}


static guint32 *draw_bit_nl(guint32 *pos, guint32 colour)
{
	pos = draw_bit_nh(draw_bit_nh(pos, colour), colour);
	return(pos);
}


static guint32 *draw_bit_pl(guint32 *pos, guint32 colour)
{
	pos = draw_bit_ph(draw_bit_ph(pos, colour), colour);
	return(pos);
}


static guint32 *draw_byte(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, unsigned char byte, guint32 colour)
{
	gint i;

	for(i = 0x80; i; i >>= 1)
		pos = pixel_func(pos, byte & i ? colour : 0);

	return(pos);
}


static void draw(GstBuffer *out, guchar *in, gint *in_count)
{
	guint32 *pixel = (guint32 *) (GST_BUFFER_DATA(out) + GST_BUFFER_SIZE(out));

	while((GST_BUFFER_SIZE(out) < GST_BUFFER_MAXSIZE(out)) && (*in_count > 0)) {
		if(GST_BUFFER_SIZE(out) % (4 * WIDTH) == 0) {
			pixel = draw_byte(draw_bit_nh, pixel, 0x45, 0x00ffffff);
		} else {
			pixel = draw_byte(draw_bit_nh, pixel, *in++, 0x00ffffff);
			(*in_count)--;
		}
		GST_BUFFER_SIZE(out) += 4 * PIXELS_PER_BIT * 8;
	}
}



/*
 * ============================================================================
 *
 *                           GStreamer Support Code
 *
 * ============================================================================
 */

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw-rgb, "
		"width = (int) 352, "	/* WIDTH */
		"height = (int) 253, "	/* HEIGHT */
		"framerate = (double) 60.0"
	)
);

static GstElementClass *parent_class = NULL;


/*
 * Sink pad link function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadLinkFunction
 */

static GstPadLinkReturn sink_link(GstPad *pad, const GstCaps *caps)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));

	return GST_PAD_LINK_OK;
}


/*
 * Source pad link function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadLinkFunction
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
		if(!filter->buf) {
			filter->buf = gst_buffer_new_and_alloc(4 * WIDTH * HEIGHT);
			g_return_if_fail(filter->buf != NULL);
			GST_BUFFER_SIZE(filter->buf) = 0;
		}

		draw(filter->buf, GST_BUFFER_DATA(buf) + (GST_BUFFER_SIZE(buf) - n), &n);

		if(GST_BUFFER_SIZE(filter->buf) >= GST_BUFFER_MAXSIZE(filter->buf)) {
			gst_pad_push(filter->srcpad, GST_DATA(filter->buf));
			/* FIXME: do I have to unref it? */
			filter->buf = NULL;
		}
	}
	gst_buffer_unref(in);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

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

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));
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

	filter->sinkpad = gst_pad_new_from_template(gst_element_class_get_pad_template(class, "sink"), "sink");
	gst_pad_set_link_function(filter->sinkpad, sink_link);
	gst_pad_set_chain_function(filter->sinkpad, chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	filter->srcpad = gst_pad_new_from_template(gst_element_class_get_pad_template(class, "src"), "src");
	gst_pad_set_link_function(filter->srcpad, src_link);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

	filter->buf = NULL;
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
