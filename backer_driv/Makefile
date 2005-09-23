#
# Some things to set for your system
#
#    BKR_MAJOR : the major number for the /dev files
#    BKR_MINOR : the first minor number to use
#

BKR_MAJOR   = 60

#
# Here we go...
#

INSMODFLAGS = dma=1

INCLUDEDIR = /usr/include
CC = gcc
CFLAGS = -D__KERNEL__ -DMODULE -DBKR_MAJOR=$(BKR_MAJOR) -O2 -Wall -Wstrict-prototypes -I$(INCLUDEDIR)
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
	install -d -m 0755 /lib/modules/$(VER)/misc
	install -c -m 0644 backer.o /lib/modules/$(VER)/misc

devs:
	mknod -m 600 /dev/backerfsln c $(BKR_MAJOR)  0
	mknod -m 600 /dev/backerfslp c $(BKR_MAJOR)  1
	mknod -m 600 /dev/backerfshn c $(BKR_MAJOR)  2
	mknod -m 600 /dev/backerfshp c $(BKR_MAJOR)  3
	mknod -m 600 /dev/backerfeln c $(BKR_MAJOR)  4
	mknod -m 600 /dev/backerfelp c $(BKR_MAJOR)  5
	mknod -m 600 /dev/backerfehn c $(BKR_MAJOR)  6
	mknod -m 600 /dev/backerfehp c $(BKR_MAJOR)  7
	mknod -m 600 /dev/backerrln  c $(BKR_MAJOR)  8
	mknod -m 600 /dev/backerrlp  c $(BKR_MAJOR)  9
	mknod -m 600 /dev/backerrhn  c $(BKR_MAJOR) 10
	mknod -m 600 /dev/backerrhp  c $(BKR_MAJOR) 11

uninstall:
	rm -f /lib/modules/$(VER)/misc/backer.o
	rm -f /dev/backerfsln
	rm -f /dev/backerfslp
	rm -f /dev/backerfshn
	rm -f /dev/backerfshp
	rm -f /dev/backerfeln
	rm -f /dev/backerfelp
	rm -f /dev/backerfehn
	rm -f /dev/backerfehp
	rm -f /dev/backerrln
	rm -f /dev/backerrlp
	rm -f /dev/backerrhn
	rm -f /dev/backerrhp

load:
	insmod backer $(INSMODFLAGS)
	cat /proc/devices

unload:
	rmmod backer
	cat /proc/devices

clean:
	rm -f *.o
