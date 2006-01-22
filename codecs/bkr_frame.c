/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                             Formating Layer
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

#include <string.h>

#include <gst/gst.h>
#include <gst/bytestream/adapter.h>
#include <backer.h>
#include <bkr_frame.h>

#define DEFAULT_VIDMODE BKR_NTSC
#define DEFAULT_DENSITY BKR_HIGH
#define DEFAULT_FORMAT  BKR_SP


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */

#define  BKR_LEADER             0xe2    /* leader is filled with this */
#define  BKR_TRAILER            0x33    /* trailer is filled with this */
#define  FRAME_THRESHOLD_A      21
#define  FRAME_THRESHOLD_B      64


/*
 * ========================================================================
 *
 *                              Global Data
 *
 * ========================================================================
 */

static const guint8 sector_key[] = {
	0xd4, 0x7c, 0xb1, 0x93, 0x66, 0x65, 0x6a, 0xb5,
	0x63, 0xe4, 0x56, 0x59, 0x6c, 0xbe, 0xc5, 0xca,
	0xf4, 0x9c, 0xa3, 0xac, 0x6d, 0xb3, 0xd2, 0x7e,
	0x74, 0xa6, 0xe1, 0xa9, 0x5c, 0x9a, 0x4b, 0x5d
};


/*
 * ========================================================================
 *
 *                              CODEC Functions
 *
 * ========================================================================
 */

/*
 * Counts the number of bytes in the frame that match the key sequence.
 */

static guint correlate(const guint8 *data, gint key_interval, gint key_length, const guint8 *key)
{
	guint count = 0;

	while(key_length--) {
		count += *data == *(key++);
		data += key_interval;
	}

	return(count);
}


/*
 * Uses correlate() to scan a buffer until a sector key sequence is found.
 * On success, the return value is a pointer to the first byte following
 * the sector leader; otherwise the return value is NULL.
 */

static const guint8 *find_field(struct bkr_frame_format fmt, GstAdapter *adapter, const guint8 *key)
{
	gint threshold = fmt.key_length * FRAME_THRESHOLD_A / FRAME_THRESHOLD_B;
	const guint8 *data;
	gint corr;

	while(1) {
		if(!(data = gst_adapter_peek(adapter, fmt.active_size)))
			return NULL;

		corr = correlate(data, fmt.key_interval, fmt.key_length, key);
		if(corr >= threshold)
			break;
#if 0
		if(corr > filter->best_nonkey)
			filter->best_nonkey = corr;
#endif
		gst_adapter_flush(adapter, 1);
	}

#if 0
	if(corr < filter->worst_key)
		filter->worst_key = corr;

	if(filter->last_field_offset >= 0) {
		corr = ring_offset_sub(source->ring, source->ring->tail, filter->last_field_offset);
		if(corr < filter->smallest_field)
			filter->smallest_field = corr;
		else if(corr > filter->largest_field)
			filter->largest_field = corr;
		if(corr*4 > stream->source->capacity*3)
			filter->frame_warnings++;
	}
	filter->last_field_offset = source->ring->tail;
#endif

	return(data);
}


/*
 * Strips the sector data from a video field in the source buffer and
 * places it in the destination buffer.
 */

static void decode_field(struct bkr_frame_format fmt, guint8 *dst, const guint8 *src)
{
	int i;

	for(i = 1; i < fmt.key_length; i++) {
		src++;
		memcpy(dst, src, fmt.key_interval - 1);
		dst += fmt.key_interval - 1;
		src += fmt.key_interval - 1;
	}
	src++;
	memcpy(dst, src, fmt.active_size % fmt.key_interval - 1);
}


/*
 * Moves one field of data from the source buffer to the destination
 * buffer, adding leader trailer and sector key bytes as required.
 */

static void encode_field(struct bkr_frame_format fmt, guint8 *dst, const guint8 *src, const guint8 *key, gint field_number)
{
	gint i;

	memset(dst, BKR_LEADER, fmt.leader);
	dst += fmt.leader;

	for(i = 1; i < fmt.key_length; i++) {
		*dst++ = *key++;
		memcpy(dst, src, fmt.key_interval - 1);
		dst += fmt.key_interval - 1;
		src += fmt.key_interval - 1;
	}
	*dst++ = *key++;
	memcpy(dst, src, fmt.active_size % fmt.key_interval - 1);
	dst += fmt.active_size % fmt.key_interval - 1;

	memset(dst, BKR_TRAILER, fmt.trailer + ((field_number & 1) ? fmt.interlace : 0));
}


/*
 * Format info.
 */

static struct bkr_frame_format format(enum bkr_vidmode v, enum bkr_density d, enum bkr_format f)
{
	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {1012, 4,  40, 32,  940,  44, 22};
			case BKR_SP:
				return (struct bkr_frame_format) {1012, 4,  32, 28,  952,  45, 22};
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {1220, 0,  48, 36, 1136,  40, 29};
			case BKR_SP:
				return (struct bkr_frame_format) {1220, 0,  40, 36, 1144,  49, 24};
			}
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {2530, 10, 100, 70, 2360,  84, 29};
			case BKR_SP:
				return (struct bkr_frame_format) {2530, 10,  80, 70, 2380, 125, 20};
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {3050, 0, 120, 90, 2840,  91, 32};
			case BKR_SP:
				return (struct bkr_frame_format) {3050, 0, 100, 90, 2860, 136, 22};
			}
		}
	}
}


/*
 * ============================================================================
 *
 *                       GStreamer Encoder Support Code
 *
 * ============================================================================
 */

/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void enc_chain(GstPad *pad, GstData *data)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(GST_OBJECT_PARENT(pad));
	GstBuffer *inbuf = GST_BUFFER(data);
	GstBuffer *outbuf = gst_pad_alloc_buffer(filter->srcpad, 0, filter->format.field_size + (filter->odd_field ? filter->format.interlace : 0));

	g_return_if_fail((inbuf != NULL) && (outbuf != NULL));

	if(GST_IS_EVENT(data)) {
		GstEvent *event = GST_EVENT(data);
		switch(GST_EVENT_TYPE(event)) {
		case GST_EVENT_EOS:
			/* FIXME: add an extra field if needed to finish the
			 * current frame */
			break;
#if 0
		case GST_EVENT_NEWSEGMENT:
			break;
#endif
		default:
			break;
		}
		gst_pad_event_default(pad, event);
		return;
	}

	if(GST_BUFFER_SIZE(inbuf) >= filter->format.active_size - filter->format.key_length)
		encode_field(filter->format, GST_BUFFER_DATA(outbuf), GST_BUFFER_DATA(inbuf), sector_key, filter->odd_field);
	gst_data_unref(data);

	gst_pad_push(filter->srcpad, GST_DATA(outbuf));
	filter->odd_field ^= 1;
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static GstElementClass *enc_parent_class = NULL;

static void enc_class_init(BkrFrameEncClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	enc_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void enc_base_init(BkrFrameEncClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer Frame Encoder",
		"Filter",
		"Backer frame synchronization encoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void enc_instance_init(BkrFrameEnc *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, enc_chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad.  No link function because pad sends a
	 * typeless byte stream */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

	/* internal state */
	filter->vidmode = DEFAULT_VIDMODE;
	filter->density = DEFAULT_DENSITY;
	filter->fmt = DEFAULT_FORMAT;
	filter->odd_field = 1;	/* first video field is odd */
	filter->format = format(filter->vidmode, filter->density, filter->fmt);
}


/*
 * bkr_frameenc_get_type().
 */

GType bkr_frameenc_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrFrameEncClass),
			.class_init = (GClassInitFunc) enc_class_init,
			.base_init = (GBaseInitFunc) enc_base_init,
			.instance_size = sizeof(BkrFrameEnc),
			.instance_init = (GInstanceInitFunc) enc_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrFrameEnc", &info, 0);
	}
	return type;
}


/*
 * ============================================================================
 *
 *                       GStreamer Decoder Support Code
 *
 * ============================================================================
 */

/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */

static void dec_chain(GstPad *pad, GstData *in)
{
	BkrFrameDec *filter = BKR_FRAMEDEC(GST_OBJECT_PARENT(pad));
	GstBuffer *outbuf;
	const guint8 *data;

	gst_adapter_push(filter->adapter, GST_BUFFER(in));

	while(data = find_field(filter->format, filter->adapter, sector_key)) {
		outbuf = gst_pad_alloc_buffer(filter->srcpad, 0, filter->format.active_size - filter->format.key_length);
		decode_field(filter->format, GST_BUFFER_DATA(outbuf), data);
		gst_pad_push(filter->srcpad, GST_DATA(outbuf));
		gst_adapter_flush(filter->adapter, filter->format.active_size);
	}
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */

static GstElementClass *dec_parent_class = NULL;

static void dec_class_init(BkrFrameDecClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	dec_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */

static void dec_base_init(BkrFrameDecClass *class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	static GstElementDetails plugin_details = {
		"Backer Frame Decoder",
		"Filter",
		"Backer frame synchronization decoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};

	gst_element_class_set_details(element_class, &plugin_details);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */

static void dec_instance_init(BkrFrameDec *filter)
{
	GstElementClass *class = GST_ELEMENT_GET_CLASS(filter);

	/* input, "sink", pad.  No link function because pad can accept
	 * anything as input */
	filter->sinkpad = gst_pad_new("sink", GST_PAD_SINK);
	gst_pad_set_chain_function(filter->sinkpad, dec_chain);
	gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

	/* output, "source", pad.  No link function because pad sends a
	 * typeless byte stream */
	filter->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

	/* internal state */
	filter->vidmode = DEFAULT_VIDMODE;
	filter->density = DEFAULT_DENSITY;
	filter->fmt = DEFAULT_FORMAT;
	filter->format = format(filter->vidmode, filter->density, filter->fmt);
	filter->adapter = gst_adapter_new();

#if 0
	filter->worst_key = filter->format.key_length;
	filter->best_nonkey = 0;
	filter->frame_warnings = 0;
	filter->last_field_offset = -1;
	filter->smallest_field = INT_MAX;
	filter->largest_field = 0;
#endif
}


/*
 * bkr_framedec_get_type().
 */

GType bkr_framedec_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrFrameDecClass),
			.class_init = (GClassInitFunc) dec_class_init,
			.base_init = (GBaseInitFunc) dec_base_init,
			.instance_size = sizeof(BkrFrameDec),
			.instance_init = (GInstanceInitFunc) dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrFrameDec", &info, 0);
	}
	return type;
}

