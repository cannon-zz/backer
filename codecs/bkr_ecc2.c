/*
 * Driver for Danmere's Backer 16/32 video tape backup cards.
 *
 *                   Sector Drop-Out Error Correction Codec
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


/*
 * ========================================================================
 *
 *                                  Preamble
 *
 * ========================================================================
 */


#include <string.h>


#include <gst/gst.h>
#include <backer.h>
#include <bkr_elements.h>
#include <bkr_bytes.h>
#include <bkr_ecc2.h>
#include <rs.h>


/*
 * ========================================================================
 *
 *                              Parameters
 *
 * ========================================================================
 */


/*
 * Tests show the mean sector loss rate to be 36/107015 ~= 3*10^-3.  If
 * it's a Poisson process (which it probably isn't, but bear with me), then
 * the probability of observing exactly n bad sectors in a group of 255 is
 *
 * P(n) = 255! / n! / (255 - n)! * (36/107015)^n * (106979/107015)^(255 - n)
 *
 * P(0) = 0.9177807063            P(>0) = 0.8221929363e-1
 * P(1) = 0.7875589493e-1         P(>1) = 0.3463398695e-2
 * P(2) = 0.3365819008e-2         P(>2) = 0.9757968701e-4
 * P(3) = 0.9551992922e-4         P(>3) = 0.2059757784e-5
 * P(4) = 0.2025062857e-5         P(>4) = 0.3469492608e-7
 * P(5) = 0.342094579e-7          P(>5) = 0.4854681841e-9
 * P(6) = 0.4796659797e-9         P(>6) = 0.5802204293e-11
 * P(7) = 0.5741748838e-11        P(>7) = 0.6045545561e-13
 * P(8) = 0.5989765938e-13        P(>8) = 0.5577962293e-15
 * P(9) = 0.5531822832e-15        P(>9) = 0.4613946186e-17
 * P(10) = 0.4579386889e-17       P(>10) = 0.3455929702e-19
 *      ...                             ...
 * P(20) = 0.8228609676e-40       P(>20) = 0.3109825249e-42
 *
 * I tested a 545 MB recording, consisting of 321045 sectors, or 1259
 * groups.  Of those, I observed 1 group to have 3 sectors requiring
 * correction, no group required more than that.  According to the table
 * above, there is a 12% chance of that occuring:  P(>2) * 1259 = 0.12.
 * That's a little low considering that I saw it happen, but then sector
 * failures are probably not a Poisson process (one probably finds bad
 * sectors clustered together), and so the cumulative probabilities listed
 * above are likely underestimates but also aparently not far off.
 *
 * A T-120 tape in EP mode (6 hours) has about 1296000 sectors, or about
 * 5100 ~= 10^4 sector groups.  Therefore, with 10 parity sectors per group
 * of 255 we expect something like 1 tape in 10^13 to have at least one
 * uncorrectable sector group.
 *
 * Therefore, 20 parity sectors per group of 255 is probably a huge
 * over-kill.  One could probably reduce it to 10 or even fewer quite
 * safely, thereby increasing the tape capacity by 4% or more.
 */


#define  ECC2_TIMEOUT_MULT  1
#define  BLOCK_SIZE         255
#define  PARITY             20
#define  ECC2_FILLER        0x00


/*
 * Header definition
 */


struct bkr_ecc2_header {
	guint32 length;
};


/*
 * Format info.
 */


static struct bkr_ecc2_format *compute_format(enum bkr_videomode videomode, enum bkr_bitdensity bitdensity)
{
	int sector_capacity;
	struct bkr_ecc2_format *format;

	switch(bitdensity) {
	case BKR_LOW:
		switch(videomode) {
		case BKR_NTSC:
			sector_capacity = 716;
			break;
		case BKR_PAL:
			sector_capacity = 884;
			break;
		default:
			GST_DEBUG("unrecognized videomode");
			return NULL;
		}
		break;
	case BKR_HIGH:
		switch(videomode) {
		case BKR_NTSC:
			sector_capacity = 1844;
			break;
		case BKR_PAL:
			sector_capacity = 2284;
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

	*format = (struct bkr_ecc2_format) {
		BLOCK_SIZE * sector_capacity,
		(BLOCK_SIZE - PARITY) * sector_capacity,
		PARITY * sector_capacity,
		(BLOCK_SIZE - PARITY) * sector_capacity - sizeof(struct bkr_ecc2_header),
		sector_capacity
	};

	return format;
}


/*
 * ========================================================================
 *
 *                                   Header
 *
 * ========================================================================
 */


static struct bkr_ecc2_header get_header(const guint8 *data, const struct bkr_ecc2_format *format)
{
	struct bkr_ecc2_header header;

	memcpy(&header, data + format->capacity, sizeof(header));
	header.length = __le32_to_cpu(header.length);

	return header;
}


static void put_header(guint8 *data, const struct bkr_ecc2_format *format, int length)
{
	struct bkr_ecc2_header header = {.length = length};

	header.length = __cpu_to_le32(header.length);
	memcpy(data + format->capacity, &header, sizeof(header));
}


/*
 * ========================================================================
 *
 *              Multi-Sector Forward Error Correction CODEC
 *
 * ========================================================================
 */


/*
 * Decode a sector from the adapter.  Note that the buffer returned by this
 * function does not have any metadata set on it except its size.
 */


static GstFlowReturn decode_group(BkrECC2Dec *filter, GstBuffer **srcbuf)
{
	guint8 *data;
	int block;
	int corrections;
	struct bkr_ecc2_header header;

	/*
	 * Extract the data from the adapter as a new buffer.
	 */

	*srcbuf = gst_adapter_take_buffer(filter->adapter, filter->format->group_size);
	if(!*srcbuf) {
		GST_DEBUG("gst_adapter_take_buffer() failed");
		return GST_FLOW_ERROR;
	}
	data = GST_BUFFER_DATA(*srcbuf);

	/*
	 * Do error correction.
	 */

	/* This pre-processor conditional disables error correction.
	 * Useful for confirming that the decoding pipeline is, infact, the
	 * inverse of the encoding pipeline (the error corrector could be
	 * hiding off-by-one problems by just fixing the data). */
#if 1
	for(block = 0; block < filter->format->interleave; block++) {
		memcpy(filter->rs_format->erasure, filter->erasure, filter->num_erasure * sizeof(gf));
		corrections = reed_solomon_decode(data + filter->format->data_size + block, data + block, filter->num_erasure, *filter->rs_format);
		if(corrections < 0) {
			/* uncorrectable block.  ignore, there's nothing we
			 * can do at this point anyway. */
			continue;
		}
		if(corrections > filter->worst_group)
			filter->worst_group = corrections;
		if(corrections > filter->num_erasure) {
			/* error corrector identified additional corrupt
			 * sectors, beyond what the sector decoder told us
			 * about.  add them to our list, and pass them in
			 * as erasures for the next block.  */
			filter->extra_errors += corrections - filter->num_erasure;
			memcpy(filter->erasure, filter->rs_format->erasure, corrections * sizeof(gf));
			filter->num_erasure = corrections;
		}
	}
#endif
	filter->num_erasure = 0;

	/*
	 * Retrieve header, and resize the buffer.
	 */

	header = get_header(data, filter->format);
	GST_BUFFER_SIZE(*srcbuf) = header.length;

	/*
	 * Done
	 */

	return GST_FLOW_OK;
}


/*
 * Write a (possibly short) sector group.  Takes as much data as will fit
 * into a sector group from the filter's adapter, encodes it, and pushes it
 * out the srcpad.  If there isn't enough data in the adapter to fill a
 * sector group then a short sector is encoded.  This is a waste of tape if
 * this is not the end of the input stream.
 */


#ifndef min
#define min(x,y) ({ \
	const typeof(x) _x = (x); \
	const typeof(y) _y = (y); \
	(void) (&_x == &_y); \
	_x < _y ? _x : _y ; \
})
#endif


static GstFlowReturn write_group(BkrECC2Enc *filter, GstCaps *caps)
{
	size_t size = min(gst_adapter_available(filter->adapter), (unsigned) filter->format->capacity);
	GstBuffer *srcbuf;
	const guint8 *data;
	int block;
	GstFlowReturn result;

	data = gst_adapter_peek(filter->adapter, size);

	result = gst_pad_alloc_buffer(filter->srcpad, GST_BUFFER_OFFSET_NONE, filter->format->group_size, caps, &srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_alloc_buffer() failed");
		return result;
	}

	/* copy data from adapter into buffer */
	memcpy(GST_BUFFER_DATA(srcbuf), data, size);

	/* pad with 0 if short */
	memset(GST_BUFFER_DATA(srcbuf) + size, 0, filter->format->capacity - size);

	/* insert the header */
	put_header(GST_BUFFER_DATA(srcbuf), filter->format, size);

	/* compute parity */
	for(block = 0; block < filter->format->interleave; block++)
		reed_solomon_encode(GST_BUFFER_DATA(srcbuf) + filter->format->data_size + block, GST_BUFFER_DATA(srcbuf) + block, *filter->rs_format);

	gst_adapter_flush(filter->adapter, size);

	/* transmit buffer */
	result = gst_pad_push(filter->srcpad, srcbuf);
	if(result != GST_FLOW_OK) {
		GST_DEBUG("gst_pad_push() failed");
		return result;
	}

	return GST_FLOW_OK;
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


static struct bkr_ecc2_format *caps_to_format(GstCaps *caps)
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
	BkrECC2Enc *filter = BKR_ECC2ENC(gst_pad_get_parent(pad));
	gboolean result;

	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;

	free(filter->format);
	filter->format = caps_to_format(caps);
	if(filter->format) {
		filter->rs_format = reed_solomon_codec_new((filter->format->data_size + filter->format->parity_size) / filter->format->interleave, filter->format->data_size / filter->format->interleave, filter->format->interleave);
		if(!filter->rs_format) {
			GST_DEBUG("reed_solomon_codec_new() failed");
			free(filter->format);
			filter->format = NULL;
		}
	}

	result = filter->format ? TRUE : FALSE;

	gst_object_unref(filter);

	return result;
}


/*
 * Event function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadEventFunction
 */


static GstFlowReturn enc_flush(BkrECC2Enc *filter, GstCaps *caps)
{
	/*
	 * write any unfinished group
	 */

	while(gst_adapter_available(filter->adapter)) {
		GstFlowReturn result = write_group(filter, caps);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("write_group() failed");
			return result;
		}
	}

	return GST_FLOW_OK;
}


static gboolean enc_event(GstPad *pad, GstEvent *event)
{
	BkrECC2Enc *filter = BKR_ECC2ENC(gst_pad_get_parent(pad));
	GstCaps *caps = GST_PAD_CAPS(pad);
	gboolean result;

	switch(GST_EVENT_TYPE(event)) {
	case GST_EVENT_EOS:
		/*
		 * there is no special end-of-record mark, the data simply
		 * ends.  just flush the adapter.
		 */

		if(enc_flush(filter, caps) != GST_FLOW_OK) {
			GST_DEBUG("enc_flush() failed");
			gst_event_unref(event);
			result = FALSE;
			break;
		}

		/*
		 * forward the end-of-stream event.
		 */

		result = gst_pad_push_event(filter->srcpad, event);
		break;

	default:
		/*
		 * FIXME:  if GST_EVENT_NEWSEGMENT comes, do something
		 * special if a recording is in progress?
		 */
		result = gst_pad_event_default(pad, event);
		break;
	}

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
	BkrECC2Enc *filter = BKR_ECC2ENC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	gst_adapter_push(filter->adapter, sinkbuf);

	while(gst_adapter_available(filter->adapter) >= filter->format->capacity) {
		result = write_group(filter, caps);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("write_group() failed");
			goto done;
		}
	}

	result = GST_FLOW_OK;

done:
	gst_caps_unref(caps);
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
	BkrECC2Enc *filter = BKR_ECC2ENC(object);

	g_object_unref(filter->adapter);
	filter->adapter = NULL;
	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;
	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
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
		"Backer ECC2 Encoder",
		"Filter",
		"Backer sector drop-out error correction encoder",
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
	BkrECC2Enc *filter = BKR_ECC2ENC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, enc_setcaps);
	gst_pad_set_event_function(pad, enc_event);
	gst_pad_set_chain_function(pad, enc_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->adapter = gst_adapter_new();
	filter->rs_format = NULL;
	filter->format = NULL;
}


/*
 * bkr_ecc2enc_get_type().
 */


GType bkr_ecc2enc_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrECC2EncClass),
			.class_init = enc_class_init,
			.base_init = enc_base_init,
			.instance_size = sizeof(BkrECC2Enc),
			.instance_init = enc_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrECC2Enc", &info, 0);
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
	ARG_DEC_WORST_GROUP = 1,
	ARG_DEC_EXTRA_ERRORS
};


static void dec_set_property(GObject *object, enum property id, const GValue *value, GParamSpec *pspec)
{
	BkrECC2Dec *filter = BKR_ECC2DEC(object);

	switch(id) {
	case ARG_DEC_WORST_GROUP:
		filter->worst_group = g_value_get_int(value);
		break;

	case ARG_DEC_EXTRA_ERRORS:
		filter->extra_errors = g_value_get_int(value);
		break;
	}
}


static void dec_get_property(GObject *object, enum property id, GValue *value, GParamSpec *pspec)
{
	BkrECC2Dec *filter = BKR_ECC2DEC(object);

	switch(id) {
	case ARG_DEC_WORST_GROUP:
		g_value_set_int(value, filter->worst_group);
		break;

	case ARG_DEC_EXTRA_ERRORS:
		g_value_set_int(value, filter->extra_errors);
		break;
	}
}


/*
 * Sink pad setcaps function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadBufferAllocFunction
 */


static void reset_statistics(BkrECC2Dec *filter)
{
	filter->worst_group = 0;
	filter->extra_errors = 0;
}


static gboolean dec_setcaps(GstPad *pad, GstCaps *caps)
{
	BkrECC2Dec *filter = BKR_ECC2DEC(gst_pad_get_parent(pad));
	gboolean result;

	gst_adapter_clear(filter->adapter);

	filter->sector_number = 0;
	reset_statistics(filter);

	reed_solomon_codec_free(filter->rs_format);

	free(filter->erasure);
	filter->erasure = NULL;
	filter->num_erasure = 0;

	free(filter->format);
	filter->format = caps_to_format(caps);

	if(filter->format) {
		filter->rs_format = reed_solomon_codec_new((filter->format->data_size + filter->format->parity_size) / filter->format->interleave, filter->format->data_size / filter->format->interleave, filter->format->interleave);
		filter->erasure = malloc(PARITY * sizeof(*filter->erasure));
		if(!filter->rs_format || !filter->erasure) {
			GST_DEBUG("reed_solomon_codec_new() or malloc() failed");
			free(filter->format);
			filter->format = NULL;
			reed_solomon_codec_free(filter->rs_format);
			filter->rs_format = NULL;
			free(filter->erasure);
			filter->erasure = NULL;
		}
	}

	result = filter->format ? TRUE : FALSE;

	gst_object_unref(filter);

	return result;
}


/*
 * Event function.  See
 *
 * file:///usr/share/doc/gstreamer0.10-doc/gstreamer-0.10/GstPad.html#GstPadEventFunction
 */


static gboolean dec_event(GstPad *pad, GstEvent *event)
{
	BkrECC2Dec *filter = BKR_ECC2DEC(gst_pad_get_parent(pad));
	GstBuffer *zero_padding;
	gboolean result;

	switch(GST_EVENT_TYPE(event)) {
	case GST_EVENT_CUSTOM_DOWNSTREAM:
		switch(bkr_event_parse(event)) {
		case BKR_EVENT_SKIPPED_SECTOR:
			zero_padding = gst_buffer_new_and_alloc(filter->format->interleave);
			memset(GST_BUFFER_DATA(zero_padding), 0, GST_BUFFER_SIZE(zero_padding));
			gst_adapter_push(filter->adapter, zero_padding);
			filter->erasure[filter->num_erasure++] = filter->sector_number++;
			result = TRUE;
			break;

		case BKR_EVENT_NEXT_SECTOR_INVALID:
			filter->erasure[filter->num_erasure++] = filter->sector_number;
			result = TRUE;
			break;

		default:
			/* not one of our custom events, pass it along */
			result = gst_pad_push_event(filter->srcpad, event);
			break;
		}
		break;

	default:
		result = gst_pad_event_default(pad, event);
		break;
	}

	gst_object_unref(filter);
	return result;
}


/*
 * Chain function.  See
 *
 * file:///usr/share/doc/gstreamer0.8-doc/gstreamer-0.8/GstPad.html#GstPadChainFunction
 */


static GstFlowReturn dec_chain(GstPad *pad, GstBuffer *sinkbuf)
{
	BkrECC2Dec *filter = BKR_ECC2DEC(gst_pad_get_parent(pad));
	GstCaps *caps = gst_buffer_get_caps(sinkbuf);
	GstPad *srcpad = filter->srcpad;
	GstFlowReturn result;

	if(!caps || (caps != GST_PAD_CAPS(pad))) {
		if(!caps)
			GST_DEBUG("caps not set on buffer");
		else if(caps != GST_PAD_CAPS(pad))
			GST_DEBUG("buffer's caps don't match pad's caps");
		result = GST_FLOW_NOT_NEGOTIATED;
		goto done;
	}

	if(GST_BUFFER_SIZE(sinkbuf) != filter->format->interleave) {
		GST_ELEMENT_ERROR(filter, STREAM, FAILED, ("recieved incorrect buffer size, got %d bytes expected %d bytes", GST_BUFFER_SIZE(sinkbuf), filter->format->interleave), (NULL));
		gst_buffer_unref(sinkbuf);
		result = GST_FLOW_ERROR;
		goto done;
	}

	gst_adapter_push(filter->adapter, sinkbuf);

	filter->sector_number++;

	if(gst_adapter_available(filter->adapter) >= filter->format->group_size) {
		GstBuffer *srcbuf;
		result = decode_group(filter, &srcbuf);
		filter->sector_number = 0;
		if(result != GST_FLOW_OK) {
			GST_DEBUG("decode_group() failed");
			goto done;
		}
		gst_buffer_set_caps(srcbuf, caps);
		result = gst_pad_push(srcpad, srcbuf);
		if(result != GST_FLOW_OK) {
			GST_DEBUG("gst_pad_push() failed");
			goto done;
		}
	}

	result = GST_FLOW_OK;

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
	BkrECC2Dec *filter = BKR_ECC2DEC(object);

	g_object_unref(filter->adapter);
	filter->adapter = NULL;
	gst_object_unref(filter->srcpad);
	filter->srcpad = NULL;
	reed_solomon_codec_free(filter->rs_format);
	filter->rs_format = NULL;
	free(filter->erasure);
	filter->erasure = NULL;
	free(filter->format);
	filter->format = NULL;

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
		"Backer ECC2 Decoder",
		"Filter",
		"Backer sector drop-out error correction decoder",
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

	object_class->set_property = dec_set_property;
	object_class->get_property = dec_get_property;
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
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	g_object_class_install_property(object_class, ARG_DEC_WORST_GROUP, g_param_spec_int("worst_group", "Worst group", "Worst group", 0, INT_MAX, 0, G_PARAM_READWRITE));
	g_object_class_install_property(object_class, ARG_DEC_EXTRA_ERRORS, g_param_spec_int("extra_errors", "Extra errors", "Extra errors", 0, INT_MAX, 0, G_PARAM_READWRITE));

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
	BkrECC2Dec *filter = BKR_ECC2DEC(object);
	GstPad *pad;

	gst_element_create_all_pads(element);

	/* configure sink pad */
	pad = gst_element_get_static_pad(element, "sink");
	gst_pad_set_setcaps_function(pad, dec_setcaps);
	gst_pad_set_event_function(pad, dec_event);
	gst_pad_set_chain_function(pad, dec_chain);
	gst_object_unref(pad);

	/* configure src pad */
	pad = gst_element_get_static_pad(element, "src");

	/* consider this to consume the reference */
	filter->srcpad = pad;

	/* internal state */
	filter->adapter = gst_adapter_new();
	filter->rs_format = NULL;
	filter->format = NULL;
	filter->erasure = NULL;
	filter->num_erasure = 0;
	filter->sector_number = 0;
	reset_statistics(filter);
}


/*
 * bkr_ecc2dec_get_type().
 */


GType bkr_ecc2dec_get_type(void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			.class_size = sizeof(BkrECC2DecClass),
			.class_init = dec_class_init,
			.base_init = dec_base_init,
			.instance_size = sizeof(BkrECC2Dec),
			.instance_init = dec_instance_init,
		};
		type = g_type_register_static(GST_TYPE_ELEMENT, "BkrECC2Dec", &info, 0);
	}
	return type;
}
