#
# Some things to set for your system
#
#    DRIVER_DIR : location of driver sources (no trailing slash).
#    DEVICE_NAME : file to use as default device.
#    PREFIX : top of installation hierarchy (no trailing slahs).
#

DRIVER_DIR = ../backer-driv-1.107
DEVICE_NAME = /dev/tape
PREFIX = /usr/local

#
# Here we go...
#

BIN_DIR = $(PREFIX)/bin
MAN_DIR = $(PREFIX)/man
CC = gcc
CFLAGS = -O3 -Wall -Wstrict-prototypes -I$(DRIVER_DIR) -DDEFAULT_DEVICE=\"$(DEVICE_NAME)\"

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

install: all
	install -d -m 0755 $(BIN_DIR)
	install -d -m 0755 $(MAN_DIR)/man8
	install -m 0755 bkrcheck $(BIN_DIR)
	install -m 0755 bkrencode $(BIN_DIR)
	install -m 0755 bkrmonitor $(BIN_DIR)
	[ -x bkrcheck ]   && install -m 0644 bkrcheck.8 $(MAN_DIR)/man8
	[ -x bkrencode ]  && install -m 0644 bkrencode.8 $(MAN_DIR)/man8
	[ -x bkrmonitor ] && install -m 0644 bkrmonitor.8 $(MAN_DIR)/man8

uninstall:
	rm -f $(BIN_DIR)/bkrcheck
	rm -f $(BIN_DIR)/bkrencode
	rm -f $(BIN_DIR)/bkrmonitor
	rm -f $(MAN_DIR)/man8/bkrcheck.8
	rm -f $(MAN_DIR)/man8/bkrencode.8
	rm -f $(MAN_DIR)/man8/bkrmonitor.8

clean:
	rm -f *.o core

realclean: clean
	rm -f bkrcheck bkrencode bkrmonitor
