/* 
 */

#ifndef __BKR_FRAME_H__
#define __BKR_FRAME_H__

#include <gst/gst.h>
#include <gst/bytestream/adapter.h>
#include <backer.h>

G_BEGIN_DECLS

/*
 * Format information.
 * 	interlace = number of extra bytes in an odd field
 */

struct bkr_frame_format {
	gint field_size;
	gint interlace;
	gint leader;
	gint trailer;
	gint active_size;
	gint key_interval;
	gint key_length;
};


/*
 * Encoder
 */

#define BKR_FRAMEENC_TYPE			(bkr_frameenc_get_type())
#define BKR_FRAMEENC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_FRAMEENC_TYPE, BkrFrameEnc))
#define BKR_FRAMEENC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_FRAMEENC_TYPE, BkrFrameEnc))
#define GST_IS_BKR_FRAMEENC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_FRAMEENC_TYPE))
#define GST_IS_BKR_FRAMEENC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_FRAMEENC_TYPE))

typedef struct _BkrFrameEncClass BkrFrameEncClass;
typedef struct _BkrFrameEnc BkrFrameEnc;

struct _BkrFrameEncClass {
	GstElementClass parent_class;
};

struct _BkrFrameEnc {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	enum bkr_vidmode vidmode;
	enum bkr_density density;
	enum bkr_format fmt;
	gint odd_field;
	struct bkr_frame_format format;
};

GType bkr_frameenc_get_type(void);


/*
 * Decoder
 */

#define BKR_FRAMEDEC_TYPE			(bkr_framedec_get_type())
#define BKR_FRAMEDEC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_FRAMEDEC_TYPE, BkrFrameDec))
#define BKR_FRAMEDEC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_FRAMEDEC_TYPE, BkrFrameDec))
#define GST_IS_BKR_FRAMEDEC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_FRAMEDEC_TYPE))
#define GST_IS_BKR_FRAMEDEC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_FRAMEDEC_TYPE))

typedef struct _BkrFrameDecClass BkrFrameDecClass;
typedef struct _BkrFrameDec BkrFrameDec;

struct _BkrFrameDecClass {
	GstElementClass parent_class;
};

struct _BkrFrameDec {
	GstElement element;

	GstPad *sinkpad, *srcpad;
	GstAdapter *adapter;

	enum bkr_vidmode vidmode;
	enum bkr_density density;
	enum bkr_format fmt;
	struct bkr_frame_format format;
};

GType bkr_framedec_get_type(void);

G_END_DECLS
#endif				/* __BKR_FRAME_H__ */
