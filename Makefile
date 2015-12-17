CC=gcc
CFLAGS=-I.
OBJ=screenshot_capture.o 

LIBS=-lavfilter -lavutil -lswscale -lavresample -lavcodec -lavformat -lx264 -lz -lfreetype -lfdk-aac -lbz2 -lfontconfig -lpng

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    EXTRALIBS=-lm
endif

ifeq ($(UNAME_S),Darwin)
    EXTRALIBS=-framework CoreFoundation -framework VideoDecodeAcceleration -framework CoreVideo
endif

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

screenshot_capture: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LIBS) $(EXTRALIBS)

.PHONY: clean

clean:
	rm -f *.o *~ screenshot_capture
