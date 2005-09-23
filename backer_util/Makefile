#
# Set this to the directory containing the driver sources and
# be sure to leave off the trailing slash
#
DRIVERDIR = ../backer-driv-1.101

CFLAGS = -O2 -Wall -Wstrict-prototypes -I$(DRIVERDIR)

all : bkrcheck bkrencode bkrmode bkrmonitor

bkrcheck : bkrcheck.c $(DRIVERDIR)/backer.h bkr_disp_mode.c
	gcc $(CFLAGS) -lm -o bkrcheck bkrcheck.c bkr_disp_mode.c

bkrencode : bkrencode.c bkr_disp_mode.c $(DRIVERDIR)/backer_fmt.c      \
            $(DRIVERDIR)/rs.c $(DRIVERDIR)/rs.h $(DRIVERDIR)/backer.h  \
            $(DRIVERDIR)/backer_device.h $(DRIVERDIR)/backer_fmt.h
	gcc $(CFLAGS) -o bkrencode bkrencode.c bkr_disp_mode.c         \
	    $(DRIVERDIR)/backer_fmt.c $(DRIVERDIR)/rs.c

bkrmode : bkrmode.c bkr_disp_mode.c $(DRIVERDIR)/backer.h
	gcc $(CFLAGS) -o bkrmode bkrmode.c bkr_disp_mode.c

bkrmonitor : bkrmonitor.c $(DRIVERDIR)/backer.h
	gcc $(CFLAGS) $(shell gtk-config --cflags --libs) -o bkrmonitor \
	    bkrmonitor.c

clean:
	rm -f *.o core

realclean: clean
	rm -f bkrcheck bkrencode bkrmode bkrmonitor
