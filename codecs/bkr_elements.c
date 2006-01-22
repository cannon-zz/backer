/*
 */

#include <gst/gst.h>

#include <bkr_frame.h>
#include <bkr_splp.h>
#include <bkr_video_out.h>
#include <rs.h>


/*
 * Entry point.
 */

static gboolean plugin_init(GstPlugin *plugin)
{
	struct element_info {
		gchar *name;
		GType (*type)(void);
	} *element, elements[] = {
		{"bkr_framedec", bkr_framedec_get_type},
		{"bkr_frameenc", bkr_frameenc_get_type},
		{"bkr_splpdec", bkr_splpdec_get_type},
		{"bkr_splpenc", bkr_splpenc_get_type},
		{"bkr_video_out", bkr_video_out_get_type},
		{NULL, 0},
	};

	/* initialize Reed-Solomon coder/decoder */
	galois_field_init(GF00256);

	/* tell gstreamer about the elements */
	for(element = elements; element->name; element++)
		if(!gst_element_register(plugin, element->name, GST_RANK_NONE, element->type()))
			return FALSE;
	return TRUE;
}


/*
 * This is the structure that gst-register looks for.
 */

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "bkr_elements", "Backer Bytestream Processing Elements", plugin_init, PACKAGE_VERSION, "GPL", PACKAGE_NAME, "http://linbacker.sf.net")
