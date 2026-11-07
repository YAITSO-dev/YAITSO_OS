CC      ?= gcc
CFLAGS  ?= -Wall -Wextra   -std=c11 -O2 -D_GNU_SOURCE -I../libywt \
           $(shell pkg-config --cflags wayland-client xkbcommon freetype2)
LIBS     = -L../libywt -lywt \
           $(shell pkg-config --libs wayland-client xkbcommon freetype2) \
           -lutil -lm

SRC = yterm.c pty.c vt.c render.c tabs.c search.c font.c scroll.c
OBJ = $(SRC:.c=.o)

all: yterm

yterm: $(OBJ) ../libywt/libywt.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c yterm.h
	$(CC) $(CFLAGS) -c -o $@ $<

../libywt/libywt.a:
	$(MAKE) -C ../libywt

clean:
	rm -f $(OBJ) yterm

.PHONY: all clean
