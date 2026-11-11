CC      ?= gcc
CFLAGS  ?= -I. -DWLR_USE_UNSTABLE -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE \
           $(shell pkg-config --cflags wlroots-0.18 wayland-server xkbcommon)
LIBS     = $(shell pkg-config --libs wlroots-0.18 wayland-server xkbcommon) -lm

SRC = ycomp.c server.c output.c view.c input.c evdev.c layer.c decorations.c config.c ipc.c
OBJ = $(SRC:.c=.o)

all: ycomp

ycomp: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c ycomp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) ycomp

.PHONY: all clean
