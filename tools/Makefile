CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: yglibc

yglibc: yglibc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f yglibc

install: yglibc
	install -Dm755 yglibc $(DESTDIR)/usr/bin/yglibc

.PHONY: all clean install
