/* 
 */


#ifndef __BKR_VIDEO_OUT_H__
#define __BKR_VIDEO_OUT_H__


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasetransform.h>
#include <backer.h>


G_BEGIN_DECLS


#define BKR_VIDEO_OUT_TYPE			(bkr_video_out_get_type())
#define BKR_VIDEO_OUT(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), BKR_VIDEO_OUT_TYPE, BkrVideoOut))
#define BKR_VIDEO_OUT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), BKR_VIDEO_OUT_TYPE, BkrVideoOutClass))
#define GST_IS_BKR_VIDEO_OUT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), BKR_VIDEO_OUT_TYPE))
#define GST_IS_BKR_VIDEO_OUT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), BKR_VIDEO_OUT_TYPE))


typedef struct {
	GstBaseTransformClass parent_class;
} BkrVideoOutClass;


typedef struct {
	GstBaseTransform parent;

	GstAdapter *adapter;

	/*
	 * next field number (first field is number 1, an odd field)
	 */

	gint field_number;

	/*
	 * Format information.
	 */

	enum bkr_videomode videomode;
	enum bkr_bitdensity bitdensity;

	struct bkr_video_out_format {
		gint gst_seconds_per_field_a;	/* numerator of GST_SECOND / (fields/second) */
		gint gst_seconds_per_field_b;	/* denominator of GST_SECOND / (fields/second) */
		gint bytes_per_line;	/* data consumed per video line */
		gint interlace;	/* how many extra lines in odd fields */
		gint width;	/* frame width in pixels */
		gint height;	/* frame height in lines not including interlace */
		guint32 *(*byte_func)(guint32 *, guint8);
	} format;
} BkrVideoOut;


GType bkr_video_out_get_type(void);


G_END_DECLS


#endif	/* __BKR_VIDEO_OUT_H__ */
