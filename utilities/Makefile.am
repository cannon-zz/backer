SUBDIRS = DOCUMENTATION

INCLUDES = -I../drivers -I../codecs

bin_PROGRAMS = bkrcheck bkrencode

bkrcheck_SOURCES = bkrcheck.c bkr_disp_mode.c bkr_puts.c bkr_proc_io.c ../codecs/bkr_splp_randomize.c

bkrencode_SOURCES = bkrencode.c bkr_disp_mode.c bkr_proc_io.c bkr_stdio_dev.c ../drivers/bkr_stream.c ../drivers/bkr_ring_buffer.c ../codecs/bkr_frame.c ../codecs/bkr_gcr.c ../codecs/bkr_splp.c ../codecs/bkr_splp_randomize.c ../codecs/bkr_ecc2.c ../codecs/rs.c
bkrencode_LDADD = pthread

bkrmonitor_SOURCES = bkrmonitor.c bkr_proc_io.c

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
