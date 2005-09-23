#
# Some things to set for your system
#
#    BKR_MAJOR             : the major number for the /dev files
#    DEV_DIR               : the /dev directory on your system
#    MAN_DIR               : the man pages go in $(MAN_DIR)/man4/
#    MODULES_DIR           : install directory for the driver
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
CONFIG_BACKER_TIMEOUT = 15


#
# Here we go...
#
# The targets you should know about are:
#
#   all: (default)
#	build the driver, backer.o.
#   install: 
#	copy the driver modules to MODULES_DIR, run depmod -a, generate
#	device inodes if devfs is known to not be in use, put man pages in
#	$(PREFIX)/man/man4.
#   devs:
#	force creation of device inodes.
#   uninstall:
#	unload the driver from the kernel, delete the driver modules from
#	MODULES_DIR, run depmod -a, delete the /dev entries if they exist,
#	delete the man pages from $(PREFIX)/man/man4.
#   clean:
#	Delete all intermediate files from the build directory.
#   realclean:
#	Same as clean plus all build targets are also deleted.
#

CC = gcc
INCLUDE_DIR = /usr/include
CONFIGFLAGS = -DBKR_MAJOR=$(BKR_MAJOR) \
              -DCONFIG_BACKER_TIMEOUT=$(CONFIG_BACKER_TIMEOUT)
KERNELFLAGS  = -D__KERNEL__ -DMODULE
KERNELFLAGS += -DMODVERSIONS -include $(INCLUDE_DIR)/linux/modversions.h
CFLAGS = -c -O3 -Wall -Wstrict-prototypes -I$(INCLUDE_DIR) $(KERNELFLAGS) $(CONFIGFLAGS)
LDFLAGS = -r
MODULES =  backer.o backer_isa.o backer_parport.o
MANPAGES = backer.4 backer_isa.4 backer_parport.4
DEVNAMES = nhe nhr nhs nle nlr nls phe phr phs ple plr pls

.PHONY : all clean devs install load realclean uninstall unload

all : $(MODULES)

backer.o : backer_main.o backer_fmt.o rs.o
	$(LD) $(LDFLAGS) -o $@ $^
backer_main.c : backer.h backer_device.h backer_unit.h backer_fmt.h
backer_fmt.c : backer.h backer_device.h backer_fmt.h rs.h
rs.c : rs.h

backer_isa.o : backer.h backer_device.h backer_unit.h

backer_parport.o : backer.h backer_device.h backer_unit.h

uninstall :
	@echo "Uninstalling..."
	if grep backer_isa     -q /proc/modules; then rmmod backer_isa;     fi
	if grep backer_parport -q /proc/modules; then rmmod backer_parport; fi
	if grep backer         -q /proc/modules; then rmmod backer;         fi
	-rm -f $(MODULES:%=$(MODULES_DIR)/%)
	-rm -f $(MANPAGES:%=$(MAN_DIR)/man4/%.gz)
	depmod -a
	if [ -d $(DEV_DIR)/backer ]; then rm -Rf $(DEV_DIR)/backer; fi
	@echo "Done."

install : all
	@echo "Installing..."
	install -d -m 0700 $(MODULES_DIR)
	install -m 0600 $(MODULES) $(MODULES_DIR)
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
	install -m 0644 $(MANPAGES:%=DOCUMENTATION/%) $(MAN_DIR)/man4
	gzip $(MANPAGES:%=$(MAN_DIR)/man4/%)
	@echo "Done."

devs :
	@if [ -d $(DEV_DIR)/backer/0 ]; then \
	   echo "ERROR:  Device files appear to have already been created."; \
	   exit 1; \
	fi
	mkdir -m 0755 -p $(DEV_DIR)/backer/0
	DEVPATHS=($(DEVNAMES:%=$(DEV_DIR)/backer/0/%)); \
	for minor in 0 1 2 3 4 5 6 7 8 9 10 11; do \
	   mknod -m 0666 $${DEVPATHS[$${minor}]} c $(BKR_MAJOR) $${minor}; \
	done

clean :
	rm -f backer_main.o backer_fmt.o rs.o

realclean : clean
	rm -f $(MODULES)
