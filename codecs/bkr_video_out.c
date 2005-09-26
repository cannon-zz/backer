/*
 */

#include <gst/gst.h>
#include "bkr_video_out.h"


enum {
	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_SILENT
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"ANY"
	)
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
 * This function handles the link with other plug-ins
 */

static GstPadLinkReturn bkr_video_out_link(GstPad *pad, const GstCaps *caps)
{
	BkrVideoOut *filter;
	GstPad *otherpad;

	filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));
	g_return_val_if_fail(filter != NULL, GST_PAD_LINK_REFUSED);
	g_return_val_if_fail(GST_IS_BKR_VIDEO_OUT(filter), GST_PAD_LINK_REFUSED);
	otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);

	/* set caps on next or previous element's pad, and see what they
	 * think. In real cases, we would (after this step) extract
	 * properties from the caps such as video size or audio samplerat. */
	return gst_pad_try_set_caps(otherpad, caps);
}


/*
 * chain function
 * this function does the actual processing
 */

static void bkr_video_out_chain(GstPad *pad, GstData *in)
{
	BkrVideoOut *filter;
	GstBuffer *buf = GST_BUFFER(in);

	g_return_if_fail(GST_IS_PAD(pad));
	g_return_if_fail(buf != NULL);

	filter = BKR_VIDEO_OUT(GST_OBJECT_PARENT(pad));
	g_return_if_fail(GST_IS_BKR_VIDEO_OUT(filter));

	if (filter->silent == FALSE)
		g_print("I'm plugged, therefore I'm in.\n");

	/* just push out the incoming buffer without touching it */
	gst_pad_push(filter->srcpad, GST_DATA(buf));
}


/*
 * initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */

static void bkr_video_out_init(BkrVideoOut *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	filter->sinkpad = gst_pad_new_from_template(gst_element_class_get_pad_template(class, "sink"), "sink");
	gst_pad_set_link_function(filter->sinkpad, bkr_video_out_link);
	gst_pad_set_getcaps_function(filter->sinkpad, gst_pad_proxy_getcaps);

	filter->srcpad = gst_pad_new_from_template(gst_element_class_get_pad_template(class, "src"), "src");
	gst_pad_set_link_function(filter->srcpad, bkr_video_out_link);
	gst_pad_set_getcaps_function(filter->srcpad, gst_pad_proxy_getcaps);

	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);
	gst_pad_set_chain_function(filter->sinkpad, bkr_video_out_chain);
	filter->silent = FALSE;
}


static void bkr_video_out_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter;

	g_return_if_fail(GST_IS_BKR_VIDEO_OUT(object));
	filter = BKR_VIDEO_OUT(object);

	switch (prop_id) {
	case ARG_SILENT:
		filter->silent = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}


static void bkr_video_out_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *filter;

	g_return_if_fail(GST_IS_BKR_VIDEO_OUT(object));
	filter = BKR_VIDEO_OUT(object);

	switch (prop_id) {
	case ARG_SILENT:
		g_value_set_boolean(value, filter->silent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}


/*
 * Initialize the plugin's class
 */

static void bkr_video_out_class_init(BkrVideoOutClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

	g_object_class_install_property(gobject_class, ARG_SILENT, g_param_spec_boolean("silent", "Silent", "Produce verbose output ?", FALSE, G_PARAM_READWRITE));

	gobject_class->set_property = bkr_video_out_set_property;
	gobject_class->get_property = bkr_video_out_get_property;
}


static void bkr_video_out_base_init(BkrVideoOutClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer Video Out",
		"Codec/Decoder/Video/bkr_video_out",
		"Simulates a Backer's byte-stream to video conversion",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */

GType bkr_video_out_get_type(void)
{
	static GType plugin_type = 0;

	if (!plugin_type) {
		static const GTypeInfo plugin_info = {
			.class_size = sizeof(BkrVideoOutClass),
			.base_init = (GBaseInitFunc) bkr_video_out_base_init,
			.class_init = (GClassInitFunc) bkr_video_out_class_init,
			.instance_size = sizeof(BkrVideoOut),
			.instance_init = (GInstanceInitFunc) bkr_video_out_init,
		};
		plugin_type = g_type_register_static(GST_TYPE_ELEMENT, "BkrVideoOut", &plugin_info, 0);
	}
	return plugin_type;
}


static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "plugin", GST_RANK_NONE, BKR_VIDEO_OUT_TYPE);
}


/*
 * this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered
 */

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "plugin", "Template plugin", plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
