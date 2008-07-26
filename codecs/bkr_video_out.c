/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                            Video Modem Emulator
 *
 * Copyright (C) 2008  Kipp C. Cannon
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
#include <gst/base/gstadapter.h>
#include <backer.h>
#include <bkr_elements.h>
#include <bkr_video_out.h>


#define VIDEO_BPP 32	/* cannot be changed without modifying draw_field */


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
	/* 4 pixels for a high density bit */
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	*pos++ = colour;
	return pos;
}


static guint32 *draw_bit_l(guint32 *pos, guint32 colour)
{
	/* 8 pixels for a low density bit */
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
	for(; lines--; data += bytes_per_line)
		dest = draw_line(pixel_func, dest, data, bytes_per_line, 0x00ffffff);
}


/*
 * Format information.
 */


static struct bkr_video_out_format compute_format(enum bkr_videomode v, enum bkr_bitdensity d)
{
	/* initialize to 0 to silence compiler warning about possibly
	 * uninitialized data */
	struct bkr_video_out_format format = {0,};

	switch(d) {
	case BKR_LOW:
		format.bytes_per_line = 4;
		format.width = 8;	/* pixels / bit */
		format.pixel_func = draw_bit_l;
		break;

	case BKR_HIGH:
		format.bytes_per_line = 10;
		format.width = 4;	/* pixels / bit */
		format.pixel_func = draw_bit_h;
		break;

	default:
		/* set to impossible values */
		format.bytes_per_line = 0;
		format.width = 0;
		format.pixel_func = NULL;
		break;
	}

	/* *= bits / byte * bytes / line = pixels / line */
	format.width *= 8 * (format.bytes_per_line + 1);

	switch(v) {
	case BKR_NTSC:
		format.height = 253;
		format.interlace = 1;
		break;

	case BKR_PAL:
		format.height = 305;
		format.interlace = 0;
		break;

	default:
		/* set to impossible values */
		format.height = 0;
		format.interlace = 0;
		break;
	}

	return format;
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
	enum bkr_videomode videomode = filter->videomode;
	enum bkr_bitdensity bitdensity = filter->bitdensity;
	struct bkr_video_out_format format;
	GstCaps *caps;

	switch(id) {
	case ARG_VIDEOMODE:
		videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		bitdensity = g_value_get_enum(value);
		break;
	}

	format = compute_format(videomode, bitdensity);

	caps = gst_caps_new_simple(
		"video/x-raw-rgb",
		"width", G_TYPE_INT, format.width,
		"height", G_TYPE_INT, format.height + format.interlace,
		"bpp", G_TYPE_INT, VIDEO_BPP,
		"depth", G_TYPE_INT, 24,
		"framerate", GST_TYPE_FRACTION, videomode == BKR_NTSC ? 60000 : 50000, videomode == BKR_NTSC ? 1001 : 1000,
		/*"endianness", G_TYPE_INT, 4321,
		"red_mask", G_TYPE_INT, 0x0000ff00,
		"green_mask", G_TYPE_INT, 0x00ff0000,
		"blue_mask", G_TYPE_INT, 0xff000000,*/
		NULL
	);

	if(gst_pad_set_caps(filter->srcpad, caps)) {
		/* caps were accepted, update element properties */
		filter->videomode = videomode;
		filter->bitdensity = bitdensity;
		filter->format = format;
	}
	gst_caps_unref(caps);
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
 * Pad chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(gst_pad_get_parent(pad));
	GstPad *srcpad = filter->srcpad;
	const guint8 *data;
	GstBuffer *srcbuf;
	GstFlowReturn result;
	gint lines;
	gint bytes_per_image = (VIDEO_BPP/8) * filter->format.width * (filter->format.height + filter->format.interlace);

	if(!GST_PAD_CAPS(srcpad)) {
		GST_DEBUG("caps not set on src pad");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	gst_adapter_push(filter->adapter, sinkbuf);

	/* loop until the adapter runs out of data */
	while(1) {
		lines = filter->format.height + (filter->field_number & 1 ? filter->format.interlace : 0);
		data = gst_adapter_peek(filter->adapter, lines * filter->format.bytes_per_line);
		if(!data) {
			GST_DEBUG("not enough data yet, need %d lines (%d bytes)", lines, lines * filter->format.bytes_per_line);
			result = GST_FLOW_OK;
			break;
		}

		/* ask the downstream peer for a buffer.  use the video
		 * field number as the media-specific offset */
		result = gst_pad_alloc_buffer(srcpad, filter->field_number, bytes_per_image, GST_PAD_CAPS(srcpad), &srcbuf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_alloc_buffer() failed");
			break;
		}

		/* set the time stamp from the field number.  field number
		 * is origin 1, subtract 1 so that time is origin 0 */
#if 0
		GST_BUFFER_TIMESTAMP(srcbuf) = GST_SECOND * (GST_BUFFER_OFFSET(srcbuf) - 1) / bkr_fields_per_second(filter->videomode);
		GST_BUFFER_DURATION(srcbuf) = GST_SECOND / bkr_fields_per_second(filter->videomode);
#endif

		/* draw the video field */
		draw_field(filter->format.pixel_func, (guint32 *) GST_BUFFER_DATA(srcbuf), filter->format.bytes_per_line, lines, data);

		/* send image */
		result = gst_pad_push(srcpad, srcbuf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_push() failed");
			break;
		}

		/* flush the data we've just used from adapter */
		gst_adapter_flush(filter->adapter, lines * filter->format.bytes_per_line);

		/* reset for next */
		filter->field_number++;
	}

done:
	gst_object_unref(filter);
	return result;
}


/*
 * Parent class.
 */


static GstElementClass *parent_class = NULL;


/*
 * Instance finalize function.  See
 *
 * file:///usr/share/doc/libglib2.0-doc/gobject/gobject-The-Base-Object-Type.html
 *
 * FIXME:  should the unref()ing be done in a dispose() function?  The
 * gobject documentation says so.
 */


static void finalize(GObject *object)
{
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);

	/* FIXME: need to call gst_adapter_clear() on FLUSH_STOP, EOS, and
	 * GST_STATE_PAUSED --> GST_STATE_READY. */

	g_object_unref(filter->adapter);
	filter->adapter = NULL;
	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;

	G_OBJECT_CLASS(parent_class)->finalize(object);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 *
 * NOTE:  the srcpad template must be kept synchronized with the caps
 * computed in set_property().
 */


static void base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer Video Out",
		"Codec/Decoder/Video",
		"Simulates a Backer's byte-stream to video conversion",
		"Kipp Cannon <kcannon@ligo.caltech.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_CAPS_ANY
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		gst_caps_from_string(
			"video/x-raw-rgb, " \
			"width = (int) { 320, 352 }, " /* format.width */ \
			"height = (int) 254, " /* format.height + format.interlace */ \
			"bpp = (int) 32, " /* VIDEO_BPP */ \
			"depth = (int) 24, " \
			"framerate = (fraction) 60000/1001; " \
			"video/x-raw-rgb, " \
			"width = (int) { 320, 352 }, " /* format.width */ \
			"height = (int) 305, " /* format.height + format.interlace */ \
			"bpp = (int) 32, " /* VIDEO_BPP */ \
			"depth = (int) 24, " \
			"framerate = (fraction) 50000/1000"
		)
	);

	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	gst_element_class_set_details(element_class, &plugin_details);
	gst_element_class_add_pad_template(element_class, sinkpad_template);
	gst_element_class_add_pad_template(element_class, srcpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void class_init(gpointer class, gpointer class_data)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("bitdensity", "Bit density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */


static void instance_init(GTypeInstance *object, gpointer class)
{
	GstElement *element = GST_ELEMENT(object);
	BkrVideoOut *filter = BKR_VIDEO_OUT(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_chain_function(pad, chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->adapter = gst_adapter_new();
	filter->field_number = 1;
	filter->format = compute_format(filter->videomode, filter->bitdensity);
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
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(BkrVideoOut),
			.instance_init = instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrVideoOut", &info, 0);
	}
	return type;
}
