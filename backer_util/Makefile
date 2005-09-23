#
# Set this to the directory containing the driver sources and
# be sure to leave off the trailing slash
#
DRIVER_DIR = ../backer-driv-1.102

#
# Set this to your dev directory entry
#
DEVICE_NAME = /dev/backer

#
#
#

CFLAGS = -O2 -Wall -Wstrict-prototypes -I$(DRIVER_DIR) -DDEFAULT_DEVICE=\"$(DEVICE_NAME)\"

all : bkrcheck bkrencode bkrmode bkrmonitor

bkrcheck : bkrcheck.c $(DRIVER_DIR)/backer.h bkr_disp_mode.c
	gcc $(CFLAGS) -lm -o bkrcheck bkrcheck.c bkr_disp_mode.c

bkrencode : bkrencode.c bkr_disp_mode.c $(DRIVER_DIR)/backer_fmt.c      \
            $(DRIVER_DIR)/rs.c $(DRIVER_DIR)/rs.h $(DRIVER_DIR)/backer.h  \
            $(DRIVER_DIR)/backer_device.h $(DRIVER_DIR)/backer_fmt.h
	gcc $(CFLAGS) -o bkrencode bkrencode.c bkr_disp_mode.c         \
	    $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c

bkrmode : bkrmode.c bkr_disp_mode.c $(DRIVER_DIR)/backer.h
	gcc $(CFLAGS) -o bkrmode bkrmode.c bkr_disp_mode.c

bkrmonitor : bkrmonitor.c $(DRIVER_DIR)/backer.h
	gcc $(CFLAGS) $(shell gtk-config --cflags --libs) -o bkrmonitor \
	    bkrmonitor.c

clean:
	rm -f *.o core

realclean: clean
	rm -f bkrcheck bkrencode bkrmode bkrmonitor
