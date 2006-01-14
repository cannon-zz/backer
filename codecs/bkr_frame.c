/*
 */

#include <string.h>

#include <gst/gst.h>
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

static const unsigned char  sector_key[] = {
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

static guint correlate(const guchar *data, gint key_interval, gint key_length, const guchar *key)
{
	guint count = 0;

	while(key_length--) {
		count += *data == *(key++);
		data += key_interval;
	}

	return(count);
}


/*
 * Uses correlate() to scan the data stream until a sector key sequence is
 * found.  On success, the buffer tail is left pointing to the first byte
 * following the sector leader and the return value is non-negative;
 * otherwise the return value is non-positive (0 == EOF on stream source).
 */

#if 0
static int find_field(struct bkr_stream_t *stream, const unsigned char *key)
{
	struct bkr_stream_t  *source = stream->source;
	bkr_frame_private_t  *private = stream->private;
	int  threshold = stream->fmt.key_length * FRAME_THRESHOLD_A / FRAME_THRESHOLD_B;
	int  result;

	while(1) {
		result = source->ops.read(source);
		if(result <= 0)
			return(result);
		if(result < stream->fmt.active_size)
			return(-EAGAIN);

		ring_lock(source->ring);
		result = correlate(*source->ring, stream->fmt.key_interval, stream->fmt.key_length, key);
		if(result >= threshold) {
			ring_unlock(source->ring);
			break;
		}
		if(result > private->best_nonkey)
			private->best_nonkey = result;
		_ring_drain(source->ring, 1);
		ring_unlock(source->ring);
	}

	if(result < private->worst_key)
		private->worst_key = result;

	if(private->last_field_offset >= 0) {
		result = ring_offset_sub(source->ring, source->ring->tail, private->last_field_offset);
		if(result < private->smallest_field)
			private->smallest_field = result;
		else if(result > private->largest_field)
			private->largest_field = result;
		if(result*4 > stream->source->capacity*3)
			private->frame_warnings++;
	}
	private->last_field_offset = source->ring->tail;

	return(1);
}
#endif


/*
 * Strips the sector data from a video field in the source buffer and
 * places it in the destination buffer.
 */

static void decode_field(struct bkr_frame_format fmt, guchar *dst, const guchar *src)
{
	int  i;

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

static void encode_field(struct bkr_frame_format fmt, guchar *dst, const guchar *src, const guchar *key, gint field_number)
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

static void enc_chain(GstPad *pad, GstData *in)
{
	BkrFrameEnc *filter = BKR_FRAMEENC(GST_OBJECT_PARENT(pad));
	GstBuffer *inbuf = GST_BUFFER(in);
	GstBuffer *outbuf = gst_buffer_new_and_alloc(2 * filter->format.field_size + filter->format.interlace);
	guchar *oddsrc, *evensrc;
	guchar *odddst, *evendst;

	g_return_if_fail((inbuf != NULL) && (outbuf != NULL));

	oddsrc = GST_BUFFER_DATA(inbuf);
	evensrc = oddsrc + filter->format.active_size;

	odddst = GST_BUFFER_DATA(outbuf);
	evendst = odddst + filter->format.field_size + filter->format.interlace;

	if(GST_BUFFER_SIZE(inbuf) >= 2 * filter->format.active_size) {
		encode_field(filter->format, odddst, oddsrc, sector_key, 1);	/* Odd field */
		encode_field(filter->format, evendst, evensrc, sector_key, 2);	/* Even field */
	}
	gst_buffer_unref(in);

	gst_pad_push(filter->srcpad, GST_DATA(outbuf));
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

	gst_buffer_unref(in);
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

