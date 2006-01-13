/*
 */

#include <gst/gst.h>

#include <bkr_frame.h>
#include <bkr_video_out.h>


/*
 * Entry point.
 */

static gboolean plugin_init(GstPlugin *plugin)
{
	struct element_info {
		gchar *name;
		GType (*type)(void);
	} *element, elements[] = {
		{"bkr_frame", bkr_frame_get_type},
		{"bkr_video_out", bkr_video_out_get_type},
		{NULL, 0},
	};

	for(element = elements; element->name; element++)
		if(!gst_element_register(plugin, element->name, GST_RANK_NONE, element->type()))
			return FALSE;
	return TRUE;
}


/*
 * This is the structure that gst-register looks for.
 */

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "bkr_elements", "Backer Bytestream Processing Elements", plugin_init, PACKAGE_VERSION, "GPL", PACKAGE_NAME, "http://linbacker.sf.net")
