#
# Some things to set for your system
#
#    BKR_MAJOR             : the major number for the /dev files
#    DEV_DIR               : the /dev directory on your system
#    MAN_DIR               : the man page goes in $(MAN_DIR)/man4/
#    MODULES_DIR           : install directory for the driver
#    CONFIG_BACKER_IOPORT  : Backer's I/O port
#    CONFIG_BACKER_DMA     : Backer's DMA channel
#    CONFIG_BACKER_TIMEOUT : operation timeout in seconds.
#
# Major numbers that your system is probably not using (because they are
# for other kinds of tape devices) and so can probably be used with this
# driver if you don't want to use 60:
#	9: SCSI tape devices
#	12: QIC-02 tape devices
#	27: QIC-117 tape devices
#	37: IDE tape devices
#	96: parallel port ATAPI tape devices
#	206: OnStream SC-x0 tape devices
#

BKR_MAJOR             = 60
DEV_DIR               = /dev
MAN_DIR               = /usr/man
MODULES_DIR           = /lib/modules/misc
CONFIG_BACKER_IOPORT  = 0x300
CONFIG_BACKER_DMA     = 3
CONFIG_BACKER_TIMEOUT = 10


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

CC = gcc
INCLUDE_DIR = /usr/include
CONFIGFLAGS = -DBKR_MAJOR=$(BKR_MAJOR) \
              -DCONFIG_BACKER_IOPORT=$(CONFIG_BACKER_IOPORT) \
              -DCONFIG_BACKER_DMA=$(CONFIG_BACKER_DMA) \
              -DCONFIG_BACKER_TIMEOUT=$(CONFIG_BACKER_TIMEOUT)
KERNELFLAGS  = -D__KERNEL__ -DMODULE
KERNELFLAGS += -DMODVERSIONS -include $(INCLUDE_DIR)/linux/modversions.h
CFLAGS = -O3 -Wall -Wstrict-prototypes -I$(INCLUDE_DIR) $(KERNELFLAGS) $(CONFIGFLAGS)
LDFLAGS = -r
INSMODFLAGS = dma=1

.PHONY : all install devs uninstall clean realclean load unload

all : backer.o

backer.o : backer_isa.o backer_fmt.o rs.o
	$(LD) $(LDFLAGS) -o $@ $^
backer_isa.o : backer_isa.c backer.h backer_device.h backer_fmt.h
backer_fmt.o : backer_fmt.c backer.h backer_device.h backer_fmt.h rs.h
rs.o : rs.c rs.h

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
	install -d -m 0755 $(MAN_DIR)/man4
	install -m 0644 backer.4 $(MAN_DIR)/man4
	@echo "Done."

devs:
	@if [ -d $(DEV_DIR)/backer/0 ]; then \
	   echo "ERROR:  Device files appear to have already been created." ;\
	   exit 1; \
	fi
	mkdir -m 0755 -p $(DEV_DIR)/backer/0
	mknod -m 0666 $(DEV_DIR)/backer/0/nhe c $(BKR_MAJOR) 0
	mknod -m 0666 $(DEV_DIR)/backer/0/nhr c $(BKR_MAJOR) 1
	mknod -m 0666 $(DEV_DIR)/backer/0/nhs c $(BKR_MAJOR) 2
	mknod -m 0666 $(DEV_DIR)/backer/0/nle c $(BKR_MAJOR) 3
	mknod -m 0666 $(DEV_DIR)/backer/0/nlr c $(BKR_MAJOR) 4
	mknod -m 0666 $(DEV_DIR)/backer/0/nls c $(BKR_MAJOR) 5
	mknod -m 0666 $(DEV_DIR)/backer/0/phe c $(BKR_MAJOR) 6
	mknod -m 0666 $(DEV_DIR)/backer/0/phr c $(BKR_MAJOR) 7
	mknod -m 0666 $(DEV_DIR)/backer/0/phs c $(BKR_MAJOR) 8
	mknod -m 0666 $(DEV_DIR)/backer/0/ple c $(BKR_MAJOR) 9
	mknod -m 0666 $(DEV_DIR)/backer/0/plr c $(BKR_MAJOR) 10
	mknod -m 0666 $(DEV_DIR)/backer/0/pls c $(BKR_MAJOR) 11

uninstall:
	-rmmod backer
	rm -f $(MODULES_DIR)/backer.o
	rm -f $(MAN_DIR)/man4/backer.4
	depmod -a
	if [ -d $(DEV_DIR)/backer ]; then rm -Rf $(DEV_DIR)/backer; fi

clean:
	rm -f backer_isa.o backer_fmt.o rs.o

realclean: clean
	rm -f backer.o

load:
	/sbin/insmod backer.o $(INSMODFLAGS)
	cat /proc/devices

unload:
	/sbin/rmmod backer
	cat /proc/devices
