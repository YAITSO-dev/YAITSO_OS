CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2

all: install

install: install.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f install

.PHONY: all clean
