plugin_LTLIBRARIES = libtapefile.la

INCLUDES = -I$(top_srcdir)/drivers

libtapefile_la_SOURCES = bkr_elements.c bkr_frame.c bkr_video_out.c
libtapefile_la_CFLAGS = $(GST_CFLAGS)
libtapefile_la_LIBADD = $(GST_LIBS)
libtapefile_la_LDFLAGS = $(GST_LDFLAGS)

noinst_HEADERS = bkr_frame.h bkr_video_out.h
