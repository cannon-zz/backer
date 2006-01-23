/*
 */

#include <gst/gst.h>
#include <gst/bytestream/adapter.h>
#include <backer.h>
#include <bkr_elements.h>
#include <bkr_video_out.h>

#define VIDEO_BPP 32


/*
 * ============================================================================
 *
 *                                 Codec Code
 *
 * ============================================================================
 */

/*
 * How to draw data.
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


static guint32 *draw_byte(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, guint8 byte, guint32 colour)
{
	gint i;

	for(i = 0x80; i; i >>= 1)
		pos = pixel_func(pos, byte & i ? colour : 0);

	return pos;
}


static guint32 *draw_line(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *pos, const guint8 *data, gint n, guint32 colour)
{
	pos = draw_byte(pixel_func, pos, 0x45, colour);
	while(n--)
		pos = draw_byte(pixel_func, pos, *data++, colour);

	return pos;
}


static void draw_field(guint32 *(*pixel_func)(guint32 *, guint32), guint32 *dest, gint bytes_per_line, gint lines, const guint8 *data)
{
	while(lines--) {
		dest = draw_line(pixel_func, dest, data, bytes_per_line, 0x00ffffff);
		data += bytes_per_line;
	}
}


/*
 * Format information.
 * 	width = pixels/bit * bits/byte * bytes/line
 */

static struct bkr_video_out_format format(enum bkr_videomode v, enum bkr_bitdensity d)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			return (struct bkr_video_out_format) {4, 1, 8 * 8 * 5, 253, draw_bit_l};
		case BKR_PAL:
			return (struct bkr_video_out_format) {4, 0, 8 * 8 * 5, 305, draw_bit_l};
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			return (struct bkr_video_out_format) {10, 1, 4 * 8 * 11, 253, draw_bit_h};
		case BKR_PAL:
			return (struct bkr_video_out_format) {10, 0, 4 * 8 * 11, 305, draw_bit_h};
		}
	}

	return (struct bkr_video_out_format) {0,};
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
	ARG_VIDEOMODE = 1,
	ARG_BITDENSITY
};


static void set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;
	}

	filter->format = format(filter->videomode, filter->bitdensity);
}


static void get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;
	}
}


/*
 * Source pad getcaps function.
 */

static GstCaps *src_getcaps(GstPad *pad)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));
	GstCaps *caps = gst_caps_new_simple(
		"video/x-raw-rgb",
		"width", G_TYPE_INT, filter->format.width,
		"height", G_TYPE_INT, filter->format.height + filter->format.interlace,
		"bpp", G_TYPE_INT, VIDEO_BPP,
		"depth", G_TYPE_INT, 24,
		"framerate", G_TYPE_DOUBLE, (double) 60.0,
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

static void chain(GstPad *pad, GstData *data)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(GST_OBJECT_PARENT(pad));
	const guint8 *inbuf;
	GstBuffer *outbuf;
	gint lines;

	/* check that element properties are set */
	g_return_if_fail(filter->format.pixel_func != NULL);

	/* add input data to adapter */
	gst_adapter_push(filter->adapter, GST_BUFFER(data));

	/* try to extract one video field's worth */
	while(1) {
		lines = filter->format.height + (filter->odd_field ? filter->format.interlace : 0);
		inbuf = gst_adapter_peek(filter->adapter, lines * filter->format.bytes_per_line);
		if(!inbuf)
			return;

		/* draw video image */
		outbuf = gst_pad_alloc_buffer(filter->srcpad, 0, (VIDEO_BPP/8) * filter->format.width * (filter->format.height + filter->format.interlace));
		g_return_if_fail(outbuf != NULL);

		draw_field(filter->format.pixel_func, (guint32 *) GST_BUFFER_DATA(outbuf), filter->format.bytes_per_line, lines, inbuf);

		/* send picture */
		gst_adapter_flush(filter->adapter, lines * filter->format.bytes_per_line);
		gst_pad_push(filter->srcpad, GST_DATA(outbuf));
		filter->odd_field ^= 1;
	}
}


/*
 * Parent class.
 */

static GstElementClass *parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */

static void finalize(GObject *object)
{
	g_object_unref(BKR_VIDEO_OUT(object)->adapter);

	G_OBJECT_CLASS(parent_class)->finalize(object);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static void class_init(BkrVideoOutClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("density", "Density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	object_class->set_property = set_property;
	object_class->get_property = get_property;

	object_class->finalize = finalize;

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
		"Codec/Decoder/Video",
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
	filter->adapter = gst_adapter_new();
	filter->odd_field = 1;
	filter->format = format(filter->videomode, filter->bitdensity);
}


/*
 * bkr_video_out_get_type()
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
