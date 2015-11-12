AM_CPPFLAGS = -I$(top_srcdir)/drivers -I$(top_srcdir)/codecs

dist_bin_SCRIPTS = bkrvideo

bin_PROGRAMS = bkrencode bkrcheck

bkrcheck_SOURCES = bkrcheck.c bkr_disp_mode.h bkr_disp_mode.c bkr_puts.h bkr_puts.c bkr_font.xpm $(top_srcdir)/drivers/backer.h $(top_srcdir)/codecs/bkr_splp_randomize.h $(top_srcdir)/codecs/bkr_splp_randomize.c
bkrcheck_CFLAGS = $(AM_CFLAGS) $(gstreamer_CFLAGS)

bkrencode_SOURCES = bkrencode.c bkr_disp_mode.h bkr_disp_mode.c $(top_srcdir)/drivers/backer.h
bkrencode_CFLAGS = $(AM_CFLAGS) $(gstreamer_CFLAGS)
bkrencode_LDADD = $(gstreamer_LIBS)
bkrencode_LDFLAGS = -L$(top_srcdir)/codecs/

#if COND_GTK
#bin_PROGRAMS += bkrmonitor
#bkrmonitor_SOURCES = bkrmonitor.c bkr_proc_io.h bkr_proc_io.c
#bkrmonitor_CFLAGS = $(AM_CFLAGS) $(GTK_CFLAGS)
#bkrmonitor_LDADD = $(GTK_LIBS)
#bkrmonitor_LDFLAGS = $(GTK_LDFLAGS)
#endif

benchmark : all
	@echo "Testing 512MB..."
	@echo -n "Encoding baseline:"
	@time dd bs=1048576 count=512 </dev/zero 2>/dev/null >/dev/null
	@echo
	@echo -n "Encoding time:"
	@time dd bs=1048576 count=512 </dev/zero 2>/dev/null | ./bkrencode -Vn -Dh -Fs >/dev/null
	@echo
	@echo -n "Decoding baseline:"
	@time dd bs=1048576 count=512 </dev/zero 2>/dev/null | ./bkrencode -Vn -Dh -Fs | ../diagnostics/bkrnoise -Vn -Dh -Fs >/dev/null
	@echo
	@echo -n "Decoding time: "
	@time dd bs=1048576 count=512 </dev/zero 2>/dev/null | ./bkrencode -Vn -Dh -Fs | ../diagnostics/bkrnoise -Vn -Dh -Fs | ./bkrencode -u -Vn -Dh -Fs >/dev/null
