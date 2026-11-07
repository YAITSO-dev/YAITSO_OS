CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: ybox

ybox: ybox.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ybox

.PHONY: all clean
