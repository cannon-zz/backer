/*
 */

#include <gst/gst.h>
#include "bkr_video_out.h"


enum {
	LAST_SIGNAL
};

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
		"width = (int) 352, "
		"height = (int) 253"
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
 * Pad chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void chain(GstPad *pad, GstData *in)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(GST_OBJECT_PARENT(pad));
	GstBuffer *buf = GST_BUFFER(in);

	g_return_if_fail(buf != NULL);

	gst_pad_push(filter->srcpad, GST_DATA(buf));
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

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));
	gst_element_class_set_details(element_class, &plugin_details);
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
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
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
			.base_init = (GBaseInitFunc) base_init,
			.class_init = (GClassInitFunc) class_init,
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
