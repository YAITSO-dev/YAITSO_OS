CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: svc

svc: svc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f svc

.PHONY: all clean
