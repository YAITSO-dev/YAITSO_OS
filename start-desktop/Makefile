# start-desktop собирается из start-desktop.c (статически, как в образе).
# start-desktop.sh — старый вариант, не устанавливается.
PREFIX ?= /usr
CC     ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2

all: start-desktop

start-desktop: start-desktop.c
	$(CC) $(CFLAGS) -static -o $@ $<

install: all
	install -Dm755 start-desktop $(DESTDIR)$(PREFIX)/bin/start-desktop

clean:
	rm -f start-desktop

.PHONY: all install clean
