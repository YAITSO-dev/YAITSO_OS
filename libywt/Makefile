CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE \
           $(shell pkg-config --cflags wayland-client xkbcommon freetype2)

SRC = ywt-ctx.c ywt-shm.c ywt-surface.c ywt-input.c ywt-font.c ywt-draw.c xdg-shell-client-protocol.c
OBJ = $(SRC:.c=.o)

all: libywt.a

libywt.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c libywt.h ywt-internal.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) libywt.a

.PHONY: all clean
