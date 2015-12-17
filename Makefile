CC=gcc
CFLAGS=-I.
OBJ=screenshot_capture.o 

LIBS=-lavfilter -lavutil -lswscale -lavresample -lavcodec -lavformat -lx264 -lz -lfreetype -lfdk-aac -lbz2 -lfontconfig -lpng

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

screenshot_capture: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)