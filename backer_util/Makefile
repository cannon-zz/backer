#
# Set this to the directory containing the driver sources and
# be sure to leave off the trailing slash
#
DRIVERDIR = ../backer-driv-1.0

CFLAGS = -O2 -Wall -Wstrict-prototypes -I$(DRIVERDIR)

all : bkrencode bkrlog bkrmode bkrmonitor bkrtest

bkrencode : bkrencode.c bkr_disp_mode.c $(DRIVERDIR)/backer_fmt.c      \
            $(DRIVERDIR)/rs.c $(DRIVERDIR)/rs.h $(DRIVERDIR)/backer.h  \
            $(DRIVERDIR)/backer_device.h $(DRIVERDIR)/backer_fmt.h
	gcc $(CFLAGS) -o bkrencode bkrencode.c bkr_disp_mode.c         \
	    $(DRIVERDIR)/backer_fmt.c $(DRIVERDIR)/rs.c

bkrlog : bkrlog.c $(DRIVERDIR)/backer.h bkr_aux_puts.c bkrfont.xpm
	gcc $(CFLAGS) -o bkrlog bkrlog.c bkr_aux_puts.c

bkrmode : bkrmode.c bkr_disp_mode.c $(DRIVERDIR)/backer.h
	gcc $(CFLAGS) -o bkrmode bkrmode.c bkr_disp_mode.c

bkrmonitor : bkrmonitor.c $(DRIVERDIR)/backer.h
	gcc $(CFLAGS) $(shell gtk-config --cflags --libs) -o bkrmonitor \
	    bkrmonitor.c

bkrtest : bkrtest.c $(DRIVERDIR)/backer.h bkr_aux_puts.c bkr_disp_mode.c  \
          bkrfont.xpm
	gcc $(CFLAGS) -o bkrtest bkrtest.c bkr_aux_puts.c bkr_disp_mode.c

clean:
	rm -f *.o core

realclean: clean
	rm -f bkrencode bkrlog bkrmode bkrmonitor bkrtest
