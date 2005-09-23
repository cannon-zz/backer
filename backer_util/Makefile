#
# Some things to set for your system
#
#    DRIVER_DIR   : location of driver sources.
#    PREFIX       : top of installation hierarchy.
#    DEVICE_NAME  : file to use as default device.
#    DEFAULT_MODE : default mode for bkrencode.
#
# See $(DRIVER_DIR)/backer.h for the constants to use for DEFAULT_MODE.
# You must select exactly one from each "category" and you must not use
# BKR_RAW for the format selection.
#

DRIVER_DIR   = ../backer-driv-2.1
PREFIX       = /usr/local
DEVICE_NAME  = /dev/tape
DEFAULT_MODE = (BKR_NTSC | BKR_LOW | BKR_SP)

#
# Here we go...
#

BIN_DIR = $(PREFIX)/bin
MAN_DIR = $(PREFIX)/man
BKRFLAGS = -DDEFAULT_DEVICE=\"$(DEVICE_NAME)\" -DDEFAULT_MODE="$(DEFAULT_MODE)"
CFLAGS  = -O3 -Wall -Wstrict-prototypes -I$(DRIVER_DIR)
GTK_CONFIG  = $(shell which gtk-config)
TODAYS_DATE = $(shell date +"%B %d, %Y")

TARGETS   = bkrcheck bkrencode bkrmonitor
MAN_PAGES = $(TARGETS:%=%.8)

.PHONY : all install uninstall clean realclean

all : $(TARGETS) $(MAN_PAGES)
	@echo "Done."

bkrcheck : bkrcheck.c bkr_disp_mode.c $(DRIVER_DIR)/backer.h Makefile
	$(CC) $(CFLAGS) $(BKRFLAGS) -lm -o $@ bkrcheck.c bkr_disp_mode.c

bkrencode : bkrencode.c bkr_disp_mode.c stdio_dev.c \
            $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c bkr_disp_mode.h \
            $(DRIVER_DIR)/rs.h $(DRIVER_DIR)/backer.h \
            $(DRIVER_DIR)/backer_device.h $(DRIVER_DIR)/backer_fmt.h \
            Makefile
	$(CC) $(CFLAGS) $(BKRFLAGS) -o $@ bkrencode.c bkr_disp_mode.c \
	    stdio_dev.c $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c

bkrmonitor : bkrmonitor.c $(DRIVER_DIR)/backer.h Makefile
ifneq ($(GTK_CONFIG),)
	$(CC) $(CFLAGS) $(BKRFLAGS) $(shell $(GTK_CONFIG) --cflags --libs) \
	    -o $@ bkrmonitor.c
else
	@echo
	@echo "ATTENTION:  GTK+ not found:  bkrmonitor will not be built."
	@echo
endif

%.8 : %.man Makefile
	sed -e "s?DEFAULT_MODE?${DEFAULT_MODE}?" -e "s?DEVICE_NAME?${DEVICE_NAME}?" -e "s?TODAYS_DATE?$(TODAYS_DATE)?" < $< > $@

install: all
	mkdir -p -m 0755 $(BIN_DIR)
	mkdir -p -m 0755 $(MAN_DIR)/man8
	for x in $(TARGETS); do \
	   install -m 0755 $${x} $(BIN_DIR) && install -m 0644 $${x}.8 $(MAN_DIR)/man8; \
	done
	@echo "Done."

uninstall:
	for x in $(TARGETS); do \
	   { \
	   rm -f $(BIN_DIR)/$${x}; \
	   rm -f $(MAN_DIR)/man8/$${x}.8; \
	   } \
	done
	@echo "Done."

clean:
	rm -f *.o core

realclean: clean
	rm -f $(TARGETS) $(MAN_PAGES)
