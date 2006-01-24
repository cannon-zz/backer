INCLUDES = -I../drivers -I../codecs

bin_PROGRAMS = bkrencode

bkrencode_SOURCES = bkrencode.c bkr_disp_mode.c
bkrencode_CFLAGS = $(GST_CFLAGS)
bkrencode_LDADD = $(GST_LIBS) -ltapefile
bkrencode_LDFLAGS = $(GST_LDFLAGS) -L../codecs/

#if COND_GTK
#bin_PROGRAMS += bkrmonitor
#bkrmonitor_SOURCES = bkrmonitor.c bkr_proc_io.c
#bkrmonitor_CFLAGS = $(GTK_CFLAGS)
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
