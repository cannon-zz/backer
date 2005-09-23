@SET_MAKE@

# This is two Makefiles in one.  This stuff right here is a 2.6.x kernel
# module Makefile with the information needed to get the kernel build
# system to compile these modules.

EXTRA_CFLAGS := -Wno-unused-function -I$(src) -I$(src)/../codecs -DBKR_VERSION=\"3.103\" -DBKR_MAJOR=60

obj-m += backer.o backer_isa.o backer_parport.o

backer-objs := bkr_main.o bkr_stream.o bkr_ring_buffer.o rs.o bkr_ecc2.o \
               bkr_splp.o bkr_splp_randomize.o bkr_gcr.o bkr_frame.o

backer_isa-objs := bkr_isa.o

backer_parport-objs := bkr_parport.o


# The stuff inside the ifeq() is the non-kernel part of the Makefile,
# except for modules, modules_install clean which are used to initiate the
# kernel build system for those targets.

ifeq ($(KERNELRELEASE),)

modules modules_install clean :
	$(MAKE) -C @KERNEL_SOURCE@ SUBDIRS=$(CURDIR) $@

prefix = @prefix@
exec_prefx = @exec_prefix@
mandir = @mandir@
modulesdir = @MODULES_DIR@/misc
DESTDIR =

DEVNAMES = nhe nhr nhs nle nlr nls phe phr phs ple plr pls

TARGETS = $(obj-m:%.o=%.ko)
MANPAGES = $(obj-m:%.o=%.4)

install : modules_install
	@INSTALL@ -d -m 0755 $(DESTDIR)$(mandir)/man4 && @INSTALL@ -m 0644 $(MANPAGES:%=DOCUMENTATION/%) $(DESTDIR)$(mandir)/man4 && @GZIP@ $(MANPAGES:%=$(DESTDIR)$(mandir)/man4/%)
	@echo -e "Done.\n"

install-devs :
	for unit in 0 1 2 ; do \
	   if [ -d $(DESTDIR)/dev/backer/$${unit} ] ; then \
	      echo "Device inodes for Backer unit $${unit} already exist: skipping" ; \
	      continue ; \
	   fi ; \
	   @INSTALL@ -d -m 0755 $(DESTDIR)/dev/backer/$${unit} ; \
	   DEVPATHS=($(DEVNAMES:%=$(DESTDIR)/dev/backer/$${unit}/%)) ; \
	   for minor in 0 1 2 3 4 5 6 7 8 9 10 11 ; do \
	      mknod -m 0666 $${DEVPATHS[$${minor}]} c @BKR_MAJOR@ $$((12*$${unit}+$${minor})) ; \
	   done ; \
	done

uninstall :
	@echo -e "\nUninstalling device drivers..."
	-rmmod backer_isa backer_parport backer
	-rm -f $(TARGETS:%=$(modulesdir)/%)
	-rm -f $(MANPAGES:%=$(mandir)/man4/%.gz)
	-[ -d /dev/backer ] && rm -Rf /dev/backer
	-[ -L /dev/tape ] && @READLINK@ /dev/tape | @GREP@ backer >/dev/null && rm -f /dev/tape
	@echo -e "Done.\n"

distclean : clean
	rm -f Makefile

realclean : distclean

endif	# $(KERNELRELEASE)
