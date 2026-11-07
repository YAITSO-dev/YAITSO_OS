CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: init

init: init.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f init

.PHONY: all clean
