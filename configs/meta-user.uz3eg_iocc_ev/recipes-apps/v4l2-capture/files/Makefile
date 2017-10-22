CC = $(CROSS_COMPILE)gcc

CFLAGS=-Wall -O0 -g -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-label
CFLAGS+=-I$(AUXLIBS_INSTALL)/include
LIBS=-L$(LD_LIBRARY_PATH) -ljpeg

all:
	$(CC) -o $(OUTDIR)/camtest v4l2grab.c  $(CFLAGS) $(LIBS) $(INCLUDE)
clean:
	rm -f camtest
	rm -f *~
	rm -f $(OUTDIR)/camtest

