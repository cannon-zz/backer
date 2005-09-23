#ifndef _BKR_STREAM_H
#define _BKR_STREAM_H

#include <bkr_ring_buffer.h>
#include <backer.h>


#define BKR_FILLER 0x33         /* Generic filler */


typedef enum {
	BKR_STOPPED = 0,
	BKR_READING,
	BKR_WRITING
} bkr_direction_t;


struct bkr_stream_t;

struct bkr_stream_ops_t {
	struct bkr_stream_t  *(*new)(struct bkr_stream_t *, int, const bkr_format_info_t *);
	int  (*start)(struct bkr_stream_t *, bkr_direction_t);
	int  (*release)(struct bkr_stream_t *);
	int  (*read)(struct bkr_stream_t *);
	int  (*write)(struct bkr_stream_t *);
};


struct bkr_stream_t {
	struct bkr_stream_t  *source;   /* the stream from which this flows */
	struct ring  *ring;             /* this stream's I/O ring */
	bkr_format_info_t  fmt;         /* stream format paramters */
	struct bkr_stream_ops_t  ops;   /* stream control functions */
	int  mode;                      /* stream settings */
	volatile bkr_direction_t  direction;     /* stream state */
	void  (*callback)(void *);      /* I/O activity call-back */
	void  *callback_data;           /* call-back data */
	unsigned int  timeout;          /* I/O activity timeout */
	int  capacity;                  /* sector capacity */
	void  *private;                 /* per-stream private data */
};


static void bkr_stream_set_callback(struct bkr_stream_t *stream, void (*callback)(void *), void *data)
{
	stream->callback_data = data;
	stream->callback = callback;
}

static void bkr_stream_do_callback(struct bkr_stream_t *stream)
{
	if(stream->callback)
		stream->callback(stream->callback_data);
}

int bkr_stream_bytes(struct bkr_stream_t *);
int bkr_stream_size(struct bkr_stream_t *);
int bkr_source_read_status(struct bkr_stream_t *);
int bkr_source_write_status(struct bkr_stream_t *);
int bkr_simple_stream_read(struct bkr_stream_t *);
int bkr_simple_stream_write(struct bkr_stream_t *);
int bkr_stream_fill_to(struct bkr_stream_t *, int, unsigned char);

#endif /* _BKR_STREAM_H */
