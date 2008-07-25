/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                               Framing Codec
 *
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


#include <string.h>


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <backer.h>
#include <bkr_elements.h>
#include <bkr_frame.h>


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
	guint count;

	for(count = 0; key_length--; data += key_interval)
		count += *data == *(key++);

	return count;
}


/*
 * Uses correlate() to scan a buffer until a sector key sequence is found.
 * On success, the return value is a pointer to the first byte following
 * the sector leader; otherwise the return value is NULL.
 */


static const guint8 *find_field(BkrFrameDec *filter, const guint8 *key)
{
	gint threshold = filter->format.key_length * FRAME_THRESHOLD_A / FRAME_THRESHOLD_B;
	const guint8 *data;
	gint corr;

	while(1) {
		data = gst_adapter_peek(filter->adapter, filter->format.active_size);
		if(!data)
			return NULL;

		corr = correlate(data, filter->format.key_interval, filter->format.key_length, key);
		if(corr >= threshold)
			break;
		if(corr > filter->best_nonkey)
			filter->best_nonkey = corr;
		gst_adapter_flush(filter->adapter, 1);
	}

	if(corr < filter->worst_key)
		filter->worst_key = corr;

#if 0
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

	return data;
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
 * Statistics
 */


static void reset_statistics(BkrFrameDec *filter)
{
	filter->worst_key = filter->format.key_length;
	filter->best_nonkey = 0;
	filter->frame_warnings = 0;
	filter->last_field_offset = -1;
	filter->smallest_field = INT_MAX;
	filter->largest_field = 0;
}


/*
 * Format info.
 */


static struct bkr_frame_format compute_format(enum bkr_videomode v, enum bkr_bitdensity d, enum bkr_sectorformat f)
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
			default:
				GST_DEBUG("unrecognized sectorformat");
				break;
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {1220, 0,  48, 36, 1136,  40, 29};
			case BKR_SP:
				return (struct bkr_frame_format) {1220, 0,  40, 36, 1144,  49, 24};
			default:
				GST_DEBUG("unrecognized sectorformat");
				break;
			}
		default:
			GST_DEBUG("unrecognized videomode");
			break;
		}
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {2530, 10, 100, 70, 2360,  84, 29};
			case BKR_SP:
				return (struct bkr_frame_format) {2530, 10,  80, 70, 2380, 125, 20};
			default:
				GST_DEBUG("unrecognized sectorformat");
				break;
			}
		case BKR_PAL:
			switch(f) {
			case BKR_EP:
				return (struct bkr_frame_format) {3050, 0, 120, 90, 2840,  91, 32};
			case BKR_SP:
				return (struct bkr_frame_format) {3050, 0, 100, 90, 2860, 136, 22};
			default:
				GST_DEBUG("unrecognized sectorformat");
				break;
			}
		default:
			GST_DEBUG("unrecognized videomode");
			break;
		}
	default:
		GST_DEBUG("unrecognized bitdensity");
		break;
	}

	return (struct bkr_frame_format) {0,};
}


/*
 * ============================================================================
 *
 *                       GStreamer Encoder Support Code
 *
 * ============================================================================
 */


/*
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static struct bkr_frame_format *caps_to_format(struct bkr_frame_format *format, GstCaps *caps)
{
	const GstStructure *s;
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	s = gst_caps_get_structure(caps, 0);
	if(!s) {
		GST_DEBUG("failed to retrieve structure from caps");
		return NULL;
	}
	/* FIXME:  when I figure out how to make the caps integers, put
	 * these back */
	/*if(!gst_structure_get_enum(s, "videomode", BKR_TYPE_VIDEOMODE, (int *) &videomode)) {*/
	if(!gst_structure_get_int(s, "videomode", (int *) &videomode)) {
		GST_DEBUG("could not retrieve videomode from caps");
		return NULL;
	}
	/*if(!gst_structure_get_enum(s, "bitdensity", BKR_TYPE_BITDENSITY, (int *) &bitdensity)) {*/
	if(!gst_structure_get_int(s, "bitdensity", (int *) &bitdensity)) {
		GST_DEBUG("could not retrieve bitdensity from caps");
		return NULL;
	}
	/*if(!gst_structure_get_enum(s, "sectorformat", BKR_TYPE_SECTORFORMAT, (int *) &sectorformat)) {*/
	if(!gst_structure_get_int(s, "sectorformat", (int *) &sectorformat)) {
		GST_DEBUG("could not retrieve sectorformat from caps");
		return NULL;
	}

	*format = compute_format(videomode, bitdensity, sectorformat);

	return format;
}


static gboolean enc_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(gst_pad_get_parent(pad));
	gboolean result;

	result = caps_to_format(&filter->format, caps) != NULL;

	gst_object_unref(filter);

	return result;
}


/*
 * Buffer alloc function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static GstFlowReturn enc_bufferalloc(GstPad *pad, guint64 offset, guint size, GstCaps *caps, GstBuffer **buf)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(gst_pad_get_parent(pad));
	struct bkr_frame_format format;
	GstFlowReturn result;

	/* avoid computing the format if we already know what it is */
	if(caps == GST_PAD_CAPS(pad))
		format = filter->format;
	else {
		/* FIXME:  add error handling errors */
		result = caps_to_format(&format, caps) ? GST_FLOW_OK : GST_FLOW_OK;
	}

	*buf = gst_buffer_new_and_alloc(format.active_size - format.key_length);
	if(!*buf) {
		result = GST_FLOW_ERROR;
		goto done;
	}

	gst_buffer_set_caps(*buf, caps);
	result = GST_FLOW_OK;

done:
	gst_object_unref(filter);
	return result;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn enc_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstPad *srcpad = filter->srcpad;
	GstBuffer *srcbuf;
	GstFlowReturn result;

	if(!caps) {
		GST_DEBUG("caps not set on buffer");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}
	if(GST_BUFFER_SIZE(sinkbuf) != filter->format.active_size - filter->format.key_length) {
		GST_ELEMENT_ERROR(filter, STREAM, FAILED, ("received incorrect buffer size, got %d bytes expected %d bytes.", GST_BUFFER_SIZE(sinkbuf), filter->format.active_size - filter->format.key_length), (NULL));
		result = GST_FLOW_ERROR;
		goto done;
	}

	result = gst_pad_alloc_buffer(srcpad, GST_BUFFER_OFFSET_NONE, filter->format.field_size + (filter->odd_field ? filter->format.interlace : 0), caps, &srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_alloc_buffer() failed");
		goto done;
	}

	encode_field(filter->format, GST_BUFFER_DATA(srcbuf), GST_BUFFER_DATA(sinkbuf), sector_key, filter->odd_field);

	result = gst_pad_push(srcpad, srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		goto done;
	}

	filter->odd_field ^= 1;

done:
	gst_caps_unref(caps);
	gst_buffer_unref(sinkbuf);
	gst_object_unref(filter);
	return result;
}


/*
 * Parent class.
 */


static GstElementClass *enc_parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */


static void enc_finalize(GObject *object)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(object);

	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;

	G_OBJECT_CLASS(enc_parent_class)->finalize(object);
}



/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */


static void enc_base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer Frame Encoder",
		"Filter",
		"Backer frame synchronization encoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		/* FIXME:  I figure out how to make the caps enums */
		gst_caps_from_string(
			"application/x-backer, " \
			"videomode=(int){ 1, 2 }, " \
			"bitdensity=(int){ 4, 8 }, " \
			"sectorformat=(int){ 16, 32 }"
		)
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_CAPS_ANY
	);

	gst_element_class_set_details(element_class, &plugin_details);

	object_class->finalize = enc_finalize;

	gst_element_class_add_pad_template(element_class, sinkpad_template);
	gst_element_class_add_pad_template(element_class, srcpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void enc_class_init(gpointer class, gpointer class_data)
{
	enc_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */


static void enc_instance_init(GTypeInstance *object, gpointer class)
{
	GstElement *element = GST_ELEMENT(object);
	BkrFrameEnc *filter = BKR_FRAMEENC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, enc_setcaps);
	gst_pad_set_bufferalloc_function(pad, enc_bufferalloc);
	gst_pad_set_chain_function(pad, enc_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->odd_field = 1;
}


/*
 * bkr_frameenc_get_type().
 */


GType bkr_frameenc_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrFrameEncClass),
			.class_init = enc_class_init,
			.base_init = enc_base_init,
			.instance_size = sizeof(BkrFrameEnc),
			.instance_init = enc_instance_init,
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
 * Properties
 */


enum property {
	ARG_VIDEOMODE = 1,
	ARG_BITDENSITY,
	ARG_SECTORFORMAT
};


static void dec_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrFrameDec *filter = BKR_FRAMEDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		filter->videomode = g_value_get_enum(value);
		break;

	case ARG_BITDENSITY:
		filter->bitdensity = g_value_get_enum(value);
		break;

	case ARG_SECTORFORMAT:
		filter->sectorformat = g_value_get_enum(value);
		break;
	}

	filter->format = compute_format(filter->videomode, filter->bitdensity, filter->sectorformat);
	reset_statistics(filter);
}


static void dec_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrFrameDec *filter = BKR_FRAMEDEC(object);

	switch(id) {
	case ARG_VIDEOMODE:
		g_value_set_enum(value, filter->videomode);
		break;

	case ARG_BITDENSITY:
		g_value_set_enum(value, filter->bitdensity);
		break;

	case ARG_SECTORFORMAT:
		g_value_set_enum(value, filter->sectorformat);
		break;
	}
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn dec_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrFrameDec *filter = BKR_FRAMEDEC(gst_pad_get_parent(pad));
	GstPad *srcpad = filter->srcpad;
	GstBuffer *srcbuf;
	const guint8 *data;
	GstFlowReturn result;

	if(!GST_PAD_CAPS(srcpad)) {
		GST_DEBUG("caps not set on src pad");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	gst_adapter_push(filter->adapter, sinkbuf);

	while((data = find_field(filter, sector_key))) {
		result = gst_pad_alloc_buffer(filter->srcpad, GST_BUFFER_OFFSET_NONE, filter->format.active_size - filter->format.key_length, GST_PAD_CAPS(filter->srcpad), &srcbuf);
		if(result != GST_FLOW_OK)
			goto done;

		decode_field(filter->format, GST_BUFFER_DATA(srcbuf), data);

		gst_adapter_flush(filter->adapter, filter->format.active_size);

		result = gst_pad_push(filter->srcpad, srcbuf);

		if(result != GST_FLOW_OK)
			goto done;
	}

done:
	gst_object_unref(filter);
	return result;
}


/*
 * Parent class.
 */


static GstElementClass *dec_parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */


static void dec_finalize(GObject *object)
{
	BkrFrameDec *filter = BKR_FRAMEDEC(object);

	g_object_unref(filter->adapter);
	filter->adapter = NULL;
	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;

	G_OBJECT_CLASS(dec_parent_class)->finalize(object);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */


static void dec_base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer Frame Decoder",
		"Filter",
		"Backer frame synchronization decoder",
		"Kipp Cannon <kipp@gravity.phys.uwm.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_CAPS_ANY
	);

	gst_element_class_set_details(element_class, &plugin_details);

	object_class->set_property = dec_set_property;
	object_class->get_property = dec_get_property;
	object_class->finalize = dec_finalize;

	gst_element_class_add_pad_template(element_class, sinkpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void dec_class_init(gpointer class, gpointer class_data)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_VIDEOMODE, g_param_spec_enum("videomode", "Video mode", "Video mode", BKR_TYPE_VIDEOMODE, DEFAULT_VIDEOMODE, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_BITDENSITY, g_param_spec_enum("bitdensity", "Bit density", "Bit density", BKR_TYPE_BITDENSITY, DEFAULT_BITDENSITY, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_SECTORFORMAT, g_param_spec_enum("sectorformat", "Sector format", "Sector format", BKR_TYPE_SECTORFORMAT, DEFAULT_SECTORFORMAT, G_PARAM_READWRITE));

	dec_parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}


/*
 * Instance init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GInstanceInitFunc
 */


static void dec_instance_init(GTypeInstance *object, gpointer class)
{
	GstElement *element = GST_ELEMENT(object);
	BkrFrameDec *filter = BKR_FRAMEDEC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_chain_function(pad, dec_chain);
	gst_object_unref(pad);

	/* create and configure src pad */
	pad = gst_pad_new("src", GST_PAD_SRC);
	gst_element_add_pad(GST_ELEMENT(filter), pad);

	/* save a reference */
	filter->srcpad = pad;
	gst_object_ref(pad);

	/* internal state */
	filter->adapter = gst_adapter_new();
	reset_statistics(filter);
}


/*
 * bkr_framedec_get_type().
 */


GType bkr_framedec_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrFrameDecClass),
			.class_init = dec_class_init,
			.base_init = dec_base_init,
			.instance_size = sizeof(BkrFrameDec),
			.instance_init = dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrFrameDec", &info, 0);
	}
	return type;
}
