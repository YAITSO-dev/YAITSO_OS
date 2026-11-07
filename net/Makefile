CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE

all: ywifi

ywifi: ywifi.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ywifi

.PHONY: all clean
