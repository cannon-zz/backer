/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                         GStreamer Element Support
 *
 * Copyright (C) 2000,2001,2002  Kipp C. Cannon
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


#include <gst/gst.h>


#include <backer.h>
#include <bkr_elements.h>
#include <bkr_frame.h>
#include <bkr_gcr.h>
#include <bkr_splp.h>
#include <bkr_video_out.h>
#include <rs.h>


/*
 * ============================================================================
 *
 *                                 Properties
 *
 * ============================================================================
 */


GType bkr_videomode_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static GEnumValue values[] = {
			{ BKR_NTSC, "NTSC", "NTSC video mode" },
			{ BKR_PAL, "PAL", "PAL video mode" },
			{ 0, NULL, NULL }
		};

		type = g_enum_register_static("BKR_VIDEOMODE", values);
	}

	return type;
}


GType bkr_bitdensity_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static GEnumValue values[] = {
			{ BKR_LOW, "LOW", "low bit density" },
			{ BKR_HIGH, "HIGH", "high bit density" },
			{ 0, NULL, NULL }
		};

		type = g_enum_register_static("BKR_DENSITY", values);
	}

	return type;
}


GType bkr_sectorformat_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static GEnumValue values[] = {
			{ BKR_SP, "SP/LP", "SP/LP sector format" },
			{ BKR_EP, "EP", "EP sector format" },
			{ 0, NULL, NULL }
		};

		type = g_enum_register_static("BKR_SECTORFORMAT", values);
	}

	return type;
}


/*
 * ============================================================================
 *
 *                             Plugin Entry Point
 *
 * ============================================================================
 */


static gboolean plugin_init(GstPlugin *plugin)
{
	struct element_info {
		const gchar *name;
		GType (*type)(void);
	} *element, elements[] = {
		/*{"bkr_framedec", bkr_framedec_get_type},
		{"bkr_frameenc", bkr_frameenc_get_type},
		{"bkr_gcrdec", bkr_gcrdec_get_type},
		{"bkr_gcrenc", bkr_gcrenc_get_type},
		{"bkr_splpdec", bkr_splpdec_get_type},
		{"bkr_splpenc", bkr_splpenc_get_type},*/
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
