CC=gcc
LD=gcc
CFLAGS=-I.
OBJ=screenshot_capture.o 

LIBS=$(shell pkg-config --libs libavformat libavcodec libavfilter libavutil libswscale libavresample)

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

screenshot_capture: $(OBJ)
	$(LD) $^ $(CFLAGS) $(LIBS) $(EXTRALIBS) -o $@

.PHONY: clean

clean:
	rm -f *.o *~ screenshot_capture
