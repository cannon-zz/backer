#
# Some things to set for your system
#
#    DRIVER_DIR : location of driver sources.
#    DEVICE_NAME : file to use as default device.
#    PREFIX : top of installation hierarchy.
#

DRIVER_DIR = ../backer-driv-2.0
DEVICE_NAME = /dev/tape
PREFIX = /usr/local

#
# Here we go...
#
# If building a package, DESTDIR can be set if desired.
#

DESTDIR =
BIN_DIR = $(DESTDIR)/$(PREFIX)/bin
MAN_DIR = $(DESTDIR)/$(PREFIX)/man

TARGETS = bkrcheck bkrencode bkrmonitor
MAN_PAGES = $(TARGETS:%=%.8)

CFLAGS = -O3 -Wall -Wstrict-prototypes -I$(DRIVER_DIR) -DDEFAULT_DEVICE=\"$(DEVICE_NAME)\"

GTK_CONFIG = $(shell which gtk-config)

.PHONY : all install uninstall clean realclean

all : $(TARGETS) $(MAN_PAGES)
	@echo "Done."

bkrcheck : bkrcheck.c bkr_disp_mode.c $(DRIVER_DIR)/backer.h
	$(CC) $(CFLAGS) -lm -o $@ bkrcheck.c bkr_disp_mode.c

bkrencode : bkrencode.c bkr_disp_mode.c stdio_dev.c $(DRIVER_DIR)/backer_fmt.c \
            $(DRIVER_DIR)/rs.c bkr_disp_mode.h $(DRIVER_DIR)/rs.h \
            $(DRIVER_DIR)/backer.h $(DRIVER_DIR)/backer_device.h \
            $(DRIVER_DIR)/backer_fmt.h
	$(CC) $(CFLAGS) -o $@ bkrencode.c bkr_disp_mode.c stdio_dev.c \
	   $(DRIVER_DIR)/backer_fmt.c $(DRIVER_DIR)/rs.c

bkrmonitor : bkrmonitor.c $(DRIVER_DIR)/backer.h
ifneq ($(GTK_CONFIG),)
	$(CC) $(CFLAGS) -DGTK_CONFIG $(shell $(GTK_CONFIG) --cflags --libs) -o $@ \
	    bkrmonitor.c
else
	@echo
	@echo "ATTENTION:  GTK+ not found:  building bkrmonitor as text-only."
	@echo "Ignore \"unused variable\" warnings."
	@echo
	$(CC) $(CFLAGS) -o $@ bkrmonitor.c
endif

%.8 : %.8.orig Makefile
	sed -e "s?DEVICE_NAME?${DEVICE_NAME}?" < $< > $@

install: all
	install -d -m 0755 $(BIN_DIR)
	install -d -m 0755 $(MAN_DIR)/man8
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
