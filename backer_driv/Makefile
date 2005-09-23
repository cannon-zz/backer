#
# Some things to set for your system
#
#    BKR_MAJOR : the major number for the /dev files.
#    MODULES_DIR : install directory for the driver (no trailing slash).
#    PREFIX : prefix for the install hierarchy (no trailing slash).
#

BKR_MAJOR = 60
MODULES_DIR = /lib/modules/misc
PREFIX = /usr

#
# Here we go...
#
# The targets you should know about are:
#
#   all: (default)
#	build the driver, backer.o.
#   install: 
#	copy the driver to MODULES_DIR, run depmod -a, generate device
#	inodes if devfs is known to not be in use, put backer.4 in
#	$(PREFIX)/man/man4.
#   devs:
#	force creation of device inodes.
#   uninstall:
#	delete backer.o from /lib/modules/misc, run depmod -a, delete the /dev
#	entries if they exist, delete backer.4 from $(PREFIX)/man/man4.
#   clean:
#	Delete all intermediate files from the build directory.
#   realclean:
#	Same as clean plus all build targets are also deleted.
#

INSMODFLAGS = dma=1
INCLUDEDIR = /usr/include
CC = gcc
CFLAGS = -D__KERNEL__ -DMODULE -DBKR_MAJOR=$(BKR_MAJOR) -O6 -Wall -Wstrict-prototypes -I$(INCLUDEDIR)
LDFLAGS = -r

VER = $(shell awk -F\" '/REL/ {print $$2}' $(INCLUDEDIR)/linux/version.h)

SOURCES = backer_fmt.c backer_isa.c rs.c
OBJECTS = $(SOURCES:%.c=%.o)
TARGET = backer.o

all : $(TARGET)

$(TARGET) : $(OBJECTS)
	$(LD) $(LDFLAGS) $^ -o $@

backer_fmt.o : backer_fmt.c backer.h backer_device.h backer_fmt.h rs.h
backer_isa.o : backer_isa.c backer.h backer_device.h backer_fmt.h
rs.o :  rs.c rs.h

install:
	install -d -m 0755 $(MODULES_DIR)
	install -m 0644 backer.o $(MODULES_DIR)
	@echo "Doing depmod:  ignore \"Unresolved symbols\" warnings."
	-depmod -a
	@if [ -f /proc/mounts ]; then \
	   if grep devfs -q /proc/mounts; then \
	      echo "ATTENTION:  devfs in use:  device inode creation not required."; \
	   else \
	      echo "devfs not in use:  creating device inodes..."; \
	      make devs; \
	   fi \
	else \
	   echo "WARNING:  devfs status unknown:  device inode creation skipped."; \
	   echo "You must manually run \"make devs\" if device inodes are required."; \
	fi
	install -d -m 0755 $(PREFIX)/man/man4
	install -m 0644 backer.4 $(PREFIX)/man/man4
	@echo "Done."

devs:
	mknod -m 0666 /dev/backernhr c $(BKR_MAJOR) 0
	mknod -m 0666 /dev/backernhs c $(BKR_MAJOR) 1
	mknod -m 0666 /dev/backernls c $(BKR_MAJOR) 2
	mknod -m 0666 /dev/backernlr c $(BKR_MAJOR) 3
	mknod -m 0666 /dev/backerphr c $(BKR_MAJOR) 4
	mknod -m 0666 /dev/backerphs c $(BKR_MAJOR) 5
	mknod -m 0666 /dev/backerplr c $(BKR_MAJOR) 6
	mknod -m 0666 /dev/backerpls c $(BKR_MAJOR) 7

uninstall:
	rm -f $(MODULES_DIR)/backer.o
	rm -f $(PREFIX)/man/man4/backer.4
	depmod -a
	rm -f /dev/backernhe
	rm -f /dev/backernhr
	rm -f /dev/backernhs
	rm -f /dev/backernle
	rm -f /dev/backernlr
	rm -f /dev/backernls
	rm -f /dev/backerphe
	rm -f /dev/backerphr
	rm -f /dev/backerphs
	rm -f /dev/backerple
	rm -f /dev/backerplr
	rm -f /dev/backerpls

load:
	insmod backer.o $(INSMODFLAGS)
	cat /proc/devices

unload:
	rmmod backer
	cat /proc/devices

clean:
	rm -f backer_isa.o backer_fmt.o rs.o

realclean: clean
	rm -f backer.o
