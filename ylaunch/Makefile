CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE -I../libywt \
           $(shell pkg-config --cflags wayland-client xkbcommon freetype2)
LIBS     = -L../libywt -lywt \
           $(shell pkg-config --libs wayland-client xkbcommon freetype2) -lm

SRC = ylaunch.c desktop.c
OBJ = $(SRC:.c=.o)

all: ylaunch

ylaunch: $(OBJ) ../libywt/libywt.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c ylaunch.h
	$(CC) $(CFLAGS) -c -o $@ $<

../libywt/libywt.a:
	$(MAKE) -C ../libywt

clean:
	rm -f $(OBJ) ylaunch

.PHONY: all clean
