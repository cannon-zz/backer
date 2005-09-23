INCLUDEDIR = /usr/include

INSMODFLAGS = dma=1

CC = gcc
CFLAGS = -D__KERNEL__ -DMODULE -O2 -Wall -Wstrict-prototypes -I$(INCLUDEDIR)
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

uninstall:
	rm -f /lib/modules/$(VER)/misc/backer.o

load:
	insmod backer $(INSMODFLAGS)
	cat /proc/devices

unload:
	rmmod backer
	cat /proc/devices

clean:
	rm -f *.o
