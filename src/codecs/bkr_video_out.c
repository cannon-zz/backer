/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                            Video Modem Emulator
 *
 * Copyright (C) 2008,2009,2010  Kipp C. Cannon
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
#include <gst/base/gstbasetransform.h>
#include <backer.h>
#include <bkr_elements.h>
#include <bkr_video_out.h>


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


static guint32 *draw_byte_h(guint32 *pos, guint8 byte)
{
	guint8 i;

	for(i = 0x80; i; i >>= 1)
		/* 4 pixels for a high density bit (draw all four by
		 * writing a single 32-bit number to memory) */
		*pos++ = byte & i ? 0xffffffff : 0;

	return pos;
}


static guint32 *draw_byte_l(guint32 *pos, guint8 byte)
{
	guint8 i;
	guint64 *posl = (guint64 *) pos;

	for(i = 0x80; i; i >>= 1)
		/* 8 pixels for a low density bit (draw all eight by
		 * writing 64-bit numbers to memory) */
		*posl++ = byte & i ? 0xffffffffffffffff : 0;

	return (guint32 *) posl;
}


static guint32 *draw_line(guint32 *(*byte_func)(guint32 *, guint8), guint32 *pos, const guint8 *data, int n)
{
	pos = byte_func(pos, 0x45);
	while(n--)
		pos = byte_func(pos, *data++);

	return pos;
}


static void draw_field(guint32 *(*byte_func)(guint32 *, guint8), guint8 *dest, gint bytes_per_line, gint lines, const guint8 *data)
{
	guint32 *pos = (guint32 *) dest;

	for(; lines--; data += bytes_per_line)
		pos = draw_line(byte_func, pos, data, bytes_per_line);
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
		format.byte_func = draw_byte_l;
		break;

	case BKR_HIGH:
		format.bytes_per_line = 10;
		format.width = 4;	/* pixels / bit */
		format.byte_func = draw_byte_h;
		break;

	default:
		g_assert_not_reached();
		break;
	}

	/* *= bits / byte * bytes / line --> pixels / line */
	format.width *= 8 * (format.bytes_per_line + 1);

	switch(v) {
	case BKR_NTSC:
		format.height = 253;
		format.interlace = 1;
		gst_util_fraction_multiply(GST_SECOND, 1, 1001, 60000, &format.gst_seconds_per_field_a, &format.gst_seconds_per_field_b);
		break;

	case BKR_PAL:
		format.height = 305;
		format.interlace = 0;
		gst_util_fraction_multiply(GST_SECOND, 1, 1, 50, &format.gst_seconds_per_field_a, &format.gst_seconds_per_field_b);
		break;

	default:
		g_assert_not_reached();
		break;
	}

	return format;
}


static guint lines_in_next_field(const BkrVideoOut *element)
{
	return element->format.height + (element->field_number & 1 ? element->format.interlace : 0);
}


static guint lines_in_two_fields(const BkrVideoOut *element)
{
	return 2 * element->format.height + element->format.interlace;
}


static guint bytes_in_image(const BkrVideoOut *element)
{
	return element->format.width * (element->format.height + element->format.interlace);
}


/*
 * ============================================================================
 *
 *                           GStreamer Boiler Plate
 *
 * ============================================================================
 */


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
	GST_BASE_TRANSFORM_SINK_NAME,
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
);


/*
 * NOTE:  the srcpad template must be kept synchronized with the caps
 * computed in transform_caps().
 */


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
	GST_BASE_TRANSFORM_SRC_NAME,
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw-gray, " \
		"width = (int) {320, 352}, " /* format.width */ \
		"height = (int) 254, " /* format.height + format.interlace */ \
		"bpp = (int) 8, " \
		"depth = (int) 8, " \
		"framerate = (fraction) 60000/1001; " \
		"video/x-raw-gray, " \
		"width = (int) {320, 352}, " /* format.width */ \
		"height = (int) 305, " /* format.height + format.interlace */ \
		"bpp = (int) 8, " \
		"depth = (int) 8, " \
		"framerate = (fraction) 50/1"
	)
);


GST_BOILERPLATE(
	BkrVideoOut,
	bkr_video_out,
	GstBaseTransform,
	GST_TYPE_BASE_TRANSFORM
);


enum property {
	ARG_VIDEOMODE = 1,
	ARG_BITDENSITY
};


/*
 * ============================================================================
 *
 *                     GstBaseTransform Method Overrides
 *
 * ============================================================================
 */


/*
 * transform_caps()
 */


static GstCaps *transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(trans);
	GstCaps *othercaps = NULL;

	switch(direction) {
	case GST_PAD_SINK:
		/*
		 * NOTE:  the srcpad template must be kept synchronized
		 * with the caps computed here.
		 */

		othercaps = gst_caps_new_simple(
			"video/x-raw-gray",
			"width", G_TYPE_INT, element->format.width,
			"height", G_TYPE_INT, element->format.height + element->format.interlace,
			"bpp", G_TYPE_INT, (gint) 8,
			"depth", G_TYPE_INT, (gint) 8,
			"framerate", GST_TYPE_FRACTION, element->videomode == BKR_NTSC ? 60000 : 50, element->videomode == BKR_NTSC ? 1001 : 1,
			NULL
		);
		break;

	case GST_PAD_SRC:
		othercaps = gst_static_pad_template_get_caps(&sink_factory);
		break;

	default:
		g_assert_not_reached();
		break;
	}

	return othercaps;
}


/*
 * transform_size()
 */


static gboolean transform_size(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, guint size, GstCaps *othercaps, guint *othersize)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(trans);
	guint available = gst_adapter_available(element->adapter);
	gboolean success = TRUE;

	switch(direction) {
	case GST_PAD_SINK:
		/*
		 * if the data in the buffer combined with the input size
		 * bytes adds up to at least as much as is required for a
		 * video frame then the output size is one video frame.
		 * otherwise it's zero.
		 */

		if(available + size >= lines_in_next_field(element) * element->format.bytes_per_line)
			*othersize = bytes_in_image(element);
		else
			*othersize = 0;
		break;

	case GST_PAD_SRC:
		/*
		 * if the data in the buffer is less than the amount
		 * required for a video frame then the input size is the
		 * additional number of bytes needed to create one frame of
		 * video.  otherwise the input size is 0.
		 */

		if(available < lines_in_next_field(element) * element->format.bytes_per_line)
			*othersize = lines_in_next_field(element) * element->format.bytes_per_line - available;
		else
			*othersize = 0;
		break;

	default:
		g_assert_not_reached();
		break;
	}

	return success;
}


/*
 * get_unit_size()
 */


static gboolean get_unit_size(GstBaseTransform *trans, GstCaps *caps, guint *size)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(trans);
	gboolean success = TRUE;

	if(gst_caps_is_equal(caps, GST_PAD_CAPS(GST_BASE_TRANSFORM_SRC_PAD(trans))))
		*size = bytes_in_image(element);
	else
		*size = 1;

	return success;
}


/*
 * event()
 */


gboolean event(GstBaseTransform *trans, GstEvent *event)
{
	gboolean forward_event = TRUE;

	/* FIXME: need to call gst_adapter_clear() on FLUSH_START, EOS, and
	 * GST_STATE_PAUSED --> GST_STATE_READY. */

	return forward_event;
}


/*
 * transform()
 */


static GstFlowReturn transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(trans);
	GstFlowReturn result = GST_FLOW_OK;
	guint bytes_per_image = bytes_in_image(element);
	guint lines;

	/*
	 * push data into adapter.  don't let adapter unref it
	 */

	gst_buffer_ref(inbuf);
	gst_adapter_push(element->adapter, inbuf);

	/*
	 * if we can produce more than 1 video field of output, generate
	 * the "bonus" fields and push downstream
	 */

	while(gst_adapter_available(element->adapter) >= lines_in_two_fields(element) * element->format.bytes_per_line) {
		GstBuffer *buf;

		result = gst_pad_alloc_buffer(GST_BASE_TRANSFORM_SRC_PAD(trans), element->field_number, bytes_per_image, GST_PAD_CAPS(GST_BASE_TRANSFORM_SRC_PAD(trans)), &buf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_alloc_buffer() failed");
			goto done;
		}

		/*
		 * draw the video field
		 */

		lines = lines_in_next_field(element);
		draw_field(element->format.byte_func, GST_BUFFER_DATA(buf), element->format.bytes_per_line, lines, gst_adapter_peek(element->adapter, lines * element->format.bytes_per_line));

		/*
		 * set the time stamp from the field number.  field number
		 * is origin 1, subtract 1 so that time is origin 0
		 */

		GST_BUFFER_TIMESTAMP(buf) = gst_util_uint64_scale_int_round(GST_BUFFER_OFFSET(buf) - 1, element->format.gst_seconds_per_field_a, element->format.gst_seconds_per_field_b);
		GST_BUFFER_DURATION(buf) = gst_util_uint64_scale_int_round(GST_BUFFER_OFFSET(buf), element->format.gst_seconds_per_field_a, element->format.gst_seconds_per_field_b) - GST_BUFFER_TIMESTAMP(buf);

		/*
		 * send image
		 */

		result = gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(trans), buf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_push() failed");
			goto done;
		}

		/*
		 * flush the data we've just used from adapter and
		 * increment field count
		 */

		gst_adapter_flush(element->adapter, lines * element->format.bytes_per_line);
		element->field_number++;
	}

	/*
	 * enough data to generate 1 video field?
	 */

	lines = lines_in_next_field(element);
	if(gst_adapter_available(element->adapter) < lines * element->format.bytes_per_line) {
		result = GST_BASE_TRANSFORM_FLOW_DROPPED;
		goto done;
	}

	/*
	 * generate video field
	 */

	draw_field(element->format.byte_func, GST_BUFFER_DATA(outbuf), element->format.bytes_per_line, lines, gst_adapter_peek(element->adapter, lines * element->format.bytes_per_line));

	GST_BUFFER_OFFSET(outbuf) = element->field_number;

	/*
	 * set the time stamp from the field number.  field number is
	 * origin 1, subtract 1 so that time is origin 0
	 */

	GST_BUFFER_TIMESTAMP(outbuf) = gst_util_uint64_scale_int_round(GST_BUFFER_OFFSET(outbuf) - 1, element->format.gst_seconds_per_field_a, element->format.gst_seconds_per_field_b);
	GST_BUFFER_DURATION(outbuf) = gst_util_uint64_scale_int_round(GST_BUFFER_OFFSET(outbuf), element->format.gst_seconds_per_field_a, element->format.gst_seconds_per_field_b) - GST_BUFFER_TIMESTAMP(outbuf);

	/*
	 * flush the data we've just used from adapter, and increment field
	 * count
	 */

	gst_adapter_flush(element->adapter, lines * element->format.bytes_per_line);
	element->field_number++;

done:
	return result;
}


/*
 * ============================================================================
 *
 *                          GObject Method Overrides
 *
 * ============================================================================
 */


/*
 * set_property()
 */


static void set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(object);
	enum bkr_videomode videomode = element->videomode;
	enum bkr_bitdensity bitdensity = element->bitdensity;
	gboolean reconfigure = FALSE;

	GST_OBJECT_LOCK(object);

	switch(id) {
	case ARG_VIDEOMODE:
		videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		bitdensity = g_value_get_enum(value);
		break;

	default:
		g_assert_not_reached();
		break;
	}

	if(videomode != element->videomode || bitdensity != element->bitdensity) {
		element->videomode = videomode;
		element->bitdensity = bitdensity;
		element->format = compute_format(videomode, bitdensity);
		reconfigure = TRUE;
	}

	GST_OBJECT_UNLOCK(object);

	/*
	 * force caps negotiation.  can't call function with object lock
	 * held
	 */

	if(reconfigure)
		gst_base_transform_reconfigure(GST_BASE_TRANSFORM(object));
}


/*
 * get_property()
 */


static void get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(object);

	GST_OBJECT_LOCK(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, element->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, element->bitdensity);
		break;

	default:
		g_assert_not_reached();
		break;
	}

	GST_OBJECT_UNLOCK(object);
}


/*
 * finalize()
 */


static void finalize(GObject *object)
{
	BkrVideoOut *element = BKR_VIDEO_OUT(object);

	if(element->adapter) {
		g_object_unref(element->adapter);
		element->adapter = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(object);
}


/*
 * base_init()
 */


static void bkr_video_out_base_init(gpointer klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
	GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

	gst_element_class_set_details_simple(
		element_class,
		"Backer Video Out",
		"Codec/Decoder/Video",
		"Simulates a Backer's byte-stream to video conversion",
		"Kipp Cannon <kipp.cannon@ligo.org>"
	);

	object_class->set_property = GST_DEBUG_FUNCPTR(set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(get_property);
	object_class->finalize = GST_DEBUG_FUNCPTR(finalize);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));

	transform_class->transform_caps = GST_DEBUG_FUNCPTR(transform_caps);
	transform_class->transform_size = GST_DEBUG_FUNCPTR(transform_size);
	transform_class->get_unit_size = GST_DEBUG_FUNCPTR(get_unit_size);
	transform_class->event = GST_DEBUG_FUNCPTR(event);
	transform_class->transform = GST_DEBUG_FUNCPTR(transform);
}


/*
 * class_init()
 */


static void bkr_video_out_class_init(BkrVideoOutClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	g_object_class_install_property(
		object_class,
		ARG_VIDEOMODE,
		g_param_spec_enum(
			"videomode",
			"Video mode",
			"Simulated video mode",
			BKR_TYPE_VIDEOMODE,
			DEFAULT_VIDEOMODE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT
		)
	);
	g_object_class_install_property(
		object_class,
		ARG_BITDENSITY,
		g_param_spec_enum(
			"bitdensity",
			"Bit density",
			"Simulated bit density",
			BKR_TYPE_BITDENSITY,
			DEFAULT_BITDENSITY,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT
		)
	);
}


/*
 * init()
 */


static void bkr_video_out_init(BkrVideoOut *element, BkrVideoOutClass *klass)
{
	element->adapter = gst_adapter_new();
	element->field_number = 1;
	element->videomode = DEFAULT_VIDEOMODE;
	element->bitdensity = DEFAULT_BITDENSITY;
	element->format = compute_format(element->videomode, element->bitdensity);
}
