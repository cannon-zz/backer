BACKER_DIR = ../backer/trunk
CODEC_DIR = $(BACKER_DIR)/codecs
DRIVER_DIR = $(BACKER_DIR)/drivers
UTIL_DIR = $(BACKER_DIR)/utilities

TARGETS = bkranalyze gcr_encode maketables nonblock
SOURCES = bkranalyze.c gcr_encode.c maketables.c nonblock.c \
          $(UTIL_DIR)/bkr_disp_mode.c \
          $(UTIL_DIR)/bkr_proc_io.c \
          $(UTIL_DIR)/bkr_screen.c \
          $(DRIVER_DIR)/bkr_ring_buffer.c \
	  $(DRIVER_DIR)/bkr_stream.c \
          $(CODEC_DIR)/bkr_frame.c \
          $(CODEC_DIR)/bkr_gcr.c \
          $(CODEC_DIR)/bkr_splp.c \
	  $(CODEC_DIR)/bkr_splp_randomize.c \
          $(CODEC_DIR)/rs.c

CC      = gcc
CFLAGS  = -O3 -Wall -Wstrict-prototypes -Wno-unused-function
CFLAGS += -I$(CODEC_DIR) -I$(DRIVER_DIR) -I$(UTIL_DIR) $(shell pkg-config --cflags gtk+-2.0 gthread-2.0)
LDFLAGS = -lpthread $(shell pkg-config --libs gtk+-2.0 gthread-2.0)

.PHONY : all clean realclean

all : $(TARGETS)


bkranalyze : bkranalyze.o bkr_ring_buffer.o bkr_stream.o \
	bkr_frame.o bkr_screen.o


gcr_encode : gcr_encode.o


maketables : maketables.o


nonblock : nonblock.o

include depend

depend :
	$(CC) $(CFLAGS) -MM $(SOURCES) | sed -e ": repeat" -e "s:$(DRIVER_DIR):\$$(DRIVER_DIR):" -e "s:$(UTIL_DIR):\$$(UTIL_DIR):" -e "t repeat" >depend


clean:
	rm -f *.o *.i *.s core

realclean: clean
	rm -f $(TARGETS)
