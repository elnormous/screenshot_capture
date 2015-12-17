CC=gcc
CFLAGS=-I.
OBJ=screenshot_capture.o 

LIBS=-lavfilter -lavutil -lswscale -lavresample -lavcodec -lavformat -lx264 -lz -lfreetype -lfdk-aac -lbz2 -lfontconfig -lpng -lpthread

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    EXTRALIBS=-lm
endif

ifeq ($(UNAME_S),Darwin)
    EXTRALIBS=-framework CoreFoundation -framework VideoDecodeAcceleration -framework CoreVideo
endif

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

screenshot_capture: $(OBJ)
	$(CC) $^ $(CFLAGS) $(LIBS) $(EXTRALIBS) -o $@

.PHONY: clean

clean:
	rm -f *.o *~ screenshot_capture
