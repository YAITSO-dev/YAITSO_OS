CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
LDLIBS  ?= -lcrypt

all: install

# Установщик работает в минимальном live-rootfs, поэтому статическая сборка
# надёжнее: не зависит от того, какие библиотеки попали в образ.
install: install.c
	$(CC) $(CFLAGS) -static -o $@ $< $(LDLIBS)

clean:
	rm -f install

.PHONY: all clean
