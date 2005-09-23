#
# Some things to set for your system
#
#    BKR_MAJOR : the major number for the /dev files.
#    MODULES_DIR : install directory for the driver (no trailing slash).
#    PREFIX : prefix for the install hierarchy (no trailing slash).
#
# Major numbers that your system is probably not using (because they are
# for other kinds of tape devices) and so can probably be used with this
# driver:
#	9: SCSI tape devices
#	12: QIC-02 tape devices
#	27: QIC-117 tape devices
#	37: IDE tape devices
#	96: parallel port ATAPI tape devices
#	206: OnStream SC-x0 tape devices
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
#	delete backer.o from MODULES_DIR, run depmod -a, delete the /dev
#	entries if they exist, delete backer.4 from $(PREFIX)/man/man4.
#   clean:
#	Delete all intermediate files from the build directory.
#   realclean:
#	Same as clean plus all build targets are also deleted.
#

INCLUDE_DIR = /usr/include
CFLAGS = -O3 -Wall -Wstrict-prototypes -D__KERNEL__ -DMODULE -DMODVERSIONS -include $(INCLUDE_DIR)/linux/modversions.h -DBKR_MAJOR=$(BKR_MAJOR) -I$(INCLUDE_DIR)
LDFLAGS = -r
INSMODFLAGS = dma=1

VER = $(shell awk -F\" '/REL/ {print $$2}' $(INCLUDE_DIR)/linux/version.h)

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
	install -d -m 0700 $(MODULES_DIR)
	install -m 0600 backer.o $(MODULES_DIR)
	depmod -a
	@if [ -f /proc/mounts ]; then \
	   if grep devfs -q /proc/mounts; then \
	      echo -e "\nATTENTION:  devfs in use:  device inode creation not required.\n"; \
	   else \
	      echo -e "\nATTENTION:  devfs not in use:  creating device inodes...\n"; \
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
	mkdir -m 0755 -p /dev/backer/0
	mknod -m 0666 /dev/backer/0/nhr c $(BKR_MAJOR) 1
	mknod -m 0666 /dev/backer/0/nhs c $(BKR_MAJOR) 2
	mknod -m 0666 /dev/backer/0/nlr c $(BKR_MAJOR) 4
	mknod -m 0666 /dev/backer/0/nls c $(BKR_MAJOR) 5
	mknod -m 0666 /dev/backer/0/phr c $(BKR_MAJOR) 7
	mknod -m 0666 /dev/backer/0/phs c $(BKR_MAJOR) 8
	mknod -m 0666 /dev/backer/0/plr c $(BKR_MAJOR) 10
	mknod -m 0666 /dev/backer/0/pls c $(BKR_MAJOR) 11

uninstall:
	-rmmod backer
	rm -f $(MODULES_DIR)/backer.o
	rm -f $(PREFIX)/man/man4/backer.4
	depmod -a
	rm -Rf /dev/backer

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
