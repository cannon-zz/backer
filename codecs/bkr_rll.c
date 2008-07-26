/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                         Run Length Limiting Codec
 *
 * Copyright (C) 2000,2001,2002,2008  Kipp C. Cannon
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
#include <backer.h>
#include <bkr_bytes.h>
#include <bkr_elements.h>
#include <bkr_rll.h>


/*
 * ========================================================================
 *
 *                              PARAMETERS
 *
 * ========================================================================
 */


/*
 * Format info.
 */


static struct bkr_rll_format *compute_format(enum bkr_videomode v, enum bkr_bitdensity d)
{
	struct bkr_rll_format initializer;
	struct bkr_rll_format *format;

	switch(d) {
	case BKR_LOW:
		switch(v) {
		case BKR_NTSC:
			initializer = (struct bkr_rll_format) { 816, 102};
			break;
		case BKR_PAL:
			initializer = (struct bkr_rll_format) { 984, 123};
			break;
		default:
			GST_DEBUG("unrecognized videomode");
			return NULL;
		}
		break;
	case BKR_HIGH:
		switch(v) {
		case BKR_NTSC:
			initializer = (struct bkr_rll_format) {2072, 259};
			break;
		case BKR_PAL:
			initializer = (struct bkr_rll_format) {2496, 312};
			break;
		default:
			GST_DEBUG("unrecognized videomode");
			return NULL;
		}
		break;
	default:
		GST_DEBUG("unrecognized bitdensity");
		return NULL;
	}

	format = malloc(sizeof(*format));
	if(!format) {
		GST_DEBUG("memory allocation failure");
		return NULL;
	}

	*format = initializer;

	return format;
}


/*
 * ========================================================================
 *
 *                              Global Data
 *
 * ========================================================================
 */


static const guint16 rll_encode[] = {
	0x089, 0x08a, 0x08b, 0x08c, 0x08d, 0x08e, 0x091, 0x092,
	0x093, 0x094, 0x095, 0x096, 0x099, 0x09a, 0x09b, 0x09c,
	0x09d, 0x09e, 0x0a2, 0x0a3, 0x0a4, 0x0a5, 0x0a6, 0x0a9,
	0x0aa, 0x0ab, 0x0ac, 0x0ad, 0x0ae, 0x0b1, 0x0b2, 0x0b3,
	0x0b4, 0x0b5, 0x0b6, 0x0b9, 0x0ba, 0x0bb, 0x0bc, 0x0bd,
	0x0be, 0x0c2, 0x0c3, 0x0c4, 0x0c5, 0x0c6, 0x0c9, 0x0ca,
	0x0cb, 0x0cc, 0x0cd, 0x0ce, 0x0d1, 0x0d2, 0x0d3, 0x0d4,
	0x0d5, 0x0d6, 0x0d9, 0x0da, 0x0db, 0x0dc, 0x0dd, 0x0de,
	0x0e1, 0x0e2, 0x0e3, 0x0e4, 0x0e5, 0x0e6, 0x0e9, 0x0ea,
	0x0eb, 0x0ec, 0x0ed, 0x0ee, 0x0f1, 0x0f2, 0x0f3, 0x0f4,
	0x0f5, 0x0f6, 0x0f9, 0x0fa, 0x0fb, 0x0fc, 0x0fd, 0x109,
	0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x111, 0x112, 0x113,
	0x114, 0x115, 0x116, 0x119, 0x11a, 0x11b, 0x11c, 0x11d,
	0x11e, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x129,
	0x12a, 0x12b, 0x12c, 0x12d, 0x12e, 0x131, 0x132, 0x133,
	0x134, 0x135, 0x136, 0x139, 0x13a, 0x13b, 0x13c, 0x13d,
	0x13e, 0x142, 0x143, 0x144, 0x145, 0x146, 0x149, 0x14a,
	0x14b, 0x14c, 0x14d, 0x14e, 0x151, 0x152, 0x153, 0x154,
	0x155, 0x156, 0x159, 0x15a, 0x15b, 0x15c, 0x15d, 0x15e,
	0x161, 0x162, 0x163, 0x164, 0x165, 0x166, 0x169, 0x16a,
	0x16b, 0x16c, 0x16d, 0x16e, 0x171, 0x172, 0x173, 0x174,
	0x175, 0x176, 0x179, 0x17a, 0x17b, 0x17c, 0x17d, 0x17e,
	0x184, 0x185, 0x186, 0x189, 0x18a, 0x18b, 0x18c, 0x18d,
	0x18e, 0x191, 0x192, 0x193, 0x194, 0x195, 0x196, 0x199,
	0x19a, 0x19b, 0x19c, 0x19d, 0x19e, 0x1a1, 0x1a2, 0x1a3,
	0x1a4, 0x1a5, 0x1a6, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad,
	0x1ae, 0x1b1, 0x1b2, 0x1b3, 0x1b4, 0x1b5, 0x1b6, 0x1b9,
	0x1ba, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1c2, 0x1c3, 0x1c4,
	0x1c5, 0x1c6, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce,
	0x1d1, 0x1d2, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d9, 0x1da,
	0x1db, 0x1dc, 0x1dd, 0x1de, 0x1e1, 0x1e2, 0x1e3, 0x1e4,
	0x1e5, 0x1e6, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ee
};


static guint8 rll_decode[512];


#define  RLL_MASK  ((guint16) 0x01ff)


/*
 * ========================================================================
 *
 *                              CODEC Functions
 *
 * ========================================================================
 */


/* use inline assembly if possible */
#if 1
#define  rolw(x, n)  asm("rolw %2, %0" : "=r" (x) : "0" (x), "c" (n))
#else
#define  rolw(x, n)  do { x = bswap_16(x) >> (8 - (n)) } while(0)
#endif


static void rll_demodulate(const guint8 *src, guint8 *dst, gint n)
{
	guint16 state = 0, rgstr;
	gint8 shift = 1;

	n >>= 3;

	while(1) {
		rgstr = __le16_to_cpu(get_unaligned((guint16 *) src++));
		rolw(rgstr, shift);
		if(state & 1)
			rgstr = ~rgstr;
		state ^= rgstr;
		*dst++ = rll_decode[rgstr & RLL_MASK];
		if(++shift > 8) {
			if(!--n)
				break;
			src++;
			shift = 1;
		}
	}
}


static void rll_modulate(const guint8 *src, guint8 *dst, gint n)
{
	guint16 state = 0, rgstr = 0;
	gint8 shift = 7;

	n >>= 3;

	while(1) {
		if(state &= 1)
			state = RLL_MASK;
		state ^= rll_encode[*src++];
		rgstr |= state << shift;
		if(--shift >= 0) {
			rgstr = bswap_16(rgstr);
			*dst++ = rgstr; /* write low byte */
			rgstr &= (guint16) 0xff00;
		} else {
			put_unaligned(__cpu_to_be16(rgstr), (guint16 *) dst);
			if(!--n)
				break;
			dst += sizeof(guint16);
			rgstr = 0;
			shift = 7;
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
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static struct bkr_rll_format *caps_to_format(GstCaps *caps)
{
	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;
	enum bkr_sectorformat sectorformat;

	if(!bkr_parse_caps(caps, &videomode, &bitdensity, &sectorformat)) {
		GST_DEBUG("failure parsing caps");
		return NULL;
	}

	if(sectorformat != BKR_EP) {
		GST_DEBUG("sectorformat != BKR_EP");
		return NULL;
	}

	return compute_format(videomode, bitdensity);
}


static gboolean enc_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrRLLEnc *filter = BKR_RLLENC(gst_pad_get_parent(pad));

	free(filter->format);
	filter->format = caps_to_format(caps);

	/* FIXME:  strictly, the unref shouldn't be done while we still
	 * need to check its contents */
	gst_object_unref(filter);

	return filter->format ? TRUE : FALSE;
}


/*
 * Buffer alloc function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static GstFlowReturn enc_bufferalloc(GstPad *pad, guint64 offset, guint size, GstCaps *caps, GstBuffer **buf)
{
	size_t buffer_size;
	GstFlowReturn result;

	/* incase something goes wrong */
	*buf = NULL;

	/* avoid computing the format if we already know what it is */
	if(caps == GST_PAD_CAPS(pad)) {
		BkrRLLEnc *filter = BKR_RLLENC(gst_pad_get_parent(pad));
		buffer_size = filter->format->capacity;
		gst_object_unref(filter);
	} else {
		struct bkr_rll_format *format = caps_to_format(caps);
		if(!format) {
			/* FIXME:  is this enough error handling? */
			result = GST_FLOW_ERROR;
			goto done;
		}
		buffer_size = format->capacity;
		free(format);
	}

	*buf = gst_buffer_new_and_alloc(buffer_size);
	if(!*buf) {
		result = GST_FLOW_ERROR;
		goto done;
	}

	gst_buffer_set_caps(*buf, caps);
	result = GST_FLOW_OK;

done:
	return result;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn enc_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrRLLEnc *filter = BKR_RLLENC(gst_pad_get_parent(pad));
	GstPad *srcpad = filter->srcpad;
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstBuffer *srcbuf;
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	if(GST_BUFFER_SIZE(sinkbuf) != filter->format->capacity) {
		GST_ELEMENT_ERROR(filter, STREAM, FAILED, ("received incorrect buffer size, got %d bytes expected %d bytes.", GST_BUFFER_SIZE(sinkbuf), filter->format->capacity), (NULL));
		result = GST_FLOW_ERROR;
		goto done;
	}

	result = gst_pad_alloc_buffer(srcpad, GST_BUFFER_OFFSET_NONE, filter->format->capacity + filter->format->modulation_pad, caps, &srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_alloc_buffer() failed");
		goto done;
	}

	rll_modulate(GST_BUFFER_DATA(sinkbuf), GST_BUFFER_DATA(srcbuf), filter->format->capacity);

	result = gst_pad_push(srcpad, srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		goto done;
	}

done:
	gst_caps_unref(caps);
	gst_buffer_unref(sinkbuf);
	gst_object_unref(filter);
	return result;
}


/*
 * Parent class
 */


static GstElementClass *enc_parent_class = NULL;


/*
 * Instance finalize function.  See ???
 */


static void enc_finalize(GObject *object)
{
	BkrRLLEnc *filter = BKR_RLLENC(object);

	free(filter->format);
	filter->format = NULL;

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
		"Backer RLL Encoder",
		"Filter",
		"Backer run-length limiting bit modulation encoder",
		"Kipp Cannon <kcannon@ligo.caltech.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
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
	BkrRLLEnc *filter = BKR_RLLENC(object);
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
	filter->format = NULL;
}


/*
 * bkr_rllenc_get_type().
 */


GType bkr_rllenc_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrRLLEncClass),
			.class_init = enc_class_init,
			.base_init = enc_base_init,
			.instance_size = sizeof(BkrRLLEnc),
			.instance_init = enc_instance_init,
		};
		int  i;

		/* construct the decode look-up table */
		memset(rll_decode, (gint8) -1, 512);
		for(i = 0; i < 256; i++)
			rll_decode[rll_encode[i]] = i;

		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrRLLEnc", &info, 0);
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
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static gboolean dec_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrRLLDec *filter = BKR_RLLDEC(gst_pad_get_parent(pad));

	free(filter->format);
	filter->format = caps_to_format(caps);

	gst_object_unref(filter);

	return filter->format ? TRUE : FALSE;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn dec_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrRLLDec *filter = BKR_RLLDEC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstPad *srcpad = filter->srcpad;
	GstBuffer *srcbuf;
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	result = gst_pad_alloc_buffer(srcpad, GST_BUFFER_OFFSET_NONE, filter->format->capacity, caps, &srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_alloc_buffer() failed");
		goto done;
	}

	rll_demodulate(GST_BUFFER_DATA(sinkbuf), GST_BUFFER_DATA(srcbuf), filter->format->capacity);

	result = gst_pad_push(srcpad, srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		goto done;
	}

done:
	gst_caps_unref(caps);
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
	BkrRLLDec *filter = BKR_RLLDEC(object);

	free(filter->format);
	filter->format = NULL;

	G_OBJECT_CLASS(enc_parent_class)->finalize(object);
}


/*
 * Base init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GBaseInitFunc
 */


static void dec_base_init(gpointer class)
{
	static GstElementDetails plugin_details = {
		"Backer RLL Decoder",
		"Filter",
		"Backer run-length limiting bit modulation decoder",
		"Kipp Cannon <kcannon@ligo.caltech.edu>"
	};
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GstElementClass *element_class = GST_ELEMENT_CLASS(class);
	GstPadTemplate *sinkpad_template = gst_pad_template_new(
		"sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);
	GstPadTemplate *srcpad_template = gst_pad_template_new(
		"src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		bkr_get_template_caps()
	);

	gst_element_class_set_details(element_class, &plugin_details);

	object_class->finalize = dec_finalize;

	gst_element_class_add_pad_template(element_class, sinkpad_template);
	gst_element_class_add_pad_template(element_class, srcpad_template);
}


/*
 * Class init function.  See
 *
 * http://developer.gnome.org/doc/API/2.0/gobject/gobject-Type-Information.html#GClassInitFunc
 */


static void dec_class_init(gpointer class, gpointer class_data)
{
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
	BkrRLLDec *filter = BKR_RLLDEC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* confiugre sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, dec_setcaps);
	gst_pad_set_chain_function(pad, dec_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->format = NULL;
}


/*
 * bkr_rlldec_get_type().
 */


GType bkr_rlldec_get_type(void)
{
	static GType type = 0;

	if(!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrRLLDecClass),
			.class_init = dec_class_init,
			.base_init = dec_base_init,
			.instance_size = sizeof(BkrRLLDec),
			.instance_init = dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrRLLDec", &info, 0);
	}
	return type;
}
