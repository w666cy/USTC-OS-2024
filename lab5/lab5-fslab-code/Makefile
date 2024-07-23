CFLAGS=$(shell pkg-config fuse3 --cflags) -Wall -std=gnu11 -Wno-unused-variable
LDFLAGS=$(shell pkg-config fuse3 --libs)
LDLIBS=

CC=gcc

.PHONY: clean debug static

all: simple_fat16

debug: CFLAGS += -g
debug: simple_fat16

static: CFLAGS += -static
static: simple_fat16

simple_fat16: simple_fat16.o fat16_fixed.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

fat16_fixed.o: fat16_fixed.c fat16.h
	$(CC) $(CFLAGS) -c -o $@ $<

simple_fat16.o: simple_fat16.c fat16.h fat16_utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

hello: hello.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

hello.o: hello.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f simple_fat16 hello *.o


