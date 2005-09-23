#
# Set this to the directory containing the driver sources and
# be sure to leave off the trailing slash
#
DRIVER_DIR = ../backer-driv-1.106

#
# Set this to your prefered device
#
DEVICE_NAME = /dev/tape

#
#
#

CC = gcc
CFLAGS = -O3 -fstrict-aliasing -Wall -Wstrict-prototypes -I$(DRIVER_DIR) -DDEFAULT_DEVICE=\"$(DEVICE_NAME)\"

all : bkrcheck bkrencode bkrmonitor

bkrcheck : bkrcheck.c $(DRIVER_DIR)/backer.h bkr_disp_mode.c
	$(CC) $(CFLAGS) -lm -o bkrcheck bkrcheck.c bkr_disp_mode.c

bkrencode : bkrencode.c bkr_disp_mode.c stdio_dev.c \
            $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c $(DRIVER_DIR)/rs.h \
            $(DRIVER_DIR)/backer.h $(DRIVER_DIR)/backer_device.h \
            $(DRIVER_DIR)/backer_fmt.h
	$(CC) $(CFLAGS) -o bkrencode bkrencode.c bkr_disp_mode.c stdio_dev.c \
	    $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c

bkrmonitor : bkrmonitor.c $(DRIVER_DIR)/backer.h
	$(CC) $(CFLAGS) $(shell gtk-config --cflags --libs) -o bkrmonitor \
	    bkrmonitor.c

clean:
	rm -f *.o core

realclean: clean
	rm -f bkrcheck bkrencode bkrmonitor
