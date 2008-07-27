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
#include <bkr_rll.h>
#include <bkr_splp.h>
#include <bkr_video_out.h>
#include <rs.h>


/*
 * ============================================================================
 *
 *                                Custom Types
 *
 * ============================================================================
 */


/*
 * videomode enum
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


/*
 * bitdensity enum
 */


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


/*
 * sectorformat enum
 */


GType bkr_sectorformat_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static GEnumValue values[] = {
			{ BKR_SP, "SPLP", "SP/LP sector format" },
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
 *                                    Data
 *
 * ============================================================================
 */


GstCaps *bkr_get_template_caps(void)
{
	static GstCaps *caps = NULL;

	if(!caps) {
		/* FIXME:  figure out how to write the caps as enums */
		caps = gst_caps_from_string(
			"application/x-backer, " \
			"videomode=(int){ 1, 2 }, " \
			"bitdensity=(int){ 4, 8 }, " \
			"sectorformat=(int){ 16, 32 }"
		);
	} else
		gst_caps_ref(caps);

	return caps;
}


/*
 * ============================================================================
 *
 *                                 Utilities
 *
 * ============================================================================
 */


/*
 * compute the number of video fields per second.  used to translate marker
 * lengths into sector counts.
 */


double bkr_fields_per_second(enum bkr_videomode videomode)
{
	switch(videomode) {
	default:
	case BKR_NTSC:
		return 60.0 / 1.001;
	case BKR_PAL:
		return 50.0;
	}
}


/*
 * parse the contents of a GstCaps containing an application/x-backer
 * format specification.  The videomode, bitdensity, and sectorformat are
 * written into the given locations.  The return value is TRUE if the caps
 * could be parsed, or FALSE if not, in which case the output variables are
 * set to 0.
 */


int bkr_parse_caps(GstCaps *caps, enum bkr_videomode *videomode, enum bkr_bitdensity *bitdensity, enum bkr_sectorformat *sectorformat)
{
	const GstStructure *s;

	/* incase something goes wrong */
	*videomode = 0;
	*bitdensity = 0;
	*sectorformat = 0;

	/* retrieve the structure */
	s = gst_caps_get_structure(caps, 0);
	if(!s) {
		GST_DEBUG("failed to retrieve structure from caps");
		return FALSE;
	}

	/* FIXME:  when I figure out how to make the caps enums, put these
	 * back */
	/*if(!gst_structure_get_enum(s, "videomode", BKR_TYPE_VIDEOMODE, (int *) &videomode)) {*/
	if(!gst_structure_get_int(s, "videomode", (int *) videomode)) {
		GST_DEBUG("could not retrieve videomode from caps");
		return FALSE;
	}

	/*if(!gst_structure_get_enum(s, "bitdensity", BKR_TYPE_BITDENSITY, (int *) &bitdensity)) {*/
	if(!gst_structure_get_int(s, "bitdensity", (int *) bitdensity)) {
		GST_DEBUG("could not retrieve bitdensity from caps");
		return FALSE;
	}

	/*if(!gst_structure_get_enum(s, "sectorformat", BKR_TYPE_SECTORFORMAT, (int *) &sectorformat)) {*/
	if(!gst_structure_get_int(s, "sectorformat", (int *) sectorformat)) {
		GST_DEBUG("could not retrieve sectorformat from caps");
		return FALSE;
	}

	return TRUE;
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
	struct {
		const gchar *name;
		GType (*type)(void);
	} *element, elements[] = {
		{"bkr_rllenc", bkr_rllenc_get_type},
		{"bkr_rlldec", bkr_rlldec_get_type},
		{"bkr_splpenc", bkr_splpenc_get_type},
		{"bkr_splpdec", bkr_splpdec_get_type},
		{"bkr_frameenc", bkr_frameenc_get_type},
		{"bkr_framedec", bkr_framedec_get_type},
		{"bkr_video_out", bkr_video_out_get_type},
		{NULL, 0},
	};

	/* initialize Reed-Solomon coder/decoder */
	galois_field_init(GF00256);

	/* make sure the enums are created before a caps string has to be
	 * parsed */
	/* FIXME:  is this needed? */
	BKR_TYPE_VIDEOMODE;
	BKR_TYPE_BITDENSITY;
	BKR_TYPE_SECTORFORMAT;

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
