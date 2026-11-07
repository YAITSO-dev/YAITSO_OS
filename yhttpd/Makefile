CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: yhttpd

yhttpd: yhttpd.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f yhttpd

.PHONY: all clean
